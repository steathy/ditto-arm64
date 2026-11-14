#pragma once
#include "UpdateCore.h"

namespace ditto_update {
// Shared by the real helper and fault-injection tests. The caller supplies only
// local operations; nothing in the feed or handoff can replace these callbacks.
struct InstallActions {
    std::function<void()> close, waiting, backup, install, verify, restart;
    std::function<bool(uint32_t)> waitForExit;
};
struct InstallResult {
    bool success = false, recovered = false;
    std::wstring error;
};
InstallResult RunInstallTransaction(const InstallActions& actions);
void BackupInstallation(const std::wstring& application, const std::wstring& database,
    const std::wstring& directory, const std::wstring& startup,
    const std::function<void(const std::wstring&)>& exportSettings);
void RestartAndWait(const std::wstring& executable);
void SignalStartupReady() noexcept;

// A lease serializes checks, helper preparation and installation across processes.
// Cleanup never runs concurrently with a download or a helper using this cache.
std::shared_ptr<void> AcquireCacheLease(const std::wstring& root);
void MarkBackupSuccessful(const std::wstring& directory);
// Unknown/legacy and failed backups are retained for explicit user review.
void PruneUpdateCache(const std::wstring& root, const std::wstring& activeInstall = {});
// Explicit removal uses the same confined, link-rejecting deletion as retention.
void RemoveUpdateCache(const std::wstring& root);
}
