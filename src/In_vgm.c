//-----------------------------------------------------------------
// in_vgm
// VGM audio input plugin for Winamp
// http://www.smspower.org/music
// by Maxim <maxim@mwos.cjb.net> in 2001
//-----------------------------------------------------------------

#define FMPERCENT 60

#define PLUGINNAME "VGM input plugin v0.23"
#define MINVERSION 0x100
#define REQUIREDMAJORVER 0x100
#define INISECTION "Maxim's VGM input plugin"
#define MINGD3VERSION 0x100
#define REQUIREDGD3MAJORVER 0x100
// PSG has 4 channels, 2^4-1=0xf
#define PSGMUTE_ALLON 0xf
// YM2413 has 14 (9 + 5 percussion), BUT it uses 1=mute, 0=on
#define YM2413MUTE_ALLON 0

#include <windows.h>
#include <stdio.h>

#include "in2.h"

#define EMU2413_COMPACTION
#include "emu2413.h"

#include "sn76489.h"
#include "zlib.h"
#include "resource.h"
#include "urlmon.h"

#define ROUND(x) ((int)(x>0?x+0.5:x-0.5))

typedef unsigned short wchar;	// For Unicode strings

// avoid CRT. Evil. Big. Bloated.
BOOL WINAPI _DllMainCRTStartup(HANDLE hInst, ULONG ul_reason_for_call, LPVOID lpReserved)
{
	return TRUE;
}

// post this to the main window at end of file (after playback has stopped)
#define WM_WA_MPEG_EOF WM_USER+2

// raw configuration
#define NCH 2			// Number of channels
						// NCH 1 doesn't work properly... yet. I might fix it later.
#define SAMPLERATE 44100// Sampling rate

In_Module mod;			// the output module (declared near the bottom of this file)
char lastfn[MAX_PATH]="";	// currently playing file (used for getting info on the current file)
int paused;				// are we paused?
char TextFileName[MAX_PATH];	// holds a filename for the Unicode text file

#define SampleBufferSize (576*NCH*2)
short SampleBuffer[SampleBufferSize];	// sample buffer

gzFile *InputFile;

OPLL *opll;

struct TVGMHeader {
	char	VGMIdent[4];	// "Vgm "
	long	EoFOffset;		// relative offset (from this point, 0x04) of the end of file
	long	Version;		// 0x00000101 for 1.01
	long	PSGClock;		// typically 3579545, 0 for no PSG
	long	FMClock;		// typically 3579545, 0 for no FM
	long	GD3Offset;		// relative offset (from this point, 0x14) of the Gd3 tag, 0 if not present
	long	TotalLength;	// in samples
	long	LoopOffset;		// relative again (to 0x1c), 0 if no loop
	long	LoopLength;		// in samples, 0 if no loop
	long	RecordingRate;	// in Hz, for speed-changing, 0 for no changing
};

struct TGD3Header {
	char	IDString[4];	// "Gd3 "
    long	Version;		// 0x000000100 for 1.00
	long	Length;			// Length of string data following this point
};

//enum FileType { ftVGM,ftSSLGYM };

int killDecodeThread=0;						// the kill switch for the decode thread
HANDLE thread_handle=INVALID_HANDLE_VALUE;	// the handle to the decode thread

DWORD WINAPI __stdcall DecodeThread(void *b); // the decode thread procedure

int
	TrackLengthInms,	// Current track length in ms
	PlaybackRate,	// in Hz
	FileRate,		// in Hz
	NumLoops,		// how many times to play looped section
	NumLoopsDone,	// how many loops we've played
	LoopLengthInms,	// length of looped section in ms
	LoopOffset,		// File offset of looped data start
	PSGClock=1,		// SN76489 clock rate
	FMClock=1,		// YM2413 (and other) clock rate
	SeekToSampleNumber,	// For seeking
	FileInfoJapanese,	// Whether to show Japanese in the info dialogue
	UseMB,			// Whether to open HTML in the MB
	AutoMB,			// Whether to automatically show HTML in MB
	ForceMBOpen;	// Whether to force the MB to open if closed when doing AutoMB
long int 
	FMChannels=YM2413MUTE_ALLON;		// backup when stopped. PSG does it itself.
char
	TrackTitleFormat[100],			// Track title formatting
	CurrentURLFilename[MAX_PATH],	// Filename current URL has been saved to
	CurrentURL[1024];				// Current URL
char
	*FilenameForInfoDlg;			// Filename passed to file info dialogue

//-----------------------------------------------------------------
// Check if string is a URL
//-----------------------------------------------------------------
int IsURL(char *url) {
	if (
		(strstr(url,"http://")==url) ||
		(strstr(url,"ftp://" )==url) ||
		(strstr(url,"www."   )==url)
	) return 1;
	return 0;
};

//-----------------------------------------------------------------
// Open URL in minibrowser or browser
//-----------------------------------------------------------------
void OpenURL(char *url) {
	FILE *f;

	f=fopen(TextFileName,"wb");
	// Put start text
	fprintf(f,
		"<html><head><META HTTP-EQUIV=\"Refresh\" CONTENT=\"0; URL=%s\"></head><body>Opening %s<br><a href=\"%s\">Click here</a> if page does not open</body></html>",
		url,url,url
	);
	fclose(f);

	if (UseMB) {
		url=malloc(strlen(TextFileName)+9);
		strcpy(url,"file:///");
		strcat(url,TextFileName);
		SendMessage(mod.hMainWindow,WM_USER,(WPARAM)NULL,241);	// open minibrowser
		SendMessage(mod.hMainWindow,WM_USER,(WPARAM)url,241);	// display file
		free(url);
	}
	else ShellExecute(mod.hMainWindow,NULL,TextFileName,NULL,NULL,SW_SHOWNORMAL);
};


//-----------------------------------------------------------------
// Show GD3 info as HTML
//-----------------------------------------------------------------
void InfoInBrowser(char *filename, int ForceOpen) {
	FILE *f;
	gzFile	*fh;
	struct TVGMHeader	VGMHeader;
	struct TGD3Header	GD3Header;
	wchar *GD3string,*p;
	int i,j;
	char *url;
	const char What[10][32]={
		"Track title",
		"",
		"Game name",
		"",
		"System name",
		"",
		"Track author",
		"",
		"Game release date",
		"VGM creator"
	};

	// Read in Unicode string
	fh=gzopen(filename,"rb");
	i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

	if (
		(i<sizeof(VGMHeader)) ||										// file too short/error reading
		(strncmp(VGMHeader.VGMIdent,"Vgm ",4)!=0) ||					// no marker
		(VGMHeader.Version<MINVERSION) ||								// below min ver
		((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER) ||	// != required major ver
		(!VGMHeader.GD3Offset)											// no GD3
	) {
		gzclose(fh);
		return;
	};
	gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
	i=gzread(fh,&GD3Header,sizeof(GD3Header));
	if (
		(i<sizeof(GD3Header)) ||										// file too short/error reading
		(strncmp(GD3Header.IDString,"Gd3 ",4)!=0) ||					// no marker
		(GD3Header.Version<MINGD3VERSION) ||							// below min ver
		((GD3Header.Version & REQUIREDGD3MAJORVER)!=REQUIREDGD3MAJORVER)// != required major ver
	) {
		gzclose(fh);
		return;
	};
	p=malloc(GD3Header.Length*2);	// Allocate memory for string data (x2 for Unicode)
	i=gzread(fh,p,GD3Header.Length*2);	// Read it in
	gzclose(fh);

	if (i<sizeof(GD3Header)) return;

	GD3string=p;

	f=fopen(TextFileName,"wb");
	// Put start text
	fputs(
		#include "htmlbefore.txt"
		,f);
	for (i=0;i<8;++i) {
		if (i%2-1) fprintf(f,"<tr><td class=what>%s</td><td class=is>",What[i]);
		if (wcslen(GD3string))	// have a string
			for (j=0;j<(int)wcslen(GD3string);++j)
				fprintf(f,"&#%d;",*(GD3string+j));
		else
			fputs("&nbsp;",f);

		if (i%2)
			fputs("</td></tr>",f);
		else
			fputs("</td><td class=is>",f);

		GD3string+=wcslen(GD3string)+1;
	};
	for (i=8;i<10;++i) {
		fprintf(f,"<tr><td class=what>%s</td><td colspan=2 class=is>",What[i]);
		if (wcslen(GD3string))	// have a string
			for (j=0;j<(int)wcslen(GD3string);++j)
				fprintf(f,"&#%d;",*(GD3string+j));
		else
			fputs("&nbsp;",f);
		fputs("</td></tr>",f);
		GD3string+=wcslen(GD3string)+1;
	};
	fputs("<tr><td class=what>Notes</td><td colspan=2 class=is>",f);

	for (j=0;j<(int)wcslen(GD3string);++j) {
		if (*(GD3string+j)=='\n')
			fprintf(f,"<br>");
		else
			fprintf(f,"&#%d;",*(GD3string+j));
	};

	fputs(
		#include "htmlafter.txt"
		,f
	);
	fclose(f);

	free(p);

	if (UseMB) {
		url=malloc(strlen(TextFileName)+9);
		strcpy(url,"file:///");
		strcat(url,TextFileName);
		if (ForceOpen) SendMessage(mod.hMainWindow,WM_USER,(WPARAM)NULL,241);	// open minibrowser
		SendMessage(mod.hMainWindow,WM_USER,(WPARAM)url,241);	// display file
		free(url);
	}
	else ShellExecute(mod.hMainWindow,NULL,TextFileName,NULL,NULL,SW_SHOWNORMAL);
};

//-----------------------------------------------------------------
// Configuration dialogue
//-----------------------------------------------------------------
// Dialogue box callback function
BOOL CALLBACK ConfigDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam) {
    switch (wMessage) {	// process messages
		case WM_INITDIALOG:	{// Initialise dialogue
			int i;
			if (PSGClock) { // Check PSG channel checkboxes
				for (i=0;i<4;i++) CheckDlgButton(DlgWin,cbTone1+i,((PSGMute & (1<<i))>0));
				CheckDlgButton(DlgWin,cbPSGToneAll,((PSGMute&0x7)==0x7));
			} else {	// or disable them
				for (i=0;i<4;i++) EnableWindow(GetDlgItem(DlgWin,cbTone1+i),FALSE);
				EnableWindow(GetDlgItem(DlgWin,cbPSGToneAll),FALSE);
				EnableWindow(GetDlgItem(DlgWin,lblPSGPerc),FALSE);
				EnableWindow(GetDlgItem(DlgWin,gbPSG),FALSE);
			};
			if (FMClock) { // Check FM channel checkboxes
				for (i=0;i<15;i++) CheckDlgButton(DlgWin,cbFM1+i,!((FMChannels & (1<<i))>0));
				CheckDlgButton(DlgWin,cbFMToneAll,((FMChannels&0x1ff )==0));
				CheckDlgButton(DlgWin,cbFMPercAll,((FMChannels&0x3e00)==0));
			} else {
				for (i=0;i<15;i++) EnableWindow(GetDlgItem(DlgWin,cbFM1+i),FALSE);
				EnableWindow(GetDlgItem(DlgWin,cbFMToneAll),FALSE);
				EnableWindow(GetDlgItem(DlgWin,cbFMPercAll),FALSE);
				EnableWindow(GetDlgItem(DlgWin,lblExtraTone),FALSE);
				EnableWindow(GetDlgItem(DlgWin,lblExtraToneNote),FALSE);
				EnableWindow(GetDlgItem(DlgWin,gbYM2413),FALSE);
			};
			// Set loop count
			SetDlgItemInt(DlgWin,ebLoopCount,NumLoops,TRUE);
			// Set title format text
			SetDlgItemText(DlgWin,ebTrackTitle,TrackTitleFormat);
			// Speed settings
			switch (PlaybackRate) {
				case 0:
					CheckRadioButton(DlgWin,rbRateOriginal,rbRateOther,rbRateOriginal);
					break;
				case 50:
					CheckRadioButton(DlgWin,rbRateOriginal,rbRateOther,rbRate50);
					break;
				case 60:
					CheckRadioButton(DlgWin,rbRateOriginal,rbRateOther,rbRate60);
					break;
				default:
					CheckRadioButton(DlgWin,rbRateOriginal,rbRateOther,rbRateOther);
					break;
			};
			EnableWindow(GetDlgItem(DlgWin,ebPlaybackRate),(IsDlgButtonChecked(DlgWin,rbRateOther)?TRUE:FALSE));
			if (PlaybackRate!=0) {
				char tempstr[18];	// buffer for itoa
				SetDlgItemText(DlgWin,ebPlaybackRate,itoa(PlaybackRate,tempstr,10));
			} else {
				SetDlgItemText(DlgWin,ebPlaybackRate,"60");
			};
			CheckDlgButton(DlgWin,cbUseMB        ,UseMB);
			CheckDlgButton(DlgWin,cbAutoMB       ,AutoMB);
			CheckDlgButton(DlgWin,cbForceMBOpen  ,ForceMBOpen);
			CheckDlgButton(DlgWin,cbPSGOverSample,PSGOverSample);
			EnableWindow(GetDlgItem(DlgWin,cbAutoMB     ),UseMB);
			EnableWindow(GetDlgItem(DlgWin,cbForceMBOpen),UseMB & AutoMB);

			return (TRUE);
		};
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
				case IDOK: {		// OK button
					int i;
					BOOL MyBool;
					// Loop count
					i=GetDlgItemInt(DlgWin,ebLoopCount,&MyBool,TRUE);
					if (MyBool) NumLoops=i;
					// Track title format
					GetDlgItemText(DlgWin,ebTrackTitle,TrackTitleFormat,100);
					// Playback rate
					PlaybackRate=0;
					if (IsDlgButtonChecked(DlgWin,rbRate50)) {
						PlaybackRate=50;
					} else if (IsDlgButtonChecked(DlgWin,rbRate60)) {
						PlaybackRate=60;
					} else if (IsDlgButtonChecked(DlgWin,rbRateOther)) {
						i=GetDlgItemInt(DlgWin,ebPlaybackRate,&MyBool,TRUE);
						if ((MyBool) && (i>0) && (i<500)) PlaybackRate=i;
					};
				};	// fall through to close dialogue
                case IDCANCEL:	// [X] button, Alt+F4, etc
                    EndDialog(DlgWin,0);
                    return (TRUE) ;
				case cbTone1:
				case cbTone2:
				case cbTone3:
				case cbTone4:
					PSGMute=(IsDlgButtonChecked(DlgWin,cbTone1)     )
                          | (IsDlgButtonChecked(DlgWin,cbTone2) << 1)
                          | (IsDlgButtonChecked(DlgWin,cbTone3) << 2)
                          | (IsDlgButtonChecked(DlgWin,cbTone4) << 3);
					CheckDlgButton(DlgWin,cbPSGToneAll,((PSGMute&7)==7));
					break;
				case rbRateOriginal:
				case rbRate50:
				case rbRate60:
				case rbRateOther:
					CheckRadioButton(DlgWin,rbRateOriginal,rbRateOther,LOWORD(wParam));
					EnableWindow(GetDlgItem(DlgWin,ebPlaybackRate),((LOWORD(wParam)==rbRateOther)?TRUE:FALSE));
					if (LOWORD(wParam)==rbRateOther) SetFocus(GetDlgItem(DlgWin,ebPlaybackRate));
					break;
				case cbFM1:		case cbFM2:		case cbFM3:		case cbFM4:
				case cbFM5:		case cbFM6:		case cbFM7:		case cbFM8:
				case cbFM9:		case cbFM10:	case cbFM11:	case cbFM12:
				case cbFM13:	case cbFM14: {
					int i;
					FMChannels=0;
					for (i=0;i<15;i++) FMChannels|=(!IsDlgButtonChecked(DlgWin,cbFM1+i))<<i;
					OPLL_setMask(opll,FMChannels);
					CheckDlgButton(DlgWin,cbFMToneAll,((FMChannels&0x1ff )==0));
					CheckDlgButton(DlgWin,cbFMPercAll,((FMChannels&0x3e00)==0));
					break;
				};
				case cbFMToneAll: {
					int i;
					const int Checked=IsDlgButtonChecked(DlgWin,cbFMToneAll);
					for (i=0;i<9;i++) CheckDlgButton(DlgWin,cbFM1+i,Checked);
					PostMessage(DlgWin,WM_COMMAND,cbFM1,0);
					break;
				};
				case cbFMPercAll: {
					int i;
					const int Checked=IsDlgButtonChecked(DlgWin,cbFMPercAll);
					for (i=0;i<5;i++) CheckDlgButton(DlgWin,cbFM10+i,Checked);
					PostMessage(DlgWin,WM_COMMAND,cbFM1,0);
					break;
				};
				case cbPSGToneAll: {
					int i;
					const int Checked=IsDlgButtonChecked(DlgWin,cbPSGToneAll);
					for (i=0;i<3;i++) CheckDlgButton(DlgWin,cbTone1+i,Checked);
					PostMessage(DlgWin,WM_COMMAND,cbTone1,0);
					break;
				};
				case cbUseMB:
					UseMB=IsDlgButtonChecked(DlgWin,cbUseMB);
					EnableWindow(GetDlgItem(DlgWin,cbAutoMB     ),UseMB);
					EnableWindow(GetDlgItem(DlgWin,cbForceMBOpen),UseMB & AutoMB);
					break;
				case cbAutoMB:
					AutoMB=IsDlgButtonChecked(DlgWin,cbAutoMB);
					EnableWindow(GetDlgItem(DlgWin,cbForceMBOpen),UseMB & AutoMB);
					break;
				case cbForceMBOpen:
					ForceMBOpen=IsDlgButtonChecked(DlgWin,cbForceMBOpen);
					break;
				case cbPSGOverSample:
					PSGOverSample=IsDlgButtonChecked(DlgWin,cbPSGOverSample);
					break;

            };
            break ;
    }
    return (FALSE) ;    // return FALSE to signify message not processed
};
void config(HWND hwndParent)
{
	DialogBox(mod.hDllInstance, "CONFIGDIALOGUE", hwndParent, ConfigDialogProc);
};

//-----------------------------------------------------------------
// About dialogue
//-----------------------------------------------------------------
void about(HWND hwndParent)
{
	MessageBox(hwndParent,
		"VGM 1.01 Winamp input plugin\n\n"
		"by Maxim in 2001 and 2002\n"
		"maxim@mwos.cjb.net\n"
		"http://www.smspower.org/music\n\n"
		"Current status:\n"
		"PSG - emulated as a perfect device, leading to slight differences in sound\n"
		"  compared to the real thing. Noise is as accurate as it can be since the\n"
		"  feedback system is unknown.\n"
		"YM2413 - via EMU2413\n"
		"YM(other) - not yet considered\n\n"
		"Don\'t be put off by the pre-1.0 version numbers. This is a non-commercial\n"
		"project and as such it is permanently in beta."
		,PLUGINNAME,MB_OK);
}

//-----------------------------------------------------------------
// Initialisation (one-time)
//-----------------------------------------------------------------
void init() {
	char INIFileName[MAX_PATH];
	char *PChar;

	GetModuleFileName(mod.hDllInstance,INIFileName,MAX_PATH);	// get exe path
    GetFullPathName(INIFileName,MAX_PATH,INIFileName,&PChar);	// make it fully qualified plus find the filename bit
	strcpy(PChar,"plugin.ini");	// Change to plugin.ini


    GetTempPath(MAX_PATH,TextFileName);
	strcat(TextFileName,"GD3.html");
	GetShortPathName(TextFileName,TextFileName,MAX_PATH);

	NumLoops        =GetPrivateProfileInt(INISECTION,"NumLoops"            ,2,INIFileName);
	PlaybackRate    =GetPrivateProfileInt(INISECTION,"Playback rate"       ,0,INIFileName);
	FileInfoJapanese=GetPrivateProfileInt(INISECTION,"Japanese in info box",0,INIFileName);
	UseMB           =GetPrivateProfileInt(INISECTION,"Use Minibrowser"     ,1,INIFileName);
	AutoMB          =GetPrivateProfileInt(INISECTION,"Auto-show HTML"      ,0,INIFileName);
	ForceMBOpen     =GetPrivateProfileInt(INISECTION,"Force MB open"       ,0,INIFileName);
	PSGOverSample   =GetPrivateProfileInt(INISECTION,"PSG oversampling"    ,0,INIFileName);

	GetPrivateProfileString(INISECTION,"Title format","%t (%g) - %a",TrackTitleFormat,100,INIFileName);

	PSGMute=0xf;	// important
};

//-----------------------------------------------------------------
// Deinitialisation (one-time)
//-----------------------------------------------------------------
void quit() {
	char INIFileName[MAX_PATH];
	char *PChar;	// pointer to INI string
	char tempstr[18];	// buffer for itoa

	GetModuleFileName(mod.hDllInstance,INIFileName,MAX_PATH);	// get exe path
    GetFullPathName(INIFileName,MAX_PATH,INIFileName,&PChar);	// make it fully qualified plus find the filename bit
	strcpy(PChar,"plugin.ini");	// Change to plugin.ini

	WritePrivateProfileString(INISECTION,"NumLoops"            ,itoa(NumLoops        ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Playback rate"       ,itoa(PlaybackRate    ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Japanese in info box",itoa(FileInfoJapanese,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Title format"        ,TrackTitleFormat                 ,INIFileName);
	WritePrivateProfileString(INISECTION,"Use Minibrowser"     ,itoa(UseMB           ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Auto-show HTML"      ,itoa(AutoMB          ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"Force MB open"       ,itoa(ForceMBOpen     ,tempstr,10),INIFileName);
	WritePrivateProfileString(INISECTION,"PSG oversampling"    ,itoa(PSGOverSample   ,tempstr,10),INIFileName);

	DeleteFile(TextFileName);
};

//-----------------------------------------------------------------
// Pre-extension check file claiming
//-----------------------------------------------------------------
int isourfile(char *fn) {
	char *p=strrchr(fn,'.');

	return (
		(p) && (
			(strcmpi(p,".vgm")==0) ||
			(strcmpi(p,".vgz")==0)
		)
		&&
		IsURL(fn)
	);
	// I must be getting good at this, that line is quite
	// impressively obfuscated
};

//-----------------------------------------------------------------
// File download callback function
//-----------------------------------------------------------------
BOOL CALLBACK DownloadDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam) {
    switch (wMessage) {	// process messages
		case WM_INITDIALOG:	// Initialise dialogue
			return (TRUE);
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDCANCEL:	// [X] button, Alt+F4, etc
                    EndDialog(DlgWin,0);
                    return (TRUE) ;
  			};
    };
    return (FALSE);    // return FALSE to signify message not processed
};

//-----------------------------------------------------------------
// Play filename
//-----------------------------------------------------------------
int play(char *fn) 
{ 
	int maxlatency;
	int thread_id;
	HANDLE f;
	int FileSize;
	struct TVGMHeader VGMHeader;
	int i;

	strcpy(CurrentURL,"");
	strcpy(CurrentURLFilename,"");

	if (IsURL(fn)) {	// It's a URL!
		HRESULT hr;
		HWND DlgWnd;

		strcpy(CurrentURL,fn);

		// Try to download file

		DlgWnd=CreateDialog(mod.hDllInstance,"DOWNLOADDIALOGUE",mod.hMainWindow,DownloadDialogProc); // Open dialog
		hr=URLDownloadToCacheFile(
			NULL,				// Must be NULL for non-ActiveX
			CurrentURL,			// URL to get
			CurrentURLFilename,	// Buffer to get location string
			MAX_PATH,			// Buffer size
			0,					// Reserved, must be 0
			NULL);				// Callback function
		// URLMon.h says:
		//		Flags for the UrlDownloadToCacheFile                                                                    
		//		...
		//		#define URLOSTRM_USECACHEDCOPY                  0x2      // Get from cache if available else download      
		// but where do I put it? If I put it in the reserved DWORD nothing happens :/
		// This is a problem because:
		// - the connect dialogue has Cancel, not Work Offline
		// - every time you close the MB it switches off Offline mode
		// Bah >:(

		SendMessage(DlgWnd,WM_CLOSE,0,0);
        SendMessage(mod.hMainWindow,WM_PAINT,0,0);

		if (hr==S_OK) {
			strcpy(fn,CurrentURLFilename);
		} else {
			// MessageBox(mod.hMainWindow,"Error downloading file","Error",0);
			return -1;	// File not found
		};

	};

	if ((*lastfn) && (strcmp(fn,lastfn)!=0)) {
		// If file has changed, reset channel muting
		PSGMute=PSGMUTE_ALLON;
		FMChannels=YM2413MUTE_ALLON;
	};

	strcpy(lastfn,fn);
	
	InputFile=gzopen(fn,"rb");	// Open file - read, binary

	if (InputFile==NULL) {
		return -1;
	};	// File not opened/found, advance playlist

	// Get file size for bitrate calculations 
	f=CreateFile(fn,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	FileSize=GetFileSize(f,NULL);
	CloseHandle(f);

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
	};

	// Check for VGM marker
	if (strncmp(VGMHeader.VGMIdent,"Vgm ",4)!=0) {
		char msgstring[1024];
		char foundstr[5];
		strncpy(foundstr,VGMHeader.VGMIdent,4);
		foundstr[4]='\0';
		sprintf(msgstring,"VGM marker not found in \"%s\"\nFound this instead: \"%s\"",fn,foundstr);
		MessageBox(mod.hMainWindow,msgstring,mod.description,0);
		gzclose(InputFile);
		InputFile=NULL;
		return -1;
	};

	// Check version
	if ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER)) {
		char msgstring[1024];
		sprintf(msgstring,"Unsupported VGM version found in \"%s\" (%x).\n\nDo you want to try to play it anyway?",fn,VGMHeader.Version);

		if (MessageBox(mod.hMainWindow,msgstring,mod.description,MB_YESNO+MB_DEFBUTTON2)==IDNO) {
			gzclose(InputFile);
			InputFile=NULL;
			return -1;
		};
	};

	// Get length
	if (VGMHeader.TotalLength==0) {
		TrackLengthInms=0;
	} else {
		TrackLengthInms=(int)(VGMHeader.TotalLength/44.1);
	};

	// Get loop data
	if (VGMHeader.LoopOffset==0) {
		LoopLengthInms=0;
		LoopOffset=0;
	} else {
		LoopLengthInms=(int)(VGMHeader.LoopLength/44.1);
		LoopOffset=VGMHeader.LoopOffset+0x1c;
	};

	// Get clock values
	PSGClock=VGMHeader.PSGClock;
	FMClock=VGMHeader.FMClock;

	// Get rate
	FileRate=VGMHeader.RecordingRate;

	// Open output plugin
	maxlatency = mod.outMod->Open(SAMPLERATE,NCH,16, -1,-1);
	if (maxlatency < 0) {	// error opening device
		gzclose(InputFile);
		InputFile=NULL;
		return 1;
	};

	// Set info
	if (TrackLengthInms==0) {
		mod.SetInfo(0,SAMPLERATE/1000,NCH,1);
	} else {
		mod.SetInfo(
			(int)(FileSize*8000.0/1024/TrackLengthInms+0.5),	// Bitrate /Kbps (+0.5 for rounding)
			SAMPLERATE/1000,			// Sampling rate /kHz
			NCH,						// Channels
			1);							// Synched (?)
	};

	// Open page in MB if wanted
	if (UseMB & AutoMB) InfoInBrowser(fn,ForceMBOpen);

	// initialize vis stuff
	mod.SAVSAInit(maxlatency,SAMPLERATE);
	mod.VSASetInfo(NCH,SAMPLERATE);

	mod.outMod->SetVolume(-666); // set the output plug-ins default volume

    gzseek(InputFile,0x40,SEEK_SET);

	if (FMClock) {
		// Start up the YM2413
		OPLL_init(FMClock,44100);
		opll = OPLL_new();
		OPLL_reset(opll);
		OPLL_reset_patch(opll,0);
		OPLL_setMask(opll,FMChannels);
	};

	// Start up SN76489
	if (PSGClock) SN76489_Init(PSGClock);

	// Reset some stuff
	paused=0;
	NumLoopsDone=0;
	SeekToSampleNumber=-1;

	// Start up decode thread
	killDecodeThread=0;
	thread_handle = CreateThread(NULL,0,(LPTHREAD_START_ROUTINE) DecodeThread,(void *) &killDecodeThread,0,&thread_id);
	// Set it to highest priority to avoid breaks
	SetThreadPriority(thread_handle,THREAD_PRIORITY_HIGHEST);
	
	return 0; 
}

//-----------------------------------------------------------------
// Pause
//-----------------------------------------------------------------
void pause() {
	paused=1;
	mod.outMod->Pause(1);
};

//-----------------------------------------------------------------
// Unpause
//-----------------------------------------------------------------
void unpause() {
	paused=0;
	mod.outMod->Pause(0);
};

//-----------------------------------------------------------------
// Is it paused?
//-----------------------------------------------------------------
int ispaused() {
	return paused;
};

//-----------------------------------------------------------------
// Stop
//-----------------------------------------------------------------
void stop() {
	SeekToSampleNumber=0;	// Fixes near=eof errors - it breaks it out of the wait-for-output-to-stop loop in DecodeThread
	if (thread_handle != INVALID_HANDLE_VALUE) {	// If the playback thread is going
		killDecodeThread=1;	// Set the flag telling it to stop
		if (WaitForSingleObject(thread_handle,INFINITE) == WAIT_TIMEOUT) {
			MessageBox(mod.hMainWindow,"error asking thread to die!","error killing decode thread",0);
			TerminateThread(thread_handle,0);
		}
		CloseHandle(thread_handle);
		thread_handle = INVALID_HANDLE_VALUE;
	};
	if (InputFile!=NULL) {
		gzclose(InputFile);	// Close input file
		InputFile=NULL;
	};

	mod.outMod->Close();	// close output plugin

	mod.SAVSADeInit();	// Deinit vis

	// Stop YM2413
	if (FMClock) {
		OPLL_delete(opll);
		OPLL_close();
	};

	// Stop SN76489
	// not needed
};

//-----------------------------------------------------------------
// Get track length in ms
//-----------------------------------------------------------------
int getlength() {
	return (int) ((TrackLengthInms+NumLoops*LoopLengthInms)*((PlaybackRate&&FileRate)?(float)FileRate/PlaybackRate:1));
}

//-----------------------------------------------------------------
// Get playback position in ms - sent to output plugin
//-----------------------------------------------------------------
int getoutputtime() {
	return mod.outMod->GetOutputTime();
}

//-----------------------------------------------------------------
// Seek
//-----------------------------------------------------------------
void setoutputtime(int time_in_ms) {
	int IntroLengthInms=TrackLengthInms-LoopLengthInms;
	long int FMChannels;

	if (InputFile==NULL) return;

	if ((LoopLengthInms>0) &&	// file is looped
		(time_in_ms>IntroLengthInms)) {	// seek is past loop point
			gzseek(InputFile,LoopOffset,SEEK_SET);
			NumLoopsDone=(time_in_ms-IntroLengthInms)/LoopLengthInms;
			SeekToSampleNumber=(int)((time_in_ms-IntroLengthInms-NumLoopsDone*LoopLengthInms)*44.1);
	} else {	// file is not looped
		gzseek(InputFile,0x40,SEEK_SET);
		NumLoopsDone=0;
		SeekToSampleNumber=(int)(time_in_ms*44.1);
	};

	mod.outMod->Flush(time_in_ms);

	FMChannels=OPLL_toggleMask(opll,0);
	OPLL_reset(opll);
	OPLL_setMask(opll,FMChannels);
}

//-----------------------------------------------------------------
// Set volume - sent to output plugin
//-----------------------------------------------------------------
void setvolume(int volume) {
	mod.outMod->SetVolume(volume);
};

//-----------------------------------------------------------------
// Set balance - sent to output plugin
//-----------------------------------------------------------------
void setpan(int pan) {
	mod.outMod->SetPan(pan);
};

//-----------------------------------------------------------------
// File info dialogue
//-----------------------------------------------------------------
// Dialogue box callback function
char GD3Strings[11][256];
BOOL CALLBACK FileInfoDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam) {
	const int DlgFields[11]={ebTitle,ebTitle,ebName,ebName,ebSystem,ebSystem,ebAuthor,ebAuthor,ebDate,ebCreator,ebNotes};
    switch (wMessage) {	// process messages
		case WM_INITDIALOG:	{	// Initialise dialogue
			// Read VGM/GD3 info
			gzFile	*fh;
			struct TVGMHeader	VGMHeader;
			struct TGD3Header	GD3Header;
			int i;
			unsigned int FileSize;

			SendDlgItemMessage(DlgWin,rbEnglish+FileInfoJapanese,BM_SETCHECK,1,0);

			if (IsURL(FilenameForInfoDlg)) {
				// Filename is a URL
				if (
					(strcmp(FilenameForInfoDlg,CurrentURLFilename)==0) ||
					(strcmp(FilenameForInfoDlg,CurrentURL)==0)
					) {
					// If it's the current file, look at that temp file
					SetDlgItemText(DlgWin,ebFilename,CurrentURL);
					strcpy(FilenameForInfoDlg,CurrentURLFilename);
				} else {
					// If it's not the current file, no info
					SetDlgItemText(DlgWin,ebFilename,FilenameForInfoDlg);
					SetDlgItemText(DlgWin,ebNotes,"Information unavailable for this URL");
					return (TRUE);
				};
			} else {
				// Filename is not a URL
				SetDlgItemText(DlgWin,ebFilename,FilenameForInfoDlg);
			};

			// Get file size
			{
				HANDLE f=CreateFile(FilenameForInfoDlg,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
				char tempstr[100];
				FileSize=GetFileSize(f,NULL);
				CloseHandle(f);

				sprintf(tempstr,"%d bytes",FileSize);
				SetDlgItemText(DlgWin,ebSize,tempstr);
			};

			for (i=0;i<11;++i) GD3Strings[i][0]='\0';	// clear strings

			fh=gzopen(FilenameForInfoDlg,"rb");
			if (fh==0) {	// file not opened
				char msgstring[1024];
				sprintf(msgstring,"Unable to open:\n%s",FilenameForInfoDlg);
				MessageBox(mod.hMainWindow,msgstring,mod.description,0);
				return (TRUE);
			};
			i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

			if (i<sizeof(VGMHeader)) {
				// File too short/error reading
				char msgstring[1024];
				sprintf(msgstring,"File too short:\n%s",FilenameForInfoDlg);
				MessageBox(mod.hMainWindow,msgstring,mod.description,0);
				return (TRUE);
			} else if (strncmp(VGMHeader.VGMIdent,"Vgm ",4)!=0) {
				// VGM marker incorrect
				char msgstring[1024];
				sprintf(msgstring,"VGM marker not found in:\n%s",FilenameForInfoDlg);
				MessageBox(mod.hMainWindow,msgstring,mod.description,0);
				return (TRUE);
			} else if ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER)) {
				// VGM version incorrect
				char msgstring[1024];
				sprintf(msgstring,"Unsupported VGM version (%x) in %s",VGMHeader.Version,FilenameForInfoDlg);
				MessageBox(mod.hMainWindow,msgstring,mod.description,0);
				return (TRUE);
			} else {	// VGM header exists
				char tempstr[256];
				sprintf(tempstr,"%x.%02x",VGMHeader.Version>>8,VGMHeader.Version&0xff);
				SetDlgItemText(DlgWin,ebVersion,tempstr);

				SendDlgItemMessage(DlgWin,rbPSG,BM_SETCHECK,(VGMHeader.PSGClock!=0),0);
				SendDlgItemMessage(DlgWin,rbFM,BM_SETCHECK,(VGMHeader.FMClock!=0),0);

				sprintf(
					tempstr,
					"%d bytes (%d bps)",
					FileSize,
					(int)(FileSize*8.0/(VGMHeader.TotalLength/44100.0))
				);
				SetDlgItemText(DlgWin,ebSize,tempstr);

				sprintf(
					tempstr,
					"%.1fs total (inc. %.1fs loop)",
					VGMHeader.TotalLength/44100.0,
					VGMHeader.LoopLength/44100.0
				);
				SetDlgItemText(DlgWin,ebLength,tempstr);

				if (VGMHeader.GD3Offset>0) {
					// GD3 tag exists
					gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
					i=gzread(fh,&GD3Header,sizeof(GD3Header));
					if ((i==sizeof(GD3Header)) &&
						(strncmp(GD3Header.IDString,"Gd3 ",4)==0) &&
						(GD3Header.Version>=MINGD3VERSION) &&
						((GD3Header.Version & REQUIREDGD3MAJORVER)==REQUIREDGD3MAJORVER)) {
							// GD3 is acceptable version
							wchar *p,*GD3string;
							int i;

							p=malloc(GD3Header.Length);	// Allocate memory for string data
							gzread(fh,p,GD3Header.Length);	// Read it in
							GD3string=p;

							for (i=0;i<11;++i) {
								// Get next string and move the pointer to the next one
								WideCharToMultiByte(CP_ACP,0,GD3string,-1,GD3Strings[i],256,NULL,NULL);
//								wcstombs(GD3Strings[i],GD3string,256);
								GD3string+=wcslen(GD3string)+1;
							};

							{	// Make Notes text break properly by making \n into \r\n
								char tempstr[512];
								char *p=tempstr+strlen(GD3Strings[10]);
								strcpy(tempstr,GD3Strings[10]);
								while (p>=tempstr) {
									if (*p=='\n') {
										memmove(p+1,p,strlen(p)+1);
										*p='\r';										
									};
									p--;
								};
								strcpy(GD3Strings[10],tempstr);
							};

							free(p);	// Free memory for string buffer

							// Set non-language-changing fields here
							for (i=8;i<11;++i) {
								SetDlgItemText(DlgWin,DlgFields[i],GD3Strings[i]);
							};

							PostMessage(DlgWin,WM_COMMAND,rbEnglish+FileInfoJapanese,0);
					} else {	// Unacceptable GD3 version
						char msgstring[1024];
						sprintf(msgstring,"File too short or unsupported GD3 version (%x) in %s",GD3Header.Version,FilenameForInfoDlg);
						MessageBox(mod.hMainWindow,msgstring,mod.description,0);
						return (TRUE);
					};
				} else {	// no GD3 tag
					SetDlgItemText(DlgWin,ebNotes,"No GD3 tag or incompatible GD3 version");
					EnableWindow(GetDlgItem(DlgWin,btnUnicodeText),FALSE);	// disable button if it can't be used
				};
			};
			gzclose(fh);

			return (TRUE);
		};
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
				case IDOK:	// OK button
					FileInfoJapanese=IsDlgButtonChecked(DlgWin,rbJapanese);
					// fall through to close dialogue
                case IDCANCEL:	// [X] button, Alt+F4, etc
                    EndDialog(DlgWin,0);
                    return (TRUE);
				case btnConfigure:
					config(DlgWin);
					break;
				case rbEnglish:
				case rbJapanese:
					{
						int i;
						const int x=(LOWORD(wParam)==rbJapanese?1:0);
						for (i=0;i<4;++i) {
							SetDlgItemText(DlgWin,DlgFields[i*2+x],GD3Strings[i*2+x]);
						};
					};
					break;
				case btnURL:
					if (UseMB)
						OpenURL("http://www.smspower.org/music/index_mb.shtml");
					else
						OpenURL("http://www.smspower.org/music/index.shtml");
					break;
				case btnUnicodeText:
					InfoInBrowser(FilenameForInfoDlg,1);
					break;
				default:
					break;
            };
            break ;
    }
    return (FALSE) ;    // return FALSE to signify message not processed
};
int infoDlg(char *fn, HWND hwnd)
{
	FilenameForInfoDlg=fn;
	return DialogBox(mod.hDllInstance, "FILEINFODIALOGUE", hwnd, FileInfoDialogProc);
}

//-----------------------------------------------------------------
// Get file info for playlist/title display
//-----------------------------------------------------------------
void getfileinfo(char *filename, char *title, int *length_in_ms)
{
	long int	TrackLength;
	char		TrackTitle[1024],
				FileToUse[MAX_PATH],
				JustFileName[MAX_PATH];

	// if filename is blank then we want the current file
	if ((filename==NULL) || (*filename=='\0'))
		strcpy(FileToUse,lastfn);
	else
		strcpy(FileToUse,filename);

	if (IsURL(FileToUse)) {
		if (title) strcpy(title,FileToUse);
		if (length_in_ms) *length_in_ms=-1000;
		return;
	};

	{	// trim to just filename
		char *p=FileToUse+strlen(FileToUse);
		while (*p != '\\' && p >= FileToUse) p--;
		strcpy(JustFileName,++p);
	};

	{	// get GD3 info
		gzFile	*fh;
		struct TVGMHeader	VGMHeader;
		struct TGD3Header	GD3Header;
		int i;

		fh=gzopen(FileToUse,"rb");
		if (fh==0) {	// file not opened
			if (title) sprintf(title,"Unable to open %s",FileToUse);
			if (length_in_ms) *length_in_ms=-1000;
			return;
		};
		i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

		if (i<sizeof(VGMHeader)) {
			sprintf(TrackTitle,"File too short: %s",JustFileName);
		} else if (strncmp(VGMHeader.VGMIdent,"Vgm ",4)!=0) {
			// VGM marker incorrect
			sprintf(TrackTitle,"VGM marker not found in %s",JustFileName);
		} else if ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER)) {
			// VGM version incorrect
			sprintf(TrackTitle,"Unsupported version (%x) in %s",VGMHeader.Version,JustFileName);
		} else {
			// VGM header OK
			TrackLength=(long int) ((VGMHeader.TotalLength+NumLoops*VGMHeader.LoopLength)/44.1*((PlaybackRate&&FileRate)?(float)VGMHeader.RecordingRate/PlaybackRate:1));

			if (VGMHeader.GD3Offset>0) {
				// GD3 tag exists
				gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
				i=gzread(fh,&GD3Header,sizeof(GD3Header));
				if ((i==sizeof(GD3Header)) &&
					(strncmp(GD3Header.IDString,"Gd3 ",4)==0) &&
					(GD3Header.Version>=MINGD3VERSION) &&
					((GD3Header.Version & REQUIREDGD3MAJORVER)==REQUIREDGD3MAJORVER)) {
						// GD3 is acceptable version
						wchar *p,*GD3string;
						const char strings[]="tgsadc";	// Title Game System Author Date Creator
						char GD3strings[10][256];
						const char What[10][12]={"track title","track title","game","game","system","system","author","author","date","creator"};
						int i;

						p=malloc(GD3Header.Length);	// Allocate memory for string data
						gzread(fh,p,GD3Header.Length);	// Read it in
						GD3string=p;

						for (i=0;i<10;++i) {
							// Get next string and move the pointer to the next one
							WideCharToMultiByte(CP_ACP,0,GD3string,-1,GD3strings[i],256,NULL,NULL);
							GD3string+=wcslen(GD3string)+1;
						};

						free(p);	// Free memory for string buffer

						for (i=0;i<10;++i) if (!GD3strings[i][0]) {	// Handle empty fields...
							// First, see if I can substitute the other language
							if (i<8) strcpy(GD3strings[i],GD3strings[(i%2?i-1:i+1)]);
							// if it is still blank, put "Unknown xxx"
							if (!GD3strings[i][0]) {
								strcpy(GD3strings[i],"Unknown ");
								strcat(GD3strings[i],What[i]);
							};
						};

						strcpy(TrackTitle,TrackTitleFormat);

						i=0;
						while (i<6) {
							char SearchStr[]="%x";
							char *pos;
							// Search for format strings
							SearchStr[1]=strings[i];
							pos=strstr(TrackTitle,SearchStr);
							if (pos!=NULL) {	// format string found
								// redo this to use one string?
								char After[1024];
								*pos='\0';
								strcpy(After,TrackTitle);	// copy text before it
								if ((*(pos+2)=='j') && (i<4)) {
									strcat(After,GD3strings[i*2+1]);	// add GD3 string
									strcat(After,pos+3);	// add text after it
								} else {
									if (i==5) {
										strcat(After,GD3strings[9]);
									} else {
										strcat(After,GD3strings[i*2]);	// add GD3 string
									};
									strcat(After,pos+2);	// add text after it
								};
								strcpy(TrackTitle,After);
							} else {
								i++;
							};
						};
				};
			} else {	// No GD3 tag, so use filename
				strcpy(TrackTitle,JustFileName);
			};
		};
		gzclose(fh);
	};

	if (title) strcpy(title,TrackTitle);
	if (length_in_ms) *length_in_ms=TrackLength;
};

//-----------------------------------------------------------------
// Input-side EQ - not used
//-----------------------------------------------------------------
void eq_set(int on, char data[10], int preamp) {};

//-----------------------------------------------------------------
// Decode thread
//-----------------------------------------------------------------
#define ReadByte() gzgetc(InputFile)
DWORD WINAPI __stdcall DecodeThread(void *b)
{
	int SamplesTillNextRead=0;
	float WaitFactor,FractionalSamplesTillNextRead=0;

	if ((PlaybackRate==0) || (FileRate==0)) {
		WaitFactor=1.0;
	} else {
		WaitFactor=(float)FileRate/PlaybackRate;
	};

	while (! *((int *)b) ) {
		if (
			mod.outMod->CanWrite()	// Number of bytes I can write
			>=
			(int)(
				SampleBufferSize*2		// Size of buffer in bytes
				<<
				(mod.dsp_isactive()?1:0)	// x2 if DSP is active
			)
		) {
			int samplesinbuffer=SampleBufferSize/NCH;
			int x;

			int b1,b2;

			for (x=0;x<samplesinbuffer/2;++x) {
				// Read file, write stuff
				while (!SamplesTillNextRead) {
					switch (ReadByte()) {
					case 0x4f:	// GG stereo
						b1=ReadByte();
						if (PSGClock) SN76489_GGStereoWrite((char)b1);
						break;
					case 0x50:	// SN76489 write
						b1=ReadByte();
						if (PSGClock) SN76489_Write((char)b1);
						break;
					case 0x51:	// YM24123 write
						b1=ReadByte();
						b2=ReadByte();
						if (FMClock) OPLL_writeReg(opll,b1,b2);
						break;
					case 0x52:	// Reserved/unsupported
					case 0x53:	// chips
					case 0x54:	// (fall through)
					case 0x55:	//  |
					case 0x56:	//  |
					case 0x57:	//  |
					case 0x58:	//  |
					case 0x59:	//  |
					case 0x5a:	//  |
					case 0x5b:	//  |
					case 0x5c:	//  |
					case 0x5d:	//  |
					case 0x5e:	//  |
					case 0x5f:	// <-
						ReadByte();
						ReadByte();
						break;
					case 0x61:	// Wait n samples
						b1=ReadByte();
						b2=ReadByte();
						SamplesTillNextRead=b1 | (b2 << 8);
						break;
					case 0x62:	// Wait 1/60 s
						SamplesTillNextRead=735;
						break;
					case 0x63:	// Wait 1/50 s
						SamplesTillNextRead=882;
						break;
					case 0x66:	// End of data
						++NumLoopsDone;	// increment loop count
						// if we've done all the loops needed then finish
						if ((NumLoopsDone>NumLoops) || (LoopOffset==0)) while (1) {
							if (SeekToSampleNumber>-1) break;
							mod.outMod->CanWrite();	// hmm... does something :P
							if (!mod.outMod->IsPlaying()) {	// if the buffer has run out
								PostMessage(mod.hMainWindow,WM_WA_MPEG_EOF,0,0);	// tell WA it's EOF
								return 0;
							}
							Sleep(10);	// otherwise wait 10ms and try again
						};
						// Otherwise, loop the file
						gzseek(InputFile,LoopOffset,SEEK_SET);
						break;
					};	// end case

					FractionalSamplesTillNextRead+=SamplesTillNextRead*WaitFactor;
					SamplesTillNextRead=(int)FractionalSamplesTillNextRead;
					FractionalSamplesTillNextRead-=SamplesTillNextRead;

					if (SeekToSampleNumber>-1) {	// If a seek is wanted
						SeekToSampleNumber-=SamplesTillNextRead;	// Decrease the required seek by the current delay
						SamplesTillNextRead=0;
						if (SeekToSampleNumber<0) {
							SamplesTillNextRead=-SeekToSampleNumber;
							SeekToSampleNumber=-1;
						};
						continue;
					};
				};	// end while

				// Write sample
				#if NCH == 2
				// Stereo
				SampleBuffer[2*x]=0;
				SampleBuffer[2*x+1]=0;
				// PSG
				if (PSGClock) SN76489_WriteToBuffer(SampleBuffer,x);
				// YM2413
				if (FMClock) {
					if (PSGClock) {	// mix the two
						int FM=OPLL_calc(opll);
						SampleBuffer[2*x]  =(FM*FMPERCENT+SampleBuffer[2*x  ]*(100-FMPERCENT))/100;
						SampleBuffer[2*x+1]=(FM*FMPERCENT+SampleBuffer[2*x+1]*(100-FMPERCENT))/100;
					} else {	// just FM
						SampleBuffer[2*x]  =OPLL_calc(opll);
						SampleBuffer[2*x+1]=SampleBuffer[2*x];
					};
				};

				#else
				// Mono
				if (FMClock) SampleBuffer[x]=OPLL_calc(opll);
				#endif

				--SamplesTillNextRead;
			};
	
			x=mod.outMod->GetWrittenTime();	// returns time written in ms (used for synching up vis stuff)
			// Check these two later (not important)
			mod.SAAddPCMData ((char *)SampleBuffer,NCH,16,x);	// Add to vis
			mod.VSAAddPCMData((char *)SampleBuffer,NCH,16,x);	// Add to vis

			samplesinbuffer=mod.dsp_dosamples(
				(short *)SampleBuffer,	// Samples
				samplesinbuffer/NCH,	// No. of samples (?)
				16,						// Bits per sample
				NCH,					// Number of channels
				SAMPLERATE				// Sampling rate
			);
			mod.outMod->Write(
				(char *)SampleBuffer,	// Buffer
				samplesinbuffer*NCH*2	// Size of data in bytes
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
	0,	// hMainWindow
	0,	// hDllInstance
	"vgz;vgm\0VGM Audio File (*.vgm;*.vgz)\0",
	// Due to a Winamp bug (v2.76 and others) *.vgm files will not appear when "All files" is selected in
	// the Open box, but will when the filter is changed to this, and will be in the ext list.
	1,	// is_seekable
	1,	// uses output
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
	NULL,		// setinfo
	0 // out_mod
};

__declspec( dllexport ) In_Module * winampGetInModule2()
{
	return &mod;
}
