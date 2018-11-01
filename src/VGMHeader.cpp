#include "VGMHeader.h"
#include <cstring>
#include <exception>

VGMHeaderObj::VGMHeaderObj()
: m_IsValid(false)
{}

void VGMHeaderObj::loadFromFile(gzFile f)
{
	m_IsValid = true;
	int i = gzread(f, &m_header, sizeof(VGMHeader));

	// Check for some basic stuff in case this is not a valid VGM file
	if ((i < sizeof(VGMHeader)) // file too short/error reading
		|| strncmp(m_header.VGMIdent, VGMIDENT, VGMIDENT_LEN) != 0) // no marker
	{
		m_IsValid = false;
		throwIfInvalid();
	}

	// Patches
	// VGM 1.01 added rate scaling. The default value of 0 is best left alone.
	// VGM 1.10 added per-chip clocks (the YM2413Clock position held a global value)
	// and configurable SN76489 white noise settings (default to Sega settings).
	if (m_header.Version < 0x0110)
	{
		m_header.YM2151Clock = m_header.YM2612Clock = m_header.YM2413Clock;
		m_header.PSGShiftRegisterWidth = DEFAULTPSGSHIFTREGISTERWIDTH;
		m_header.PSGWhiteNoiseFeedback = DEFAULTPSGWHITENOISEFEEDBACK;
	}
	// VGM 1.50 added a configurable data offset (used to be assumed to follow the header at 0x40),
	// but it can be left at zero
	if (m_header.VGMDataOffset == 0)
	{
		m_header.VGMDataOffset = DEFAULTVGMDATAOFFSET;
	}

	// Patch things up from relative to absolute offsets
	m_header.EoFOffset += EOFDELTA;
	m_header.VGMDataOffset += VGMDATADELTA;
	if (m_header.GD3Offset > 0)
	{
		m_header.GD3Offset += GD3DELTA;
	}
	if (m_header.LoopOffset > 0)
	{
		m_header.LoopOffset += LOOPDELTA;
	}
	
	// Sanity checks
	// We don't check EoFOffset because it's probably (?) screwed on many existing files
	if ((m_header.VGMDataOffset < 0x40)
		|| ((m_header.LoopOffset > 0) && (m_header.LoopOffset < 0x40))
		|| (m_header.LoopLength > m_header.TotalLength)
		|| ((m_header.GD3Offset > 0) && (m_header.GD3Offset < 0x40))
		|| ((m_header.LoopLength > 0) && (m_header.LoopOffset == 0))
		|| ((m_header.LoopOffset > 0) && (m_header.LoopLength == 0))
		)
	{
		// looks loony
		m_IsValid = false;
		throwIfInvalid();
	}
}

int VGMHeaderObj::UnBCD(unsigned char val) const
{
	int l = val & 0xf;
	int h = val >> 4;
	if ((h > 9) || (l > 9))
	{
		return -1;
	}
	return (h * 10 + l);
}

int VGMHeaderObj::getMajorVersion() const
{
	throwIfInvalid();
	return UnBCD((unsigned char)(m_header.Version >> 8));
}

int VGMHeaderObj::getMinorVersion() const
{
	throwIfInvalid();
	return UnBCD((unsigned char)(m_header.Version & 0xff));
}

unsigned long VGMHeaderObj::getSN76489Clock() const
{
	throwIfInvalid();
	return m_header.PSGClock;
}

unsigned long VGMHeaderObj::getYM2413Clock() const
{
	throwIfInvalid();
	return m_header.YM2413Clock;
}

unsigned long VGMHeaderObj::getVGMDataOffset() const
{
	throwIfInvalid();
	return m_header.VGMDataOffset;
}

unsigned long VGMHeaderObj::getYM2612Clock() const
{
	throwIfInvalid();
	return m_header.YM2612Clock;
}

unsigned long VGMHeaderObj::getYM2151Clock() const
{
	throwIfInvalid();
	return m_header.YM2151Clock;
}

unsigned long VGMHeaderObj::getGD3Offset() const
{
	throwIfInvalid();
	return m_header.GD3Offset;
}

unsigned long VGMHeaderObj::getTotalLengthInSamples() const
{
	throwIfInvalid();
	return m_header.TotalLength;
}

unsigned long VGMHeaderObj::getLoopOffset() const
{
	throwIfInvalid();
	return m_header.LoopOffset;
}

unsigned long VGMHeaderObj::getLoopLengthInSamples() const
{
	throwIfInvalid();
	return m_header.LoopLength;
}

int VGMHeaderObj::getRecordingRate() const
{
	throwIfInvalid();
	return m_header.RecordingRate;
}

int VGMHeaderObj::getSN76489WhiteNoiseFeedback() const
{
	throwIfInvalid();
	return m_header.PSGWhiteNoiseFeedback;
}

int VGMHeaderObj::getSN76489hiftRegisterWidth() const
{
	throwIfInvalid();
	return m_header.PSGShiftRegisterWidth;
}

void VGMHeaderObj::throwIfInvalid() const
{
	if (!m_IsValid)
	{
		throw std::exception("Invalid header");
	}
}

bool VGMHeaderObj::canLoop() const
{
	throwIfInvalid();
	return m_header.LoopOffset > 0 && m_header.LoopOffset > 0;
}

unsigned long long VGMHeaderObj::getLoopedLengthInSamples(int numLoops)
{
	if (canLoop())
	{
		return m_header.TotalLength + (numLoops - 1) * (long long)m_header.LoopLength;
	}
	else
	{
		return m_header.TotalLength;
	}
}
