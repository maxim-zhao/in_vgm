// hacked from in_flac source by Maxim
#include <windows.h>
#include "Winamp SDK/winamp/wa_ipc.h"
#include "Winamp SDK/Wasabi/api/service/waservicefactory.h"
#include "Winamp SDK/Agave/config/api_config.h"
#include "Winamp SDK/winamp/in2.h"
#include "GlobalConfig.h"

extern In_Module mod;

// {B6CB4A7C-A8D0-4c55-8E60-9F7A7A23DA0F}
static const GUID playbackConfigGroupGUID =
  {
    0xb6cb4a7c, 0xa8d0, 0x4c55, { 0x8e, 0x60, 0x9f, 0x7a, 0x7a, 0x23, 0xda, 0xf }
  };


static api_service *serviceApi = 0;
static api_config *configApi = 0;

api_config *GetConfigAPI()
{
	if (!serviceApi)
	{
		serviceApi = (api_service *)SendMessage(mod.hMainWindow, WM_WA_IPC, 0, IPC_GET_API_SERVICE);
	}

	if ((int)serviceApi == 1)
		return NULL;

	if (serviceApi && !configApi)
	{
		waServiceFactory *sf = (waServiceFactory *)serviceApi->service_getServiceByGuid(AgaveConfigGUID);
		if (!sf)
			return NULL;
		configApi = (api_config *)sf->getInterface();
	}

	return configApi;
}
int GetBitsPerSample()
{
	api_config *config = GetConfigAPI();
	int bits = 16;
	if (config)
		bits = config->GetUnsigned(playbackConfigGroupGUID, L"bits", 16);

	return bits;
}

int GetNumChannels()
{
	api_config *config = GetConfigAPI();
	bool mono = false;
	if (config)
		mono = config->GetBool(playbackConfigGroupGUID, L"mono", false);

	if (mono)
		return 1;
	else
		return 2;

}

bool AllowSurround()
{
	api_config *config = GetConfigAPI();
	bool surround = true;
	if (config)
		surround = config->GetBool(playbackConfigGroupGUID, L"surround", true);

	return surround;
}

bool ReplayGainAlbumMode()
{
	api_config *config = GetConfigAPI();
	int mode = RG_SOURCE_TRACK;
	if (config)
		mode = config->GetUnsigned(playbackConfigGroupGUID, L"replaygain_source", RG_SOURCE_TRACK);

	return mode == RG_SOURCE_ALBUM;
}

bool ReplayGainEnabled()
{
	api_config *config = GetConfigAPI();
	bool enabled = false;
	if (config)
		enabled = config->GetBool(playbackConfigGroupGUID, L"replaygain", false);

	return enabled;
}

bool DitherEnabled()
{
	api_config *config = GetConfigAPI();
	bool enabled = false;
	if (config)
		enabled = config->GetBool(playbackConfigGroupGUID, L"dither", false);

	return enabled;
}
float GetPreamp()
{
	api_config *config = GetConfigAPI();
	float preamp = -6.0;
	if (config)
		preamp = config->GetFloat(playbackConfigGroupGUID, L"non_replaygain", -6.0);

	return preamp;
}

int ReplayGainMode()
{
	api_config *config = GetConfigAPI();
	int mode = RG_MODE_GAIN_NOCLIP;
	if (config)
		mode = config->GetUnsigned(playbackConfigGroupGUID, L"replaygain_mode", false);

	return mode;
}

bool ReplayGainPreferredOnly()
{
	api_config *config = GetConfigAPI();
	bool enabled = false;
	if (config)
		enabled = config->GetBool(playbackConfigGroupGUID, L"replaygain_non_rg_gain", false);

	return enabled;
}

int GetPlaybackThreadPriority()
{
	api_config *config = GetConfigAPI();
	int pri = THREAD_PRIORITY_ABOVE_NORMAL;
	if (config)
		pri = config->GetInt(playbackConfigGroupGUID, L"priority", THREAD_PRIORITY_ABOVE_NORMAL);

	return pri;
}
