/*

  SN76489 emulation
  by Maxim in 2001 and 2002
  converted from my original Delphi implementation

  I'm a C newbie so I'm sure there are loads of stupid things
  in here which I'll come back to some day and redo

  Includes:
  - Super-high quality tone channel "oversampling" by calculating fractional positions on transitions
  - Noise output pattern reverse engineered from actual SMS output
  - Volume levels taken from actual SMS output

*/

#include <limits.h>
#include "sn76489.h"

// Constants
#define NoiseInitialState  0x8000
//#define NoiseWhiteFeedback 0x0009

// These values are taken from a real SMS2's output
static const int PSGVolumeValues[2][16] = {
	{892,892,892,760,623,497,404,323,257,198,159,123,96,75,60,0}, // I can't remember why 892... :P some scaling I did at some point
	{892,774,669,575,492,417,351,292,239,192,150,113,80,50,24,0}
};

// Variables
static float
  Clock,
  dClock;
static int
  PSGFrequencyLowBits,
  Channel,
  PSGStereo,
  NumClocksForSample,
  Active=0,		// Set to true by SN76489_Init(), if false then all procedures exit immediately
  WhiteNoiseFeedback;

// PSG registers:
static unsigned short int  Registers[8];		// Tone, vol x4
static                int  LatchedRegister;
static unsigned short int  NoiseShiftRegister;
static   signed short int  NoiseFreq;			// Noise channel signal generator frequency


// Output calculation variables
static   signed short int  ToneFreqVals[4];		// Frequency register values (counters)
static   signed       char ToneFreqPos[4];		// Frequency channel flip-flops
static   signed short int  Channels[4];			// Value of each channel, before stereo is applied
static   signed long  int  IntermediatePos[4];	// intermediate values used at boundaries between + and -


//------------------------------------------------------------------------------
void SN76489_Init(const unsigned long PSGClockValue,const unsigned long SamplingRate,const int FeedbackPattern)
{
	int i;

	Active=(PSGClockValue>0);

	if (!Active) return;

	WhiteNoiseFeedback=FeedbackPattern;

	dClock=(float)PSGClockValue/16/SamplingRate;
	PSGFrequencyLowBits=0;
	Channel=4;
	PSGStereo=0xff;
	for (i=0;i<=3;i++) {
		// Initialise PSG state
		Registers[2*i]=1;		// tone freq=1
		Registers[2*i+1]=0xf;	// vol=off
		NoiseFreq=0x10;

		// Set counters to 0
		ToneFreqVals[i]=0;
		// Set flip-flops to 1
		ToneFreqPos[i]=1;
		// Set intermediate positions to do-not-use value
		IntermediatePos[i]=LONG_MIN;
	};
	LatchedRegister=0;
	// Initialise noise generator
	NoiseShiftRegister=NoiseInitialState;
	// Zero clock
	Clock=0;
}


//------------------------------------------------------------------------------
void SN76489_Write(const unsigned char data)
{
	if (!Active) return;
	if (data&0x80) {
		// Latch/data byte	%1 cc t dddd
		LatchedRegister=((data>>4)&0x07);
		Registers[LatchedRegister]=
			(Registers[LatchedRegister] & 0x3f0)	// zero low 4 bits
			| (data&0xf);							// and replace with data
	} else {
		// Data byte		%0 - dddddd
		if (!(LatchedRegister%2)&&(LatchedRegister<5))
			// Tone register
			Registers[LatchedRegister]=
				(Registers[LatchedRegister] & 0x00f)	// zero high 6 bits
				| ((data&0x3f)<<4);						// and replace with data
		else
			// Other register
			Registers[LatchedRegister]=data&0x0f;		// Replace with data
	};
	switch (LatchedRegister) {
	case 0:
	case 2:
	case 4:	// Tone channels
		if (Registers[LatchedRegister]==0) Registers[LatchedRegister]=1;	// Zero frequency changed to 1 to avoid div/0
		break;
	case 6:	// Noise
		NoiseShiftRegister=NoiseInitialState;	// reset shift register
		NoiseFreq=0x10<<(Registers[6]&0x3);		// set noise signal generator frequency
		break;
	};
};

//------------------------------------------------------------------------------
void SN76489_GGStereoWrite(const unsigned char data)
{
	if (!Active) return;
	PSGStereo=data;
};

//------------------------------------------------------------------------------
void SN76489_GetValues(int *left,int *right)
{
	int i;
	if (!Active) return;

	for (i=0;i<=2;++i)
		if (IntermediatePos[i]!=LONG_MIN)
			Channels[i]=(short)((SN76489_Mute >> i & 0x1)*PSGVolumeValues[SN76489_VolumeArray][Registers[2*i+1]]*IntermediatePos[i]/65536);
		else
			Channels[i]=(SN76489_Mute >> i & 0x1)*PSGVolumeValues[SN76489_VolumeArray][Registers[2*i+1]]*ToneFreqPos[i];

	Channels[3]=(short)((SN76489_Mute >> 3 & 0x1)*PSGVolumeValues[SN76489_VolumeArray][Registers[7]]*(NoiseShiftRegister & 0x1));

	if (SN76489_BoostNoise) Channels[3]<<=1; // Double noise volume to make some people happy

	*left =0;
	*right=0;
	for (i=0;i<=3;++i) {
		*left +=(PSGStereo >> (i+4) & 0x1)*Channels[i];	// left
		*right+=(PSGStereo >>  i    & 0x1)*Channels[i];	// right
	};

	Clock+=dClock;
	NumClocksForSample=(int)Clock;	// truncates
	Clock-=NumClocksForSample;	// remove integer part
	// Looks nicer in Delphi...
	//  Clock:=Clock+dClock;
	//  NumClocksForSample:=Trunc(Clock);
	//  Clock:=Frac(Clock);

	// Decrement tone channel counters
	for (i=0;i<=2;++i)
		ToneFreqVals[i]-=NumClocksForSample;
	
	// Noise channel: match to tone2 or decrement its counter
	if (NoiseFreq==0x80) ToneFreqVals[3]=ToneFreqVals[2];
	else ToneFreqVals[3]-=NumClocksForSample;

// Value below which PSG does not output
#define PSG_CUTOFF 0x6

	// Tone channels:
	for (i=0;i<=2;++i) {
		if (ToneFreqVals[i]<=0) {	// If it gets below 0...
			if (Registers[i*2]>PSG_CUTOFF) {
				// Calculate how much of the sample is + and how much is -
				// Go to floating point and include the clock fraction for extreme accuracy :D
				// Store as long int, maybe it's faster? I'm not very good at this
				IntermediatePos[i]=(long)((NumClocksForSample-Clock+2*ToneFreqVals[i])*ToneFreqPos[i]/(NumClocksForSample+Clock)*65536);
				ToneFreqPos[i]=-ToneFreqPos[i];	// Flip the flip-flop
			} else {
				ToneFreqPos[i]=1;	// stuck value
				IntermediatePos[i]=LONG_MIN;
			};
			ToneFreqVals[i]+=Registers[i*2]*(NumClocksForSample/Registers[i*2]+1);
		} else IntermediatePos[i]=LONG_MIN;
	};
/*
The player works by calculating the number of PSG clocks per sample (dClock).
For each sample, it renders an integer number of PSG clocks (NumClocksForSample)
and any fraction is saved (Clock) and added into the next sample's number of
clocks. This "saved" fraction is just making sure that the PSG clock flips the
right number of times since for some samples it has to flip more times to stay
at the right speed. The rendered sample still reflects the whole period.

Number of PSG clocks for this sample = dClock (float)
Integer part of that = NumClocksForSample
Fractional part of that = Clock

ToneFreqVals[i] = underflowed sample counter for channel i, ie. a negative integer

|   |   |   |   |   |   |   |   | PSG clocks
  |                            |  Sample boundaries
  .                            .
----------------+              .
  .             |              .  Waveform
  .             |              .
  .             +----------------
  .             .              .
  |   |   |   | . |   |   |   ||  Re-aligned PSG clocks
  <------ A ---->             ..
  .           . <----- B ------>
  .           .               ..
  <---- NumClocksForSample --->.
  .           .               >< Clock
              <------ D ------>

If the first half-wave has sign S, the average value over the sample is:
   A*S+B*(-S)   (A-B)*S
   ---------- = -------
      A+B         A+B

A+B = NumClocksForSample+Clock (integer + fraction of aligned PSG clocks)

The half-wave counter will have just overflowed so that

D = -ToneFreqVals[i]

A = NumClocksForSample-D
  = NumClocksForSample+ToneFreqVals[i]
B = NumClocksForSample+Clock-A
  = NumClocksForSample+Clock-(NumClocksForSample+ToneFreqVals[i])
  = Clock-ToneFreqVals[i]
S = ToneFreqPos[i]

Thus,
                (NumClocksForSample+ToneFreqVals[i]) - (Clock-ToneFreqVals[i]) * ToneFreqPos[i]
Average value = -------------------------------------------------------------------------------
                                           NumClocksForSample+Clock

              = ((NumClocksForSample-Clock+2*ToneFreqVals[i])*ToneFreqPos[i])/(float)(NumClocksForSample+Clock)

However, as a small and probably useless optimisation, I try to avoid storing
this in floating point by using fixed point; I do the calculation (I expect it
is performed in double precision), multiply the result by 64K and round to an
integer. When using this "intermediate position" value, the resulting sample
is divided by 64K to get back to the correct value (ie. I avoid a floating-
*/

	// Noise channel
	if (ToneFreqVals[3]<=0) {	// If it gets below 0...
		ToneFreqPos[3]=-ToneFreqPos[3];	// Flip the flip-flop
		if (NoiseFreq!=0x80)			// If not matching tone2, decrement counter
			ToneFreqVals[3]+=NoiseFreq*(NumClocksForSample/NoiseFreq+1);
		if (ToneFreqPos[3]==1) {	// Only once per cycle...
			int Feedback;
			if (Registers[6]&0x4) {	// White noise
				// Calculate parity of fed-back bits for feedback
				switch (WhiteNoiseFeedback) {
					// Do some optimised calculations for common (known) feedback values
				case 0x0006:	// SC-3000		%00000110
				case 0x0009:	// SMS, GG, MD	%00001001
					// If two bits fed back, I can do Feedback=(nsr & fb) && (nsr & fb ^ fb)
					// since that's (one or more bits set) && (not all bits set)
					Feedback=((NoiseShiftRegister&WhiteNoiseFeedback) && (NoiseShiftRegister&WhiteNoiseFeedback^WhiteNoiseFeedback));
					break;
				case 0x8005:	// BBC Micro
					// fall through :P can't be bothered to think too much
				default:		// Default handler for all other feedback values
					Feedback=NoiseShiftRegister&WhiteNoiseFeedback;
					Feedback^=Feedback>>8;
					Feedback^=Feedback>>4;
					Feedback^=Feedback>>2;
					Feedback^=Feedback>>1;
					Feedback&=1;
					break;
				};
			} else		// Periodic noise
				Feedback=NoiseShiftRegister&1;

			NoiseShiftRegister=(NoiseShiftRegister>>1) | (Feedback<<15);

// Original code:
//			NoiseShiftRegister=(NoiseShiftRegister>>1) | ((Registers[6]&0x4?((NoiseShiftRegister&0x9) && (NoiseShiftRegister&0x9^0x9)):NoiseShiftRegister&1)<<15);
		};
	};
};
