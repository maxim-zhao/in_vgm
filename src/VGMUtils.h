#ifndef VGMUtils_h__
#define VGMUtils_h__

#include "VGM.h"
#include "GD3.h"
#include <zlib.h>

#ifdef CPLUSPLUS
extern "C" {
#endif

// Caller must free
// Attempts to patch older headers where possible with sensible values
VGMHeader* ReadVGMHeader(gzFile f);
GD3Header* ReadGD3Header(gzFile f);

#ifdef CPLUSPLUS
}
#endif

#endif // VGMUtils_h__