#ifndef __APPLY_GAIN_H
#define __APPLY_GAIN_H

void apply_replay_gain_16bit(float gain, float peak, short int samples[], int numsamples, int channels, int reduce_gain_on_clipping);

#endif __APPLY_GAIN_H
