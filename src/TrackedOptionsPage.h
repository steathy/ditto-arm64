#pragma once
#include <afxdlgs.h>
#include <set>
#include "OptionsChangeTracking.h"

class COptionsDirtyState {
    std::set<HWND> pages;
public:
    void Observe(UINT message, WPARAM page) {
        if (message == PSM_CHANGED) pages.insert(reinterpret_cast<HWND>(page));
        else if (message == PSM_UNCHANGED) pages.erase(reinterpret_cast<HWND>(page));
    }
    bool Pending() const { return !pages.empty(); }
};

// Observe changes at the page boundary even when existing handlers bypass MFC's
// default handler. Initial control population is not a pending user edit.
template<class Page> class CTrackedOptionsPage : public Page {
    bool initialized = false;
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        if (initialized && IsOptionsChange(message, wParam, lParam)) this->SetModified(TRUE);
        LRESULT result = Page::WindowProc(message, wParam, lParam);
        if (message == WM_INITDIALOG) initialized = true;
        return result;
    }
};
