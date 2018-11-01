#ifndef VGMMetadata_h__
#define VGMMetadata_h__

#include <string>
#include <xstring>
#include <map>
#include <zlib.h>
#include "libs\sqlite\sqlite3.h"
#include <vector>

class VGMHeaderObj;

class VGMMetaData
{
private: // we are a singleton
	VGMMetaData();
	VGMMetaData(VGMMetaData& other);
	~VGMMetaData();
public:
	static VGMMetaData& getInstance();

	// Enumeration of string metadata types we support
	enum String
	{
		Author,
		Title,
        Game,
		System,
		GameAllAuthors, // synthesised
		Date,
		Comment,
		Creator
	};

	enum Int
	{
		TrackLengthInSamples, // at 44100Hz
		LoopLengthInSamples,
		TrackNumber,
		SizeInBytes,
		Version, // precise (*100)
		RecordingRate,
		LoopedLengthInSamples // synthesized
	};

	enum Bool
	{
		HasSN76489,
		HasYM2413,
		HasYM2612,
		HasYM2151
	};

	enum Double
	{
		ReplayGainTrackGain,
		ReplayGainTrackPeak,
		ReplayGainAlbumGain,
		ReplayGainAlbumPeak,
		TrackLengthInMs, // synthesized
		LoopLengthInMs,
		LoopedLengthInMs,
		Bitrate,
		VersionAsDouble,
		Compression
	};

	enum Language
	{
		Auto = -1,
		English = 0,
		Japanese = 1
	};

	// Getters
	int Get(const std::string& filename, Int item);
	bool Get(const std::string& filename, Bool item);
	double Get(const std::string& filename, Double item);
	const std::wstring& Get(const std::string& filename, String item, Language language = Language::Auto);
	const std::wstring& GetAsWString(const std::string& filename, Int item);
	const std::wstring& GetAsWString(const std::string& filename, Double item);

	// Setters (limited)
	bool Write(const std::string& filename, Double item, double value);
	bool WriteAsWString(const std::string& filename, Double item, const std::wstring& value);
	bool Save(const std::string& filename);

	// Options
	void SetOptions(bool preferJapanese, bool appendFMForYM2413, bool trimWhitespace, int playbackRate, bool loopForever, int numLoops, const std::string& dbFilename);

private: // helpers
	void LoadFile(const std::string& filename);

	void SniffData(gzFile file, VGMHeaderObj &header);
	void LoadReplayGain(const std::string& filename);
	void LoadAlbumMetadata(const std::string& filename);

	void Set(enum Int    item, int value);
	void Set(enum Bool   item, bool value);
	void Set(enum String item, enum Language language, const std::wstring& value);
	void Set(enum Double item, double value);
	void ClearData();
	double adjustTime(double time, const VGMHeaderObj& header); // applies rate adjustment
	void LoadPlaylist(const std::string& filename, std::vector<std::string>& items);

	sqlite3* OpenDB();
	int CRCString(const std::string& string);
	static double CharStarToDouble(char* str);
	static int DBReplayGainCallback(void*, int argc, char** argv, char** azColName);

private:
	// settings
	Language m_preferredLanguage;
	bool m_appendFMForYM2413;
	bool m_trimWhitespace;
	int m_playbackRate;
	bool m_loopForever;
	int m_numLoops;

	// data
	std::string m_lastFilename;
	typedef std::pair<std::wstring, std::wstring> Strings;
	typedef std::map<String, Strings> StringMap;
	typedef std::map<Int, int> IntMap;
	typedef std::map<Double, double> DoubleMap;
	typedef std::map<Bool, bool> BoolMap;
	StringMap m_strings;
	IntMap m_ints;
	DoubleMap m_doubles;
	BoolMap m_bools;
	std::string m_dbFilename;
	std::string m_lastPlaylistFilename;
	std::vector<std::string> m_lastPlaylistItems;

private:
	// helpers that need to know about a typedef above
	const std::wstring& ChooseString(const Strings& items, Language language);
	void LoadGD3(gzFile file, int offset);
};

#endif // VGMMetadata_h__
