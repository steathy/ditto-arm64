#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include "sqlite3mc_amalgamation.h"
#pragma comment(lib, "user32.lib")

namespace {
void Check(bool ok, const char* operation) {
    if (!ok) throw std::runtime_error(std::string(operation) + " (Windows error " + std::to_string(GetLastError()) + ")");
}
std::string Utf8(const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    Check(n > 0, "encode path"); std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), n, nullptr, nullptr); return result;
}
void Pump(DWORD milliseconds) {
    ULONGLONG end = GetTickCount64() + milliseconds;
    do { MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } Sleep(10); } while (GetTickCount64() < end);
}
void SetText(HWND owner, const std::wstring& value) {
    Check(OpenClipboard(owner) != FALSE, "open private clipboard");
    Check(EmptyClipboard() != FALSE, "empty private clipboard");
    auto block = GlobalAlloc(GMEM_MOVEABLE, (value.size() + 1) * sizeof(wchar_t));
    Check(block != nullptr, "allocate clipboard fixture");
    auto memory = GlobalLock(block); memcpy(memory, value.c_str(), (value.size() + 1) * sizeof(wchar_t)); GlobalUnlock(block);
    bool ok = SetClipboardData(CF_UNICODETEXT, block) != nullptr; CloseClipboard();
    if (!ok) GlobalFree(block); Check(ok, "set private clipboard fixture");
}
std::wstring GetText(HWND owner) {
    if (!OpenClipboard(owner)) return {};
    std::wstring result; auto data = GetClipboardData(CF_UNICODETEXT);
    if (data) { auto memory = static_cast<const wchar_t*>(GlobalLock(data)); if (memory) { result = memory; GlobalUnlock(data); } }
    CloseClipboard(); return result;
}
int CapturedId(const std::wstring& path, const std::wstring& text) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(Utf8(path).c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) { sqlite3_close(db); return 0; }
    sqlite3_busy_timeout(db, 200); sqlite3_stmt* query = nullptr; int id = 0;
    if (sqlite3_prepare_v2(db, "SELECT Main.lID FROM Main JOIN Data ON Data.lParentID=Main.lID WHERE Main.mText=? AND Data.strClipBoardFormat='CF_UNICODETEXT'", -1, &query, nullptr) == SQLITE_OK) {
        auto value = Utf8(text); sqlite3_bind_text(query, 1, value.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(query) == SQLITE_ROW) id = sqlite3_column_int(query, 0);
    }
    sqlite3_finalize(query); sqlite3_close(db); return id;
}
struct Isolation {
    HWINSTA originalStation = GetProcessWindowStation(), station = nullptr;
    HDESK originalDesktop = GetThreadDesktop(GetCurrentThreadId()), desktop = nullptr;
    HWND owner = nullptr, edit = nullptr; HANDLE app = nullptr;
    std::wstring name, desktopName, exe;
    ~Isolation() {
        if (app) {
            try { auto command = Start(L"/Exit"); WaitForSingleObject(command, 10000); CloseHandle(command); } catch (...) {}
            if (WaitForSingleObject(app, 10000) != WAIT_OBJECT_0) { TerminateProcess(app, 1); WaitForSingleObject(app, 5000); }
            CloseHandle(app);
        }
        if (owner) DestroyWindow(owner);
        SetThreadDesktop(originalDesktop); SetProcessWindowStation(originalStation);
        if (desktop) CloseDesktop(desktop); if (station) CloseWindowStation(station);
    }
    HANDLE Start(const std::wstring& arguments) {
        std::wstring command = L"\"" + exe + L"\" " + arguments;
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.lpDesktop = desktopName.data();
        startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION process{}; auto dir = std::filesystem::path(exe).parent_path().wstring();
        Check(CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &startup, &process) != FALSE, "start isolated Ditto");
        CloseHandle(process.hThread); return process.hProcess;
    }
};
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Pass an isolated portable executable and its fixture database.");
        Isolation test; test.exe = argv[1];
        test.name = L"DittoClipboardTest-" + std::to_wstring(GetCurrentProcessId());
        test.station = CreateWindowStationW(test.name.c_str(), 0, WINSTA_ALL_ACCESS, nullptr);
        Check(test.station != nullptr, "create private window station");
        Check(SetProcessWindowStation(test.station) != FALSE, "select private window station");
        test.desktop = CreateDesktopW(L"Test", nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        Check(test.desktop != nullptr && SetThreadDesktop(test.desktop), "select private desktop");
        test.desktopName = test.name + L"\\Test";
        test.owner = CreateWindowExW(0, L"STATIC", L"Ditto clipboard fixture", WS_POPUP, 0, 0, 300, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(test.owner != nullptr, "create private clipboard owner");
        test.edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_MULTILINE, 0, 0, 300, 100, test.owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(test.edit != nullptr, "create private paste target");
        test.app = test.Start(L"");
        Check(WaitForInputIdle(test.app, 20000) == 0, "wait for isolated startup"); Pump(1000);
        const std::wstring sentinel = L"Ditto ARM64 capture/paste fixture caf\u00e9 \u03a9";
        SetText(test.owner, sentinel);
        int id = 0; ULONGLONG deadline = GetTickCount64() + 20000;
        do { Pump(100); id = CapturedId(argv[2], sentinel); } while (!id && GetTickCount64() < deadline);
        Check(id > 0, "real clipboard capture saved Unicode text and its format to SQLite");
        SetText(test.owner, L"replacement fixture"); Pump(500);
        auto command = test.Start(L"/Paste:" + std::to_wstring(id));
        DWORD waited = WaitForSingleObject(command, 15000); CloseHandle(command);
        Check(waited == WAIT_OBJECT_0, "recorded-clip paste command finished");
        deadline = GetTickCount64() + 10000;
        while (GetText(test.owner) != sentinel && GetTickCount64() < deadline) Pump(100);
        Check(GetText(test.owner) == sentinel, "Ditto restored the selected clip from its database");
        SendMessageW(test.edit, WM_PASTE, 0, 0);
        wchar_t pasted[1024]{}; GetWindowTextW(test.edit, pasted, 1024);
        Check(sentinel == pasted, "native edit control consumed the recorded clip");
        std::cout << "PASS: real Unicode clipboard capture, SQLite format storage, recorded-clip restoration and WM_PASTE into a native control on a private window station. Interactive clipboard untouched. Keyboard input delivery is not tested.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
