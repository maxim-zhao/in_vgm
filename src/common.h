#ifndef COMMON_H
#define COMMON_H

#include "vgm.h"      // VGM header
#include "gd3.h"

typedef unsigned short wchar;  // For Unicode strings

#define MINGD3VERSION 0x100
#define REQUIREDGD3MAJORVER 0x100
#define MINVERSION 0x100
#define REQUIREDMAJORVER 0x100

char* PrintTime(char *s,double timeinseconds);
int IsURL(const char *url);

TVGMHeader* ReadVGMHeader(gzFile *fh, int needsGD3);
TGD3Header* ReadGD3Header(gzFile *fh);

int FileSize(const char *filename);

char *getNormalisedFilename(const wchar *wfn); // caller must free
char *URLEncode(char *s);
#endif