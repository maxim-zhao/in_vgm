/*
**
** File: fm_ym2612.h -- header for ym2612.c
** software implementation of Yamaha FM sound generator
**
** Copyright (C) 2001, 2002, 2003 Jarek Burczynski (bujar at mame dot net)
** Copyright (C) 1998 Tatsuyuki Satoh , MultiArcadeMachineEmulator development
**
** Version 1.4 (final beta)
**
*/

#ifndef _H_FM_FM_
#define _H_FM_FM_

/* compiler dependence */
#ifndef INLINE
#define INLINE static __forceinline
#endif
#ifndef INT32
#define INT32 int
#endif
#ifndef UINT32
#define UINT32 unsigned int
#endif
#ifndef UINT8
#define UINT8 unsigned char
#endif


void* YM2612Init(int baseclock, int rate);
void YM2612Free(void* ym2612);
int YM2612ResetChip(void* ym2612);
void YM2612UpdateOne(void* ym2612,int **buffer, int length);
int YM2612Write(void* ym2612,unsigned char a, unsigned char v);
int YM2612Read(void* ym2612);


#endif /* _H_FM_FM_ */
