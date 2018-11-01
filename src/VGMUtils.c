#include <stdlib.h>
#include <string.h>
#include "VGMUtils.h"

VGMHeader* ReadVGMHeader(gzFile f)
{
	int i;
	VGMHeader* header = (VGMHeader*)malloc(sizeof(VGMHeader));

	if (!header)
	{
		return NULL; // failed to allocate it
	}

	i = gzread(f, header, sizeof(VGMHeader));

	// Check for some basic stuff in case this is not a valid VGM file
	if ((i < sizeof(VGMHeader)) // file too short/error reading
		|| strncmp(header->VGMIdent, VGMIDENT, VGMIDENT_LEN) != 0) // no marker
	{
		free(header);
		return NULL;
	}

	// Patches
	// VGM 1.01 added rate scaling. The default value of 0 is best left alone.
	// VGM 1.10 added per-chip clocks (the YM2413Clock position held a global value)
	// and configurable SN76489 white noise settings (default to Sega settings).
	if (header->Version < 0x0110)
	{
		header->YM2151Clock = header->YM2612Clock = header->YM2413Clock;
		header->PSGShiftRegisterWidth = DEFAULTPSGSHIFTREGISTERWIDTH;
		header->PSGWhiteNoiseFeedback = DEFAULTPSGWHITENOISEFEEDBACK;
	}
	// VGM 1.50 added a configurable data offset (used to be assumed to follow the header at 0x40),
	// but it can be left at zero
	if (header->VGMDataOffset == 0)
	{
		header->VGMDataOffset = DEFAULTVGMDATAOFFSET;
	}

	// Sanity checks
	if ((header->VGMDataOffset + VGMDATADELTA < 0x40)
		|| ((header->LoopOffset > 0) && (header->LoopOffset + LOOPDELTA < 0x40))
		|| (header->LoopLength > header->TotalLength))
	{
		// looks loony
		free(header);
		return NULL;
	}

	return header;
}

GD3Header* ReadGD3Header(gzFile f)
{
	int i;
	GD3Header* header = (GD3Header*)malloc(sizeof(GD3Header));

	if (!header)
	{
		return NULL; // failed to allocate it
	}

	i = gzread(f, header, sizeof(GD3Header));

	// Check for some basic stuff in case this is not a valid GD3 header
	if ((i < sizeof(GD3Header)) // file too short/error reading
		|| strncmp(header->GD3Ident, GD3IDENT, GD3IDENT_LEN) != 0) // no marker
	{
		free(header);
		return NULL;
	}

	return header;
}
