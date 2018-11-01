#ifndef PluginConfig_h__
#define PluginConfig_h__

#include <string>
#include <map>

/*
class Config
{
public: // interface
	void loadFromINI(const std::string& filename, const std::string& section);
	void saveToINI(const std::string& filename, const std::string& section);

	const std::string& getString(const std::string& key);
	int getInt(const std::string& key);
	bool getBool(const std::string& key);

	void setString(const std::string& key, const std::string& val);
	void setInt(const std::string& key, int val);
	void setBool(const std::string& key, bool val);

private: // helpers
	std::string loadString(const std::string& filename, const std::string& section, const std::string& key, const std::string& defaultValue);

private: // data
	std::map<std::string, std::string> m_settings;
};

*/
class PluginConfig
{
public:
	// Constructor
	PluginConfig();
	// Default destructor, copy constructor and operator= are OK

	static const std::string& getINISection();
	void loadFromINI(const std::string& filename);
	void saveToINI(const std::string& filename);

	enum Setting
	{
		PLAYBACKNUMLOOPS,
		PLAYBACKLOOPFOREVER,
//		const char* PLAYBACKFADEOUTLENGTH = "Fade out length";
//		const char* PLAYBACKINTERTRACKPAUSE = "Pause between tracks";
		PLAYBACKRATE,
		SAMPLINGRATE,
//		const char* OVERDRIVE = "Overdrive";
//		const char* IMMEDIATEUPDATE = "Immediate update";
		TAGSCUSTOMFILETYPE,
		TAGSADDFMTOYM2413,
		TAGSPREFERJAPANESE,
// 		const char* TAGSGUESSTRACKNUMBERs = "Guess track numbers";
// 		const char* TAGSGUESSALBUMARTISTS = "Guess album artists";
// 		const char* TAGSSTANDARDISESEPARATORS = "Fix separators";
// 		const char* TAGSSTANDARDISEDATES = "Fix dates";
		TAGSTRIM,
// 		const char* VGM7Z_ENABLE = "Enable VGM7z support";
// 		const char* VGM7ZCOMPRESSION = "VGM Compression";
// 		const char* VGM7ZSAMEFOLDER = "VGM7z to same folder";
// 		const char* VGM7ZSUBDIR = "VGM7z to subfolder";
// 		const char* VGM7ZPROMPT = "VGM7z extract prompt";
// 		const char* VGM7ZDELETE = "Delete VGM7z";

		INVALID
	};

	const std::string& getString(Setting setting);
	int getInt(Setting setting);
	bool getBool(Setting setting);

	void set(Setting setting, int value);
	void set(Setting setting, bool value);
	void set(Setting setting, const std::string& value);

private: // helpers
	void addSetting(Setting setting, const std::string& key, const std::string& defaultVal);
	void addSetting(Setting setting, const std::string& key, int defaultVal);
	void addSetting(Setting setting, const std::string& key, bool defaultVal);
	std::string loadString(const std::string& filename, const std::string& key, const std::string& defaultVal);
private: // data
	struct SettingData
	{
		SettingData(const std::string& key, const std::string& val)
		:key(key), val(val)
		{}
		std::string key;
		std::string val;
	};
	typedef std::map<Setting, SettingData> SettingMap;
	SettingMap m_settings;

	static const std::string s_inisection;
};

#endif // PluginConfig_h__
