#include <stdio.h>    // sprintf, FILE
#include <windows.h>  // HWND
#include <commctrl.h> // TBM_*
#include "winampsdk/Winamp/wa_ipc.h"   // Winamp messages
#include "platform_specific.h"

//-----------------------------------------------------------------
// Setup a trackbar control
//-----------------------------------------------------------------
void SetupSlider(HWND hDlg, int nDlgItem, int min, int max, int ticfreq, int position) {
  SendDlgItemMessage(hDlg,nDlgItem,TBM_SETRANGE,0,MAKELONG(min,max));
  SendDlgItemMessage(hDlg,nDlgItem,TBM_SETTICFREQ,ticfreq,0);
  SendDlgItemMessage(hDlg,nDlgItem,TBM_SETPOS,TRUE,position);
}

//-----------------------------------------------------------------
// Open URL in minibrowser or browser
//-----------------------------------------------------------------
void OpenURL(HWND winamp_wnd, char *url, char *TempHTMLFile, int UseMB) {
  FILE *f;

  f=fopen(TempHTMLFile,"wb");
  // Put start text
  fprintf(f,
    "<html><head><META HTTP-EQUIV=\"Refresh\" CONTENT=\"0; URL=%s\"></head><body>Opening %s<br><a href=\"%s\">Click here if page does not open</a></body></html>",
    url,url,url
  );
  fclose(f);

  if (UseMB) {
    url=malloc(strlen(TempHTMLFile)+9);
    strcpy(url,"file:///");
    strcat(url,TempHTMLFile);
    SendMessage(winamp_wnd,WM_USER,(WPARAM)NULL, IPC_MBOPEN);  // open minibrowser
    SendMessage(winamp_wnd,WM_USER,(WPARAM)url,  IPC_MBOPEN);  // display file
    free(url);
  }
  else ShellExecute(winamp_wnd,NULL,TempHTMLFile,NULL,NULL,SW_SHOWNORMAL);
}

