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

unsigned short int  ToneFreqs[4];		// Frequency register values (total)
  signed short int  ToneFreqVals[4];	// Frequency register values (counters)
  signed       char ToneFreqPos[4];		// Frequency channel flip-flops
  signed short int  Channels[4];		// Value of each channel, before stereo is applied
unsigned       char PSGVolumes[4];		// Volume value (0..15)
  signed long  int	IntermediatePos[4]; // intermediate values used at boundaries between + and -

static unsigned short int NoiseShiftRegister;
static int WhiteNoise;

static unsigned char NumClocksForSample;

static char Active = 0; // Set to true by SN76489_Init(), if false then all procedures exit immediately

//------------------------------------------------------------------------------
void SN76489_Init(const unsigned long PSGClockValue,const unsigned long SamplingRate)
{
	int i;

	Active=(PSGClockValue>0);	// probably unnecessarily verbose

	if (!Active) return;

	dClock=(float)PSGClockValue/16/SamplingRate;
	PSGFrequencyLowBits=0;
	Channel=4;
	PSGStereo=0xff;
	for (i=0;i<=3;i++) {
		// Set volumes to off (0xf)
		PSGVolumes[i]=0xf;
		// Set all frequencies to 1
		ToneFreqs[i]=1;
		// Set counters to 0
		ToneFreqVals[i]=0;
		// Set flip-flops to 0
		ToneFreqPos[i]=0;
		// Set intermediate positions to do-not-use value
		IntermediatePos[i]=LONG_MIN;
	};
	// Special cases for the noise channel
	ToneFreqPos[3]=1;
	ToneFreqs[3]=16;	// These 2 found by testing a real SMS2
	WhiteNoise=0;		// (equivalent to relevant bits = 0)
	// Initialise noise generator
	NoiseShiftRegister=NoiseInitialState;
	// Zero clock
	Clock=0;
}


//------------------------------------------------------------------------------
void SN76489_Write(const unsigned char data)
{
	if (!Active) return;
	switch (data & 0x90) {
	case 0x00:	// fall through
	case 0x10:	// second frequency byte
		if (Channel!=4) {
		    ToneFreqs[Channel]=(data & 0x3F) << 4 | PSGFrequencyLowBits; // Set frequency register
			if (Channel!=3) {
				// If the flip-flop was at 0, set it to +1
				if (!ToneFreqPos[Channel]) ToneFreqPos[Channel]=1;
				// Frequency 0 is changed to 1 internally
				if (!ToneFreqs[Channel]) ToneFreqs[Channel]=1;
			} else {	// Last write was the noise channel
				// So treat this value as a noise write
			    ToneFreqs[3] = 0x10 << (data & 0x3);	// set shift rate
				WhiteNoise=(data & 0x4);				// set feedback type
				NoiseShiftRegister=NoiseInitialState;	// reset register
			};
		};
		break;
	case 0x80:
		if ((data & 0x60) == 0x60) {				// noise
		    ToneFreqs[3] = 0x10 << (data & 0x3);	// set shift rate
			WhiteNoise=(data & 0x4);				// set feedback type
			NoiseShiftRegister=NoiseInitialState;	// reset register
			Channel=3;
		} else {								// First frequency byte
			Channel=(data & 0x60) >> 5;			// select channel
			PSGFrequencyLowBits = data & 0xF;	// remember frequency data
		};
		break;
	case 0x90:
		PSGVolumes[(data & 0x60) >> 5] = data & 0xF;	// set volume
		Channel=4;
		break;
	}; // end case
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
			Channels[i]=(PSGMute >> i & 0x1)*PSGVolumeValues[PSGVolumes[i]]*IntermediatePos[i]/65536;
		else
			Channels[i]=(PSGMute >> i & 0x1)*PSGVolumeValues[PSGVolumes[i]]*ToneFreqPos[i];

	Channels[3]=(short)((PSGMute >> 3 & 0x1)*PSGVolumeValues[PSGVolumes[3]]*(NoiseShiftRegister & 0x1));

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
	if (ToneFreqs[3]==128) ToneFreqVals[3]=ToneFreqVals[2];
	else ToneFreqVals[3]-=NumClocksForSample;

// Value below which PSG does not output
#define PSG_CUTOFF 0x6

	// Tone channels:
	for (i=0;i<=2;++i) {
		if (ToneFreqVals[i]<=0) {	// If it gets below 0...
			if (ToneFreqs[i]>PSG_CUTOFF) {
				// Calculate how much of the sample is + and how much is -
				// Go to floating point and include the clock fraction for extreme accuracy :D
				// Store as long int, maybe it's faster? I'm not very good at this
				IntermediatePos[i]=(long)((NumClocksForSample-Clock+2*ToneFreqVals[i])*ToneFreqPos[i]/(NumClocksForSample+Clock)*65536);
				ToneFreqPos[i]=-ToneFreqPos[i];	// Flip the flip-flop
			} else {
				ToneFreqPos[i]=1;	// stuck value
				IntermediatePos[i]=LONG_MIN;
			};
			ToneFreqVals[i]+=ToneFreqs[i]*(NumClocksForSample/ToneFreqs[i]+1);
		} else IntermediatePos[i]=LONG_MIN;
	};

	// Noise channel
	if (ToneFreqVals[3]<=0) {	// If it gets below 0...
		ToneFreqPos[3]=-ToneFreqPos[3];	// Flip the flip-flop
		if (!ToneFreqs[3]) ToneFreqs[3]=1;	// 0 state not allowed
		ToneFreqVals[3]+=ToneFreqs[3]*(NumClocksForSample/ToneFreqs[3]+1);
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
			int Feedback=0;
			if (WhiteNoise) Feedback=((NoiseShiftRegister&0x9) && (NoiseShiftRegister&0x9^0x9));
			else Feedback=NoiseShiftRegister&1;	// For periodic: feedback=output
			NoiseShiftRegister=(NoiseShiftRegister>>1) | (Feedback<<15);
		};
	};
};
