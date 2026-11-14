#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include "UpdateInstall.h"
#include <filesystem>
#include <cstdio>

using namespace ditto_update;
namespace fs = std::filesystem;
namespace {
std::shared_ptr<void> Handle(HANDLE h) {
    if (!h || h == INVALID_HANDLE_VALUE) throw Error("Cannot open the running Ditto process.");
    return {h, [](void* value) { CloseHandle(value); }};
}
bool SamePath(const std::wstring& a, const std::wstring& b) { return _wcsicmp(fs::path(a).lexically_normal().c_str(), fs::path(b).lexically_normal().c_str()) == 0; }
class InstallLog {
    std::shared_ptr<void> file;
public:
    std::wstring path;
    void Open(const fs::path& directory) {
        auto target = (directory / L"install.log").wstring();
        RejectReparsePath(target);
        HANDLE raw = CreateFileW(FileApiPath(target).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (raw == INVALID_HANDLE_VALUE) throw Error("Cannot create the update log (Windows error " + std::to_string(GetLastError()) + "). Installation was not started.");
        file = {raw, [](void* value) { CloseHandle(value); }}; path = target;
    }
    void Write(const std::string& message) noexcept {
        if (!file) return;
        try {
            SYSTEMTIME time{}; GetSystemTime(&time);
            char prefix[64];
            sprintf_s(prefix, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ ", time.wYear, time.wMonth,
                time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
            auto line = std::string(prefix) + message + "\r\n";
            size_t offset = 0;
            while (offset < line.size()) {
                DWORD written = 0;
                if (!WriteFile(file.get(), line.data() + offset, static_cast<DWORD>(line.size() - offset), &written, nullptr) || !written) return;
                offset += written;
            }
            FlushFileBuffers(file.get());
        } catch (...) { /* Logging must never prevent recovery after a failure. */ }
    }
    void InspectPath(const char* label, const std::wstring& target) {
        DWORD attributes = GetFileAttributesW(FileApiPath(target).c_str());
        DWORD error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
        Write(std::string(label) + ": " + Utf8(target) + " (attributes=" + std::to_string(attributes)
            + ", Windows error=" + std::to_string(error) + ")");
    }
};
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

}
int RunHelper() {
    std::wstring executable, backup;
    bool backupCreated = false;
    InstallLog log;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    try {
        int argc = 0; wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) throw Error("Invalid update handoff.");
        std::shared_ptr<void> arguments(argv, [](void* p) { LocalFree(p); });
        if ((argc == 2 || (argc == 3 && std::wstring(argv[2]) == L"--uninstall")) && std::wstring(argv[1]) == L"--clear-cache") {
            ManageUpdateCache(0, argc == 3);
            CoUninitialize(); return 0;
        }
        HANDLE rawToken = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) throw Error("Cannot check update helper identity.");
        auto token = Handle(rawToken); TOKEN_ELEVATION elevation{}; DWORD bytes = 0;
        if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &bytes) || elevation.TokenIsElevated) throw Error("Start updates from Ditto running as your normal user, not as administrator.");
        if (argc != 3 || std::wstring(argv[1]) != L"--install") throw Error("This helper is started by Ditto's Install now button.");
        fs::path ticketPath(argv[2]);
        if (ticketPath.filename() != L"install.json" || ticketPath.parent_path().filename().wstring().rfind(L"install-", 0) != 0
            || !SamePath(ticketPath.parent_path().parent_path().wstring(), PrivateCacheRoot())) throw Error("Invalid install handoff location.");
        auto lease = AcquireCacheLease(PrivateCacheRoot());
        log.Open(ticketPath.parent_path());
        log.Write("Update helper started.");
        STARTUPINFOW startupInfo{}; startupInfo.cb = sizeof(startupInfo); GetStartupInfoW(&startupInfo);
        log.Write("Startup flags=" + std::to_string(startupInfo.dwFlags) + ", showWindow=" + std::to_string(startupInfo.wShowWindow)
            + ", Windows build=" + std::to_string(WindowsBuild()) + ".");
        wchar_t currentDirectory[32768];
        DWORD currentLength = GetCurrentDirectoryW(static_cast<DWORD>(std::size(currentDirectory)), currentDirectory);
        if (currentLength && currentLength < std::size(currentDirectory)) log.InspectPath("Working directory", currentDirectory);
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
        log.Write("Verifying installer " + manifest.release.version.String() + ".");
        VerifyHash(installer.get(), manifest.release, cancel);
        VerifyPublisher(installer.get(), installerPath, manifest.release.version, L"Ditto ARM64", unsignedConsent);
        const auto originalVersion = FileVersion(executable);
        {
            auto app = LockFile(executable);
            VerifyPublisher(app.get(), executable, originalVersion, L"Ditto ARM64", unsignedConsent);
        }
        auto startup = StartupCommand();
        backup = PrivateCacheRoot() + L"\\backup-" + UniqueName();
        ShutdownRequest shutdown(static_cast<uint32_t>(pid));
        DWORD installerCode = 1;
        InstallActions actions;
        actions.close = [&] {
            log.Write("Verification passed; requesting shutdown of Ditto process " + std::to_string(pid) + ".");
            shutdown.Post(reinterpret_cast<uintptr_t>(window));
        };
        actions.waitForExit = [&](uint32_t timeout) {
            DWORD result = WaitForSingleObject(parent.get(), timeout);
            if (result == WAIT_FAILED) throw Error("Could not monitor the original Ditto process.");
            return result == WAIT_OBJECT_0;
        };
        actions.waiting = [&] {
            log.Write("Ditto is taking longer to close; offering cancellation.");
            bool retry = MessageBoxW(nullptr, L"Ditto is taking longer than expected to close. Save and close any open Ditto dialogs.\n\nRetry waits another 30 seconds. Cancel stops this update; no installer or permission prompt will appear later. If Ditto has already begun closing, the helper will finish monitoring it and restart it without installing.",
                L"Waiting for Ditto", MB_RETRYCANCEL | MB_ICONINFORMATION | MB_DEFBUTTON2 | MB_SETFOREGROUND) == IDRETRY;
            if (retry) shutdown.Retry(reinterpret_cast<uintptr_t>(window));
            return retry;
        };
        actions.cancelClose = [&] {
            log.Write("Installation cancelled. Withdrawing shutdown or waiting for an already-committed close.");
            // Cancel neutralizes queued requests atomically. If OnClose has
            // already started, monitor refusal/exit before abandoning recovery.
            for (;;) {
                if (WaitForSingleObject(parent.get(), 0) == WAIT_OBJECT_0) return true;
                if (shutdown.Cancel()) return false;
                DWORD result = WaitForSingleObject(parent.get(), 100);
                if (result == WAIT_OBJECT_0) return true;
                if (result == WAIT_FAILED) throw Error("Could not monitor cancelled update shutdown.");
            }
        };
        actions.backup = [&] {
            log.Write("Ditto exited; creating backup.");
            EnsurePrivateDirectory(backup);
            backupCreated = true;
            BackupInstallation(application, database, backup, startup, ExportSettings);
            log.Write("Backup complete: " + Utf8(backup));
        };
        actions.install = [&](const std::function<void()>& started) {
            std::wstring parameters = L"/SP- /SILENT /NORESTART /RESTARTEXITCODE=3010 /NOCLOSEAPPLICATIONS /NORESTARTAPPLICATIONS /DIR=\"" + application + L"\" ";
            parameters += startup.empty() ? L"/TASKS=\"\"" : L"/TASKS=RunAtStartup";
            log.InspectPath("Installer", installerPath);
            log.Write("Requesting Windows elevation.");
            auto setup = LaunchInstallerProcess(installerPath, parameters);
            started();
            log.Write("Installer started; process ID " + std::to_string(GetProcessId(setup.get())) + ".");
            if (WaitForSingleObject(setup.get(), INFINITE) != WAIT_OBJECT_0) throw Error("Could not wait for the installer.");
            if (!GetExitCodeProcess(setup.get(), &installerCode) || (installerCode != 0 && installerCode != 3010))
                throw Error("Installer did not complete successfully (exit " + std::to_string(installerCode) + ").");
        };
        std::shared_ptr<void> restartLock;
        actions.verify = [&] {
            restartLock = LockFile(executable);
            VerifyPublisher(restartLock.get(), executable, manifest.release.version, L"Ditto ARM64", unsignedConsent);
            log.Write("Installed application version and publisher verified.");
        };
        actions.verifyRecovery = [&] {
            restartLock = LockFile(executable);
            auto version = FileVersion(executable);
            if (!(version == originalVersion) && !(version == manifest.release.version))
                throw Error("Setup left an unexpected application version.");
            VerifyPublisher(restartLock.get(), executable, version, L"Ditto ARM64", unsignedConsent);
            log.Write("Application left by failed Setup verified before recovery restart.");
        };
        actions.restart = [&] { RestartAndWait(executable); log.Write("Ditto confirmed a working database and main window."); };
        auto result = RunInstallTransaction(actions);
        // Installation and startup confirmation are distinct. A verified install
        // keeps its recovery backup even if the new process starts slowly.
        std::wstring cleanupWarning;
        try {
            if (fs::exists(backup)) {
                if (result.verified) MarkBackupSuccessful(backup);
                else WriteNewFile(backup + L"\\update-failed", "1");
            }
            auto cleanup = PruneUpdateCache(PrivateCacheRoot(), ticketPath.parent_path().wstring());
            for (const auto& item : cleanup.retained) {
                log.Write("Retained cache item: " + Utf8(item));
                cleanupWarning += L"\n" + item;
            }
        } catch (const std::exception& error) { cleanupWarning = DiagnosticText(error.what()); log.Write("Backup retention: " + std::string(error.what())); }
        if (!cleanupWarning.empty()) cleanupWarning = L"\n\nSome update files could not be removed. About > Manage update files can retry cleanup:\n" + cleanupWarning;
        if (result.cancelled && result.error.empty()) {
            log.Write("Update cancelled; installer was never started.");
            CoUninitialize(); return ERROR_CANCELLED;
        }
        if (!result.success) {
            log.Write(result.verified ? "Installation verified; startup was not confirmed." : "Update did not complete; recovery was attempted before formatting the diagnostic.");
            if (result.recovered) log.Write("Ditto restarted after failure.");
            auto message = InstallFailureMessage(result, manifest.release.version, backup, backupCreated);
            message += L"\n\nUpdate log: " + log.path + cleanupWarning;
            MessageBoxW(nullptr, message.c_str(), result.verified ? L"Installed - check Ditto startup" : L"Ditto ARM64 update", MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
            CoUninitialize(); return 1;
        }
        std::wstring success = L"Ditto ARM64 " + Wide(manifest.release.version.String()) + L" was updated successfully and has restarted.";
        if (installerCode == 3010) success += L"\n\nWindows reports that a restart is needed to finish replacing some files. No restart will happen automatically.";
        success += L"\n\nThe latest two successful-update backups are kept here:\n" + PrivateCacheRoot() +
            L"\nUp to two legacy backups are also kept. Failed-attempt backups are removed. About > Manage update files offers explicit cleanup." + cleanupWarning;
        MessageBoxW(nullptr, success.c_str(), L"Update successful", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        CoUninitialize(); return 0;
    } catch (const std::exception& error) {
        log.Write(std::string("Failure: ") + error.what());
        std::wstring message = DiagnosticText(error.what());
        message += BackupStatusMessage(backup, backupCreated);
        if (!log.path.empty()) message += L"\n\nUpdate log: " + log.path;
        MessageBoxW(nullptr, message.c_str(), L"Ditto ARM64 update", MB_OK | MB_ICONEXCLAMATION | MB_SETFOREGROUND);
        CoUninitialize(); return 1;
    } catch (...) {
        MessageBoxW(nullptr, L"An unexpected update error occurred. Check the installation before starting Ditto; reinstall a known release if needed.", L"Ditto ARM64 update", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        CoUninitialize(); return 1;
    }
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Covers exceptions raised inside an error handler as well as the main body.
    try { return RunHelper(); }
    catch (...) {
        MessageBoxW(nullptr, L"The update could not finish. Check the installation before starting Ditto; reinstall a known release if needed. The backup status could not be determined.",
            L"Ditto ARM64 update", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        CoUninitialize(); return 1;
    }
}
