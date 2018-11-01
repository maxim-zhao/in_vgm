#ifndef _SN76489_H_
#define _SN76489_H_

#ifdef __cplusplus
extern "C" {
#endif

void SN76489_Init(const unsigned long PSGClockValue);
void SN76489_Write(const unsigned char data);
void SN76489_GGStereoWrite(const unsigned char data);
void SN76489_WriteToBuffer(short int *buffer, const int position);

int PSGMute; // expose this for inspection/modification for channel muting

#ifdef __cplusplus
}
#endif

#endif