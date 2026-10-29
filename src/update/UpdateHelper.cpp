#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include "UpdateCore.h"
#include <filesystem>

using namespace ditto_update;
namespace fs = std::filesystem;
namespace {
std::shared_ptr<void> Handle(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) throw Error("Cannot open the running Ditto process.");
    return {h, [](void* value) { CloseHandle(value); }};
}
bool SamePath(const std::wstring& a, const std::wstring& b) { return _wcsicmp(fs::path(a).lexically_normal().c_str(), fs::path(b).lexically_normal().c_str()) == 0; }
void Restart(const std::wstring& executable) {
    std::wstring command = L"\"" + executable + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{}; auto directory = fs::path(executable).parent_path().wstring();
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &process)) throw Error("Ditto could not be restarted. Open it from the Start menu.");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
void CopyLocked(const std::wstring& source, const std::wstring& target) {
    auto lock = LockFile(source);
    if (!CopyFileW(FileApiPath(source).c_str(), FileApiPath(target).c_str(), TRUE)) throw Error("Backup could not copy a file. Installation was not started.");
    // Confirm the bytes in the backup before allowing any installation.
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(lock.get(), &size)) throw Error("Backup could not read a file size.");
    auto copy = LockFile(target); LARGE_INTEGER copied{};
    if (!GetFileSizeEx(copy.get(), &copied) || size.QuadPart != copied.QuadPart) throw Error("Backup verification failed.");
    unsigned char left[65536], right[65536];
    for (;;) {
        DWORD a = 0, b = 0;
        if (!ReadFile(lock.get(), left, sizeof(left), &a, nullptr) || !ReadFile(copy.get(), right, sizeof(right), &b, nullptr) || a != b || memcmp(left, right, a)) throw Error("Backup verification failed.");
        if (!a) break;
    }
}
void ExportSettings(const std::wstring& directory) {
    wchar_t system[MAX_PATH]; if (!GetSystemDirectoryW(system, MAX_PATH)) throw Error("Cannot locate Windows registry export.");
    std::wstring reg = std::wstring(system) + L"\\reg.exe";
    std::wstring command = L"\"" + reg + L"\" export HKCU\\Software\\Ditto \"" + directory + L"\\Ditto-settings.reg\" /y";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(reg.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process)) throw Error("Cannot back up Ditto settings.");
    auto thread = Handle(process.hThread); auto child = Handle(process.hProcess);
    WaitForSingleObject(child.get(), INFINITE); DWORD code = 1;
    if (!GetExitCodeProcess(child.get(), &code) || code) throw Error("Registry backup failed. Installation was not started.");
}
std::wstring StartupCommand() {
    wchar_t buffer[32768]; DWORD bytes = sizeof(buffer);
    auto result = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Ditto", RRF_RT_REG_SZ, nullptr, buffer, &bytes);
    if (result == ERROR_FILE_NOT_FOUND) return {};
    if (result != ERROR_SUCCESS) throw Error("Cannot read Ditto startup registration.");
    return buffer;
}
void Backup(const std::wstring& application, const std::wstring& database,
    const std::wstring& directory, const std::wstring& startup) {
    RejectReparsePath(application); RejectReparsePath(database);
    if (database.rfind(L"\\\\", 0) == 0) throw Error("Automatic updates cannot back up a database on a network share.");
    auto appCopy = directory + L"\\application"; EnsurePrivateDirectory(appCopy);
    auto dataCopy = directory + L"\\database"; EnsurePrivateDirectory(dataCopy);
    std::vector<fs::path> files;
    uint64_t bytes = 64 * 1024 * 1024;
    for (const auto& entry : fs::recursive_directory_iterator(application)) {
        RejectReparsePath(entry.path().wstring());
        if (entry.is_regular_file()) { files.push_back(entry.path()); bytes += entry.file_size(); }
    }
    // Acquire every database/WAL handle before copying. This refuses an active
    // writer (including another Ditto instance) instead of copying a live DB.
    std::vector<std::pair<std::wstring, std::shared_ptr<void>>> databaseLocks;
    for (const auto* suffix : {L"", L"-wal", L"-shm"}) {
        auto path = database + suffix;
        if (*suffix && !fs::exists(path)) continue;
        auto lock = LockFile(path); LARGE_INTEGER size{};
        if (!GetFileSizeEx(lock.get(), &size)) throw Error("Cannot inspect database backup size.");
        bytes += static_cast<uint64_t>(size.QuadPart);
        databaseLocks.emplace_back(path, lock);
    }
    ULARGE_INTEGER free{};
    if (!GetDiskFreeSpaceExW(directory.c_str(), &free, nullptr, nullptr) || free.QuadPart < bytes) throw Error("Not enough space for a complete backup. Installation was not started.");
    for (const auto& path : files) {
        auto relative = path.lexically_relative(application);
        auto target = fs::path(appCopy) / relative;
        fs::create_directories(target.parent_path());
        CopyLocked(path.wstring(), target.wstring());
    }
    for (const auto& entry : databaseLocks) CopyLocked(entry.first, (fs::path(dataCopy) / fs::path(entry.first).filename()).wstring());
    ExportSettings(directory);
    WriteNewFile(directory + L"\\backup.json", "{\"application\":" + JsonQuote(application) + ",\"database\":" + JsonQuote(database) + ",\"startupCommand\":" + JsonQuote(startup) + "}");
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::wstring executable, backup;
    bool stopped = false;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    try {
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) throw Error("Cannot check update helper identity.");
        auto token = Handle(rawToken); TOKEN_ELEVATION elevation{}; DWORD bytes = 0;
        if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &bytes) || elevation.TokenIsElevated) throw Error("Start updates from Ditto running as your normal user, not as administrator.");
        int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) throw Error("Invalid update handoff.");
        std::shared_ptr<void> arguments(argv, [](void* p) { LocalFree(p); });
        if (argc != 3 || std::wstring(argv[1]) != L"--install") throw Error("This helper is started by Ditto's Install now button.");
        fs::path ticketPath(argv[2]);
        if (ticketPath.filename() != L"install.json" || ticketPath.parent_path().filename().wstring().rfind(L"install-", 0) != 0
            || !SamePath(ticketPath.parent_path().parent_path().wstring(), PrivateCacheRoot())) throw Error("Invalid install handoff location.");
        auto ticket = ParseJson(ConsumeInstallTicket(argv[2]));
        if (ticket.At("schemaVersion").Number() != 3) throw Error("Unsupported update handoff.");
        auto consent = ticket.At("unsignedConsent").Number();
        if (consent > 1) throw Error("Invalid unsigned-install consent.");
        bool unsignedConsent = consent == 1;
        executable = Wide(ticket.At("executable").Text());
        auto application = fs::path(executable).parent_path().wstring();
        if (!SamePath(executable, (fs::path(RegisteredInstallDirectory()) / L"Ditto.exe").wstring())) throw Error("The running Ditto does not match the registered installation.");
        auto database = Wide(ticket.At("database").Text());
        auto installerPath = Wide(ticket.At("installer").Text());
        auto channel = ParseChannel(ticket.At("channel").Text());
        auto manifest = ParseManifest(ReadSmallFile(Wide(ticket.At("manifest").Text())), channel == Channel::Beta);
        if (!manifest.hasRelease || manifest.release.channel != channel) throw Error("The update channel does not match the approved release.");
        if (!manifest.hasRelease || !(FileVersion(executable) < manifest.release.version)) throw Error("The update is not newer than the installed version.");
        if (manifest.release.minimumWindowsBuild > WindowsBuild()) throw Error("The update needs a newer Windows build.");
        auto pid = ticket.At("parentPid").Number();
        if (!pid || pid > MAXDWORD) throw Error("Invalid Ditto process identity.");
        auto parent = Handle(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid)));
        wchar_t parentPath[32768]; DWORD pathSize = 32768;
        if (!QueryFullProcessImageNameW(parent.get(), 0, parentPath, &pathSize) || !SamePath(parentPath, executable)) throw Error("The original Ditto process is no longer available.");
        auto window = reinterpret_cast<HWND>(static_cast<uintptr_t>(ticket.At("mainWindow").Number()));
        DWORD windowPid = 0; GetWindowThreadProcessId(window, &windowPid);
        if (windowPid != pid) throw Error("Ditto's window identity changed.");
        // Keep this handle through UAC, setup and process exit. It denies writes
        // and replacement of the exact file whose hash/signature were verified.
        auto installer = LockFile(installerPath); std::atomic_bool cancel(false);
        VerifyHash(installer.get(), manifest.release, cancel);
        VerifyPublisher(installer.get(), installerPath, manifest.release.version, L"Ditto ARM64", unsignedConsent);
        {
            auto app = LockFile(executable);
            VerifyPublisher(app.get(), executable, FileVersion(executable), L"Ditto ARM64", unsignedConsent);
        }
        auto startup = StartupCommand();
        backup = PrivateCacheRoot() + L"\\backup-" + UniqueName(); EnsurePrivateDirectory(backup);
        if (!PostMessageW(window, WM_CLOSE, 0, 0)) throw Error("Ditto could not be closed cleanly.");
        if (WaitForSingleObject(parent.get(), 30000) != WAIT_OBJECT_0) throw Error("Ditto is still open. Save your work and try the update again.");
        stopped = true;
        Backup(application, database, backup, startup);
        std::wstring parameters = L"/SP- /SILENT /NORESTART /RESTARTEXITCODE=3010 /NOCLOSEAPPLICATIONS /NORESTARTAPPLICATIONS /DIR=\"" + application + L"\" ";
        parameters += startup.empty() ? L"/TASKS=\"\"" : L"/TASKS=RunAtStartup";
        SHELLEXECUTEINFOW launch{}; launch.cbSize = sizeof(launch);
        launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        launch.lpVerb = L"runas"; launch.lpFile = installerPath.c_str(); launch.lpParameters = parameters.c_str();
        // The installer displays progress and Windows owns the elevation prompt.
        launch.nShow = SW_SHOWNORMAL;
        std::wstring installerDirectory = fs::path(installerPath).parent_path().wstring(); launch.lpDirectory = installerDirectory.c_str();
        if (!ShellExecuteExW(&launch)) {
            if (GetLastError() == ERROR_CANCELLED) throw Error("Installation permission was cancelled. Your existing Ditto will restart.");
            throw Error("Windows could not launch the verified installer.");
        }
        auto setup = Handle(launch.hProcess);
        WaitForSingleObject(setup.get(), INFINITE); DWORD code = 1;
        if (!GetExitCodeProcess(setup.get(), &code) || (code != 0 && code != 3010)) throw Error("Installer did not complete successfully (exit " + std::to_string(code) + "). The backup has been retained.");
        {
            auto app = LockFile(executable);
            VerifyPublisher(app.get(), executable, manifest.release.version, L"Ditto ARM64", unsignedConsent);
        }
        Restart(executable); stopped = false;
        if (code == 3010) MessageBoxW(nullptr, L"Ditto was updated. Windows reports that a restart is needed to finish replacing some files. No restart will happen automatically.", L"Ditto ARM64 update", MB_OK | MB_ICONINFORMATION);
        CoUninitialize(); return 0;
    } catch (const std::exception& error) {
        std::wstring message = Wide(error.what());
        if (stopped) {
            try { Restart(executable); } catch (const std::exception& restartError) { message += L"\n\n" + Wide(restartError.what()); }
        }
        if (!backup.empty()) message += L"\n\nBackup location: " + backup;
        MessageBoxW(nullptr, message.c_str(), L"Ditto ARM64 update", MB_OK | MB_ICONEXCLAMATION);
        CoUninitialize(); return 1;
    }
}
