#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace ditto_update {

constexpr size_t MaxManifestBytes = 64 * 1024;
constexpr uint64_t MaxInstallerBytes = 200ull * 1024 * 1024;
extern const wchar_t* const FeedUrl;

struct Error : std::runtime_error {
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};
struct Cancelled : Error { Cancelled() : Error("Update cancelled.") {} };
std::wstring Wide(const std::string& text);
std::string Utf8(const std::wstring& text);

// A deliberately limited JSON grammar for the versioned update schema: objects,
// strings, unsigned integers and null. Duplicate keys and other types are errors.
struct Json {
    enum class Kind { Null, Object, String, Number } kind = Kind::Null;
    std::map<std::string, Json> object;
    std::string string;
    uint64_t number = 0;
    const Json& At(const char* key) const;
    std::string Text() const;
    uint64_t Number() const;
};
Json ParseJson(const std::string& bytes);
std::string JsonQuote(const std::wstring& text);

struct Version {
    std::array<uint16_t, 4> parts{};
    static Version Parse(const std::string& text);
    std::string String() const;
    bool operator<(const Version& other) const { return parts < other.parts; }
    bool operator==(const Version& other) const { return parts == other.parts; }
};
enum class Channel { Stable, Beta };
const char* ChannelKey(Channel channel);
const wchar_t* BuildLabel(Channel channel);
Channel ParseChannel(const std::string& channel);
struct Release {
    Channel channel = Channel::Stable;
    Version version;
    uint32_t minimumWindowsBuild = 0;
    std::wstring notesUrl, notes, url;
    std::string sha256;
    uint64_t size = 0;
};
struct Manifest { bool hasRelease = false; Release release; };
// Validates both channel entries. Beta opt-in selects the newest of stable/beta;
// equal versions prefer stable. Each entry is a single release or null.
Manifest ParseManifest(const std::string& bytes, bool includeBeta = false);
bool IsInstallerUrl(const std::wstring& url);
bool IsReleaseNotesUrl(const std::wstring& url);
bool IsAllowedDownloadUrl(const std::wstring& url, bool redirect);
void CheckCancelled(const std::atomic_bool& cancel);

using Sink = std::function<void(const unsigned char*, size_t)>;
class Transport {
public:
    virtual ~Transport() = default;
    virtual void Get(const std::wstring& url, bool manifest, uint64_t limit,
        const Sink& sink, const std::atomic_bool& cancel) = 0;
};
class HttpTransport : public Transport {
public:
    void Get(const std::wstring& url, bool manifest, uint64_t limit,
        const Sink& sink, const std::atomic_bool& cancel) override;
};
enum class SignatureStatus { Unknown, Trusted, Unsigned };
struct Verification {
    SignatureStatus installerSignature = SignatureStatus::Unknown;
    bool requiresUnsignedConsent = false;
};
// Consent is local to one installation attempt; it is never a feed/registry option.
void RequireUnsignedConsent(SignatureStatus status, bool consent);
class InstallerVerifier {
public:
    virtual ~InstallerVerifier() = default;
    virtual Verification Verify(const std::wstring& path, const Release& release,
        const std::atomic_bool& cancel) = 0;
};
class WindowsVerifier : public InstallerVerifier {
    std::wstring application;
public:
    explicit WindowsVerifier(const std::wstring& executable = {}) : application(executable) {}
    Verification Verify(const std::wstring& path, const Release& release,
        const std::atomic_bool& cancel) override;
};

bool SigningConfigured();
// Holds a non-writable/non-deletable file handle for the lifetime of the result.
std::shared_ptr<void> LockFile(const std::wstring& path);
void VerifyHash(void* handle, const Release& release, const std::atomic_bool& cancel);
SignatureStatus InspectPublisher(void* handle, const std::wstring& path,
    const Version& version, const std::wstring& product);
void VerifyPublisher(void* handle, const std::wstring& path,
    const Version& version, const std::wstring& product, bool unsignedConsent = false);
std::string HashBytes(const unsigned char* data, size_t size);
Version FileVersion(const std::wstring& path);
std::wstring PrivateCacheRoot();
void EnsurePrivateDirectory(const std::wstring& path);
void RejectReparsePath(const std::wstring& path);
std::wstring FileApiPath(const std::wstring& path);
std::string ReadSmallFile(const std::wstring& path);
std::string ConsumeInstallTicket(const std::wstring& path);
void WriteNewFile(const std::wstring& path, const std::string& bytes);
std::wstring UniqueName();
uint32_t WindowsBuild();
std::wstring RegisteredInstallDirectory();

enum class Phase { Idle, Checking, NoRelease, Current, NewerInstalled, Unsupported,
    Downloading, Verifying, Ready, Installing, Cancelled, Failed };
struct Snapshot {
    Phase phase = Phase::Idle;
    std::wstring message;
    Release release;
    Verification verification;
    uint64_t downloaded = 0;
    std::wstring installerPath, manifestPath;
};
struct Session {
    std::atomic_bool cancel{false};
    mutable std::mutex mutex;
    Snapshot state;
    Snapshot Read() const;
    void Set(Phase phase, const std::wstring& message);
};
struct CheckOptions {
    Version installed;
    uint32_t windowsBuild = 0;
    bool installedMode = false;
    bool includeBeta = false;
    std::wstring executable;
    std::wstring cacheRoot;
};
// Tests inject transports/verifiers here; the application always uses Windows APIs.
void CheckAndDownload(Session& session, const CheckOptions& options,
    Transport& transport, InstallerVerifier& verifier);
void StartCheck(const std::shared_ptr<Session>& session, const CheckOptions& options);

// Writes a local handoff ticket and launches a verified copy of the installed helper.
void LaunchInstallHelper(const Snapshot& ready, const std::wstring& executable,
    const std::wstring& database, uint32_t parentPid, uintptr_t mainWindow, bool unsignedConsent);

} // namespace ditto_update
