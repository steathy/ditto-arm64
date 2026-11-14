#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "UpdateInstall.h"
#include <filesystem>
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <iomanip>

namespace ditto_update {
namespace fs = std::filesystem;
namespace {
std::shared_ptr<void> Own(HANDLE handle) {
    if (!handle || handle == INVALID_HANDLE_VALUE) throw Error("Could not open update recovery resources (Windows error " + std::to_string(GetLastError()) + ").");
    return {handle, [](void* p) { CloseHandle(p); }};
}
std::wstring ReadyEvent(DWORD pid) { return L"Local\\DittoARM64-StartupReady-" + std::to_wstring(pid); }
enum CloseState : LONG { Pending, Closing, Refused, Withdrawn };
struct SharedClose { volatile LONG state; DWORD targetPid; };
std::wstring CloseName(DWORD pid) { return L"Local\\DittoARM64-Shutdown-" + std::to_wstring(pid); }
std::vector<std::shared_ptr<void>> PinCacheRoot(const std::wstring& root) {
    RejectReparsePath(root);
    std::vector<std::shared_ptr<void>> pinned; fs::path current;
    for (const auto& component : fs::path(root)) {
        current /= component;
        if (!current.has_root_directory()) continue;
        auto handle = Own(CreateFileW(FileApiPath(current.wstring()).c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle.get(), &info) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            throw Error("Cleanup requires a regular cache directory and regular ancestors.");
        pinned.push_back(handle);
    }
    return pinned;
}
// Keep every ancestor pinned while removing children. All removals operate on
// opened handles; a concurrently inserted junction can never redirect deletion.
void RemoveConfined(const fs::path& root, const fs::path& target) {
    auto relative = target.lexically_relative(root);
    if (relative.empty() || relative == L"." || relative.is_absolute()) throw Error("Refusing to remove the update cache root.");
    for (const auto& part : relative) if (part == L"..") throw Error("Cleanup escaped its cache directory.");
    RejectReparsePath(target.wstring());
    auto file = Own(CreateFileW(FileApiPath(target.wstring()).c_str(), FILE_READ_ATTRIBUTES | DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (!GetFileInformationByHandleEx(file.get(), FileAttributeTagInfo, &attributes, sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) throw Error("Cleanup cannot remove a link or junction.");
    if (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        std::vector<fs::path> children;
        for (const auto& child : fs::directory_iterator(target)) children.push_back(child.path());
        // Once a backup is selected for deletion, remove its sensitive data first.
        std::stable_sort(children.begin(), children.end(), [](const auto& a, const auto& b) {
            return a.filename() == L"database" && b.filename() != L"database";
        });
        for (const auto& child : children) RemoveConfined(root, child);
    }
    FILE_DISPOSITION_INFO_EX disposition{FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_IGNORE_READONLY_ATTRIBUTE};
    if (!SetFileInformationByHandle(file.get(), FileDispositionInfoEx, &disposition, sizeof(disposition)))
        throw Error("Cannot delete this update-cache item (Windows error " + std::to_string(GetLastError()) + ").");
}
bool GuidName(const std::wstring& name, const std::wstring& prefix) {
    if (name.rfind(prefix, 0) != 0) return false;
    auto suffix = name.substr(prefix.size());
    if (suffix.empty()) return false;
    return std::all_of(suffix.begin(), suffix.end(), [](wchar_t c) {
        return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') ||
            (c >= L'A' && c <= L'F') || c == L'-' || c == L'{' || c == L'}';
    });
}
bool CacheName(const std::wstring& name) {
    for (const auto* prefix : {L"stable-", L"beta-"}) {
        if (name.rfind(prefix, 0)) continue;
        auto rest = name.substr(wcslen(prefix)); auto dash = rest.find(L'-');
        if (dash == rest.npos || rest.size() - dash - 1 != 64) return false;
        try { Version::Parse(Utf8(rest.substr(0, dash))); } catch (...) { return false; }
        return std::all_of(rest.begin() + dash + 1, rest.end(), [](wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); });
    }
    return false;
}
bool ManagedName(const std::wstring& name) {
    return GuidName(name, L"backup-") || GuidName(name, L"install-") || CacheName(name);
}
CleanupResult RemoveEntries(const fs::path& root, const std::vector<fs::path>& paths) {
    CleanupResult result;
    for (const auto& path : paths) {
        try { RemoveConfined(root, path); ++result.removed; }
        catch (const std::exception& error) { result.retained.push_back(path.wstring() + L": " + DiagnosticText(error.what())); }
    }
    return result;
}
uint64_t MeasureConfined(const fs::path& path) {
    RejectReparsePath(path.wstring());
    auto file = Own(CreateFileW(FileApiPath(path.wstring()).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.get(), &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        throw Error("Cannot inspect a link or inaccessible update-cache item.");
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    uint64_t bytes = 0;
    for (const auto& child : fs::directory_iterator(path)) bytes += MeasureConfined(child.path());
    return bytes;
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
} // namespace
InstallResult RunInstallTransaction(const InstallActions& actions) {
    InstallResult result;
    bool stopped = false;
    try {
        actions.close();
        while (!actions.waitForExit(30000)) {
            if (!actions.waiting()) {
                result.cancelled = true;
                // Neutralize a queued close, or finish monitoring an already
                // committed shutdown. Cancellation can never reach Setup.
                if (actions.cancelClose()) { actions.restart(); result.recovered = true; }
                return result;
            }
        }
        stopped = true;
        actions.backup();
        actions.install([&] { result.installerStarted = true; }); result.installed = true;
        actions.verify(); result.verified = true;
        // Once a restart has been attempted, do not launch a second instance if
        // its readiness signal times out. The diagnostic tells the user to check it.
        stopped = false; actions.restart(); result.success = true;
    } catch (...) {
        auto failure = std::current_exception();
        // Recovery precedes all error-message allocation/conversion.
        if (stopped && result.installerStarted) {
            // A failed target verification must never be bypassed by recovery.
            // If Setup itself failed, inspect what it left before any restart.
            if (result.installed) result.restartBlocked = true;
            else {
                try { actions.verifyRecovery(); }
                catch (...) { result.restartBlocked = true; }
            }
        }
        if (stopped && !result.restartBlocked) {
            try { actions.restart(); result.recovered = true; }
            catch (...) { /* Report the original failure even if restart failed. */ }
        }
        try { std::rethrow_exception(failure); }
        catch (const std::exception& error) { result.error = DiagnosticText(error.what()); }
        catch (...) { result.error = L"An unexpected update error occurred."; }
        if (result.error.empty()) result.error = L"The update could not be completed.";
        if (result.restartBlocked)
            result.error += L"\n\nDitto was not restarted because the installed application could not be verified. Reinstall a known Ditto ARM64 release before opening it.";
        else if (stopped && !result.recovered)
            result.error += L"\n\nDitto startup could not be confirmed. Check whether it is already running before opening it again.";
    }
    return result;
}
std::wstring BackupStatusMessage(const std::wstring& directory, bool created) {
    std::error_code error;
    bool exists = !directory.empty() && fs::exists(directory, error);
    if (error) return L"\n\nThe backup status could not be checked. See the update log for details.";
    if (exists) return L"\n\nBackup folder: " + directory;
    return created ? L"\n\nThe update backup folder was removed."
        : L"\n\nNo update backup was created.";
}
std::wstring InstallFailureMessage(const InstallResult& result, const Version& target,
    const std::wstring& backup, bool backupCreated) {
    std::wstring message;
    if (result.verified) message = L"Ditto ARM64 " + Wide(target.String()) + L" was installed and verified, but startup was not confirmed.\n\n";
    else if (result.installed) message = L"Setup completed, but the installed application could not be verified.\n\n";
    else if (result.cancelled) message = L"The update was cancelled. No installer was started.\n\n";
    else if (result.installerStarted) message = L"Setup started but did not complete successfully.\n\n";
    else message = L"The update stopped before the installer was started.\n\n";
    return message + result.error + BackupStatusMessage(backup, backupCreated);
}
bool ResetMissingReadyDownload(Session& session) {
    std::lock_guard<std::mutex> lock(session.mutex);
    if (session.state.phase != Phase::Ready) return false;
    std::error_code error;
    if (fs::is_regular_file(session.state.installerPath, error) && !error) return false;
    session.state = Snapshot{};
    session.state.message = L"The downloaded installer is no longer available. Check Update to download it again.";
    return true;
}
uint32_t UpdateShutdownMessage() {
    static UINT message = RegisterWindowMessageW(L"DittoARM64.CancelableShutdown.v1");
    return message;
}
ShutdownRequest::ShutdownRequest(uint32_t targetPid) {
    HANDLE raw = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedClose), CloseName(GetCurrentProcessId()).c_str());
    DWORD error = GetLastError(); mapping = Own(raw);
    if (error == ERROR_ALREADY_EXISTS) throw Error("An update shutdown request already exists.");
    auto memory = MapViewOfFile(raw, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedClose));
    if (!memory) throw Error("Cannot prepare a cancellable Ditto shutdown.");
    view = {memory, [](void* p) { UnmapViewOfFile(p); }};
    auto state = static_cast<SharedClose*>(view.get()); state->targetPid = targetPid;
    InterlockedExchange(&state->state, Pending);
}
void ShutdownRequest::Post(uintptr_t window) {
    if (!UpdateShutdownMessage() || !PostMessageW(reinterpret_cast<HWND>(window), UpdateShutdownMessage(), GetCurrentProcessId(), 0))
        throw Error("Ditto could not be asked to close cleanly.");
}
bool ShutdownRequest::Cancel() {
    auto state = static_cast<SharedClose*>(view.get());
    LONG value = InterlockedCompareExchange(&state->state, Withdrawn, Pending);
    if (value == Refused) value = InterlockedCompareExchange(&state->state, Withdrawn, Refused);
    return value == Pending || value == Refused || value == Withdrawn;
}
void ShutdownRequest::Retry(uintptr_t window) {
    auto state = static_cast<SharedClose*>(view.get());
    if (InterlockedCompareExchange(&state->state, Pending, Refused) == Refused) Post(window);
}
void HandleUpdateShutdown(uint32_t helperPid, const std::function<bool()>& close) {
    // Missing/withdrawn requests are ignored, including messages left in a queue
    // after a cancelled helper exits. Never fall back to an unconditional WM_CLOSE.
    HANDLE raw = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, CloseName(helperPid).c_str());
    if (!raw) return;
    auto mapping = Own(raw); auto memory = MapViewOfFile(raw, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedClose));
    if (!memory) return;
    std::shared_ptr<void> view(memory, [](void* p) { UnmapViewOfFile(p); });
    auto state = static_cast<SharedClose*>(memory);
    if (state->targetPid != GetCurrentProcessId() || InterlockedCompareExchange(&state->state, Closing, Pending) != Pending) return;
    try { if (!close()) InterlockedExchange(&state->state, Refused); }
    catch (...) { InterlockedExchange(&state->state, Refused); throw; }
}
void SignalStartupReady() noexcept {
    try {
        HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, ReadyEvent(GetCurrentProcessId()).c_str());
        if (event) { SetEvent(event); CloseHandle(event); }
    } catch (...) {}
}
void RestartAndWait(const std::wstring& executable) {
    std::wstring command = L"\"" + executable + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{}; auto directory = fs::path(executable).parent_path().wstring();
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
        nullptr, directory.c_str(), &startup, &process)) throw Error("Ditto could not be restarted. Open it from the Start menu.");
    auto child = Own(process.hProcess); auto thread = Own(process.hThread);
    HANDLE rawEvent = CreateEventW(nullptr, TRUE, FALSE, ReadyEvent(process.dwProcessId).c_str());
    DWORD eventError = GetLastError();
    // Always resume a successfully created process, even if readiness monitoring
    // cannot be established. Never leave a suspended Ditto behind on error.
    if (rawEvent && eventError != ERROR_ALREADY_EXISTS) ResetEvent(rawEvent);
    ResumeThread(thread.get());
    auto ready = Own(rawEvent);
    if (eventError == ERROR_ALREADY_EXISTS) throw Error("Ditto's startup notification was already in use. Check whether Ditto is running.");
    HANDLE handles[] = {ready.get(), child.get()};
    bool confirmed = false;
    const auto deadline = GetTickCount64() + 30000;
    do {
        DWORD waited = WaitForMultipleObjects(2, handles, FALSE, 100);
        if (waited == WAIT_OBJECT_0) { confirmed = true; break; }
        if (waited != WAIT_TIMEOUT) break;
        // Older released clients predate the event protocol. Their main-window
        // registration is written only after opening the database. Require the
        // newly launched process to own that window and finish GUI startup;
        // an old handle, another Ditto instance or the no-database UI cannot pass.
        DWORD value = 0, size = sizeof(value), owner = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Ditto", L"MainhWnd", RRF_RT_REG_DWORD,
            nullptr, &value, &size) == ERROR_SUCCESS) {
            auto window = reinterpret_cast<HWND>(static_cast<uintptr_t>(value));
            if (IsWindow(window)) GetWindowThreadProcessId(window, &owner);
            if (owner == process.dwProcessId && WaitForInputIdle(child.get(), 0) == 0) {
                confirmed = true; break;
            }
        }
    } while (GetTickCount64() < deadline);
    if (!confirmed || WaitForSingleObject(child.get(), 250) != WAIT_TIMEOUT)
        throw Error("Ditto was started but did not confirm a working database and main window. Check Ditto before continuing.");
}
std::shared_ptr<void> AcquireCacheLease(const std::wstring& root) {
    RejectReparsePath(root);
    auto normalized = fs::path(root).lexically_normal().wstring();
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), towlower);
    auto bytes = Utf8(normalized);
    auto name = L"Local\\DittoARM64-Cache-" + Wide(HashBytes(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()));
    HANDLE raw = CreateMutexW(nullptr, FALSE, name.c_str());
    auto mutex = Own(raw);
    DWORD result = WaitForSingleObject(raw, 0);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) throw Error("Another Ditto update is using the cache. Wait for it to finish and try again.");
    return {raw, [mutex](void* p) { ReleaseMutex(p); }};
}
void MarkBackupSuccessful(const std::wstring& directory) { WriteNewFile(directory + L"\\update-successful", "1"); }
CleanupResult PruneUpdateCache(const std::wstring& rootText, const std::wstring& activeInstall) {
    RejectReparsePath(rootText);
    if (!fs::exists(rootText)) return {};
    fs::path root(rootText);
    auto pinned = PinCacheRoot(rootText);
    std::vector<fs::path> successful, legacy, expired;
    auto now = fs::file_time_type::clock::now();
    for (const auto& entry : fs::directory_iterator(root)) {
        auto path = entry.path(); auto name = path.filename().wstring();
        if (!activeInstall.empty() && _wcsicmp(path.c_str(), activeInstall.c_str()) == 0) continue;
        try {
            RejectReparsePath(path.wstring());
            if (!entry.is_directory()) continue;
            if (GuidName(name, L"backup-")) {
                auto ok = path / L"update-successful", bad = path / L"update-failed";
                RejectReparsePath(ok.wstring()); RejectReparsePath(bad.wstring());
                if (fs::is_regular_file(ok)) successful.push_back(path);
                else if (fs::is_regular_file(bad)) expired.push_back(path);
                else legacy.push_back(path);
            } else if ((CacheName(name) || GuidName(name, L"install-")) &&
                now - entry.last_write_time() > std::chrono::hours(24 * 7)) expired.push_back(path);
        } catch (...) { /* Unknown or locked items are retained. */ }
    }
    auto retainTwo = [&](std::vector<fs::path>& paths, const wchar_t* marker) {
        std::sort(paths.begin(), paths.end(), [&](const auto& a, const auto& b) {
            auto left = fs::last_write_time(*marker ? a / marker : a), right = fs::last_write_time(*marker ? b / marker : b);
            return left != right ? left > right : a.native() > b.native();
        });
        for (size_t i = 2; i < paths.size(); ++i) expired.push_back(paths[i]);
    };
    retainTwo(successful, L"update-successful");
    retainTwo(legacy, L"");
    return RemoveEntries(root, expired);
}
CleanupResult RemoveUpdateCache(const std::wstring& rootText) {
    RejectReparsePath(rootText);
    if (!fs::exists(rootText)) return {};
    auto lease = AcquireCacheLease(rootText);
    fs::path root(rootText);
    auto pinned = PinCacheRoot(rootText);
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(root)) {
        const auto name = entry.path().filename().wstring();
        if (ManagedName(name)) paths.push_back(entry.path());
    }
    // Preserve accepted-version records; deleting backups must not reset rollback protection.
    return RemoveEntries(root, paths);
}
CacheInventory InspectUpdateCache(const std::wstring& rootText) {
    CacheInventory result; RejectReparsePath(rootText);
    if (!fs::exists(rootText)) return result;
    auto lease = AcquireCacheLease(rootText); auto pinned = PinCacheRoot(rootText);
    for (const auto& entry : fs::directory_iterator(rootText)) {
        const auto path = entry.path(); auto name = path.filename().wstring();
        if (!ManagedName(name)) continue;
        try {
            RejectReparsePath(path.wstring());
            if (GuidName(name, L"backup-")) {
                RejectReparsePath((path / L"update-successful").wstring()); RejectReparsePath((path / L"update-failed").wstring());
                if (fs::is_regular_file(path / L"update-successful")) ++result.successful;
                else if (fs::is_regular_file(path / L"update-failed")) ++result.failed;
                else ++result.legacy;
            } else ++result.downloads;
            result.bytes += MeasureConfined(path);
        } catch (const std::exception& error) { result.unreadable.push_back(path.wstring() + L": " + DiagnosticText(error.what())); }
    }
    return result;
}
void ManageUpdateCache(uintptr_t owner, bool uninstall, Session* session) {
    // A different administrator, or a session with no identifiable shell, must
    // not receive a misleading offer to clean the desktop user's profile.
    if (uninstall && !IsCurrentInteractiveUser()) {
        MessageBoxW(reinterpret_cast<HWND>(owner), L"Automatic cleanup was skipped because this uninstaller's account could not be matched to the desktop user.\n\nUpdate backups containing clipboard history may remain in the original user's %LOCALAPPDATA%\\DittoARM64-Updates folder.\n\nUse About > Manage update files before uninstalling, or sign in as the original user to review and remove unwanted backup and download folders afterwards. No other user's profile was examined.",
            L"Update backups may remain", MB_OK | MB_ICONINFORMATION);
        return;
    }
    auto root = UpdateCachePath(); auto inventory = InspectUpdateCache(root);
    auto window = reinterpret_cast<HWND>(owner);
    if (inventory.Empty() && inventory.unreadable.empty()) {
        if (!uninstall) MessageBoxW(window, L"There are no update backups or downloaded installers to remove.", L"Manage update files", MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wostringstream prompt;
    prompt << L"Update files in:\n" << root << L"\n\nSuccessful-update backups: " << inventory.successful
        << L"\nLegacy backups: " << inventory.legacy << L"\nFailed-attempt backups awaiting cleanup: " << inventory.failed
        << L"\nInstaller/helper folders: " << inventory.downloads << L"\nReadable size: " << std::fixed << std::setprecision(1)
        << inventory.bytes / 1048576.0 << L" MB\n\nKeep policy: two successful-update backups and two legacy backups; no failed-attempt backups."
        << L"\n\nDelete all these backups and downloaded installers now? Backups can contain clipboard history. This cannot be undone."
        << L"\n\nYour live database, settings and update rollback-protection records will be kept.";
    if (!inventory.unreadable.empty()) prompt << L"\n\nSome items could not be inspected. Cleanup will report any items it cannot remove.";
    if (MessageBoxW(window, prompt.str().c_str(), L"Manage update files", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    auto result = RemoveUpdateCache(root);
    if (session) ResetMissingReadyDownload(*session);
    std::wstring message = std::to_wstring(result.removed) + L" update-cache folders removed.";
    if (result.retained.empty()) message += L"\n\nYour live database, settings and rollback-protection records were kept.";
    else {
        message += L"\n\nSome items remain (a folder may have been partially cleaned):";
        for (const auto& item : result.retained) message += L"\n\n" + item;
    }
    MessageBoxW(window, message.c_str(), L"Update-file cleanup", MB_OK | (result.retained.empty() ? MB_ICONINFORMATION : MB_ICONWARNING));
}
void BackupInstallation(const std::wstring& application, const std::wstring& database,
    const std::wstring& directory, const std::wstring& startup,
    const std::function<void(const std::wstring&)>& exportSettings) {
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
    exportSettings(directory);
    WriteNewFile(directory + L"\\backup.json", "{\"application\":" + JsonQuote(application) + ",\"database\":" + JsonQuote(database) + ",\"startupCommand\":" + JsonQuote(startup) + "}");
}
}
