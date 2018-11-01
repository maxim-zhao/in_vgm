//-----------------------------------------------------------------
// in_vgm
// VGM audio input plugin for Winamp
// http://www.smspower.org/music
// by Maxim <maxim\x40smspower\x2eorg> in 2001-2006
// with help from BlackAura in March and April 2002
// YM2612 PCM additions with Blargg in November 2005
//-----------------------------------------------------------------

#define BETA
#define VERSION "0.36"

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
#include "winampsdk/Winamp/in2.h"
#include "winampsdk/Winamp/wa_ipc.h"
#include "winampsdk/Winamp/ipc_pe.h"
#include "winampsdk/GlobalConfig.h"

// a few items removed from recent Winamp SDKs
#define WINAMP_BUTTON1                  40044
#define WINAMP_BUTTON2                  40045
#define WINAMP_BUTTON3                  40046
#define WINAMP_BUTTON4                  40047
#define WINAMP_BUTTON5                  40048

#include "PluginOptions.h"
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
#include "mame/ym2151.h"  // MAME YM2151 (old)

#include "gens/ym2612.h"  // Gens YM2612

#include "mame_ym2612_emu/Ym2612_Emu.h" // MAME YM2612

#define ROUND(x) ((int)(x>0?x+0.5:x-0.5))

HANDLE PluginhInst;

// raw configuration
#define NCH 2            // Number of channels
#define SAMPLERATE 44100 // Sampling rate
#define FADEOUT_MAX_VOLUME 100   // Number of steps for fadeout; can't have too many because of overflow

In_Module mod;             // the output module (declared near the bottom of this file)
char lastfn[MAX_PATH]="";  // currently playing file (used for getting info on the current file)
char *TempHTMLFile;        // holds a filename for the Unicode text file

#define SampleBufferSize (576*NCH*2)
short SampleBuffer[SampleBufferSize];  // sample buffer

gzFile *coreInputFile;

OPLL *coreYM2413;  // EMU2413 structure
SN76489_Context *coreSN76489; // SN76489 structure
ym2612_ *coreYM2612;
struct MAME_YM2612 *mame_ym2612;

// BlackAura - FMChip flags
#define CHIPS_USED_YM2413  0x01  // Bit 0 = YM2413
#define CHIPS_USED_YM2612  0x02  // Bit 1 = YM2612
#define CHIPS_USED_YM2151  0x04  // Bit 2 = TM2151

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

enum
{
	YM2612_GENS,
	YM2612_MAME
};

PluginOptions pluginOptions;


// evil global variables
int
	paused,             // are we paused?
	TrackLengthInms,    // Current track length in ms
	FileRate,           // in Hz
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
	TagsPreferJapanese,
	TagsAddFMToYM2413,
	TagsTrim,
	TagsStandardiseSeparators,
	TagsStandardiseDates,
	TagsGuessTrackNumbers,
	TagsGuessAlbumArtists,
	TagsFileType,
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
//	enableReplayGainHack,
	YM2612engine,
	YM2612engine_changed;
long int 
	coreMuting_YM2413=YM2413_MUTE_ALLON,  // backup when stopped. PSG does it itself.
	coreMuting_YM2612=YM2612_MUTE_ALLON,
	coreMuting_YM2151=YM2151_MUTE_ALLON;
char
	TrackTitleFormat[100],    // Track title formatting
	INIFileName[MAX_PATH+1];
const char
	*FilenameForInfoDlg;      // Filename passed to file info dialogue
char
	*vgm7z_extract_dir,
	*CurrentURL,            // Current URL being played
	*CurrentURLFilename;    // Filename current URL has been saved to
float
	SN_preamp,
	YM2413_preamp,
	YM2612_preamp,
	YM2151_preamp;

float ReplayGain;
float ReplayPeak;
int ReplayNoClip;

// Blargg: PCM data for current file (loaded in play thread)
static unsigned char* corePCM_buffer = NULL;
static unsigned long corePCM_bufferSize;
static unsigned long corePCM_bufferPos;

VGMCore* core;





BOOL WINAPI DllMain(HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved) {
	PluginhInst=hInst;
	return TRUE;
}








// Helpers
void UpdateIfPlaying() {
	if (ImmediateUpdate
		&&(mod.outMod)
		&&(mod.outMod->IsPlaying())	)
		setoutputtime(getoutputtime());
}

//-----------------------------------------------------------------
// Configuration dialogue
//-----------------------------------------------------------------
#define NumCfgTabChildWnds 8
HWND CfgTabChildWnds[NumCfgTabChildWnds];  // Holds child windows' HWnds
// Defines to make it easier to place stuff where I want
#define CfgPlayback CfgTabChildWnds[0]
#define CfgTags     CfgTabChildWnds[1]
#define Cfg7z       CfgTabChildWnds[2]
#define CfgLegacy   CfgTabChildWnds[3]
#define CfgPSG      CfgTabChildWnds[4]
#define Cfg2413     CfgTabChildWnds[5]
#define Cfg2612     CfgTabChildWnds[6]
#define Cfg2151     CfgTabChildWnds[7]
// Dialogue box tabsheet handler
BOOL CALLBACK ConfigDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam);

void AddTab(HWND tabCtrlWnd, int imgIndex, char* title)
{
	TC_ITEM newTab;
	int tabIndex = TabCtrl_GetItemCount(tabCtrlWnd);
	newTab.mask = TCIF_TEXT;
	if (imgIndex >= 0)
		newTab.mask |= TCIF_IMAGE;
	newTab.pszText=title;
	newTab.iImage=imgIndex;
	TabCtrl_InsertItem(tabCtrlWnd,tabIndex,&newTab);
}

void InitConfigDialog(HWND hWndMain)
{
	// Variables
	TC_ITEM NewTab;
	HWND TabCtrlWnd = GetDlgItem(hWndMain,tcMain);
	RECT TabDisplayRect,TabRect;
	HIMAGELIST il;
	int i;

	// Load images for tabs
	InitCommonControls(); // required before doing imagelist stuff
	il = ImageList_LoadImage(
		PluginhInst,          // HInst
		(LPCSTR)tabicons,     // Resource ID
		16,                   // width of images
		0,                    // Amount imagelist can grow dynamically
		RGB(255,0,255),       // transparent colour
		IMAGE_BITMAP,         // Bitmap or icon?
		LR_CREATEDIBSECTION   // Don't map to display colours (ie. 16-colour palette)
	);
	TabCtrl_SetImageList(TabCtrlWnd,il);
	// Add tabs
	NewTab.mask=TCIF_TEXT | TCIF_IMAGE;

	AddTab(TabCtrlWnd, 0, "Playback");
	AddTab(TabCtrlWnd, 1, "Tags");
	AddTab(TabCtrlWnd, 2, "VGM7z");
	AddTab(TabCtrlWnd, 3, "Legacy");
	AddTab(TabCtrlWnd, 4, "SN76489");
	AddTab(TabCtrlWnd, 5, "YM2413");
	AddTab(TabCtrlWnd, 6, "YM2612");
	AddTab(TabCtrlWnd, 7, "YM2151");

	// Get display rect
	GetWindowRect(TabCtrlWnd,&TabDisplayRect);
	GetWindowRect(hWndMain,&TabRect);
	OffsetRect(&TabDisplayRect,-TabRect.left-GetSystemMetrics(SM_CXDLGFRAME),-TabRect.top-GetSystemMetrics(SM_CYDLGFRAME)-GetSystemMetrics(SM_CYCAPTION));
	TabCtrl_AdjustRect(TabCtrlWnd,FALSE,&TabDisplayRect);
	
	// Create child windows
	CfgPlayback = CreateDialog(PluginhInst, (LPCTSTR)DlgCfgPlayback, hWndMain, ConfigDialogProc);
	CfgTags     = CreateDialog(PluginhInst, (LPCTSTR)DlgCfgTags,     hWndMain, ConfigDialogProc);
	CfgPSG      = CreateDialog(PluginhInst, (LPCTSTR)DlgCfgPSG,      hWndMain, ConfigDialogProc);
	Cfg2413     = CreateDialog(PluginhInst, (LPCTSTR)DlgCfgYM2413,   hWndMain, ConfigDialogProc);
	Cfg2612     = CreateDialog(PluginhInst, (LPCTSTR)DlgCfgYM2612,   hWndMain, ConfigDialogProc);
	Cfg2151     = CreateDialog(PluginhInst, (LPCTSTR)DlgCfgYM2151,   hWndMain, ConfigDialogProc);
	Cfg7z       = CreateDialog(PluginhInst, (LPCTSTR)DlgCfgVgm7z,    hWndMain, ConfigDialogProc);
	CfgLegacy   = CreateDialog(PluginhInst, (LPCTSTR)DlgCfgLegacy,   hWndMain, ConfigDialogProc);

	// Enable WinXP styles 
	{
		HINSTANCE dllinst = LoadLibrary("uxtheme.dll");
		if (dllinst)
		{
			FARPROC EnableThemeDialogTexture    = GetProcAddress(dllinst, "EnableThemeDialogTexture");
			FARPROC IsThemeDialogTextureEnabled = GetProcAddress(dllinst, "IsThemeDialogTextureEnabled");
			if ((IsThemeDialogTextureEnabled)
				&& (EnableThemeDialogTexture) // All functions found
				&& IsThemeDialogTextureEnabled(hWndMain)) // and app is themed
			{ 
				// then draw pages with theme texture
				// Windows header up-to-dating:
#ifndef ETDT_ENABLETAB
#define ETDT_ENABLETAB 6
#endif
				for (i = 0; i < NumCfgTabChildWnds; ++i)
				{
					EnableThemeDialogTexture(CfgTabChildWnds[i], ETDT_ENABLETAB);
				}
			}
			FreeLibrary(dllinst);
		}
	}

	// Put them in the right place, and hide all but the first
	for (i = 0; i < NumCfgTabChildWnds; ++i)
	{
		SetWindowPos(
			CfgTabChildWnds[i],
			HWND_TOP,
			TabDisplayRect.left,
			TabDisplayRect.top,
			TabDisplayRect.right - TabDisplayRect.left,
			TabDisplayRect.bottom - TabDisplayRect.top,
			(i == 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW)
		);
	}
}

int LoadConfigDialogInfo( HWND DlgWin ) 
{
	int i; // counter
	// Playback tab -----------------------------------------------------------
#define WND CfgPlayback
	// Set loop count
	SetDlgItemInt( WND, ebLoopCount, NumLoops, FALSE );
	CheckDlgButton( WND, cbLoopForever, pluginOptions.LoopForever );
	SETCONTROLENABLED( WND, ebLoopCount, !pluginOptions.LoopForever );
	// Set fadeout length
	SetDlgItemInt( WND, ebFadeOutLength, LoopingFadeOutms, FALSE );
	// Set between-track pause length
	SetDlgItemInt( WND, ebPauseLength, PauseBetweenTracksms, FALSE);
	// Playback rate
	switch (pluginOptions.PlaybackRate)
	{
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
	if (pluginOptions.PlaybackRate != 0)
	{
		SetDlgItemInt( WND, ebPlaybackRate, pluginOptions.PlaybackRate, FALSE );
	}
	else
	{
		SetDlgItemInt( WND, ebPlaybackRate, 60, FALSE );
	}
	// Volume overdrive
	CheckDlgButton( WND, cbOverDrive, Overdrive );
	// Persistent muting checkbox
	CheckDlgButton( WND, cbMutePersistent, MutePersistent );
	// randomise panning checkbox
	CheckDlgButton( WND, cbRandomisePanning, RandomisePanning );
	// Replay Gain transcoding hack checkbox
	CheckDlgButton( WND, cbEnableReplayGainHack, pluginOptions.enableReplayGainHack);

	// Filter
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

	// Tags tab ----------------------------------------------------------------
#define WND CfgTags
	CheckDlgButton(WND,cbMLJapanese,TagsPreferJapanese);
	// File type
	SetDlgItemInt(WND,ebMLType,TagsFileType,FALSE);
	// Scan other files to guess
	CheckDlgButton(WND,cbTagsGuessTrackNumbers,TagsGuessTrackNumbers);
	CheckDlgButton(WND,cbTagsGuessAlbumArtist,TagsGuessAlbumArtists);
	// Tag tweaks
	CheckDlgButton(WND,cbTagsTrimWhitespace,TagsTrim);
	CheckDlgButton(WND,cbTagsStandardiseSeparators,TagsStandardiseSeparators);
	CheckDlgButton(WND,cbMLShowFM,TagsAddFMToYM2413);
	CheckDlgButton(WND,cbTagsStandardiseDates,TagsStandardiseDates);
#undef WND

	// PSG tab ----------------------------------------------------------------
#define WND CfgPSG
	if (SNClock)
	{
		// Check PSG channel checkboxes
		for ( i = 0; i < SN76489_NUM_CHANNELS; i++)
		{
			CheckDlgButton(
				WND,
				cbTone1 + i,
				(SN76489_Mute & (1<<i)) > 0
			);
		}
	} else {
		// or disable them
		for ( i = 0; i < SN76489_NUM_CHANNELS; i++ ) 
			DISABLECONTROL(WND,cbTone1+i);
		DISABLECONTROL(WND,btnRandomPSG);
		DISABLECONTROL(WND,btnCentrePSG);
		DISABLECONTROL(WND,slSNCh0);
		DISABLECONTROL(WND,slSNCh1);
		DISABLECONTROL(WND,slSNCh2);
		DISABLECONTROL(WND,slSNCh3);
	}
	// Panning
	SetupSlider(WND, slSNCh0, 0, 254, 127, SN76489_Pan[0]);
	SetupSlider(WND, slSNCh1, 0, 254, 127, SN76489_Pan[1]);
	SetupSlider(WND, slSNCh2, 0, 254, 127, SN76489_Pan[2]);
	SetupSlider(WND, slSNCh3, 0, 254, 127, SN76489_Pan[3]);
	// Chip enable
	CheckDlgButton(WND,cbSNEnable,(SN_enable!=0));
	SETCONTROLENABLED(WND,cbSNEnable,(SNClock!=0));
	// Preamp
	SetupSlider(WND,slSNPreamp,0,200,100,ROUND(SN_preamp*100));
#undef WND

	// YM2413 tab -------------------------------------------------------------
#define WND Cfg2413
	if USINGCHIP(CHIPS_USED_YM2413) {  // Check YM2413 FM channel checkboxes
		for (i=0;i<YM2413_NUM_CHANNELS;i++) CheckDlgButton(WND,cbYM24131+i,!((coreMuting_YM2413 & (1<<i))>0));
		CheckDlgButton(WND,cbYM2413ToneAll,((coreMuting_YM2413&0x1ff )==0));
		CheckDlgButton(WND,cbYM2413PercAll,((coreMuting_YM2413&0x3e00)==0));
	} else {
		for (i=0;i<YM2413_NUM_CHANNELS;i++) DISABLECONTROL(WND,cbYM24131+i);
		DISABLECONTROL(WND,cbYM2413ToneAll);
		DISABLECONTROL(WND,cbYM2413PercAll);
		DISABLECONTROL(WND,lblExtraTone);
		DISABLECONTROL(WND,lblExtraToneNote);
		DISABLECONTROL(WND,gb2413Emulation);
		DISABLECONTROL(WND,cbYM2413HiQ);
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
	SETCONTROLENABLED(WND,cbYM2413Enable,USINGCHIP(CHIPS_USED_YM2413));
	// Preamp
	SetupSlider(WND,slYM2413Preamp,0,200,100,ROUND(YM2413_preamp*100));
	// HQ option
	CheckDlgButton(WND,cbYM2413HiQ,YM2413HiQ);
#undef WND

	// YM2612 tab -------------------------------------------------------
#define WND Cfg2612
	// YM2612 enable
	CheckDlgButton(WND,cbYM2612Enable,YM2612_enable);
	SETCONTROLENABLED(WND,cbYM2612Enable,USINGCHIP(CHIPS_USED_YM2612));
	// Preamp
	SetupSlider(WND,slYM2612Preamp,0,200,100,ROUND(YM2612_preamp*100));
	// Muting checkboxes
	if USINGCHIP(CHIPS_USED_YM2612) {  // Check YM2161 FM channel checkboxes
		for (i=0;i<YM2612_NUM_CHANNELS;i++) CheckDlgButton(WND,cb2612Tone1+i,!((coreMuting_YM2612 & (1<<i))>0));
		CheckDlgButton(WND,cb2612All,((coreMuting_YM2612&0x3f )==0));
	} else {
		for (i=0;i<YM2612_NUM_CHANNELS;i++) DISABLECONTROL(WND,cb2612Tone1+i);
		DISABLECONTROL(WND,cb2612All);
		DISABLECONTROL(WND,gbYM2612);
	}
	// YM2612 engine
	switch(YM2612engine)
	{
	case YM2612_GENS: CheckDlgButton(WND, rbYM2612Gens, TRUE); break;
	case YM2612_MAME: CheckDlgButton(WND, rbYM2612MAME, TRUE); break;
	}

#undef WND

	// YM2151 tab -------------------------------------------------------
#define WND Cfg2151
	// YM2151 enable
	CheckDlgButton(WND,cbYM2151Enable,YM2151_enable);
	SETCONTROLENABLED(WND,cbYM2151Enable,USINGCHIP(CHIPS_USED_YM2151));
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

	// Legacy tab -------------------------------------------------------
#define WND CfgLegacy
	// Set title format text
	SetDlgItemText(WND,ebTrackTitle,TrackTitleFormat);
	// Now Playing settings
	CheckDlgButton(WND,cbUseMB         ,UseMB);
	CheckDlgButton(WND,cbAutoMB        ,AutoMB);
	CheckDlgButton(WND,cbForceMBOpen   ,ForceMBOpen);
	SETCONTROLENABLED(WND,cbAutoMB     ,UseMB);
	SETCONTROLENABLED(WND,cbForceMBOpen,UseMB & AutoMB);
#undef WND

	// Stuff not in tabs ------------------------------------------------------
	// Immediate update checkbox
	CheckDlgButton(DlgWin,cbMuteImmediate,ImmediateUpdate);
	return i;
}
// Dialogue box callback function
BOOL CALLBACK ConfigDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam) {
	switch (wMessage) {  // process messages
		case WM_INITDIALOG: { // Initialise dialogue
			// do nothing if this is a child window (tab page) callback, pass to the parent
			if ( GetWindowLong(DlgWin,GWL_STYLE) & WS_CHILD )
				return FALSE;

			InitConfigDialog(DlgWin);

			SetWindowText(DlgWin,PLUGINNAME " configuration");

			LoadConfigDialogInfo(DlgWin);

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
					pluginOptions.LoopForever = IsDlgButtonChecked(CfgPlayback,cbLoopForever);
					// Fadeout length
					i=GetDlgItemInt(CfgPlayback,ebFadeOutLength,&b,FALSE);
					if (b) LoopingFadeOutms=i;
					// Between-track pause length
					i=GetDlgItemInt(CfgPlayback,ebPauseLength,&b,FALSE);
					if (b) PauseBetweenTracksms=i;
					// Playback rate
					pluginOptions.PlaybackRate=0;
					if (IsDlgButtonChecked(CfgPlayback,rbRate50)) {
						pluginOptions.PlaybackRate=50;
					} else if (IsDlgButtonChecked(CfgPlayback,rbRate60)) {
						pluginOptions.PlaybackRate=60;
					} else if (IsDlgButtonChecked(CfgPlayback,rbRateOther)) {
						i=GetDlgItemInt(CfgPlayback,ebPlaybackRate,&b,TRUE);
						if ((b) && (i>0) && (i<=6000)) pluginOptions.PlaybackRate=i;
					}
					// Persistent muting checkbox
					MutePersistent=IsDlgButtonChecked(CfgPlayback,cbMutePersistent);
					// Randomise panning checkbox
					RandomisePanning=IsDlgButtonChecked(CfgPlayback,cbRandomisePanning);
					// Replay Gain transcoding hack checkbox
					pluginOptions.enableReplayGainHack = IsDlgButtonChecked(CfgPlayback, cbEnableReplayGainHack);

					// Tags tab ----------------------------------------------------------
					TagsPreferJapanese=IsDlgButtonChecked(CfgTags,cbMLJapanese);
					// File type
					i=GetDlgItemInt(CfgTags,ebMLType,&b,FALSE);
					if (b) TagsFileType=i;
					// Scan other files to guess
					TagsGuessTrackNumbers=IsDlgButtonChecked(CfgTags,cbTagsGuessTrackNumbers);
					TagsGuessAlbumArtists=IsDlgButtonChecked(CfgTags,cbTagsGuessAlbumArtist);
					// Tag tweaks
					TagsTrim=IsDlgButtonChecked(CfgTags,cbTagsTrimWhitespace);
					TagsStandardiseSeparators=IsDlgButtonChecked(CfgTags,cbTagsStandardiseSeparators);
					TagsAddFMToYM2413=IsDlgButtonChecked(CfgTags,cbMLShowFM);
					TagsStandardiseDates=IsDlgButtonChecked(CfgTags,cbTagsStandardiseDates);

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

					// Legacy tab ---------------------------------------------------------
					// Track title format
					GetDlgItemText(CfgLegacy,ebTrackTitle,TrackTitleFormat,100);
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
				SN76489_SetMute(coreSN76489,SN76489_Mute);
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
				coreMuting_YM2413=0;
				for (i=0;i<YM2413_NUM_CHANNELS;i++) coreMuting_YM2413|=(!IsDlgButtonChecked(Cfg2413,cbYM24131+i))<<i;
				if USINGCHIP(CHIPS_USED_YM2413) {
					OPLL_setMask(coreYM2413,coreMuting_YM2413);
					UpdateIfPlaying();
				}
				CheckDlgButton(Cfg2413,cbYM2413ToneAll,((coreMuting_YM2413&0x1ff )==0));
				CheckDlgButton(Cfg2413,cbYM2413PercAll,((coreMuting_YM2413&0x3e00)==0));
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
				if USINGCHIP(CHIPS_USED_YM2413) {
					OPLL_set_quality(coreYM2413,YM2413HiQ);
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
			// no immediacy required

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
				coreMuting_YM2612=0;
				for (i=0;i<YM2612_NUM_CHANNELS;i++) coreMuting_YM2612|=(!IsDlgButtonChecked(Cfg2612,cb2612Tone1+i))<<i;
				if USINGCHIP(CHIPS_USED_YM2612) {
					switch (YM2612engine)
					{
					case YM2612_GENS: GENS_YM2612_SetMute(coreYM2612, coreMuting_YM2612); break;
					case YM2612_MAME: MAME_YM2612Mute(mame_ym2612, coreMuting_YM2612); break;
					}
					UpdateIfPlaying();
				}
				CheckDlgButton(Cfg2612,cb2612All,((coreMuting_YM2612&0x3f)==0));
				break;
			}
			case rbYM2612Gens:
				YM2612engine_changed = YM2612_GENS;
				break;
			case rbYM2612MAME:
				YM2612engine_changed = YM2612_MAME;
				break;

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

			// Legacy tab -----------------------------------------------------------------
			case cbUseMB:
				UseMB=IsDlgButtonChecked(CfgLegacy,cbUseMB);
				SETCONTROLENABLED(CfgLegacy,cbAutoMB     ,UseMB);
				SETCONTROLENABLED(CfgLegacy,cbForceMBOpen,UseMB & AutoMB);
				break;
			case cbAutoMB:
				AutoMB=IsDlgButtonChecked(CfgLegacy,cbAutoMB);
				SETCONTROLENABLED(CfgLegacy,cbForceMBOpen,UseMB & AutoMB);
				break;
			case cbForceMBOpen:
				ForceMBOpen=IsDlgButtonChecked(CfgLegacy,cbForceMBOpen);
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
				SN76489_SetPanning(coreSN76489,SN76489_Pan[0],SN76489_Pan[1],SN76489_Pan[2],SN76489_Pan[3]);

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
			if (USINGCHIP(CHIPS_USED_YM2413))
				for ( i=0; i< YM2413_NUM_CHANNELS; ++i )
					OPLL_set_pan( coreYM2413, i, YM2413_Pan[i] );

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
		"maxim@" "smsp" "ower" "." "org\n" // spam avoidance in source code... :(
		"http://www.smspower.org/music\n\n"
		"Current status:\n"
		"PSG - emulated as a perfect device, leading to slight differences in sound compared to the real thing. Noise pattern is 100% accurate, unlike almost every other core out there :P\n"
		"YM2413 - via EMU2413 0.61 (Mitsutaka Okazaki) (http://www.angel.ne.jp/~okazaki/ym2413).\n"
		"YM2612 - via Gens 2.10 core (Stephane Dallongeville) (http://gens.consolemul.com) (or MAME).\n"
		"YM2151 - via MAME FM core (Jarek Burczynski, Hiro-shi), thanks to BlackAura.\n\n"
		"Don\'t be put off by the pre-1.0 version numbers. This is a non-commercial project and as such it is permanently in beta.\n\n"
		"Using:\n"
		"ZLib " ZLIB_VERSION " (http://www.zlib.org)\n"
		"LZMA SDK 4.40 (http://www.7-zip.org)\n"
		"Silk icons (http://www.famfamfam.com/lab/icons/silk/)\n"
		"\n"
		"Thanks also go to:\n"
		"Bock, Heliophobe, Mike G, Steve Snake, Dave, Charles MacDonald, Ville Helin, John Kortink, fx^, DukeNukem, Blargg, Christoph Bölitz\n\n"
		"  ...and Zhao Yuehua xxx wo ai ni"
		,mod.description,MB_ICONINFORMATION|MB_OK);
}

void getINIfilename()
{
	// see if we are in Winamp 5.11+ with user profiles
	char* dir = (char*)SendMessage(mod.hMainWindow,WM_WA_IPC,0,IPC_GETINIDIRECTORY);

	if (dir)
	{
		// Winamp told us where to store it, construct our path from it
		strcpy(INIFileName,dir);
		strcat(INIFileName,"\\plugins");
		// make sure folder exists
		CreateDirectory(INIFileName,NULL);
		strcat(INIFileName,"\\in_vgm.ini");

		// check if settings exist in this file, if not try migrating from the old file location
		if(GetPrivateProfileInt(INISECTION,"NumLoops",-1,INIFileName)==-1)
		{
			char oldINI[MAX_PATH];
			char* section;
			int sectionsize;

			// get DLL path
			GetModuleFileName(PluginhInst, oldINI, MAX_PATH);
			// change filename part to plugin.ini
			strcpy(strrchr(oldINI, '\\') + 1, "plugin.ini");

			// transfer INI section en masse
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
		// get DLL path
		GetModuleFileName(PluginhInst, INIFileName, MAX_PATH);
		// change filename part to plugin.ini
		strcpy(strrchr(INIFileName, '\\') + 1, "plugin.ini");
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
	pluginOptions.LoopForever         =GetPrivateProfileInt(INISECTION,"Loop forever"         ,0    ,INIFileName);
	LoopingFadeOutms    =GetPrivateProfileInt(INISECTION,"Fade out length"      ,5000 ,INIFileName);
	PauseBetweenTracksms=GetPrivateProfileInt(INISECTION,"Pause between tracks" ,1000 ,INIFileName);
	pluginOptions.PlaybackRate        =GetPrivateProfileInt(INISECTION,"Playback rate"        ,0    ,INIFileName);
	FileInfoJapanese    =GetPrivateProfileInt(INISECTION,"Japanese in info box" ,0    ,INIFileName);
	UseMB               =GetPrivateProfileInt(INISECTION,"Use Minibrowser"      ,1    ,INIFileName);
	AutoMB              =GetPrivateProfileInt(INISECTION,"Auto-show HTML"       ,0    ,INIFileName);
	ForceMBOpen         =GetPrivateProfileInt(INISECTION,"Force MB open"        ,0    ,INIFileName);
	YM2413HiQ           =GetPrivateProfileInt(INISECTION,"High quality YM2413"  ,0    ,INIFileName);
	Overdrive           =GetPrivateProfileInt(INISECTION,"Overdrive"            ,0    ,INIFileName);
	ImmediateUpdate     =GetPrivateProfileInt(INISECTION,"Immediate update"     ,1    ,INIFileName);
	TagsPreferJapanese  =GetPrivateProfileInt(INISECTION,"ML Japanese"          ,0    ,INIFileName);
	TagsAddFMToYM2413   =GetPrivateProfileInt(INISECTION,"ML show FM"           ,1    ,INIFileName);
	TagsFileType        =GetPrivateProfileInt(INISECTION,"ML type"              ,0    ,INIFileName);
	TagsGuessTrackNumbers=GetPrivateProfileInt(INISECTION,"Guess track numbers" ,1    ,INIFileName);
	TagsGuessAlbumArtists=GetPrivateProfileInt(INISECTION,"Guess album artists" ,1    ,INIFileName);
	TagsStandardiseSeparators=GetPrivateProfileInt(INISECTION,"Fix separators"  ,1    ,INIFileName);
	TagsStandardiseDates=GetPrivateProfileInt(INISECTION,"Fix dates"            ,1    ,INIFileName);
	TagsTrim            =GetPrivateProfileInt(INISECTION,"Trim whitespace"      ,1    ,INIFileName);
	filter_type         =GetPrivateProfileInt(INISECTION,"Audio filter type"    ,FILTER_NONE,INIFileName);

	vgm_compression_level=GetPrivateProfileInt(INISECTION,"VGM compression"      ,9    ,INIFileName);
	vgm7z_enable        =GetPrivateProfileInt(INISECTION,"Enable VGM7z support" ,1    ,INIFileName);
	vgm7z_extract_same  =GetPrivateProfileInt(INISECTION,"VGM7z to same folder" ,1    ,INIFileName);
	vgm7z_extract_subdir=GetPrivateProfileInt(INISECTION,"VGM7z to subfolder"   ,1    ,INIFileName);
	vgm7z_extract_prompt=GetPrivateProfileInt(INISECTION,"VGM7z extract prompt" ,1    ,INIFileName);
	vgm7z_delete_after_extract=GetPrivateProfileInt(INISECTION,"Delete VGM7z"   ,1    ,INIFileName);
	GetPrivateProfileString(INISECTION,"VGM7z to folder"   ,"c:\\",buffer,1023,INIFileName);
	vgm7z_extract_dir   =strdup(buffer);
	pluginOptions.enableReplayGainHack=GetPrivateProfileInt(INISECTION,"Enable kill Replay Gain",0    ,INIFileName);

	YM2612engine = YM2612engine_changed = GetPrivateProfileInt(INISECTION,"YM2612 engine",YM2612_GENS,INIFileName);

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

	corePCM_buffer = NULL;
}

//-----------------------------------------------------------------
// Deinitialisation (one-time)
//-----------------------------------------------------------------
void quit() {
	char tempstr[1024];  // buffer for itoa

	WritePrivateProfileString(INISECTION,"NumLoops"            ,itoa(NumLoops             ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Loop forever"        ,itoa(pluginOptions.LoopForever          ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Fade out length"     ,itoa(LoopingFadeOutms     ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Pause between tracks",itoa(PauseBetweenTracksms ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Playback rate"       ,itoa(pluginOptions.PlaybackRate         ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Japanese in info box",itoa(FileInfoJapanese     ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Title format"        ,TrackTitleFormat                      ,INIFileName);
	WritePrivateProfileString(INISECTION,"Use Minibrowser"     ,itoa(UseMB                ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Auto-show HTML"      ,itoa(AutoMB               ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Force MB open"       ,itoa(ForceMBOpen          ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"High quality YM2413" ,itoa(YM2413HiQ            ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Overdrive"           ,itoa(Overdrive            ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Immediate update"    ,itoa(ImmediateUpdate      ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"ML Japanese"         ,itoa(TagsPreferJapanese   ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"ML show FM"          ,itoa(TagsAddFMToYM2413    ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"ML type"             ,itoa(TagsFileType         ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Guess track numbers" ,itoa(TagsGuessTrackNumbers,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Guess album artists" ,itoa(TagsGuessAlbumArtists,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Fix separators"      ,itoa(TagsStandardiseSeparators,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Fix dates"           ,itoa(TagsStandardiseDates ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Trim whitespace"     ,itoa(TagsTrim             ,tempstr,10),INIFileName);

	WritePrivateProfileString(INISECTION,"Audio filter type"   ,itoa(filter_type          ,tempstr,10),INIFileName);

	WritePrivateProfileString(INISECTION,"VGM compression"      ,itoa(vgm_compression_level,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Enable VGM7z support" ,itoa(vgm7z_enable        ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"VGM7z to same folder" ,itoa(vgm7z_extract_same  ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"VGM7z to subfolder"   ,itoa(vgm7z_extract_subdir,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"VGM7z extract prompt" ,itoa(vgm7z_extract_prompt,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Delete VGM7z"         ,itoa(vgm7z_delete_after_extract,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"VGM7z to folder"      ,vgm7z_extract_dir                    ,INIFileName);
	free(vgm7z_extract_dir);
	WritePrivateProfileString(INISECTION,"Enable kill Replay Gain",itoa(pluginOptions.enableReplayGainHack,tempstr,10),INIFileName);

	WritePrivateProfileString(INISECTION,"YM2612 engine"        ,itoa(YM2612engine_changed,tempstr,10),INIFileName);
	
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
		CurrentURLFilename = NULL;
	}

	if ( CurrentURL )
	{
		free( CurrentURL );
		CurrentURL = NULL;
	}
}

//-----------------------------------------------------------------
// Pre-extension check file claiming
//-----------------------------------------------------------------
int isourfile(const char *fn) {
	// First, check for URLs
	gzFile *f;
	char *p=strrchr(fn,'.');
	if ( (p) && ( (stricmp(p,".vgm")==0) || (stricmp(p,".vgz")==0) ) && IsURL(fn) ) return TRUE;
	if(strlen(fn)==0)
		return FALSE;

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


int un7zip_and_gzip(const char *filename, const char *folder, int compressionLevel, char **playlistFilename);
void un7zip_free_pl_fn( char *playlistFilename );


//-----------------------------------------------------------------
// Play filename
//-----------------------------------------------------------------
int play(const char *fn) { 
	int maxlatency;
	int thread_id;
	int file_size;
	TVGMHeader VGMHeader;
	int i;
	char *p;

	coreInputFile=NULL;

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
			coreInputFile = NULL;
			return -1; // return "file not found" to skip on to the next track
		}
	} // end VGM7z handler

	if (CurrentURL)
	{
		free(CurrentURL);
		CurrentURL = NULL;
	}

	if (CurrentURLFilename)
	{
		free(CurrentURLFilename);
		CurrentURLFilename = NULL;
	}

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
		coreMuting_YM2413=YM2413_MUTE_ALLON;
		coreMuting_YM2612=YM2612_MUTE_ALLON;
		coreMuting_YM2151=YM2151_MUTE_ALLON;
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
	if (corePCM_buffer) free( corePCM_buffer );
	corePCM_buffer = NULL;
	corePCM_bufferSize = 0;
	corePCM_bufferPos = 0;

	coreInputFile=gzopen(fn,"rb");  // Open file - read, binary

	if (coreInputFile==NULL) {
		return -1;
	}  // File not opened/found, advance playlist

	// Get file size for bitrate calculations 
	file_size=FileSize(fn);

	// Read header
	i=gzread(coreInputFile,&VGMHeader,sizeof(VGMHeader));

	// Check it read OK
	if (i<sizeof(VGMHeader)) {
		char msgstring[1024];
		sprintf(msgstring,"File too short:\n%s",fn);
		MessageBox(mod.hMainWindow,msgstring,mod.description,0);
		gzclose(coreInputFile);
		coreInputFile=NULL;
		return -1;
	}

	// Check for VGM marker
	if (VGMHeader.VGMIdent != VGMIDENT) {
		char msgstring[1024];
		sprintf(msgstring,"VGM marker not found in \"%s\"",fn);
		MessageBox(mod.hMainWindow,msgstring,mod.description,0);
		gzclose(coreInputFile);
		coreInputFile=NULL;
		return -1;
	}

	// Check version
	if ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER)) {
		char msgstring[1024];
		sprintf(msgstring,"Unsupported VGM version found in \"%s\" (%x).\n\nDo you want to try to play it anyway?",fn,VGMHeader.Version);

		if (MessageBox(mod.hMainWindow,msgstring,mod.description,MB_YESNO+MB_DEFBUTTON2)==IDNO) {
			gzclose(coreInputFile);
			coreInputFile=NULL;
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

	// Take last selected YM2612 engine
	YM2612engine = YM2612engine_changed;

	// BlackAura - Disable all FM chips
	FMChips=0;

	// Get rate
	FileRate=VGMHeader.RecordingRate;

	// get Replay Gain stuff
	getReplayGainData(fn, &ReplayGain, &ReplayPeak, &ReplayNoClip);

	// Open output plugin
	maxlatency = mod.outMod->Open(SAMPLERATE,NCH,16, -1,-1);
	if (maxlatency < 0) {  // error opening device
		gzclose(coreInputFile);
		coreInputFile=NULL;
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
	mod.VSASetInfo(SAMPLERATE,NCH);

	mod.outMod->SetVolume(-666); // set the output plug-ins default volume

	gzseek(coreInputFile,VGMDataOffset,SEEK_SET);

	// FM Chip startups are done whenever a chip is used for the first time

	// Start up SN76489 (if used)
	if (SNClock) {
		coreSN76489 = SN76489_Init(SNClock,SAMPLERATE);
		if(coreSN76489)
		{
			SN76489_Config(coreSN76489,SN76489_Mute,VGMHeader.PSGWhiteNoiseFeedback, VGMHeader.PSGShiftRegisterWidth, ((int)(VGMHeader.YM2612Clock/1000000)==7?0:1) ); // nasty hack: boost noise except for YM2612 music
			SN76489_SetPanning(coreSN76489,SN76489_Pan[0],SN76489_Pan[1],SN76489_Pan[2],SN76489_Pan[3]);
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

// #ifdef _DEBUG
// 	SetThreadPriority(thread_handle,THREAD_PRIORITY_LOWEST); // go low on debug to keep the debugger responsive on lockup - can cause breaks in the audio
// #else
	SetThreadPriority(thread_handle,GetPlaybackThreadPriority());
//#endif
	
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
	if (coreInputFile!=NULL) {
		gzclose(coreInputFile);  // Close input file
		coreInputFile=NULL;
	}

	mod.outMod->Close();  // close output plugin

	mod.SAVSADeInit();  // Deinit vis

	// Stop YM2413
	if USINGCHIP(CHIPS_USED_YM2413) 
		OPLL_delete(coreYM2413);

	// Stop YM2612
	if USINGCHIP(CHIPS_USED_YM2612)
	{
		switch (YM2612engine)
		{
		case YM2612_GENS: GENS_YM2612_End(coreYM2612); break;
		case YM2612_MAME: MAME_YM2612Shutdown(mame_ym2612); break;
		}
	}

	// Stop YM2151
	if USINGCHIP(CHIPS_USED_YM2151)
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
int getlength() 
{
	int l = (int)((TrackLengthInms+NumLoops*LoopLengthInms)*((pluginOptions.PlaybackRate&&FileRate)?(double)FileRate/pluginOptions.PlaybackRate:1));
	if (!pluginOptions.LoopForever && l>mod.outMod->GetOutputTime())
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
	TVGMHeader  VGMHeader;
	TGD3Header  GD3Header;
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
	if (FileToUse==NULL || *FileToUse==0 || (fh=gzopen(FileToUse,"rb"))==0)
	{
		if (title) sprintf(title,"Unable to open \"%s\"",FileToUse);
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
		if ( pluginOptions.LoopForever )
			TrackLength = -1000;
		else
			TrackLength=(long) (
				(VGMHeader.TotalLength+NumLoops*VGMHeader.LoopLength)
				/44.1
				*((pluginOptions.PlaybackRate&&VGMHeader.RecordingRate)?(double)VGMHeader.RecordingRate/pluginOptions.PlaybackRate:1)
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

#define ReadByte() gzgetc(coreInputFile)

DWORD WINAPI __stdcall DecodeThread(void *outputBuffer) 
{
	int SamplesTillNextRead=0;
	float WaitFactor,FractionalSamplesTillNextRead=0;

	if ((pluginOptions.PlaybackRate==0) || (FileRate==0)) 
	{
		WaitFactor=1.0;
	} else 
	{
		WaitFactor=(float)FileRate/pluginOptions.PlaybackRate;
	}

	while (! *((int *)outputBuffer) ) {
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
					if (coreInputFile==NULL) break;

					if (getlength()<0) break; // disable seeking on fadeout/silence

					if USINGCHIP(CHIPS_USED_YM2413) {  // If using YM2413, reset it
						int i;
						long int YM2413Channels = OPLL_toggleMask( coreYM2413, 0 );
						OPLL_reset( coreYM2413 );
						OPLL_setMask( coreYM2413, YM2413Channels );
						for ( i = 0; i < YM2413_NUM_CHANNELS; ++i )
							OPLL_set_pan( coreYM2413, i, YM2413_Pan[i] );
					}

					if USINGCHIP( CHIPS_USED_YM2612 ) {
						switch (YM2612engine)
						{
						case YM2612_GENS: GENS_YM2612_Reset(coreYM2612); break;
						case YM2612_MAME: MAME_YM2612ResetChip(mame_ym2612); break;
						}
					}

					if USINGCHIP( CHIPS_USED_YM2151 ) {
						YM2151ResetChip( 0 );
					}

					mod.outMod->Flush( SeekToTimeInms );

					gzseek( coreInputFile, VGMDataOffset, SEEK_SET );
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
						if (SNClock) SN76489_GGStereoWrite(coreSN76489,(char)b1);
						break;
					case VGM_PSG:  // SN76489 write
						b1=ReadByte();
						if (SNClock) SN76489_Write(coreSN76489,(char)b1);
						break;
					case VGM_YM2413:  // YM2413 write
						b1=ReadByte();
						b2=ReadByte();
						if (YM2413Clock) {
							if (!USINGCHIP(CHIPS_USED_YM2413)) {  // BlackAura - If YM2413 emu not started, start it
								int i;
								// Start the emulator up
								coreYM2413=OPLL_new(YM2413Clock,SAMPLERATE);
								OPLL_reset(coreYM2413);
								OPLL_reset_patch(coreYM2413,0);
								OPLL_setMask(coreYM2413,coreMuting_YM2413);
								for ( i = 0; i< YM2413_NUM_CHANNELS; ++i )
									OPLL_set_pan( coreYM2413, i, YM2413_Pan[i] );
								OPLL_set_quality(coreYM2413,YM2413HiQ);
								// Set the flag for it
								FMChips|=CHIPS_USED_YM2413;
							}
							OPLL_writeReg(coreYM2413,b1,b2);  // Write to the chip
						}
						break;
					case VGM_YM2612_0:  // YM2612 write (port 0)
						b1=ReadByte();
						b2=ReadByte();
						if (YM2612Clock) {
							switch (YM2612engine)
							{
							case YM2612_GENS: 
								if (!USINGCHIP(CHIPS_USED_YM2612))
								{
									coreYM2612 = GENS_YM2612_Init(YM2612Clock,SAMPLERATE,0);
									GENS_YM2612_SetMute(coreYM2612, coreMuting_YM2612);
								}
								GENS_YM2612_Write(coreYM2612, 0,b1);
								GENS_YM2612_Write(coreYM2612, 1,b2);
								break;
							case YM2612_MAME: 
								if (!USINGCHIP(CHIPS_USED_YM2612))
								{
									mame_ym2612 = MAME_YM2612Init(YM2612Clock,SAMPLERATE);
									MAME_YM2612Mute(mame_ym2612, coreMuting_YM2612); 
								}
								MAME_YM2612Write(mame_ym2612, 0, b1);
								MAME_YM2612Write(mame_ym2612, 1, b2);
								break;
							}
							FMChips|=CHIPS_USED_YM2612;
						}
						break;
					case VGM_YM2612_1:  // YM2612 write (port 1)
						b1=ReadByte();
						b2=ReadByte();
						if (YM2612Clock) {
							switch (YM2612engine)
							{
							case YM2612_GENS: 
								if (!USINGCHIP(CHIPS_USED_YM2612))
								{
									coreYM2612 = GENS_YM2612_Init(YM2612Clock,SAMPLERATE,0);
									GENS_YM2612_SetMute(coreYM2612, coreMuting_YM2612);
								}
								GENS_YM2612_Write(coreYM2612, 2,b1);
								GENS_YM2612_Write(coreYM2612, 3,b2);
								break;
							case YM2612_MAME: 
								if (!USINGCHIP(CHIPS_USED_YM2612))
								{
									mame_ym2612 = MAME_YM2612Init(YM2612Clock,SAMPLERATE);
									MAME_YM2612Mute(mame_ym2612, coreMuting_YM2612); 
								}
								MAME_YM2612Write(mame_ym2612, 2, b1);
								MAME_YM2612Write(mame_ym2612, 3, b2);
								break;
							}
							FMChips|=CHIPS_USED_YM2612;
						}
						break;
					case VGM_YM2151:  // BlackAura - YM2151 write
						b1=ReadByte();
						b2=ReadByte();
						if (YM2151Clock) {
							if (!USINGCHIP(CHIPS_USED_YM2151)) {
								YM2151Init(1,YM2151Clock,SAMPLERATE);
								FMChips|=CHIPS_USED_YM2151;
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
						if (YM2612Clock && corePCM_buffer) {
							b1 = 0x2A;
							b2 = corePCM_buffer [corePCM_bufferPos++];
							assert( corePCM_bufferPos <= corePCM_bufferSize );

							if (YM2612Clock) {
								switch (YM2612engine)
								{
								case YM2612_GENS: 
									if (!USINGCHIP(CHIPS_USED_YM2612))
									{
										coreYM2612 = GENS_YM2612_Init(YM2612Clock,SAMPLERATE,0);
										GENS_YM2612_SetMute(coreYM2612, coreMuting_YM2612);
									}
									GENS_YM2612_Write(coreYM2612, 0,b1);
									GENS_YM2612_Write(coreYM2612, 1,b2);
									break;
								case YM2612_MAME: 
									if (!USINGCHIP(CHIPS_USED_YM2612))
									{
										mame_ym2612 = MAME_YM2612Init(YM2612Clock,SAMPLERATE);
										MAME_YM2612Mute(mame_ym2612, coreMuting_YM2612); 
									}
									MAME_YM2612Write(mame_ym2612, 0, b1);
									MAME_YM2612Write(mame_ym2612, 1, b2);
									break;
								}
								FMChips|=CHIPS_USED_YM2612;
							}
							break;
						}
						break;

					case VGM_YM2612_PCM_SEEK: { // Set position in PCM data
						unsigned char buf [4];
						gzread( coreInputFile, buf, sizeof buf );
						corePCM_bufferPos = buf [3] * 0x1000000L + buf [2] * 0x10000L + buf [1] * 0x100L + buf [0];
						assert( corePCM_bufferPos < corePCM_bufferSize );
						break;
											  }

					case VGM_DATA_BLOCK: { // data block at beginning of file
						unsigned char buf[6];
						unsigned long data_size;
						gzread( coreInputFile, buf, sizeof(buf));
						assert( buf[0] == 0x66 );
						data_size = (buf[5] << 24) | (buf[4] << 16) | (buf[3] << 8) | buf[2];
						switch (buf[1]) {
					case VGM_DATA_BLOCK_YM2612_PCM:
						if ( !corePCM_buffer )
						{
							corePCM_bufferSize = data_size;
							corePCM_buffer = malloc( corePCM_bufferSize );
							if ( corePCM_buffer )
							{
								gzread( coreInputFile, corePCM_buffer, corePCM_bufferSize );
								break; // <-- exits out of (nested) case block on successful load
							}
						}
						// ignore data block for subsequent blocks and also on malloc() failure
						gzseek( coreInputFile, corePCM_bufferSize, SEEK_CUR );
						break;
					default:
						// skip unknown data blocks
						gzseek( coreInputFile, data_size, SEEK_CUR );
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
							gzungetc(VGM_END,coreInputFile);
						} else {
							// if there is looping, and the required number of loops have played, then go to fadeout
							if ( (!pluginOptions.LoopForever) && (NumLoopsDone>NumLoops) && (LoopingFadeOutTotal==-1) ) {
								// Start fade out
								LoopingFadeOutTotal=(long)LoopingFadeOutms*44100/1000;  // number of samples to fade over
								LoopingFadeOutCounter=LoopingFadeOutTotal;
							}
							// Loop the file
							gzseek(coreInputFile,LoopOffset,SEEK_SET);
						}
						break;

					default:
						// Unknown commands
						if ( b1 >= VGM_RESERVED_1_PARAM_BEGIN && b1 <= VGM_RESERVED_1_PARAM_END )
							gzseek(coreInputFile,1,SEEK_CUR);
						else if ( b1 >= VGM_RESERVED_2_PARAMS_BEGIN && b1 <= VGM_RESERVED_2_PARAMS_END )
							gzseek(coreInputFile,2,SEEK_CUR);
						else if ( b1 >= VGM_RESERVED_3_PARAMS_BEGIN && b1 <= VGM_RESERVED_3_PARAMS_END )
							gzseek(coreInputFile,3,SEEK_CUR);
						else if ( b1 >= VGM_RESERVED_4_PARAMS_BEGIN && b1 <= VGM_RESERVED_4_PARAMS_END )
							gzseek(coreInputFile,4,SEEK_CUR);
#if _DEBUG
						{
							char buffer[10*1024];
							sprintf(buffer,"Invalid data 0x%02x at offset 0x%06x in file \"%s\". Debug?",b1,gztell(coreInputFile)-1, lastfn);
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
								SN76489_UpdateOne(coreSN76489,&l,&r);
								l = (int)(l * SN_preamp);
								r = (int)(r * SN_preamp);
							}
						}

						if (YM2413Clock) {
							// YM2413
							if USINGCHIP(CHIPS_USED_YM2413) {
								int channels[2];
								NumChipsUsed++;
								if(YM2413_enable)
						  {
							  OPLL_calc_stereo(coreYM2413,channels);
							  l += (int)(channels[0] * YM2413_preamp);
							  r += (int)(channels[1] * YM2413_preamp);
						  }
							}
						}

						if (YM2612Clock) {
							// YM2612
							if USINGCHIP(CHIPS_USED_YM2612) {
								int Left=0,Right=0;
								if (YM2612_enable) {
									switch (YM2612engine)
									{
									case YM2612_GENS:
										{
											int *Buffer[2];
											NumChipsUsed++;
											Buffer[0]=&Left;
											Buffer[1]=&Right;

											GENS_YM2612_Update(coreYM2612, Buffer,1);
											GENS_YM2612_DacAndTimers_Update(coreYM2612, Buffer,1);
											Left =(int)(Left  * YM2612_preamp);
											Right=(int)(Right * YM2612_preamp);
										}
										break;
									case YM2612_MAME: 
										{
											int Buffer[2] = {0,0};
											MAME_YM2612UpdateOne(mame_ym2612, Buffer, 1);
											Left =(int)(Buffer[0] * YM2612_preamp);
											Right=(int)(Buffer[1] * YM2612_preamp);
										}
										break;
									}

								} else
									Left=Right=0;

								l+=Left;
								r+=Right;
							}
						}

						if (YM2151Clock) {
							// YM2151
							if USINGCHIP(CHIPS_USED_YM2151) {
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

						if ((Overdrive)&&(NumChipsUsed)) {
							l=l*8/NumChipsUsed;
							r=r*8/NumChipsUsed;
						}

						if (LoopingFadeOutTotal!=-1) { // Fade out
							long v;
							// Check if the counter has finished
							if (LoopingFadeOutCounter<=0) {
								// if so, go to pause between tracks
								PauseBetweenTracksCounter=(long)PauseBetweenTracksms*44100/1000;
							} else {
								// Alter volume
								v=LoopingFadeOutCounter*FADEOUT_MAX_VOLUME/LoopingFadeOutTotal;
								l=(long)l*v/FADEOUT_MAX_VOLUME;
								r=(long)r*v/FADEOUT_MAX_VOLUME;
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
					apply_replay_gain_16bit(ReplayGain,ReplayPeak,SampleBuffer,samplesinbuffer,NCH,ReplayNoClip);

				x=mod.outMod->GetWrittenTime();  // returns time written in ms (used for syncing up vis stuff)
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


__declspec( dllexport ) intptr_t winampGetExtendedRead_open(const char *filename, int *size, int *bitDepth, int *channels, int *samplingRate)
{
	VGMCore *core = vgmcore_init();

	// In general, don't apply replay gain
	BOOL killReplayGain = TRUE;
	// We try to detect if we're in Replay Gain calculating mode
	// to apply some speedups/sanity checks
	BOOL calculatingReplayGain = FindWindow(NULL, "Calculating Replay Gain") != NULL;

	// HACK: if the Replay Gain window is not there, turn it on
	if (pluginOptions.enableReplayGainHack && !calculatingReplayGain)
	{
		killReplayGain = FALSE;
	}

	if (core && vgmcore_loadfile(core, filename, killReplayGain) == 0 )
	{
		core->coreNumLoops = (calculatingReplayGain?0:NumLoops);
		core->coreLoopingFadeOutms = (calculatingReplayGain?0:LoopingFadeOutms);
		core->coreFilterType = filter_type;
		core->coreLoopForever = (calculatingReplayGain?0:pluginOptions.LoopForever); // for safety for Replay Gain, etc

		*size = vgmcore_getlength(core) * 4;
		*bitDepth = 16; // MAYBE: support different depth/channels/sampling rate
		*channels = 2;
		*samplingRate = 44100;

		return (intptr_t)core;
	}
	else
		return (intptr_t)NULL;
}

__declspec( dllexport ) intptr_t winampGetExtendedRead_getData(intptr_t handle, char *dest, int len, int *killswitch)
{
	int bytesPerSample = 4; // 16-bit stereo
	return vgmcore_getsamples(
		(VGMCore*)handle,
		(short*)dest, 
		len / bytesPerSample,
		killswitch
	) * bytesPerSample;
}

__declspec( dllexport ) int winampGetExtendedRead_setTime(intptr_t handle, int millisecs)
{
	return vgmcore_seek((VGMCore*)handle, millisecs);
}

__declspec( dllexport ) void winampGetExtendedRead_close(intptr_t handle)
{
	vgmcore_free((VGMCore*)handle);
}
