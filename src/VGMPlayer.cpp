#ifdef _DEBUG
#include <windows.h> // for outputdebugstring
#endif

#include "VGMPlayer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "VGM.h"
#include <zlib.h>

#include "VGMUtils.h"
#include <assert.h>
#include <limits.h>

#include "PlatformDependent.h"

#pragma warning (disable:4068) // allow #pragma region

VGMPlayer::VGMPlayer()
: m_filename(""),
  m_file(NULL),
  m_fileSize(0),
  m_sampleRate(0),
  m_numLoops(2), // not zero
  isPaused(false),
  m_playbackRate(0),
  m_rateAdjustment(1.0), // not zero
  m_nativeRendering(0),
  m_numLoopsPlayed(0),
  m_isPlaying(false),
  m_samplesToRender(0)/*,
  m_pSN76489(NULL),
  m_pYM2612(NULL),
  m_YM2612SampleBuffer(NULL),
  m_YM2612SampleBufferLength(0),
  m_YM2612SampleBufferPosition(0),
  m_pYM2413(NULL),
  m_SN76489BufferLen(0),
  m_YM2612BufferLen(0)*/
{
// 	m_SN76489Buffers[0] = m_SN76489Buffers[1] = NULL;
// 	m_YM2612Buffers[0] = m_YM2612Buffers[1] = NULL;
}

VGMPlayer::~VGMPlayer()
{
	CloseFile();
// 	// Clean up emulators (and buffers)
//  	StopSN76489();
//  	StopYM2612();
//  	StopYM2413();
}

bool VGMPlayer::OpenFile(const std::string& filename, int sampleRate, bool renderNativeRate)
{
	if (m_file)
	{
		CloseFile();
	}

	// copy parameters in
	m_filename = filename;
	m_sampleRate = sampleRate;
	m_nativeRendering = renderNativeRate;

	// get the file size
	// (for bps calculations)
	m_fileSize = PlatformDependent::getFileSize(filename);

	// open the file
	m_file = ::gzopen(m_filename.c_str(), "rb");
	if (m_file == NULL)
	{
		CloseFile();
		return false;
	}

	// read in the header
	try
	{
		m_VGMHeader.loadFromFile(m_file);
	}
	catch (std::exception&)
	{
		CloseFile();
		return false;
	}

	// check whether we like it
	if (!IsAcceptableVGM())
	{
		CloseFile();
		return false;
	}

	// Get the file ready to return some data
	gzseek(m_file, m_VGMHeader.getVGMDataOffset(), SEEK_SET);

	// Reset various playback-related things
	m_numLoopsPlayed = 0;
	m_isPlaying = true;

	// Need to recalculate rate adjustment when file changes
	CalculateRateAdjustment();
	return true;
}

void VGMPlayer::CloseFile()
{
	m_isPlaying = false;

// 	StopSN76489();
// 	StopYM2612();
// 	StopYM2413();

	// Clean up resources allocated in OpenFile()
	if (m_file != NULL)
	{
		gzclose(m_file);
		m_file = NULL;
	}
	m_filename = "";
}

bool VGMPlayer::IsAcceptableVGM()
{
	// So long as the major version is 1 we are happy
	return m_VGMHeader.getMajorVersion() == 1;
}

void VGMPlayer::CalculateRateAdjustment()
{
	int recordingRate = m_VGMHeader.getRecordingRate();
	if (recordingRate == 0 || m_playbackRate == 0)
	{
		// can't adjust
		m_rateAdjustment = 1.0;
	}
	else
	{
		m_rateAdjustment = (double)m_playbackRate / recordingRate;
	}
}

// The whole point is here: parse the VGM file and get samples from it
int VGMPlayer::GetSamples(short* buffer, int spaceLeftInBuffer)
{
	int bufferSize = spaceLeftInBuffer;

	// exit early if this is stopped
	if (!m_isPlaying)
	{
		return 0;
	}

	// Set the buffer to zero before we start
	memset(buffer, 0, spaceLeftInBuffer * 2 * 2); // 16-bit stereo

	// Render until there's no space left in the buffer
	while (spaceLeftInBuffer > 0)
	{
		// If we are not in a pause command, read from the file until we are (or we get to the end)
		ParseVGMDataUntilWaitIsNeeded();

		// We stopped reading from the file, either because we stopped or because we had samples to render
		// Handle the latter
		if (m_samplesToRender >= 1.0)
		{
			// Only render an integral number of samples
			int intSamplesToRender = (int)m_samplesToRender;
			// Only render as many as will fit in the buffer
			if (intSamplesToRender > spaceLeftInBuffer)
			{
				intSamplesToRender = spaceLeftInBuffer;
			}

			// Emulate and mix into buffer
// 			RenderSamplesSN76489(buffer, intSamplesToRender);
// 			RenderSamplesYM2612(buffer, intSamplesToRender);
// 			RenderSamplesYM2413(buffer, intSamplesToRender);

			// leave any residual fractional samples in m_samplesToRender
			m_samplesToRender -= intSamplesToRender;

			// reduce the counter for how many samples remain to be rendered
			spaceLeftInBuffer -= intSamplesToRender;
			// and make the buffer pointer point to the next place we can write
			buffer += intSamplesToRender * 2;
		}

		if (!m_isPlaying)
		{
			// stop doing stuff
			break;
		}
	}

/*
#ifdef _DEBUG
	{
		char buf[1024];
		static int total = 0;
		total += samplesRendered;
		sprintf(buf,"Output %d samples, total %d\n", samplesRendered, total);
		OutputDebugString(buf);
	}
#endif
*/

	// Return value is the number of samples we actually rendered
	return bufferSize - spaceLeftInBuffer;
}

int VGMPlayer::GetLengthInMs()
{
	// can easily overflow 2^31 when we multiply samples by 1000
	long long length = (long long)GetLengthInSamples() * 1000 / m_sampleRate;
	if (length <= 0)
	{
		return -1000;
	}
	return (int)length;
}

long long VGMPlayer::GetLengthInSamples()
{
	if (m_numLoops < 0)
	{
		return -1000;
	}
	else
	{
		return (long long)BaseSampleCountToRendered((double)m_VGMHeader.getLoopedLengthInSamples(m_numLoops));
	}
}

bool VGMPlayer::SeekTo(int timeInMs)
{
	if (!this->m_isPlaying || timeInMs >= GetLengthInMs() || timeInMs < 0)
	{
		return false;
	}

	// Figure out where in the file that is
	unsigned int sampleNumber = (unsigned int)BaseSampleCountToRendered((unsigned int)((long long)timeInMs * VGM_BASE_FREQUENCY / 1000));

	// Seek to the start of the VGM data
	::gzseek(m_file, m_VGMHeader.getVGMDataOffset(), SEEK_SET);

	// Reset to start-of-file
	ResetSN76489();
	ResetYM2612();
	ResetYM2413();
	m_numLoopsPlayed = 0;
	m_samplesToRender = 0;

	// If we are seeking past any entirely-skipped loops then we can
	// act as if we were seeking into the first loop, so long as we start 
	// the loop counter at non-zero
	if (m_VGMHeader.canLoop())
	{
		while (sampleNumber > m_VGMHeader.getTotalLengthInSamples())
		{
			sampleNumber -= m_VGMHeader.getLoopLengthInSamples();
			++m_numLoopsPlayed;
		}
	}

	// Parse through it until the specified number of samples have been "processed"
	while (sampleNumber > 0)
	{
		// Parse the VGM data until a wait is encountered
		ParseVGMDataUntilWaitIsNeeded();
		// Get the integral number of samples
		unsigned int intWaitLengthInSamples = (int)m_samplesToRender;
		
		// If the wait takes us past the point we want, consider a lesser number
		if (intWaitLengthInSamples > sampleNumber)
		{
			intWaitLengthInSamples = sampleNumber;
		}

		// Subtract that from the time we are seeking to
		sampleNumber -= intWaitLengthInSamples;
		// And from the sample count, so it won't want to render them
		m_samplesToRender -= intWaitLengthInSamples;

		// Check if we ran out of samples
		if (!this->m_isPlaying)
		{
			return false;
		}
	}
	return true;
}

void VGMPlayer::SetPlaybackRate(int rate)
{
	if ((rate < 1) || (rate > 1000))
	{
		// no likey
		throw std::exception("Invalid playback rate");
	}

	m_playbackRate = rate;
	CalculateRateAdjustment();
}

int VGMPlayer::GetPlaybackRate()
{
	return m_playbackRate;
}

void VGMPlayer::SetNumLoops(int numLoops)
{
	if (numLoops == 0)
	{
		// zero makes no sense
		throw std::exception("Invalid number of loops");
	}
	m_numLoops = numLoops;
}

int VGMPlayer::GetNumLoops()
{
	return this->m_numLoops;
}

double VGMPlayer::GetBitrate()
{
	double seconds = (double)GetLengthInMs() / 1000.0;
	if (seconds <= 0.0)
	{
		return 0;
	}
	else
	{
		return (double)m_fileSize * 8 / seconds;
	}
}

// helper methods

int VGMPlayer::GetSampleRate()
{
	return m_sampleRate;
}

double VGMPlayer::BaseSampleCountToRendered(double numSamples)
{
	// numSamples is 44100Hz samples
	// we want to convert to m_sampleRate
	// and apply m_rateAdjustment
	double samples = (double)numSamples * m_sampleRate / VGM_BASE_FREQUENCY;
	samples /= m_rateAdjustment;
	return samples;
}

void VGMPlayer::ParseVGMDataUntilWaitIsNeeded() 
{
#define NEXTBYTE gzgetc(m_file)
#define SKIPBYTES(numBytes) gzseek(m_file, numBytes, SEEK_CUR)

	while ((m_isPlaying) && (m_samplesToRender < 1.0))
	{
		int command = NEXTBYTE;

		switch (command)
		{
		case VGM_GGST:
			// 1 byte = stereo mapping
// 			WriteSN76489Stereo(NEXTBYTE);
			break;

		case VGM_SN76489:
			// 1 byte = port write
// 			WriteSN76489(NEXTBYTE);
			break;

		case VGM_YM2413:
			// 2 bytes = address, data
			{
				int address = NEXTBYTE;
				int value = NEXTBYTE;
// 				WriteYM2413(address, value);
			}
			break;

		case VGM_YM2612_0:
		case VGM_YM2612_1:
			// 2 bytes = address, data
			// (command - VGM_YM2612_0) = 0 for VGM_YM2612_0, 1 for VGM_YM2612_1
			{
				// have to do this outside the function call because arguments are
				// evaluated left to right and then the values are the wrong way round
				int address = NEXTBYTE;
				int value = NEXTBYTE;
// 				WriteYM2612((command - VGM_YM2612_0), address, value);
			}
			break;

		case VGM_YM2151:
			// 2 bytes = address, data
			// TODO: YM2151 support
			NEXTBYTE;
			NEXTBYTE;
			break;

		case VGM_PAUSE_WORD:
			// 2 bytes = number of 44100Hz cycles to pause for
			{
				int samples = NEXTBYTE;
				samples |= (NEXTBYTE << 8);
				m_samplesToRender += BaseSampleCountToRendered(samples);
			}
			break;

		case VGM_PAUSE_60TH:
			// no data; wait 1/50s
			m_samplesToRender += BaseSampleCountToRendered(VGM_BASE_FREQUENCY / 60);
			break;

		case VGM_PAUSE_50TH:
			// no data; wait 1/50s
			m_samplesToRender += BaseSampleCountToRendered(VGM_BASE_FREQUENCY / 50);
			break;

		case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
		case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
			// Wait for 1..16 base samples
			m_samplesToRender += BaseSampleCountToRendered((command & 0xf) + 1);
			break;

		case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
		case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
// 			// Wait for 0..15 base samples and write from m_pYM2612 PCM buffer
// 			m_samplesToRender += BaseSampleCountToRendered(command & 0x0f);
// 			assert(m_YM2612SampleBuffer);
// 			// write pointed byte from buffer to port 0 address 0x2A
// 			// and increment pointer
// 			WriteYM2612(0, 0x2A, m_YM2612SampleBuffer[m_YM2612SampleBufferPosition++]);
// 			assert(m_YM2612SampleBufferPosition <= m_YM2612SampleBufferLength);
			break;

		case VGM_YM2612_PCM_SEEK: 
			// 4 bytes: set seek offset within m_pYM2612 PCM data
			{
				unsigned int offset = NEXTBYTE;
				offset |= (NEXTBYTE << 8);
				offset |= (NEXTBYTE << 16);
				offset |= (NEXTBYTE << 24);
// 				assert(offset < m_YM2612SampleBufferLength);
// 				m_YM2612SampleBufferPosition = offset;
			}
			break;

		case VGM_DATA_BLOCK: // data block (should be at beginning of file)
			// 6 bytes:
			// 0x66 = guard byte
			// 1 byte = type
			// 4 bytes = length
			{
				int guard = NEXTBYTE;
				int type = NEXTBYTE;
				unsigned int size = NEXTBYTE;
				size |= (NEXTBYTE << 8);
				size |= (NEXTBYTE << 16);
				size |= (NEXTBYTE << 24);
				assert(guard == VGM_END);
				switch (type)
				{
				case VGM_DATA_BLOCK_YM2612_PCM:
					// We load only the first one encountered.
// 					if (m_YM2612SampleBuffer == NULL)
// 					{
// 						m_YM2612SampleBufferLength = size;
// 						m_YM2612SampleBufferPosition = 0;
// 						m_YM2612SampleBuffer = new char[size];
// 						if (m_YM2612SampleBuffer)
// 						{
// 							gzread(m_file, m_YM2612SampleBuffer, size);
// 						}
// 						else
// 						{
// 							SKIPBYTES(size);
// 						}
// 					}
// 					else
					{
						SKIPBYTES(size);
					}
					break;
				default:
					SKIPBYTES(size);
					break;
				}
			}
			break;

		case VGM_END:
			// TODO: fadeout, gap

			if (m_VGMHeader.canLoop())
			{
				// Looping
				++m_numLoopsPlayed;

				if (m_numLoops > 0)
				{
					// A finite number of loops
					if (m_numLoopsPlayed < m_numLoops)
					{
						// We want to loop
						// by seeking to the loop point
						gzseek(m_file, m_VGMHeader.getLoopOffset(), SEEK_SET);
					}
					else
					{
						// Stop any more attempts to get samples
						m_isPlaying = false;
					}
				}
				else
				{
					// infinite looping
					gzseek(m_file, m_VGMHeader.getLoopOffset(), SEEK_SET);
				}
			}
			else
			{
				// Non-looping file
				m_isPlaying = false;
			}

			break;

		case EOF:
			// Stop any more attempts to get samples
			m_isPlaying = false;
			break;

		default:
			// Reserved commands
			// They have defined sizes so we can safely skip over them
			if ( command >= VGM_RESERVED_1_PARAM_BEGIN && command <= VGM_RESERVED_1_PARAM_END )
			{
				SKIPBYTES(1);
			}
			else if ( command >= VGM_RESERVED_2_PARAMS_BEGIN && command <= VGM_RESERVED_2_PARAMS_END )
			{
				SKIPBYTES(2);
			}
			else if ( command >= VGM_RESERVED_3_PARAMS_BEGIN && command <= VGM_RESERVED_3_PARAMS_END )
			{
				SKIPBYTES(3);
			}
			else if ( command >= VGM_RESERVED_4_PARAMS_BEGIN && command <= VGM_RESERVED_4_PARAMS_END )
			{
				SKIPBYTES(4);
			}
			break;
		} // end VGM command switch
	} // end while
#undef NEXTBYTE

}

void VGMPlayer::StartSN76489()
{
	if (m_pSN76489 != NULL || m_VGMHeader.getSN76489Clock() == 0)
	{
		return;
	}

	m_pSN76489 = SN76489_Init(m_VGMHeader.getSN76489Clock(), m_sampleRate);
	SN76489_Reset(m_pSN76489);
}

void VGMPlayer::StopSN76489()
{
	if (m_pSN76489 == NULL)
	{
		return;
	}

	SN76489_Shutdown(m_pSN76489);
	m_pSN76489 = NULL;

	delete m_SN76489Buffers[0];
	delete m_SN76489Buffers[1];

	m_SN76489Buffers[0] = NULL;
	m_SN76489Buffers[1] = NULL;
	m_SN76489BufferLen = 0;
}

void VGMPlayer::ResetSN76489()
{
	if (m_pSN76489 == NULL)
	{
		return;
	}

	SN76489_Reset(m_pSN76489);
}

void VGMPlayer::WriteSN76489(int value)
{
	StartSN76489();
	if (m_pSN76489 != NULL)
	{
		SN76489_Write(m_pSN76489, value);
	}
}

void VGMPlayer::WriteSN76489Stereo(int value)
{
	StartSN76489();
	if (m_pSN76489 != NULL)
	{
		SN76489_GGStereoWrite(m_pSN76489, value);
	}
}

void VGMPlayer::PrepareBufferSN76489(unsigned int numSamples) 
{
	// Prepare/enlarge buffers as needed
	if (m_SN76489BufferLen < numSamples)
	{
		// Avoid overly-small buffers
		// 576 is the smallest buffer size Winamp will ask for
		if (numSamples < 576)
		{
			numSamples = 576;
		}

		// free any existing buffers
		delete m_SN76489Buffers[0];
		delete m_SN76489Buffers[1];

		// Allocate them
		m_SN76489BufferLen = numSamples;
		m_SN76489Buffers[0] = new INT16[m_SN76489BufferLen];
		m_SN76489Buffers[1] = new INT16[m_SN76489BufferLen];
	}
}

void VGMPlayer::RenderSamplesSN76489(short* buffer, unsigned int numSamples) 
{
	if (m_pSN76489 == NULL)
	{
		return;
	}

	PrepareBufferSN76489(numSamples);

	// Get samples into buffers
	SN76489_Update(m_pSN76489, m_SN76489Buffers, numSamples);

	// and transfer into output
	for (unsigned int i = 0; i < numSamples; ++i)
	{
		buffer[i * 2 + 0] += m_SN76489Buffers[0][i];
		buffer[i * 2 + 1] += m_SN76489Buffers[1][i];
	}
}

void VGMPlayer::StartYM2612()
{
	if (m_pYM2612 || m_VGMHeader.getYM2612Clock() == 0)
	{
		return;
	}

	m_pYM2612 = YM2612Init(m_VGMHeader.getYM2612Clock(), m_sampleRate);
	YM2612ResetChip(m_pYM2612);
}

void VGMPlayer::StopYM2612()
{
	if (m_pYM2612 == NULL)
	{
		return;
	}

	YM2612Free(m_pYM2612);
	m_pYM2612 = NULL;

	free(m_YM2612Buffers[0]);
	free(m_YM2612Buffers[1]);
	m_YM2612Buffers[0] = NULL;
	m_YM2612Buffers[1] = NULL;
	m_YM2612BufferLen = 0;

	free(m_YM2612SampleBuffer);
	m_YM2612SampleBuffer = NULL;
	m_YM2612SampleBufferLength = 0;
}

void VGMPlayer::ResetYM2612()
{
	if (m_pYM2612 == NULL)
	{
		return;
	}

	YM2612ResetChip(m_pYM2612);
}

void VGMPlayer::WriteYM2612(int port, int address, int value)
{
    StartYM2612();
	if (m_pYM2612 != NULL)
	{
		// Eke-Eke's YM2612 is screwy
		// it makes us set the address and port separately
		YM2612Write(m_pYM2612, (port << 1) | 0, address);
		YM2612Write(m_pYM2612, (port << 1) | 1, value);
	}
}

void VGMPlayer::RenderSamplesYM2612(short* buffer, unsigned int numSamples)
{
	// Very similar to SN76489 above but with int instead of INT16 for the buffers
	if (m_pYM2612 == NULL)
	{
		return;
	}

	if (m_YM2612BufferLen < numSamples)
	{
		if (numSamples < 576)
		{
			numSamples = 576;
		}

		free(m_YM2612Buffers[0]);
		free(m_YM2612Buffers[1]);

		m_YM2612Buffers[0] = (int*)malloc(numSamples * sizeof(int));
		m_YM2612Buffers[1] = (int*)malloc(numSamples * sizeof(int));

		m_YM2612BufferLen = numSamples;
	}

	YM2612UpdateOne(m_pYM2612, m_YM2612Buffers, numSamples);

	for (unsigned int i = 0; i < numSamples; ++i)
	{
		buffer[i * 2 + 0] += m_YM2612Buffers[0][i];
		buffer[i * 2 + 1] += m_YM2612Buffers[1][i];
	}
}

void VGMPlayer::StartYM2413()
{
	if (m_pYM2413 != NULL || m_VGMHeader.getYM2413Clock() == 0)
	{
		return;
	}

	m_pYM2413 = OPLL_new(m_VGMHeader.getYM2413Clock(), m_sampleRate);
//	OPLL_set_quality(this->m_pYM2413, 1); // seems broken to me...
	OPLL_reset(m_pYM2413);
}

void VGMPlayer::StopYM2413()
{
	if (m_pYM2413 == NULL)
	{
		return;
	}

	OPLL_delete(m_pYM2413);
	m_pYM2413 = NULL;
}

void VGMPlayer::ResetYM2413()
{
	if (m_pYM2413 != NULL)
	{
		OPLL_reset(m_pYM2413);
	}
}

void VGMPlayer::WriteYM2413(int address, int value)
{
	StartYM2413();
	if (m_pYM2413 != NULL)
	{
		OPLL_writeReg(m_pYM2413, address, value);
	}
}

void VGMPlayer::RenderSamplesYM2413(short* buffer, unsigned int numSamples)
{
	// EMU2413 doesn't support multi-sample generation 
	// so we don't need intermediate buffers
	if (m_pYM2413)
	{
		int pair[2];
		for (unsigned int i = 0; i < numSamples; ++i)
		{
			OPLL_calc_stereo(m_pYM2413, pair);
			buffer[i * 2 + 0] += pair[0];
			buffer[i * 2 + 1] += pair[1];
		}
	}
}

#pragma endregion "Private (helper) functions"
