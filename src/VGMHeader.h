#ifndef VGMHeader_h__
#define VGMHeader_h__

#include <zlib.h>
#include "VGM.h"

class VGMHeaderObj
{
public:
	VGMHeaderObj();
	void loadFromFile(gzFile f);
	int getMajorVersion() const;
	int getMinorVersion() const;
	unsigned long getSN76489Clock() const;
	unsigned long getYM2413Clock() const;
	unsigned long getYM2612Clock() const;
	unsigned long getYM2151Clock() const;
	unsigned long getGD3Offset() const;
	unsigned long getTotalLengthInSamples() const;
	unsigned long getLoopOffset() const;
	unsigned long getLoopLengthInSamples() const;
	int getRecordingRate() const;
	int getSN76489WhiteNoiseFeedback() const;
	int getSN76489hiftRegisterWidth() const;
	unsigned long getVGMDataOffset() const;
	bool canLoop() const;
	unsigned long long getLoopedLengthInSamples(int numLoops);

private:
	VGMHeader m_header;
	bool m_IsValid;
	int UnBCD(unsigned char val) const;
	void throwIfInvalid() const;
};

#endif // VGMHeader_h__