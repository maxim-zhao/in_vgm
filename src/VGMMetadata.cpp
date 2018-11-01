#include "VGMMetadata.h"
#include "PlatformDependent.h"
#include <zlib.h>
#include "VGMHeader.h"
#include "GD3.h"
#include <wchar.h>
#include <sstream>
#include <fstream>
#include "libs/sqlite/sqlite3.h"
extern "C"
{
#include "libs/LZMA/C/7zip/Archive/7z_C/7zCrc.h"
}
#include <Windows.h>

/*
#include "VGM.h"
#include "GD3.h"
#include "VGMUtils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
*/

/*
static char* replayGainDBFilename = NULL;

// We load all the metadata from the given filename on the first request so it
// is cached for alter requests. This makes things nice and fast.

static struct
{
    int trimWhitespace;
	int appendFMForYM2413;
	int preferJapanese;
	int playbackRate;
	int loopForever;
	int numLoops;
} Options = {0};

static struct
{
	char* filename;
	int numBytes;
	unsigned long trackLength44100ths;
	unsigned long loopLength44100ths;
	int vgmVersion;
	wchar_t vgmVersionAsText[7]; // "XXX.XX\0"
	wchar_t* GD3Tags[NUMGD3TAGS];
	int recordingRate;
} lastFileInfo = {0};

// TODO: this is in more than one place
// sort it out somehow...
static int FileSize(const char* filename)
{
	int bytesRead = -1;
	FILE* f = fopen(filename, "rb");
	if (f)
	{
		fseek(f, 0, SEEK_END);
		bytesRead = ftell(f);
		fclose(f);
	}
	return bytesRead;
}

void CleanUpTag(int tagNumber, VGMHeader* header) 
{
	if (lastFileInfo.GD3Tags[tagNumber] == NULL || lastFileInfo.GD3Tags[tagNumber][0] == '\0')
	{
		// nothing to do for blank tags
		return;
	}
	// Do various cleanups
	switch (tagNumber)
	{
	case GD3_SYSTEMEN:
	case GD3_SYSTEMJP:
		if (Options.appendFMForYM2413)
		{
			// Append "(FM)" to YM2413
			// Pre-1.10 we don't know if the clock number is for
			// another chip; assume <7MHz means it is YM2413
			if (header->YM2413Clock
				&& (header->Version >= 0x0110 || header->YM2413Clock < 7000000))
			{
				// need to reallocate it
				wchar_t* newTag = malloc((wcslen(lastFileInfo.GD3Tags[tagNumber]) + 6) * sizeof(wchar_t));
				if (newTag)
				{
					wcscpy(newTag, lastFileInfo.GD3Tags[tagNumber]);
					wcscat(newTag, L" (FM)");
					free(lastFileInfo.GD3Tags[tagNumber]);
					lastFileInfo.GD3Tags[tagNumber] = newTag;
				}
			}
		}
		break;
	}
	// TODO: others
}

// trim leading and trailing whitespace
// caller must free result
wchar_t* trimdup(wchar_t* str)
{
	wchar_t *result = NULL;
	int len;

	while (iswspace(*str))
	{
		++str; // skip leading
	}

	// skip trailing
	len = wcslen(str);
	while (iswspace(*(str+len-1)))
	{
		len--;
	}

	// copy
	result = malloc((len + 1) * sizeof(wchar_t));
	if (result)
	{
		wcsncpy(result, str, len);
		result[len] = 0;
	}

	return result;
}

int BCDToInt(int BCD)
{
	int units = BCD & 0x0f;
	int tens = (BCD >> 4) & 0x0f;
	if (units > 9)
	{
		units = 9;
	}
	if (tens > 9)
	{
		tens = 9;
	}
	return tens*10 + units;
}

// returns 0 on failure, non-zero otherwise
int LoadAllFileInfo(const char* filename)
{
	gzFile file;
	VGMHeader* header = NULL;
	int success = 0;

	// return immediately if we already loaded this file
	if (lastFileInfo.filename && strcmp(filename, lastFileInfo.filename) == 0)
	{
		return 1;
	}

	FreeLastFileInfo();
	lastFileInfo.filename = strdup(filename);

	// Load the file
	file = gzopen(filename, "rb");
	if (file)
	{
		// get the file bytesRead
		lastFileInfo.numBytes = FileSize(filename);

		// Read in the VGM header
		header = ReadVGMHeader(file);
		if (header)
		{
			int majorVersion = BCDToInt(header->Version >> 8 & 0xff);
			int minorVersion = BCDToInt(header->Version & 0xff);

			// Fill in the header-related metadata
			lastFileInfo.trackLength44100ths = header->TotalLength;
			lastFileInfo.loopLength44100ths  = header->LoopLength;
			lastFileInfo.vgmVersion = majorVersion * 100 + minorVersion;
			swprintf(lastFileInfo.vgmVersionAsText, L"%d.%02d", majorVersion, minorVersion);
			lastFileInfo.recordingRate = header->RecordingRate;

			// handle the GD3
			if (header->GD3Offset != 0)
			{
				GD3Header* gd3Header;
				gzseek(file, header->GD3Offset + GD3DELTA, SEEK_SET);
				gd3Header = ReadGD3Header(file);
				if (gd3Header)
				{
                    // Read strings in
					wchar_t* buffer = malloc(gd3Header->Length);
					if (buffer)
					{
						wchar_t* p = buffer;
						int tagNumber;
						// Read into it
						gzread(file, buffer, gd3Header->Length);

						// Copy all the tags out
						for (tagNumber = 0; tagNumber < NUMGD3TAGS; ++tagNumber)
						{
							free(lastFileInfo.GD3Tags[tagNumber]);
							if (Options.trimWhitespace)
							{
								lastFileInfo.GD3Tags[tagNumber] = trimdup(p);
							}
							else
							{
								lastFileInfo.GD3Tags[tagNumber] = wcsdup(p);
							}

							CleanUpTag(tagNumber, header);

							// Move on to the next string
							p += wcslen(p) + 1;

							// Check we're in bounds
							if ((unsigned int)(p - buffer) >= gd3Header->Length)
							{
								// stop if we're not
								break;
							}
						} // loop over all tags
						free(buffer);
					}
					free(gd3Header);
				}
			}
			success = 1;

			free(header);
		}
		gzclose(file);
	}
	// success
	return success;
}

wchar_t* ChooseTag(int english, int japanese)
{
	// choose according to the option
	int chosen = (Options.preferJapanese ? japanese : english);
	// if the chosen one's NULL, then we probably don't have anything
	if (lastFileInfo.GD3Tags[chosen] == NULL)
	{
		return NULL;
	}
	// if it's empty, try the other one
	if (*(lastFileInfo.GD3Tags[chosen]) == 0)
	{
		chosen = (chosen == japanese ? english : japanese);
	}
	return lastFileInfo.GD3Tags[chosen];
}

int VGMMetaData_GetLengthInMs(const char* filename)
{
	if (Options.loopForever)
	{
		return -1000; // = infinite
	}

	if (LoadAllFileInfo(filename))
	{
		long long samples = lastFileInfo.trackLength44100ths
			+ lastFileInfo.loopLength44100ths * (Options.numLoops - 1);
		double ms = samples / 44.1;
		if (Options.playbackRate > 0 && lastFileInfo.recordingRate > 0)
		{
			ms *= Options.playbackRate / lastFileInfo.recordingRate; 
		}
		return (int)(ms + 0.5);
	}
	else
	{
		return 0;
	}
}

wchar_t* VGMMetaData_GetArtist(const char* filename)
{
	if (LoadAllFileInfo(filename))
	{
		return ChooseTag(GD3_AUTHOREN, GD3_AUTHORJP);
	}
	else
	{
		return NULL;
	}
}

wchar_t* VGMMetaData_GetTitle(const char* filename)
{
	if (LoadAllFileInfo(filename))
	{
		return ChooseTag(GD3_TITLEEN, GD3_TITLEJP);
	}
	else
	{
		return NULL;
	}
}

wchar_t* VGMMetaData_GetAlbum(const char* filename)
{
	if (LoadAllFileInfo(filename))
	{
		return ChooseTag(GD3_GAMEEN, GD3_GAMEJP);
	}
	else
	{
		return NULL;
	}
}

wchar_t* VGMMetaData_GetComment(const char* filename)
{
	if (LoadAllFileInfo(filename))
	{
		return lastFileInfo.GD3Tags[GD3_NOTES];
	}
	else
	{
		return NULL;
	}
}

wchar_t* VGMMetaData_GetSystem(const char* filename)
{
	if (LoadAllFileInfo(filename))
	{
		return ChooseTag(GD3_SYSTEMEN, GD3_SYSTEMJP);
	}
	else
	{
		return NULL;
	}
}

wchar_t* VGMMetaData_GetTrackNumberAsText(const char* filename)
{
	return NULL;
}

wchar_t* VGMMetaData_GetAlbumArtist(const char* filename)
{
	return NULL;
}

wchar_t* VGMMetaData_GetTrackGainAsText(const char* filename)
{
	return NULL;
}

wchar_t* VGMMetaData_GetTrackPeakAsText(const char* filename)
{
	return NULL;
}

wchar_t* VGMMetaData_GetAlbumGainAsText(const char* filename)
{
	return NULL;
}

wchar_t* VGMMetaData_GetAlbumPeakAsText(const char* filename)
{
	return NULL;
}

void VGMMetaData_CleanUpResources()
{
	FreeLastFileInfo();

	free(replayGainDBFilename);
}

void VGMMetaData_SetReplayGainDB(const char* filename)
{
	free(replayGainDBFilename);
	replayGainDBFilename = strdup(filename);
}

void VGMMetaData_SetPreferJapanese(int prefer)
{
	Options.preferJapanese = prefer;
}

void VGMMetaData_SetAppendFMForYM2413(int append)
{
	Options.appendFMForYM2413 = append;
}

void VGMMetaData_SetTrimWhitespace(int trim)
{
    Options.trimWhitespace = trim;    
}

void VGMMetaData_SetPlaybackRate(int rate)
{
	Options.playbackRate = rate;
}

void VGMMetaData_SetLoopForever(int forever)
{
	Options.loopForever = forever;
}

void VGMMetaData_SetNumLoops(int numLoops)
{
	Options.numLoops = numLoops;
}
*/

VGMMetaData::VGMMetaData()
{
}

VGMMetaData::~VGMMetaData()
{
}

/* static */ VGMMetaData& VGMMetaData::getInstance()
{
	static VGMMetaData instance;
	return instance;
}

int VGMMetaData::Get(const std::string& filename, enum Int item)
{
	LoadFile(filename);
	IntMap::const_iterator it = m_ints.find(item);
	if (it == m_ints.end())
	{
		throw std::exception("Key not found");
	}
	else
	{
		return it->second;
	}
}

bool VGMMetaData::Get(const std::string& filename, enum Bool item)
{
	LoadFile(filename);
	BoolMap::const_iterator it = m_bools.find(item);
	if (it == m_bools.end())
	{
		throw std::exception("Key not found");
	}
	else
	{
		return it->second;
	}
}

double VGMMetaData::Get(const std::string& filename, Double item)
{
	LoadFile(filename);
	DoubleMap::const_iterator it = m_doubles.find(item);
	if (it == m_doubles.end())
	{
		throw std::exception("Key not found");
	}
	else
	{
		return it->second;
	}
}

const std::wstring& VGMMetaData::Get(const std::string& filename, String item, Language language /* = Language::Auto */)
{
	static const std::wstring empty;
	LoadFile(filename);
	StringMap::const_iterator it = m_strings.find(item);
	if (it == m_strings.end())
	{
		throw std::exception("Key not found");
	}
	else
	{
		return ChooseString(it->second, language);
	}
}

void VGMMetaData::LoadFile(const std::string& filename)
{
    if (m_lastFilename == filename)
	{
		return;
	}

	m_lastFilename = filename;
	ClearData();

	gzFile file = ::gzopen(filename.c_str(), "rb");
	if (file)
	{
		// get the file size
		int sizeInBytes = PlatformDependent::getFileSize(filename);
		Set(Int::SizeInBytes,           sizeInBytes);

		// Read in the VGM header
		VGMHeaderObj header;
		header.loadFromFile(file);

		Set(Int::TrackLengthInSamples,  header.getTotalLengthInSamples());
		Set(Int::LoopLengthInSamples,   header.getLoopLengthInSamples());
		int version = header.getMajorVersion() * 100 + header.getMinorVersion();
		Set(Int::Version,               version);
		Set(Double::VersionAsDouble,    (double)version / 100);
		Set(Int::RecordingRate,         header.getRecordingRate());

		// Synthesis
		double totalLengthInMs = adjustTime((double)header.getTotalLengthInSamples() / VGM_BASE_FREQUENCY * 1000, header);
		double loopLengthInMs  = adjustTime((double)header.getLoopLengthInSamples()  / VGM_BASE_FREQUENCY * 1000, header);
		Set(Double::TrackLengthInMs, totalLengthInMs);
		Set(Double::LoopLengthInMs,  loopLengthInMs);
		Set(Double::Bitrate,         (double)sizeInBytes * 8 / ((double)header.getTotalLengthInSamples() / VGM_BASE_FREQUENCY));
		if (m_loopForever)
		{
			Set(Int::LoopedLengthInSamples, -1000);
			Set(Double::LoopedLengthInMs, -1000);
		}
		else
		{
			unsigned long long loopedLengthInSamples = header.getLoopedLengthInSamples(m_numLoops);
			Set(Int::LoopedLengthInSamples, (int)loopedLengthInSamples);
			Set(Double::LoopedLengthInMs,   adjustTime((double)loopedLengthInSamples / VGM_BASE_FREQUENCY * 1000, header));
		}

		// Most of the rest comes from the GD3
		if (header.getGD3Offset() != 0)
		{
			LoadGD3(file, header.getGD3Offset());
		}

		// Sniff for chips used (inaccurately, we don't want to be all day)
		SniffData(file, header);

		// Is the file compressed We are probably near the end...
		while (::gzseek(file, 1024, SEEK_CUR) > 0); // seek on a bit at a time
		int uncompressedSizeInBytes = ::gztell(file);
		if (uncompressedSizeInBytes == sizeInBytes)
		{
			Set(Double::Compression, 0.0);
		}
		else
		{
			Set(Double::Compression, 1.0 - (double)sizeInBytes/uncompressedSizeInBytes);
		}
	}
	::gzclose(file);

	LoadReplayGain(filename);

	LoadAlbumMetadata(filename);
}

void VGMMetaData::ClearData()
{
	m_ints.clear();
	m_strings.clear();
	m_doubles.clear();
	m_ints.clear();
}

void VGMMetaData::Set(enum Int item, int value)
{
	m_ints[item] = value;
}

void VGMMetaData::Set(enum String item, enum Language language, const std::wstring& value)
{
	if (language == Language::Japanese)
	{
		m_strings[item].second = value;
	}
	else
	{
		m_strings[item].first = value;
	}
}

void VGMMetaData::Set(enum Double item, double value)
{
	m_doubles[item] = value;
}

void VGMMetaData::Set(enum Bool item, bool value)
{
	m_bools[item] = value;
}

const std::wstring& VGMMetaData::ChooseString(const Strings& items, Language language)
{
	switch (language)
	{
	case Language::English:
		return items.first;
	case Language::Japanese:
		return items.second;
	case Language::Auto:
		{
			const std::wstring& str = ChooseString(items, m_preferredLanguage);
			if (str.length() == 0)
			{
				return ChooseString(items, (m_preferredLanguage == Language::English ? Language::Japanese : Language::English));
			}
			return str;
		}
	default:
		throw std::exception("Invalid language");
	}
}

void VGMMetaData::LoadGD3(gzFile file, int offset)
{
	::gzseek(file, offset, SEEK_SET);
	GD3Header gd3Header;
	int bytesRead = ::gzread(file, &gd3Header, sizeof(GD3Header));

	if ((bytesRead != sizeof(GD3Header)) // file too short/error reading
		|| strncmp(gd3Header.GD3Ident, GD3IDENT, GD3IDENT_LEN) != 0) // no marker
	{
		return;
	}

	// Read strings into a buffer
	wchar_t* buffer = new wchar_t[gd3Header.Length + 1];
	if (!buffer)
	{
		return;
	}
	bytesRead = ::gzread(file, buffer, gd3Header.Length);
	if (bytesRead != gd3Header.Length)
	{
		delete [] buffer;
		return;
	}
	// Null super-terminate for safety
	buffer[gd3Header.Length] = 0;

	wchar_t* p = buffer;
	// Copy all the tags out
	for (int tagNumber = 0; tagNumber < NUMGD3TAGS; ++tagNumber)
	{
		// construct a wstring from the wchar_t*
		std::wstring str(p);

		// Stick it in the list
		if (tagNumber < GD3_DATE)
		{
			// Bilingual tag
			const String lookup[] = {String::Title, String::Game, String::System, String::Author};
			String key = lookup[tagNumber / 2];
			Language language = (tagNumber % 2 == 0 ? Language::English : Language::Japanese);
			Set(key, language, str);
		}
		else
		{
			// Monolingual
			const String lookup[] = {String::Date, String::Creator, String::Comment};
			String key = lookup[tagNumber - GD3_DATE];
			Set(key, Language::English, str); // put them in the English slot...
		}

		// Move on to the next string
		p += wcslen(p) + 1;

		// Check we're in bounds
		if ((unsigned int)(p - buffer) >= gd3Header.Length)
		{
			// stop if we're not
			break;
		}
	} // loop over all tags
	delete [] buffer;

	// CleanUpTags
}

void VGMMetaData::SetOptions(bool preferJapanese, bool appendFMForYM2413, bool trimWhitespace, int playbackRate, bool loopForever, int numLoops, const std::string& dbFilename)
{
	m_preferredLanguage = (preferJapanese ? Language::Japanese : Language::English);
	m_appendFMForYM2413 = appendFMForYM2413;
	m_trimWhitespace = trimWhitespace;
	m_playbackRate = playbackRate;
	m_loopForever = loopForever;
	m_numLoops = numLoops;
	if (m_numLoops < 1)
	{
		m_loopForever = true;
	}
	m_dbFilename = dbFilename;
	// trigger reload of metadata on next query
	m_lastFilename = "";
}

double VGMMetaData::adjustTime(double time, const VGMHeaderObj& header)
{
	int recordingRate = header.getRecordingRate();
	if (m_playbackRate != recordingRate && recordingRate > 0 && m_playbackRate > 0)
	{
		return time * (double)m_playbackRate / recordingRate;
	}
	else
	{
		return time;
	}
}

int VGMMetaData::CRCString(const std::string& string)
{
	// 7-zip provides useful functions for us
	static bool initted = false;
	if (!initted)
	{
		::InitCrcTable();
		initted = true;
	}
	return ::CrcCalculateDigest((void *)string.c_str(), string.length());
}

void VGMMetaData::LoadReplayGain(const std::string& filename)
{
	char* sql = sqlite3_mprintf("select replaygain_track_gain, replaygain_track_peak, replaygain_album_gain, replaygain_album_peak from replaygain where filename_hash = %d", CRCString(filename));

	sqlite3* db = OpenDB();

	sqlite3_exec(db, sql, DBReplayGainCallback, (void*)this, NULL);

	sqlite3_close(db);

	sqlite3_free(sql);
}

void VGMMetaData::LoadAlbumMetadata(const std::string& filename)
{
	// Find playlist
	
	// Assumes a filename "Streets of Rage II - Never Return Alive.vgz"
	// will have a playlist "Streets of Rage II.m3u"

	// get the directory
	std::string::size_type slashPos = filename.find_last_of('\\');
	if (slashPos == std::string::npos)
	{
		// give up
		return;
	}
	std::string directory = filename.substr(0, slashPos);
	std::string filenameOnly = filename.substr(slashPos + 1);

	// Look through all the M3Us in the directory
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = FindFirstFile(std::string(directory + "*.m3u").c_str(), &FindFileData);
	if (hFind == INVALID_HANDLE_VALUE)
	{
		return;
	}

	std::string m3uFilename;
	std::vector<std::string> playlistFiles;
	int trackNumber = 0;
	bool foundIt = false;
	do 
	{
		m3uFilename = directory + FindFileData.cFileName;
		// check if this is the right playlist
		LoadPlaylist(m3uFilename, playlistFiles);
		for (std::vector<std::string>::const_iterator it = playlistFiles.begin(); it != playlistFiles.end(); ++it)
		{
			++trackNumber;
			if (::stricmp(it->c_str(), filenameOnly.c_str()) == 0)
			{
				foundIt = true;
				break;
			}
		}
	} 
	while (!foundIt && FindNextFile(hFind, &FindFileData) != 0);
	FindClose(hFind);

	if (!foundIt)
	{
		return;
	}

	// OK, we found it, and thus we know the track number
	Set(Int::TrackNumber, trackNumber);

	// Chances are, we will be asked about the same playlist later
	m_lastPlaylistFilename = m3uFilename;

	// 


/*
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
*/
}

void VGMMetaData::LoadPlaylist(const std::string& filename, std::vector<std::string>& items)
{
	std::vector<char> buf;
	const int bufsize = 1024;
	buf.reserve(bufsize);
	std::ifstream f(filename.c_str());
	items.clear();
	while (f.good())
	{
		f.getline(&buf[0], bufsize);
		if (buf[0] == '#' || buf[0] == '\0')
		{
			continue; // skip to next line
		}

		// Add to vector
		items.push_back(std::string(&buf[0]));
	}
}

sqlite3* VGMMetaData::OpenDB()
{
	if (m_dbFilename.length() == 0)
	{
		throw std::exception("DB name not set");
	}
	sqlite3* db;
	int status = sqlite3_open(m_dbFilename.c_str(), &db);

	if (status != SQLITE_OK)
	{
		sqlite3_close(db);
		return NULL;
	}

	return db;
}

int VGMMetaData::DBReplayGainCallback(void*, int argc, char** argv, char** azColName)
{
	VGMMetaData& metadata = VGMMetaData::getInstance();
	// need to add a handler here for every piece of metadata persisted in the DB
	for(int i = 0; i < argc; ++i)
	{
		if (stricmp(azColName[i], "replaygain_album_gain") == 0)
		{
			metadata.Set(Double::ReplayGainAlbumGain, CharStarToDouble(argv[i]));
		}
		else if (stricmp(azColName[i], "replaygain_album_peak") == 0)
		{
			metadata.Set(Double::ReplayGainAlbumPeak, CharStarToDouble(argv[i]));
		}
		else if (stricmp(azColName[i], "replaygain_track_gain") == 0)
		{
			metadata.Set(Double::ReplayGainTrackGain, CharStarToDouble(argv[i]));
		}
		else if (stricmp(azColName[i], "replaygain_track_peak") == 0)
		{
			metadata.Set(Double::ReplayGainTrackPeak, CharStarToDouble(argv[i]));
		}
	}
	return 0;
}

double VGMMetaData::CharStarToDouble(char* str)
{
	double val;
	std::stringstream ss;
	ss << str;
	ss >> val;
	return val;
}

const std::wstring& VGMMetaData::GetAsWString(const std::string& filename, Int item)
{
	static std::wstring result;
	std::wostringstream ss;
	ss << Get(filename, item);
	result = ss.str();
	return result;
}

const std::wstring& VGMMetaData::GetAsWString(const std::string& filename, Double item)
{
	static std::wstring result;
	std::wostringstream ss;
	ss << std::fixed << Get(filename, item);
	result = ss.str();
	return result;
}

void VGMMetaData::SniffData(gzFile file, VGMHeaderObj &header)
{
	::gzseek(file, header.getVGMDataOffset(), SEEK_SET);
	bool foundSN76489 = false;
	bool foundYM2413 = false;
	bool foundYM2612 = false;
	bool foundYM2151 = false;
	for (int commands = 0; commands < 1000; ++commands)
	{
		int command = ::gzgetc(file);
		switch (command)
		{
		case VGM_END:
		case EOF:
			commands += 1000;
			break;
		case VGM_GGST:
		case VGM_SN76489:
			foundSN76489 = true;
			::gzseek(file, 1, SEEK_CUR);
			break;
		case VGM_YM2413:
			foundYM2413 = true;
			::gzseek(file, 2, SEEK_CUR);
			break;
		case VGM_YM2612_0:
		case VGM_YM2612_1:
			foundYM2612 = true;
			::gzseek(file, 2, SEEK_CUR);
			break;
		case VGM_YM2151:
			foundYM2151 = true;
			::gzseek(file, 2, SEEK_CUR);
			break;
		case VGM_PAUSE_WORD:
			::gzseek(file, 2, SEEK_CUR);
			break;
		case VGM_PAUSE_60TH:
		case VGM_PAUSE_50TH:
		case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
		case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
			break;
		case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
		case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
		case VGM_YM2612_PCM_SEEK: 
			foundYM2612 = true;
			break;
		case VGM_DATA_BLOCK:
			{
				int guard = ::gzgetc(file);
				int type = ::gzgetc(file);
				unsigned int size = ::gzgetc(file);
				size |= (::gzgetc(file) << 8);
				size |= (::gzgetc(file) << 16);
				size |= (::gzgetc(file) << 24);
				::gzseek(file, size, SEEK_CUR);
				switch (type)
				{
				case VGM_DATA_BLOCK_YM2612_PCM:
					foundYM2612 = true;
					break;
				}
			}
		default:
			if ( command >= VGM_RESERVED_1_PARAM_BEGIN && command <= VGM_RESERVED_1_PARAM_END )
			{
				::gzseek(file, 1, SEEK_SET);
			}
			else if ( command >= VGM_RESERVED_2_PARAMS_BEGIN && command <= VGM_RESERVED_2_PARAMS_END )
			{
				::gzseek(file, 2, SEEK_SET);
			}
			else if ( command >= VGM_RESERVED_3_PARAMS_BEGIN && command <= VGM_RESERVED_3_PARAMS_END )
			{
				::gzseek(file, 3, SEEK_SET);
			}
			else if ( command >= VGM_RESERVED_4_PARAMS_BEGIN && command <= VGM_RESERVED_4_PARAMS_END )
			{
				::gzseek(file, 4, SEEK_SET);
			}
			break;
		}
	}

	Set(Bool::HasSN76489, foundSN76489);
	Set(Bool::HasYM2413, foundYM2413);
	Set(Bool::HasYM2612, foundYM2612);
	Set(Bool::HasYM2151, foundYM2151);
}

bool VGMMetaData::Write(const std::string& filename, Double item, double value)
{
	// Deliberately incomplete write support
	if (filename != m_lastFilename)
	{
		LoadFile(filename);
	}
	switch (item)
	{
	case Double::ReplayGainAlbumGain:
	case Double::ReplayGainAlbumPeak:
	case Double::ReplayGainTrackGain:
	case Double::ReplayGainTrackPeak:
		Set(item, value);
		return true;
	default:
		return false;
	}
}

bool VGMMetaData::WriteAsWString(const std::string& filename, Double item, const std::wstring& value)
{
	std::wistringstream ss(value);
	double realVal;
	ss >> realVal;
	return Write(filename, item, realVal);
}

bool VGMMetaData::Save(const std::string& filename)
{
	if (filename != m_lastFilename)
	{
		return false;
	}
	
	// We only save certain items (not to the file)
	sqlite3* db = OpenDB();
    if (db == NULL)
	{
		return false;
	}

	// make the table
	int status = sqlite3_exec(db,
		"PRAGMA page_size=4096;"
		"PRAGMA synchronous = OFF;"
		"PRAGMA temp_store = MEMORY;"
		"create table if not exists replaygain ("
			"filename_hash int primary key,"
			"replaygain_track_gain text,"
			"replaygain_track_peak text,"
			"replaygain_album_gain text,"
			"replaygain_album_peak text" 
		");", NULL, NULL, NULL
	);

	// output SQL for all the data
	std::ostringstream buf;
	buf << "insert or replace into replaygain ("
				"filename_hash,"
				"replaygain_track_gain,"
				"replaygain_track_peak,"
				"replaygain_album_gain,"
				"replaygain_album_peak "
			") values ("
		<<		CRCString(filename) << ","
		<<		m_doubles[Double::ReplayGainTrackGain] << ","
		<<		m_doubles[Double::ReplayGainTrackPeak] << ","
		<<		m_doubles[Double::ReplayGainAlbumGain] << ","
		<<		m_doubles[Double::ReplayGainAlbumPeak]
		<< ");";
	status += sqlite3_exec(db, buf.str().c_str(), NULL, NULL, NULL);
	status += sqlite3_close(db);

	if (status != SQLITE_OK)
	{
		return 0; // failed
	}

	return 1; // success
}
