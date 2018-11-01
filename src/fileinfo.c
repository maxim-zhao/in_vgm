#include <windows.h>
#include <stdio.h>
#include <zlib.h>
#include "fileinfo.h"
#include "vgm.h"
#include "gd3.h"
#include "common.h"
#include "platform_specific.h"
#include "winampsdk/Winamp/wa_ipc.h"
#include "winampsdk/Winamp/in2.h"
#include "winampsdk/GlobalConfig.h"
#include "resource.h"
#include "sqlite/sqlite3.h"
#include "LZMA/C/7zip/Archive/7z_C/7zCrc.h"
#include <math.h>
#include "PluginOptions.h"

extern In_Module mod;
// Ought to move these "globals" into a struct or something, just to make it less icky
extern char *TempHTMLFile;
extern char *CurrentURL;
extern char *CurrentURLFilename;
extern char *FilenameForInfoDlg;
extern int UseMB, TagsPreferJapanese, TagsFileType, TagsAddFMToYM2413, NumLoops, FileInfoJapanese, TagsTrim, TagsStandardiseSeparators, TagsStandardiseDates, TagsGuessTrackNumbers, TagsGuessAlbumArtists;
extern PluginOptions pluginOptions;

// forward declarations
int LoadInfo(const char *filename, TFileTagInfo *fileinfo);

//-----------------------------------------------------------------
// Returns a formatted string for the file's length
// Doesn't scale it using the playback rate
//-----------------------------------------------------------------
#define SMALL_INTRO_LIMIT 500 // ms for a small intro that I will ignore
char *FormatLength(char* buffer, TFileTagInfo* fileinfo)
{
	char temp[1024];

	PrintTime(buffer, fileinfo->tracklength / 1000.0 ); // total length
	if ( fileinfo->looplength > 0 )
	{
		// file has a loop
		if ( fileinfo->tracklength - fileinfo->looplength > SMALL_INTRO_LIMIT )
		{
			// intro > 500ms
			strcat( buffer, " (" );
			strcat( buffer, PrintTime( temp, ( fileinfo->tracklength - fileinfo->looplength ) / 1000.0 ) );
			strcat( buffer, " intro and " );
			strcat( buffer, PrintTime( temp, fileinfo->looplength / 1000.0 ) );
			strcat( buffer, " loop)" );
		}
		else
		{
			// small or no intro
			strcat( buffer, " (looped)" );
		}
	}
	else
	{
		strcat( buffer, " (no loop)" );
	}

	return buffer;
}

//-----------------------------------------------------------------
// Show GD3 info as HTML
//-----------------------------------------------------------------
void InfoInBrowser(const char *filename, int UseMB, int ForceOpen) {
	FILE *f;
	char *packname = NULL;
	int i, j;
	char *url;
	char tempstr[1024]; // used for inline printf()s
	const char What[10][32]={
		"Track Title", "",
		"Game Name", "",
		"System Name", "",
		"Track Author", "",
		"Game Release Date", "VGM Creator"
	};
	TFileTagInfo fileinfo;

	memset( &fileinfo, 0, sizeof(TFileTagInfo) );
	if ( LoadInfo( filename, &fileinfo ) )
	{
		f = fopen(TempHTMLFile,"w"); // open temp HTML file for writing
		if ( f )
		{
			// Put start text
			fprintf(f,
				#include "htmlbefore.txt"
			);

			// Add length row
			fprintf( f, "<tr><td class=what>Length</td><td colspan=2 class=is>%s</td></tr>", FormatLength( tempstr, &fileinfo ) );

			// Add file size row
			fprintf( f, "<tr><td class=what>Size</td><td class=is colspan=2>%d bytes (%.2f bps)</td></tr>",
				fileinfo.filesize,
				fileinfo.bitrate
			);


			// first 8 strings are alternating English/Japanese
			for ( i = GD3_TITLEEN; i <= GD3_AUTHORJP; ++i ) {
				if ( i % 2 - 1 ) // odd number: add row title
					fprintf( f, "<tr><td class=what>%s</td><td class=is>", What[i] );

				if ( fileinfo.tags[i] && *(fileinfo.tags[i]) )
				{
					// string is not blank
					for ( j = 0; *(fileinfo.tags[i] + j); j++ )
						// write each character as a NCR
						fprintf( f, "&#%d;", *(fileinfo.tags[i] + j) );
				}
				else
					// put a NBSP for blanks to make the cell fill out properly
					fprintf( f, "&nbsp;");

				if ( i % 2 ) // even number: end row
					fprintf( f, "</td></tr>" );
				else         // odd number: continue row
					fprintf( f, "</td><td class=is>" );

			}
			// Strings 8 to 10 have rows to themselves
			for ( i = GD3_DATE; i < GD3_NOTES; ++i ) {
				fprintf( f, "<tr><td class=what>%s</td><td colspan=2 class=is>", What[i] );
				if ( fileinfo.tags[i] && *(fileinfo.tags[i]) ) // string is not blank
					for ( j = 0; *(fileinfo.tags[i] + j); j++ )
						fprintf( f, "&#%d;", *(fileinfo.tags[i] + j) );
				else
					fprintf( f, "&nbsp;" );

				fprintf(f, "</td></tr>" );
			}
			if ( fileinfo.tags[GD3_NOTES] && *(fileinfo.tags[GD3_NOTES]) )
			{
				// Notes (10th string) need some processing of line breaks
				fprintf(f, "<tr><td class=what>Notes</td><td colspan=2 class=is>" );
				
				for ( j = 0; *(fileinfo.tags[GD3_NOTES] + j); ++j ) {
					if (*(fileinfo.tags[GD3_NOTES] + j) == '\n')
						fprintf( f, "<br>" );
					else
						fprintf( f, "&#%d;", *(fileinfo.tags[GD3_NOTES] + j) );
				}
				// End of strings
				fprintf( f, "</td></tr>" );
			}

			// Try for the pack readme
			{
				char *p;
				FILE *freadme;
				char *buffer;
				char *readme;
				BOOL success = FALSE;

				readme = malloc( strlen( filename ) + 16 ); // plenty of space in all weird cases

				if( readme )
				{
					p = strrchr( filename, '\\' ); // p points to the last \ in the filename
					if ( p )
					{
						while( !success ) {
							p = strstr( p, " - " ); // find first " - " after current position of p
							if ( p ) {
								strncpy( readme, filename, p - filename ); // copy filename up until the " - "
								strcpy( readme + ( p - filename ), ".txt" ); // terminate it with a ".txt\0"

								freadme = fopen( readme, "r" ); // try to open file
								if ( freadme ) {
									// save pack name for later use
									packname = malloc( strlen( readme ) );
									if ( packname ) {
										strncpy( packname, readme, strlen( readme ) - 4 );
										packname[ strlen( readme ) - 4 ] = '\0';
									}

									success = TRUE;
									fprintf( f, "<tr><td class=what>Pack readme</td><td class=is colspan=2><textarea id=readme cols=50 rows=20 readonly>");
									buffer = malloc( 1024 );
									while ( fgets( buffer, 1024, freadme ) )
										fputs( buffer, f );
									free( buffer );
									fprintf( f, "</textarea></td></tr>" );
									fclose( freadme );
								}
								p++; // make it not find this " - " next iteration
							} else break;
						}
					}
					free(readme);
				}
			}

			// Try for image
			if ( packname ) {
				FILE *fimage;
				char *imagefile = malloc( strlen( packname ) + 10 );
				BOOL foundit = FALSE;

				sprintf( imagefile, "%s.png", packname );
				if ( fimage = fopen( imagefile, "r" ) )
					foundit = TRUE;
				if ( !foundit )
				{
					sprintf( imagefile, "%s.gif", packname );
					if ( fimage = fopen( imagefile, "r" ) )
						foundit = TRUE;
				}
				if ( !foundit )
				{
					sprintf( imagefile, "%s.jpg", packname );
					if ( fimage = fopen( imagefile, "r" ) )
						foundit = TRUE;
				}
				if ( !foundit )
				{
					// try folder.jpg
					char *p;
					strcpy( imagefile, packname );
					p = strrchr( imagefile, '\\' );
					if ( p )
					{
						strcpy( p, "\\folder.jpg" );
						if ( fimage = fopen( imagefile, "r" ) )
							foundit = TRUE;
					}
				}

				if ( foundit )
				{
					fclose( fimage );
					fprintf(f,"<tr><td class=what>Pack image</td><td class=is colspan=2><img src=\"file:///%s\"></td></tr>", imagefile);
				}

				free( imagefile );
			}

			fputs("</table>",f);

			fputs(
				#include "htmlafter.txt"
				,f
			);
			fclose(f);

			if ( UseMB ) {
				url = malloc( strlen( TempHTMLFile ) + 9 );
				sprintf( url, "file:///%s", TempHTMLFile );
				if ( ForceOpen ) SendMessage(mod.hMainWindow,WM_USER,(WPARAM)NULL,IPC_MBOPEN);  // open minibrowser
				SendMessage(mod.hMainWindow,WM_USER,(WPARAM)url,IPC_MBOPEN);  // display file
				free(url);
			}
			else ShellExecute(mod.hMainWindow,NULL,TempHTMLFile,NULL,NULL,SW_SHOWNORMAL);
		}
	}
}

//-----------------------------------------------------------------
// DB helper stuff
//-----------------------------------------------------------------
extern char INIFileName[]; // DB filename is based on that

wchar *atow(const char *a)
{
	// never tested...
	int len = MultiByteToWideChar(CP_ACP, 0, a, -1, NULL, 0);
	wchar *result = malloc(len * sizeof(wchar));
	MultiByteToWideChar(CP_ACP, 0, a, -1, result, len);
	return result;
	// caller must free it
}

char *wchartoutf8(const wchar *w)
{
	int len;
	char *result;
	if (w == NULL)
		return NULL;
	len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL) + 1;
	result = malloc(len);
	WideCharToMultiByte(CP_UTF8, 0, w, -1, result, len, NULL, NULL);
	return result;
	// caller must free it
}

static int db_callback_fileinfo(TFileTagInfo* fi, int argc, char** argv, char** azColName)
{
	// need to add a handler here for every piece of metadata persisted in the DB
	int i;
	for(i=0; i<argc; i++){
		if (stricmp(azColName[i],"replaygain_album_gain")==0)
			fi->replaygain_album = atow(argv[i]);
		else if (stricmp(azColName[i],"replaygain_album_peak")==0)
			fi->peak_album = atow(argv[i]);
		else if (stricmp(azColName[i],"replaygain_track_gain")==0)
			fi->replaygain_track = atow(argv[i]);
		else if (stricmp(azColName[i],"replaygain_track_peak")==0)
			fi->peak_track = atow(argv[i]);
	}
	return 0;
}

sqlite3* db_open()
{
	// get filename
	char *db_filename_part = "in_vgm Replay Gain data.sqlite3";
	static char *fn = NULL; // remembered across invocations
	int status;
	sqlite3* db;

	if (fn == NULL)
	{
		// make the filename
		char *p;
		fn = malloc( strlen( INIFileName ) + strlen( db_filename_part ) );
		strcpy( fn, INIFileName );
		p = strrchr( fn, '\\' );
		if ( p == NULL )
		{
			free(fn);
			fn = NULL;
			return NULL;
		}
		strcpy( ++p, db_filename_part );
	}

	status = sqlite3_open( fn, &db );

	if (status != SQLITE_OK)
	{
		sqlite3_close( db );
		return NULL;
	}

	return db;
}

BOOL db_exec_readfileinfo(char *sql, TFileTagInfo *fi)
{
	int status;
	sqlite3* db = db_open();

	status = sqlite3_exec( db, sql, db_callback_fileinfo, fi, NULL ); // ignore error message, just look at status
	sqlite3_close( db );

	return (status == SQLITE_OK);
}

int crcstring(const char *s)
{
	static int initted = FALSE;
	if (!initted)
	{
		InitCrcTable();
		initted = TRUE;
	}
	return CrcCalculateDigest((void *)s, strlen(s));
}

//-----------------------------------------------------------------
// GD3 helper stuff
//-----------------------------------------------------------------

TFileTagInfo LastFileInfo = {NULL};



int tryM3Ufile(const char* m3ufilename, const char* filename)
{
	FILE *f;
	int linenumber;
	char buff[1024]; // buffer for reading lines from file

	f=fopen(m3ufilename,"r"); // try to open file
	if(f) {
		linenumber=1;
		// read through file, a line at a time
		while(fgets(buff,1024,f)) {    // fgets will read in all characters up to and including the line break
			if(strnicmp(buff,filename,strlen(filename))==0) {
				// found it!
				fclose(f);
				return linenumber;
			}
			if((strlen(buff)>3)&&(buff[0]!='#')) linenumber++; // count lines that are (probably) valid but not #EXTINF lines
		}
		// got to the end
		fclose(f); 
	}

	return 0;
}

//-----------------------------------------------------------------
// Get the track number for a given filename
// by trying to find its M3U playlist
//-----------------------------------------------------------------
int getTrackNumber(const char *filename, char **playlistFilename) {
	// assumes a filename "Streets of Rage II - Never Return Alive.vgz"
	// will have a playlist "Streets of Rage II.m3u"
	// returns track number, or 0 for not found
	char *playlist;  // our target string for the playlist file
	char *p;         // our current point in *filename, we copy section-by-section into *playlist
	char *fn;        // the filename part of the current file (no path)
	int number=0;

	p=strrchr(filename,'\\'); // p points to the last \ in the filename
	if(p)
	{
		// isolate filename
		fn=malloc(strlen(p));
		if(fn) 
		{
			strcpy(fn,p+1);

			playlist=malloc(strlen(filename) + MAX_PATH); // plenty of space in all weird cases, I hope
			if(playlist) {
				// first try: the first .m3u file in the folder - if there's only one, it'll be the right one
				WIN32_FIND_DATA FindFileData;
				HANDLE hFind;

				strncpy(playlist,filename,p-filename+1); // copy filename up until the "\"
				strcpy(playlist+(p-filename)+1,"*.m3u"); // add wildcard search
				hFind = FindFirstFile(playlist, &FindFileData);
				if (hFind != INVALID_HANDLE_VALUE) 
					strcpy(playlist+(p-filename)+1, FindFileData.cFileName); // replace wildcard search with found filename
					FindClose(hFind);
					number = tryM3Ufile(playlist, fn);
				while(number==0) {
					p=strstr(p," - "); // find first " - " after current position of p
					if(p) {
						strncpy(playlist,filename,p-filename); // copy filename up until the " - "
						strcpy(playlist+(p-filename),".m3u"); // terminate it with a ".m3u\0"

						number = tryM3Ufile(playlist, fn);

						p++; // make it not find this " - " next iteration
					} else break;
				}
				if (playlistFilename != NULL)
					*playlistFilename = playlist;
				else
					free(playlist);
			}
			free(fn);
		}
	}

	if (number == 0 && playlistFilename != NULL)
		*playlistFilename = NULL;

	return number;
}

// replaces "wrong" separators in a given string and generally tidies it up
void fixSeparators(wchar **str)
{
	int need = 0, bad = 0;
	wchar *src, *buffer;
	// chars to replace
	wchar *badseparators = L",/\\&\xff0c\xff0c\xff0f\xff3c";

	// count how many chars we need to add
	for (src = *str; *src!=0; src++)
	{
		if(wcschr(badseparators, *src) != NULL)
		{
			++bad;
			++need; // one extra char for the space after the semicolon (assuming a 1-2 replacement)
			if (*(src-1)==' ' || *(src-1)==0x3000)
			{
				// space before, decrement
				--need;
			}
			if (*(src+1)==' ' || *(src+1)==0x3000)
			{
				// space after, decrement
				--need;
			}
		}
	}

	// do nothing if no bad chars
	if (bad == 0)
		return;

	// allocate enough space
	buffer = malloc( (wcslen(*str) + need + 1)*sizeof(wchar) );
	if (buffer)
	{
		// copy across
		// replacing as we go
		wchar *dest = buffer;
		for (src = *str; *src!=0; src++)
		{
			if(wcschr(badseparators, *src) == NULL)
			{
				*dest++ = *src;
			}
			else
			{
				// do the magic
				if (*(dest-1)==L' ' || *(dest-1)==0x3000)
				{
					// space before, decrement dest pointer to overwrite
					--dest;
				}
				// write the semicolon!
				*dest++ = L';';
				*dest++ = L' ';
				if (*(src+1)==' ' || *(src+1)==0x3000)
				{
					// space after, increment src pointer to skip
					src++;
				}
			}
		}
		// null terminate
		*dest = 0;
		// replace string
		free( *str );
		*str = buffer;
	}
}

void fixLineBreaks(wchar **str)
{
	int numCRsNeeded;
	wchar *src, *dest, *buffer;

	// count how many CRs there are
	for( numCRsNeeded = 0, src = *str; *src != 0; src++ )
	{
		if (*src == L'\r' && *(src+1) == L'\n')
			src++; // skip if it's already \r\n
		else if (*src == L'\n' || *src == L'\r' )
			++numCRsNeeded;
	}

	if (numCRsNeeded > 0)
	{
		// allocate a new string
		buffer = malloc( (wcslen(*str) + numCRsNeeded + 1)*sizeof(wchar) );
		if ( buffer )
		{
			for( src = *str, dest = buffer; *src != 0; src++ )
			{
				if (*src == L'\r' && *(src+1) == L'\n')
				{
					src++; // it's already \r\n
					*dest++ = L'\r';
					*dest++ = L'\n';
				}
				else if (*src == L'\n' || *src == L'\r' )
				{
					*dest++ = L'\r';
					*dest++ = L'\n';
				}
				else
					*dest++ = *src;
			}
			*dest=0; // null terminate

			// replace string
			free( *str );
			*str = buffer;
		}
	}
}

// trim leading and trailing whitespace
// caller must free result
wchar *trimdup(wchar *str)
{
	wchar *result = NULL;
	int len;

	for (;iswspace(*str); ++str); // skip leading

	// skip trailing
	len = wcslen(str);
	while (iswspace(*(str+len-1)))
		len--;

	// copy
	result = malloc((len + 1) * sizeof(wchar));
	if (result)
	{
		wcsncpy(result, str, len);
		result[len] = 0;
	}

	return result;
}

// caller must free *en, *jp
void getFileAuthors(char *dir, char *fn, wchar **en, wchar **jp)
{

	// make the whole filename
	char *filename = malloc(strlen(dir)+strlen(fn)+1);
	*en = *jp = NULL;
	if (filename)
	{
		gzFile  *fh;
		int i;
		TVGMHeader VGMHeader;

		strcpy(filename, dir);
		strcat(filename, fn);

		fh=gzopen(filename,"rb");
		free(filename);
		if (fh==0)  // file not opened
			return;

		i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

		if ( (i < sizeof(VGMHeader))
		|| (VGMHeader.VGMIdent != VGMIDENT)
		|| ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER))) 
		{
			gzclose(fh);
			return;
		}

		// VGM header OK
		if (VGMHeader.GD3Offset>0) 
		{
			// GD3 tag exists
			TGD3Header  GD3Header;
			gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
			i=gzread(fh,&GD3Header,sizeof(GD3Header));
			if ((i==sizeof(GD3Header)) 
			 && (GD3Header.GD3Ident == GD3IDENT)
			 && (GD3Header.Version>=MINGD3VERSION)
			 && ((GD3Header.Version & REQUIREDGD3MAJORVER)==REQUIREDGD3MAJORVER))
			{
				// GD3 is acceptable version
				wchar *GD3data,*p;

				GD3data=malloc(GD3Header.Length);  // Allocate memory for string data
				if (GD3data)
				{
					gzread(fh,GD3data,GD3Header.Length);  // Read it in

					// seek to wanted entry
					p = GD3data;
					for (i = 0; i < GD3_AUTHOREN; ++i)
						p+=wcslen(p)+1;
					// copy tag
					*en = trimdup(p);
					p += wcslen(p)+1;
					*jp = trimdup(p);
					// clean and return
					fixSeparators(en);
					fixSeparators(jp);

					free(GD3data);
				}
			}
		}
		gzclose(fh);
	}
}

void mergeAuthors(wchar **existing, wchar *newstr)
{
	// tokenise, trim and merge
	if (newstr == NULL || wcslen(newstr) == 0)
		return;
	if (*existing == NULL)
	{
		*existing = wcsdup(newstr);
	}
	else
	{
		wchar *p = wcstok (newstr,L";");
		while (p)
		{
			wchar *dup = trimdup(p);
			if (wcsstr(*existing, dup) == NULL)
			{
				// reallocate string
				int extra = wcslen(dup) + 2; // 2 chars for the semicolon
				*existing = realloc(*existing, (wcslen(*existing) + extra + 1)*sizeof(wchar));
				if (existing)
				{
					wcscat(*existing, L"; ");
					wcscat(*existing, dup);
				}
			}
			free(dup);

			p = wcstok (NULL, L";");
		}
	}
}

void getAlbumArtists(TFileTagInfo *fileinfo)
{
	// go through the M3U
	// get the artist(s) for each track
	// make a nice list
	// format it to a string
	FILE* f;
	char buff[1024]; // for reading playlist lines
	char* dir;
	char* p;
	
	fileinfo->albumartists_en = fileinfo->albumartists_jp = NULL;
	if (fileinfo->playlist == NULL)
		return;

	// get the playlist folder
	dir = strdup(fileinfo->playlist);
	p = strrchr(dir, '\\');
	if (!p)
	{
		free(dir);
		return;
	}
	*++p=0; // terminate 

	f=fopen(fileinfo->playlist,"r"); // try to open file
	if(f) {
		// read through file, a line at a time
		while(fgets(buff,1024,f))    // fgets will read in all characters up to and including the line break
		{
			if((strlen(buff)>3)&&(buff[0]!='#'))
			{
				wchar *en, *jp;
				// clean line break from line
				p = buff + strlen(buff) - 1;
				while (*p == '\r' || *p == '\n')
                    *p--=0;
				// load info from file
				getFileAuthors(dir,buff, &en, &jp);
				// for each artist in the string, see if it's in the result yet
				mergeAuthors(&fileinfo->albumartists_en, en);
				mergeAuthors(&fileinfo->albumartists_jp, jp);
			}
		}
		// got to the end
		fclose(f); 
	}
	free(dir);
}

/*
BOOL hasTags(wchar* str)
{
	return 1; // debug
	// simplistic but should save a few cycles
	return wcschr(str,L'%') != NULL
	    || wcschr(str,L'$') != NULL;
}

void handleEmbeddedTags(const char *filename, wchar* ret, int retlen)
{
	if (hasTags(ret))
	{
		// parse the tag as a formatting string
		wchar* lastValue;
		wchar* wfilename;
		waFormatTitleExtended formatStruct;

		// back up value
		lastValue = wcsdup(ret);

		// convert the filename
		wfilename = atow(filename);

		// init struct
		formatStruct.filename = wfilename;
		formatStruct.out = ret;
		formatStruct.out_len = retlen;
		formatStruct.spec = lastValue;
		formatStruct.useExtendedInfo = 1; // use my file info function for tags
		formatStruct.TAGFUNC = NULL;
		formatStruct.TAGFREEFUNC = NULL;

		// reparse as if lastValue was a format string
		SendMessage(mod.hMainWindow, WM_USER, (WPARAM)&formatStruct, IPC_FORMAT_TITLE_EXTENDED);

		// check for errors and revert string if found
		// HACK
		if (wcsncmp(ret,L"[SYNTAX ERROR",13) == 0)
			wcscpy(ret,lastValue);

		// clean up
		free(lastValue);
		free(wfilename);
	}
}
*/

//-----------------------------------------------------------------
// Load the given file's GD3, DB, etc info into the given TFileTagInfo struct
//-----------------------------------------------------------------
int LoadInfo(const char* filename, TFileTagInfo* fileinfo)
{
	// load info from filename into fileinfo
	gzFile  *fh;
	TVGMHeader VGMHeader;
	int i;
	char* sql;
	char *oldplaylist;

	if (fileinfo->playlist)
		oldplaylist = strdup(fileinfo->playlist);
	else
		oldplaylist = NULL;

	// Blank info
	// Free pointed data first (not all, see below)
	free( fileinfo->filename );
	for (i=0;i<NUMGD3TAGS;++i) {
		free( fileinfo->tags[i] );
	}
	free( fileinfo->peak_album );
	free( fileinfo->peak_track );
	free( fileinfo->replaygain_album );
	free( fileinfo->replaygain_track );

	// Clear the struct - assuming zero is a nice field to initialise things to
	// preserve a few items though
	{
		wchar* en = fileinfo->albumartists_en;
		wchar* jp = fileinfo->albumartists_jp;
		memset( fileinfo, 0, sizeof(TFileTagInfo) );	
		fileinfo->albumartists_en = en;
		fileinfo->albumartists_jp = jp;
	}

	// Start to read things in
	fh=gzopen(filename,"rb");
	if (fh==0) {  // file not opened
		return 0;
	}

	// copy filename
	fileinfo->filename = malloc(strlen(filename)+1);
	strcpy(fileinfo->filename, filename);

	// get file size
	fileinfo->filesize = FileSize(filename);

	i=gzread(fh,&VGMHeader,sizeof(VGMHeader));

	if ( (i < sizeof(VGMHeader))
		|| (VGMHeader.VGMIdent != VGMIDENT)
		|| ((VGMHeader.Version<MINVERSION) || ((VGMHeader.Version & REQUIREDMAJORVER)!=REQUIREDMAJORVER))
	) {
		gzclose(fh);
		return 0;
	}

	// VGM header OK
	fileinfo->tracklength = (int) (
		(VGMHeader.TotalLength)
		/44.1
		*((pluginOptions.PlaybackRate&&VGMHeader.RecordingRate)?(double)VGMHeader.RecordingRate/pluginOptions.PlaybackRate:1)
	);

	fileinfo->looplength = (int) (
		(VGMHeader.LoopLength)
		/44.1
		*((pluginOptions.PlaybackRate&&VGMHeader.RecordingRate)?(double)VGMHeader.RecordingRate/pluginOptions.PlaybackRate:1)
	);

	if (pluginOptions.LoopForever)
	{
		fileinfo->length = -1000;
	}
	else
	{
		fileinfo->length = fileinfo->tracklength + NumLoops * fileinfo->looplength;
	}

	// bytes * 8 = bits
	// ms / 1000 = seconds
	// thus (bytes * 8 ) / (ms / 1000) = bytes * 8000 / ms
	fileinfo->bitrate = (float)fileinfo->filesize * 8000 / fileinfo->tracklength;

	fileinfo->chips_used[VGM_CHIP_SN76489] = VGMHeader.PSGClock > 0;
	fileinfo->chips_used[VGM_CHIP_YM2413] = VGMHeader.YM2413Clock > 0;
	if (VGMHeader.Version<0x0110)
	{
		// VGM <1.10 only has one clock setting for all FM chips
		fileinfo->chips_used[VGM_CHIP_YM2612] = fileinfo->chips_used[VGM_CHIP_YM2151] = fileinfo->chips_used[VGM_CHIP_YM2413];
	}
	else
	{
		fileinfo->chips_used[VGM_CHIP_YM2612] = VGMHeader.YM2612Clock > 0;
		fileinfo->chips_used[VGM_CHIP_YM2151] = VGMHeader.YM2151Clock > 0;
	}

	// vgmversion is a fixed-size buffer, big enough for all possible values
	_snprintf( fileinfo->vgmversion, 7, "%x.%02x", VGMHeader.Version >> 8, VGMHeader.Version & 0xff );
	
	if (VGMHeader.GD3Offset>0) {
		// GD3 tag exists
		TGD3Header  GD3Header;
		gzseek(fh,VGMHeader.GD3Offset+0x14,SEEK_SET);
		i=gzread(fh,&GD3Header,sizeof(GD3Header));
		if (
			(i==sizeof(GD3Header)) &&
			(GD3Header.GD3Ident == GD3IDENT) &&
			(GD3Header.Version>=MINGD3VERSION) &&
			((GD3Header.Version & REQUIREDGD3MAJORVER)==REQUIREDGD3MAJORVER)
		) {
			// GD3 is acceptable version
			wchar *GD3data,*p;

			GD3data=malloc(GD3Header.Length);  // Allocate memory for string data
			gzread(fh,GD3data,GD3Header.Length);  // Read it in

			// convert into struct
			p = GD3data;
			for (i=0; i<NUMGD3TAGS; ++i)
			{
				int len;
				if (TagsTrim)
				{
					// skip leading whitespace
					while (*p && iswspace(*p))
						p++;
				}
				// copy tag, exclude trailing whitespace
				len = wcslen(p);
				if (TagsTrim)
				{
					while (len > 0 && iswspace(p[len-1]))
						len--;
				}
				if (len > 0)
				{
					fileinfo->tags[i] = malloc( (len + 1) * sizeof(wchar) );
					if ( fileinfo->tags[i] )
					{
						wcsncpy( fileinfo->tags[i], p, len );
						fileinfo->tags[i][len]=0; // null terminate
					}

					// special cases?
					switch(i) {
					case GD3_GAMEEN:
					case GD3_GAMEJP: // album: append " (FM)" for FM soundtracks if wanted
						if ( TagsAddFMToYM2413 
							&& VGMHeader.YM2413Clock 
							&& (
							  // hack: not for 7MHzish clocks, for VGM < 1.10, because that's likely to be a YM2612 clock
							  (VGMHeader.Version >= 0x0110) || (VGMHeader.YM2413Clock < 7000000)
							)
						)
						{
							// re-allocate with a bit on the end and replace original
							wchar *buffer = malloc( (len + 6)*sizeof(wchar) );
							if ( buffer )
							{
								swprintf( buffer, L"%s (FM)", fileinfo->tags[i] );
								free( fileinfo->tags[i] );
								fileinfo->tags[i] = buffer;
							}
						}
						break;

					case GD3_NOTES: // notes: expand CRs to CRLF
						fixLineBreaks(&fileinfo->tags[i]);
						break;
					case GD3_AUTHOREN:
					case GD3_AUTHORJP: // Author: convert / & , to ; and fix up spacing
						if (TagsStandardiseSeparators) fixSeparators(&fileinfo->tags[i]);
						break;
					}
				}
				else
				{
					fileinfo->tags[i]=NULL; // not needed, but I'm being explicit
				}
				// move pointer on
				p += wcslen(p)+1;
			}

			free(GD3data);  // Free memory for string buffer
		}
	}
	else
	{
		// blank GD3 entries
		// note: the pointers were all zeroed above (memset()) but I'm doing this explicitly just in case
		for (i=0; i<NUMGD3TAGS; ++i)
		{
			fileinfo->tags[i] = NULL;
		}
	}
	gzclose(fh);

	// Replay Gain DB stuff
	fileinfo->peak_album=fileinfo->peak_track=fileinfo->replaygain_album=fileinfo->replaygain_track=NULL;
	sql = sqlite3_mprintf("select replaygain_track_gain, replaygain_track_peak, replaygain_album_gain, replaygain_album_peak from replaygain where filename_hash = %d", crcstring(filename));
	i = db_exec_readfileinfo( sql, fileinfo );
	sqlite3_free( sql );

	// Playlist stuff
	if (TagsGuessTrackNumbers)
		fileinfo->trackNumber = getTrackNumber(filename, &(fileinfo->playlist));

	if (TagsGuessAlbumArtists && (fileinfo->playlist != NULL) && ((oldplaylist == NULL) || (strcmp(fileinfo->playlist, oldplaylist) != 0)))
	{
		// new playlist, do playlist-intensive stuff
		free( fileinfo->albumartists_en );
		free( fileinfo->albumartists_jp );
		getAlbumArtists(fileinfo);
	}

	return 1;
	// done!
}

void chooseTag(wchar *en, wchar *jp, int MLJapanese, wchar *buf, int buflen)
{
	wchar *result = MLJapanese ? jp : en;
	if (result == NULL || wcslen(result) == 0)
		result = MLJapanese ? en : jp; // return the other if the desired is empty
	if (result)
		wcsncpy(buf, result, buflen);
}

//-----------------------------------------------------------------
// GetExtendedFileInfoW worker function
//-----------------------------------------------------------------
#define EXTENDEDFILEINFO_SUCCESS 1
#define EXTENDEDFILEINFO_UNKNOWNTAG 0
int GetExtendedFileInfo(const char *filename,const char *metadata,wchar *ret,int retlen) {
	int tagindex=GD3X_UNKNOWN;

	// can't do anything with no buffer, but return success anyway (should never happen?)
	if(!ret || !retlen)
		return EXTENDEDFILEINFO_SUCCESS;

	// default to a blank string
	*ret=0;

	// first, tags that don't need to look at the file itself
	if(stricmp(metadata,"type")==0)
	{
		_snwprintf(ret,retlen,L"%d",TagsFileType);
		return EXTENDEDFILEINFO_SUCCESS;
	}
	if (stricmp(metadata,"family")==0)
	{
		// depends on the extension
		char *p = strrchr(filename, '.');
		if (p == NULL)
		{
			// default
			_snwprintf(ret,retlen,L"Video Game Music");
		}
		else
		{
			if      (stricmp(p, ".vgm"  ) == 0) _snwprintf(ret,retlen,L"Video Game Music");
			else if (stricmp(p, ".vgz"  ) == 0) _snwprintf(ret,retlen,L"Video Game Music");
			else if (stricmp(p, ".vgm7z") == 0) _snwprintf(ret,retlen,L"Video Game Music archive");
		}
		return EXTENDEDFILEINFO_SUCCESS;
	}

	// next, check if we've already looked at this file
	if (!LastFileInfo.filename || strcmp(LastFileInfo.filename,filename)!=0)
	{
		// see if LoadInfo fails, or if it passed, if the filename is OK
		if (!LoadInfo(filename, &LastFileInfo) || !LastFileInfo.filename || strcmp(LastFileInfo.filename,filename)!=0)
			return EXTENDEDFILEINFO_UNKNOWNTAG;
	}

	// now start returning stuff from GD3 tags
	if(stricmp(metadata,"artist")==0)                     tagindex=GD3_AUTHOREN;   // author
	else if(stricmp(metadata,"length")==0)                tagindex=GD3X_LENGTH;    // length /ms
	else if(stricmp(metadata,"title")==0)                 tagindex=GD3_TITLEEN;    // track title
	else if(stricmp(metadata,"album")==0)                 tagindex=GD3_GAMEEN;     // game name
	else if(stricmp(metadata,"comment")==0)               tagindex=GD3_NOTES;      // comment
	else if(stricmp(metadata,"year")==0)                  tagindex=GD3_DATE;       // year
	else if(stricmp(metadata,"genre")==0)                 tagindex=GD3_SYSTEMEN;   // system
	else if(stricmp(metadata,"replaygain_track_gain")==0) tagindex=GD3X_TRACKGAIN;
	else if(stricmp(metadata,"replaygain_track_peak")==0) tagindex=GD3X_TRACKPEAK;
	else if(stricmp(metadata,"replaygain_album_gain")==0) tagindex=GD3X_ALBUMGAIN;
	else if(stricmp(metadata,"replaygain_album_peak")==0) tagindex=GD3X_ALBUMPEAK;
	else if(stricmp(metadata,"track")==0)                 tagindex=GD3X_TRACKNO;   // track number
	else if(stricmp(metadata,"albumartist")==0)           tagindex=GD3X_ALBUMARTIST;
//	else if(stricmp(metadata,"publisher")==0)             tagindex=GD3X_PUBLISHER;


	if(tagindex==GD3X_UNKNOWN)
	{
#ifdef _DEBUG
		// debug: get metadata types
		OutputDebugString("in_vgm: Unknown metadata type: \"");
		OutputDebugString(metadata);
		OutputDebugString("\"\n");
#endif
		return EXTENDEDFILEINFO_UNKNOWNTAG;
	}
		
	if((TagsPreferJapanese)&&(tagindex>=GD3_TITLEEN)&&(tagindex<=GD3_AUTHOREN)) tagindex++; // Increment index for Japanese

	switch (tagindex) {
		case GD3X_LENGTH:
			_snwprintf(ret,retlen,L"%d",LastFileInfo.length);
			break;
		case GD3_TITLEEN: case GD3_TITLEJP: case GD3_GAMEEN: case GD3_GAMEJP: case GD3_SYSTEMEN: case GD3_SYSTEMJP:
		case GD3_AUTHOREN: case GD3_AUTHORJP: case GD3_DATE: case GD3_RIPPER: case GD3_NOTES:
			// copy from LastFileInfo, but check for empty ones and use the alternative
			if (
				( (LastFileInfo.tags[tagindex] == NULL) || (wcslen(LastFileInfo.tags[tagindex]) == 0) )
				&&( tagindex <= GD3_AUTHORJP)
			) {
				// tweak index to point to alternative
				if(tagindex%2) tagindex--;
				else tagindex++;
			}

			if ( LastFileInfo.tags[tagindex] )
			{
				// special cases
				switch(tagindex)
				{
					case GD3_DATE: // year
						{
							// try to detect various date formats
							// Not yyyy/mm/dd:
							// nn/nn/yy    n/n/yy    nn/n/yy    n/nn/yy
							// nn/nn/yyyy  n/n/yyyy  nn/n/yyyy  n/nn/yyyy
							// Should be:
							// yyyy
							// yyyy/mm
							// yyyy/mm/dd
							wchar *lastslash = wcsrchr( LastFileInfo.tags[tagindex], L'/' );
							if (lastslash != NULL)
							{
								long year = wcstol(lastslash+1,NULL,10);
								if (year > 31) // looks like a year
								{
									if ( year < 100 ) // 2-digit, yuck
									{
										year += 1900;
										if (year < 1950) // not many sound chips around then, due to lack of transistors
											year += 100;
									}
									_snwprintf(ret,retlen,L"%d",year);
								}
							}
							if (!*ret)
							{
								// else, try the first bit
								wcsncpy( ret, LastFileInfo.tags[tagindex], 4 );
								ret[4]=0;
							}
						}
						break;
					default:
						// copy
						wcsncpy( ret, LastFileInfo.tags[tagindex], retlen );
						break;
				}
			}
			break;
		case GD3X_TRACKGAIN:
			if (LastFileInfo.replaygain_track)
				wcsncpy( ret, LastFileInfo.replaygain_track, retlen );
			break;
		case GD3X_TRACKPEAK:
			if (LastFileInfo.peak_track)
				wcsncpy( ret, LastFileInfo.peak_track, retlen );
			break;
		case GD3X_ALBUMGAIN:
			if (LastFileInfo.replaygain_album)
				wcsncpy( ret, LastFileInfo.replaygain_album, retlen );
			break;
		case GD3X_ALBUMPEAK:
			if (LastFileInfo.peak_album)
				wcsncpy( ret, LastFileInfo.peak_album, retlen );
			break;
		case GD3X_TRACKNO:
			if (LastFileInfo.trackNumber > 0)
				_snwprintf(ret, retlen, L"%d", LastFileInfo.trackNumber );
			break;
		case GD3X_ALBUMARTIST:
			chooseTag(LastFileInfo.albumartists_en, LastFileInfo.albumartists_jp, TagsPreferJapanese, ret, retlen);
			break;
		default:
			// do nothing
			return EXTENDEDFILEINFO_UNKNOWNTAG;
			break;
	}

//	handleEmbeddedTags(filename, ret, retlen);

	return EXTENDEDFILEINFO_SUCCESS;
}

//-----------------------------------------------------------------
// winampGetExtendedFileInfoW (Unicode) export for Winamp 5.25+
// Winamp will call this one when it expects a Unicode result
//-----------------------------------------------------------------
__declspec(dllexport) int winampGetExtendedFileInfoW(const wchar *wfilename,const char *metadata,wchar *ret,int retlen) {
	char *filename;
	int result;

	// hack: last.fm scrobbler
	if(retlen == 2048)
		retlen = 1024;

	// hack: convert filename to ASCII
	filename = getNormalisedFilename(wfilename);

	// get tag info
	result = GetExtendedFileInfo( filename, metadata, ret, retlen );

	// free ASCII filename
	free(filename);
	return result;
}

//-----------------------------------------------------------------
// winampGetExtendedFileInfo (non-Unicode) export for old Winamps
// Winamp will call this one when it expects an ASCII result
// eg. from a non-Unicode API call
//-----------------------------------------------------------------
__declspec(dllexport) int winampGetExtendedFileInfo(const char *filename,const char *metadata,char *ret,int retlen) {
	wchar *wret;
	int result;

	// allocate wchar result buffer
	wret = malloc((retlen+1) * sizeof(wchar));

	// get tag info
	result = GetExtendedFileInfo( filename, metadata, wret, retlen );

	// convert result to ASCII/MBCS
	WideCharToMultiByte(CP_ACP,WC_COMPOSITECHECK,wret,-1,ret,retlen,NULL,NULL);

	// free local buffer
	free(wret);

	return result;
}

// store RG data in here (and anything else I ever persist in the DB)
struct
{
	wchar *filename;
	char *trackgain;
	char *trackpeak;
	char *albumgain;
	char *albumpeak;
} extendedFileInfoData;

void clearExtendedFileInfoData()
{
	free(extendedFileInfoData.albumgain);
	free(extendedFileInfoData.albumpeak);
	free(extendedFileInfoData.trackgain);
	free(extendedFileInfoData.trackpeak);
	free(extendedFileInfoData.filename);
	memset(&extendedFileInfoData, 0, sizeof(extendedFileInfoData));
}

__declspec(dllexport) int winampSetExtendedFileInfoW(const wchar *filename,const char *metadata,wchar *value) {
	if ( !extendedFileInfoData.filename 
		|| (wcscmp(filename, extendedFileInfoData.filename) != 0) 
	)
	{
		// clear the struct and set the new filename
		// (discard existing data - the caller was supposed to call Write...
		clearExtendedFileInfoData();
		extendedFileInfoData.filename = wcsdup(filename);
	}

	// save the (wanted) data for processing later
	if (stricmp(metadata,"replaygain_track_gain")==0)
	{
		extendedFileInfoData.trackgain = wchartoutf8(value);
		return EXTENDEDFILEINFO_SUCCESS;
	}
	if (stricmp(metadata,"replaygain_track_peak")==0)
	{
		extendedFileInfoData.trackpeak = wchartoutf8(value);
		return EXTENDEDFILEINFO_SUCCESS;
	}
	if (stricmp(metadata,"replaygain_album_gain")==0)
	{
		extendedFileInfoData.albumgain = wchartoutf8(value);
		return EXTENDEDFILEINFO_SUCCESS;
	}
	if (stricmp(metadata,"replaygain_album_peak")==0) 
	{
		extendedFileInfoData.albumpeak = wchartoutf8(value);
		return EXTENDEDFILEINFO_SUCCESS;
	}

	// unhandled case
	return EXTENDEDFILEINFO_UNKNOWNTAG;
}

__declspec(dllexport) int winampSetExtendedFileInfo(const char *filename,const char *metadata,char *value) {
	// ASCII version, rarely used
	// attempt to pass through to the unicode handler
	wchar *wfilename = atow(filename),
		*wvalue = atow(value);

	int result = winampSetExtendedFileInfoW(wfilename, metadata, wvalue);

	free(wfilename);
	free(wvalue);

	return result;
}

__declspec( dllexport ) int winampWriteExtendedFileInfo()
{
	int crc, status=0;
	sqlite3 *db;
	char *buffer;

	db = db_open();
	if (!db)
		return EXTENDEDFILEINFO_UNKNOWNTAG;

	status += sqlite3_exec(db,
		"PRAGMA page_size=4096; PRAGMA synchronous = OFF; PRAGMA temp_store = MEMORY; create table if not exists replaygain ("
			"filename_hash int primary key,"
			"replaygain_track_gain text,"
			"replaygain_track_peak text,"
			"replaygain_album_gain text,"
			"replaygain_album_peak text" 
		");", NULL, NULL, NULL
	);

	buffer = getNormalisedFilename(extendedFileInfoData.filename);
	crc = crcstring(buffer);
	free(buffer);

	// output SQL for all the data
	buffer = sqlite3_mprintf(
		"insert or replace into replaygain (filename_hash,replaygain_track_gain,replaygain_track_peak,replaygain_album_gain,replaygain_album_peak)"
		" values (%d,'%s','%s','%s','%s');",
		crc, 
		extendedFileInfoData.trackgain,
		extendedFileInfoData.trackpeak,
		extendedFileInfoData.albumgain,
		extendedFileInfoData.albumpeak);
	status += sqlite3_exec(db, buffer, NULL, NULL, NULL);
	sqlite3_free(buffer);

	status+=sqlite3_close(db);

	if (!status)
		return EXTENDEDFILEINFO_UNKNOWNTAG;

	return EXTENDEDFILEINFO_SUCCESS;
}

//-----------------------------------------------------------------
// File info dialogue callback function
// Have to store file info outside because it's called multiple times
//-----------------------------------------------------------------
TFileTagInfo InfoDialogFileInfo;

BOOL CALLBACK FileInfoDialogProc(HWND DlgWin,UINT wMessage,WPARAM wParam,LPARAM lParam) {
	const int DlgFields[NUMGD3TAGS] = {
		ebTitle, ebTitle,
		ebName, ebName,
		ebSystem, ebSystem,
		ebAuthor, ebAuthor,
		ebDate,
		ebCreator,
		ebNotes
	};
	switch (wMessage) {  // process messages
		case WM_INITDIALOG:  {
			//----------------------------------------------------------------
			// Initialise dialogue
			//----------------------------------------------------------------

			int i;
			char tempstr[1024];

			if ( IsURL( FilenameForInfoDlg ) ) {
				// Filename is a URL
				if (
					CurrentURLFilename && CurrentURL && (
						( strcmp( FilenameForInfoDlg, CurrentURLFilename ) == 0) ||
						( strcmp( FilenameForInfoDlg, CurrentURL         ) == 0)
					)
				)
				{
					// If it's the current file, look at the temp file
					// but display the URL
					SetDlgItemText( DlgWin, ebFilename, CurrentURL );
					FilenameForInfoDlg = CurrentURLFilename;
				}
				else
				{
					// If it's not the current file, no info
					SetDlgItemText( DlgWin, ebFilename, FilenameForInfoDlg );
					SetDlgItemText( DlgWin, ebNotes, "Information unavailable for this URL" );
					DISABLECONTROL( DlgWin, btnInfoInBrowser );
					return TRUE;
				}
			} else {
				// Filename is not a URL
				SetDlgItemText( DlgWin, ebFilename, FilenameForInfoDlg );
			}

			// Load metadata
			LoadInfo( FilenameForInfoDlg, &InfoDialogFileInfo );

			// VGM version
			SetDlgItemText( DlgWin, ebVersion, InfoDialogFileInfo.vgmversion );

			// Chips used
			SETCHECKBOX( DlgWin, rbSN76489, InfoDialogFileInfo.chips_used[VGM_CHIP_SN76489] );
			SETCHECKBOX( DlgWin, rbYM2413,  InfoDialogFileInfo.chips_used[VGM_CHIP_YM2413] );
			SETCHECKBOX( DlgWin, rbYM2612,  InfoDialogFileInfo.chips_used[VGM_CHIP_YM2612] );
			SETCHECKBOX( DlgWin, rbYM2151,  InfoDialogFileInfo.chips_used[VGM_CHIP_YM2151] );
			if ( InfoDialogFileInfo.chips_used[VGM_CHIP_YM2413] == InfoDialogFileInfo.chips_used[VGM_CHIP_YM2612]
				&& InfoDialogFileInfo.chips_used[VGM_CHIP_YM2413] == InfoDialogFileInfo.chips_used[VGM_CHIP_YM2151] )
			{
				// looks like an old VGM file, so hide those extra boxes and rename the other one
				SetDlgItemText( DlgWin, rbYM2413, "FM chips" );
				HIDECONTROL( DlgWin, rbYM2612 );
				HIDECONTROL( DlgWin, rbYM2151 );
			}
			
			// Size and bitrate
			sprintf(
				tempstr,
				"%d bytes (%.2f bps)",
				InfoDialogFileInfo.filesize,
				InfoDialogFileInfo.bitrate
			);
			SetDlgItemText(DlgWin,ebSize,tempstr);

			// Length
			SetDlgItemText( DlgWin, ebLength, FormatLength( tempstr, &InfoDialogFileInfo ) );

			// Replay Gain
			{
				char *trackgain, *albumgain;
				trackgain = wchartoutf8(InfoDialogFileInfo.replaygain_track);
				albumgain = wchartoutf8(InfoDialogFileInfo.replaygain_album);
				if (trackgain && strlen(trackgain) > 0)
				{
					if (albumgain && strlen(albumgain) > 0)
						sprintf(
							tempstr,
							"%s (Album: %s)",
							trackgain, albumgain
						);
					else 
						sprintf(
							tempstr,
							"%s",
							trackgain
						);
				}
				else
					strcpy(tempstr,"Unknown");
				free(trackgain);
				free(albumgain);
				SetDlgItemText( DlgWin, ebReplayGain, tempstr );
			}

			// Set non-language-changing fields here
			for ( i = GD3_DATE; i < NUMGD3TAGS; ++i ) {
				if ( !SetDlgItemTextW( DlgWin, DlgFields[i], InfoDialogFileInfo.tags[i] ) )
				{
					// Widechar text setting failed, try WC2MB
					char MBCSstring[1024 * 2] = "";
					WideCharToMultiByte( CP_ACP, WC_COMPOSITECHECK, InfoDialogFileInfo.tags[i], -1, MBCSstring, 1024 * 2, NULL, NULL );
					SetDlgItemTextA( DlgWin, DlgFields[i], MBCSstring );
				}
			}

			// trigger English or Japanese for other fields
			SETCHECKBOX( DlgWin, rbEnglish + FileInfoJapanese, TRUE );
			PostMessage( DlgWin, WM_COMMAND, rbEnglish + FileInfoJapanese, 0);

			return TRUE;
		}
		case WM_COMMAND:
			//----------------------------------------------------------------
			// Control messages
			//----------------------------------------------------------------
			switch (LOWORD(wParam)) {
				case IDOK:
					//----------------------------------------------------------------
					// OK button
					//----------------------------------------------------------------
					FileInfoJapanese=IsDlgButtonChecked(DlgWin,rbJapanese);
					EndDialog(DlgWin,0);  // return 0 = OK
					return TRUE;
				case IDCANCEL:
					//----------------------------------------------------------------
					// [X], Esc, Alt+F4
					//----------------------------------------------------------------
					EndDialog(DlgWin,1);  // return 1 = Cancel, stops further dialogues being opened
					return TRUE;
				case btnConfigure:
					//----------------------------------------------------------------
					// Configure Plugin
					//----------------------------------------------------------------
					mod.Config(DlgWin);
					break;
				case rbEnglish:
				case rbJapanese:
					//----------------------------------------------------------------
					// English/Japanese
					//----------------------------------------------------------------
					{
						int i;
						// offset by 1 for Japanese
						const int offset = (LOWORD(wParam) == rbJapanese ? 1 : 0);
						// fill in fields
						for ( i = GD3_TITLEEN; i <= GD3_AUTHOREN; i += 2 ) {
							if ( !SetDlgItemTextW( DlgWin, DlgFields[i + offset], InfoDialogFileInfo.tags[i + offset] ) )
							{
								// ASCII conversion fallback
								char MBCSstring[1024 * 2] = "";
								WideCharToMultiByte( CP_ACP, WC_COMPOSITECHECK, InfoDialogFileInfo.tags[i + offset], -1, MBCSstring, 1024 * 2, NULL, NULL );
								SetDlgItemText( DlgWin, DlgFields[i + offset], MBCSstring );
							}
						}
					}
					break;
				case btnInfoInBrowser:
					//----------------------------------------------------------------
					// Info In Browser
					//----------------------------------------------------------------
					InfoInBrowser( FilenameForInfoDlg, UseMB, TRUE );
					break;
				default:
					break;
				}
			break;
		}
		return FALSE ;    // return FALSE to signify message not processed
}

struct rg_settings {
	float album_gain;
	float album_peak;
	float track_gain;
	float track_peak;
};


static int db_callback_RG(struct rg_settings *rgs, int argc, char **argv, char **azColName)
{
	// need to add a handler here for every piece of metadata persisted in the DB
	int i;
	for(i=0; i<argc; i++){
		if (stricmp(azColName[i],"replaygain_album_gain")==0)
			rgs->album_gain = (float)atof(argv[i]);
		else if (stricmp(azColName[i],"replaygain_album_peak")==0)
			rgs->album_peak = (float)atof(argv[i]);
		else if (stricmp(azColName[i],"replaygain_track_gain")==0)
			rgs->track_gain = (float)atof(argv[i]);
		else if (stricmp(azColName[i],"replaygain_track_peak")==0)
			rgs->track_peak = (float)atof(argv[i]);
	}
	return 0;
}


void getReplayGainData(const char *filename, float *gain, float *peak, int *noclip)
{
	// attempt to get data
	sqlite3* db;
	float default_gain;
	int rg_mode;
	int result = 0;
	char *sql;
	struct rg_settings temp_settings;

	if (!ReplayGainEnabled())
	{
		*gain = *peak = 1.0;
		*noclip = 0;
		return;
	}

	default_gain = GetPreamp();
	rg_mode = ReplayGainMode();

	db = db_open();
	
	if (!db)
	{
		// no DB, return defaults
		*gain = default_gain;
		*peak = 1000; // whatever
		if (rg_mode == RG_MODE_GAIN_NOCLIP || rg_mode == RG_MODE_NOCLIP)
			*noclip = 1;
		else
			*noclip = 0;
		return;
	}

	// get values from the DB
	sql = sqlite3_mprintf("select replaygain_track_gain, replaygain_track_peak, replaygain_album_gain, replaygain_album_peak from replaygain where filename_hash = %d",  crcstring(filename) );
	memset(&temp_settings, 0, sizeof(temp_settings));
	result += sqlite3_exec(db, sql, db_callback_RG, &temp_settings, NULL);
	sqlite3_free(sql);
	sqlite3_close(db);

	if(temp_settings.album_peak == 0.0 && temp_settings.album_gain == 0.0 && temp_settings.track_peak == 0.0 && temp_settings.track_gain == 0)
	{
		// no data on hand, no RG please
		*gain = 0.0;
		*peak = 0.0;
		*noclip = 0;
		return;
	}

	if (!ReplayGainPreferredOnly())
	{
		// Preferred only is OFF, so overwrite any empty ones with the other side
		// Note: peak = 0.0 and gain = 0.0 is an invalid combination used to signify no data
		if (temp_settings.album_peak == 0.0 && temp_settings.album_gain == 0.0)
		{
			temp_settings.album_peak = temp_settings.track_peak;
			temp_settings.album_gain = temp_settings.track_gain;
		}
		else if (temp_settings.track_gain == 0.0 && temp_settings.track_peak == 0.0)
		{
			temp_settings.track_peak = temp_settings.album_peak;
			temp_settings.track_gain = temp_settings.album_gain;
		}
	}

	if (ReplayGainAlbumMode())
	{
		*gain = temp_settings.album_gain;
		*peak = temp_settings.album_peak;
	}
	else
	{
		*gain = temp_settings.track_gain;
		*peak = temp_settings.track_peak;
	}

	// look at the mode
	switch(rg_mode)
	{
	case RG_MODE_GAIN:
		// nothing to change here
		break;
	case RG_MODE_GAIN_NOCLIP:
		// enable no clipping and apply gain
		*noclip = 1;
		break;
	case RG_MODE_NOCLIP:
		// disable gain, enable no clipping
		*gain = 0.0;
		*noclip = 1;
		break;
	case RG_MODE_NORMALIZE:
		// need to calculate the gain that'll scale peak to 1.0
		// scale = pow(10.0, gain/20) = 1/peak
		// so gain = -20 * log10(peak)
		*gain = (float)(-20.0 * log10(*peak));
		break;
	default:
		// should not be possible to get here
		*noclip = 0;
		break;
	}
}
