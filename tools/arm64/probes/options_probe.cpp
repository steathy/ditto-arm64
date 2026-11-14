#include <afxwin.h>
#include "../../../src/TrackedOptionsPage.h"
#include <cstdio>
#include <stdexcept>

CWinApp application;
class FixturePage : public CPropertyPage {
public:
    FixturePage() : CPropertyPage(101) {}
    BOOL OnInitDialog() override {
        CPropertyPage::OnInitDialog(); SetDlgItemText(1001, L"Initial value"); return TRUE;
    }
};
class FixtureSheet : public CPropertySheet {
public:
    COptionsDirtyState changes;
    FixtureSheet() : CPropertySheet(L"Options change test") { m_psh.dwFlags |= PSH_NOAPPLYNOW; }
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) override {
        changes.Observe(message, wParam); return CPropertySheet::WindowProc(message, wParam, lParam);
    }
};
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        Check(AfxWinInit(GetModuleHandle(nullptr), nullptr, GetCommandLine(), SW_HIDE) != FALSE, "MFC initialization");
        CTrackedOptionsPage<FixturePage> editable;
        FixturePage immediate;
        FixtureSheet sheet; sheet.AddPage(&editable); sheet.AddPage(&immediate);
        Check(sheet.Create(nullptr, WS_POPUP | WS_CAPTION) != FALSE, "create Options sheet");
        Check(!sheet.changes.Pending(), "initializing Options must not mark unsaved changes");
        editable.SetDlgItemText(1001, L"Unsaved edit");
        Check(sheet.changes.Pending(), "real EN_CHANGE must mark the Options sheet modified");
        editable.SetModified(FALSE);
        Check(!sheet.changes.Pending(), "saving a page must clear its modified state");
        Check(sheet.SetActivePage(1) != FALSE, "activate immediate-preference page");
        immediate.SetDlgItemText(1001, L"Saved immediately");
        Check(!sheet.changes.Pending(), "About-style immediate preferences must not block installation");
        Check(sheet.SetActivePage(0) != FALSE, "reactivate editable page");
        editable.GetDlgItem(1002)->SendMessage(BM_CLICK);
        Check(sheet.changes.Pending(), "real checkbox notification must block installation until saved");
        sheet.DestroyWindow();
        std::puts("PASS: two-control MFC fixture using the production tracking wrapper: initialization stays clean, edits/checkboxes mark dirty, saving clears dirty, immediate preferences stay independent. This fixture does not instantiate Ditto's own pages.");
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
}
