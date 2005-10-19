#ifndef _SPECTRAL_NUMERIC_H_
#define _SPECTRAL_NUMERIC_H_

void cisi(float x, float *ci, float *si);

float qsimp(float (*func)(float,float), float param, float a, float b);

#endif
