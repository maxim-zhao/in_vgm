//-----------------------------------------------------------------
// in_vgm
// VGM audio input plugin for Winamp
// http://www.smspower.org/music
// by Maxim <maxim\x40smspower\x2eorg> in 2001-2006
// with help from BlackAura in March and April 2002
// YM2612 PCM additions with Blargg in November 2005
//-----------------------------------------------------------------

// Relative volumes of sound cores
// SN76489 = 1
//#define YM2413RelativeVol 1  // SMS/Mark III with FM pack - empirical value, real output would help
//#define GENSYM2612RelativeVol 4  // Mega Drive/Genesis
//#define MAMEYM2612RelativeVol 3  // Mega Drive/Genesis
//#define YM2151RelativeVol 4 // CPS1

//#define BETA

#define VERSION "0.35"

#ifdef BETA
#define PLUGINNAME "VGM input plugin v" VERSION " beta "__DATE__" "__TIME__
#else
#define PLUGINNAME "VGM input plugin v" VERSION
#endif

const char *INISECTION = "Maxim's VGM input plugin";


// SN76489 has 4 channels, 2^4-1=0xf
#define SN76489_NUM_CHANNELS 4
#define SN76489_MUTE_ALLON 0xf
// YM2413 has 14 (9 + 5 percussion), BUT it uses 1=mute, 0=on
#define YM2413_NUM_CHANNELS 14
#define YM2413_MUTE_ALLON 0
// YM2612 has 8 (6 + DAC + SSG_EG), 1=mute, 0=on
#define YM2612_NUM_CHANNELS 8
#define YM2612_MUTE_ALLON 0x80 // default SSG off
// This is preliminary and may change
#define YM2151_MUTE_ALLON 0

#include <windows.h>
#include <stdio.h>
#include <float.h>
#include <commctrl.h>
#include <math.h>
#include <zlib.h>
#include <assert.h>
#include "Winamp SDK/Winamp/in2.h"
#include "Winamp SDK/Winamp/wa_ipc.h"
#include "Winamp SDK/Winamp/ipc_pe.h"
#include "Winamp SDK/GlobalConfig.h"

// a few items removed from recent Winamp SDKs
#define WINAMP_BUTTON1                  40044
#define WINAMP_BUTTON2                  40045
#define WINAMP_BUTTON3                  40046
#define WINAMP_BUTTON4                  40047
#define WINAMP_BUTTON5                  40048

#include "vgmcore.h"
#include "vgm.h"
#include "gd3.h"
#include "common.h"
#include "platform_specific.h"
#include "fileinfo.h"
#include "panning.h"
#include "apply_gain.h"

// #define EMU2413_COMPACTION
#include "emu2413/emu2413.h"

#include "sn76489/sn76489.h"
#include "resource.h"

// BlackAura - MAME FM synthesiser (aargh!)
#include "mame/ym2151.h"  // MAME YM2151

#include "gens/ym2612.h"  // Gens YM2612

#define ROUND(x) ((int)(x>0?x+0.5:x-0.5))

HANDLE PluginhInst;

BOOL WINAPI DllMain(HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved) {
  PluginhInst=hInst;
  return TRUE;
}

// raw configuration
#define NCH 2      // Number of channels
#define SAMPLERATE 44100// Sampling rate
#define MAX_VOLUME 100  // Number of steps for fadeout; can't have too many because of overflow

In_Module mod;      // the output module (declared near the bottom of this file)
char lastfn[MAX_PATH]="";  // currently playing file (used for getting info on the current file)
char *TempHTMLFile;  // holds a filename for the Unicode text file

#define SampleBufferSize (576*NCH*2)
short SampleBuffer[SampleBufferSize];  // sample buffer

gzFile *InputFile;

OPLL *opll;  // EMU2413 structure
SN76489_Context *snchip; // SN76489 structure
ym2612_ *ym2612;

// BlackAura - FMChip flags
#define FM_YM2413  0x01  // Bit 0 = YM2413
#define FM_YM2612  0x02  // Bit 1 = YM2612
#define FM_YM2151  0x04  // Bit 2 = TM2151

#define USINGCHIP(chip) (FMChips&chip)

int killDecodeThread=0;                       // the kill switch for the decode thread
HANDLE thread_handle=INVALID_HANDLE_VALUE;    // the handle to the decode thread
DWORD WINAPI __stdcall DecodeThread(void *b); // the decode thread procedure

// forward references
void setoutputtime(int time_in_ms);
int  getoutputtime();

enum
{
   FILTER_NONE,
   FILTER_LOWPASS,
   FILTER_WEIGHTED
};

// evil global variables
int
  paused,             // are we paused?
  TrackLengthInms,    // Current track length in ms
  PlaybackRate,       // in Hz
  FileRate,           // in Hz
  LoopForever,
  NumLoops,           // how many times to play looped section
  NumLoopsDone,       // how many loops we've played
  LoopLengthInms,     // length of looped section in ms
  LoopOffset,         // File offset of looped data start
  VGMDataOffset,      // File offset of data start
  SNClock=0,          // SN76489 clock rate
  YM2413Clock=0,      // FM clock rates
  YM2612Clock=0,      // 
  YM2151Clock=0,      // 
  FMChips=0,          // BlackAura - FM Chips enabled
  SeekToSampleNumber, // For seeking
	SeekToTimeInms,
  FileInfoJapanese,   // Whether to show Japanese in the info dialogue
  UseMB,              // Whether to open HTML in the MB
  AutoMB,             // Whether to automatically show HTML in MB
  ForceMBOpen,        // Whether to force the MB to open if closed when doing AutoMB
  YM2413HiQ,
  Overdrive,
  ImmediateUpdate,
  PauseBetweenTracksms,
  PauseBetweenTracksCounter,
  LoopingFadeOutms,
  LoopingFadeOutCounter,
  LoopingFadeOutTotal,
  MutePersistent=0,
  MLJapanese,
  MLShowFM,
  MLType,
  SN76489_Mute = SN76489_MUTE_ALLON,
  SN76489_Pan [ SN76489_NUM_CHANNELS ], // panning 0..254, 127 is centre
  YM2413_Pan [ YM2413_NUM_CHANNELS ],
  RandomisePanning,
  SN_enable = 1,
  YM2413_enable = 1,
  YM2612_enable = 1,
  YM2151_enable = 1,
	filter_type = FILTER_NONE,
	prev_sample[2] = {0,0},
	vgm_compression_level,
	vgm7z_enable,                      // 1 to enable VGM7z support
	vgm7z_extract_same,                // 1 to extract VGM7z to same dir, else use vgm7z_extract_dir
	vgm7z_extract_subdir,              // 1 to use VGM7z name as new dir name
	vgm7z_extract_prompt,              // 1 to prompt before extraction
	vgm7z_delete_after_extract,        // 1 if VGM7z should be deleted after extraction
	enableReplayGainHack;
long int 
  YM2413Channels=YM2413_MUTE_ALLON,  // backup when stopped. PSG does it itself.
  YM2612Channels=YM2612_MUTE_ALLON,
  YM2151Channels=YM2151_MUTE_ALLON;
char
  TrackTitleFormat[100],      // Track title formatting
  INIFileName[MAX_PATH+1];
const char
  *FilenameForInfoDlg;      // Filename passed to file info dialogue
char
	*vgm7z_extract_dir,
	*CurrentURL,							// Current URL being played
	*CurrentURLFilename;			// Filename current URL has been saved to
float
	SN_preamp,
	YM2413_preamp,
	YM2612_preamp,
	YM2151_preamp;

float ReplayGain;
float ReplayPeak;
int ReplayNoClip;

// Blargg: PCM data for current file (loaded in play thread)
static unsigned char* pcm_data = NULL;
static unsigned long pcm_data_size;
static unsigned long pcm_pos;

void UpdateIfPlaying() {
  if (ImmediateUpdate
    &&(mod.outMod)
    &&(mod.outMod->IsPlaying())
	)
	setoutputtime(getoutputtime());
}

//-----------------------------------------------------------------
// Configuration dialogue
//-----------------------------------------------------------------
#define NumCfgTabChildWnds 7
HWND CfgTabChildWnds[NumCfgTabChildWnds];  // Holds child windows' HWnds
// Defines to make it easier to place stuff where I want
#define CfgPlayback CfgTabChildWnds[0]
#define CfgTags     CfgTabChildWnds[1]
#define CfgPSG      CfgTabChildWnds[2]
#define Cfg2413     CfgTabChildWnds[3]
#define Cfg2612     CfgTabChildWnds[4]
#define Cfg2151     CfgTabChildWnds[5]
#define Cfg7z       CfgTabChildWnds[6]
// Dialogue box tabsheet handler
BOOL CALLBACK ConfigDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam);
void MakeTabbedDialogue(HWND hWndMain) {
  // Variables
  TC_ITEM NewTab;
  HWND TabCtrlWnd=GetDlgItem(hWndMain,tcMain);
  RECT TabDisplayRect,TabRect;
  HIMAGELIST il;
  int i;
  // Load images for tabs
  InitCommonControls(); // required before doing imagelist stuff
  il=ImageList_LoadImage(PluginhInst,(LPCSTR)tabicons,16,0,RGB(255,0,255),IMAGE_BITMAP,LR_CREATEDIBSECTION);
  TabCtrl_SetImageList(TabCtrlWnd,il);
  // Add tabs
	NewTab.mask=TCIF_TEXT | TCIF_IMAGE;
  NewTab.pszText="Playback";
  NewTab.iImage=0;
  TabCtrl_InsertItem(TabCtrlWnd,0,&NewTab);
  NewTab.pszText="GD3 tags";
  NewTab.iImage=1;
  TabCtrl_InsertItem(TabCtrlWnd,1,&NewTab);
  NewTab.pszText="SN76489";
  NewTab.iImage=2;
  TabCtrl_InsertItem(TabCtrlWnd,2,&NewTab);
  NewTab.pszText="YM2413";
  NewTab.iImage=3;
  TabCtrl_InsertItem(TabCtrlWnd,3,&NewTab);
  NewTab.pszText="YM2612";
  NewTab.iImage=4;
  TabCtrl_InsertItem(TabCtrlWnd,4,&NewTab);
  NewTab.pszText="YM2151";
  NewTab.iImage=5;
  TabCtrl_InsertItem(TabCtrlWnd,5,&NewTab);
  NewTab.pszText="VGM7z";
  NewTab.iImage=6;
  TabCtrl_InsertItem(TabCtrlWnd,6,&NewTab);
  // Get display rect
  GetWindowRect(TabCtrlWnd,&TabDisplayRect);
  GetWindowRect(hWndMain,&TabRect);
  OffsetRect(&TabDisplayRect,-TabRect.left-GetSystemMetrics(SM_CXDLGFRAME),-TabRect.top-GetSystemMetrics(SM_CYDLGFRAME)-GetSystemMetrics(SM_CYCAPTION));
  TabCtrl_AdjustRect(TabCtrlWnd,FALSE,&TabDisplayRect);
  
  // Create child windows
  CfgPlayback =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgPlayback,hWndMain,ConfigDialogProc);
  CfgTags     =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgTags,    hWndMain,ConfigDialogProc);
  CfgPSG      =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgPSG,     hWndMain,ConfigDialogProc);
  Cfg2413     =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgYM2413,  hWndMain,ConfigDialogProc);
	Cfg2612     =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgYM2612,  hWndMain,ConfigDialogProc);
  Cfg2151     =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgYM2151,  hWndMain,ConfigDialogProc);
  Cfg7z       =CreateDialog(PluginhInst,(LPCTSTR) DlgCfgVgm7z,   hWndMain,ConfigDialogProc);
  // Enable WinXP styles
  {
    HINSTANCE dllinst=LoadLibrary("uxtheme.dll");
    if (dllinst) {
      FARPROC EnableThemeDialogTexture    =GetProcAddress(dllinst,"EnableThemeDialogTexture"),
              SetThemeAppProperties       =GetProcAddress(dllinst,"GetThemeAppProperties"),
              IsThemeDialogTextureEnabled =GetProcAddress(dllinst,"IsThemeDialogTextureEnabled");
      // I try to test if the app is in a themed XP but without a manifest to allow control theming, but the one which
      // should tell me (GetThemeAppProperties) returns STAP_ALLOW_CONTROLS||STAP_ALLOW_NONCLIENT when it should only return
      // STAP_ALLOW_NONCLIENT (as I understand it). None of the other functions help either :(
      if (
        (IsThemeDialogTextureEnabled)&&(EnableThemeDialogTexture)&& // All functions found
        (IsThemeDialogTextureEnabled(hWndMain))) { // and app is themed
        for (i=0;i<NumCfgTabChildWnds;++i) EnableThemeDialogTexture(CfgTabChildWnds[i],6); // then draw pages with theme texture
      }
      FreeLibrary(dllinst);
    }
  }

  // Put them in the right place, and hide them
  for (i=0;i<NumCfgTabChildWnds;++i) 
    SetWindowPos(CfgTabChildWnds[i],HWND_TOP,TabDisplayRect.left,TabDisplayRect.top,TabDisplayRect.right-TabDisplayRect.left,TabDisplayRect.bottom-TabDisplayRect.top,SWP_HIDEWINDOW);
  // Show the first one, though
  SetWindowPos(CfgTabChildWnds[0],HWND_TOP,0,0,0,0,SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW);
}

// Dialogue box callback function
BOOL CALLBACK ConfigDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam) {
  switch (wMessage) {  // process messages
    case WM_INITDIALOG: { // Initialise dialogue
      int i;
      if (GetWindowLong(DlgWin,GWL_STYLE)&WS_CHILD) return FALSE;
      MakeTabbedDialogue(DlgWin);

      SetWindowText(DlgWin,PLUGINNAME " configuration");

      // Playback tab -----------------------------------------------------------
#define WND CfgPlayback
      // Set loop count
      SetDlgItemInt( WND, ebLoopCount, NumLoops, FALSE );
      CheckDlgButton( WND, cbLoopForever, LoopForever );
			SETCONTROLENABLED( WND, ebLoopCount, !LoopForever );
      // Set fadeout length
      SetDlgItemInt( WND, ebFadeOutLength, LoopingFadeOutms, FALSE );
      // Set between-track pause length
      SetDlgItemInt( WND, ebPauseLength, PauseBetweenTracksms, FALSE);
      // Playback rate
      switch (PlaybackRate) {
        case 0:
          CheckRadioButton(WND,rbRateOriginal,rbRateOther,rbRateOriginal);
          break;
        case 50:
          CheckRadioButton(WND,rbRateOriginal,rbRateOther,rbRate50);
          break;
        case 60:
          CheckRadioButton(WND,rbRateOriginal,rbRateOther,rbRate60);
          break;
        default:
          CheckRadioButton(WND,rbRateOriginal,rbRateOther,rbRateOther);
          break;
      }
      SETCONTROLENABLED( WND, ebPlaybackRate, IsDlgButtonChecked( WND, rbRateOther ) );
      if ( PlaybackRate != 0 )
        SetDlgItemInt( WND, ebPlaybackRate, PlaybackRate, FALSE );
			else
        SetDlgItemText( WND, ebPlaybackRate, "60" );
      // Volume overdrive
      CheckDlgButton( WND, cbOverDrive, Overdrive );
      // Persistent muting checkbox
      CheckDlgButton( WND, cbMutePersistent, MutePersistent );
      // randomise panning checkbox
      CheckDlgButton( WND, cbRandomisePanning, RandomisePanning );
			// Replay Gain transcoding hack checkbox
			CheckDlgButton( WND, cbEnableReplayGainHack, enableReplayGainHack);

			
			switch(filter_type)
			{
				case FILTER_NONE:
					CheckRadioButton(WND,rbFilterNone,rbFilterWeighted,rbFilterNone);
					break;
				case FILTER_LOWPASS:
					CheckRadioButton(WND,rbFilterNone,rbFilterWeighted,rbFilterLowPass);
					break;
				case FILTER_WEIGHTED:
					CheckRadioButton(WND,rbFilterNone,rbFilterWeighted,rbFilterWeighted);
					break;
			}
#undef WND

      // GD3 tab ----------------------------------------------------------------
#define WND CfgTags
      // Set title format text
      SetDlgItemText(WND,ebTrackTitle,TrackTitleFormat);
      // Media Library options
      CheckDlgButton(WND,cbMLJapanese,MLJapanese);
      CheckDlgButton(WND,cbMLShowFM,MLShowFM);
      SetDlgItemInt(WND,ebMLType,MLType,FALSE);
      // Now Playing settings
      CheckDlgButton(WND,cbUseMB         ,UseMB);
      CheckDlgButton(WND,cbAutoMB        ,AutoMB);
      CheckDlgButton(WND,cbForceMBOpen   ,ForceMBOpen);
      SETCONTROLENABLED(WND,cbAutoMB     ,UseMB);
      SETCONTROLENABLED(WND,cbForceMBOpen,UseMB & AutoMB);
#undef WND

      // PSG tab ----------------------------------------------------------------
#define WND CfgPSG
      if (SNClock) {
        // Check PSG channel checkboxes
        for (i=0;i<SN76489_NUM_CHANNELS;i++) CheckDlgButton(WND,cbTone1+i,((SN76489_Mute & (1<<i))>0));
      } else {
        // or disable them
        for ( i = 0; i < SN76489_NUM_CHANNELS; i++ ) DISABLECONTROL(WND,cbTone1+i);
        DISABLECONTROL(WND,btnRandomPSG);
        DISABLECONTROL(WND,btnCentrePSG);
        DISABLECONTROL(WND,slSNCh0);
        DISABLECONTROL(WND,slSNCh1);
        DISABLECONTROL(WND,slSNCh2);
        DISABLECONTROL(WND,slSNCh3);
      }
      // Panning
      SetupSlider(WND,slSNCh0,0,254,127,SN76489_Pan[0]);
      SetupSlider(WND,slSNCh1,0,254,127,SN76489_Pan[1]);
      SetupSlider(WND,slSNCh2,0,254,127,SN76489_Pan[2]);
      SetupSlider(WND,slSNCh3,0,254,127,SN76489_Pan[3]);
      // Chip enable
      CheckDlgButton(WND,cbSNEnable,(SN_enable!=0));
      SETCONTROLENABLED(WND,cbSNEnable,(SNClock!=0));
      // Preamp
      SetupSlider(WND,slSNPreamp,0,200,100,ROUND(SN_preamp*100));
#undef WND

      // YM2413 tab -------------------------------------------------------------
#define WND Cfg2413
      if USINGCHIP(FM_YM2413) {  // Check YM2413 FM channel checkboxes
        for (i=0;i<YM2413_NUM_CHANNELS;i++) CheckDlgButton(WND,cbYM24131+i,!((YM2413Channels & (1<<i))>0));
        CheckDlgButton(WND,cbYM2413ToneAll,((YM2413Channels&0x1ff )==0));
        CheckDlgButton(WND,cbYM2413PercAll,((YM2413Channels&0x3e00)==0));
      } else {
        for (i=0;i<YM2413_NUM_CHANNELS;i++) DISABLECONTROL(WND,cbYM24131+i);
        DISABLECONTROL(WND,cbYM2413ToneAll);
        DISABLECONTROL(WND,cbYM2413PercAll);
        DISABLECONTROL(WND,lblExtraTone);
        DISABLECONTROL(WND,lblExtraToneNote);
        DISABLECONTROL(WND,gbYM2413);
        DISABLECONTROL(WND,sl2413ch1);
        DISABLECONTROL(WND,sl2413ch2);
        DISABLECONTROL(WND,sl2413ch3);
        DISABLECONTROL(WND,sl2413ch4);
        DISABLECONTROL(WND,sl2413ch5);
        DISABLECONTROL(WND,sl2413ch6);
        DISABLECONTROL(WND,sl2413ch7);
        DISABLECONTROL(WND,sl2413ch8);
        DISABLECONTROL(WND,sl2413ch9);
        DISABLECONTROL(WND,sl2413hh );
        DISABLECONTROL(WND,sl2413cym);
        DISABLECONTROL(WND,sl2413tt );
        DISABLECONTROL(WND,sl2413sd );
        DISABLECONTROL(WND,sl2413bd );
        DISABLECONTROL(WND,btnCentre2413);
        DISABLECONTROL(WND,btnRandom2413);
      }                             
      // Panning
      SetupSlider(WND,sl2413ch1,0,254,127,YM2413_Pan[0]);
      SetupSlider(WND,sl2413ch2,0,254,127,YM2413_Pan[1]);
      SetupSlider(WND,sl2413ch3,0,254,127,YM2413_Pan[2]);
      SetupSlider(WND,sl2413ch4,0,254,127,YM2413_Pan[3]);
      SetupSlider(WND,sl2413ch5,0,254,127,YM2413_Pan[4]);
      SetupSlider(WND,sl2413ch6,0,254,127,YM2413_Pan[5]);
      SetupSlider(WND,sl2413ch7,0,254,127,YM2413_Pan[6]);
      SetupSlider(WND,sl2413ch8,0,254,127,YM2413_Pan[7]);
      SetupSlider(WND,sl2413ch9,0,254,127,YM2413_Pan[8]);
      SetupSlider(WND,sl2413hh ,0,254,127,YM2413_Pan[9]);
      SetupSlider(WND,sl2413cym,0,254,127,YM2413_Pan[10]);
      SetupSlider(WND,sl2413tt ,0,254,127,YM2413_Pan[11]);
      SetupSlider(WND,sl2413sd ,0,254,127,YM2413_Pan[12]);
      SetupSlider(WND,sl2413bd ,0,254,127,YM2413_Pan[13]);
      // Chip enable
      CheckDlgButton(WND,cbYM2413Enable,YM2413_enable);
      SETCONTROLENABLED(WND,cbYM2413Enable,USINGCHIP(FM_YM2413));
      // Preamp
      SetupSlider(WND,slYM2413Preamp,0,200,100,ROUND(YM2413_preamp*100));
      // HQ option
      CheckDlgButton(WND,cbYM2413HiQ,YM2413HiQ);
#undef WND

      // YM2612 tab -------------------------------------------------------
#define WND Cfg2612
      // YM2612 enable
      CheckDlgButton(WND,cbYM2612Enable,YM2612_enable);
      SETCONTROLENABLED(WND,cbYM2612Enable,USINGCHIP(FM_YM2612));
      // Preamp
      SetupSlider(WND,slYM2612Preamp,0,200,100,ROUND(YM2612_preamp*100));
      // Muting checkboxes
      if USINGCHIP(FM_YM2612) {  // Check YM2161 FM channel checkboxes
        for (i=0;i<YM2612_NUM_CHANNELS;i++) CheckDlgButton(WND,cb2612Tone1+i,!((YM2612Channels & (1<<i))>0));
        CheckDlgButton(WND,cb2612All,((YM2612Channels&0x3f )==0));
      } else {
        for (i=0;i<YM2612_NUM_CHANNELS;i++) DISABLECONTROL(WND,cb2612Tone1+i);
        DISABLECONTROL(WND,cb2612All);
        DISABLECONTROL(WND,gbYM2612);
      }                             

#undef WND

      // YM2151 tab -------------------------------------------------------
#define WND Cfg2151
      // YM2151 enable
      CheckDlgButton(WND,cbYM2151Enable,YM2151_enable);
      SETCONTROLENABLED(WND,cbYM2151Enable,USINGCHIP(FM_YM2151));
      // Preamp
      SetupSlider(WND,slYM2151Preamp,0,200,100,ROUND(YM2151_preamp*100));
#undef WND

      // VGM7z tab -------------------------------------------------------
#define WND Cfg7z
			// VGM7z enable
			CheckDlgButton(WND,cbEnable7zSupport,vgm7z_enable);
			// Prompt on extract
			CheckDlgButton(WND,cbPromptOnExtract,vgm7z_extract_prompt);
			// Extract to same
			CheckDlgButton(WND,(vgm7z_extract_same?rbExtractSameDir:rbExtractFixedDir),TRUE);
			// Extract to where
			SetDlgItemText(WND,ebExtractDir,vgm7z_extract_dir);
			SETCONTROLENABLED(WND,ebExtractDir,IsDlgButtonChecked(WND,cbEnable7zSupport) && IsDlgButtonChecked(WND,rbExtractFixedDir));
			// Extract to subdir
			CheckDlgButton(WND,cbExtractToSubfolder,vgm7z_extract_subdir);
      // Compression level
      SetupSlider(WND,slVGMcompression,0,9,1,vgm_compression_level);
			// Delete VGM7z after extracting
			CheckDlgButton(WND,cbDelete7z,vgm7z_delete_after_extract);
			// trigger enabling of stuff
			SendMessage(WND,WM_COMMAND,MAKEWPARAM(cbEnable7zSupport,0),0);

#undef WND

      // Stuff not in tabs ------------------------------------------------------
      // Immediate update checkbox
      CheckDlgButton(DlgWin,cbMuteImmediate,ImmediateUpdate);
      
      SendMessage(DlgWin,WM_HSCROLL,0,0); // trigger slider value change handlers
      SetFocus(DlgWin);

      return (TRUE);
    }
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case IDOK: {    // OK button
          int i;
          BOOL b;

          // Playback tab ------------------------------------------------------
          // Loop count
          i=GetDlgItemInt(CfgPlayback,ebLoopCount,&b,FALSE);
          if (b) NumLoops=i;
          // Loop forever
          LoopForever = IsDlgButtonChecked(CfgPlayback,cbLoopForever);
          // Fadeout length
          i=GetDlgItemInt(CfgPlayback,ebFadeOutLength,&b,FALSE);
          if (b) LoopingFadeOutms=i;
          // Between-track pause length
          i=GetDlgItemInt(CfgPlayback,ebPauseLength,&b,FALSE);
          if (b) PauseBetweenTracksms=i;
          // Playback rate
          PlaybackRate=0;
          if (IsDlgButtonChecked(CfgPlayback,rbRate50)) {
            PlaybackRate=50;
          } else if (IsDlgButtonChecked(CfgPlayback,rbRate60)) {
            PlaybackRate=60;
          } else if (IsDlgButtonChecked(CfgPlayback,rbRateOther)) {
            i=GetDlgItemInt(CfgPlayback,ebPlaybackRate,&b,TRUE);
            if ((b) && (i>0) && (i<=6000)) PlaybackRate=i;
          }
          // Persistent muting checkbox
          MutePersistent=IsDlgButtonChecked(CfgPlayback,cbMutePersistent);
          // Randomise panning checkbox
          RandomisePanning=IsDlgButtonChecked(CfgPlayback,cbRandomisePanning);
					// Replay Gain transcoding hack checkbox
					enableReplayGainHack = IsDlgButtonChecked(CfgPlayback, cbEnableReplayGainHack);

          // Tags tab ----------------------------------------------------------
          // Track title format
          GetDlgItemText(CfgTags,ebTrackTitle,TrackTitleFormat,100);
          // Media Library options
          MLJapanese=IsDlgButtonChecked(CfgTags,cbMLJapanese);
          MLShowFM  =IsDlgButtonChecked(CfgTags,cbMLShowFM);
          i=GetDlgItemInt(CfgTags,ebMLType,&b,FALSE);
          if (b) MLType=i;

					// VGM7z tab ---------------------------------------------------------
					// VGM7z enable
					vgm7z_enable = IsDlgButtonChecked(Cfg7z, cbEnable7zSupport);
					// Prompt on extract
					vgm7z_extract_prompt = IsDlgButtonChecked(Cfg7z, cbPromptOnExtract);
					// Extract to same
					vgm7z_extract_same = IsDlgButtonChecked(Cfg7z, rbExtractSameDir);
					// Extract to where
					{
						char *buffer = malloc(512); // probably MAX_PATH anyway
						i = GetDlgItemText(Cfg7z, ebExtractDir, buffer, 511);
						free(vgm7z_extract_dir);
						vgm7z_extract_dir = strdup(buffer);
						free(buffer);
					}
					// Extract to subdir
					vgm7z_extract_subdir = IsDlgButtonChecked(Cfg7z, cbExtractToSubfolder);
					// Compression level
					vgm_compression_level = GETTRACKBARPOS(Cfg7z,slVGMcompression);
					// Delete VGM7z after extracting
					vgm7z_delete_after_extract = IsDlgButtonChecked(Cfg7z, cbDelete7z);

				}
        EndDialog(DlgWin,0);
        return (TRUE);
      case IDCANCEL:  // [X] button, Alt+F4, etc
        EndDialog(DlgWin,1);
        return (TRUE) ;

      // PSG tab -------------------------------------------------------------------
      case cbTone1: case cbTone2: case cbTone3: case cbTone4:
        SN76489_Mute=
           (IsDlgButtonChecked(CfgPSG,cbTone1)     )
          |(IsDlgButtonChecked(CfgPSG,cbTone2) << 1)
          |(IsDlgButtonChecked(CfgPSG,cbTone3) << 2)
          |(IsDlgButtonChecked(CfgPSG,cbTone4) << 3);
        SN76489_SetMute(snchip,SN76489_Mute);
        UpdateIfPlaying();
        break;
      case btnCentrePSG: // centre PSG fake stereo sliders
        SendDlgItemMessage(CfgPSG,slSNCh0,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(CfgPSG,slSNCh1,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(CfgPSG,slSNCh2,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(CfgPSG,slSNCh3,TBM_SETPOS,TRUE,127);
        SendMessage(DlgWin,WM_HSCROLL,TB_ENDTRACK,0);
        break;
      case btnRandomPSG: // randomise PSG fake stereo sliders
        SendDlgItemMessage(CfgPSG,slSNCh0,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(CfgPSG,slSNCh1,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(CfgPSG,slSNCh2,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(CfgPSG,slSNCh3,TBM_SETPOS,TRUE,random_stereo());
        SendMessage(DlgWin,WM_HSCROLL,TB_ENDTRACK,0);
        break;
      case cbSNEnable:
        SN_enable = IsDlgButtonChecked(CfgPSG, cbSNEnable);
        UpdateIfPlaying();
        break;
       
       // YM2413 tab ----------------------------------------------------------------
      case cbYM24131:   case cbYM24132:   case cbYM24133:   case cbYM24134:
      case cbYM24135:   case cbYM24136:   case cbYM24137:   case cbYM24138:
      case cbYM24139:   case cbYM241310:  case cbYM241311:  case cbYM241312:
      case cbYM241313:  case cbYM241314: {
        int i;
        YM2413Channels=0;
        for (i=0;i<YM2413_NUM_CHANNELS;i++) YM2413Channels|=(!IsDlgButtonChecked(Cfg2413,cbYM24131+i))<<i;
        if USINGCHIP(FM_YM2413) {
          OPLL_setMask(opll,YM2413Channels);
          UpdateIfPlaying();
        }
        CheckDlgButton(Cfg2413,cbYM2413ToneAll,((YM2413Channels&0x1ff )==0));
        CheckDlgButton(Cfg2413,cbYM2413PercAll,((YM2413Channels&0x3e00)==0));
        break;
      }
      case cbYM2413ToneAll: {
        int i;
        const int Checked=IsDlgButtonChecked(Cfg2413,cbYM2413ToneAll);
        for (i=0;i<9;i++) CheckDlgButton(Cfg2413,cbYM24131+i,Checked);
        PostMessage(Cfg2413,WM_COMMAND,cbYM24131,0);
        break;
      }
      case cbYM2413PercAll: {
        int i;
        const int Checked=IsDlgButtonChecked(Cfg2413,cbYM2413PercAll);
        for (i=0;i<5;i++) CheckDlgButton(Cfg2413,cbYM241310+i,Checked);
        PostMessage(Cfg2413,WM_COMMAND,cbYM24131,0);
        break;
      }
      case cbYM2413HiQ:
        YM2413HiQ=IsDlgButtonChecked(Cfg2413,cbYM2413HiQ);
        if USINGCHIP(FM_YM2413) {
          OPLL_set_quality(opll,YM2413HiQ);
          UpdateIfPlaying();
        }
        break;
      case btnCentre2413: // centre YM2413 fake stereo sliders
        SendDlgItemMessage(Cfg2413,sl2413ch1,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch2,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch3,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch4,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch5,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch6,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch7,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch8,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413ch9,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413hh ,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413cym,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413tt ,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413sd ,TBM_SETPOS,TRUE,127);
        SendDlgItemMessage(Cfg2413,sl2413bd ,TBM_SETPOS,TRUE,127);
        SendMessage(DlgWin,WM_HSCROLL,TB_ENDTRACK,0);
        break;
      case btnRandom2413: // randomise YM2413 fake stereo sliders
        SendDlgItemMessage(Cfg2413,sl2413ch1,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch2,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch3,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch4,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch5,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch6,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch7,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch8,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413ch9,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413hh ,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413cym,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413tt ,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413sd ,TBM_SETPOS,TRUE,random_stereo());
        SendDlgItemMessage(Cfg2413,sl2413bd ,TBM_SETPOS,TRUE,random_stereo());
        SendMessage(DlgWin,WM_HSCROLL,TB_ENDTRACK,0);
        break;
      case cbYM2413Enable:
        YM2413_enable = IsDlgButtonChecked(Cfg2413, cbYM2413Enable);
        UpdateIfPlaying();
        break;

      // Playback tab --------------------------------------------------------------
      case rbRateOriginal:
      case rbRate50:
      case rbRate60:
      case rbRateOther:
        CheckRadioButton( CfgPlayback, rbRateOriginal, rbRateOther, LOWORD(wParam) );
        SETCONTROLENABLED( CfgPlayback, ebPlaybackRate, ( LOWORD(wParam) == rbRateOther ) );
        if ( LOWORD(wParam) == rbRateOther ) SetFocus( GetDlgItem( CfgPlayback, ebPlaybackRate ) );
        break;
      case cbOverDrive:
        Overdrive=IsDlgButtonChecked(CfgPlayback,cbOverDrive);
        UpdateIfPlaying();
        break;
      case cbLoopForever:
        SETCONTROLENABLED( CfgPlayback, ebLoopCount, !IsDlgButtonChecked( CfgPlayback, cbLoopForever ) );
        break;
			case rbFilterNone:
			case rbFilterLowPass:
			case rbFilterWeighted:
				CheckRadioButton( CfgPlayback, rbFilterNone, rbFilterWeighted, LOWORD(wParam) );
				if (IsDlgButtonChecked(CfgPlayback,rbFilterNone))
					filter_type = FILTER_NONE;
				else if(IsDlgButtonChecked(CfgPlayback,rbFilterLowPass))
					filter_type = FILTER_LOWPASS;
				else // if(IsDlgButtonChecked(CfgPlayback,rbFilterWeighted))
					filter_type = FILTER_WEIGHTED;
				UpdateIfPlaying();
				break;


      // Tags tab -----------------------------------------------------------------
      case cbUseMB:
        UseMB=IsDlgButtonChecked(CfgTags,cbUseMB);
        SETCONTROLENABLED(CfgTags,cbAutoMB     ,UseMB);
        SETCONTROLENABLED(CfgTags,cbForceMBOpen,UseMB & AutoMB);
        break;
      case cbAutoMB:
        AutoMB=IsDlgButtonChecked(CfgTags,cbAutoMB);
        SETCONTROLENABLED(CfgTags,cbForceMBOpen,UseMB & AutoMB);
        break;
      case cbForceMBOpen:
        ForceMBOpen=IsDlgButtonChecked(CfgTags,cbForceMBOpen);
        break;

      // YM2612 tab -----------------------------------------------------------------
      case cbYM2612Enable:
        YM2612_enable = IsDlgButtonChecked(Cfg2612,cbYM2612Enable);
        UpdateIfPlaying();
        break;
      case cb2612All: {
        int i;
        const int Checked=IsDlgButtonChecked(Cfg2612,cb2612All);
        for (i=0;i<6;i++) CheckDlgButton(Cfg2612,cb2612Tone1+i,Checked);
        PostMessage(Cfg2612,WM_COMMAND,cb2612Tone1,0);
        break;
      }
      case cb2612Tone1:   case cb2612Tone2:   case cb2612Tone3:   case cb2612Tone4:
			case cb2612Tone5:   case cb2612Tone6:   case cb2612DAC:	    case cb2612SSGEG: {
        int i;
        YM2612Channels=0;
        for (i=0;i<YM2612_NUM_CHANNELS;i++) YM2612Channels|=(!IsDlgButtonChecked(Cfg2612,cb2612Tone1+i))<<i;
        if USINGCHIP(FM_YM2612) {
          YM2612_SetMute(ym2612, YM2612Channels);
          UpdateIfPlaying();
        }
        CheckDlgButton(Cfg2612,cb2612All,((YM2612Channels&0x3f)==0));
        break;
      }

			// YM2151 tab -----------------------------------------------------------------
      case cbYM2151Enable:
        YM2151_enable = IsDlgButtonChecked(Cfg2151,cbYM2151Enable);
        UpdateIfPlaying();
        break;

			// VGM7z tab -----------------------------------------------------------------
			case rbExtractFixedDir:
			case rbExtractSameDir:
				SETCONTROLENABLED(Cfg7z,ebExtractDir,IsDlgButtonChecked(Cfg7z,rbExtractFixedDir));
				break;

			case cbEnable7zSupport:
				{
					int enabled = IsDlgButtonChecked(Cfg7z, cbEnable7zSupport);
					SETCONTROLENABLED(Cfg7z,cbPromptOnExtract,enabled);
					SETCONTROLENABLED(Cfg7z,gbExtractTo,enabled);
					SETCONTROLENABLED(Cfg7z,rbExtractSameDir,enabled);
					SETCONTROLENABLED(Cfg7z,rbExtractFixedDir,enabled);
					SETCONTROLENABLED(Cfg7z,ebExtractDir,enabled && IsDlgButtonChecked(Cfg7z,rbExtractFixedDir));
					SETCONTROLENABLED(Cfg7z,cbExtractToSubfolder,enabled);
					SETCONTROLENABLED(Cfg7z,lblVGMCompression,enabled);
					SETCONTROLENABLED(Cfg7z,lblVGMCompressionValue,enabled);
					SETCONTROLENABLED(Cfg7z,slVGMcompression,enabled);
					SETCONTROLENABLED(Cfg7z,cbDelete7z,enabled);
				}
				break;

      // Stuff not in tabs --------------------------------------------------------
      case cbMuteImmediate:
        ImmediateUpdate=IsDlgButtonChecked(DlgWin,cbMuteImmediate);
        UpdateIfPlaying();
        break;
      case btnReadMe: {
          char FileName[MAX_PATH];
          char *PChar;
          GetModuleFileName(PluginhInst,FileName,MAX_PATH);  // get *dll* path
          GetFullPathName(FileName,MAX_PATH,FileName,&PChar);  // make it fully qualified plus find the filename bit
          strcpy(PChar,"in_vgm.html");
          if ((int)ShellExecute(mod.hMainWindow,NULL,FileName,NULL,NULL,SW_SHOWNORMAL)<=32)
            MessageBox(DlgWin,"Error opening in_vgm.html from plugin folder",mod.description,MB_ICONERROR+MB_OK);
        }
        break;
      }
    break; // end WM_COMMAND handling

    case WM_HSCROLL: // trackbar notifications
    {
      int i;
      // Get PSG panning controls
      SN76489_Pan[0]=GETTRACKBARPOS(CfgPSG,slSNCh0);
      SN76489_Pan[1]=GETTRACKBARPOS(CfgPSG,slSNCh1);
      SN76489_Pan[2]=GETTRACKBARPOS(CfgPSG,slSNCh2);
      SN76489_Pan[3]=GETTRACKBARPOS(CfgPSG,slSNCh3);
      if (SNClock)
        SN76489_SetPanning(snchip,SN76489_Pan[0],SN76489_Pan[1],SN76489_Pan[2],SN76489_Pan[3]);

      // Get YM2413 panning controls
      YM2413_Pan[ 0]=GETTRACKBARPOS(Cfg2413,sl2413ch1);
      YM2413_Pan[ 1]=GETTRACKBARPOS(Cfg2413,sl2413ch2);
      YM2413_Pan[ 2]=GETTRACKBARPOS(Cfg2413,sl2413ch3);
      YM2413_Pan[ 3]=GETTRACKBARPOS(Cfg2413,sl2413ch4);
      YM2413_Pan[ 4]=GETTRACKBARPOS(Cfg2413,sl2413ch5);
      YM2413_Pan[ 5]=GETTRACKBARPOS(Cfg2413,sl2413ch6);
      YM2413_Pan[ 6]=GETTRACKBARPOS(Cfg2413,sl2413ch7);
      YM2413_Pan[ 7]=GETTRACKBARPOS(Cfg2413,sl2413ch8);
      YM2413_Pan[ 8]=GETTRACKBARPOS(Cfg2413,sl2413ch9);
      YM2413_Pan[ 9]=GETTRACKBARPOS(Cfg2413,sl2413hh );
      YM2413_Pan[10]=GETTRACKBARPOS(Cfg2413,sl2413cym);
      YM2413_Pan[11]=GETTRACKBARPOS(Cfg2413,sl2413tt );
      YM2413_Pan[12]=GETTRACKBARPOS(Cfg2413,sl2413sd );
      YM2413_Pan[13]=GETTRACKBARPOS(Cfg2413,sl2413bd );
      if (USINGCHIP(FM_YM2413))
        for ( i=0; i< YM2413_NUM_CHANNELS; ++i )
          OPLL_set_pan( opll, i, YM2413_Pan[i] );

      // Get slider values and update accompanying text
      {
        int i;
        i = GETTRACKBARPOS(CfgPSG,slSNPreamp);
        SetDlgItemInt(CfgPSG, txtSNPreamp, i, FALSE);
        SN_preamp    =i/100.0f;
        i = GETTRACKBARPOS(Cfg2413,slYM2413Preamp);
        SetDlgItemInt(Cfg2413, txt2413Preamp, i, FALSE);
        YM2413_preamp    =i/100.0f;
        i = GETTRACKBARPOS(Cfg2612,slYM2612Preamp);
        SetDlgItemInt(Cfg2612, txt2612Preamp, i, FALSE);
        YM2612_preamp    =i/100.0f;
        i = GETTRACKBARPOS(Cfg2151,slYM2151Preamp);
        SetDlgItemInt(Cfg2151, txt2151Preamp, i, FALSE);
        YM2151_preamp    =i/100.0f;

        i = GETTRACKBARPOS(Cfg7z,slVGMcompression);
				SetDlgItemInt(Cfg7z, lblVGMCompressionValue, i, FALSE);
			}

      if((LOWORD(wParam)==TB_THUMBPOSITION)||(LOWORD(wParam)==TB_ENDTRACK)) UpdateIfPlaying();
      break;
    }
    case WM_NOTIFY:
      switch (LOWORD(wParam)) {
        case tcMain:
          switch (((LPNMHDR)lParam)->code) {
            case TCN_SELCHANGING:  // hide current window
              SetWindowPos(CfgTabChildWnds[TabCtrl_GetCurSel(GetDlgItem(DlgWin,tcMain))],HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_HIDEWINDOW);
              break;
            case TCN_SELCHANGE:  // show current window
              {
                int i=TabCtrl_GetCurSel(GetDlgItem(DlgWin,tcMain));
                SetWindowPos(CfgTabChildWnds[i],HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
              }
              break;
          }
          break;
      }
      return TRUE;
  }
  return (FALSE) ;    // return FALSE to signify message not processed
}

void config(HWND hwndParent) {
  DialogBox(mod.hDllInstance, "CONFIGDIALOGUE", hwndParent, ConfigDialogProc);
}

//-----------------------------------------------------------------
// About dialogue
//-----------------------------------------------------------------
void about(HWND hwndParent)
{
  MessageBox(hwndParent,
    PLUGINNAME "\n\n"
    "Build date: "__DATE__"\n\n"
    "by Maxim in 2001-2006\n"
    "maxim@mwos.cjb.net\n"
    "http://www.smspower.org/music\n\n"
    "Current status:\n"
    "PSG - emulated as a perfect device, leading to slight differences in sound compared to the real thing. Noise pattern is 100% accurate, unlike almost every other core out there :P\n"
    "YM2413 - via EMU2413 0.61 (Mitsutaka Okazaki) (http://www.angel.ne.jp/~okazaki/ym2413).\n"
    "YM2612 - via Gens 2.10 core (Stephane Dallongeville) (http://gens.consolemul.com).\n"
    "YM2151 - via MAME FM core (Jarek Burczynski, Hiro-shi), thanks to BlackAura.\n\n"
    "Don\'t be put off by the pre-1.0 version numbers. This is a non-commercial project and as such it is permanently in beta.\n\n"
		"Using:\n"
		"ZLib " ZLIB_VERSION " (http://www.zlib.org)\n"
		"LZMA SDK 4.40 (http://www.7-zip.org)\n"
		"\n"
    "Thanks also go to:\n"
    "Bock, Heliophobe, Mike G, Steve Snake, Dave, Charles MacDonald, Ville Helin, John Kortink, fx^, DukeNukem, Blargg\n\n"
    "  ...and Zhao Yuehua xxx wo ai ni"
    ,mod.description,MB_ICONINFORMATION|MB_OK);
}

void getINIfilename()
{
  // see if we are in Winamp 5.11+ with user profiles
  char *dir = (char *)SendMessage(mod.hMainWindow,WM_WA_IPC,0,IPC_GETINIDIRECTORY);
  if (dir)
  {
    strcpy(INIFileName,dir);
    strcat(INIFileName,"\\plugins");
    // make sure folder exists
    CreateDirectory(INIFileName,NULL);
    strcat(INIFileName,"\\in_vgm.ini");
    // check if settings exist in this file
    if(GetPrivateProfileInt(INISECTION,"NumLoops",-1,INIFileName)==-1)
    {
      // if not, try to import settings from old INI location
      char *p,oldINI[MAX_PATH+1],*section;
      int sectionsize;
      GetModuleFileName(0,oldINI,MAX_PATH);  // get dll path
      GetFullPathName(oldINI,MAX_PATH,oldINI,&p);  // make it fully qualified plus find the filename bit
      strcpy(p,"plugins\\plugin.ini");
      
      section=malloc(32768);
      sectionsize=GetPrivateProfileSection(INISECTION,section,32768,oldINI);
      if (sectionsize>0)
      {
        // write section to new file
        WritePrivateProfileSection(INISECTION,section,INIFileName);
        // delete section from old file
        WritePrivateProfileSection(INISECTION,NULL,oldINI);
      }
      free(section);
    }
  }
  else
  {
    // old Winamp, old INI location
    char *p;
    GetModuleFileName(0,INIFileName,MAX_PATH);  // get dll path
    GetFullPathName(INIFileName,MAX_PATH,INIFileName,&p);  // make it fully qualified plus find the filename bit
    strcpy(p,"plugins\\plugin.ini");
  }
}

//-----------------------------------------------------------------
// Initialisation (one-time)
//-----------------------------------------------------------------
void init() {
  char buffer[1024];
  int i;

  getINIfilename();

	i = GetTempPath( 0, NULL );
	TempHTMLFile = malloc( i + 8 ); // allocate buffer size needed
  if ( TempHTMLFile )
	{
		GetTempPath ( i, TempHTMLFile );
    strcat(TempHTMLFile,"GD3.html");
	}

  NumLoops            =GetPrivateProfileInt(INISECTION,"NumLoops"             ,1    ,INIFileName);
  LoopForever         =GetPrivateProfileInt(INISECTION,"Loop forever"         ,0    ,INIFileName);
  LoopingFadeOutms    =GetPrivateProfileInt(INISECTION,"Fade out length"      ,5000 ,INIFileName);
  PauseBetweenTracksms=GetPrivateProfileInt(INISECTION,"Pause between tracks" ,1000 ,INIFileName);
  PlaybackRate        =GetPrivateProfileInt(INISECTION,"Playback rate"        ,0    ,INIFileName);
  FileInfoJapanese    =GetPrivateProfileInt(INISECTION,"Japanese in info box" ,0    ,INIFileName);
  UseMB               =GetPrivateProfileInt(INISECTION,"Use Minibrowser"      ,1    ,INIFileName);
  AutoMB              =GetPrivateProfileInt(INISECTION,"Auto-show HTML"       ,0    ,INIFileName);
  ForceMBOpen         =GetPrivateProfileInt(INISECTION,"Force MB open"        ,0    ,INIFileName);
  YM2413HiQ           =GetPrivateProfileInt(INISECTION,"High quality YM2413"  ,0    ,INIFileName);
  Overdrive           =GetPrivateProfileInt(INISECTION,"Overdrive"            ,1    ,INIFileName);
  ImmediateUpdate     =GetPrivateProfileInt(INISECTION,"Immediate update"     ,1    ,INIFileName);
  MLJapanese          =GetPrivateProfileInt(INISECTION,"ML Japanese"          ,0    ,INIFileName);
  MLShowFM            =GetPrivateProfileInt(INISECTION,"ML show FM"           ,1    ,INIFileName);
  MLType              =GetPrivateProfileInt(INISECTION,"ML type"              ,0    ,INIFileName);
	filter_type         =GetPrivateProfileInt(INISECTION,"Audio filter type"    ,FILTER_NONE,INIFileName);

	vgm_compression_level=GetPrivateProfileInt(INISECTION,"VGM compression"      ,9    ,INIFileName);
	vgm7z_enable        =GetPrivateProfileInt(INISECTION,"Enable VGM7z support" ,1    ,INIFileName);
	vgm7z_extract_same  =GetPrivateProfileInt(INISECTION,"VGM7z to same folder" ,1    ,INIFileName);
	vgm7z_extract_subdir=GetPrivateProfileInt(INISECTION,"VGM7z to subfolder"   ,1    ,INIFileName);
	vgm7z_extract_prompt=GetPrivateProfileInt(INISECTION,"VGM7z extract prompt" ,1    ,INIFileName);
	vgm7z_delete_after_extract=GetPrivateProfileInt(INISECTION,"Delete VGM7z"   ,1    ,INIFileName);
	i                   =GetPrivateProfileString(INISECTION,"VGM7z to folder"   ,"c:\\",buffer,1023,INIFileName);
	vgm7z_extract_dir   =strdup(buffer);
	enableReplayGainHack=GetPrivateProfileInt(INISECTION,"Enable kill Replay Gain",0    ,INIFileName);

  GetPrivateProfileString(INISECTION,"Title format","%t (%g) - %a",TrackTitleFormat,100,INIFileName);

  GetPrivateProfileString(INISECTION,"SN76489 panning","127,127,127,127",buffer,1023,INIFileName);
  if ( sscanf( buffer, "%d,%d,%d,%d", &SN76489_Pan[0], &SN76489_Pan[1], &SN76489_Pan[2], &SN76489_Pan[3] ) != 4 )
    for ( i = 0; i < SN76489_NUM_CHANNELS; ++i ) SN76489_Pan[i] = 127;
  GetPrivateProfileString(INISECTION,"YM2413 panning","127,127,127,127,127,127,127,127,127,127,127,127,127,127",buffer,1023,INIFileName);
  if ( sscanf( buffer, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &YM2413_Pan[0], &YM2413_Pan[1], &YM2413_Pan[2], &YM2413_Pan[3], &YM2413_Pan[4], &YM2413_Pan[5], &YM2413_Pan[6], &YM2413_Pan[7], &YM2413_Pan[8], &YM2413_Pan[9], &YM2413_Pan[10], &YM2413_Pan[11], &YM2413_Pan[12], &YM2413_Pan[13] ) != YM2413_NUM_CHANNELS )
    for ( i = 0; i < YM2413_NUM_CHANNELS; ++i ) YM2413_Pan[i] = 127;
  RandomisePanning    =GetPrivateProfileInt(INISECTION,"Randomise panning"     ,0    ,INIFileName);

  GetPrivateProfileString(INISECTION,"PSG preamp","1",buffer,1023,INIFileName);
  if ( sscanf( buffer, "%f", &SN_preamp ) != 1 ) SN_preamp = 1;
  GetPrivateProfileString(INISECTION,"YM2413 preamp","1",buffer,1023,INIFileName);
  if ( sscanf( buffer, "%f", &YM2413_preamp ) != 1 ) YM2413_preamp = 1;
  GetPrivateProfileString(INISECTION,"YM2612 preamp","0.25",buffer,1023,INIFileName);
  if ( sscanf( buffer, "%f", &YM2612_preamp ) != 1 ) YM2612_preamp = 0.25;
  GetPrivateProfileString(INISECTION,"YM2151 preamp","0.25",buffer,1023,INIFileName);
  if ( sscanf( buffer, "%f", &YM2151_preamp ) != 1 ) YM2151_preamp = 0.25;

  pcm_data = NULL;
}

//-----------------------------------------------------------------
// Deinitialisation (one-time)
//-----------------------------------------------------------------
void quit() {
  char tempstr[1024];  // buffer for itoa

  WritePrivateProfileString(INISECTION,"NumLoops"            ,itoa(NumLoops             ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Loop forever"        ,itoa(LoopForever          ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Fade out length"     ,itoa(LoopingFadeOutms     ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Pause between tracks",itoa(PauseBetweenTracksms ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Playback rate"       ,itoa(PlaybackRate         ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Japanese in info box",itoa(FileInfoJapanese     ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Title format"        ,TrackTitleFormat                      ,INIFileName);
  WritePrivateProfileString(INISECTION,"Use Minibrowser"     ,itoa(UseMB                ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Auto-show HTML"      ,itoa(AutoMB               ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Force MB open"       ,itoa(ForceMBOpen          ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"High quality YM2413" ,itoa(YM2413HiQ            ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Overdrive"           ,itoa(Overdrive            ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Immediate update"    ,itoa(ImmediateUpdate      ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"ML Japanese"         ,itoa(MLJapanese           ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"ML show FM"          ,itoa(MLShowFM             ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"ML type"             ,itoa(MLType               ,tempstr,10),INIFileName);
  WritePrivateProfileString(INISECTION,"Audio filter type"   ,itoa(filter_type          ,tempstr,10),INIFileName);

	WritePrivateProfileString(INISECTION,"VGM compression"      ,itoa(vgm_compression_level,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Enable VGM7z support" ,itoa(vgm7z_enable        ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"VGM7z to same folder" ,itoa(vgm7z_extract_same  ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"VGM7z to subfolder"   ,itoa(vgm7z_extract_subdir,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"VGM7z extract prompt" ,itoa(vgm7z_extract_prompt,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Delete VGM7z"         ,itoa(vgm7z_delete_after_extract,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"VGM7z to folder"      ,vgm7z_extract_dir                    ,INIFileName);
	free(vgm7z_extract_dir);
	WritePrivateProfileString(INISECTION,"Enable kill Replay Gain",itoa(enableReplayGainHack,tempstr,10),INIFileName);
	
	sprintf(tempstr, "%d,%d,%d,%d", SN76489_Pan[0], SN76489_Pan[1], SN76489_Pan[2], SN76489_Pan[3]);
  WritePrivateProfileString(INISECTION,"SN76489 panning"     ,tempstr,INIFileName);
  sprintf(tempstr, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", YM2413_Pan[0], YM2413_Pan[1], YM2413_Pan[2], YM2413_Pan[3], YM2413_Pan[4], YM2413_Pan[5], YM2413_Pan[6], YM2413_Pan[7], YM2413_Pan[8], YM2413_Pan[9], YM2413_Pan[10], YM2413_Pan[11], YM2413_Pan[12], YM2413_Pan[13]);
  WritePrivateProfileString(INISECTION,"YM2413 panning"      ,tempstr,INIFileName);
  WritePrivateProfileString(INISECTION,"Randomise panning"   ,itoa(RandomisePanning     ,tempstr,10),INIFileName);

  sprintf(tempstr, "%f", SN_preamp);
  WritePrivateProfileString(INISECTION,"PSG preamp"          ,tempstr,INIFileName);
  sprintf(tempstr, "%f", YM2413_preamp);
  WritePrivateProfileString(INISECTION,"YM2413 preamp"       ,tempstr,INIFileName);
  sprintf(tempstr, "%f", YM2612_preamp);
  WritePrivateProfileString(INISECTION,"YM2612 preamp"       ,tempstr,INIFileName);
  sprintf(tempstr, "%f", YM2151_preamp);
  WritePrivateProfileString(INISECTION,"YM2151 preamp"       ,tempstr,INIFileName);

	if ( TempHTMLFile )
	{
    DeleteFile( TempHTMLFile );
		free( TempHTMLFile );
	}

	if ( CurrentURLFilename )
	{
    DeleteFile( CurrentURLFilename );
		free( CurrentURLFilename );
	}

	if ( CurrentURL )
		free( CurrentURL );

}

//-----------------------------------------------------------------
// Pre-extension check file claiming
//-----------------------------------------------------------------
int isourfile(const char *fn) {
  // First, check for URLs
  gzFile *f;
  char *p=strrchr(fn,'.');
  if ( (p) && ( (stricmp(p,".vgm")==0) || (stricmp(p,".vgz")==0) ) && IsURL(fn) ) return TRUE;

  // second, check the file itself
  f = gzopen( fn, "rb" );
  if (f) {
    unsigned long i = 0;
    gzread( f, &i, sizeof(i) );
    gzclose( f );
    if ( i == VGMIDENT )
      return TRUE;
  }

  return FALSE;
}

//-----------------------------------------------------------------
// Returns a new string that is the urlencoded version of the one
// given, eg. http://www.site.com/file%20name.vgm
//-----------------------------------------------------------------
char *URLEncode(char *s) {
  // URL encode string s
  // caller must free returned string
  char *hexchars="0123456789ABCDEF";
  char *result=malloc(strlen(s)*3+1),*p,*q;

	if(!result)
		return s;
  
  strcpy(result,s); // fill it up to start with

  q=strstr(s,"://");

  if(!q) {
    *result=0;
    return result;
  }

  if(strchr(s,'%')) return result; // do nothing if there's a % in it already since it's probably already URL encoded

  p=result+(q-s+2);

  for (q+=2;*q!=0;q++)
    if ((*q<'/' && *q!='-' && *q!='.') ||
		  (*q<'A' && *q>'9' && *q!=':') ||
			(*q>'Z' && *q<'a' && *q!='_') ||
			(*q>'z')) {
        *p++='%';
        *p++=hexchars[*q>>4];
        *p++=hexchars[*q&0xf];
    } else *p++=*q;
  *p=0;

  return result;
}

int un7zip_and_gzip(const char *filename, const char *folder, int compressionLevel, char **playlistFilename);
void un7zip_free_pl_fn( char *playlistFilename );


//-----------------------------------------------------------------
// Play filename
//-----------------------------------------------------------------
int play(const char *fn) { 
  int maxlatency;
  int thread_id;
  int file_size;
  struct TVGMHeader VGMHeader;
  int i;
  char *p;

  InputFile=NULL;

	// vgm7z handler
  p=strrchr(fn,'.');
  if ( (p) && (stricmp(p,".vgm7z")==0) )
	{
		int result;
		char *playlistFilename;
		char *folder;

		if (!vgm7z_enable) return -1;

    if (vgm7z_extract_prompt && (MessageBox(mod.hMainWindow,"Do you want to decompress this VGM7z package?",PLUGINNAME,MB_YESNO)==IDNO))
      return -1;

		if (vgm7z_extract_same)
		{
			// get folder name
			folder = strdup(fn);
			p = strrchr(folder,'\\');
			if ( p != NULL )
				*p='\0'; // cut off before last backslash
		}
		else
		{
			// predefined dir
			folder = strdup(vgm7z_extract_dir);
			// remove any trailing backslash
			if (folder[strlen(folder)-1] == '\\')
				folder[strlen(folder)-1] = '\0';
		}

		if (vgm7z_extract_subdir)
		{
      // get filename
			char *dot = strrchr(fn,'.');
			char *slash = strrchr(fn,'\\');
      if (dot && slash)
			{
				int len = dot - slash; // length of filename (including a slash)
				int oldlen = strlen(folder);
				folder = realloc(folder, oldlen + len + 1); // +1 for terminator
				strncat(folder,slash,len);
				folder[oldlen + len]='\0'; // no final slash yet
			}
		}

		// try to create the dir, error if it fails for any reason other than the fact it already exists
		if (!CreateDirectory( folder, NULL ) && GetLastError() != ERROR_ALREADY_EXISTS)
			result = 1;
		else
		{
			// go ahead and extract it
			result = un7zip_and_gzip(fn, folder, vgm_compression_level, &playlistFilename);
		}
		free(folder);

		if (result)
		{
			// error
			MessageBox(mod.hMainWindow,"Error decompressing VGM package",p,MB_OK+MB_ICONERROR);
      return -1;
    }
		else
		{
			// replace file with playlist
			if ( playlistFilename != NULL )
			{
				// all this is to add the playlist at the desired index
				HWND h = (HWND)SendMessage( mod.hMainWindow, WM_WA_IPC, IPC_GETWND_PE, IPC_GETWND );
				int pos = SendMessage( h, WM_WA_IPC, IPC_PE_GETCURINDEX, 0 );
				int count = SendMessage( h, WM_WA_IPC, IPC_PE_GETINDEXTOTAL, 0);
				COPYDATASTRUCT cds;
				fileinfo f;

				strncpy(f.file, playlistFilename, MAX_PATH);
				f.index = pos + 1; // insert after current index

				cds.dwData = IPC_PE_INSERTFILENAME;
				cds.lpData = (PVOID)&f;
				cds.cbData = sizeof(fileinfo);

				SendMessage( h, WM_COPYDATA, 0, (LPARAM)&cds );

				// remove the original item
				SendMessage( h, WM_WA_IPC, IPC_PE_DELETEINDEX, pos );

				// unpacked... delete original
				if (vgm7z_delete_after_extract)
					DeleteFile(fn);
			}
			else
			{
				// error
				MessageBox(mod.hMainWindow,"Playlist not found in VGM7z package",p,MB_OK+MB_ICONERROR);
				return -1;
			}

			un7zip_free_pl_fn( playlistFilename );
      InputFile = NULL;
			return -1; // return "file not found" to skip on to the next track
		}
	} // end VGM7z handler

	if (CurrentURL)
		free(CurrentURL);

	if (CurrentURLFilename)
		free(CurrentURLFilename);

  if (IsURL(fn))
	{  // URL handler
    int (*httpRetrieveFile)(HWND hwnd, char *url, char *file, char *dlgtitle);
    int error;
    char *EncodedURL;

		CurrentURL = malloc(strlen(fn)+1);
    strcpy(CurrentURL,fn);

    error=SendMessage(mod.hMainWindow,WM_USER,(WPARAM)NULL,IPC_GETHTTPGETTER);  // get HTTP getter function

    if (error>1) {
			int bufsize = GetTempPath( 0, NULL ) + 10;
			CurrentURLFilename=malloc( bufsize );
			GetTempPath( bufsize, CurrentURLFilename );
      strcat( CurrentURLFilename, "temp.vgm" );
      
      httpRetrieveFile=(void *)error;
			EncodedURL=URLEncode(CurrentURL);
      error=httpRetrieveFile(mod.hMainWindow,EncodedURL,CurrentURLFilename,mod.description);
      free(EncodedURL);
    }
		else
		{
      error=1;
		}

    if(!error) {
      fn = CurrentURLFilename;
    } else {
      return -1;  // File not found
    }

  } // end URL handler

  if ((!MutePersistent) && (*lastfn) && (strcmp(fn,lastfn)!=0)) {
    // If file has changed, reset channel muting (if not blocked)
    SN76489_Mute  =SN76489_MUTE_ALLON;
    YM2413Channels=YM2413_MUTE_ALLON;
    YM2612Channels=YM2612_MUTE_ALLON;
    YM2151Channels=YM2151_MUTE_ALLON;
  }

  // If wanted, randomise panning
  if ( RandomisePanning )
  {
    for ( i = 0; i < SN76489_NUM_CHANNELS; ++i )
      SN76489_Pan[i] = random_stereo();
    for ( i = 0; i < YM2413_NUM_CHANNELS; ++i )
      YM2413_Pan[i] = random_stereo();
  }

  strcpy(lastfn,fn);
  
  // Blargg - free PCM data
  if (pcm_data) free( pcm_data );
  pcm_data = NULL;
  pcm_data_size = 0;
  pcm_pos = 0;

  InputFile=gzopen(fn,"rb");  // Open file - read, binary

  if (InputFile==NULL) {
    return -1;
  }  // File not opened/found, advance playlist

  // Get file size for bitrate calculations 
  file_size=FileSize(fn);

  // Read header
  i=gzread(InputFile,&VGMHeader,sizeof(VGMHeader));

  // Check it read OK
  if (i<sizeof(VGMHeader)) {
    char msgstring[1024];
    sprintf(msgstring,"File too short:\n%s",fn);
    MessageBox(mod.hMainWindow,msgstring,mod.description,0);
    gzclose(InputFile);
    InputFile=NULL;
    return -1;
  }

  // Check for VGM marker
  if (VGMHeader.VGMIdent != VGMIDENT) {
    char msgstring[1024];
    sprintf(msgstring,"VGM marker not found in \"%s\"",fn);
    MessageBox(mod.hMainWindow,msgstring,mod.description,0);
    gzclose(InputFile);
    InputFile=NULL;
    return -1;
  }

  // Check version
  if ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER)) {
    char msgstring[1024];
    sprintf(msgstring,"Unsupported VGM version found in \"%s\" (%x).\n\nDo you want to try to play it anyway?",fn,VGMHeader.Version);

    if (MessageBox(mod.hMainWindow,msgstring,mod.description,MB_YESNO+MB_DEFBUTTON2)==IDNO) {
      gzclose(InputFile);
      InputFile=NULL;
      return -1;
    }
  }

  // Fix stuff for older version files:
  // Nothing to fix for 1.01
  if ( VGMHeader.Version < 0x0110 ) {
    VGMHeader.PSGWhiteNoiseFeedback = 0x0009;
    VGMHeader.PSGShiftRegisterWidth = 16;
    VGMHeader.YM2612Clock = VGMHeader.YM2413Clock;
    VGMHeader.YM2151Clock = VGMHeader.YM2413Clock;
  }

  // handle VGM data offset
  if ( VGMHeader.VGMDataOffset == 0 )
    VGMDataOffset = 0x40;
  else
    // header has a value, but it is relative; make it absolute
    VGMDataOffset = VGMHeader.VGMDataOffset + VGMDATADELTA;

  // Get length
  if (VGMHeader.TotalLength==0) {
    TrackLengthInms=0;
  } else {
    TrackLengthInms=(int)(VGMHeader.TotalLength/44.1);
  }

  // Get loop data
  if (VGMHeader.LoopLength==0) {
    LoopLengthInms=0;
    LoopOffset=0;
  } else {
    LoopLengthInms=(int)(VGMHeader.LoopLength/44.1);
    LoopOffset=VGMHeader.LoopOffset+0x1c;
  }

  // Get clock values
  SNClock=VGMHeader.PSGClock;
  YM2413Clock=VGMHeader.YM2413Clock;
  YM2612Clock=VGMHeader.YM2612Clock;
  YM2151Clock=VGMHeader.YM2151Clock;

  // BlackAura - Disable all FM chips
  FMChips=0;

  // Get rate
  FileRate=VGMHeader.RecordingRate;

	// get Replay Gain stuff
	getReplayGainSettings(fn, &ReplayGain, &ReplayPeak, &ReplayNoClip);

  // Open output plugin
  maxlatency = mod.outMod->Open(SAMPLERATE,NCH,16, -1,-1);
  if (maxlatency < 0) {  // error opening device
    gzclose(InputFile);
    InputFile=NULL;
    return 1;
  }

  // Set info
  if (TrackLengthInms==0) {
    mod.SetInfo(0,SAMPLERATE/1000,NCH,1);
  } else {
    mod.SetInfo(
      (int)(file_size*8000.0/1000/TrackLengthInms+0.5),  // Bitrate /kbps (+0.5 for rounding)
      SAMPLERATE/1000,      // Sampling rate /kHz
      NCH,            // Channels
      1);              // Synched (?)
  }

  // Open page in MB if wanted
  if (UseMB & AutoMB) {
    InfoInBrowser(fn,UseMB,ForceMBOpen);
    SendMessage(mod.hMainWindow,WM_USER,1,IPC_MBBLOCK); // block any more things opening in there
  }

  // initialize vis stuff
  mod.SAVSAInit(maxlatency,SAMPLERATE);
  mod.VSASetInfo(NCH,SAMPLERATE);

  mod.outMod->SetVolume(-666); // set the output plug-ins default volume

  gzseek(InputFile,VGMDataOffset,SEEK_SET);

  // FM Chip startups are done whenever a chip is used for the first time

  // Start up SN76489 (if used)
  if (SNClock) {
    snchip = SN76489_Init(SNClock,SAMPLERATE);
		if(snchip)
		{
			SN76489_Config(snchip,SN76489_Mute,VGMHeader.PSGWhiteNoiseFeedback, VGMHeader.PSGShiftRegisterWidth, ((int)(VGMHeader.YM2612Clock/1000000)==7?0:1) ); // nasty hack: boost noise except for YM2612 music
			SN76489_SetPanning(snchip,SN76489_Pan[0],SN76489_Pan[1],SN76489_Pan[2],SN76489_Pan[3]);
		}
  }

  // Reset some stuff
  paused=0;
  NumLoopsDone=0;
  SeekToSampleNumber=-1;
	SeekToTimeInms = 0;
  PauseBetweenTracksCounter=-1;  // signals "not pausing"; 0+ = samples left to pause
  LoopingFadeOutTotal=-1;      // signals "haven't started fadeout yet"

  // Start up decode thread
  killDecodeThread=0;
  thread_handle = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE) DecodeThread,(void *) &killDecodeThread,0,&thread_id);
  // Set it to high priority to avoid breaks
#ifdef _DEBUG
  SetThreadPriority(thread_handle,THREAD_PRIORITY_LOWEST); // go low on debug to keep the debugger responsive on lockup - can cause breaks in the audio
#else
  SetThreadPriority(thread_handle,THREAD_PRIORITY_ABOVE_NORMAL);
#endif
  
  return 0; 
}

//-----------------------------------------------------------------
// Pause
//-----------------------------------------------------------------
void pause() {
  paused=1;
  mod.outMod->Pause(1);
}

//-----------------------------------------------------------------
// Unpause
//-----------------------------------------------------------------
void unpause() {
  paused=0;
  mod.outMod->Pause(0);
}

//-----------------------------------------------------------------
// Is it paused?
//-----------------------------------------------------------------
int ispaused() {
  return paused;
}

//-----------------------------------------------------------------
// Stop
//-----------------------------------------------------------------
void stop() {
  SeekToSampleNumber=0;  // Fixes near-eof errors - it breaks it out of the wait-for-output-to-stop loop in DecodeThread
  if (thread_handle != INVALID_HANDLE_VALUE) {  // If the playback thread is going
    killDecodeThread=1;  // Set the flag telling it to stop
    if (WaitForSingleObject(thread_handle,INFINITE) == WAIT_TIMEOUT) {
      MessageBox(mod.hMainWindow,"error asking thread to die!",mod.description,0);
      TerminateThread(thread_handle,0);
    }
    CloseHandle(thread_handle);
    thread_handle = INVALID_HANDLE_VALUE;
  }
  if (InputFile!=NULL) {
    gzclose(InputFile);  // Close input file
    InputFile=NULL;
  }

  mod.outMod->Close();  // close output plugin

  mod.SAVSADeInit();  // Deinit vis

  // Stop YM2413
  if USINGCHIP(FM_YM2413) 
    OPLL_delete(opll);

  // Stop YM2612
  if USINGCHIP(FM_YM2612)
    YM2612_End(ym2612);

  // Stop YM2151
  if USINGCHIP(FM_YM2151)
    YM2151Shutdown();

  // Stop SN76489
  // not needed

	FMChips = 0; // reset chip usage flags
	SNClock = 0;

  SendMessage(mod.hMainWindow,WM_USER,0,IPC_MBBLOCK); // let Now Playing work as normal
}

//-----------------------------------------------------------------
// Get track length in ms
//-----------------------------------------------------------------
int getlength() {
  int l=(int)((TrackLengthInms+NumLoops*LoopLengthInms)*((PlaybackRate&&FileRate)?(double)FileRate/PlaybackRate:1));
  if (!LoopForever && l>mod.outMod->GetOutputTime())
    return l;
  else return -1000;
}

//-----------------------------------------------------------------
// Get playback position in ms - sent to output plugin
//-----------------------------------------------------------------
int getoutputtime() {
	if ( SeekToTimeInms )
		return SeekToTimeInms;
	else
	  return mod.outMod->GetOutputTime();
}

//-----------------------------------------------------------------
// Seek
//-----------------------------------------------------------------
void setoutputtime(int time_in_ms) {
	SeekToTimeInms = time_in_ms;
}

//-----------------------------------------------------------------
// Set volume - sent to output plugin
//-----------------------------------------------------------------
void setvolume(int volume) {
  mod.outMod->SetVolume(volume);
}

//-----------------------------------------------------------------
// Set balance - sent to output plugin
//-----------------------------------------------------------------
void setpan(int pan) {
  mod.outMod->SetPan(pan);
}

int infoDlg(const char *fn, HWND hwnd)
{
  FilenameForInfoDlg=fn;
  return DialogBox(mod.hDllInstance, "FILEINFODIALOGUE", hwnd, FileInfoDialogProc);
}

//-----------------------------------------------------------------
// Get file info for playlist/title display
//-----------------------------------------------------------------
void getfileinfo(const char *filename, char *title, int *length_in_ms) {
  long int TrackLength = -1000; // default to no length
  char TrackTitle[1024];
	char *FileToUse, *JustFileName;
  gzFile  *fh;
  struct TVGMHeader  VGMHeader;
  struct TGD3Header  GD3Header;
  int i;

  // if filename is blank then we want the current file
  if ((filename==NULL) || (*filename=='\0'))
    FileToUse=lastfn;
  else
    FileToUse=(char *)filename;

	if (IsURL(FileToUse)) {
    if (title) strcpy(title,FileToUse);
    return;
  }

  JustFileName=strrchr(FileToUse,'\\');
	if(!JustFileName)
		JustFileName=FileToUse;
	else
		JustFileName++;

  // get GD3 info

  fh=gzopen(FileToUse,"rb");
  if (fh==0) {  // file not opened
    if (title) sprintf(title,"Unable to open %s",FileToUse);
    if (length_in_ms) *length_in_ms=-1000;
    return;
  }
  i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

  if (i<sizeof(VGMHeader)) {
    sprintf(TrackTitle,"File too short: %s",JustFileName);
  } else if (VGMHeader.VGMIdent != VGMIDENT) {
    // VGM marker incorrect
    sprintf(TrackTitle,"VGM marker not found in %s",JustFileName);
  } else if ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER)) {
    // VGM version incorrect
    sprintf(TrackTitle,"Unsupported version (%x) in %s",VGMHeader.Version,JustFileName);
  } else {
    // VGM header OK
    if ( LoopForever )
      TrackLength = -1000;
    else
      TrackLength=(long) (
        (VGMHeader.TotalLength+NumLoops*VGMHeader.LoopLength)
        /44.1
        *((PlaybackRate&&VGMHeader.RecordingRate)?(double)VGMHeader.RecordingRate/PlaybackRate:1)
//        +PauseBetweenTracksms+(VGMHeader.LoopOffset?LoopingFadeOutms:0)
      );

    if (VGMHeader.GD3Offset>0) {
      // GD3 tag exists
      gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
      i=gzread(fh,&GD3Header,sizeof(GD3Header));
      if ((i==sizeof(GD3Header)) &&
        (GD3Header.GD3Ident == GD3IDENT) &&
        (GD3Header.Version>=MINGD3VERSION) &&
        ((GD3Header.Version & REQUIREDGD3MAJORVER)==REQUIREDGD3MAJORVER)) {
          // GD3 is acceptable version
          wchar *p,*GD3string;
          const char strings[]="tgsadc";  // Title Game System Author Date Creator
          char GD3strings[10][256];
          const char What[10][12]={"track title","track title","game","game","system","system","author","author","date","creator"};
          int i;

          p=malloc(GD3Header.Length);  // Allocate memory for string data
          gzread(fh,p,GD3Header.Length);  // Read it in
          GD3string=p;

          for (i=0;i<10;++i) {
            // Get next string and move the pointer to the next one
            WideCharToMultiByte(CP_ACP,0,GD3string,-1,GD3strings[i],256,NULL,NULL);
            GD3string+=wcslen(GD3string)+1;
          }

          free(p);  // Free memory for string buffer

          for (i=0;i<10;++i) if (!GD3strings[i][0]) {  // Handle empty fields...
            // First, see if I can substitute the other language
            if (i<8) strcpy(GD3strings[i],GD3strings[(i%2?i-1:i+1)]);
            // if it is still blank, put "Unknown xxx"
            if (!GD3strings[i][0]) {
              strcpy(GD3strings[i],"Unknown ");
              strcat(GD3strings[i],What[i]);
            }
          }

          strcpy(TrackTitle,TrackTitleFormat);

          i=0;
          while (i<6) {
            char SearchStr[]="%x";
            char *pos;
            // Search for format strings
            SearchStr[1]=strings[i];
            pos=strstr(TrackTitle,SearchStr);
            if (pos!=NULL) {  // format string found
              // redo this to use one string?
              char After[1024];
              *pos='\0';
              strcpy(After,TrackTitle);  // copy text before it
              if ((*(pos+2)=='j') && (i<4)) {
                strcat(After,GD3strings[i*2+1]);  // add GD3 string
                strcat(After,pos+3);  // add text after it
              } else {
                if (i==5) {
                  strcat(After,GD3strings[9]);
                } else {
                  strcat(After,GD3strings[i*2]);  // add GD3 string
                }
                strcat(After,pos+2);  // add text after it
              }
              strcpy(TrackTitle,After);
            } else {
              i++;
            }
          }
      } else {
        // Problem with GD3
        sprintf(TrackTitle,"GD3 invalid: %s",JustFileName);
      }
    } else {  // No GD3 tag, so use filename
      strcpy(TrackTitle,JustFileName);
    }
  }
  gzclose(fh);

  if (title) strcpy(title,TrackTitle);
  if (length_in_ms) *length_in_ms=TrackLength;
}

#ifdef DEBUG
void debuglog(int number) {
  FILE *f;
  f=fopen("c:\\in_vgm log.txt","a");
  fprintf(f,"%d\n",number);
  fclose(f);
}
#endif

//-----------------------------------------------------------------
// Input-side EQ - not used
//-----------------------------------------------------------------
void eq_set(int on, char data[10], int preamp) {}

//-----------------------------------------------------------------
// Decode thread
//-----------------------------------------------------------------
#define ReadByte() gzgetc(InputFile)
DWORD WINAPI __stdcall DecodeThread(void *b) {
  int SamplesTillNextRead=0;
  float WaitFactor,FractionalSamplesTillNextRead=0;

  if ((PlaybackRate==0) || (FileRate==0)) {
    WaitFactor=1.0;
  } else {
    WaitFactor=(float)FileRate/PlaybackRate;
  }

  while (! *((int *)b) ) {
    if (
      mod.outMod->CanWrite()  // Number of bytes I can write
      >=
      (int)(
        SampleBufferSize*2    // Size of buffer in bytes
        <<
        (mod.dsp_isactive()?1:0)  // x2 if DSP is active
      )
    ) {
      int samplesinbuffer=SampleBufferSize/NCH;
      int x;

      int b1,b2;

			// handle seeking inside this thread's loop to avoid nastiness
			if ( SeekToTimeInms )
			{
				if (InputFile==NULL) break;

				if (getlength()<0) break; // disable seeking on fadeout/silence

				if USINGCHIP(FM_YM2413) {  // If using YM2413, reset it
					int i;
					long int YM2413Channels = OPLL_toggleMask( opll, 0 );
					OPLL_reset( opll );
					OPLL_setMask( opll, YM2413Channels );
					for ( i = 0; i < YM2413_NUM_CHANNELS; ++i )
						OPLL_set_pan( opll, i, YM2413_Pan[i] );
				}

				if USINGCHIP( FM_YM2612 ) {
					YM2612_Reset(ym2612);
				}

				if USINGCHIP( FM_YM2151 ) {
					YM2151ResetChip( 0 );
				}

				mod.outMod->Flush( SeekToTimeInms );

				gzseek( InputFile, VGMDataOffset, SEEK_SET );
				NumLoopsDone = 0;

				if ( LoopLengthInms > 0 ) {  // file is looped
					// See if I can skip some loops
					while ( SeekToTimeInms > TrackLengthInms ) {
						++NumLoopsDone;
						SeekToTimeInms -= LoopLengthInms;
					}
				} else {        // Not looped
					if ( SeekToTimeInms > TrackLengthInms ) NumLoopsDone = NumLoops + 1; // for seek-past-eof in non-looped files
				}
				SeekToSampleNumber=(int)(SeekToTimeInms * 44.1);
				SeekToTimeInms = 0;

				// If seeking beyond EOF...
				if (NumLoopsDone>NumLoops) {
					// Tell Winamp it's the end of the file
					while (1) {
						mod.outMod->CanWrite();  // hmm... does something :P
						if (!mod.outMod->IsPlaying()) {  // if the buffer has run out
							PostMessage(mod.hMainWindow,WM_WA_MPEG_EOF,0,0);  // tell WA it's EOF
							break;
						}
						Sleep(10);  // otherwise wait 10ms and try again
					}
				}
			}

      for (x=0;x<samplesinbuffer/2;++x) {
        // Read file, write stuff
				while ( SamplesTillNextRead == 0 && PauseBetweenTracksCounter == -1 ) {
          switch (b1=ReadByte()) {
          case VGM_GGST:  // GG stereo
            b1=ReadByte();
            if (SNClock) SN76489_GGStereoWrite(snchip,(char)b1);
            break;
          case VGM_PSG:  // SN76489 write
            b1=ReadByte();
            if (SNClock) SN76489_Write(snchip,(char)b1);
            break;
          case VGM_YM2413:  // YM2413 write
            b1=ReadByte();
            b2=ReadByte();
            if (YM2413Clock) {
              if (!USINGCHIP(FM_YM2413)) {  // BlackAura - If YM2413 emu not started, start it
                int i;
                // Start the emulator up
                opll=OPLL_new(YM2413Clock,SAMPLERATE);
                OPLL_reset(opll);
                OPLL_reset_patch(opll,0);
                OPLL_setMask(opll,YM2413Channels);
                for ( i = 0; i< YM2413_NUM_CHANNELS; ++i )
                  OPLL_set_pan( opll, i, YM2413_Pan[i] );
                OPLL_set_quality(opll,YM2413HiQ);
                // Set the flag for it
                FMChips|=FM_YM2413;
              }
              OPLL_writeReg(opll,b1,b2);  // Write to the chip
            }
            break;
          case VGM_YM2612_0:  // YM2612 write (port 0)
            b1=ReadByte();
            b2=ReadByte();
            if (YM2612Clock) {
              if (!USINGCHIP(FM_YM2612)) {
                ym2612 = YM2612_Init(YM2612Clock,SAMPLERATE,0);
                YM2612_SetMute(ym2612, YM2612Channels);
                FMChips|=FM_YM2612;
              }
              YM2612_Write(ym2612, 0,b1);
              YM2612_Write(ym2612, 1,b2);
            }
            break;
          case VGM_YM2612_1:  // YM2612 write (port 1)
            b1=ReadByte();
            b2=ReadByte();
            if (YM2612Clock) {
              if (!USINGCHIP(FM_YM2612)) {
                ym2612 = YM2612_Init(YM2612Clock,SAMPLERATE,0);
                YM2612_SetMute(ym2612, YM2612Channels);
                FMChips|=FM_YM2612;
              }
              YM2612_Write(ym2612, 2,b1);
              YM2612_Write(ym2612, 3,b2);
            }
            break;
          case VGM_YM2151:  // BlackAura - YM2151 write
            b1=ReadByte();
            b2=ReadByte();
            if (YM2151Clock) {
              if (!USINGCHIP(FM_YM2151)) {
                YM2151Init(1,YM2151Clock,SAMPLERATE);
                FMChips|=FM_YM2151;
              }
              YM2151WriteReg(0,b1,b2);
            }
            break;
          case VGM_PAUSE_WORD:  // Wait n samples
            b1=ReadByte();
            b2=ReadByte();
            SamplesTillNextRead=b1 | (b2 << 8);
            break;
          case VGM_PAUSE_60TH:  // Wait 1/60 s
            SamplesTillNextRead=LEN60TH;
            break;
          case VGM_PAUSE_50TH:  // Wait 1/50 s
            SamplesTillNextRead=LEN50TH;
            break;
          // No-data waits
          case 0x70:
          case 0x71:
          case 0x72:
          case 0x73:
          case 0x74:
          case 0x75:
          case 0x76:
          case 0x77:
          case 0x78:
          case 0x79:
          case 0x7a:
          case 0x7b:
          case 0x7c:
          case 0x7d:
          case 0x7e:
          case 0x7f:
            SamplesTillNextRead=(b1 & 0xf) + 1;
            break;

          // (YM2612 sample then short delay)s
          case 0x80:  // YM2612 write (port 0) 0x2A (PCM)
          case 0x81:
          case 0x82:
          case 0x83:
          case 0x84:
          case 0x85:
          case 0x86:
          case 0x87:
          case 0x88:
          case 0x89:
          case 0x8A:
          case 0x8B:
          case 0x8C:
          case 0x8D:
          case 0x8E:
          case 0x8F:
            SamplesTillNextRead = b1 & 0x0f;
            if (YM2612Clock && pcm_data) {
              b1 = 0x2A;
              b2 = pcm_data [pcm_pos++];
              assert( pcm_pos <= pcm_data_size );

              if (!USINGCHIP(FM_YM2612)) {
                YM2612_Init(YM2612Clock,SAMPLERATE,0);
                FMChips|=FM_YM2612;
              }
              YM2612_Write(ym2612, 0,b1);
              YM2612_Write(ym2612, 1,b2);
            }
            break;
            
          case VGM_YM2612_PCM_SEEK: { // Set position in PCM data
            unsigned char buf [4];
            gzread( InputFile, buf, sizeof buf );
            pcm_pos = buf [3] * 0x1000000L + buf [2] * 0x10000L + buf [1] * 0x100L + buf [0];
            assert( pcm_pos < pcm_data_size );
            break;
          }
          
          case VGM_DATA_BLOCK: { // data block at beginning of file
            unsigned char buf[6];
            unsigned long data_size;
            gzread( InputFile, buf, sizeof(buf));
            assert( buf[0] == 0x66 );
            data_size = (buf[5] << 24) | (buf[4] << 16) | (buf[3] << 8) | buf[2];
            switch (buf[1]) {
            case VGM_DATA_BLOCK_YM2612_PCM:
              if ( !pcm_data )
              {
                pcm_data_size = data_size;
            	  pcm_data = malloc( pcm_data_size );
            	  if ( pcm_data )
            	  {
            		  gzread( InputFile, pcm_data, pcm_data_size );
            		  break; // <-- exits out of (nested) case block on successful load
            	  }
              }
              // ignore data block for subsequent blocks and also on malloc() failure
        	    gzseek( InputFile, pcm_data_size, SEEK_CUR );
              break;
            default:
              // skip unknown data blocks
              gzseek( InputFile, data_size, SEEK_CUR );
              break;
            }
            
        	  break;
          }

          case VGM_END:  // End of data
            ++NumLoopsDone;  // increment loop count
            // If there's no looping then go to the inter-track pause
            if (LoopOffset==0) {
              if ((PauseBetweenTracksms)&&(PauseBetweenTracksCounter==-1)) {
                // I want to output silence
                PauseBetweenTracksCounter=(long)PauseBetweenTracksms*44100/1000;
              } else {
                // End track
                while (1) {
                  mod.outMod->CanWrite();  // hmm... does something :P
                  if (!mod.outMod->IsPlaying()) {  // if the buffer has run out
                    PostMessage(mod.hMainWindow,WM_WA_MPEG_EOF,0,0);  // tell WA it's EOF
                    return 0;
                  }
                  Sleep(10);  // otherwise wait 10ms and try again
                  if (SeekToSampleNumber>-1) break;
                }
              }
              gzungetc(VGM_END,InputFile);
            } else {
              // if there is looping, and the required number of loops have played, then go to fadeout
              if ( (!LoopForever) && (NumLoopsDone>NumLoops) && (LoopingFadeOutTotal==-1) ) {
                // Start fade out
                LoopingFadeOutTotal=(long)LoopingFadeOutms*44100/1000;  // number of samples to fade over
                LoopingFadeOutCounter=LoopingFadeOutTotal;
              }
              // Loop the file
              gzseek(InputFile,LoopOffset,SEEK_SET);
            }
            break;

          default:
            // Unknown commands
            if ( b1 >= VGM_RESERVED_1_PARAM_BEGIN && b1 <= VGM_RESERVED_1_PARAM_END )
              gzseek(InputFile,1,SEEK_CUR);
            else if ( b1 >= VGM_RESERVED_2_PARAMS_BEGIN && b1 <= VGM_RESERVED_2_PARAMS_END )
              gzseek(InputFile,2,SEEK_CUR);
            else if ( b1 >= VGM_RESERVED_3_PARAMS_BEGIN && b1 <= VGM_RESERVED_3_PARAMS_END )
              gzseek(InputFile,3,SEEK_CUR);
            else if ( b1 >= VGM_RESERVED_4_PARAMS_BEGIN && b1 <= VGM_RESERVED_4_PARAMS_END )
              gzseek(InputFile,4,SEEK_CUR);
#if _DEBUG
						{
							char buffer[10*1024];
							sprintf(buffer,"Invalid data 0x%02x at offset 0x%06x in file \"%s\". Debug?",b1,gztell(InputFile)-1, lastfn);
							OutputDebugString( buffer );
							if (MessageBox(mod.hMainWindow, buffer, "Debug error: invalid data", MB_ICONERROR|MB_YESNO) == IDYES)
								assert(FALSE);
						}
#endif
            break;
          }  // end case

          // debug
//          if(SamplesTillNextRead)debuglog(SamplesTillNextRead);
          if (SamplesTillNextRead)
          {
            FractionalSamplesTillNextRead+=SamplesTillNextRead*WaitFactor;
            SamplesTillNextRead=(int)FractionalSamplesTillNextRead;
            FractionalSamplesTillNextRead-=SamplesTillNextRead;

            if (SeekToSampleNumber>-1) {  // If a seek is wanted
              SeekToSampleNumber-=SamplesTillNextRead;  // Decrease the required seek by the current delay
              SamplesTillNextRead=0;
              if (SeekToSampleNumber<0) {
                SamplesTillNextRead=-SeekToSampleNumber;
                SeekToSampleNumber=-1;
              }
              continue;
            }
          }
        }  // end while

        //SamplesTillNextRead<<=4; // speed up by 2^4 = 16 (for stress testing)

        // Write sample
        if (PauseBetweenTracksCounter==-1) {
          int NumChipsUsed=0;
          signed int l=0,r=0;

          // PSG
          if (SNClock) {
            NumChipsUsed++;
            if(SN_enable)
            {
              SN76489_UpdateOne(snchip,&l,&r);
              l = (int)(l * SN_preamp);
              r = (int)(r * SN_preamp);
            }
          }

          if (YM2413Clock) {
            // YM2413
            if USINGCHIP(FM_YM2413) {
              int channels[2];
              NumChipsUsed++;
              if(YM2413_enable)
              {
                OPLL_calc_stereo(opll,channels);
                l += (int)(channels[0] * YM2413_preamp);
                r += (int)(channels[1] * YM2413_preamp);
              }
            }
          }

          if (YM2612Clock) {
            // YM2612
            if USINGCHIP(FM_YM2612) {
              int *Buffer[2];
              int Left=0,Right=0;
              NumChipsUsed++;
              Buffer[0]=&Left;
              Buffer[1]=&Right;
              if (YM2612_enable) {
                YM2612_Update(ym2612, Buffer,1);
                YM2612_DacAndTimers_Update(ym2612, Buffer,1);
                Left =(int)(Left  * YM2612_preamp);
                Right=(int)(Right * YM2612_preamp);
              } else
                Left=Right=0;  // Dodgy muting until per-channel gets done
            
              l+=Left;
              r+=Right;
            }
          }

          if (YM2151Clock) {
            // YM2151
            if USINGCHIP(FM_YM2151) {
              signed short *mameBuffer[2];
              signed short mameLeft;
              signed short mameRight;
              NumChipsUsed++;
              mameBuffer[0]=&mameLeft ;
              mameBuffer[1]=&mameRight;
              if (YM2151_enable) {
                YM2151UpdateOne(0,mameBuffer,1);
                mameLeft =(short)(mameLeft  * YM2151_preamp);
                mameRight=(short)(mameRight * YM2151_preamp);
              } else
                mameLeft=mameRight=0;  // Dodgy muting until per-channel gets done

              l+=mameLeft ;
              r+=mameRight;
            }
          }

					/* do any filtering */
					if ( filter_type != FILTER_NONE )
					{
						int pre_filter_l = l, pre_filter_r = r;

						if ( filter_type == FILTER_LOWPASS )
						{
              // output = average of current and previous sample
							l += prev_sample[0];
							l >>= 1;
							r += prev_sample[1];
							r >>= 1;
						}
						else
						{
							// output = current sample * 0.75 + previous sample * 0.25
							l = (l + l + l + prev_sample[0]) >> 2;
							r = (r + r + r + prev_sample[1]) >> 2;
						}

						prev_sample[0] = pre_filter_l;
						prev_sample[1] = pre_filter_r;
					}
/*
					if ((Overdrive)&&(NumChipsUsed)) {
            l=l*8/NumChipsUsed;
            r=r*8/NumChipsUsed;
          }
*/
          if (LoopingFadeOutTotal!=-1) { // Fade out
            long v;
            // Check if the counter has finished
            if (LoopingFadeOutCounter<=0) {
              // if so, go to pause between tracks
              PauseBetweenTracksCounter=(long)PauseBetweenTracksms*44100/1000;
            } else {
              // Alter volume
              v=LoopingFadeOutCounter*MAX_VOLUME/LoopingFadeOutTotal;
              l=(long)l*v/MAX_VOLUME;
              r=(long)r*v/MAX_VOLUME;
              // Decrement counter
              --LoopingFadeOutCounter;
            }
          }

          // Clip values
          if (l>+32767) l=+32767;  else if (l<-32767) l=-32767;
          if (r>+32767) r=+32767;  else if (r<-32767) r=-32767;

          SampleBuffer[2*x]  =l;
          SampleBuffer[2*x+1]=r;

        } else {
          // Pause between tracks
          // output silence
          SampleBuffer[2*x]  =0;
          SampleBuffer[2*x+1]=0;
          --PauseBetweenTracksCounter;
          if (PauseBetweenTracksCounter<=0) {
            while (1) {
              if (SeekToSampleNumber>-1) break;
              mod.outMod->CanWrite();  // hmm... does something :P
              if (!mod.outMod->IsPlaying()) {  // if the buffer has run out
                PostMessage(mod.hMainWindow,WM_WA_MPEG_EOF,0,0);  // tell WA it's EOF
                return 0;
              }
              Sleep(10);  // otherwise wait 10ms and try again
            }
          }
        }

        --SamplesTillNextRead;
      }

			// Replay Gain
			if(ReplayGain!=1.0)
				apply_replay_gain_16bit(ReplayGain,ReplayPeak,SampleBuffer,samplesinbuffer,NCH,ReplayNoClip,1); // TODO: make hard limit an option
  
      x=mod.outMod->GetWrittenTime();  // returns time written in ms (used for synching up vis stuff)
      // Check these two later (not important)
      mod.SAAddPCMData ((char *)SampleBuffer,NCH,16,x);  // Add to vis
      mod.VSAAddPCMData((char *)SampleBuffer,NCH,16,x);  // Add to vis

      samplesinbuffer=mod.dsp_dosamples(
        (short *)SampleBuffer,  // Samples
        samplesinbuffer/NCH,  // No. of samples (?)
        16,            // Bits per sample
        NCH,          // Number of channels
        SAMPLERATE        // Sampling rate
      );
      mod.outMod->Write(
        (char *)SampleBuffer,  // Buffer
        samplesinbuffer*NCH*2  // Size of data in bytes
      );
    }
    else Sleep(50);
  }
  return 0;
}

//-----------------------------------------------------------------
// Plugin definition structure
//-----------------------------------------------------------------
In_Module mod = 
{
  IN_VER,
  PLUGINNAME,
  0,  // hMainWindow
  0,  // hDllInstance
  "vgm;vgz;vgm7z\0VGM Audio File (*.vgm;*.vgz;*.vgm7z)\0",
  1,  // is_seekable
  1,  // uses output
  config,
  about,
  init,
  quit,
  getfileinfo,
  infoDlg,
  isourfile,
  play,
  pause,
  unpause,
  ispaused,
  stop,
  getlength,
  getoutputtime,
  setoutputtime,
  setvolume,
  setpan,
  0,0,0,0,0,0,0,0,0, // vis stuff
  0,0, // dsp
  eq_set,
  NULL,    // setinfo
  0 // out_mod
};

__declspec(dllexport) In_Module *winampGetInModule2() {
  return &mod;
}


__declspec( dllexport ) intptr_t winampGetExtendedRead_open(const char *fn, int *size, int *bps, int *nch, int *srate)
{
	VGMCore *core = vgmcore_init();
	// HACK: don't calculate Replay Gain if the Replay Gain window is open
	int killReplayGain = TRUE;
	if (enableReplayGainHack)
		killReplayGain = FindWindow(NULL, "Calculating Replay Gain") != NULL;

	if (core && vgmcore_loadfile( core, fn, killReplayGain ) == 0 )
	{
		core->NumLoops = (killReplayGain?0:NumLoops);
		core->LoopingFadeOutms = (killReplayGain?0:LoopingFadeOutms);
		core->filter_type = filter_type;
		core->LoopForever = 0; // for safety for Replay Gain, etc

		*size = vgmcore_getlength( core ) * 4;
		*bps = 16; // MAYBE: support different depth/channels/sampling rate
		*nch = 2;
		*srate = 44100;

		return (intptr_t)core;
	}
	else
		return (intptr_t)NULL;
}

__declspec( dllexport ) intptr_t winampGetExtendedRead_getData(intptr_t handle, char *dest, int len, int *killswitch)
{
	return vgmcore_getsamples( (VGMCore *)handle, (short *)dest, len/4, killswitch ) * 4; // 4 bytes per sample (16-bit stereo)
}

__declspec( dllexport ) int winampGetExtendedRead_setTime(intptr_t handle, int millisecs)
{
	return vgmcore_seek( (VGMCore *)handle, millisecs );
}

__declspec( dllexport ) void winampGetExtendedRead_close(intptr_t handle)
{
	vgmcore_free( (VGMCore *)handle );
}
