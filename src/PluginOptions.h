#ifndef PluginOptions_h__
#define PluginOptions_h__

typedef struct
{
	BOOL enableReplayGainHack; // If true, Replay Gain will be applied except when it is detected that Replay Gain is being calculated
	BOOL LoopForever;          // If true, the plugin will play looped tracks forever

	int PlaybackRate;          // in Hz
} PluginOptions;

#endif // PluginOptions_h__