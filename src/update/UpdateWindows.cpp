#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <wintrust.h>
#include <softpub.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <sddl.h>
#include <aclapi.h>
#include <winternl.h>
#include "UpdateCore.h"
#include "TrustedPublishers.h"
#include <algorithm>
#include <filesystem>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "version.lib")

namespace ditto_update {
namespace fs = std::filesystem;
namespace {
void WinCheck(BOOL good, const char* operation) {
    if (!good) throw Error(std::string(operation) + " (Windows error " + std::to_string(GetLastError()) + ").");
}
struct Internet {
    HINTERNET h = nullptr;
    explicit Internet(HINTERNET value) : h(value) { WinCheck(h != nullptr, "Network operation failed"); }
    ~Internet() { WinHttpCloseHandle(h); }
    Internet(const Internet&) = delete;
};
std::shared_ptr<void> OwnHandle(HANDLE h) {
    WinCheck(h != INVALID_HANDLE_VALUE && h != nullptr, "Could not open update file");
    return {h, [](void* p) { CloseHandle(p); }};
}
class Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
public:
    Hash() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw Error("SHA-256 is unavailable.");
        if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0); throw Error("Could not initialize SHA-256.");
        }
    }
    ~Hash() { BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); }
    void Add(const unsigned char* bytes, size_t size) {
        if (size > MAXDWORD || BCryptHashData(hash, const_cast<PUCHAR>(bytes), static_cast<ULONG>(size), 0) < 0) throw Error("SHA-256 calculation failed.");
    }
    std::string Finish() {
        unsigned char digest[32];
        if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) throw Error("SHA-256 calculation failed.");
        std::string out; const char* hex = "0123456789abcdef";
        for (auto b : digest) { out += hex[b >> 4]; out += hex[b & 15]; }
        return out;
    }
};
std::wstring Header(HINTERNET request, DWORD query) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return {};
    if (bytes == 0 || bytes > 16384) throw Error("Invalid HTTP response header.");
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1);
    WinCheck(WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &bytes, WINHTTP_NO_HEADER_INDEX), "Could not read response header");
    return buffer.data();
}
struct TrustState {
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data{};
    ~TrustState() { data.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr, &action, &data); }
};
std::vector<unsigned char> VersionData(const std::wstring& path) {
    auto native = FileApiPath(path);
    DWORD ignored = 0, bytes = GetFileVersionInfoSizeW(native.c_str(), &ignored);
    if (bytes == 0 || bytes > 1024 * 1024) throw Error("Signed file has no valid version metadata.");
    std::vector<unsigned char> buffer(bytes);
    WinCheck(GetFileVersionInfoW(native.c_str(), 0, bytes, buffer.data()), "Could not read signed version metadata");
    return buffer;
}
void WriteAll(HANDLE file, const unsigned char* data, size_t size) {
    while (size) {
        DWORD written = 0;
        WinCheck(WriteFile(file, data, static_cast<DWORD>(std::min<size_t>(size, MAXDWORD)), &written, nullptr), "Could not write update file");
        if (!written) throw Error("Update file write made no progress.");
        data += written; size -= written;
    }
}
bool Exists(const std::wstring& path) {
    auto attr = GetFileAttributesW(FileApiPath(path).c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) return true;
    if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return false;
    throw Error("Cannot inspect the update cache.");
}
std::wstring HighestVersionPath(const std::wstring& root, Channel channel) {
    // Keep the old stable filename; beta verification must not raise its floor.
    return root + (channel == Channel::Beta ? L"\\highest-version-beta" : L"\\highest-version");
}
void StoreHighestVersion(const std::wstring& root, Channel channel, const Version& version) {
    auto destination = HighestVersionPath(root, channel);
    RejectReparsePath(destination);
    auto temporary = root + L"\\version-" + UniqueName();
    WriteNewFile(temporary, version.String());
    if (!MoveFileExW(FileApiPath(temporary).c_str(), FileApiPath(destination).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(FileApiPath(temporary).c_str()); throw Error("Could not save the accepted update version.");
    }
}
}
std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!n) throw Error("Invalid UTF-8 text.");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw Error("Invalid Unicode text.");
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}
void HttpTransport::Get(const std::wstring& initial, bool manifest, uint64_t limit, const Sink& sink, const std::atomic_bool& cancel) {
    if (manifest ? initial != FeedUrl : !IsInstallerUrl(initial)) throw Error("Unexpected update endpoint.");
    Internet session(WinHttpOpen(L"DittoARM64-Updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    WinCheck(WinHttpSetTimeouts(session.h, 10000, 15000, 15000, 15000), "Could not set network timeouts");
    const ULONGLONG deadline = GetTickCount64() + (manifest ? 120000 : 1800000);
    std::wstring url = initial;
    for (unsigned hop = 0; hop < 5; ++hop) {
        CheckCancelled(cancel);
        if (manifest ? url != FeedUrl : !IsAllowedDownloadUrl(url, hop != 0)) throw Error("Update redirect was rejected.");
        URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        WinCheck(WinHttpCrackUrl(url.c_str(), 0, 0, &parts), "Invalid update URL");
        if (parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT) throw Error("Updates require HTTPS.");
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        Internet connection(WinHttpConnect(session.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
        Internet request(WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        DWORD disabled = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES;
        WinCheck(WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)), "Could not restrict network request");
        DWORD logon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        WinCheck(WinHttpSetOption(request.h, WINHTTP_OPTION_AUTOLOGON_POLICY, &logon, sizeof(logon)), "Could not restrict network authentication");
        const wchar_t* headers = L"Cache-Control: no-cache\r\nPragma: no-cache\r\nAccept-Encoding: identity\r\n";
        WinCheck(WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0), "Could not contact the update server");
        CheckCancelled(cancel);
        WinCheck(WinHttpReceiveResponse(request.h, nullptr), "Could not receive update response");
        DWORD status = 0, statusSize = sizeof(status);
        WinCheck(WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX), "Could not read HTTP status");
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (manifest) throw Error("The update feed unexpectedly redirected.");
            url = Header(request.h, WINHTTP_QUERY_LOCATION); continue;
        }
        if (status == 404) throw Error(manifest ? "Update feed is not published yet (HTTP 404)." : "The installer is no longer available (HTTP 404).");
        if (status == 403 || status == 429) throw Error("GitHub refused or rate-limited this request. Please try again later.");
        if (status != 200) throw Error("Update server returned HTTP " + std::to_string(status) + ".");
        auto encoding = Header(request.h, WINHTTP_QUERY_CONTENT_ENCODING);
        if (!encoding.empty() && encoding != L"identity") throw Error("Unsupported HTTP content encoding.");
        uint64_t total = 0;
        unsigned char buffer[32768];
        for (;;) {
            CheckCancelled(cancel);
            if (GetTickCount64() > deadline) throw Error("Update request timed out.");
            DWORD read = 0;
            WinCheck(WinHttpReadData(request.h, buffer, sizeof(buffer), &read), "Update download was interrupted");
            if (!read) break;
            if (total > limit || read > limit - total) throw Error("Update response exceeds the expected size.");
            total += read; sink(buffer, read);
        }
        CheckCancelled(cancel); return;
    }
    throw Error("Too many update redirects.");
}
std::string HashBytes(const unsigned char* bytes, size_t size) { Hash hash; hash.Add(bytes, size); return hash.Finish(); }
std::wstring FileApiPath(const std::wstring& path) {
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    if (path.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + path.substr(2);
    return L"\\\\?\\" + path;
}
void RejectReparsePath(const std::wstring& path) {
    fs::path p(path);
    if (!p.is_absolute() || path.find(L'\0') != path.npos || path.find(L'"') != path.npos || path.rfind(L"\\\\?\\", 0) == 0 || path.rfind(L"\\\\.\\", 0) == 0) throw Error("Invalid update file path.");
    fs::path current;
    for (const auto& component : p) {
        if (component == L".." || component == L".") throw Error("Relative path components are not allowed.");
        current /= component;
        if (!current.has_root_directory()) continue;
        auto attributes = GetFileAttributesW(FileApiPath(current.wstring()).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) throw Error("Update paths cannot use links or junctions.");
        if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) throw Error("Cannot inspect update path permissions.");
    }
}
std::shared_ptr<void> LockFile(const std::wstring& path) {
    RejectReparsePath(path);
    // Pin the entire directory chain as well as the file. Denying only deletion
    // of the file would still allow an ancestor directory to be swapped out.
    std::vector<std::shared_ptr<void>> directories;
    fs::path parent;
    for (const auto& component : fs::path(path).parent_path()) {
        parent /= component;
        if (!parent.has_root_directory()) continue;
        auto directory = OwnHandle(CreateFileW(FileApiPath(parent.wstring()).c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        WinCheck(GetFileInformationByHandle(directory.get(), &info), "Could not inspect update directory");
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) throw Error("Update path is not a regular directory.");
        directories.push_back(directory);
    }
    auto file = OwnHandle(CreateFileW(FileApiPath(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    WinCheck(GetFileInformationByHandle(file.get(), &info), "Could not inspect update file");
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) throw Error("Update file must be a regular file.");
    return std::shared_ptr<void>(file.get(), [file, directories](void*) {});
}
void VerifyHash(void* file, const Release& release, const std::atomic_bool& cancel) {
    LARGE_INTEGER size{}, start{};
    WinCheck(GetFileSizeEx(file, &size), "Could not read installer size");
    if (static_cast<uint64_t>(size.QuadPart) != release.size) throw Error("Installer size does not match the update feed.");
    WinCheck(SetFilePointerEx(file, start, nullptr, FILE_BEGIN), "Could not read installer");
    Hash hash; unsigned char buffer[65536];
    for (;;) {
        CheckCancelled(cancel); DWORD read = 0;
        WinCheck(ReadFile(file, buffer, sizeof(buffer), &read, nullptr), "Could not read installer");
        if (!read) break; hash.Add(buffer, read);
    }
    if (hash.Finish() != release.sha256) throw Error("Installer SHA-256 does not match. Installation is blocked.");
}
Version FileVersion(const std::wstring& path) {
    auto buffer = VersionData(path); VS_FIXEDFILEINFO* info = nullptr; UINT size = 0;
    if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &size) || size < sizeof(*info) || info->dwSignature != 0xfeef04bd) throw Error("Invalid installer version resource.");
    Version version; version.parts = {HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
    return version;
}
bool SigningConfigured() { return !TrustedPublisherKeys.empty(); }
namespace {
bool HasCertificateTable(void* file) {
    LARGE_INTEGER length{}; WinCheck(GetFileSizeEx(file, &length), "Cannot inspect executable");
    auto read = [&](uint64_t offset, void* out, DWORD bytes) {
        if (offset > static_cast<uint64_t>(length.QuadPart) || bytes > static_cast<uint64_t>(length.QuadPart) - offset) throw Error("Truncated executable header.");
        LARGE_INTEGER position{}; position.QuadPart = offset; DWORD received = 0;
        WinCheck(SetFilePointerEx(file, position, nullptr, FILE_BEGIN) && ReadFile(file, out, bytes, &received, nullptr), "Cannot inspect executable header");
        if (received != bytes) throw Error("Incomplete executable header.");
    };
    IMAGE_DOS_HEADER dos{}; read(0, &dos, sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos)) throw Error("Not a valid Windows executable.");
    DWORD signature = 0; read(dos.e_lfanew, &signature, sizeof(signature));
    if (signature != IMAGE_NT_SIGNATURE) throw Error("Not a valid PE executable.");
    IMAGE_FILE_HEADER header{}; read(dos.e_lfanew + sizeof(signature), &header, sizeof(header));
    uint64_t offset = static_cast<uint64_t>(dos.e_lfanew) + sizeof(signature) + sizeof(header);
    WORD magic = 0; read(offset, &magic, sizeof(magic));
    IMAGE_DATA_DIRECTORY certificate{};
    if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        IMAGE_OPTIONAL_HEADER64 optional{};
        if (header.SizeOfOptionalHeader < sizeof(optional)) throw Error("Invalid PE optional header.");
        read(offset, &optional, sizeof(optional));
        if (optional.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_SECURITY) throw Error("Missing PE directory metadata.");
        certificate = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
    } else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        IMAGE_OPTIONAL_HEADER32 optional{};
        if (header.SizeOfOptionalHeader < sizeof(optional)) throw Error("Invalid PE optional header.");
        read(offset, &optional, sizeof(optional));
        if (optional.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_SECURITY) throw Error("Missing PE directory metadata.");
        certificate = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY];
    } else throw Error("Unsupported executable format.");
    // A damaged/nonempty signature table must never be treated as unsigned.
    return certificate.VirtualAddress != 0 || certificate.Size != 0;
}
void VerifyIdentity(const std::wstring& path, const Version& version, const std::wstring& product) {
    if (!(FileVersion(path) == version)) throw Error("File version does not match the expected version.");
    auto data = VersionData(path);
    struct Translation { WORD language, codepage; };
    Translation* translations = nullptr; UINT bytes = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &bytes) || bytes < sizeof(Translation)) throw Error("Missing product identity.");
    bool found = false;
    for (UINT i = 0; i < bytes / sizeof(Translation); ++i) {
        wchar_t query[80]; swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\ProductName", translations[i].language, translations[i].codepage);
        wchar_t* name = nullptr; UINT chars = 0;
        if (VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&name), &chars) && chars) {
            // Inno Setup pads version-resource strings with trailing spaces.
            std::wstring value(name, chars);
            while (!value.empty() && (value.back() == L'\0' || value.back() == L' ')) value.pop_back();
            if (value == product) found = true;
        }
    }
    if (!found) throw Error("Product identity does not match Ditto ARM64.");
}
}
SignatureStatus InspectPublisher(void* file, const std::wstring& path, const Version& version, const std::wstring& product) {
    VerifyIdentity(path, version, product);
    WINTRUST_FILE_INFO info{}; info.cbStruct = sizeof(info); info.pcwszFilePath = path.c_str(); info.hFile = file;
    TrustState trust; trust.data.cbStruct = sizeof(trust.data);
    trust.data.dwUIChoice = WTD_UI_NONE; trust.data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trust.data.dwUnionChoice = WTD_CHOICE_FILE; trust.data.pFile = &info;
    trust.data.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.data.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | WTD_DISABLE_MD2_MD4;
    LONG status = WinVerifyTrust(nullptr, &trust.action, &trust.data);
    if (status == TRUST_E_NOSIGNATURE && !HasCertificateTable(file)) return SignatureStatus::Unsigned;
    if (status != ERROR_SUCCESS) throw Error("The file has an invalid, untrusted, revoked, or unverifiable signature. Installation is blocked.");
    auto provider = WTHelperProvDataFromStateData(trust.data.hWVTStateData);
    auto signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
    if (!signer || !signer->csCertChain || !signer->pasCertChain[0].pCert) throw Error("Could not identify the verified publisher.");
    BYTE* encoded = nullptr; DWORD encodedSize = 0;
    WinCheck(CryptEncodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, &signer->pasCertChain[0].pCert->pCertInfo->SubjectPublicKeyInfo, CRYPT_ENCODE_ALLOC_FLAG, nullptr, &encoded, &encodedSize), "Could not identify the signing key");
    std::shared_ptr<void> encodedOwner(encoded, [](void* p) { LocalFree(p); });
    auto key = HashBytes(encoded, encodedSize);
    if (!SigningConfigured()) throw Error("A trusted publisher has not been configured for this development build. Installation is blocked.");
    if (std::none_of(TrustedPublisherKeys.begin(), TrustedPublisherKeys.end(), [&](const char* pin) { return key == pin; })) throw Error("The signed file is from a different publisher. Installation is blocked.");
    return SignatureStatus::Trusted;
}
void VerifyPublisher(void* file, const std::wstring& path, const Version& version, const std::wstring& product, bool unsignedConsent) {
    RequireUnsignedConsent(InspectPublisher(file, path, version, product), unsignedConsent);
}
Verification WindowsVerifier::Verify(const std::wstring& path, const Release& release, const std::atomic_bool& cancel) {
    auto file = LockFile(path); VerifyHash(file.get(), release, cancel);
    CheckCancelled(cancel);
    Verification result; result.installerSignature = InspectPublisher(file.get(), path, release.version, L"Ditto ARM64");
    result.requiresUnsignedConsent = result.installerSignature == SignatureStatus::Unsigned;
    if (!application.empty()) {
        auto app = LockFile(application); auto version = FileVersion(application);
        auto helperPath = (fs::path(application).parent_path() / L"DittoUpdate.exe").wstring();
        auto helper = LockFile(helperPath);
        auto appStatus = InspectPublisher(app.get(), application, version, L"Ditto ARM64");
        CheckCancelled(cancel);
        auto helperStatus = InspectPublisher(helper.get(), helperPath, version, L"Ditto ARM64 Update Helper");
        result.requiresUnsignedConsent = result.requiresUnsignedConsent || appStatus == SignatureStatus::Unsigned || helperStatus == SignatureStatus::Unsigned;
    }
    CheckCancelled(cancel); return result;
}
void EnsurePrivateDirectory(const std::wstring& path) {
    RejectReparsePath(path);
    HANDLE rawToken = nullptr; WinCheck(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken), "Could not identify the current user");
    auto token = OwnHandle(rawToken); DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> data(size);
    WinCheck(GetTokenInformation(token.get(), TokenUser, data.data(), size, &size), "Could not identify the current user");
    wchar_t* sid = nullptr;
    WinCheck(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid), "Could not create private update cache");
    std::shared_ptr<void> sidOwner(sid, [](void* p) { LocalFree(p); });
    std::wstring sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + std::wstring(sid) + L")";
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    WinCheck(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr), "Could not secure update cache");
    std::shared_ptr<void> descriptorOwner(descriptor, [](void* p) { LocalFree(p); });
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    auto native = FileApiPath(path);
    if (!CreateDirectoryW(native.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS) throw Error("Could not create private update cache.");
    RejectReparsePath(path);
    if (!(GetFileAttributesW(native.c_str()) & FILE_ATTRIBUTE_DIRECTORY)) throw Error("Update cache is not a directory.");
    BOOL present = FALSE, defaulted = FALSE; PACL acl = nullptr;
    WinCheck(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted), "Could not secure update cache");
    if (SetNamedSecurityInfoW(native.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr) != ERROR_SUCCESS) throw Error("Could not secure update cache permissions.");
}
std::wstring PrivateCacheRoot() {
    wchar_t* local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) throw Error("Could not find the local update cache.");
    std::wstring path = std::wstring(local) + L"\\DittoARM64-Updates"; CoTaskMemFree(local);
    if (path.rfind(L"\\\\", 0) == 0) throw Error("Updates require a local cache directory.");
    EnsurePrivateDirectory(path); return path;
}
std::wstring UniqueName() {
    GUID id; if (FAILED(CoCreateGuid(&id))) throw Error("Could not create a unique update name.");
    wchar_t value[40]; StringFromGUID2(id, value, 40); return value;
}
std::string ReadSmallFile(const std::wstring& path) {
    auto file = LockFile(path); LARGE_INTEGER size{};
    WinCheck(GetFileSizeEx(file.get(), &size), "Could not read cached metadata");
    if (size.QuadPart < 0 || size.QuadPart > MaxManifestBytes) throw Error("Cached metadata is too large.");
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
    WinCheck(ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr), "Could not read cached metadata");
    if (read != bytes.size()) throw Error("Incomplete cached metadata."); return bytes;
}
void WriteNewFile(const std::wstring& path, const std::string& bytes) {
    RejectReparsePath(path);
    auto file = OwnHandle(CreateFileW(FileApiPath(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    WriteAll(file.get(), reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
    WinCheck(FlushFileBuffers(file.get()), "Could not save update metadata");
}
std::string ConsumeInstallTicket(const std::wstring& path) {
    RejectReparsePath(path);
    auto file = OwnHandle(CreateFileW(FileApiPath(path).c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    LARGE_INTEGER size{}; WinCheck(GetFileSizeEx(file.get(), &size), "Cannot read install handoff");
    if (size.QuadPart <= 0 || size.QuadPart > MaxManifestBytes) throw Error("Invalid install handoff size.");
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0'); DWORD read = 0;
    WinCheck(ReadFile(file.get(), bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr), "Cannot read install handoff");
    if (read != bytes.size()) throw Error("Incomplete install handoff.");
    return bytes;
}
uint32_t WindowsBuild() {
    using GetVersion = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto function = reinterpret_cast<GetVersion>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    RTL_OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
    if (!function || function(&version) != 0) throw Error("Could not identify the Windows version.");
    return version.dwBuildNumber;
}
std::wstring RegisteredInstallDirectory() {
    wchar_t path[32768]; DWORD bytes = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Ditto_is1", L"InstallLocation", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, path, &bytes) != ERROR_SUCCESS) return {};
    std::wstring result(path); while (!result.empty() && (result.back() == L'\\' || result.back() == L'/')) result.pop_back();
    return result;
}
std::shared_ptr<void> LaunchInstallerProcess(const std::wstring& installer, const std::wstring& parameters) {
    auto directory = fs::path(installer).parent_path().wstring();
    SHELLEXECUTEINFOW launch{}; launch.cbSize = sizeof(launch);
    // Without NO_UI, a shell error can wait inside an invisible dialog before
    // returning to our recovery code. Security/UAC prompts are not suppressed.
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    launch.lpVerb = L"runas"; launch.lpFile = installer.c_str();
    launch.lpParameters = parameters.c_str(); launch.lpDirectory = directory.c_str();
    launch.nShow = SW_SHOWNORMAL;
    SetLastError(ERROR_SUCCESS);
    if (!ShellExecuteExW(&launch)) {
        const DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) throw Error("Installation permission was cancelled (Windows error 1223). Your existing Ditto will restart.");
        wchar_t detail[2048]{};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
            error, 0, detail, static_cast<DWORD>(std::size(detail)), nullptr);
        std::wstring reason(detail);
        while (!reason.empty() && (reason.back() == L'\r' || reason.back() == L'\n' || reason.back() == L' ')) reason.pop_back();
        throw Error("Windows could not launch the verified installer (Windows error " + std::to_string(error)
            + ", shell result " + std::to_string(reinterpret_cast<intptr_t>(launch.hInstApp)) + ")."
            + (reason.empty() || !error ? "" : " " + Utf8(reason)));
    }
    if (!launch.hProcess) throw Error("Windows did not return an installer process. Installation could not be monitored.");
    return OwnHandle(launch.hProcess);
}
void CheckAndDownload(Session& session, const CheckOptions& options, Transport& transport, InstallerVerifier& verifier) {
    std::wstring partial;
    try {
        session.Set(Phase::Checking, L"Checking for ARM64 updates...");
        std::string bytes;
        transport.Get(FeedUrl, true, MaxManifestBytes, [&](const unsigned char* data, size_t size) {
            CheckCancelled(session.cancel);
            if (size > MaxManifestBytes - bytes.size()) throw Error("Update feed is too large.");
            bytes.append(reinterpret_cast<const char*>(data), size);
        }, session.cancel);
        CheckCancelled(session.cancel);
        auto manifest = ParseManifest(bytes, options.includeBeta);
        if (!manifest.hasRelease) { session.Set(Phase::NoRelease, options.includeBeta
            ? L"No stable or beta ARM64 release has been published yet."
            : L"No stable ARM64 release has been published yet."); return; }
        const auto& r = manifest.release;
        { std::lock_guard<std::mutex> lock(session.mutex); session.state.release = r; }
        if (r.version == options.installed) { session.Set(Phase::Current, L"No newer version is available for your update preference."); return; }
        if (r.version < options.installed) { session.Set(Phase::NewerInstalled, L"Your installed build is newer than the latest offer. No downgrade will be installed."); return; }
        if (r.minimumWindowsBuild > options.windowsBuild) { session.Set(Phase::Unsupported, L"This update needs a newer version of Windows."); return; }
        if (!options.installedMode) { session.Set(Phase::Unsupported, L"Update available. Automatic installation supports regular installed builds only; use release notes for other distributions."); return; }
        EnsurePrivateDirectory(options.cacheRoot);
        auto highest = HighestVersionPath(options.cacheRoot, r.channel);
        if (Exists(highest) && r.version < Version::Parse(ReadSmallFile(highest))) throw Error("The feed offered an older version than a previously verified update.");
        auto directory = options.cacheRoot + L"\\" + Wide(std::string(ChannelKey(r.channel)) + "-" + r.version.String() + "-" + r.sha256);
        EnsurePrivateDirectory(directory);
        auto target = directory + L"\\installer.exe";
        Verification verification;
        if (!Exists(target)) {
            ULARGE_INTEGER free{};
            WinCheck(GetDiskFreeSpaceExW(directory.c_str(), &free, nullptr, nullptr), "Could not check disk space");
            if (free.QuadPart < r.size + 64 * 1024 * 1024) throw Error("Not enough free space to download the update.");
            partial = directory + L"\\" + UniqueName() + L".partial";
            {
                auto output = OwnHandle(CreateFileW(FileApiPath(partial).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
                session.Set(Phase::Downloading, L"Downloading Ditto ARM64 " + Wide(r.version.String()) + L"...");
                uint64_t received = 0;
                transport.Get(r.url, false, r.size, [&](const unsigned char* data, size_t size) {
                    CheckCancelled(session.cancel);
                    if (size > r.size - received) throw Error("Installer is larger than the feed declares.");
                    WriteAll(output.get(), data, size); received += size;
                    std::lock_guard<std::mutex> lock(session.mutex); session.state.downloaded = received;
                }, session.cancel);
                if (received != r.size) throw Error("Installer download is incomplete.");
                WinCheck(FlushFileBuffers(output.get()), "Could not save the downloaded installer");
            }
            session.Set(Phase::Verifying, L"Verifying installer hash and publisher signature...");
            verification = verifier.Verify(partial, r, session.cancel);
            CheckCancelled(session.cancel);
            WinCheck(MoveFileExW(FileApiPath(partial).c_str(), FileApiPath(target).c_str(), MOVEFILE_WRITE_THROUGH), "Could not store the verified installer");
            partial.clear();
        } else {
            session.Set(Phase::Verifying, L"Rechecking the cached installer hash and signature...");
            verification = verifier.Verify(target, r, session.cancel);
        }
        auto manifestPath = directory + L"\\release-" + UniqueName() + L".json";
        WriteNewFile(manifestPath, bytes);
        if (verification.installerSignature == SignatureStatus::Unknown) throw Error("Installer verification did not complete.");
        verification.requiresUnsignedConsent = verification.requiresUnsignedConsent || verification.installerSignature == SignatureStatus::Unsigned;
        // An unsigned offer has not authenticated its publisher. Merely checking
        // one must not raise the rollback floor before the user even approves it.
        if (verification.installerSignature == SignatureStatus::Trusted) StoreHighestVersion(options.cacheRoot, r.channel, r.version);
        CheckCancelled(session.cancel);
        { std::lock_guard<std::mutex> lock(session.mutex); session.state.installerPath = target; session.state.manifestPath = manifestPath; session.state.downloaded = r.size; session.state.verification = verification; }
        session.Set(Phase::Ready, L"Download checked and ready. Choose Install now to review and continue.");
    } catch (const Cancelled&) { session.Set(Phase::Cancelled, L"Update cancelled. Ditto is still running."); }
    catch (const std::exception& error) { session.Set(Phase::Failed, session.cancel.load() ? L"Update cancelled. Ditto is still running." : Wide(error.what())); }
    if (!partial.empty()) DeleteFileW(FileApiPath(partial).c_str());
}
void StartCheck(const std::shared_ptr<Session>& session, const CheckOptions& options) {
    std::thread([session, options] { HttpTransport transport; WindowsVerifier verifier(options.executable); CheckAndDownload(*session, options, transport, verifier); }).detach();
}
void LaunchInstallHelper(const Snapshot& ready, const std::wstring& executable, const std::wstring& database, uint32_t parentPid, uintptr_t mainWindow, bool unsignedConsent) {
    if (ready.phase != Phase::Ready) throw Error("The update is not ready to install.");
    RequireUnsignedConsent(ready.verification.installerSignature, unsignedConsent);
    if (ready.verification.requiresUnsignedConsent && !unsignedConsent) throw Error("Unsigned installation has not been approved.");
    auto installer = LockFile(ready.installerPath); std::atomic_bool cancel(false);
    VerifyHash(installer.get(), ready.release, cancel);
    VerifyPublisher(installer.get(), ready.installerPath, ready.release.version, L"Ditto ARM64", unsignedConsent);
    auto directory = PrivateCacheRoot() + L"\\install-" + UniqueName(); EnsurePrivateDirectory(directory);
    auto originalHelper = (fs::path(executable).parent_path() / L"DittoUpdate.exe").wstring();
    auto helper = directory + L"\\DittoUpdate.exe";
    auto original = LockFile(originalHelper);
    VerifyPublisher(original.get(), originalHelper, FileVersion(executable), L"Ditto ARM64 Update Helper", unsignedConsent);
    WinCheck(CopyFileW(FileApiPath(originalHelper).c_str(), FileApiPath(helper).c_str(), TRUE), "Could not prepare the update helper");
    auto helperLock = LockFile(helper);
    VerifyPublisher(helperLock.get(), helper, FileVersion(executable), L"Ditto ARM64 Update Helper", unsignedConsent);
    auto ticket = directory + L"\\install.json";
    WriteNewFile(ticket, "{\"schemaVersion\":3,\"unsignedConsent\":" + std::string(unsignedConsent ? "1" : "0") + ",\"channel\":" + JsonQuote(Wide(ChannelKey(ready.release.channel))) + ",\"parentPid\":" + std::to_string(parentPid) + ",\"mainWindow\":" + std::to_string(mainWindow) + ",\"executable\":" + JsonQuote(executable) + ",\"database\":" + JsonQuote(database) + ",\"manifest\":" + JsonQuote(ready.manifestPath) + ",\"installer\":" + JsonQuote(ready.installerPath) + "}");
    std::wstring command = L"\"" + helper + L"\" --install \"" + ticket + L"\"";
    // This is a GUI helper with no console. Its first error dialog must be visible.
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION process{};
    WinCheck(CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.c_str(), &startup, &process), "Could not start the update helper");
    CloseHandle(process.hThread);
    auto helperProcess = OwnHandle(process.hProcess);
    WaitForSingleObject(helperProcess.get(), INFINITE);
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(helperProcess.get(), &exitCode) || exitCode != 0)
        throw Error("The update helper could not finish. Ditto has been left running; see the helper's error message.");
}
} // namespace ditto_update
