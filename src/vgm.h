// VGM file format definitions

#ifndef VGM_H
#define VGM_H

// VGM data bytes
#define VGM_GGST        0x4f
#define VGM_PSG         0x50
#define VGM_YM2413      0x51
#define VGM_YM2612_0    0x52
#define VGM_YM2612_1    0x53
#define VGM_YM2151      0x54
#define VGM_PAUSE_WORD  0x61
#define VGM_PAUSE_60TH  0x62
#define VGM_PAUSE_50TH  0x63
#define VGM_END         0x66
#define VGM_DATA_BLOCK  0x67
#define VGM_YM2612_PCM_SEEK 0xe0
// Also:
// 0x7n = pause n+1
// 0x8n = output YM2612 PCM byte and pause n

// Reserved VGM ranges (inclusive)
#define VGM_RESERVED_1_PARAM_BEGIN  0x30
#define VGM_RESERVED_1_PARAM_END    0x4e
#define VGM_RESERVED_2_PARAMS_BEGIN 0xa0
#define VGM_RESERVED_2_PARAMS_END   0xbf
#define VGM_RESERVED_3_PARAMS_BEGIN 0xc0
#define VGM_RESERVED_3_PARAMS_END   0xdf
#define VGM_RESERVED_4_PARAMS_BEGIN 0xe1
#define VGM_RESERVED_4_PARAMS_END   0xff

#define VGM_DATA_BLOCK_YM2612_PCM 0x00


#define VGMIDENT 0x206d6756 // "Vgm " - endian-unsafe!

typedef struct 
{
  unsigned long VGMIdent;               // "Vgm "
  unsigned long EoFOffset;              // relative offset (from this point, 0x04) of the end of file
  unsigned long Version;                // 0x00000110 for 1.10
  unsigned long PSGClock;               // typically 3579545, 0 for no PSG
  unsigned long YM2413Clock;            // typically 3579545, 0 for no YM2413
  unsigned long GD3Offset;              // relative offset (from this point, 0x14) of the GD3 tag, 0 if not present
  unsigned long TotalLength;            // in samples
  unsigned long LoopOffset;             // relative again (to 0x1c), 0 if no loop
  unsigned long LoopLength;             // in samples, 0 if no loop
  unsigned long RecordingRate;          // in Hz, for speed-changing, 0 for no changing
  unsigned short PSGWhiteNoiseFeedback; // Feedback pattern for white noise generator; if <=v1.01, substitute default of 0x0009
  unsigned char PSGShiftRegisterWidth;  // Shift register width for noise channel; if <=v1.01, substitute default of 16
  unsigned char Reserved;
  unsigned long YM2612Clock;            // typically 3579545, 0 for no YM2612
  unsigned long YM2151Clock;            // typically 3579545, 0 for no YM2151
  unsigned long VGMDataOffset;          // if 0 assume 0x0c relative offset = 0x40 absolute
} TVGMHeader;

#define EOFDELTA  0x04
#define GD3DELTA  0x14
#define LOOPDELTA 0x1c
#define VGMDATADELTA 0x34

// Size of registers array for YM2413 - uses 0x00-0x38, with some gaps
#define YM2413NumRegs 0x39

// Size of registers array for YM2612 - uses 0x21-0xb7, with some gaps, twice (use high 1 for channel 1, high 0 for channel 0)
#define YM2612NumRegs 0x1b7

// Frequency value below which the PSG is silent
#define PSGCutoff 6

// Lengths of pauses in samples
#define LEN60TH 735
#define LEN50TH 882

enum chip_types
{
	VGM_CHIP_SN76489 = 0,
	VGM_CHIP_YM2413,
	VGM_CHIP_YM2612,
	VGM_CHIP_YM2151,
	VGM_CHIP_COUNT
};

#endif