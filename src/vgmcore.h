#ifndef VGHMORE_H
#define VGMCORE_H

#include <zlib.h>
#include "vgm.h"
#include "sn76489/sn76489.h"
#include "emu2413/emu2413.h"
#include "gens/ym2612.h"

#define SN76489_NUM_CHANNELS 4
#define SN76489_MUTE_ALLON 0xf
// YM2413 has 14 (9 + 5 percussion), BUT it uses 1=mute, 0=on
#define YM2413_NUM_CHANNELS 14
#define YM2413_MUTE_ALLON 0
// YM2612 has 8 (6 + DAC + SSG_EG), 1=mute, 0=on
#define YM2612_NUM_CHANNELS 8
#define YM2612_MUTE_ALLON 0x80 // default SSG off
// This is preliminary and may change
#define YM2151_MUTE_ALLON 0


// TODO: figure out which of these are actually needed, do some renaming
typedef struct
{
	char *filename;
	int
		fileTrackLengthInms,    // Current track length in ms
		corePlaybackRate,       // in Hz
		filePlaybackRate,       // in Hz
		coreLoopForever,
		coreNumLoops,           // how many times to play looped section
		coreNumLoopsDone,       // how many loops we've played
		fileLoopLengthInms,     // length of looped section in ms
		fileLoopOffset,         // File offset of looped data start
		fileVGMDataOffset,      // File offset of data start
		fileSN76489Clock,       // SN76489 clock rate
		fileYM2413Clock,        // FM clock rates
		fileYM2612Clock,        // 
		fileYM2151Clock,        // 
		coreChipsUsed,          // BlackAura - FM Chips enabled
		coreSeekToSampleNumber,
		coreSeekToTimeInms,
		coreYM2413HiQ,
		coreVolumeOverdrive,
		corePauseBetweenTracksms,
		corePauseBetweenTracksCounter,
		coreLoopingFadeOutms,
		coreLoopingFadeOutCounter,
		coreLoopingFadeOutTotal,
		coreMutePersistent,
		coreMuting_SN76489,
		corePanning_SN76489[SN76489_NUM_CHANNELS], // panning 0..254, 127 is centre
		corePanning_YM2413Pan[YM2413_NUM_CHANNELS],
		coreRandomisePanning,
		coreSN76489Enable,
		coreYM2413Enable,
		coreYM2612Enable,
		coreYM2151Enable,
		coreFilterType,
		coreFilterPrevSample[2],
		coreSampleRate,
		coreKillDecodeThread,
		coreNoMoreSamples,
		coreYM2612Core;

	unsigned char* corePCM_buffer;
	unsigned long corePCM_bufferSize;
	unsigned long corePCM_bufferPos;

	long
		coreMuting_YM2413,
		coreMuting_YM2612,
		coreMuting_YM2151;

	gzFile* coreInputFile;
	TVGMHeader *fileVGMHeader;

	int coreSamplesTillNextRead;
	float coreWaitFactor,coreFractionalSamplesTillNextRead;

	OPLL *coreYM2413;  // EMU2413 structure
	SN76489_Context *coreSN76489;
	ym2612_ *coreYM2612;

	float
		SN_preamp,
		YM2413_preamp,
		YM2612_preamp,
		YM2151_preamp;

	float ReplayGain;
	float ReplayPeak;
	int ReplayNoClip;

} VGMCore;

VGMCore *vgmcore_init();
int vgmcore_loadfile(VGMCore *core, const char *filename, int killReplayGain);
int vgmcore_seek(VGMCore *core, int time_in_ms);
int vgmcore_getsamples(VGMCore *core, short *buf, int buflen, int *killswitch);
void vgmcore_free(VGMCore *core);
int vgmcore_getlength(VGMCore *core);

#endif