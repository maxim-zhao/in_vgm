#include "PluginConfigDialog.h"
#include "PluginConfig.h"
#include <windows.h>
#include <CommCtrl.h>
#include "resource.h"
#include "Winamp SDK/Winamp/IN2.H"

PluginConfigDialog* PluginConfigDialog::m_pDialogInstance = NULL;

PluginConfigDialog::PluginConfigDialog(HINSTANCE instance, HWND parent, PluginConfig& config, const std::string& title)
: m_instance(instance),
  m_parent(parent),
  m_pluginConfig(config),
  m_title(title),
  m_windowHandle(NULL),
  m_tabControlHandle(NULL)
{}

PluginConfigDialog::~PluginConfigDialog()
{
	// Clean up anything we created
	// Tabs windows
	for (std::map<TabHandle, HWND>::const_iterator it = m_tabWindows.begin(); it != m_tabWindows.end(); ++it)
	{
		DestroyWindow(it->second);
	}
	// Tab icons
	DeleteObject(m_tabIcons);
	// Main window is destroyed by EndDialog
}

bool PluginConfigDialog::Show(HINSTANCE instance, HWND parent, PluginConfig& config, const std::string& title)
{
	m_pDialogInstance = new PluginConfigDialog(instance, parent, config, title);
	bool result = DialogBox(instance, MAKEINTRESOURCE(IDD_CONFIG), parent, ConfigDialogProc) == IDOK;
	delete m_pDialogInstance;
	return result;
}

BOOL CALLBACK PluginConfigDialog::ConfigDialogProc(HWND dialogWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// This is a dialog proc
	// We return TRUE if we have handled the message and FALSE to fall back on the default handlers
	switch (message)
	{
	case WM_INITDIALOG:
		{
			// If this is a child window (tab page), return FALSE
			if (GetWindowLong(dialogWnd, GWL_STYLE) & WS_CHILD)
			{
				return FALSE;
			}
			else
			{
				m_pDialogInstance->InitialiseDialog(dialogWnd);
				m_pDialogInstance->TransferConfig(true);

				return TRUE;
			}
		}
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDOK:
			m_pDialogInstance->TransferConfig(false);
			// fall through
		case IDCANCEL:
			EndDialog(dialogWnd, LOWORD(wParam));
			return TRUE;
		default:
			return FALSE; //HandleCommand(LOWORD(wParam), lParam);
		}
		break;

		// 	case WM_HSCROLL: // trackbars
		// 		break;

	case WM_NOTIFY:
		// We need to handle the tab control notifications
		switch (LOWORD(wParam))
		{
		case TC_CONFIG:
			switch (((LPNMHDR)lParam)->code)
			{
			case TCN_SELCHANGE:
				m_pDialogInstance->ChangeTab();
				return TRUE;
			}
		}
	}

	// If we get here it's because we didn't handle it and return above.
	return FALSE;
}

void PluginConfigDialog::InitialiseDialog(HWND dialogWnd)
{
	if (m_tabControlHandle != NULL)
	{
		throw std::exception("Unexpected second entry to InitialiseDialog");
	}
	m_windowHandle = dialogWnd;
	m_tabControlHandle = GetDlgItem(m_windowHandle, TC_CONFIG);

	SetWindowText(m_windowHandle, m_title.c_str());

	InitCommonControls();

	m_tabIcons = LoadTabImageList(m_tabControlHandle, BM_TABICONS);

	// Add tabs
	AddTab(TabHandle::PLAYBACK, "Playback", IDD_PLAYBACK);
	AddTab(TabHandle::TAGS,     "Tags",     IDD_TAGS);
/*	AddTab(m_tabControlHandle, "VGM7z");
	AddTab(m_tabControlHandle, "Legacy settings");
	AddTab(m_tabControlHandle, "SN76489");
	AddTab(m_tabControlHandle, "YM2413");
	AddTab(m_tabControlHandle, "YM2612");
	AddTab(m_tabControlHandle, "YM2151");*/

	// Figure out the location in the dialog where they ought to go
	// Get the window rect
	RECT windowRect;
	GetWindowRect(m_windowHandle, &windowRect);
	// And the tab control rect
	RECT tabControlRect;
	GetWindowRect(m_tabControlHandle, &tabControlRect);
	// Adjust the tab control rect to be absolute rather than relative to the window
	OffsetRect(
		&tabControlRect, 
		-(windowRect.left + GetSystemMetrics(SM_CXDLGFRAME)),
		-(windowRect.top  + GetSystemMetrics(SM_CYDLGFRAME) + GetSystemMetrics(SM_CYCAPTION))
	);
	// Get the tab control to adjust to fit inside itself
	TabCtrl_AdjustRect(m_tabControlHandle, FALSE, &tabControlRect);

	// Fiddle with all the tabs
	for (std::map<TabHandle, HWND>::const_iterator it = m_tabWindows.begin(); it != m_tabWindows.end(); ++it)
	{
		// Turn on visual styles
		EnableTabVisualStyle(m_windowHandle, it->second);
		// Dock them to the window and hide them all
		SetWindowPos(
			it->second,
			HWND_TOP,
			tabControlRect.left,
			tabControlRect.top,
			tabControlRect.right - tabControlRect.left,
			tabControlRect.bottom - tabControlRect.top,
			SWP_HIDEWINDOW
		);
	}
	// fake a tab change
	ChangeTab();
}

void PluginConfigDialog::AddTab(TabHandle handle, char* title, int resourceID)
{
	// Add new tab to tab control
	// Image index = tab index
	TC_ITEM newTab;
	int tabIndex = TabCtrl_GetItemCount(m_tabControlHandle);
	newTab.mask = TCIF_TEXT | TCIF_IMAGE | TCIF_PARAM;
	newTab.pszText = title;
	newTab.iImage = tabIndex;
	newTab.lParam = (LPARAM)handle;
	TabCtrl_InsertItem(m_tabControlHandle, tabIndex, &newTab);

	// Create the window
	HWND tabWnd = CreateDialog(m_instance, MAKEINTRESOURCE(resourceID), m_windowHandle, ConfigDialogProc);
	m_tabWindows[handle] = tabWnd;

	// 
}

void PluginConfigDialog::EnableTabVisualStyle(HWND parentWindowHandle, HWND tabWindowHandle)
{
	HINSTANCE dllinst = LoadLibrary("uxtheme.dll");
	if (dllinst)
	{
		typedef HRESULT (*FP_EnableThemeDialogTexture)(HWND, DWORD);
		typedef BOOL (*FP_IsThemeDialogTextureEnabled)(HWND);
		FP_EnableThemeDialogTexture EnableThemeDialogTexture    = (FP_EnableThemeDialogTexture)GetProcAddress(dllinst, "EnableThemeDialogTexture");
		FP_IsThemeDialogTextureEnabled IsThemeDialogTextureEnabled = (FP_IsThemeDialogTextureEnabled)GetProcAddress(dllinst, "IsThemeDialogTextureEnabled");
		if ((IsThemeDialogTextureEnabled)
			&& (EnableThemeDialogTexture) // All functions found
			&& IsThemeDialogTextureEnabled(parentWindowHandle)) // and app is themed
		{ 
			// then draw pages with theme texture
			// Windows header up-to-dating:
#ifndef ETDT_ENABLETAB
#define ETDT_ENABLETAB 6
#endif
			EnableThemeDialogTexture(tabWindowHandle, ETDT_ENABLETAB);
		}
		FreeLibrary(dllinst);
	}
}

void PluginConfigDialog::ChangeTab()
{
	// get the current tab's handle
	int selectedTabIndex = TabCtrl_GetCurSel(m_tabControlHandle);
	if (selectedTabIndex < 0)
	{
        selectedTabIndex = 0;
	}
	TC_ITEM item = {TCIF_PARAM};
	TabCtrl_GetItem(m_tabControlHandle, selectedTabIndex, &item);
	TabHandle handle = (TabHandle)item.lParam;
	for (std::map<TabHandle, HWND>::const_iterator it = m_tabWindows.begin(); it != m_tabWindows.end(); ++it)
	{
		SetWindowPos(it->second, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE| (it->first == handle ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
	}
}

// caller should DeleteObject(result)
HIMAGELIST PluginConfigDialog::LoadTabImageList(HWND tabControlHandle, int bitmapID)
{
	HBITMAP bitmapHandle = LoadBitmap(m_instance, MAKEINTRESOURCE(bitmapID));
	HIMAGELIST imagelist = NULL;
	if (bitmapHandle)
	{
		// We want to find the image dimensions
		BITMAP bitmap;
		memset(&bitmap, 0, sizeof(BITMAP));
		if (GetObject(bitmapHandle, sizeof(BITMAP), (void*)&bitmap))
		{
			int iconSize = bitmap.bmHeight;
			int numIcons = bitmap.bmWidth / iconSize;
			imagelist = ImageList_Create(iconSize, iconSize, ILC_COLOR32|ILC_MASK, numIcons, 0);
			if (imagelist)
			{
				ImageList_Add(imagelist, bitmapHandle, NULL);
				TabCtrl_SetImageList(tabControlHandle, imagelist);
			}
		}
		DeleteObject(bitmapHandle);
	}
	return imagelist;
}

void PluginConfigDialog::TransferInt(bool toControl, TabHandle tab, int controlID, PluginConfig::Setting setting)
{
	if (toControl)
	{
		SetDlgItemInt(m_tabWindows[tab], controlID, m_pluginConfig.getInt(setting), TRUE);
	}
	else
	{
		BOOL success;
		int i = GetDlgItemInt(m_tabWindows[tab], controlID, &success, FALSE);
		if (success) 
		{
			m_pluginConfig.set(setting, i);
		}
	}
}

void PluginConfigDialog::TransferBool(bool toControl, TabHandle tab, int controlID, PluginConfig::Setting setting)
{
	// BOOLify the value
	if (toControl)
	{
		CheckDlgButton(m_tabWindows[tab], controlID, (m_pluginConfig.getBool(setting) ? 1 : 0));
	}
	else
	{
		m_pluginConfig.set(setting, IsDlgButtonChecked(m_tabWindows[tab], controlID) == 1);
	}
}

// needs testing, hence bufsize = 1
void PluginConfigDialog::TransferString(bool toControl, TabHandle tab, int controlID, PluginConfig::Setting setting)
{
	if (toControl)
	{
		SetDlgItemText(m_tabWindows[tab], controlID, m_pluginConfig.getString(setting).c_str());
	}
	else
	{
		std::vector<char> buf;
		std::vector<char>::size_type bufLen = 1; // first try will be double this length
		bool success;
		do 
		{
			bufLen *= 2;
			if (bufLen > 1024*1024)
			{
				break;
			}

			buf.reserve(bufLen);

			success = GetDlgItemText(m_tabWindows[tab], controlID, &buf[0], buf.capacity()) < bufLen - 1;
		} 
		while (!success);

		m_pluginConfig.set(setting, std::string(&buf[0]));
	}
}

// Either populate the dialog from m_config of vice versa
void PluginConfigDialog::TransferConfig(bool toDialog)
{
	TransferInt(toDialog, TabHandle::PLAYBACK, EDIT_NUMLOOPS, PluginConfig::Setting::PLAYBACKNUMLOOPS);
	TransferBool(toDialog, TabHandle::PLAYBACK, CHECK_LOOPFOREVER, PluginConfig::Setting::PLAYBACKLOOPFOREVER);
	TransferInt(toDialog, TabHandle::PLAYBACK, EDIT_PLAYBACKRATE, PluginConfig::Setting::PLAYBACKRATE);
	TransferInt(toDialog, TabHandle::PLAYBACK, EDIT_SAMPLINGRATE, PluginConfig::Setting::SAMPLINGRATE);

	TransferBool(toDialog, TabHandle::TAGS, CHECK_TRIMWHITESPACE, PluginConfig::Setting::TAGSTRIM);
	TransferBool(toDialog, TabHandle::TAGS, CHECK_PREFERJAPANESE, PluginConfig::Setting::TAGSPREFERJAPANESE);
	TransferBool(toDialog, TabHandle::TAGS, CHECK_APPENDFMTOYM2413, PluginConfig::Setting::TAGSADDFMTOYM2413);
	TransferBool(toDialog, TabHandle::TAGS, CHECK_CUSTOMTYPE, PluginConfig::Setting::TAGSCUSTOMFILETYPE);
}


/*

BOOL HandleCommand(WORD controlID, LPARAM lParam)
{
	// Dialog control commands
	switch (controlID)
	{
		
	}

	return FALSE; // if not handled
}

*/