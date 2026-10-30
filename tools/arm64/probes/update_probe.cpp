#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include "../../../src/update/UpdateCore.h"
#include <filesystem>
#include <iostream>
#include <fstream>

using namespace ditto_update;
namespace fs = std::filesystem;
namespace {
int tests = 0;
void Require(bool condition, const char* what) { ++tests; if (!condition) throw Error(std::string("FAILED: ") + what); }
template<class F> void Reject(F action, const char* what) { bool failed = false; try { action(); } catch (const std::exception&) { failed = true; } Require(failed, what); }
std::string Replace(std::string text, const std::string& from, const std::string& to) {
    auto at = text.find(from); if (at == text.npos) throw Error("Bad test fixture."); text.replace(at, from.size(), to); return text;
}
const std::string payload = "test installer bytes - never executable";
std::string ReleaseJson(const std::string& version) {
    if (version.empty()) return "null";
    return "{\"version\":\"" + version + "\",\"publishedAt\":\"2026-10-29T12:00:00Z\",\"minimumWindowsBuild\":22000,\"releaseNotesUrl\":\"https://github.com/steathy/ditto-arm64/releases/tag/v" + version + "\",\"releaseNotes\":\"Fixes\\nClipboard compatibility \\uD83D\\uDCCB\",\"installer\":{\"url\":\"https://github.com/steathy/ditto-arm64/releases/download/v" + version + "/DittoSetup_arm64.exe\",\"sha256\":\"" + HashBytes(reinterpret_cast<const unsigned char*>(payload.data()), payload.size()) + "\",\"size\":" + std::to_string(payload.size()) + "}}";
}
std::string Feed(const std::string& stable = "3.25.146.10", const std::string& beta = "") {
    return "{\"schemaVersion\":2,\"appId\":\"steathy.ditto-arm64\",\"platform\":\"windows\",\"architecture\":\"arm64\",\"channels\":{\"stable\":" + ReleaseJson(stable) + ",\"beta\":" + ReleaseJson(beta) + "}}";
}
struct FakeTransport : Transport {
    std::string feed = Feed(), data = payload, error;
    int manifests = 0, installers = 0;
    std::atomic_bool* cancelDuringDownload = nullptr;
    Session* observed = nullptr;
    bool sawDownloading = false;
    void Get(const std::wstring&, bool manifest, uint64_t, const Sink& sink, const std::atomic_bool&) override {
        if (!error.empty()) throw Error(error);
        if (manifest) { ++manifests; sink(reinterpret_cast<const unsigned char*>(feed.data()), feed.size()); }
        else {
            ++installers;
            for (size_t i = 0; i < data.size(); ++i) {
                if (observed && observed->Read().phase == Phase::Downloading) sawDownloading = true;
                if (cancelDuringDownload && i == 2) *cancelDuringDownload = true;
                sink(reinterpret_cast<const unsigned char*>(data.data() + i), 1);
            }
        }
    }
};
// Only the test process uses this verifier. The application/helper always require
// WindowsVerifier/VerifyPublisher. No feed or registry flag can approve unsigned files.
struct HashOnlyFixtureVerifier : InstallerVerifier {
    int calls = 0; bool failSignature = false, unsignedFile = false;
    Verification Verify(const std::wstring& path, const Release& r, const std::atomic_bool& cancel) override {
        ++calls; auto file = LockFile(path); VerifyHash(file.get(), r, cancel);
        if (failSignature) throw Error("Simulated untrusted publisher.");
        return {unsignedFile ? SignatureStatus::Unsigned : SignatureStatus::Trusted, unsignedFile};
    }
};
CheckOptions Options(const std::wstring& root) {
    CheckOptions options; options.installed = Version::Parse("3.25.146.5"); options.windowsBuild = 26100;
    options.installedMode = true; options.cacheRoot = root + L"\\case-" + UniqueName(); return options;
}
void AssertNoPartial(const std::wstring& root) {
    bool partial = false;
    if (fs::exists(root)) for (const auto& entry : fs::recursive_directory_iterator(root)) if (entry.path().extension() == L".partial") partial = true;
    Require(!partial, "cancel/failure removes only its partial file");
}
int MissingInstallerChild(const std::wstring& path) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int result = 4;
    try { LaunchInstallerProcess(path, L""); }
    catch (const Error& error) {
        std::string message(error.what());
        // Shell versions may report a missing leaf as either FILE_NOT_FOUND
        // or PATH_NOT_FOUND. Both must retain the numeric error for diagnosis.
        if (message.find("Windows error 2,") != message.npos || message.find("Windows error 3,") != message.npos) result = 0;
        else std::cerr << message << '\n';
    }
    CoUninitialize(); return result;
}
void CheckMissingInstaller(const std::wstring& path) {
    wchar_t executable[32768];
    if (!GetModuleFileNameW(nullptr, executable, 32768)) throw Error("Cannot locate launch-test child.");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --missing-installer \"" + path + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    // Reproduce the old helper startup conditions: a shell error would create
    // an invisible dialog and hang this child without SEE_MASK_FLAG_NO_UI.
    startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &child)) throw Error("Cannot start launch-test child.");
    DWORD waited = WaitForSingleObject(child.hProcess, 10000), code = 1;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &code);
    else { TerminateProcess(child.hProcess, 1); WaitForSingleObject(child.hProcess, 5000); }
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
    Require(waited == WAIT_OBJECT_0 && code == 0, "missing installer returns a numeric Windows error without an invisible-dialog hang");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"--missing-installer") return MissingInstallerChild(argv[2]);
        if (argc < 2) throw Error("Pass an isolated test directory.");
        std::wstring root = argv[1]; EnsurePrivateDirectory(root);
        auto launchRoot = root + L"\\launch error fixture"; EnsurePrivateDirectory(launchRoot);
        CheckMissingInstaller(launchRoot + L"\\missing-installer.exe");
        CheckMissingInstaller(launchRoot + L"\\missing-directory\\installer.exe");
        Require(Version::Parse("3.25.146.2") < Version::Parse("3.25.146.10"), "numeric multi-digit version ordering");
        Require(Version::Parse("65535.65535.65535.65535").String() == "65535.65535.65535.65535", "version bounds");
        for (const auto* bad : {"3.2.1", "3.2.1.0.5", "3.2.1.-1", "3.2.1.65536", "03.2.1.0", "3.2.1.0-beta", "3.2.1.99999999999"}) Reject([&] { Version::Parse(bad); }, "reject malformed version");
        auto base = Feed(); auto parsed = ParseManifest(base);
        Require(parsed.hasRelease && parsed.release.notes.find(L'\n') != std::wstring::npos, "versioned feed and inline notes parsed");
        Require(ParseManifest("\xef\xbb\xbf" + base).hasRelease, "UTF-8 BOM accepted");
        Reject([&] { ParseJson(std::string(MaxManifestBytes + 1, ' ')); }, "oversized JSON");
        for (const auto* bad : {"{\"a\":1,\"a\":2}", "{\"a\":1,}", "{\"a\":01}", "{\"a\":1.2}", "{\"a\":-1}", "{\"a\":18446744073709551616}", "{\"a\":\"\\uD800\"}", "{\"a\":\"\\uDC00\"}", "{}garbage"}) Reject([&] { ParseJson(bad); }, "strict JSON syntax");
        Reject([&] { ParseManifest(Replace(base, "\"arm64\"", "\"x64\"")); }, "wrong architecture");
        Reject([&] { ParseManifest(Replace(base, "\"stable\"", "\"preview\"")); }, "wrong channel");
        Reject([&] { ParseManifest(Replace(base, "\"steathy.ditto-arm64\"", "\"other-app\"")); }, "wrong app ID");
        Reject([&] { ParseManifest(Replace(base, "\"schemaVersion\":2", "\"schemaVersion\":3")); }, "unknown schema version");
        Reject([&] { ParseManifest(Replace(base, "\"schemaVersion\":2", "\"schemaVersion\":1")); }, "old schema cannot be misinterpreted");
        Reject([&] { ParseManifest(Replace(base, "\"size\":" + std::to_string(payload.size()), "\"size\":209715201")); }, "oversized installer");
        Reject([&] { ParseManifest(Replace(base, "\"sha256\":\"", "\"sha256\":\"g")); }, "invalid hash");
        Reject([&] { ParseManifest(Replace(base, "\"installer\":{", "\"command\":\"run\",\"installer\":{")); }, "remote commands cannot enter schema");
        Require(!CheckOptions{}.includeBeta, "stable updates are the default even on a nightly build");
        Require(std::wstring(BuildLabel(Channel::Beta)) == L"Nightly Build" && std::wstring(BuildLabel(Channel::Stable)) == L"Stable Build", "requested build display labels");
        Require(ParseChannel("beta") == Channel::Beta && ParseChannel("stable") == Channel::Stable, "helper channel keys parsed");
        Reject([&] { ParseChannel("nightly"); }, "handoff rejects unknown channel keys");
        Reject([&] { ParseManifest(Replace(base, ",\"beta\":null", "")); }, "both channel slots are required");
        Reject([&] { ParseManifest(Replace(base, "\"beta\":null", "\"beta\":null,\"beta\":null")); }, "duplicate channel rejected");
        Reject([&] { ParseManifest(Replace(base, "\"beta\":null", "\"beta\":[]")); }, "channel cannot hold a release list");
        Reject([&] { ParseManifest(Replace(Feed("", "3.25.146.11"), "\"size\":" + std::to_string(payload.size()), "\"size\":0")); }, "unselected beta entry is validated too");
        Require(!ParseManifest(Feed("", ""), true).hasRelease, "two null entries mean no published release");
        Require(!ParseManifest(Feed("", "3.25.146.11")).hasRelease, "stable default never falls back to beta");
        Require(ParseManifest(Feed("", "3.25.146.11"), true).release.channel == Channel::Beta, "beta-only release requires opt-in");
        Require(ParseManifest(Feed("3.25.146.10", "3.25.146.11")).release.version == Version::Parse("3.25.146.10"), "stable users ignore a newer beta");
        Require(ParseManifest(Feed("3.25.146.10", "3.25.146.11"), true).release.version == Version::Parse("3.25.146.11"), "opt-in chooses the newest beta");
        Require(ParseManifest(Feed("3.25.146.12", "3.25.146.11"), true).release.channel == Channel::Stable, "opt-in still receives a newer stable release");
        Require(ParseManifest(Feed("3.25.146.11", "3.25.146.11"), true).release.channel == Channel::Stable, "equal channel versions prefer stable");
        for (const auto* bad : {L"http://github.com/steathy/ditto-arm64/releases/download/v1/a.exe", L"https://github.com.evil.test/steathy/ditto-arm64/releases/download/v1/a.exe", L"https://github.com@evil.test/steathy/ditto-arm64/releases/download/v1/a.exe", L"https://github.com:444/steathy/ditto-arm64/releases/download/v1/a.exe", L"https://github.com/steathy/ditto-arm64/releases/download/v1/%2e%2e/a.exe", L"https://github.com/steathy/ditto-arm64/releases/download/v1/../a.exe", L"https://github.com/other/repo/releases/download/v1/a.exe", L"file:///installer.exe"}) Require(!IsInstallerUrl(bad), "unsafe installer URL rejected");
        Require(IsAllowedDownloadUrl(L"https://release-assets.githubusercontent.com/github-production-release-asset/1/asset?sp=r&sig=encoded%2Fvalue", true), "explicit GitHub asset redirect supported");
        Require(!IsAllowedDownloadUrl(L"https://release-assets.githubusercontent.com.evil.test/asset", true), "redirect suffix attack rejected");
        Require(!IsAllowedDownloadUrl(L"https://release-assets.githubusercontent.com/asset", false), "initial download must belong to fork");
        Require(!IsReleaseNotesUrl(L"https://github.com/steathy/ditto-arm64/releases/tag/../evil"), "unsafe notes link rejected");
        Require(HashBytes(reinterpret_cast<const unsigned char*>("abc"), 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known vector");
        auto fixture = root + L"\\unsigned-fixture.exe"; WriteNewFile(fixture, payload);
        {
            auto file = LockFile(fixture); std::atomic_bool cancel(false); VerifyHash(file.get(), parsed.release, cancel);
            auto changed = parsed.release; changed.size++;
            Reject([&] { VerifyHash(file.get(), changed, cancel); }, "wrong byte count rejected");
            changed = parsed.release; changed.sha256.assign(64, '0');
            Reject([&] { VerifyHash(file.get(), changed, cancel); }, "corrupt bytes rejected");
            HANDLE writer = CreateFileW(fixture.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            Require(writer == INVALID_HANDLE_VALUE, "verified file cannot be replaced or written while locked");
            if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
            Require(!DeleteFileW(fixture.c_str()), "locked verified file cannot be deleted");
        }
        WindowsVerifier realVerifier; std::atomic_bool cancelled(false);
        {
            auto directory = root + L"\\locked-parent"; EnsurePrivateDirectory(directory);
            auto name = directory + L"\\file"; WriteNewFile(name, payload);
            auto lock = LockFile(name);
            Require(!MoveFileExW(FileApiPath(directory).c_str(), FileApiPath(directory + L"-moved").c_str(), 0), "verified file ancestor cannot be swapped during launch");
        }
        Reject([&] { realVerifier.Verify(fixture, parsed.release, cancelled); }, "real Windows verifier rejects non-executable fixture");
        Require(!SigningConfigured(), "development signing policy is deliberately unset");
        Reject([&] { RequireUnsignedConsent(SignatureStatus::Unsigned, false); }, "unsigned install requires explicit consent");
        Reject([&] { RequireUnsignedConsent(SignatureStatus::Unknown, true); }, "consent cannot approve an unverified file");
        RequireUnsignedConsent(SignatureStatus::Unsigned, true);
        RequireUnsignedConsent(SignatureStatus::Trusted, false);
        {
            wchar_t self[32768]; Require(GetModuleFileNameW(nullptr, self, 32768) != 0, "locate real PE fixture");
            std::ifstream input(fs::path(self), std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            auto path = root + L"\\unsigned-pe.exe"; WriteNewFile(path, bytes);
            Release release = parsed.release; release.size = bytes.size();
            release.sha256 = HashBytes(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
            auto result = realVerifier.Verify(path, release, cancelled);
            Require(result.installerSignature == SignatureStatus::Unsigned && result.requiresUnsignedConsent, "real unsigned PE is distinguished from invalid signature");
            if (argc < 3) throw Error("Pass the built Ditto executable for local helper preflight checks.");
            WindowsVerifier withLocalFiles(argv[2]);
            Require(withLocalFiles.Verify(path, release, cancelled).requiresUnsignedConsent, "current unsigned app/helper can bootstrap an update after consent");
            {
                FakeTransport transport; transport.data = bytes;
                transport.feed = Replace(Feed(), parsed.release.sha256, release.sha256);
                transport.feed = Replace(transport.feed, "\"size\":" + std::to_string(payload.size()), "\"size\":" + std::to_string(bytes.size()));
                auto options = Options(root); Session download;
                CheckAndDownload(download, options, transport, realVerifier);
                if (download.Read().phase != Phase::Ready) std::cerr << "Real unsigned download: " << Utf8(download.Read().message) << '\n';
                Require(download.Read().phase == Phase::Ready && download.Read().verification.requiresUnsignedConsent, "real unsigned PE survives download, partial-file verification and cache rename");
                Session cached; CheckAndDownload(cached, options, transport, realVerifier);
                Require(cached.Read().phase == Phase::Ready && cached.Read().verification.requiresUnsignedConsent && transport.installers == 1, "real unsigned cached PE is reverified without reusing consent");
            }
            {
                auto file = LockFile(path);
                Reject([&] { VerifyPublisher(file.get(), path, release.version, L"Ditto ARM64"); }, "unsigned PE blocked before user consent");
                VerifyPublisher(file.get(), path, release.version, L"Ditto ARM64", true);
                Require(true, "real unsigned PE allowed after per-install consent");
                Reject([&] { VerifyPublisher(file.get(), path, release.version, L"Other Product", true); }, "unsigned consent does not bypass product identity");
                Reject([&] { VerifyPublisher(file.get(), path, Version::Parse("1.0.0.0"), L"Ditto ARM64", true); }, "unsigned consent does not bypass version identity");
            }
            auto damaged = bytes; IMAGE_DOS_HEADER dos{}; memcpy(&dos, damaged.data(), sizeof(dos));
            size_t security = dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER)
                + offsetof(IMAGE_OPTIONAL_HEADER64, DataDirectory) + IMAGE_DIRECTORY_ENTRY_SECURITY * sizeof(IMAGE_DATA_DIRECTORY);
            IMAGE_DATA_DIRECTORY fakeCertificate{static_cast<DWORD>(damaged.size()), 16};
            memcpy(&damaged[security], &fakeCertificate, sizeof(fakeCertificate)); damaged.append(16, '\0');
            auto badPath = root + L"\\damaged-signature.exe"; WriteNewFile(badPath, damaged);
            auto badFile = LockFile(badPath);
            Reject([&] { VerifyPublisher(badFile.get(), badPath, release.version, L"Ditto ARM64", true); }, "damaged signature table cannot become unsigned even with consent");
            auto ticket = root + L"\\single-use.json"; WriteNewFile(ticket, "{\"unsignedConsent\":1}");
            Require(ParseJson(ConsumeInstallTicket(ticket)).At("unsignedConsent").Number() == 1, "install handoff delivers explicit choice");
            Reject([&] { ConsumeInstallTicket(ticket); }, "install consent ticket cannot be replayed");
        }
        {
            FakeTransport transport; HashOnlyFixtureVerifier verifier; verifier.unsignedFile = true;
            auto options = Options(root); Session session;
            CheckAndDownload(session, options, transport, verifier);
            auto ready = session.Read();
            Require(ready.phase == Phase::Ready && ready.verification.requiresUnsignedConsent, "unsigned download becomes ready for user review");
            Require(!fs::exists(options.cacheRoot + L"\\highest-version"), "unapproved unsigned offer cannot raise rollback floor");
            Reject([&] { LaunchInstallHelper(ready, L"", L"", 0, 0, false); }, "installer launch is blocked without per-install consent");
            Session again; CheckAndDownload(again, options, transport, verifier);
            Require(again.Read().verification.requiresUnsignedConsent && verifier.calls == 2 && transport.installers == 1, "cached unsigned update is rechecked and requires new consent");
        }
        {
            FakeTransport transport; HashOnlyFixtureVerifier verifier; Session session; auto options = Options(root);
            transport.feed = Feed("", "");
            CheckAndDownload(session, options, transport, verifier);
            Require(session.Read().phase == Phase::NoRelease && transport.installers == 0, "empty feed is not an update");
        }
        for (auto version : {"3.25.146.5", "3.25.146.4"}) {
            FakeTransport transport; transport.feed = Feed(version); HashOnlyFixtureVerifier verifier; Session session;
            CheckAndDownload(session, Options(root), transport, verifier);
            Require(transport.installers == 0 && (session.Read().phase == Phase::Current || session.Read().phase == Phase::NewerInstalled), "equal/older releases never download");
        }
        {
            FakeTransport transport; transport.feed = Feed("", "3.25.146.11"); HashOnlyFixtureVerifier verifier; Session session;
            CheckAndDownload(session, Options(root), transport, verifier);
            Require(session.Read().phase == Phase::NoRelease && transport.installers == 0, "beta-only feed does not download without opt-in");
        }
        {
            FakeTransport transport; transport.feed = Feed("3.25.146.8", "3.25.146.11"); HashOnlyFixtureVerifier verifier; auto options = Options(root);
            options.includeBeta = true; Session beta;
            CheckAndDownload(beta, options, transport, verifier);
            Require(beta.Read().phase == Phase::Ready && beta.Read().release.channel == Channel::Beta && verifier.calls == 1, "beta download uses the same verification path");
            auto cached = ParseManifest(ReadSmallFile(beta.Read().manifestPath), true);
            Require(cached.release.channel == Channel::Beta && cached.release.version == beta.Read().release.version, "cached handoff preserves beta selection");
            Session later; CheckAndDownload(later, options, transport, verifier);
            Require(later.Read().phase == Phase::Ready && transport.installers == 1 && verifier.calls == 2, "beta cache reverified after fresh feed check");
            transport.feed = Feed("3.25.146.8", "3.25.146.10"); Session rollback;
            CheckAndDownload(rollback, options, transport, verifier);
            Require(rollback.Read().phase == Phase::Failed && transport.installers == 1, "beta rollback protection enforced");
            options.includeBeta = false; Session stable;
            CheckAndDownload(stable, options, transport, verifier);
            Require(stable.Read().phase == Phase::Ready && stable.Read().release.channel == Channel::Stable && transport.installers == 2, "downloaded beta does not block a newer-than-installed stable update");
            options.installed = Version::Parse("3.25.146.11"); Session installedBeta;
            CheckAndDownload(installedBeta, options, transport, verifier);
            Require(installedBeta.Read().phase == Phase::NewerInstalled && transport.installers == 2, "switching installed beta to stable never downgrades");
        }
        {
            FakeTransport transport; transport.feed = Feed("", "3.25.146.11"); HashOnlyFixtureVerifier verifier; verifier.failSignature = true;
            auto options = Options(root); options.includeBeta = true; Session session;
            CheckAndDownload(session, options, transport, verifier);
            Require(session.Read().phase == Phase::Failed && session.Read().installerPath.empty(), "untrusted beta installer cannot become ready");
            AssertNoPartial(options.cacheRoot);
        }
        for (int scenario = 0; scenario < 2; ++scenario) {
            FakeTransport transport; HashOnlyFixtureVerifier verifier; Session session; auto options = Options(root);
            if (!scenario) options.installedMode = false; else options.windowsBuild = 19045;
            CheckAndDownload(session, options, transport, verifier);
            Require(session.Read().phase == Phase::Unsupported && transport.installers == 0, "unsupported mode/OS cannot install system update");
        }
        {
            FakeTransport transport; HashOnlyFixtureVerifier verifier; Session session; auto options = Options(root); transport.observed = &session;
            CheckAndDownload(session, options, transport, verifier); auto ready = session.Read();
            if (ready.phase != Phase::Ready) std::cerr << "Ready fixture: " << Utf8(ready.message) << '\n';
            Require(ready.phase == Phase::Ready && ready.downloaded == payload.size() && transport.sawDownloading, "streamed download reports progress and becomes ready");
            Require(verifier.calls == 1 && transport.installers == 1, "verification precedes readiness");
            Session later; CheckAndDownload(later, options, transport, verifier);
            Require(later.Read().phase == Phase::Ready && transport.installers == 1 && verifier.calls == 2, "Later cache is reverified and reused after a fresh check");
            transport.feed = Feed("3.25.146.9"); Session rollback;
            CheckAndDownload(rollback, options, transport, verifier);
            Require(rollback.Read().phase == Phase::Failed && transport.installers == 1, "previously verified newer release prevents rollback offer");
            // Restore the same offer, replace the cached bytes, and ensure recheck fails.
            transport.feed = Feed();
            { HANDLE h = CreateFileW(ready.installerPath.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr); Require(h != INVALID_HANDLE_VALUE, "fixture can simulate later cache tampering"); DWORD n = 0; WriteFile(h, "X", 1, &n, nullptr); CloseHandle(h); }
            Session tampered; CheckAndDownload(tampered, options, transport, verifier);
            Require(tampered.Read().phase == Phase::Failed, "cached installer tampering blocks install");
        }
        for (int scenario = 0; scenario < 6; ++scenario) {
            FakeTransport transport; HashOnlyFixtureVerifier verifier; Session session; auto options = Options(root);
            if (scenario == 0) transport.data.pop_back();
            if (scenario == 1) transport.data += "extra";
            if (scenario == 2) transport.data[0] = 'X';
            if (scenario == 3) verifier.failSignature = true;
            if (scenario == 4) transport.cancelDuringDownload = &session.cancel;
            if (scenario == 5) transport.error = "Simulated timeout or HTTP 404.";
            CheckAndDownload(session, options, transport, verifier);
            Require(session.Read().phase == (scenario == 4 ? Phase::Cancelled : Phase::Failed), "download/verification/network failure cannot become ready");
            Require(session.Read().installerPath.empty(), "failed update has no install handoff"); AssertNoPartial(options.cacheRoot);
        }
        if (argc > 3 && std::wstring(argv[3]) == L"--live-feed") {
            HttpTransport transport; std::string response;
            try {
                transport.Get(FeedUrl, true, MaxManifestBytes, [&](const unsigned char* p, size_t n) { response.append(reinterpret_cast<const char*>(p), n); }, cancelled);
                ParseManifest(response); std::cout << "Live feed: valid JSON response.\n";
            } catch (const Error& error) {
                if (std::string(error.what()).find("HTTP 404") == std::string::npos) throw;
                std::cout << "Live feed: unpublished (HTTP 404), handled correctly.\n";
            }
        }
        std::cout << "PASS: " << tests << " updater checks. Signed-ready fixtures use a test verifier; unsigned PE classification and consent gates use production code. No installer is executed.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
