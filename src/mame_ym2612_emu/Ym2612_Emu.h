// YM2612 FM sound chip emulator interface

// Game_Music_Emu 0.3.0
// modified by Maxim

#ifndef YM2612_EMU_H
#define YM2612_EMU_H

struct MAME_YM2612;

struct MAME_YM2612* MAME_YM2612Init( long baseclock, long rate );
void MAME_YM2612Shutdown( struct MAME_YM2612* );
void MAME_YM2612ResetChip( struct MAME_YM2612* );
void MAME_YM2612UpdateOne( struct MAME_YM2612*, int* out, int pair_count );
int MAME_YM2612Write( struct MAME_YM2612*, int a, unsigned char v );
//void YM2612Postload( struct MAME_YM2612* );
void MAME_YM2612Mute( struct MAME_YM2612*, int mask );

/*
// Set output sample rate and chip clock rates, in Hz. Returns non-zero
// if error.
const char* set_rate( struct MAME_YM2612* inst, double sample_rate, double clock_rate );

// Reset to power-up state
void reset( struct MAME_YM2612* inst );

// Mute voice n if bit n (1 << n) of mask is set
enum { channel_count = 6 };
void mute_voices( struct MAME_YM2612* inst, int mask );

// Write addr to register 0 then data to register 1
void write0( struct MAME_YM2612* inst, int addr, int data );

// Write addr to register 2 then data to register 3
void write1( struct MAME_YM2612* inst, int addr, int data );

// Run and add pair_count samples into current output buffer contents
typedef short sample_t;
enum { out_chan_count = 2 }; // stereo
void run( struct MAME_YM2612* inst, int pair_count, sample_t* out );
*/
#endif

