#pragma once
#include "../../../src/update/UpdateInstall.h"
#include "../../../src/OptionsChangeTracking.h"
#include <thread>
#include <algorithm>

namespace {
void MaintenanceChecks(const std::wstring& root) {
    Require(!DiagnosticText("caf\xe9").empty(), "legacy-code-page diagnostic cannot throw");
    Reject([] { Wide("caf\xe9"); }, "protocol UTF-8 remains strict");
    Require(IncludeBetaUpdates(-1, true) && !IncludeBetaUpdates(-1, false), "unset preference follows build channel");
    Require(!IncludeBetaUpdates(0, true) && IncludeBetaUpdates(1, false), "explicit update preferences survive a channel change");
    for (int fault = 0; fault < 7; ++fault) {
        std::string calls; int waits = 0, starts = 0;
        InstallActions actions;
        actions.close = [&] { calls += 'c'; if (fault == 1) throw Error("close failed"); };
        actions.waitForExit = [&](uint32_t) { ++waits; calls += 'w'; return fault != 6 || waits == 2; };
        actions.waiting = [&] { calls += 'p'; return true; };
        actions.cancelClose = [] { return false; };
        actions.backup = [&] { calls += 'b'; if (fault == 2) throw std::runtime_error("backup caf\xe9 failed"); };
        actions.install = [&] { calls += 'i'; if (fault == 3) throw Error("UAC cancelled"); };
        actions.verify = [&] { calls += 'v'; if (fault == 4) throw Error("installed version differs"); };
        actions.restart = [&] { ++starts; calls += 'r'; if (fault == 5) throw Error("restart failed"); };
        auto result = RunInstallTransaction(actions);
        Require(result.success == (fault == 0 || fault == 6), "installation transaction reports failures");
        Require(starts == (fault == 1 ? 0 : 1), "closed application recovers exactly once");
        if (fault == 2) Require(calls == "cwbr" && result.recovered && !result.error.empty(), "backup failure recovers before displaying non-UTF8 error");
        if (fault == 3 || fault == 4) Require(result.recovered, "cancelled or failed installation restarts Ditto");
        if (fault == 4) Require(result.installed && !result.verified, "completed Setup is distinct from failed installed-file verification");
        if (fault == 5) Require(result.installed && result.verified && !result.success, "verified installation is distinct from unconfirmed startup");
        if (fault == 6) Require(waits == 2 && calls == "cwpwbivr", "late shutdown stays monitored until install/restart finishes");
    }
    {
        InstallActions actions;
        actions.close = [] {}; actions.waitForExit = [](uint32_t) { return true; }; actions.waiting = [] { return true; };
        actions.backup = [] { throw 42; }; actions.install = actions.verify = [] {};
        bool restarted = false; actions.restart = [&] { restarted = true; };
        auto result = RunInstallTransaction(actions);
        Require(restarted && result.recovered && !result.error.empty(), "non-standard exceptions recover too");
    }
    {
        struct Interrupted : Transport {
            int calls = 0; bool permanent = false;
            void Get(const std::wstring&, bool, uint64_t, const Sink& sink, const std::atomic_bool&) override {
                ++calls; const char* text = calls < 3 ? "partial" : "complete";
                sink(reinterpret_cast<const unsigned char*>(text), strlen(text));
                if (permanent) throw Error("TLS verification failed");
                if (calls < 3) throw TransientNetworkError("connection interrupted");
            }
        } transport;
        std::string data; std::atomic_bool cancel(false); unsigned delays = 0;
        auto sink = [&](const unsigned char* p, size_t n) { data.append(reinterpret_cast<const char*>(p), n); };
        auto reset = [&] { data.clear(); };
        GetWithRetry(transport, FeedUrl, true, 100, sink, reset, cancel, [&](unsigned, const std::atomic_bool&) { ++delays; });
        Require(data == "complete" && transport.calls == 3 && delays == 2, "transient retries discard incomplete response bytes");
        transport.calls = 0; transport.permanent = true;
        Reject([&] { GetWithRetry(transport, FeedUrl, true, 100, sink, reset, cancel); }, "permanent failures propagate");
        Require(transport.calls == 1, "TLS and validation failures never retry");
        transport.calls = 0; transport.permanent = false;
        Reject([&] { GetWithRetry(transport, FeedUrl, true, 100, sink, reset, cancel,
            [&](unsigned, const std::atomic_bool&) { cancel = true; }); }, "retry delay is cancellable");
        Require(transport.calls == 1, "cancellation prevents a second request");
    }
    {
        auto application = root + L"\\backup-source"; EnsurePrivateDirectory(application);
        auto database = root + L"\\caf\u00e9.db";
        WriteNewFile(application + L"\\app.txt", "application"); WriteNewFile(database, "clipboard fixture");
        WriteNewFile(database + L"-wal", "wal fixture"); WriteNewFile(database + L"-shm", "shm fixture");
        auto backup = root + L"\\backup-copy"; EnsurePrivateDirectory(backup);
        auto exportFixture = [](const std::wstring& directory) { WriteNewFile(directory + L"\\Ditto-settings.reg", "fixture settings"); };
        BackupInstallation(application, database, backup, L"startup fixture", exportFixture);
        Require(ReadSmallFile(backup + L"\\database\\caf\u00e9.db") == "clipboard fixture" &&
            ReadSmallFile(backup + L"\\database\\caf\u00e9.db-wal") == "wal fixture" &&
            ReadSmallFile(backup + L"\\database\\caf\u00e9.db-shm") == "shm fixture", "production backup copies database/WAL/SHM at Unicode paths");
        Require(ReadSmallFile(backup + L"\\application\\app.txt") == "application" &&
            ParseJson(ReadSmallFile(backup + L"\\backup.json")).At("startupCommand").Text() == "startup fixture", "backup verifies application bytes and startup metadata");
        HANDLE writer = CreateFileW(database.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        Require(writer != INVALID_HANDLE_VALUE, "open database writer fixture");
        auto blocked = root + L"\\backup-blocked"; EnsurePrivateDirectory(blocked);
        Reject([&] { BackupInstallation(application, database, blocked, L"", exportFixture); }, "active database writer blocks backup and installation");
        CloseHandle(writer);
    }
    {
        auto cache = root + L"\\retention"; EnsurePrivateDirectory(cache);
        auto lease = AcquireCacheLease(cache); bool rejected = false;
        std::thread other([&] { try { auto second = AcquireCacheLease(cache); } catch (const Error&) { rejected = true; } }); other.join();
        Require(rejected, "cache lease excludes another updater thread/process");
        std::vector<std::wstring> good, bad;
        for (int i = 0; i < 4; ++i) {
            auto a = cache + L"\\backup-" + UniqueName(); EnsurePrivateDirectory(a); MarkBackupSuccessful(a); good.push_back(a);
            auto b = cache + L"\\backup-" + UniqueName(); EnsurePrivateDirectory(b); WriteNewFile(b + L"\\update-failed", "1"); bad.push_back(b);
            auto time = fs::file_time_type::clock::now() - std::chrono::hours(10 - i);
            fs::last_write_time(a + L"\\update-successful", time); fs::last_write_time(b + L"\\update-failed", time);
        }
        auto legacy = cache + L"\\backup-" + UniqueName(); EnsurePrivateDirectory(legacy); WriteNewFile(legacy + L"\\history.db", "legacy");
        auto active = cache + L"\\install-" + UniqueName(); EnsurePrivateDirectory(active);
        auto expired = cache + L"\\stable-3.25.113.1-" + std::wstring(64, L'a'); EnsurePrivateDirectory(expired);
        WriteNewFile(expired + L"\\installer.exe", "fixture only");
        auto old = fs::file_time_type::clock::now() - std::chrono::hours(24 * 10);
        fs::last_write_time(expired, old); fs::last_write_time(active, old);
        WriteNewFile(cache + L"\\highest-version", "3.25.113.3");
        PruneUpdateCache(cache, active);
        Require(!fs::exists(good[0]) && !fs::exists(good[1]) && fs::exists(good[2]) && fs::exists(good[3]), "retain exactly two successful backups");
        Require(std::none_of(bad.begin(), bad.end(), [](const auto& path) { return fs::exists(path); }), "failed-attempt backups are not retained");
        Require(fs::exists(legacy) && fs::exists(active) && !fs::exists(expired), "keep legacy/active backups; remove expired installers");
        auto outside = root + L"\\outside-cache"; EnsurePrivateDirectory(outside); WriteNewFile(outside + L"\\sentinel", "keep");
        auto link = cache + L"\\backup-" + UniqueName();
        if (!CreateSymbolicLinkW(link.c_str(), outside.c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
            throw Error("Link-safety test requires Developer Mode or symbolic-link privilege.");
        PruneUpdateCache(cache, active);
        auto removal = RemoveUpdateCache(cache);
        Require(removal.retained.size() == 1 && removal.removed > 0, "explicit cleanup reports links and continues other entries");
        Require(ReadSmallFile(outside + L"\\sentinel") == "keep", "cleanup never traverses a link outside the cache");
        // Delete only the link itself; never recurse into its target.
        Require(RemoveDirectoryW(link.c_str()) != FALSE, "remove test link without touching its target");
        lease.reset(); RemoveUpdateCache(cache);
        Require(!fs::exists(legacy) && !fs::exists(active) && ReadSmallFile(cache + L"\\highest-version") == "3.25.113.3", "explicit cleanup removes history backups but keeps rollback floor");
    }
    for (bool lateExit : {false, true}) {
        InstallActions actions; int installs = 0, starts = 0;
        actions.close = [] {}; actions.waitForExit = [](uint32_t) { return false; };
        actions.waiting = [] { return false; }; actions.cancelClose = [=] { return lateExit; };
        actions.backup = actions.verify = [] {}; actions.install = [&] { ++installs; }; actions.restart = [&] { ++starts; };
        auto result = RunInstallTransaction(actions);
        Require(result.cancelled && !result.installed && installs == 0 && starts == (lateExit ? 1 : 0), "cancellation never installs and recovers a late exit exactly once");
    }
    {
        ShutdownRequest close(GetCurrentProcessId()); bool called = false;
        Require(close.Cancel(), "queued shutdown can be withdrawn");
        HandleUpdateShutdown(GetCurrentProcessId(), [&] { called = true; return true; });
        Require(!called, "late delivery of withdrawn shutdown never closes Ditto");
    }
    {
        ShutdownRequest close(GetCurrentProcessId()); bool called = false;
        HandleUpdateShutdown(GetCurrentProcessId(), [&] { called = true; return false; });
        Require(called && close.Cancel(), "refused close can be cancelled without waiting for process exit");
    }
    {
        ShutdownRequest close(GetCurrentProcessId()); bool called = false;
        HandleUpdateShutdown(GetCurrentProcessId(), [&] { called = true; return true; });
        Require(called && !close.Cancel(), "committed shutdown requires monitoring instead of abandoning recovery");
    }
    {
        auto cache = root + L"\\failed-only"; EnsurePrivateDirectory(cache);
        for (int i = 0; i < 6; ++i) {
            auto path = cache + L"\\backup-" + UniqueName(); EnsurePrivateDirectory(path);
            WriteNewFile(path + L"\\update-failed", "1"); WriteNewFile(path + L"\\history.db", "synthetic");
        }
        auto removal = PruneUpdateCache(cache);
        Require(removal.removed == 6 && fs::is_empty(cache), "all six failed backups removed with no successful backup prerequisite");
    }
    {
        auto cache = root + L"\\legacy-limit"; EnsurePrivateDirectory(cache); std::vector<std::wstring> paths;
        for (int i = 0; i < 5; ++i) {
            auto path = cache + L"\\backup-" + UniqueName(); EnsurePrivateDirectory(path); WriteNewFile(path + L"\\history.db", "legacy fixture");
            fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(10 - i)); paths.push_back(path);
        }
        auto removal = PruneUpdateCache(cache);
        Require(removal.removed == 3 && !fs::exists(paths[2]) && fs::exists(paths[3]) && fs::exists(paths[4]), "keep only the newest two legacy backups");
        auto inventory = InspectUpdateCache(cache);
        Require(inventory.legacy == 2 && inventory.bytes == 28 && inventory.successful == 0, "inventory counts legacy files and bytes without reading history contents");
        auto absent = root + L"\\never-created";
        Require(InspectUpdateCache(absent).Empty() && !fs::exists(absent), "inventory of absent cache never creates it");
    }
    {
        auto cache = root + L"\\cleanup-errors"; EnsurePrivateDirectory(cache);
        auto readonly = cache + L"\\backup-" + UniqueName(); EnsurePrivateDirectory(readonly); WriteNewFile(readonly + L"\\update-failed", "1");
        WriteNewFile(readonly + L"\\readonly.txt", "synthetic");
        Require(SetFileAttributesW((readonly + L"\\readonly.txt").c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "create read-only deletion fixture");
        auto removal = PruneUpdateCache(cache);
        Require(removal.removed == 1 && removal.retained.empty() && !fs::exists(readonly), "retention removes read-only backup files safely");
        auto locked = cache + L"\\backup-" + UniqueName(); EnsurePrivateDirectory(locked);
        EnsurePrivateDirectory(locked + L"\\database"); WriteNewFile(locked + L"\\database\\Ditto.db", "synthetic");
        WriteNewFile(locked + L"\\locked.txt", "synthetic");
        auto lock = LockFile(locked + L"\\locked.txt");
        auto other = cache + L"\\backup-" + UniqueName(); EnsurePrivateDirectory(other); WriteNewFile(other + L"\\history.db", "synthetic");
        removal = RemoveUpdateCache(cache);
        Require(removal.retained.size() == 1 && removal.removed == 1 && !fs::exists(other), "one locked file does not abort other explicit removals");
        Require(!fs::exists(locked + L"\\database\\Ditto.db"), "discarded backup data is deleted before a locked application file");
        Require(removal.retained[0].find(L"Windows error") != std::wstring::npos, "cleanup identifies actual Windows failure instead of guessing its cause");
        lock.reset(); removal = RemoveUpdateCache(cache);
        Require(removal.retained.empty() && fs::is_empty(cache), "explicit cleanup succeeds after releasing a fixture lock");
    }
    {
        HWND edit = CreateWindowExW(0, L"EDIT", L"fixture", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(edit != nullptr, "create isolated options edit fixture");
        Require(IsOptionsChange(WM_COMMAND, MAKEWPARAM(1, EN_CHANGE), reinterpret_cast<LPARAM>(edit)), "Options tracks edits even without a page-specific change handler");
        Require(!IsOptionsChange(WM_COMMAND, MAKEWPARAM(1, EN_SETFOCUS), reinterpret_cast<LPARAM>(edit)), "focusing an Options field is not a modification");
        DestroyWindow(edit);
        NMLISTVIEW item{}; item.hdr.code = LVN_ITEMCHANGED; item.uChanged = LVIF_STATE; item.uNewState = LVIS_SELECTED;
        Require(!IsOptionsChange(WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&item)), "list selection alone stays clean");
        item.uNewState = INDEXTOSTATEIMAGEMASK(2); item.uOldState = INDEXTOSTATEIMAGEMASK(1);
        Require(IsOptionsChange(WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&item)), "list checkbox changes are tracked");
    }
}
}
