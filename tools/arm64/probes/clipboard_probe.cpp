#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <prsht.h>
#include <string>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include "sqlite3mc_amalgamation.h"
#include "../../../resource.h"
#include "../../../src/update/UpdateInstall.h"
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
HWND OptionsWindow(DWORD pid) {
    struct Search { DWORD pid; HWND window = nullptr; } search{pid};
    EnumWindows([](HWND window, LPARAM value) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(value); DWORD owner = 0; GetWindowThreadProcessId(window, &owner);
        if (owner == search.pid && GetPropW(window, L"Ditto.Options.ChangeState")) { search.window = window; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}
void CheckOptionsPages(HWND main, DWORD pid) {
    auto open = [&] {
        Check(PostMessageW(main, WM_COMMAND, ID_FIRST_OPTION, 0) != FALSE, "open real Options sheet");
        HWND sheet = nullptr; auto end = GetTickCount64() + 5000;
        do { Pump(50); sheet = OptionsWindow(pid); } while (!sheet && GetTickCount64() < end);
        // STARTF_USESHOWWINDOW/SW_HIDE deliberately hides this test process's
        // first window. Visibility is not a readiness condition on this desktop.
        Check(sheet != nullptr, "find real Options sheet with pending-change metadata"); return sheet;
    };
    auto clean = [](HWND sheet) { return reinterpret_cast<uintptr_t>(GetPropW(sheet, L"Ditto.Options.ChangeState")) == 1; };
    auto dirty = [](HWND sheet) { return reinterpret_cast<uintptr_t>(GetPropW(sheet, L"Ditto.Options.ChangeState")) == 2; };
    auto select = [&](HWND sheet, int index) {
        Check(SendMessageW(sheet, PSM_SETCURSEL, index, 0) != FALSE, "select real Options page"); Pump(100);
        auto page = reinterpret_cast<HWND>(SendMessageW(sheet, PSM_GETCURRENTPAGEHWND, 0, 0));
        Check(page != nullptr, "get selected Options page"); return page;
    };
    auto close = [&](HWND sheet, int button) {
        Check(PostMessageW(sheet, PSM_PRESSBUTTON, button, 0) != FALSE, "close real Options sheet");
        auto end = GetTickCount64() + 5000; while (IsWindow(sheet) && GetTickCount64() < end) Pump(50);
        if (IsWindow(sheet)) {
            wchar_t title[256]{}, type[128]{}; GetWindowTextW(sheet, title, 256); GetClassNameW(sheet, type, 128);
            std::wcerr << L"Remaining fixture window: " << type << L" / " << title << L"\n";
        }
        Check(!IsWindow(sheet), "Options close completed");
    };
    HWND sheet = open();
    // General, Types, Keyboard, Copy Buffers, Quick Paste, Friends, Stats, About.
    for (int i = 0; i < 8; ++i) { select(sheet, i); Check(clean(sheet), "initializing an actual Options page stays clean"); }
    auto about = select(sheet, 7);
    RECT area{}, previous{}; GetClientRect(about, &area);
    for (int id : {IDC_CHECK_UPDATE, IDC_INSTALL_UPDATE, IDC_MANAGE_UPDATE_FILES}) {
        RECT button{}; auto control = GetDlgItem(about, id); Check(control != nullptr, "About action exists");
        GetWindowRect(control, &button); MapWindowPoints(nullptr, about, reinterpret_cast<POINT*>(&button), 2);
        Check(button.left >= previous.right && button.right <= area.right && button.bottom <= area.bottom, "About buttons fit without overlaps or clipping");
        if (previous.right) Check(button.top == previous.top && button.bottom == previous.bottom, "About buttons share one aligned row");
        previous = button;
    }
    // MapDialogRect is process-local. Compare independently measured rectangles
    // against the resource's 132:16 DLU ratio instead (allow pixel rounding).
    RECT list{}; GetWindowRect(GetDlgItem(about, IDC_LIST), &list);
    Check(std::abs((list.bottom - list.top) * 16 - (previous.bottom - previous.top) * 132) <= 148, "About preserves the original information-list height");
    SendMessageW(GetDlgItem(about, IDC_UPDATE_INCLUDE_BETA), BM_CLICK, 0, 0); Pump(50);
    Check(clean(sheet), "actual About immediate preference does not mark Options dirty");
    close(sheet, PSBTN_CANCEL);
    for (int i = 0; i < 6; ++i) {
        sheet = open(); auto page = select(sheet, i); Check(clean(sheet), "new actual Options page starts clean");
        switch (i) {
        case 0: Check(SendMessageW(GetDlgItem(page, IDC_MAX_SAVED_COPIES), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(L"501")) != FALSE, "change real General edit"); break;
        case 1:
            SendMessageW(GetDlgItem(page, IDC_LIST1), LB_SETSEL, TRUE, 0);
            SendMessageW(GetDlgItem(page, IDC_DELETE), BM_CLICK, 0, 0); break;
        case 2: SendMessageW(GetDlgItem(page, IDC_CHECK_SEND_PASTE), BM_CLICK, 0, 0); break;
        case 3:
            SendMessageW(GetDlgItem(page, IDC_COPY_1), WM_KEYDOWN, VK_F6, 1);
            SendMessageW(GetDlgItem(page, IDC_COPY_1), WM_KEYUP, VK_F6, 1); break;
        case 4: SendMessageW(GetDlgItem(page, IDC_CHECK_ENABLE_SECOND_PRESS), BM_CLICK, 0, 0); break;
        case 5: SendMessageW(GetDlgItem(page, IDC_CHECK_LOG_SEND_RECIEVE), BM_CLICK, 0, 0); break;
        }
        Pump(50);
        Check(dirty(sheet), ("representative edit on actual Options page " + std::to_string(i + 1) + " marks pending changes").c_str());
        select(sheet, 7); Check(dirty(sheet), "actual About page preserves other pages' unsaved state");
        close(sheet, PSBTN_CANCEL);
        std::cout << "PASS: real Options page " << i + 1 << " initialization, edit notification and Cancel isolation.\n";
    }
    // General::OnApply writes the system startup entry even in portable mode.
    // Saving is covered by the separate tracking fixture; real-page integration
    // deliberately uses Cancel and never exercises that system-wide write.
    sheet = open(); select(sheet, 0); Check(clean(sheet), "real Options Cancel/reopen clears pending state"); select(sheet, 7);
    // Leave this clean sheet open for the following real shutdown test, which
    // must close it along with Ditto without applying any edits.
}
}
int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
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
        auto main = FindWindowW(L"Ditto", nullptr); DWORD owner = 0; GetWindowThreadProcessId(main, &owner);
        Check(main != nullptr && owner == GetProcessId(test.app), "identify private Ditto main window");
        CheckOptionsPages(main, owner);
        {
            ditto_update::ShutdownRequest close(owner); Check(close.Cancel(), "withdraw private update shutdown");
            close.Post(reinterpret_cast<uintptr_t>(main)); Pump(200);
            Check(IsWindow(main) && WaitForSingleObject(test.app, 0) == WAIT_TIMEOUT, "actual Ditto ignores a cancelled queued close");
        }
        {
            ditto_update::ShutdownRequest close(owner); close.Post(reinterpret_cast<uintptr_t>(main));
            Check(WaitForSingleObject(test.app, 20000) == WAIT_OBJECT_0, "actual Ditto accepts committed update shutdown");
            CloseHandle(test.app); test.app = nullptr;
        }
        std::cout << "PASS: six real editable Options pages, clean initialization of all eight pages, immediate preference isolation, Cancel/reopen, and cancellable update-shutdown protocol. Real Options saving is excluded because General writes the system startup entry.\n";
        std::cout << "PASS: real Unicode clipboard capture, SQLite format storage, recorded-clip restoration and WM_PASTE into a native control on a private window station. Interactive clipboard untouched. Keyboard input delivery is not tested.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
