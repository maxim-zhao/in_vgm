#ifndef FILEINFO_H
#define FILEINFO_H

#include "gd3.h" // for NUMGD3TAGS
#include "vgm.h" // for VGM_CHIP_COUNT

void InfoInBrowser(const char *filename, int UseMB, int ForceOpen);
BOOL CALLBACK FileInfoDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam);
void getReplayGainData(const char *filename, float *gain, float *peak, int *noclip);

typedef unsigned short wchar;  // For Unicode strings

typedef struct {
	char *filename;
	wchar* tags[NUMGD3TAGS]; // see gd3.h
	int length; // in ms - uses loop count setting and rate setting
	int tracklength; // in ms - uses rate setting
	int looplength; // in ms - uses rate setting
	int filesize; // in bytes
	float bitrate; // in bps - uses rate setting
	int chips_used[VGM_CHIP_COUNT]; // chips used (based on clocks in header)
	char vgmversion[6]; // MM.mm\0 - preformatted for display
	wchar *replaygain_track; // Replay Gain stuff, stored as wchars since Winamp keeps asking for them
	wchar *replaygain_album;
	wchar *peak_track;
	wchar *peak_album;
	char *playlist;
	int trackNumber;
	wchar *albumartists_en;
	wchar *albumartists_jp;
} TFileTagInfo;

#endif
