#ifndef VGMPlayer_h__
#define VGMPlayer_h__

#include "VGM.h"
#include <zlib.h>
#include <string>

extern "C"
{
#include "sn76489/sn76489.h"
#include "ym2413/emu2413.h"
#include "ym2612/fm.h"
}

#include "VGMHeader.h"

class VGMPlayer
{
public:
	// Get/release player
	VGMPlayer();
	~VGMPlayer();

	// Open/close file
	bool OpenFile(const std::string& filename, int sampleRate, bool renderNativeRate);
	void CloseFile();

	// Get audio
	// returns number of samples rendered
	int GetSamples(short* buffer, int numSamples);

	// Seek
	// returns true on success
	bool SeekTo(int timeInMs);

	// Metadata
	// return negative for endless mode
	int GetLengthInMs();
	long long GetLengthInSamples();

	// returns 0 for endless files
	double GetBitrate();

	int GetSampleRate();

	void SetPlaybackRate(int rate);
	int GetPlaybackRate();

	// Get/set number of times to play any looped section
	// negative loop count -> endless mode
	// zero is not an option
	void SetNumLoops(int m_numLoops);
	int GetNumLoops();

private: // helpers
	// General helpers
	bool IsAcceptableVGM();
	void CalculateRateAdjustment();
	double BaseSampleCountToRendered(double numSamples);

	// VGM parser

	void ParseVGMDataUntilWaitIsNeeded();


	// Chip wrappers/helpers
	void StartSN76489();
	void StopSN76489();
	void ResetSN76489();
	void WriteSN76489(int value);
	void WriteSN76489Stereo(int value);
	void RenderSamplesSN76489(short* buffer, unsigned int numSamples);

	void StartYM2612();
	void StopYM2612();
	void ResetYM2612();
	void WriteYM2612(int port, int address, int value);
	void RenderSamplesYM2612(short* buffer, unsigned int numSamples);

	void StartYM2413();
	void StopYM2413();
	void ResetYM2413();
	void WriteYM2413(int address, int value);
	void RenderSamplesYM2413(short* buffer, unsigned int numSamples);

	void PrepareBufferSN76489(unsigned int numSamples);

private: // data
	// File stuff
	std::string m_filename;
	gzFile m_file;
	int m_fileSize;

	// Metadata
	VGMHeaderObj m_VGMHeader;

	// Playback configuration
	int m_sampleRate;
	int m_numLoops;
	int isPaused;
	int m_playbackRate;      // \ use SetPlaybackRate to 
	double m_rateAdjustment; // / set these two together
	bool m_nativeRendering;

	// Playback state
	int m_numLoopsPlayed;
	bool m_isPlaying;
	double m_samplesToRender; // number of samples to render before continuing

	// Chip emulators
	SN76489_Context* m_pSN76489;
	void* m_pYM2612;
	char* m_YM2612SampleBuffer;
	unsigned int m_YM2612SampleBufferLength;
	unsigned int m_YM2612SampleBufferPosition;
	OPLL* m_pYM2413;

	// Chip buffers - where needed
	unsigned int m_SN76489BufferLen;
	INT16* m_SN76489Buffers[2];
	unsigned int m_YM2612BufferLen;
	int* m_YM2612Buffers[2];
};

#endif // VGMPlayer_h__
