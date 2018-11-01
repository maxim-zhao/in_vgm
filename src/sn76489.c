/* 

  SN76489 emulation
  by Maxim in 2001
  converted from my original Delphi implementation

  I'm a C newbie so I'm sure there are loads of stupid things
  in here which I'll come back to some day and redo

*/

#include "sn76489.h"

// Constants
#define NoiseInitialState 0
#define NoiseWhiteFeedback 0x12000
#define NoiseSynchFeedback 0x8000  // 1 << 15

// These values are taken from a real SMS2's output
static const unsigned short int PSGVolumeValues[16] = {8028,8028,8028,6842,5603,4471,3636,2909,2316,1778,1427,1104,862,673,539,0};

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
  signed       int  IntermediatePos[4]; // intermediate values used at boundaried between + and -

unsigned long int NoiseShiftRegister,NoiseFeedback;

unsigned char NumClocksForSample;

static char Active = 0; // Set to true by SN76489_Init(), if false then all procedures exit immediately

//------------------------------------------------------------------------------
void SN76489_Init(const unsigned long PSGClockValue)
{
	int i;

	Active=(PSGClockValue>0);	// probably unnecessarily verbose

	if (!Active) return;

	dClock=(float)PSGClockValue/16/44100;
	PSGFrequencyLowBits=0;
	Channel=4;
	PSGStereo=0xff;
	for (i=0;i<=3;i++) {
		// Set volumes to 0
		PSGVolumes[i]=0;
		// Set all frequencies to 0
		ToneFreqs[i]=0;
		// Set counters to 0
		ToneFreqVals[i]=0;
		// Set flip-flops to 0
		ToneFreqPos[i]=0;
		IntermediatePos[i]=0;
	};
	// But not this one! It's not audible anyway, plus starting it at 0 messes up other stuff 
	ToneFreqPos[3]=1;
	// Initialise noise generator
	NoiseShiftRegister=NoiseInitialState;
	NoiseFeedback=0;
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
		if (!(Channel & 4)) {
		    ToneFreqs[Channel]=(data & 0x3F) << 4 | PSGFrequencyLowBits; // Set frequency register
			if (ToneFreqs[Channel]<4) ToneFreqs[Channel]=0;
			if (!ToneFreqPos[Channel]) ToneFreqPos[Channel]=1; // need the if, for when writes happen when pos=-1 (want to keep that)
		};
		break;
	case 0x80:
		if ((data & 0x60) == 0x60) {	// noise
		    ToneFreqs[3] = 0x10 << (data & 0x3);	// set shift rate
			NoiseFeedback=(data & 0x4?NoiseWhiteFeedback:NoiseSynchFeedback);	// set feedback type
			NoiseShiftRegister=NoiseInitialState;	// reset register
			Channel=4;
		} else {	// First frequency byte
			Channel=(data & 0x60) >> 5;	// select channel
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
void SN76489_WriteToBuffer(short int *buffer, const int position)
{
	int i;
	if (!Active) return;

	for (i=0;i<=2;++i)
		if (PSGOverSample & IntermediatePos[i])
			Channels[i]=(PSGMute >> i & 0x1)*PSGVolumeValues[PSGVolumes[i]]*ToneFreqPos[i]*IntermediatePos[i]/256;
		else
			Channels[i]=(PSGMute >> i & 0x1)*PSGVolumeValues[PSGVolumes[i]]*ToneFreqPos[i];

	Channels[3]=(short)((PSGMute >> 3 & 0x1)*PSGVolumeValues[PSGVolumes[3]]*(NoiseShiftRegister & 0x1));

	buffer[2*position  ]=0;
	buffer[2*position+1]=0;
	for (i=0;i<=3;++i) {
		buffer[2*position  ]+=(PSGStereo >> (i+4) & 0x1)*Channels[i];	// left
		buffer[2*position+1]+=(PSGStereo >>  i    & 0x1)*Channels[i];	// right
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
		if (ToneFreqs[i]!=0) ToneFreqVals[i]-=NumClocksForSample;
	
	// Noise channel: match to tone2 or decrement its counter
	if (ToneFreqs[3]==128) ToneFreqVals[3]=ToneFreqVals[2];
	else if (ToneFreqs[3]!=0) ToneFreqVals[3]-=NumClocksForSample;

	// Tone channels:
	for (i=0;i<=2;++i) {
		if (ToneFreqVals[i]<0) {	// If it gets below 0...
			// Calculate how much of the sample is + and how much is -
			if (PSGOverSample) IntermediatePos[i]=(NumClocksForSample+2*ToneFreqVals[i])*256/NumClocksForSample;
			ToneFreqPos[i]=-ToneFreqPos[i];	// Flip the flip-flop
			if (ToneFreqs[i]>0) do ToneFreqVals[i]+=ToneFreqs[i]; while (ToneFreqVals[i]<0);	// ...and increment it until it gets above 0 again
		} else IntermediatePos[i]=0;
	};

	// Noise channel
	if (ToneFreqVals[3]<0) {	// If it gets below 0...
		ToneFreqPos[3]=-ToneFreqPos[3];	// Flip the flip-flop
		if (ToneFreqs[3]>0) do ToneFreqVals[3]+=ToneFreqs[3]; while (ToneFreqVals[3]<0);	// ...and increment it until it gets above 0 again
		if (ToneFreqPos[3]==1) {	// Only once per cycle...
			if (NoiseShiftRegister==0) NoiseShiftRegister=1;	// zero state protection
			if (NoiseShiftRegister & 0x1)	// If the lowest bit is set...
				NoiseShiftRegister=NoiseShiftRegister >> 1 ^ NoiseFeedback;	// then shift and do the feedback
			else
				NoiseShiftRegister=NoiseShiftRegister >> 1;	// else just shift it
		};
	};
};
