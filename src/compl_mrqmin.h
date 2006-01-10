#include <stdlib.h>
#include <math.h>

#include "nrutil.h"
#include "structs.h"

#define NR_END 1
#define FREE_ARG char*

#ifndef _COMPL_MRQMIN_H_
#define _COMPL_MRQMIN_H_

ComplexDouble *cdvector(long nl, long nh);

void free_cdvector(ComplexDouble *v, long nl, long nh);

inline ComplexDouble cc(ComplexDouble x);

inline double cmulti_re(ComplexDouble a, ComplexDouble b);

double re(ComplexDouble x);

ComplexDouble cmulti(ComplexDouble a, ComplexDouble b);

void gaussj(double **a, int n, double **b, int m, int cancelcheck);

void covsrt(double **covar, int ma, int ia[], int mfit);

/* d and sig start at 0 */
void mrqmin(DataVector *d, double sig[], int ndata, double a[], int ia[],
	int ma, double **covar, double **alpha, double *chisq,
	void (*funcs)(double, double [], ComplexDouble *, ComplexDouble [], int), double *alamda);

#endif
