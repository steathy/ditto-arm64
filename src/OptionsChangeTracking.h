#pragma once
#include <commctrl.h>

// Shared predicate lets the native probe check the exact notifications observed
// by the Options pages. Selection/highlighting in lists is not an edit.
inline bool IsOptionsChange(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND && lParam) {
        wchar_t name[64]{};
        GetClassNameW(reinterpret_cast<HWND>(lParam), name, 64);
        auto code = HIWORD(wParam);
        if (!_wcsicmp(name, L"Edit") || !_wcsicmp(name, HOTKEY_CLASSW)) return code == EN_CHANGE;
        if (!_wcsicmp(name, L"ComboBox")) return code == CBN_SELCHANGE || code == CBN_EDITCHANGE;
        if (!_wcsicmp(name, L"Button")) return code == BN_CLICKED;
        if (!_wcsicmp(name, L"ListBox")) return code == LBN_SELCHANGE;
    }
    if (message == WM_NOTIFY && lParam) {
        auto header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->code == LVN_ITEMCHANGED) {
            auto item = reinterpret_cast<const NMLISTVIEW*>(lParam);
            return (item->uChanged & LVIF_STATE) && ((item->uOldState ^ item->uNewState) & LVIS_STATEIMAGEMASK);
        }
        if (header->code == LVN_ENDLABELEDITW) return reinterpret_cast<const NMLVDISPINFOW*>(lParam)->item.pszText != nullptr;
    }
    return (message == WM_HSCROLL || message == WM_VSCROLL) && lParam != 0;
}
