#ifndef GlobalConfig_h__
#define GlobalConfig_h__

// hacked from in_flac source by Maxim
// GPL applies?

int GetBitsPerSample();
int GetNumChannels();
bool AllowSurround();
bool ReplayGainEnabled();
bool ReplayGainAlbumMode();
bool DitherEnabled();
float GetPreamp(); // gain for tracks with no RG data
int ReplayGainMode(); // see below
bool ReplayGainPreferredOnly();
int GetPlaybackThreadPriority();

// enums for values
// ReplayGainMode() return value
enum
{
	RG_MODE_GAIN = 0,        // apply gain only (uses gain)
	RG_MODE_GAIN_NOCLIP = 1, // apply gain, if it would clip then reduce gain so it doesn't (uses gain, peak)
	RG_MODE_NORMALIZE = 2,   // normalise (uses peak)
	RG_MODE_NOCLIP = 3       // only has effect if peak > 1.0?!?
};
enum
{
	RG_SOURCE_TRACK = 0, RG_SOURCE_ALBUM = 1
};

#endif // GlobalConfig_h__
