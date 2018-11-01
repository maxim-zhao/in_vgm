#ifndef PLATFORM_H
#define PLATFORM_H

#include <windows.h>

void OpenURL(HWND winamp_wnd, char *url, char *TempHTMLFile, int UseMB);

//-----------------------------------------------------------------
// GUI helpers
//-----------------------------------------------------------------
#define GETTRACKBARPOS( wnd, id ) SendDlgItemMessage( wnd, id, TBM_GETPOS, 0, 0 )
#define DISABLECONTROL( wnd, id ) EnableWindow( GetDlgItem( wnd, id ), FALSE )
#define ENABLECONTROL( wnd, id ) EnableWindow( GetDlgItem( wnd, id ), TRUE )
#define HIDECONTROL( wnd, id ) ShowWindow( GetDlgItem( wnd, id ), FALSE )
#define SETCONTROLENABLED( wnd, id, enabled ) EnableWindow( GetDlgItem( wnd, id ), enabled )
#define SETCHECKBOX( wnd, id, checked ) SendDlgItemMessage( wnd, id, BM_SETCHECK, checked, 0 )

void SetupSlider(HWND hDlg, int nDlgItem, int min, int max, int ticfreq, int position);


#endif