#include <stdlib.h>
#include <math.h>
#include "panning.h"

#ifndef PI
#define PI 3.14159265359
#endif
#ifndef SQRT2
#define SQRT2 1.414213562
#endif
#define RANGE (PANNING_RIGHT - PANNING_LEFT)

// Set the panning values for the two stereo channels (L,R)
// for a position 0..127..254 L..C..R
void calcPanning(float channels[2], int position)
{
	// Equal power law: equation is
	// right = sin( position / range * pi / 2) * sqrt( 2 )
	// left is equivalent to right with position = range - position
	// position is in the range 0 .. RANGE
	// RANGE / 2 = centre, result = 1.0f
	if (position > PANNING_RIGHT)
	{
		position = PANNING_RIGHT;
	}
	else if (position < PANNING_LEFT)
	{
		position = PANNING_LEFT;
	}

	// convert to a 0-based scale 0..RANGE
	position -= PANNING_LEFT;

	channels[1] = (float)(sin((double)position / RANGE * PI / 2) * SQRT2);
	position = RANGE - position;
	channels[0] = (float)(sin((double)position / RANGE * PI / 2) * SQRT2);
}

// Reset the panning values to the centre position
void centrePanning(float channels[2])
{
	channels[0] = channels[1] = 1.0f;
}

// Generate a Gaussian random number with mean 0, variance 1
// Copied from an ancient C newsgroup FAQ
double gaussRand()
{
	static double V1, V2, S;
	static int phase = 0;
	double X;

	if (phase == 0) 
	{
		do 
		{
			double U1 = (double)rand() / RAND_MAX;
			double U2 = (double)rand() / RAND_MAX;

			V1 = 2 * U1 - 1;
			V2 = 2 * U2 - 1;
			S = V1 * V1 + V2 * V2;
		} 
		while (S >= 1 || S == 0);

		X = V1 * sqrt(-2 * log(S) / S);
	} 
	else
	{
		X = V2 * sqrt(-2 * log(S) / S);
	}

	phase = 1 - phase;

	return X;
}

// Generate a stereo position in the range PANNING_LEFT..PANNING_RIGHT
// with Gaussian distribution, mean PANNING_CENTRE, variance 20% of the range
int randomStereo()
{
	int n = (int)(PANNING_CENTRE + gaussRand() * (RANGE / 5) );
	if (n > PANNING_RIGHT)
	{
		n = PANNING_RIGHT;
	}
	else if (n < PANNING_LEFT) 
	{
		n = PANNING_LEFT;
	}
	return n;
}
