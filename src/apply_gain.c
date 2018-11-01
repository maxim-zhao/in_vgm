#include <math.h>

/* hard limiter still very slow, i know */

#define HL_FLOATS

void apply_replay_gain_16bit(float gain, float peak, short int samples[], int numsamples, int channels, int reduce_gain_on_clipping)
{
	int i;
	float scale = (float)pow(10.0, gain/20);
	int igain16;

	if(reduce_gain_on_clipping && (scale * peak > 1.0)) {
		// reduce gain to avoid clipping
		scale = 1.0f / peak;
	}

	// apply gain
	igain16 = (int)((scale * 65536)+0.5);

	for(i=0;i<numsamples*channels;i++) {
		samples[i] = (samples[i] * igain16) >> 16;
	}
}
