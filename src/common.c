/*
 * This is a nasty mess!
 * I need to clean it up
 */

#include <stdlib.h>   // malloc
#include <stdio.h>   // sprintf
#include <string.h>   // strstr
#include <math.h>     // log
#include <zlib.h>     // zlib stuff
#include "common.h"
#include "platform_specific.h"
#include "vgm.h"      // VGM header
#include "gd3.h"      // GD3 header

//-----------------------------------------------------------------
// Check if string is a URL
//-----------------------------------------------------------------
int IsURL(const char *url) {
  if (
    (strstr(url,"http://")==url) ||
    (strstr(url,"ftp://" )==url) ||
    (strstr(url,"www."   )==url)
  ) return 1;
  return 0;
}

//-----------------------------------------------------------------
// Convert a time in seconds to a nice format
//-----------------------------------------------------------------
char* PrintTime(char *s,double timeinseconds) {
	// Convert to the various timescales
	double secs=timeinseconds;
	int mins   =(int)timeinseconds / 60;
	int hours  =(int)timeinseconds / ( 60 * 60 );
	int days   =(int)timeinseconds / ( 60 * 60 * 24 );

	// Subtract the next largest's part to get the remainder (in smallest to largest order)
	secs  -= mins * 60;
	mins  -= hours * 60;
	hours -= days * 24;

	if(days)
		// very unlikely
		sprintf(s,"%dd+%d:%02d:%04.1f",days,hours,mins,secs); // eg. 1d+2:34:56.7
	else if(hours)
		// unlikely
		sprintf(s,    "%d:%02d:%04.1f",hours,mins,secs);      // eg.    1:23:45.6
	else if(mins)
		sprintf(s,         "%d:%04.1f",mins,secs);            // eg.       1:23.4
	else
		sprintf(s,              "%.1fs",secs);                // eg.          1.2s

	return s;
}

//-----------------------------------------------------------------
// Read and check a TVGMHeader
// returns NULL on error
// caller must free
//-----------------------------------------------------------------
TVGMHeader *ReadVGMHeader( gzFile *fh, int needsGD3 )
{
	int i;
	TVGMHeader *VGMHeader = malloc(sizeof(TVGMHeader));

	if ( !VGMHeader )
		return NULL; // failed to allocate it

	i=gzread(fh,VGMHeader,sizeof(TVGMHeader));

	if (
		( i < sizeof(TVGMHeader) ) ||                               // file too short/error reading
		( VGMHeader->VGMIdent != VGMIDENT ) ||                             // no marker
		( VGMHeader->Version < MINVERSION ) ||                             // below min ver
		( (VGMHeader->Version & REQUIREDMAJORVER) != REQUIREDMAJORVER ) || // != required major ver
		( needsGD3 && !VGMHeader->GD3Offset )                              // no GD3
		)
	{
		free(VGMHeader);
		return NULL;
	}
	return VGMHeader;
}

//-----------------------------------------------------------------
// Read and check a TGD3Header
// returns NULL on error
// caller must free
//-----------------------------------------------------------------
TGD3Header *ReadGD3Header( gzFile *fh )
{
	int i;
	TGD3Header *GD3Header = malloc(sizeof(TGD3Header));

	if ( !GD3Header )
		return NULL; // failed to allocate it

	i=gzread(fh,GD3Header,sizeof(TGD3Header));

	if (
		( i < sizeof(TGD3Header) ) ||                                    // file too short/error reading
		( GD3Header->GD3Ident != GD3IDENT ) ||                                  // no marker
		( GD3Header->Version < MINGD3VERSION) ||                                // below min ver
		( ( GD3Header->Version & REQUIREDGD3MAJORVER ) != REQUIREDGD3MAJORVER ) // != required major ver
		) 
	{
		free(GD3Header);
		return NULL;
	}
	return GD3Header;
}

int FileSize(const char *filename)
{
	int filesize;
	FILE* f = fopen(filename, "rb");
	if (f)
	{
		fseek(f, 0, SEEK_END);
		filesize = ftell(f);
		fclose(f);
	}
	return filesize;
}

char *getNormalisedFilename(const wchar *wfn)
{
	// First try just converting the string
	// If there are unrepresentable characters then use the short path name which ought to be more compatible
	char* result;
	int resultlen;
	BOOL usedSubstitute;

	// try a straight conversion first
	resultlen = WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, wfn, -1, NULL, 0, NULL, &usedSubstitute) + 1;
	if (!usedSubstitute)
	{
		result = malloc(resultlen);
		WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, wfn, -1, result, resultlen, NULL, NULL);
	}
	else
	{
		wchar* wshort;
		int wshortlen;

		// get wide short
		wshortlen = GetShortPathNameW(wfn, NULL, 0) + 1;
		wshort = malloc(wshortlen * sizeof(wchar));
		GetShortPathNameW(wfn, wshort, wshortlen);

		// convert to ACP
		resultlen = WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, wshort, -1, NULL, 0, NULL, NULL) + 1;
		result = malloc(resultlen);
		WideCharToMultiByte(CP_ACP, WC_COMPOSITECHECK, wshort, -1, result, resultlen, NULL, NULL);

		// clean up
		free(wshort);
	}

	return result;
	// caller must free
}

void add_hex_encoded(char** ppDest, char c)
{
	static char hex[] = "0123456789abcdef";

	*(*ppDest)++ = '%';
	*(*ppDest)++ = hex[c >> 4];
	*(*ppDest)++ = hex[c & 15];
}

//-----------------------------------------------------------------
// Returns a new string that is the urlencoded version of the one
// given, eg. http://www.site.com/file%20name.vgm
// caller must free
// TODO: test how much works...
//-----------------------------------------------------------------
char *URLEncode(char *src) {
	// We split the URL into parts:
	// 1. Protocol
	// 2. Server
	// 3. Path
	// 4. Query string
	// We encode the path components and the entirety of the query string.
	char* dest = malloc(strlen(src)*3+1); // maximum space for string
	char* srcptr;
	char* destptr;
	BOOL inQuery = FALSE;

	// start by copying it all over
	strcpy(dest, src);
	// Copy the protocol and server verbatim
	srcptr = strstr(src, "://");
	if (srcptr != NULL && strlen(srcptr) > 4)
	{
		srcptr = strchr(srcptr + 3, '/');
	}
	
	if (srcptr == NULL)
	{
		// doesn't look like a URL; just return the verbatim copy (which will likely fail)
		return dest;
	}
	
	// OK, time to start encoding stuff
	// start by looking at the next char
	++srcptr; 
	// get destptr in sync
	destptr = dest + (srcptr - src);
	// start copying
	for (;*srcptr != 0; ++srcptr)
	{
		if (!inQuery)
		{
			// We only need to escape spaces (apparently)
			if (*srcptr == ' ')
			{
				add_hex_encoded(&destptr, *srcptr);
			}
			else
			{
				*destptr++ = *srcptr;

				if (*srcptr == '?')
				{
					inQuery = TRUE;
				}
			}
		}
		else
		{
			// in a query, we escape everything but a-zA-Z0-9.-_
			// and we change spaces to +
			if (isalnum(*srcptr) || *srcptr == '.' || *srcptr == '-' || *srcptr == '_')
			{
				*destptr++ = *srcptr;
			}
			else if (*srcptr == ' ')
			{
				*destptr++ = '+';
			}
			else
			{
				add_hex_encoded(&destptr, *srcptr);
			}
		}
	}
	// null terminate
	*destptr = '\0';
	return dest;
	// caller must free
}
