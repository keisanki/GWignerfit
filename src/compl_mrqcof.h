#include "nrutil.h"
#include "structs.h"

#ifndef _COMPL_MRQCOF_H_
#define _COMPL_MRQCOF_H_

void mrqcof (DataVector *d, double sig[], int ndata, double a[], int ia[],
	int ma, double **alpha, double beta[], double *chisq,
	void (*funcs)(double, double [], ComplexDouble *, ComplexDouble [], int));

void mrqcof_prepare ();

void mrqcof_cleanup ();

#endif
