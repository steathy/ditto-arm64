#pragma once
#include "UpdateCore.h"

namespace ditto_update {
// Shared by the real helper and fault-injection tests. The caller supplies only
// local operations; nothing in the feed or handoff can replace these callbacks.
struct InstallActions {
    std::function<void()> close, backup, verify, verifyRecovery, restart;
    // Call started immediately after Setup launches, before waiting for it.
    std::function<void(const std::function<void()>& started)> install;
    // false cancels installation. cancelClose returns true only if Ditto has
    // already exited and needs restarting; it must neutralize any queued close.
    std::function<bool()> waiting, cancelClose;
    std::function<bool(uint32_t)> waitForExit;
};
struct InstallResult {
    bool success = false, recovered = false, cancelled = false;
    bool installerStarted = false, installed = false, verified = false;
    bool restartBlocked = false;
    std::wstring error;
};
InstallResult RunInstallTransaction(const InstallActions& actions);
std::wstring BackupStatusMessage(const std::wstring& directory, bool created);
std::wstring InstallFailureMessage(const InstallResult& result, const Version& target,
    const std::wstring& backup, bool backupCreated);
bool ResetMissingReadyDownload(Session& session);
void BackupInstallation(const std::wstring& application, const std::wstring& database,
    const std::wstring& directory, const std::wstring& startup,
    const std::function<void(const std::wstring&)>& exportSettings);
void RestartAndWait(const std::wstring& executable);
void SignalStartupReady() noexcept;

// A queued update shutdown can be withdrawn before the application consumes it.
// Once closing starts, cancellation waits for refusal/exit and never runs Setup.
uint32_t UpdateShutdownMessage();
class ShutdownRequest {
    std::shared_ptr<void> mapping, view;
public:
    explicit ShutdownRequest(uint32_t targetPid);
    void Post(uintptr_t window);
    void Retry(uintptr_t window);
    bool Cancel(); // true: no pending close remains; false: already closing
};
void HandleUpdateShutdown(uint32_t helperPid, const std::function<bool()>& close);

// A lease serializes checks, helper preparation and installation across processes.
// Cleanup never runs concurrently with a download or a helper using this cache.
std::shared_ptr<void> AcquireCacheLease(const std::wstring& root);
void MarkBackupSuccessful(const std::wstring& directory);
struct CleanupResult {
    size_t removed = 0;
    std::vector<std::wstring> retained;
};
struct CacheInventory {
    size_t successful = 0, failed = 0, legacy = 0, downloads = 0;
    uint64_t bytes = 0;
    std::vector<std::wstring> unreadable;
    bool Empty() const { return successful + failed + legacy + downloads == 0; }
};
// Keep two successful installations and two legacy backups; discard failed
// attempts. Only inactive installer/helper folders have the seven-day grace.
CleanupResult PruneUpdateCache(const std::wstring& root, const std::wstring& activeInstall = {});
// Explicit removal uses the same confined, link-rejecting deletion as retention.
CleanupResult RemoveUpdateCache(const std::wstring& root);
CacheInventory InspectUpdateCache(const std::wstring& root);
void ManageUpdateCache(uintptr_t owner, bool uninstall = false, Session* session = nullptr);
}
