#include "UpdateCore.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace ditto_update {
const wchar_t* const FeedUrl = L"https://raw.githubusercontent.com/steathy/ditto-arm64/main/updates/arm64.json";
namespace {
class Parser {
    const std::string& s;
    size_t at = 0;
    void Space() { while (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\r' || s[at] == '\n')) ++at; }
    bool Take(char c) { Space(); if (at < s.size() && s[at] == c) { ++at; return true; } return false; }
    void Need(char c) { if (!Take(c)) throw Error("Invalid update JSON syntax."); }
    unsigned Hex4() {
        unsigned n = 0;
        for (int i = 0; i < 4; ++i) {
            if (at == s.size()) throw Error("Incomplete JSON escape.");
            char c = s[at++];
            unsigned v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99;
            if (v > 15) throw Error("Invalid JSON escape.");
            n = n * 16 + v;
        }
        return n;
    }
    std::string String() {
        Need('"');
        std::string out;
        while (at < s.size()) {
            unsigned char c = s[at++];
            if (c == '"') { Wide(out); return out; }
            if (c < 32) throw Error("Control character in JSON string.");
            if (c != '\\') { out += static_cast<char>(c); continue; }
            if (at == s.size()) throw Error("Incomplete JSON escape.");
            switch (s[at++]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned hi = Hex4();
                std::wstring w(1, static_cast<wchar_t>(hi));
                if (hi >= 0xd800 && hi <= 0xdbff) {
                    if (at + 2 > s.size() || s[at++] != '\\' || s[at++] != 'u') throw Error("Invalid Unicode surrogate.");
                    unsigned lo = Hex4();
                    if (lo < 0xdc00 || lo > 0xdfff) throw Error("Invalid Unicode surrogate.");
                    w += static_cast<wchar_t>(lo);
                } else if (hi >= 0xdc00 && hi <= 0xdfff) throw Error("Invalid Unicode surrogate.");
                out += Utf8(w);
                break;
            }
            default: throw Error("Invalid JSON escape.");
            }
        }
        throw Error("Unterminated JSON string.");
    }
    Json Value(unsigned depth) {
        if (depth > 12) throw Error("Update JSON is nested too deeply.");
        Space();
        if (at == s.size()) throw Error("Incomplete update JSON.");
        Json j;
        if (Take('{')) {
            j.kind = Json::Kind::Object;
            if (Take('}')) return j;
            do {
                auto key = String(); Need(':');
                if (!j.object.emplace(key, Value(depth + 1)).second) throw Error("Duplicate JSON key.");
            } while (Take(','));
            Need('}');
        } else if (s[at] == '"') { j.kind = Json::Kind::String; j.string = String(); }
        else if (s.compare(at, 4, "null") == 0) { at += 4; }
        else if (s[at] >= '0' && s[at] <= '9') {
            j.kind = Json::Kind::Number;
            size_t begin = at;
            while (at < s.size() && s[at] >= '0' && s[at] <= '9') {
                unsigned digit = s[at++] - '0';
                if (j.number > ((std::numeric_limits<uint64_t>::max)() - digit) / 10) throw Error("JSON integer overflow.");
                j.number = j.number * 10 + digit;
            }
            if (at - begin > 1 && s[begin] == '0') throw Error("Leading zero in JSON number.");
        } else throw Error("Unsupported update JSON value.");
        return j;
    }
public:
    explicit Parser(const std::string& text) : s(text) {}
    Json Run() {
        if (s.size() >= 3 && s.compare(0, 3, "\xef\xbb\xbf") == 0) at = 3;
        auto j = Value(0); Space();
        if (at != s.size()) throw Error("Extra data after update JSON.");
        return j;
    }
};
bool Prefix(const std::wstring& s, const std::wstring& p) { return s.compare(0, p.size(), p) == 0; }
bool SafePath(const std::wstring& s) {
    if (s.empty() || s.find(L"..") != s.npos || s.find(L"//") != s.npos) return false;
    return std::all_of(s.begin(), s.end(), [](wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'/' || c == L'.' || c == L'_' || c == L'-'; });
}
void OnlyKeys(const Json& j, std::initializer_list<const char*> keys) {
    if (j.kind != Json::Kind::Object) throw Error("Expected JSON object.");
    for (const auto& entry : j.object)
        if (std::none_of(keys.begin(), keys.end(), [&](const char* k) { return entry.first == k; })) throw Error("Unknown field in update schema.");
}
}
const Json& Json::At(const char* key) const {
    if (kind != Kind::Object) throw Error("Expected JSON object.");
    auto it = object.find(key);
    if (it == object.end()) throw Error(std::string("Missing update field: ") + key);
    return it->second;
}
std::string Json::Text() const { if (kind != Kind::String) throw Error("Expected JSON string."); return string; }
uint64_t Json::Number() const { if (kind != Kind::Number) throw Error("Expected JSON integer."); return number; }
Json ParseJson(const std::string& bytes) {
    if (bytes.size() > MaxManifestBytes) throw Error("Update JSON exceeds the size limit.");
    return Parser(bytes).Run();
}
std::string JsonQuote(const std::wstring& text) {
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : Utf8(text)) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += c;
    }
    return out + "\"";
}
Version Version::Parse(const std::string& s) {
    Version v; size_t at = 0;
    for (size_t i = 0; i < 4; ++i) {
        size_t start = at; unsigned n = 0;
        while (at < s.size() && s[at] >= '0' && s[at] <= '9') {
            n = n * 10 + s[at++] - '0';
            if (n > 65535 || at - start > 5) throw Error("Invalid version component.");
        }
        if (at == start || (at - start > 1 && s[start] == '0')) throw Error("Invalid version number.");
        v.parts[i] = static_cast<uint16_t>(n);
        if (i < 3 && (at == s.size() || s[at++] != '.')) throw Error("Version must have four components.");
    }
    if (at != s.size()) throw Error("Invalid version suffix.");
    return v;
}
std::string Version::String() const {
    return std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]) + "." + std::to_string(parts[3]);
}
bool IsInstallerUrl(const std::wstring& url) {
    const std::wstring prefix = L"https://github.com/steathy/ditto-arm64/releases/download/";
    if (!Prefix(url, prefix)) return false;
    auto tail = url.substr(prefix.size()); auto slash = tail.find(L'/');
    return SafePath(tail) && slash != tail.npos && slash > 0 && tail.find(L'/', slash + 1) == tail.npos && tail.size() > slash + 5 && tail.substr(tail.size() - 4) == L".exe";
}
bool IsReleaseNotesUrl(const std::wstring& url) {
    const std::wstring prefix = L"https://github.com/steathy/ditto-arm64/releases/tag/";
    return Prefix(url, prefix) && SafePath(url.substr(prefix.size())) && url.substr(prefix.size()).find(L'/') == url.npos;
}
bool IsAllowedDownloadUrl(const std::wstring& url, bool redirect) {
    if (IsInstallerUrl(url)) return true;
    if (!redirect) return false;
    const std::wstring prefix = L"https://release-assets.githubusercontent.com/";
    if (!Prefix(url, prefix) || url.size() > 8192) return false;
    auto query = url.find(L'?');
    auto path = url.substr(prefix.size(), query == url.npos ? url.npos : query - prefix.size());
    if (!SafePath(path)) return false;
    return std::none_of(url.begin(), url.end(), [](wchar_t c) { return c <= 32 || c >= 127 || c == L'\\' || c == L'#' || c == L'"'; });
}
const char* ChannelKey(Channel channel) { return channel == Channel::Beta ? "beta" : "stable"; }
const wchar_t* BuildLabel(Channel channel) { return channel == Channel::Beta ? L"Nightly Build" : L"Stable Build"; }
Channel ParseChannel(const std::string& channel) {
    if (channel == "stable") return Channel::Stable;
    if (channel == "beta") return Channel::Beta;
    throw Error("Unknown update channel.");
}
namespace {
Manifest ParseRelease(const Json& j, Channel channel) {
    Manifest result;
    if (j.kind == Json::Kind::Null) return result;
    OnlyKeys(j, {"version", "publishedAt", "minimumWindowsBuild", "releaseNotesUrl", "releaseNotes", "installer"});
    result.hasRelease = true; auto& r = result.release;
    r.channel = channel;
    r.version = Version::Parse(j.At("version").Text());
    auto date = j.At("publishedAt").Text();
    if (date.size() != 20 || date[4] != '-' || date[7] != '-' || date[10] != 'T' || date[13] != ':' || date[16] != ':' || date[19] != 'Z') throw Error("Invalid publication date.");
    for (size_t i = 0; i < date.size(); ++i) if (i != 4 && i != 7 && i != 10 && i != 13 && i != 16 && i != 19 && (date[i] < '0' || date[i] > '9')) throw Error("Invalid publication date.");
    auto minimum = j.At("minimumWindowsBuild").Number();
    if (minimum < 22000 || minimum > 1000000) throw Error("Invalid minimum Windows build.");
    r.minimumWindowsBuild = static_cast<uint32_t>(minimum);
    r.notesUrl = Wide(j.At("releaseNotesUrl").Text());
    if (!IsReleaseNotesUrl(r.notesUrl)) throw Error("Release notes URL is outside this project.");
    auto notes = j.object.find("releaseNotes");
    if (notes != j.object.end()) {
        auto text = notes->second.Text();
        if (text.size() > 8192 || text.find('\0') != text.npos) throw Error("Invalid release notes.");
        r.notes = Wide(text);
    }
    const auto& installer = j.At("installer"); OnlyKeys(installer, {"url", "sha256", "size"});
    r.url = Wide(installer.At("url").Text());
    if (!IsInstallerUrl(r.url)) throw Error("Installer URL is outside this project's GitHub Releases.");
    r.sha256 = installer.At("sha256").Text();
    if (r.sha256.size() != 64 || !std::all_of(r.sha256.begin(), r.sha256.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })) throw Error("Invalid SHA-256 hash.");
    std::transform(r.sha256.begin(), r.sha256.end(), r.sha256.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    r.size = installer.At("size").Number();
    if (r.size == 0 || r.size > MaxInstallerBytes) throw Error("Installer size exceeds the supported limit.");
    return result;
}
}
Manifest ParseManifest(const std::string& bytes, bool includeBeta) {
    const auto root = ParseJson(bytes);
    OnlyKeys(root, {"schemaVersion", "appId", "platform", "architecture", "channels"});
    if (root.At("schemaVersion").Number() != 2 || root.At("appId").Text() != "steathy.ditto-arm64" || root.At("platform").Text() != "windows" || root.At("architecture").Text() != "arm64") throw Error("This feed is not for Ditto Windows ARM64 updates.");
    const auto& channels = root.At("channels");
    OnlyKeys(channels, {"stable", "beta"});
    auto stable = ParseRelease(channels.At("stable"), Channel::Stable);
    auto beta = ParseRelease(channels.At("beta"), Channel::Beta);
    if (includeBeta && beta.hasRelease && (!stable.hasRelease || stable.release.version < beta.release.version)) return beta;
    return stable;
}
void CheckCancelled(const std::atomic_bool& cancel) { if (cancel.load()) throw Cancelled(); }
void RequireUnsignedConsent(SignatureStatus status, bool consent) {
    if (status == SignatureStatus::Unknown) throw Error("The file has not been verified.");
    if (status == SignatureStatus::Unsigned && !consent) throw Error("This file is unsigned. Review the warning and choose Install unsigned update to continue.");
}
Snapshot Session::Read() const { std::lock_guard<std::mutex> lock(mutex); return state; }
void Session::Set(Phase phase, const std::wstring& message) { std::lock_guard<std::mutex> lock(mutex); state.phase = phase; state.message = message; }
} // namespace ditto_update
