#ifndef _CALIBRATE_OFFLINE_H_
#define _CALIBRATE_OFFLINE_H_

ComplexDouble c_add (ComplexDouble a, ComplexDouble b);
ComplexDouble c_sub (ComplexDouble a, ComplexDouble b);
ComplexDouble c_mul (ComplexDouble a, ComplexDouble b);
ComplexDouble c_div (ComplexDouble a, ComplexDouble b);

DataVector* cal_reflection (DataVector *in, DataVector *opn, DataVector *shrt, DataVector *load);

DataVector* cal_transmission (DataVector *in, DataVector *thru, DataVector *isol);

#endif

