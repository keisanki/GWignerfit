#include <math.h>

#include "structs.h"
#include "helpers.h"
#include "calibrate.h"

extern GlobalData *glob;

/* Adds two complex numbers (a+b) */
ComplexDouble c_add (ComplexDouble a, ComplexDouble b)
{
	ComplexDouble c;

	c.re = a.re + b.re;
	c.im = a.im + b.im;
	c.abs = 0.0;

	return c;
}

/* Substracts two complex numbers (a-b) */
ComplexDouble c_sub (ComplexDouble a, ComplexDouble b)
{
	ComplexDouble c;

	c.re = a.re - b.re;
	c.im = a.im - b.im;
	c.abs = 0.0;

	return c;
}

/* Multiplies two complex numbers (a*b)*/
ComplexDouble c_mul (ComplexDouble a, ComplexDouble b)
{
	ComplexDouble c;

	c.re = a.re*b.re - a.im*b.im;
	c.im = a.re*b.im + b.re*a.im;
	c.abs = 0.0;

	return c;
}

/* Divides two complex numbers (a/b) */
ComplexDouble c_div (ComplexDouble a, ComplexDouble b)
{
	ComplexDouble c, num;
	gdouble denom;

	denom = b.re*b.re + b.im*b.im;

	b.im *= -1;
	num = c_mul (a, b);
	
	c.re = num.re / denom;
	c.im = num.im / denom;
	c.abs = 0.0;

	return c;
}

/* Calculates exp(i*a) for a real a */
ComplexDouble c_exp (gdouble a)
{
	ComplexDouble c;

	c.re = cos (a);
	c.im = sin (a);
	c.abs = 0.0;

	return c;
}

/* Calibrate a reflection spectrum with the given data */
DataVector* cal_reflection (DataVector *in, DataVector *opn, DataVector *shrt, DataVector *load)
{
	DataVector *out;
	ComplexDouble edf, esf, erf;
	gdouble alphaO, gammaO, alphaS, C;
	guint i;

	out = new_datavector (in->len);

	for (i=0; i<in->len; i++)
	{
		alphaO = 2*M_PI*in->x[i] * glob->prefs->cal_tauO;
		alphaS = 2*M_PI*in->x[i] * glob->prefs->cal_tauS;
		C =   glob->prefs->cal_C0
		    + glob->prefs->cal_C1 * in->x[i] 
		    + glob->prefs->cal_C2 * in->x[i]*in->x[i]
		    + glob->prefs->cal_C3 * in->x[i]*in->x[i]*in->x[i];
		gammaO = 2*atan (2*M_PI*in->x[i]*C*50);
		
		edf = load->y[i];

		esf = c_mul (c_sub (opn->y[i], load->y[i]), 
		             c_exp (alphaO+gammaO));
		esf = c_add (esf, 
		             c_mul (c_sub (shrt->y[i], load->y[i]), 
		                    c_exp (alphaS)));
		esf.re *= -1.0;
		esf.im *= -1.0;
		esf = c_div (esf, c_sub (shrt->y[i], opn->y[i]));
		
		erf = c_mul (c_sub (opn->y[i], load->y[i]),
		             c_sub (shrt->y[i], load->y[i]));
		erf = c_mul (erf,
		             c_add (c_exp (alphaO+gammaO), c_exp (alphaS)));
		erf = c_div (erf,
		             c_sub (shrt->y[i], opn->y[i]));

		out->y[i] = c_div (c_sub (in->y[i], edf),
		                   c_add (erf,
		                          c_mul (esf,
		                                 c_sub (in->y[i], edf)
						)
					 )
				  );

		out->y[i].abs = sqrt (out->y[i].re*out->y[i].re + out->y[i].im*out->y[i].im);

		/* Need to copy in->x[i] as in->x will be freed later */
		out->x[i] = in->x[i];

		if (i % (in->len / 100) == 0)
			cal_update_progress ((gfloat)i / (gfloat)in->len);
	}

	return out;
}

/* Calibrate a transmission spectrum with the given data */
DataVector* cal_transmission (DataVector *in, DataVector *thru, DataVector *isol)
{
	DataVector *out;
	ComplexDouble numerator, denominator;
	guint i;

	out = new_datavector (in->len);

	for (i=0; i<in->len; i++)
	{
		if (isol)
		{
			numerator   = c_sub (in->y[i], isol->y[i]);
			denominator = c_sub (thru->y[i], isol->y[i]);
		}
		else
		{
			numerator   = in->y[i];
			denominator = thru->y[i];
		}

		out->y[i] = c_div (numerator, denominator);
		out->y[i].abs = sqrt (out->y[i].re*out->y[i].re + out->y[i].im*out->y[i].im);

		/* Need to copy in->x[i] as in->x will be freed later */
		out->x[i] = in->x[i];

		if (i % (in->len / 100) == 0)
			cal_update_progress ((gfloat)i / (gfloat)in->len);
	}

	return out;
}

