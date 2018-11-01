#ifndef PluginConfigDialog_h__
#define PluginConfigDialog_h__

#include <windows.h>
#include "PluginConfig.h"
#include <vector>
#include <commctrl.h>

class PluginConfig;

class PluginConfigDialog
{
public:
	static bool Show(HINSTANCE instance, HWND parent, PluginConfig& config, const std::string& title);

private:
	enum TabHandle
	{
		PLAYBACK,
		TAGS,
		COUNT
	};

private: // helpers
	static BOOL CALLBACK ConfigDialogProc(HWND dialogWnd, UINT message, WPARAM wParam, LPARAM lParam);
	void TransferConfig(bool toDialog);
	void TransferInt(bool toControl, TabHandle tab, int controlID, PluginConfig::Setting setting);
	void TransferBool(bool toControl, TabHandle tab, int controlID, PluginConfig::Setting setting);
	void TransferString(bool toControl, TabHandle tab, int controlID, PluginConfig::Setting setting);
	void InitialiseDialog(HWND dialogWnd);

	// generic windows stuff I ought to separate out and reuse
	HIMAGELIST LoadTabImageList(HWND tabControlHandle, int bitmapID);
	void AddTab(TabHandle handle, char* title, int resourceID);
	void EnableTabVisualStyle(HWND parentWindowHandle, HWND tabWindowHandle);
	void ChangeTab();

	// We are a singleton class
	static PluginConfigDialog* m_pDialogInstance;
	PluginConfigDialog(HINSTANCE instance, HWND parent, PluginConfig& config, const std::string& title);
	~PluginConfigDialog();
private: // data
	// The config info we are working on
	PluginConfig& m_pluginConfig;
	HINSTANCE m_instance;
	HWND m_parent;
	HWND m_windowHandle;
	HWND m_tabControlHandle;
	HIMAGELIST m_tabIcons;
	const std::string& m_title;
	std::map<TabHandle, HWND> m_tabWindows;
};

#endif // PluginConfigDialog_h__