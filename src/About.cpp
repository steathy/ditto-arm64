// About.cpp : implementation file
// test test test

//Main branch test

#include "stdafx.h"
#include "cp_main.h"
#include "About.h"
#include "Arm64BuildInfo.h"
#include "misc.h"
#include "OptionsSheet.h"
#include <thread>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CAbout property page

IMPLEMENT_DYNCREATE(CAbout, CPropertyPage)

CAbout::CAbout() : CPropertyPage(CAbout::IDD)
{
	m_csTitle = theApp.m_Language.GetString("AboutTitle", "About");
	m_psp.pszTitle = m_csTitle;
	m_psp.dwFlags |= PSP_USETITLE;

	//{{AFX_DATA_INIT(CAbout)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}

CAbout::~CAbout()
{
}

void CAbout::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAbout)
	DDX_Control(pDX, IDC_CHECK_UPDATE, m_CheckUpdate);
	DDX_Control(pDX, IDC_HYPER_LINK, m_HyperLink);
	DDX_Control(pDX, IDC_UPSTREAM_REPOSITORY, m_UpstreamLink);
	DDX_Control(pDX, IDC_ARM64_REPOSITORY_NAME, m_Arm64RepositoryName);
	DDX_Control(pDX, IDC_UPSTREAM_REPOSITORY_NAME, m_UpstreamRepositoryName);
	DDX_Control(pDX, IDC_LIST, m_List);
	DDX_Control(pDX, IDC_UPDATE_STATUS, m_UpdateStatus);
	DDX_Control(pDX, IDC_UPDATE_PROGRESS, m_UpdateProgress);
	DDX_Control(pDX, IDC_UPDATE_NOTES, m_ReleaseNotes);
	DDX_Control(pDX, IDC_UPDATE_NOTES_TEXT, m_ReleaseNotesText);
	DDX_Control(pDX, IDC_INSTALL_UPDATE, m_InstallUpdate);
	DDX_Control(pDX, IDC_UPDATE_INCLUDE_BETA, m_IncludeBeta);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CAbout, CPropertyPage)
	//{{AFX_MSG_MAP(CAbout)
	ON_BN_CLICKED(IDC_CHECK_UPDATE, OnCheckUpdate)
	ON_BN_CLICKED(IDC_INSTALL_UPDATE, OnInstallUpdate)
	ON_BN_CLICKED(IDC_UPDATE_INCLUDE_BETA, OnUpdateChannelChanged)
	ON_WM_TIMER()
	ON_WM_DESTROY()
	ON_WM_SIZE()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CAbout message handlers

BOOL CAbout::OnInitDialog() 
{
	CPropertyPage::OnInitDialog();

	m_List.AddString(_T("Ditto ARM64 - unofficial community build"));

	auto runningVersion = GetRunningVersion();
	CString cs = GetVersionString(runningVersion);

	CString csText;
	csText = "    Version " + cs;
#if defined(_M_ARM64)
	csText += _T(" ARM64 (native) - ") _T(DITTO_ARM64_BUILD_LABEL);
#elif _WIN64
	csText += " 64bit";
#else
	csText += " 32bit";
#endif
	m_List.AddString(csText);

#if defined(_M_ARM64)
	m_List.AddString(_T("    Unofficial ARM64 build by steathy@github"));
	m_List.AddString(_T("    Project: ") _T(DITTO_ARM64_REPOSITORY_URL));
	m_List.AddString(_T("    Upstream: build-number-146 / a80fd35"));
#endif

	const char *SqliteVersion = sqlite3_libversion();
	csText = "    Sqlite Version ";
	csText += SqliteVersion;
	m_List.AddString(csText);

	cs = CGetSetOptions::GetExeFileName();;
	csText = "    Exe Path " + cs;
	m_List.AddString(csText);

	cs = CGetSetOptions::GetDBPath();
	csText = "    DB Path " + cs;
	m_List.AddString(csText);

	m_List.AddString(_T(""));

	m_List.AddString(_T("Credits"));
	
	cs = "    Authors - Scott Brogden, sabrogden@users.sourceforge.net";
	m_List.AddString(cs);
	cs = "            - Kevin Edwards, ingenuus@users.sourceforge.net";
	m_List.AddString(cs);

	m_List.AddString(_T(""));

	m_List.AddString(_T("Addins"));

	//Show what addins are loaded
	CStringArray arr;
	theApp.m_Addins.AboutScreenText(arr);
	INT_PTR count = arr.GetCount();
	for(int i = 0; i < count; i++)
	{
		m_List.AddString(_T("    ") + arr[i]);
	}

	m_HyperLink.SetAutoSize(FALSE);
	m_HyperLink.SetWindowText(_T("- github.com/steathy/ditto-arm64"));
	m_HyperLink.SetURL(_T(DITTO_ARM64_REPOSITORY_URL));
	m_Arm64RepositoryName.SetAutoSize(FALSE);
	m_Arm64RepositoryName.SetURL(_T(DITTO_ARM64_REPOSITORY_URL));
	m_UpstreamLink.SetAutoSize(FALSE);
	m_UpstreamLink.SetWindowText(_T("- github.com/sabrogden/Ditto"));
	m_UpstreamLink.SetURL(_T(DITTO_UPSTREAM_REPOSITORY_URL));
	m_UpstreamRepositoryName.SetAutoSize(FALSE);
	m_UpstreamRepositoryName.SetURL(_T(DITTO_UPSTREAM_REPOSITORY_URL));

	m_ReleaseNotes.SetAutoSize(FALSE);
	m_IncludeBeta.SetCheck(CGetSetOptions::GetProfileLong(_T("Arm64IncludeBetaUpdates"), 0) == 1 ? BST_CHECKED : BST_UNCHECKED);
	m_UpdateProgress.SetRange32(0, 1000);
	m_InstallToolTip.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX);
	m_InstallToolTip.SetMaxTipWidth(420);
	CRect installRect; m_InstallUpdate.GetWindowRect(installRect); ScreenToClient(installRect);
	m_InstallToolTip.AddTool(this, _T("Install the downloaded update. Unsigned builds cannot verify the publisher; you will be asked whether to continue."), installRect, IDC_INSTALL_UPDATE);
	m_update = std::make_shared<ditto_update::Session>();
	SetTimer(1, 150, NULL);
	RefreshUpdate();
	LayoutControls();
		
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CAbout::OnCheckUpdate()
{
	using namespace ditto_update;
	if (!m_update) return;
	auto phase = m_update->Read().phase;
	if (phase == Phase::Checking || phase == Phase::Downloading || phase == Phase::Verifying)
	{
		m_update->cancel = true;
		RefreshUpdate();
		return;
	}
	if (phase == Phase::Installing) return;
	m_update = std::make_shared<Session>();
	m_promptShown = false;
	try {
		CheckOptions options;
		options.installed = Version::Parse(DITTO_ARM64_VERSION_STRING);
		options.executable = CGetSetOptions::GetExeFileName().GetString();
		options.includeBeta = m_IncludeBeta.GetCheck() == BST_CHECKED;
		options.windowsBuild = WindowsBuild();
		auto registered = RegisteredInstallDirectory();
		CString expected((registered + L"\\Ditto.exe").c_str());
		options.installedMode = !registered.empty() && expected.CompareNoCase(CGetSetOptions::GetExeFileName()) == 0
			&& !CGetSetOptions::GetIsPortableDitto() && !CGetSetOptions::GetIsWindowsApp()
			&& !CGetSetOptions::GetIsChocolateyApp() && !CGetSetOptions::m_bFromIni
			&& !theApp.m_databaseOnNetworkShare;
		HANDLE token = NULL;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
			TOKEN_ELEVATION elevation{}; DWORD size = 0;
			if (!GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) || elevation.TokenIsElevated)
				options.installedMode = false;
			CloseHandle(token);
		} else options.installedMode = false;
		options.cacheRoot = PrivateCacheRoot();
		m_update->Set(Phase::Checking, L"Checking for ARM64 updates...");
		StartCheck(m_update, options);
	} catch (const std::exception& error) { m_update->Set(Phase::Failed, Wide(error.what())); }
	RefreshUpdate();
}

void CAbout::OnUpdateChannelChanged()
{
	using namespace ditto_update;
	if (!m_update) return;
	auto phase = m_update->Read().phase;
	if (phase == Phase::Checking || phase == Phase::Downloading || phase == Phase::Verifying || phase == Phase::Installing) return;
	bool includeBeta = m_IncludeBeta.GetCheck() == BST_CHECKED;
	// This About-page update preference takes effect immediately, independently
	// of the editable Options pages. Changing it never initiates a network request.
	if (!CGetSetOptions::SetProfileLong(_T("Arm64IncludeBetaUpdates"), includeBeta ? 1 : 0)) {
		m_IncludeBeta.SetCheck(includeBeta ? BST_UNCHECKED : BST_CHECKED);
		AfxMessageBox(_T("The update preference could not be saved."));
		return;
	}
	m_update->cancel = true;
	m_update = std::make_shared<Session>();
	m_promptShown = false;
	RefreshUpdate();
}

void CAbout::LayoutControls()
{
	if (!m_IncludeBeta.GetSafeHwnd() || !m_UpstreamRepositoryName.GetSafeHwnd()) return;
	CRect units(0, 0, 4, 4); MapDialogRect(units);
	const int gap = units.Height(), margin = units.Width();
	CRect area; GetClientRect(area); area.DeflateRect(margin, gap);
	auto height = [this](int dlu) { CRect r(0, 0, 0, dlu); MapDialogRect(r); return r.Height(); };
	// Use each control's actual font and available width so wrapped messages
	// grow when necessary, while one-line/empty text never reserves blank rows.
	auto textSize = [](CWnd& control, int width, UINT flags) {
		CString text; control.GetWindowText(text);
		if (text.IsEmpty()) return CSize(0, 0);
		CClientDC dc(&control);
		auto oldFont = dc.SelectObject(control.GetFont());
		CRect bounds(0, 0, width, 0);
		dc.DrawText(text, bounds, DT_CALCRECT | DT_NOPREFIX | flags);
		dc.SelectObject(oldFont);
		return bounds.Size();
	};
	bool moved = false;
	auto place = [&](CWnd& control, const CRect& rect) {
		CRect previous; control.GetWindowRect(previous); ScreenToClient(previous);
		if (previous != rect) { control.MoveWindow(rect); moved = true; }
	};
	auto nextPixels = [&](CWnd& control, int pixels) {
		if (pixels <= 0) return;
		CRect r(area.left, area.top, area.right, area.top + pixels);
		place(control, r); area.top = r.bottom + gap;
	};
	auto nextText = [&](CWnd& control) { nextPixels(control, textSize(control, area.Width(), DT_WORDBREAK).cy); };
	// Preserve the original upstream information-list height. Update messages
	// flow beneath it and never expand/shrink this existing control.
	nextPixels(m_List, height(132));
	// Two real columns keep both dashes and addresses aligned in proportional
	// fonts, without relying on padding spaces or changing the system font.
	int nameWidth = max(textSize(m_Arm64RepositoryName, area.Width(), DT_SINGLELINE).cx,
		textSize(m_UpstreamRepositoryName, area.Width(), DT_SINGLELINE).cx);
	int linkLeft = area.left + nameWidth + margin * 2;
	auto repositoryRow = [&](CWnd& name, CWnd& link) {
		int rowHeight = max(textSize(name, nameWidth, DT_SINGLELINE).cy,
			textSize(link, max(1, area.right - linkLeft), DT_WORDBREAK).cy);
		place(name, CRect(area.left, area.top, area.left + nameWidth, area.top + rowHeight));
		place(link, CRect(linkLeft, area.top, area.right, area.top + rowHeight));
		area.top += rowHeight + gap;
	};
	repositoryRow(m_Arm64RepositoryName, m_HyperLink);
	repositoryRow(m_UpstreamRepositoryName, m_UpstreamLink);
	nextText(m_UpdateStatus);
	if (m_showRelease) {
		nextPixels(m_UpdateProgress, height(6));
		nextText(m_ReleaseNotes);
		int notesHeight = min(height(28), max(height(14), area.Height() - height(12 + 16) - gap * 3));
		nextPixels(m_ReleaseNotesText, notesHeight);
	}
	nextPixels(m_IncludeBeta, height(12));
	CRect buttonSize(0, 0, 80, 16); MapDialogRect(buttonSize);
	CRect button(area.left, area.top, area.left + buttonSize.Width(), area.top + buttonSize.Height());
	place(m_CheckUpdate, button);
	button.OffsetRect(button.Width() + margin, 0); place(m_InstallUpdate, button);
	if (m_InstallToolTip.GetSafeHwnd()) m_InstallToolTip.SetToolRect(this, IDC_INSTALL_UPDATE, button);
	if (moved) Invalidate(TRUE);
}

BOOL CAbout::PreTranslateMessage(MSG* message)
{
	if (m_InstallToolTip.GetSafeHwnd()) m_InstallToolTip.RelayEvent(message);
	return CPropertyPage::PreTranslateMessage(message);
}

void CAbout::RefreshUpdate()
{
	using namespace ditto_update;
	if (!m_update || !m_UpdateStatus.GetSafeHwnd()) return;
	auto state = m_update->Read();
	bool busy = state.phase == Phase::Checking || state.phase == Phase::Downloading || state.phase == Phase::Verifying;
	bool installing = state.phase == Phase::Installing;
	m_CheckUpdate.SetWindowText(busy ? _T("Cancel") : _T("Check Update"));
	m_CheckUpdate.EnableWindow(!installing && !(busy && m_update->cancel.load()));
	m_InstallUpdate.EnableWindow(state.phase == Phase::Ready);
	m_IncludeBeta.EnableWindow(!busy && !installing);
	std::wstring status = state.message;
	if (busy && m_update->cancel.load()) status = L"Cancelling the update request...";
	if (state.release.size) {
		status = L"Latest offered: " + Wide(state.release.version.String()) + L" - " + BuildLabel(state.release.channel) + L"\r\n" + status;
		if (state.phase == Phase::Downloading) {
			CString progress; progress.Format(_T(" (%.1f / %.1f MB)"), state.downloaded / 1048576.0, state.release.size / 1048576.0);
			status += progress.GetString();
		}
	}
	bool statusChanged = status != m_lastStatus;
	if (statusChanged) { m_UpdateStatus.SetWindowText(status.c_str()); m_lastStatus = status; }
	m_UpdateStatus.ShowWindow(status.empty() ? SW_HIDE : SW_SHOW);
	bool showRelease = state.release.size != 0;
	if (showRelease) {
		m_ReleaseNotes.SetURL(state.release.notesUrl.c_str());
		auto notes = state.release.notes.empty() ? L"Release notes were not included in the feed. Use the link above to read them." : state.release.notes;
		std::wstring displayNotes;
		for (wchar_t ch : notes) {
			if (ch == L'\n' && (displayNotes.empty() || displayNotes.back() != L'\r')) displayNotes += L'\r';
			displayNotes += ch;
		}
		notes = displayNotes;
		if (notes != m_lastNotes) { m_ReleaseNotesText.SetWindowText(notes.c_str()); m_lastNotes = notes; }
		m_UpdateProgress.SetPos(static_cast<int>(state.downloaded * 1000 / state.release.size));
	}
	m_ReleaseNotes.ShowWindow(showRelease ? SW_SHOW : SW_HIDE);
	m_ReleaseNotesText.ShowWindow(showRelease ? SW_SHOW : SW_HIDE);
	m_UpdateProgress.ShowWindow(showRelease ? SW_SHOW : SW_HIDE);
	if (showRelease != m_showRelease || statusChanged) { m_showRelease = showRelease; LayoutControls(); }
}

bool CAbout::CanInstallUpdate()
{
	auto sheet = DYNAMIC_DOWNCAST(COptionsSheet, GetParent());
	if (sheet && sheet->HasPendingChanges()) {
		AfxMessageBox(_T("Save your Options changes with OK, then reopen About to install the update. The download will remain cached."));
		return false;
	}
	if (theApp.m_pMainFrame && theApp.m_pMainFrame->m_pEditFrameWnd) {
		AfxMessageBox(_T("Close the clip editor after saving your work, then install the update."));
		return false;
	}
	return true;
}

void CAbout::OnInstallUpdate()
{
	using namespace ditto_update;
	if (!m_update || m_update->Read().phase != Phase::Ready || !CanInstallUpdate()) return;
	m_promptShown = true;
	auto ready = m_update->Read();
	CString instruction; instruction.Format(_T("Install Ditto ARM64 %s (%s) now?"), Wide(ready.release.version.String()).c_str(), BuildLabel(ready.release.channel));
	bool unsignedConsent = ready.verification.requiresUnsignedConsent;
	TASKDIALOG_BUTTON buttons[] = {{100, unsignedConsent ? L"Install unsigned update" : L"Install now"}, {IDCANCEL, L"Cancel"}};
	TASKDIALOGCONFIG dialog{}; dialog.cbSize = sizeof(dialog); dialog.hwndParent = m_hWnd;
	dialog.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
	dialog.pszWindowTitle = L"Ditto ARM64 update"; dialog.pszMainInstruction = instruction;
	std::wstring content;
	if (unsignedConsent) {
		content = ready.verification.installerSignature == SignatureStatus::Unsigned
			? L"This installer is unsigned. Its publisher cannot be verified. "
			: L"The installed Ditto client or update helper is unsigned. Its publisher cannot be verified. ";
		content += L"The download matches the feed's size and SHA-256, but that does not establish who created it. Continue only if you trust this release.\n\n";
		dialog.pszMainIcon = TD_WARNING_ICON;
	}
	content += L"Save and close any external clip editors first. Ditto will close while the update is installed. A backup will preserve your settings and clipboard history. Windows will ask for installation permission.";
	dialog.pszContent = content.c_str();
	dialog.cButtons = 2; dialog.pButtons = buttons; dialog.nDefaultButton = IDCANCEL;
	int selected = IDCANCEL;
	if (FAILED(TaskDialogIndirect(&dialog, &selected, nullptr, nullptr)) || selected != 100) return;
	auto session = m_update;
	std::wstring executable = CGetSetOptions::GetExeFileName().GetString();
	std::wstring database = CGetSetOptions::GetDBPath().GetString();
	const DWORD pid = GetCurrentProcessId(); auto window = reinterpret_cast<uintptr_t>(theApp.m_MainhWnd);
	session->Set(Phase::Installing, L"Preparing the verified installer and backup...");
	try {
		std::thread([session, ready, executable, database, pid, window, unsignedConsent] {
			try { LaunchInstallHelper(ready, executable, database, pid, window, unsignedConsent); }
			catch (const std::exception& error) { session->Set(Phase::Failed, Wide(error.what())); }
		}).detach();
	} catch (const std::exception& error) { session->Set(Phase::Failed, Wide(error.what())); }
	RefreshUpdate();
}

void CAbout::OnTimer(UINT_PTR eventId)
{
	if (eventId == 1) {
		RefreshUpdate();
		auto sheet = DYNAMIC_DOWNCAST(CPropertySheet, GetParent());
		if (m_update && m_update->Read().phase == ditto_update::Phase::Ready && !m_promptShown && sheet && sheet->GetActivePage() == this) {
			m_promptShown = true; OnInstallUpdate();
		}
	}
	CPropertyPage::OnTimer(eventId);
}
void CAbout::OnDestroy()
{
	KillTimer(1);
	if (m_update) m_update->cancel = true;
	CPropertyPage::OnDestroy();
}
void CAbout::OnSize(UINT type, int width, int height)
{
	CPropertyPage::OnSize(type, width, height); LayoutControls();
}
