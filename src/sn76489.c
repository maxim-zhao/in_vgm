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
#define NoiseInitialState 0x4000
#define NoiseWhiteFeedback 0x0009

// These values are taken from a real SMS2's output
static const unsigned short int PSGVolumeValues[16] = {892,892,892,760,623,497,404,323,257,198,159,123,96,75,60,0};

// Variables
static float
  Clock,
  dClock;
static unsigned char
  PSGFrequencyLowBits,
  Channel,
  PSGStereo;

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

static unsigned char NumClocksForSample;

static char Active = 0; // Set to true by SN76489_Init(), if false then all procedures exit immediately

//------------------------------------------------------------------------------
void SN76489_Init(const unsigned long PSGClockValue,const unsigned long SamplingRate)
{
	int i;

	Active=(PSGClockValue>0);

	if (!Active) return;

	dClock=(float)PSGClockValue/16/SamplingRate;
	PSGFrequencyLowBits=0;
	Channel=4;
	PSGStereo=0xff;
	for (i=0;i<=3;i++) {
/*
		// Set volumes to off (0xf)
		PSGVolumes[i]=0xf;
		// Set all frequencies to 1
		ToneFreqs[i]=1;
*/
		// Initialise PSG state
		Registers[2*i]=1;		// tone
		Registers[2*i+1]=0xf;	// vol
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
			Channels[i]=(PSGMute >> i & 0x1)*PSGVolumeValues[Registers[2*i+1]]*IntermediatePos[i]/65536;
		else
			Channels[i]=(PSGMute >> i & 0x1)*PSGVolumeValues[Registers[2*i+1]]*ToneFreqPos[i];

	Channels[3]=(short)((PSGMute >> 3 & 0x1)*PSGVolumeValues[Registers[7]]*(NoiseShiftRegister & 0x1));

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

	// Noise channel
	if (ToneFreqVals[3]<=0) {	// If it gets below 0...
		ToneFreqPos[3]=-ToneFreqPos[3];	// Flip the flip-flop
		if (NoiseFreq!=0x80)			// If not matching tone2, decrement counter
			ToneFreqVals[3]+=NoiseFreq*(NumClocksForSample/NoiseFreq+1);
		if (ToneFreqPos[3]==1) {	// Only once per cycle...

			// General method:
			/*
			int Feedback=0;
			if (WhiteNoise) {	// For white noise:
				int i;			// Calculate the XOR of the tapped bits for feedback
				unsigned short int tapped=NoiseShiftRegister&NoiseWhiteFeedback;
				for (i=0;i<16;++i) Feedback+=(tapped>>i)&1;
				Feedback&=1;
			} else Feedback=NoiseShiftRegister&1;	// For periodic: feedback=output
			NoiseShiftRegister=(NoiseShiftRegister>>1) | (Feedback<<15);
			*/

			// SMS-only method, probably a bit faster:
			// Verbose:
			/*
			int Feedback=0;
			if (Registers[6]&0x4) Feedback=((NoiseShiftRegister&0x9) && (NoiseShiftRegister&0x9^0x9));
			else Feedback=NoiseShiftRegister&1;	// For periodic: feedback=output
			NoiseShiftRegister=(NoiseShiftRegister>>1) | (Feedback<<15);
			*/
			// Obfucated:
			NoiseShiftRegister=(NoiseShiftRegister>>1) | ((Registers[6]&0x4?((NoiseShiftRegister&0x9) && (NoiseShiftRegister&0x9^0x9)):NoiseShiftRegister&1)<<15);
		};
	};
};
