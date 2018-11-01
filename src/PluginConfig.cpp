#include "PluginConfig.h"
#include <windows.h>
#include <sstream>

const std::string PluginConfig::s_inisection("Maxim's VGM input plugin");

const std::string& PluginConfig::getINISection()
{
	return s_inisection;
}

PluginConfig::PluginConfig()
{
	// Set all the default config in here
	addSetting(Setting::PLAYBACKNUMLOOPS, "NumLoops", 1);
	addSetting(Setting::PLAYBACKLOOPFOREVER, "Loop forever", false);
	addSetting(Setting::PLAYBACKRATE, "Playback rate", 60);

	addSetting(Setting::SAMPLINGRATE, "Sampling rate", 44100);

	addSetting(Setting::TAGSCUSTOMFILETYPE, "ML custom type", false);
	addSetting(Setting::TAGSADDFMTOYM2413, "ML show FM", true);
	addSetting(Setting::TAGSPREFERJAPANESE, "ML Japanese", false);
	addSetting(Setting::TAGSTRIM, "Trim whitespace", true);
}

void PluginConfig::loadFromINI(const std::string& filename)
{
	// Get all the settings we know about (see above)
	for (SettingMap::iterator it = m_settings.begin(); it != m_settings.end(); ++it)
	{
		it->second.val = loadString(filename, it->second.key, it->second.val);
	}
}

void PluginConfig::saveToINI(const std::string& filename)
{
	// Write all the settings we have
	for (SettingMap::const_iterator it = m_settings.begin(); it != m_settings.end(); ++it)
	{
		WritePrivateProfileString(s_inisection.c_str(), it->second.key.c_str(), it->second.val.c_str(), filename.c_str());
	}
}

void PluginConfig::addSetting(Setting setting, const std::string& key, const std::string& defaultVal)
{
	m_settings.insert(std::make_pair(setting, SettingData(key, defaultVal)));
}

void PluginConfig::addSetting(Setting setting, const std::string& key, int defaultVal)
{
	std::ostringstream ss;
	ss << defaultVal;
	addSetting(setting, key, ss.str());
}

void PluginConfig::addSetting(Setting setting, const std::string& key, bool defaultVal)
{
	addSetting(setting, key, (defaultVal == true ? 1 : 0));
}

std::string PluginConfig::loadString(const std::string& filename, const std::string& key, const std::string& defaultVal)
{
	// GetPrivateProfileString will not tell us how big the string is
	// So we have to mess around resizing buffers...
	char* buf = NULL;
	bool success = false;

	for (int bufSize = defaultVal.length() + 2; !success && bufSize < 1024*1024; ++bufSize)
	{
		// (re-)allocate buffer
		if (buf != NULL) delete [] buf;
		buf = new char[bufSize];

		int numChars = GetPrivateProfileString(s_inisection.c_str(), key.c_str(), defaultVal.c_str(), buf, bufSize, filename.c_str());
		// GetPrivateProfileString returns bufSize - 1 if the buffer was too small
		success = (numChars != bufSize - 1);
	}

	if (success)
	{
		std::string result(buf);
		delete [] buf;
		return result;
	}
	else
	{
		delete [] buf;
		return defaultVal;
	}

}

int PluginConfig::getInt(Setting setting)
{
	std::stringstream ss;
	const std::string& val = getString(setting);
	ss << val;
	int result;
	ss >> result;
	return result;
}

const std::string& PluginConfig::getString(Setting setting)
{
	SettingMap::const_iterator it = m_settings.find(setting);
	if (it == m_settings.end())
	{
		throw std::exception("Invalid setting");
	}
	return it->second.val;
}

bool PluginConfig::getBool(Setting setting)
{
	return getInt(setting) != 0;
}

void PluginConfig::set(Setting setting, int value)
{
	std::ostringstream ss;
	ss << value;
    set(setting, ss.str());
}

void PluginConfig::set(Setting setting, const std::string& value)
{
	SettingMap::iterator it = m_settings.find(setting);
	if (it == m_settings.end())
	{
		throw std::exception("Invalid setting");
	}
	it->second.val = value;
}

void PluginConfig::set(Setting setting, bool value)
{
	set(setting, (value ? 1 : 0));
}