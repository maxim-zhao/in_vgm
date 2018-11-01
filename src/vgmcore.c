// VGM core
// Handles playback of a VGM file in a stateful manner

#include <stdlib.h> // malloc/free
#include <stdio.h>  // *printf
#include <string.h> // str*
#include <assert.h>
#include "vgmcore.h"
#include "panning.h"
#include "common.h"
#include "gens/ym2612.h"
#include "mame/ym2151.h"
#include "emu2413/emu2413.h"
#include "sn76489/sn76489.h"
#include "apply_gain.h"

// grr, I'd like to avoid this
void getReplayGainData(const char *filename, float *gain, float *peak, int *noclip);

#define CHIPS_USED_YM2413  0x01  // Bit 0 = YM2413
#define CHIPS_USED_YM2612  0x02  // Bit 1 = YM2612
#define CHIPS_USED_YM2151  0x04  // Bit 2 = YM2151
#define CHIPS_USED_SN76489 0x08  // Bit 3 = SN76489
#define USINGCHIP(core,chip) (core->coreChipsUsed&chip)

#define FADEOUT_MAX_VOLUME 100  // Number of steps for fadeout; can't have too many because of overflow

// TODO: refactor filtering out
enum
{
	FILTER_NONE,
	FILTER_LOWPASS,
	FILTER_WEIGHTED
};

// Abstracted commands for chips

// Initialise chip emulator if necessary
void checkStarted(VGMCore* core, int chip)
{
	if (!USINGCHIP(core,chip))
	{
		switch(chip)
		{
		case CHIPS_USED_SN76489:
			core->coreSN76489 = SN76489_Init( core->fileSN76489Clock, core->coreSampleRate );
			SN76489_Config(
				core->coreSN76489, 
				core->coreMuting_SN76489, 
				core->fileVGMHeader->PSGWhiteNoiseFeedback, 
				core->fileVGMHeader->PSGShiftRegisterWidth,
				((int)(core->fileVGMHeader->YM2612Clock / 1000000) == 7 ? 0 : 1 ) // nasty hack: boost noise except for YM2612 music
			);
			SN76489_SetPanning(
				core->coreSN76489,
				core->corePanning_SN76489[0],
				core->corePanning_SN76489[1],
				core->corePanning_SN76489[2],
				core->corePanning_SN76489[3]
			);
			break;
		case CHIPS_USED_YM2612:
			core->coreYM2612 = GENS_YM2612_Init( core->fileYM2612Clock, core->coreSampleRate, 0 );
			GENS_YM2612_SetMute( core->coreYM2612, core->coreMuting_YM2612 );
			break;
		case CHIPS_USED_YM2413:
			core->coreYM2413 = OPLL_new( core->fileYM2413Clock, core->coreSampleRate );
			OPLL_reset( core->coreYM2413 );
			OPLL_reset_patch( core->coreYM2413, 0 );
			OPLL_setMask( core->coreYM2413, core->coreMuting_YM2413 );
			{
				int i;
				for ( i = 0; i < YM2413_NUM_CHANNELS; ++i )
					OPLL_set_pan( core->coreYM2413, i, core->corePanning_YM2413Pan[i] );
			}
			OPLL_set_quality( core->coreYM2413, core->coreYM2413HiQ );
			break;
		case CHIPS_USED_YM2151:
			YM2151Init( 1, core->fileYM2151Clock, core->coreSampleRate );
			break;
		}
		core->coreChipsUsed |= chip;
	}
}

void writeToChip(VGMCore* core, int chip, int val1, int val2)
{
	checkStarted(core, chip);
	
	switch(chip)
	{
		// TODO
	}
}

VGMCore* vgmcore_init()
{
	int i;
	VGMCore *core = (VGMCore *)malloc(sizeof(VGMCore));

	// most things should default to 0
	memset(core, 0, sizeof(VGMCore));

	core->coreSN76489Enable = 1;
	core->coreYM2413Enable = 1;
	core->coreYM2612Enable = 1;
	core->coreYM2151Enable = 1;

	// a lot of these need to be more configurable!
	core->coreFilterType = FILTER_NONE;

	for (i = 0; i < SN76489_NUM_CHANNELS; ++i)
		core->corePanning_SN76489[i] = PANNING_CENTRE;

	for (i = 0; i < YM2413_NUM_CHANNELS; ++i)
		core->corePanning_YM2413Pan[i] = PANNING_CENTRE;

//	for (i = 0; i < YM2612_NUM_CHANNELS; ++i)
//		core->YM2612_Pan[i] = PANNING_CENTRE;

	core->SN_preamp = 1.0f;
	core->YM2413_preamp = 1.0f;
	core->YM2612_preamp = 0.25f;
	core->YM2151_preamp = 0.25f;

	core->coreMuting_YM2413=YM2413_MUTE_ALLON;  // backup when stopped. PSG does it itself.
	core->coreMuting_YM2612=YM2612_MUTE_ALLON;
	core->coreMuting_YM2151=YM2151_MUTE_ALLON;

	core->coreSampleRate = 44100; // TODO: configurable rate (one day)

	return core;
}

int vgmcore_loadfile(VGMCore *core, const char *filename, int killReplayGain)
{
	int i;
	int file_size;

	core->coreInputFile=NULL;

	if (
		(!core->filename) // no previous filename -> reset muting
		||
		(
		(!core->coreMutePersistent) && (strcmp(filename,core->filename)!=0)
		)
		)
	{
		// If file has changed, reset channel muting (if not blocked)
		core->coreMuting_SN76489=SN76489_MUTE_ALLON;
		core->coreMuting_YM2413=YM2413_MUTE_ALLON;
		core->coreMuting_YM2612=YM2612_MUTE_ALLON;
		core->coreMuting_YM2151=YM2151_MUTE_ALLON;
	}

	if ( core->coreRandomisePanning )
	{
		// Randomise panning
		for ( i = 0; i < SN76489_NUM_CHANNELS; ++i )
			core->corePanning_SN76489[i] = random_stereo();
		for ( i = 0; i < YM2413_NUM_CHANNELS; ++i )
			core->corePanning_YM2413Pan[i] = random_stereo();
	}

	// Copy the filename
	core->filename = strdup(filename);
	if (!core->filename)
		return -1;

	// Blargg - free previous PCM data
	if (core->corePCM_buffer) free( core->corePCM_buffer );
	core->corePCM_buffer = NULL;
	core->corePCM_bufferSize = 0;
	core->corePCM_bufferPos = 0;

	// Open the file
	core->coreInputFile = gzopen( filename, "rb" );
	if (core->coreInputFile == NULL)
		return -1;

	file_size = FileSize(filename);

	// Read header
	core->fileVGMHeader = ReadVGMHeader( core->coreInputFile, 0 );
	if (!core->fileVGMHeader)
		return -1;

	// Fix header stuff for old versions:
	// VGM 1.10 added per-chip clocks
	if ( core->fileVGMHeader->Version < 0x0110 )
	{
		core->fileVGMHeader->PSGWhiteNoiseFeedback = 0x0009;
		core->fileVGMHeader->PSGShiftRegisterWidth = 16;
		core->fileVGMHeader->YM2612Clock = core->fileVGMHeader->YM2413Clock;
		core->fileVGMHeader->YM2151Clock = core->fileVGMHeader->YM2413Clock;
	}
	// VGM 1.50 added a configurable data offset, but it's better to check for 0 here
	if ( core->fileVGMHeader->VGMDataOffset == 0 )
		core->fileVGMDataOffset = 0x40;
	else
		core->fileVGMDataOffset = core->fileVGMHeader->VGMDataOffset + VGMDATADELTA;

	// Get length
	if ( core->fileVGMHeader->TotalLength == 0 )
	{
		core->fileTrackLengthInms = 0;
	}
	else
	{
		core->fileTrackLengthInms = (int)(core->fileVGMHeader->TotalLength / 44.1);
	}

	// Get loop data
	if ( core->fileVGMHeader->LoopLength == 0 )
	{
		core->fileLoopLengthInms = 0;
		core->fileLoopOffset = 0;
	} 
	else 
	{
		core->fileLoopLengthInms = (int)(core->fileVGMHeader->LoopLength / 44.1);
		core->fileLoopOffset = core->fileVGMHeader->LoopOffset + LOOPDELTA;
	}

	// Get clock values
	// TODO: unnecessary duplication here
	core->fileSN76489Clock = core->fileVGMHeader->PSGClock;
	core->fileYM2413Clock = core->fileVGMHeader->YM2413Clock;
	core->fileYM2612Clock = core->fileVGMHeader->YM2612Clock;
	core->fileYM2151Clock = core->fileVGMHeader->YM2151Clock;

	// BlackAura - Disable all FM chips
	core->coreChipsUsed=0;

	// Get rate
	// TODO: unnecessary duplication here
	core->filePlaybackRate = core->fileVGMHeader->RecordingRate;

	// get Replay Gain stuff
	// unless we've been told not to
	if (killReplayGain)
	{
		core->ReplayGain = 0.0;
		core->ReplayPeak = 1000;
		core->ReplayNoClip = 0;
	}
	else
	{
		getReplayGainData(filename, &core->ReplayGain, &core->ReplayPeak, &core->ReplayNoClip);
	}

	// FM Chip startups are done whenever a chip is used for the first time

	// Start up SN76489 (if used)
	if ( core->fileSN76489Clock )
	{
		core->coreSN76489 = SN76489_Init( core->fileSN76489Clock, core->coreSampleRate );
		// TODO: error checking here
		SN76489_Config( core->coreSN76489, 
			core->coreMuting_SN76489, 
			core->fileVGMHeader->PSGWhiteNoiseFeedback, 
			core->fileVGMHeader->PSGShiftRegisterWidth,
			((int)(core->fileVGMHeader->YM2612Clock / 1000000) == 7 ? 0 : 1 )
			); // nasty hack: boost noise except for YM2612 music
		SN76489_SetPanning( core->coreSN76489,
			core->corePanning_SN76489[0],
			core->corePanning_SN76489[1],
			core->corePanning_SN76489[2],
			core->corePanning_SN76489[3]
			);
	}

	// get ready to read data
	gzseek( core->coreInputFile, core->fileVGMDataOffset, SEEK_SET );

	// Reset some stuff
	core->coreNumLoopsDone=0;
	core->coreSeekToSampleNumber=-1;
	core->coreSeekToTimeInms = 0;
	core->corePauseBetweenTracksCounter=-1;  // signals "not adding silence at end of track"; 0+ = samples left to pause
	core->coreLoopingFadeOutTotal=-1;        // signals "haven't started fadeout yet"

	if ( (core->corePlaybackRate == 0) || (core->filePlaybackRate == 0) )
		core->coreWaitFactor=1.0;
	else
		core->coreWaitFactor = (float) core->filePlaybackRate / core->corePlaybackRate;

	core->coreNoMoreSamples = 0;

	return 0; // OK
}

int vgmcore_seek(VGMCore *core, int time_in_ms)
{
	core->coreSeekToTimeInms = time_in_ms;
	return 0; // OK
}

int vgmcore_getsamples(VGMCore *core, short *buf, int num_samples, int *killswitch)
{
	int x, b1, b2;
	short *buf_start = buf;

	if ( core->coreNoMoreSamples || *killswitch )
	{
		return 0; // caller should recognise this as "ain't got no more samples"
	}

	// is a seek needed?
	// (do seeking in this thread for safety in regular Winamp mode)
	if ( core->coreSeekToTimeInms )
	{
		if (core->coreInputFile==NULL) return 0;

		if USINGCHIP(core,CHIPS_USED_YM2413) {  // If using YM2413, reset it
			int i;
			long int YM2413Channels = OPLL_toggleMask( core->coreYM2413, 0 );
			OPLL_reset( core->coreYM2413 );
			OPLL_setMask( core->coreYM2413, YM2413Channels );
			for ( i = 0; i < YM2413_NUM_CHANNELS; ++i )
				OPLL_set_pan( core->coreYM2413, i, core->corePanning_YM2413Pan[i] );
		}

		if USINGCHIP( core, CHIPS_USED_YM2612 )
			GENS_YM2612_Reset( core->coreYM2612 );

		if USINGCHIP( core, CHIPS_USED_YM2151 )
			YM2151ResetChip( 0 );

		gzseek( core->coreInputFile, core->fileVGMDataOffset, SEEK_SET );
		core->coreNumLoopsDone = 0;

		if ( core->fileLoopLengthInms > 0 ) // file is looped
			// See if I can skip some loops
			// TODO: shouldn't this all be done in samples, not ms? Not that it matters to the result, but it might fix some weirdnesses
			while ( core->coreSeekToTimeInms > core->fileTrackLengthInms ) 
			{
				++core->coreNumLoopsDone;
				core->coreSeekToTimeInms -= core->fileLoopLengthInms;
			}
		else // Not looped
			if ( core->coreSeekToTimeInms > core->fileTrackLengthInms ) 
				core->coreNumLoopsDone = core->coreNumLoops + 1; // for seek-past-eof in non-looped files

		core->coreSeekToSampleNumber = (int)(core->coreSeekToTimeInms * 44.1);
		core->coreSeekToTimeInms = 0;

		// If seeking beyond EOF...
		if (core->coreNumLoopsDone > core->coreNumLoops)
			return 0;
	}

	// Seek done; start outputting samples
	for ( x = 0; x < num_samples; ++x )
	{
		if (*killswitch) return x;

		// Read file, output data to chips until an pause is needed or it's outputting end-of-track silence
		while ( core->coreSamplesTillNextRead == 0 && core->corePauseBetweenTracksCounter == -1 )
		{
			switch (b1 = gzgetc(core->coreInputFile))
			{
			case VGM_GGST:  // GG stereo
				b1 = gzgetc(core->coreInputFile);
				if (core->fileSN76489Clock)
					SN76489_GGStereoWrite(core->coreSN76489, b1);
				break;
			case VGM_PSG:  // SN76489 write
				b1 = gzgetc(core->coreInputFile);
				if (core->fileSN76489Clock) 
					SN76489_Write(core->coreSN76489, b1);
				break;
			case VGM_YM2413:  // YM2413 write
				b1 = gzgetc(core->coreInputFile);
				b2 = gzgetc(core->coreInputFile);
				if (core->fileYM2413Clock)
				{
					checkStarted(core, CHIPS_USED_YM2413);
					OPLL_writeReg( core->coreYM2413, b1, b2 );  // Write to the chip
				}
				break;
			case VGM_YM2612_0:  // YM2612 write (port 0)
				b1 = gzgetc(core->coreInputFile);
				b2 = gzgetc(core->coreInputFile);
				if (core->fileYM2612Clock)
				{
					checkStarted(core, CHIPS_USED_YM2612);
					GENS_YM2612_Write( core->coreYM2612, 0, b1 );
					GENS_YM2612_Write( core->coreYM2612, 1, b2 );
				}
				break;
			case VGM_YM2612_1:  // YM2612 write (port 1)
				b1 = gzgetc(core->coreInputFile);
				b2 = gzgetc(core->coreInputFile);
				if ( core->fileYM2612Clock ) {
					checkStarted(core, CHIPS_USED_YM2612);
					GENS_YM2612_Write( core->coreYM2612, 2, b1 );
					GENS_YM2612_Write( core->coreYM2612, 3, b2 );
				}
				break;
			case VGM_YM2151:  // BlackAura - YM2151 write
				b1 = gzgetc(core->coreInputFile);
				b2 = gzgetc(core->coreInputFile);
				if (core->fileYM2151Clock)
				{
					checkStarted(core, CHIPS_USED_YM2151);
					YM2151WriteReg( 0, b1, b2 );
				}
				break;
			case VGM_PAUSE_WORD:  // Wait n samples
				b1 = gzgetc(core->coreInputFile);
				b2 = gzgetc(core->coreInputFile);
				core->coreSamplesTillNextRead = b1 | (b2 << 8);
				break;
			case VGM_PAUSE_60TH:  // Wait 1/60 s
				core->coreSamplesTillNextRead = LEN60TH;
				break;
			case VGM_PAUSE_50TH:  // Wait 1/50 s
				core->coreSamplesTillNextRead = LEN50TH;
				break;

				// No-data waits
			case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
			case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
				core->coreSamplesTillNextRead = (b1 & 0xf) + 1;
				break;

				// (YM2612 sample then short delay)s
				// YM2612 write (port 0) 0x2A (PCM)
			case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
			case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
				core->coreSamplesTillNextRead = b1 & 0x0f;
				if (core->fileYM2612Clock && core->corePCM_buffer)
				{
					b1 = 0x2A; // PCM register
					b2 = core->corePCM_buffer [core->corePCM_bufferPos++];
					assert( core->corePCM_bufferPos <= core->corePCM_bufferSize );

					checkStarted(core, CHIPS_USED_YM2612);
					GENS_YM2612_Write( core->coreYM2612, 0, b1 );
					GENS_YM2612_Write( core->coreYM2612, 1, b2 );
				}
				break;

			case VGM_YM2612_PCM_SEEK: // Set position in PCM data
				{ 
					unsigned char buf [4];
					gzread( core->coreInputFile, buf, sizeof(buf) );
					core->corePCM_bufferPos = buf[3] << 24
						| buf[2] << 16
						| buf[1] <<  8
						| buf[0] <<  0;
					assert( core->corePCM_bufferPos < core->corePCM_bufferSize );
					break;
				}

			case VGM_DATA_BLOCK: // data block (should be at beginning of file)
				{
					unsigned char buf[6];
					unsigned long data_size;
					gzread( core->coreInputFile, buf, sizeof(buf));
					assert( buf[0] == 0x66 ); // first byte is 0x66 for compatibility
					data_size = buf[5] << 24  // 3rd to 6th byte is size
						| buf[4] << 16
						| buf[3] <<  8
						| buf[2] <<  0;
					switch (buf[1]) // second byte is type
					{
					case VGM_DATA_BLOCK_YM2612_PCM:
						if ( !core->corePCM_buffer )
						{
							// only load first PCM data block
							core->corePCM_bufferSize = data_size;
							core->corePCM_buffer = malloc( data_size );
							if ( core->corePCM_buffer )
							{
								gzread( core->coreInputFile, core->corePCM_buffer, core->corePCM_bufferSize );
								break; // <-- exits out of (nested) case block on successful load
							}
						}
						// ignore data block for subsequent blocks and also on malloc() failure
						gzseek( core->coreInputFile, data_size, SEEK_CUR );
						break;
					default:
						// skip unknown data blocks
						gzseek( core->coreInputFile, data_size, SEEK_CUR );
						break;
					}

					break;
				}

			case VGM_END:  // End of data
				++core->coreNumLoopsDone;  // increment loop count

				if ( core->fileLoopOffset == 0) // If there's no looping then go to the inter-track pause (if specified)
				{
					if ( (core->corePauseBetweenTracksms) && (core->corePauseBetweenTracksCounter == -1) )
						// I want to output silence for this many samples
						core->corePauseBetweenTracksCounter = (long) core->corePauseBetweenTracksms * 44100 / 1000;
					else
					{
						// End track
						core->coreNoMoreSamples = 1;
						return x;
					}
					// "unget" a byte so it'll come back here again, rather than move on to invalid data
					gzungetc(VGM_END,core->coreInputFile);
				}
				else // if there is looping, and the required number of loops have played, then go to fadeout
				{
					if ( (!core->coreLoopForever) && (core->coreNumLoopsDone > core->coreNumLoops) && (core->coreLoopingFadeOutTotal == -1) )
					{
						// - not loop forever
						// - done enough loops
						// - fade out hasn't started yet
						// Start fade out
						core->coreLoopingFadeOutTotal = (long)core->coreLoopingFadeOutms * 44100 / 1000;  // number of samples to fade over
						core->coreLoopingFadeOutCounter = core->coreLoopingFadeOutTotal;
					}
					// Jump to the loop point
					gzseek( core->coreInputFile, core->fileLoopOffset, SEEK_SET );
				}
				break;

			default:
				// Unknown commands
				if ( b1 >= VGM_RESERVED_1_PARAM_BEGIN && b1 <= VGM_RESERVED_1_PARAM_END )
					gzseek( core->coreInputFile, 1, SEEK_CUR );
				else if ( b1 >= VGM_RESERVED_2_PARAMS_BEGIN && b1 <= VGM_RESERVED_2_PARAMS_END )
					gzseek( core->coreInputFile, 2, SEEK_CUR );
				else if ( b1 >= VGM_RESERVED_3_PARAMS_BEGIN && b1 <= VGM_RESERVED_3_PARAMS_END )
					gzseek( core->coreInputFile, 3, SEEK_CUR );
				else if ( b1 >= VGM_RESERVED_4_PARAMS_BEGIN && b1 <= VGM_RESERVED_4_PARAMS_END )
					gzseek( core->coreInputFile, 4, SEEK_CUR );
#ifdef _DEBUG
				{
					char buffer[10*1024];
					sprintf( buffer, "Invalid data 0x%02x at offset 0x%06x in file \"%s\".\n", b1, gztell( core->coreInputFile ) - 1, core->filename );
					//					OutputDebugString(buffer);
					assert(0);
				}
#endif
				break;
			}  // end switch

			if ( core->coreSamplesTillNextRead ) // last command was a pause, adjust it according to the playback rate
			{
				core->coreFractionalSamplesTillNextRead += core->coreSamplesTillNextRead * core->coreWaitFactor; // add on to last saved fractional part
				core->coreSamplesTillNextRead = (int)(core->coreFractionalSamplesTillNextRead);          // strip to integer part to figure out how many samples are needed
				core->coreFractionalSamplesTillNextRead -= core->coreSamplesTillNextRead;              // and save the fractional part to avoid inaccuracy

				if ( core->coreSeekToSampleNumber > -1 ) // If a seek is wanted, subtract the pause from it and then continue the while loop
				{  
					core->coreSeekToSampleNumber -= core->coreSamplesTillNextRead;  // Decrease the required seek by the current delay
					core->coreSamplesTillNextRead = 0;                    // reset the pause length so it'll go back to reading VGM data

					if ( core->coreSeekToSampleNumber < 0 )               // get here when the seek has all been used up
					{                          
						core->coreSamplesTillNextRead = -(core->coreSeekToSampleNumber);// by moving the difference into the pause length
						core->coreSeekToSampleNumber=-1;                    // and disabling seeking
					}
					continue; // TODO: necessary? it's at the end of the loop anyway
				}
			} // end if (pause command)
		}  // end while (no pause needed)

		// Write one sample
		// TODO: batch writes? Check which chip cores add and which assign
		if ( core->corePauseBetweenTracksCounter == -1 )
		{
			int NumChipsUsed=0; // for volume adjustment
			int l=0,r=0,scratch[2]={0,0}; // accumulators for the two channels and two temp values for chips to write into

			if (*killswitch) return x;

			// SN76489
			if (core->fileSN76489Clock)
			{
				NumChipsUsed++;
				if ( core->coreSN76489Enable )
				{
					// SN76489 takes two integer pointers
					SN76489_UpdateOne( core->coreSN76489, &scratch[0], &scratch[1] );
					l = (int)(scratch[0] * core->SN_preamp);
					r = (int)(scratch[1] * core->SN_preamp);
				}
			}

			// YM2413
			if ( core->fileYM2413Clock )
			{
				if (USINGCHIP(core,CHIPS_USED_YM2413)) 
				{
					NumChipsUsed++;
					if(core->coreYM2413Enable)
					{
						// EMU2413 takes a pointer to 2 integers
						OPLL_calc_stereo(core->coreYM2413,scratch);
						l += (int)(scratch[0] * core->YM2413_preamp);
						r += (int)(scratch[1] * core->YM2413_preamp);
					}
				}
			}

			if (core->fileYM2612Clock) {
				// YM2612
				if USINGCHIP(core,CHIPS_USED_YM2612) {
					int *Buffer[2] = { &scratch[0], &scratch[1] };
					NumChipsUsed++;
					if (core->coreYM2612Enable) {
						// Gens YM2612 takes a pointer to two pointers to integers (gah)
						GENS_YM2612_Update(core->coreYM2612, Buffer,1);
						GENS_YM2612_DacAndTimers_Update(core->coreYM2612, Buffer,1);
						l += (int)(scratch[0] * core->YM2612_preamp);
						r += (int)(scratch[1] * core->YM2612_preamp);
					}
				}
			}

			if (core->fileYM2151Clock) {
				// YM2151
				if USINGCHIP(core,CHIPS_USED_YM2151) {
					// YM2151 code wants a pointer to two pointers to signed 16-bit integers (double gah)
					signed short mameLeft;
					signed short mameRight;
					signed short *mameBuffer[2] = { &mameLeft, &mameRight};
					NumChipsUsed++;
					if (core->coreYM2151Enable) {
						YM2151UpdateOne(0,mameBuffer,1);
						mameLeft =(short)(mameLeft  * core->YM2151_preamp);
						mameRight=(short)(mameRight * core->YM2151_preamp);
					} else
						mameLeft=mameRight=0;  // Dodgy muting until per-channel gets done

					l+=mameLeft ;
					r+=mameRight;
				}
			}

			// do any filtering 
			// TODO: move out to its own file, for the possibility of adding better (FIR) filtering later
			if ( core->coreFilterType != FILTER_NONE )
			{
				int pre_filter_l = l, pre_filter_r = r;

				if ( core->coreFilterType == FILTER_LOWPASS )
				{
					// output = average of current and previous sample
					l += core->coreFilterPrevSample[0];
					l >>= 1;
					r += core->coreFilterPrevSample[1];
					r >>= 1;
				}
				else
				{
					// output = current sample * 0.75 + previous sample * 0.25
					l = (l + l + l + core->coreFilterPrevSample[0]) >> 2;
					r = (r + r + r + core->coreFilterPrevSample[1]) >> 2;
				}

				core->coreFilterPrevSample[0] = pre_filter_l;
				core->coreFilterPrevSample[1] = pre_filter_r;
			}

			// If overdrive is active then boost everything a bit, less if more chips are active
			// when overdrive is not active we presume it's unlikely to clip and do no adjustment
			if ( (core->coreVolumeOverdrive) && (NumChipsUsed) )
			{
				l=l*8/NumChipsUsed;
				r=r*8/NumChipsUsed;
			}

			if (core->coreLoopingFadeOutTotal!=-1) // Fade out
				// TODO: move fading out of VGMCore, into a VGMPlayer wrapper? to make the VGM core just loop forever
			{
				long v;
				// Check if the counter has finished
				if ( core->coreLoopingFadeOutCounter <= 0 ) // <= just in case, shouldn't be necessary
				{
					core->coreNoMoreSamples = 1;
					return x+1;
				} else {
					// Fadeout is active, adjust result
					v = core->coreLoopingFadeOutCounter * FADEOUT_MAX_VOLUME / core->coreLoopingFadeOutTotal; // calculate target volume (FADEOUT_MAX_VOLUME..0)
					l = (long)l * v / FADEOUT_MAX_VOLUME;
					r = (long)r * v / FADEOUT_MAX_VOLUME;
					// Decrement counter
					--(core->coreLoopingFadeOutCounter);
				}
			}

			// Clip values
			if ( l > +32767 ) l = +32767; else if ( l < -32767 ) l = -32767;
			if ( r > +32767 ) r = +32767; else if ( r < -32767 ) r = -32767;

			// finally, write to buffer
			*buf++ = l;
			*buf++ = r;
		} 
		else 
		{
			// Pause between tracks or end of track
			// output silence
			*buf++ = 0;
			*buf++ = 0;

			--core->corePauseBetweenTracksCounter; // decrement pause counter

			if ( core->corePauseBetweenTracksCounter <= 0 ) // pause has finished
				return x+1; // return number of samples written
		}

		// we wrote one sample so decrement that counter and repeat the whole loop
		--(core->coreSamplesTillNextRead);
	}

	if(core->ReplayGain!=1.0)
		apply_replay_gain_16bit(core->ReplayGain,core->ReplayPeak,buf_start,num_samples,2 /* channels */,core->ReplayNoClip);

	return num_samples; // filled the buffer
}

void vgmcore_free(VGMCore *core)
{
	SN76489_Shutdown( core->coreSN76489 );
	OPLL_delete( core->coreYM2413 );
	GENS_YM2612_End( core->coreYM2612 );

	free(core->filename);

	gzclose(core->coreInputFile);

	free(core->fileVGMHeader);
	free(core);
}

int vgmcore_getlength(VGMCore *core)
{
	int samplecount = core->fileVGMHeader->TotalLength;

	if (core->coreLoopForever)
	{
		return -1;
	}

	if (core->coreNumLoops > 0 && core->fileVGMHeader->LoopLength > 0 )
	{
		samplecount += core->fileVGMHeader->LoopLength * core->coreNumLoops;
		samplecount += (long)core->coreLoopingFadeOutms * 44100 / 1000;
	}

	if (core->coreWaitFactor == 1.0f)
		return samplecount;
	else
		return (int)((double)samplecount * (double)core->coreWaitFactor);
}
