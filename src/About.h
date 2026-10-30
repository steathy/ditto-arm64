#if !defined(AFX_ABOUT_H__02A95202_4AEF_4854_9DC0_161326D7B914__INCLUDED_)
#define AFX_ABOUT_H__02A95202_4AEF_4854_9DC0_161326D7B914__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// About.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CAbout dialog

#include "hyperlink.h"
#include "HListBox.h"
#include "update/UpdateCore.h"

class CAbout : public CPropertyPage
{
	DECLARE_DYNCREATE(CAbout)

// Construction
public:
	CAbout();
	~CAbout();

// Dialog Data
	//{{AFX_DATA(CAbout)
	enum { IDD = IDD_ABOUT };
	CButton		m_CheckUpdate;
	CHyperLink m_Arm64RepositoryName;
	CHyperLink m_UpstreamRepositoryName;
	CHListBox	m_List;
	CStatic m_UpdateStatus;
	CToolTipCtrl m_InstallToolTip;
	CProgressCtrl m_UpdateProgress;
	CHyperLink m_ReleaseNotes;
	CEdit m_ReleaseNotesText;
	CButton m_InstallUpdate;
	CButton m_IncludeBeta;
	//}}AFX_DATA


// Overrides
	// ClassWizard generate virtual function overrides
	//{{AFX_VIRTUAL(CAbout)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL PreTranslateMessage(MSG* message);
	//}}AFX_VIRTUAL

// Implementation
protected:

	CString m_csTitle;
	std::shared_ptr<ditto_update::Session> m_update;
	bool m_promptShown = false;
	bool m_showRelease = false;
	std::wstring m_lastNotes;
	std::wstring m_lastStatus;
	void LayoutControls();
	void RefreshUpdate();
	bool CanInstallUpdate();

	// Generated message map functions
	//{{AFX_MSG(CAbout)
	virtual BOOL OnInitDialog();
	afx_msg void OnCheckUpdate();
	afx_msg void OnInstallUpdate();
	afx_msg void OnUpdateChannelChanged();
	afx_msg void OnTimer(UINT_PTR eventId);
	afx_msg void OnDestroy();
	afx_msg void OnSize(UINT type, int width, int height);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_ABOUT_H__02A95202_4AEF_4854_9DC0_161326D7B914__INCLUDED_)
