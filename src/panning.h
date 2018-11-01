/*
	panning.h by Maxim in 2006
	Implements "simple equal power" panning using sine distribution
	I am not an expert on this stuff, but this is the best sounding of the methods I've tried
*/

#ifndef PANNING_H
#define PANNING_H

void calc_panning(float channels[2], int position);
void centre_panning(float *channels);
double gauss_rand();
int random_stereo();

#define PANNING_MIN 0
#define PANNING_MAX 255
#define PANNING_CENTRE 127

#endif