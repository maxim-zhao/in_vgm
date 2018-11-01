/*
	panning.h by Maxim
	Implements "simple equal power" panning using sine distribution
	I am not an expert on this stuff, but this is the best sounding of the methods I've tried
*/

#ifndef PANNING_H
#define PANNING_H

// Sets the members of channels[] to relative volume levels
// so that scaling the left channel by channels[0] and the right by channels[1]
// will make it sound of equal loudness but in the given position
// where PANNING_LEFT is leftmost and PANNING_RIGHT is rightmost
void calcPanning(float channels[2], int position);

// Equivalent to calcPanning(channels, PANNING_CENTRE) but much faster
void centrePanning(float channels[2]);

// Returns a Gaussian random position centred on PANNING_CENTRE and with
// variance 20% of the range
int randomStereo();

#define PANNING_LEFT 0
#define PANNING_RIGHT 254
#define PANNING_CENTRE ((PANNING_RIGHT + PANNING_LEFT) / 2)

#endif