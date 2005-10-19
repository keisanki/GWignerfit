#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <unistd.h>

#include "compl_mrqmin.h"

#include "structs.h"
#include "helpers.h"
#include "resonancelist.h"
#include "callbacks.h"
#include "visualize.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

static void fit_cleanup ();

/* Derives the complex fit function */
void DeriveComplexWigner (double x, double a[], ComplexDouble *yfit, ComplexDouble dyda[], int ma) {
	double amp, phi, frq, gam, alpha, scale, transphase;
	double arctan, sq, factor, sinval, cosval;
	int i;
	
	alpha = a[ma-2];
	scale = glob->IsReflection ? a[ma-1] : 1;
	transphase = glob->IsReflection ? 0.0 : -2*M_PI*a[ma]*x;
	yfit->re = cos(alpha)*scale * glob->IsReflection;
	yfit->im = sin(alpha)*scale * glob->IsReflection;

	for (i=1; i < ma-NUM_GLOB_PARAM; i+=4)
	{
		amp = a[i];
		phi = a[i+1];
		frq = a[i+2];
		gam = a[i+3];
		
		// arctan = (x-frq) ? atan2(gam/2, x-frq) : M_PI/2;
		arctan = atan2(gam/2, -x+frq);
		sq = 1/sqrt((x-frq)*(x-frq) + gam*gam/4);

		factor = amp*sq*scale;
		yfit->re += factor * sin(alpha + phi - arctan + transphase);
		yfit->im -= factor * cos(alpha + phi - arctan + transphase);

		factor = alpha + phi - arctan + transphase;
		sinval = sin(factor) * sq * scale;
		cosval = cos(factor) * sq * scale;

		/* d / d(amp) */
		dyda[i+0].re =  sinval;
		dyda[i+0].im = -cosval;

		/* d / d(phi) */
		dyda[i+1].re = amp * cosval;
		dyda[i+1].im = amp * sinval;

		sq *= sq;
		factor -= arctan;
		sinval = sin(factor) * sq * amp;
		cosval = cos(factor) * sq * amp;

		/* d / d(frq) */
		dyda[i+2].re =  sinval * -1;
		dyda[i+2].im = -cosval * -1;

		/* d / d(gam) */
		dyda[i+3].re = -cosval/2;
		dyda[i+3].im = -sinval/2;
	}

	/* d / d(alpha) */
	dyda[ma-2].re = - yfit->im;
	dyda[ma-2].im =   yfit->re;

	/* d / d(scale) */
	dyda[ma-1].re = yfit->re / scale;
	dyda[ma-1].im = yfit->im / scale;

	/* d / d(tau) */
	dyda[ma  ].re =   yfit->im * 2*M_PI*x;
	dyda[ma  ].im = - yfit->re * 2*M_PI*x;
}

/* Returns the complex value of the fit function */
ComplexDouble ComplexWigner (double x, double a[], int ma) {
	ComplexDouble y;
	double amp, phi, frq, gam, alpha, scale, transphase;
	double arctan, sq, factor;
	int i;

	alpha = a[ma-2];
	scale = glob->IsReflection ? a[ma-1] : 1;
	transphase = glob->IsReflection ? 0.0 : -2*M_PI*a[ma]*x;
	y.re = scale * cos(alpha) * glob->IsReflection;
	y.im = scale * sin(alpha) * glob->IsReflection;

	for (i=1; i < ma-NUM_GLOB_PARAM; i+=4) 
	{
		amp = a[i];
		phi = a[i+1];
		frq = a[i+2];
		gam = a[i+3];

		arctan = atan2(gam/2, -x+frq);
		sq = 1/sqrt((x-frq)*(x-frq) + gam*gam/4);

		factor = amp*sq*scale;
		y.re += factor * sin(alpha + phi - arctan + transphase);
		y.im -= factor * cos(alpha + phi - arctan + transphase);
	}

	y.abs = sqrt(y.re*y.re + y.im*y.im);

	return y;
}

#if 0
gdouble cal_stddev (gdouble val)
{
	gdouble in_db, err_db, val_plus_err, val_minus_err, ret;
	if (val < 0)
		val *= -1.0;

	if (val > 0)
		in_db = 8.68588963807 * log (val);
	else
		in_db = -100.0;

	if      (in_db > -10) err_db = 0.06;
	else if (in_db > -30) err_db = 0.07;
	else if (in_db > -40) err_db = 0.10;
	else if (in_db > -45) err_db = 0.15;
	else if (in_db > -50) err_db = 0.20;
	else if (in_db > -60) err_db = 0.30;
	else if (in_db > -65) err_db = 0.50;
	else if (in_db > -70) err_db = 1.00;
	else if (in_db > -75) err_db = 2.00;
	else if (in_db > -80) err_db = 5.00;
	else err_db = 10.00;
	
	val_plus_err  = pow (10, (in_db + err_db)/10);
	val_minus_err = pow (10, (in_db - err_db)/10);

	ret = sqrt (val_plus_err - val_minus_err);

	//printf ("error for %e (%e dB): err_db %e, val_plus_err %e, diff %e, ret %e\n", val, in_db, err_db, val_plus_err, val-val_plus_err, ret);
	
	return ret;
}
#endif

/* This function is responsible for the fit and calls the actual mrq algorithm.
 * Furthermore it decides what parameters are to be fitted and outputs
 * some status information during the whole process.
 */
int ApplyMrqmin (
	DataVector *d, 				/* datavector */
	int n, 					/* number of datapoints */
	double p[], 				/* parameter array */
	int *ia,				/* what parameters are to fit */
	int pnum, 				/* number of parameters */
	int maxiterations,			/* number of iterations */
	void (*fitfunc)(double, double [], 	/* pointer to fitfunction */
		ComplexDouble *, ComplexDouble [], int)
	) 
{
	int i, itst=0, k, ret, numfail=0;
	double alamda, chisq, ochisq, *sig, **covar, **alpha, *a;
	FitWindowParam *fitwinparam = &glob->fitwindow;

	sig = dvector (1, n);
	a = dvector (1, pnum);
	covar = dmatrix (1, pnum, 1, pnum);
	alpha = dmatrix (1, pnum, 1, pnum);
	
	/* standard deviations set to unity */
	for (i=0; i<n; i++) sig[i] = 1;

/*
	for (i=0; i<n; i++) 
	{
		sig[i] = cal_stddev (d[i].y.abs);
		printf ("%e\t%e\t%e\n", d[i].x, d[i].y.abs, sig[i]);
	}
*/

	/* fill the initial parameters */
	for (i=1; i<=pnum; i++) a[i] = p[i];
	
	alamda = -1;			/* indicate first run */
	mrqmin (d,sig,n,a,ia,pnum,covar,alpha,&chisq,fitfunc,&alamda);
	k = 1;
	do {
/*		printf("\n%s %2d %17s %10e %10s %9.2e\n","Iteration #",k,
			"chi-squared:",chisq,"alamda:",alamda);
		for (i=1; i<=pnum; i++) {
			if ((i-1) % 4 == 0) printf("\n");
			printf("a[%i] = %.6e\t", i, a[i]);
		}
		printf("\n");
*/		
		k++;
		ochisq = chisq;
		mrqmin (d,sig,n,a,ia,pnum,covar,alpha,&chisq,fitfunc,&alamda);
		for (i=1; i<=pnum; i++) p[i] = a[i];

		if (chisq > ochisq) itst = 0;
		else if (fabs(ochisq-chisq) < 0.1) {
			itst++;
		}
		
		g_mutex_lock (glob->threads->fitwinlock);
		fitwinparam->min = -1;		/* no need to update values */
		fitwinparam->max = -1;		/* that won't change */
		fitwinparam->numpoints = -1;
		fitwinparam->freeparam = -1;
		fitwinparam->iter = itst;
		fitwinparam->maxiter = maxiterations;
		fitwinparam->chi = chisq;
		if (ochisq == chisq) 
			snprintf (fitwinparam->text, 29, "failure (stepsize: %.2e)", alamda);
		else 
			snprintf (fitwinparam->text, 29, "success (stepsize: %.2e)", alamda);
		g_mutex_unlock (glob->threads->fitwinlock);

		/* Check if fit seems to have converged */
		if ((chisq == ochisq) && (alamda > 10.0))
			numfail++;
		else
			numfail = 0;
		if ((numfail > 5) && (glob->prefs->fit_converge_detect))
		{
			ret = FIT_EXIT_CONVERGED;
			break;
		}
			
		ret = FIT_EXIT_COMPLETE;
		if ( !(glob->flag & FLAG_FIT_RUN) )
		{
			ret = glob->flag & FLAG_FIT_CANCEL ? FIT_EXIT_CANCELED : FIT_EXIT_EARLYSTOP;
			break;
		}

		g_timeout_add (1, (GSourceFunc) update_fit_window, fitwinparam );
	} while (itst < maxiterations);

	alamda = 0.0;		/* indicate last run */
	mrqmin (d,sig,n,a,ia,pnum,covar,alpha,&chisq,fitfunc,&alamda);
	for (i=1; i<=pnum; i++) p[i] = a[i];
/*
	printf("\nError estimations:\n");
	printf("raw:  chisq: %e, points: %d, noise: %e\n", chisq, n, sig[0]);
	for (i=1; i<=pnum; i++)
		printf ("val: %e +/- %e, raw: %e\n", 
				a[i], sqrt(chisq/(gdouble)(n-pnum))*sqrt(covar[i][i]), sqrt(covar[i][i]));
*/		
	/* Calculate error estimations */
	fitwinparam->stddev = g_new (gdouble, pnum+1);
	chisq /= (gdouble)(n-pnum);
	for (i=1; i<=pnum; i++)
		fitwinparam->stddev[i] = sqrt (chisq*covar[i][i]);

	free_dmatrix (alpha, 1, pnum, 1, pnum);
	free_dmatrix (covar, 1, pnum, 1, pnum);
	free_dvector (sig, 1, n);
	free_dvector (a, 1, pnum);
	
	return ret;
}

void create_param_array (GPtrArray *param, GlobalParam *gparam, gint numres, double *p)
{
	gint i;
	Resonance *res;

	for (i=0; i<numres; i++)
	{
		res = g_ptr_array_index(param, i);
		p[4*i+1] = res->amp;
		p[4*i+2] = res->phase;
		p[4*i+3] = res->frq;
		p[4*i+4] = res->width;
	}

	p[4*numres+1] = gparam->phase;
	p[4*numres+2] = gparam->scale;
	p[4*numres+3] = gparam->tau;
}

void create_param_structs (GPtrArray *param, GlobalParam *gparam, double *p, gint numres)
{
	gint i;
	Resonance *res;

	for (i=0; i<numres; i++)
	{
		res = g_ptr_array_index(param, i);
		res->amp   = p[4*i+1];
		res->phase = p[4*i+2];
		res->frq   = p[4*i+3];
		res->width = p[4*i+4];
	}
	if (glob->prefs->sortparam)
		g_ptr_array_sort (glob->param, param_compare);

	if (gparam != NULL)
	{
		gparam->phase = p[4*numres+1];
		gparam->scale = p[4*numres+2];
		gparam->tau   = p[4*numres+3];
	}
}

/* Reverse the sign of the amplitude and add M_PI to the phase instead if necessary. */
void CheckAmplitudes (gdouble p[], gint numres)
{
	gint i;

	for (i=0; i<numres; i++) 
	{
		if (p[4*i+1] < 0)
		{
			p[4*i+1] *= -1;
			p[4*i+2] += M_PI;
		}
	}
}

/* Unset flags and close the fitwindow after a fit */
static void fit_cleanup ()
{
	g_mutex_lock (glob->threads->flaglock);
	glob->flag &= ~FLAG_FIT_RUN;
	glob->flag &= ~FLAG_FIT_CANCEL;
	g_mutex_unlock (glob->threads->flaglock);

	g_timeout_add (1, (GSourceFunc) gtk_widget_destroy, 
				glade_xml_get_widget (
					glob->fitwindow.xmlfit, 
					"fit_progress")
				);
}

/* gets called in case of an Numerical Recipes error */
gboolean fit_nrerror (gchar error_text[])
{
	FitWindowParam *fitwinparam;
	fitwinparam = &glob->fitwindow;
	gdouble *param_and_stddev;
	gint i;

	fit_cleanup ();

	if (dialog_question ("Fit aborted due to\nnumerical singularity.\n\n"
			     "Recover dataset of last iteration?")
			== GTK_RESPONSE_YES)
	{
		CheckAmplitudes (fitwinparam->paramarray, glob->numres);

		param_and_stddev = g_new (gdouble, 2*(4*glob->numres+NUM_GLOB_PARAM+1)-1);
		for (i=1; i<=4*glob->numres+NUM_GLOB_PARAM; i++)
			param_and_stddev[i] = fitwinparam->paramarray[i];
		param_and_stddev[1+4*glob->numres+NUM_GLOB_PARAM] = -1; /* no stddev */
		g_free (fitwinparam->paramarray);
		fitwinparam->paramarray = NULL;

		check_and_take_parameters (param_and_stddev);
	}

	/* This function has been called by a g_timeout_add 
	 * and should only be executed once.*/
	return FALSE;
}

static gint start_fit (gpointer params)
{
	gint i, startpos, retval;
	guint numpoints = 0;
	DataVector data;
	FitWindowParam *fitwinparam;
	gint *ia = (gint *) params;
	gdouble *param_and_stddev;

	g_mutex_lock (glob->threads->flaglock);
	glob->flag |= FLAG_FIT_RUN;
	g_mutex_unlock (glob->threads->flaglock);
	
	fitwinparam = &glob->fitwindow;

	/* Count the number of points in frequency range */
	startpos = 0;
	for (i=0; i<glob->data->len; i++)
	{
		if ((glob->data->x[i] >= glob->gparam->min) &&
		    (glob->data->x[i] <= glob->gparam->max)) 
		{
			if (!numpoints) startpos = i;
			numpoints++;
		}
	}

	if (numpoints) 
	{
		/* Do the initial update of the fit progress win */
		g_mutex_lock (glob->threads->fitwinlock);
		fitwinparam->min = glob->gparam->min;
		fitwinparam->max = glob->gparam->max;
		fitwinparam->numpoints = numpoints;
		fitwinparam->freeparam = ia[0];
		fitwinparam->iter = 0;
		fitwinparam->maxiter = glob->prefs->iterations;
		fitwinparam->chi = 0;
		snprintf (fitwinparam->text, 29, "Initializing fit");
		g_mutex_unlock (glob->threads->fitwinlock);
		g_timeout_add (1, (GSourceFunc) update_fit_window, fitwinparam);

		/* Convert the parameter structs into one array (paramarray) */
		/* Structure of paramarray:
		 * 0*glob->numres+1 ... 4*glob->numres : resonance parameters
		 * 4*glob->numres+1 ... 4*glob->numres+NUM_GLOB_PARAM : global parameters
		 */
		fitwinparam->paramarray = g_new (gdouble, 4*glob->numres+NUM_GLOB_PARAM+1);
		create_param_array (glob->param, glob->gparam, glob->numres, fitwinparam->paramarray);

		data.x = glob->data->x + startpos;
		data.y = glob->data->y + startpos;
	
		/* Do the fit */
		retval = ApplyMrqmin (
			&data,		 	/* datavector at startpos */
			numpoints,		/* number of datapoints */
			fitwinparam->paramarray,/* parameter array */
			ia,			/* what parameters are to fit */
			4*glob->numres+NUM_GLOB_PARAM, 	/* number of parameters */
			glob->prefs->iterations,/* number of iterations */
			&DeriveComplexWigner	/* pointer to fitfunction */
		);

		if (!(glob->flag & FLAG_FIT_CANCEL))
		{
			CheckAmplitudes (fitwinparam->paramarray, glob->numres);

			param_and_stddev = g_new (gdouble, 2*(4*glob->numres+NUM_GLOB_PARAM+1)-1);
			for (i=1; i<=4*glob->numres+NUM_GLOB_PARAM; i++)
			{
				param_and_stddev[i] = fitwinparam->paramarray[i];
				param_and_stddev[i+4*glob->numres+NUM_GLOB_PARAM] = fitwinparam->stddev[i];
			}

			if (retval == FIT_EXIT_EARLYSTOP)
				/* If the user took "the current values" we do not have 
				 * sensible stddev information. Setting the value to -1
				 * here will make this clear to check_and_take_parameters.
				 */
				param_and_stddev[1+4*glob->numres+NUM_GLOB_PARAM] = -1;
			
			g_timeout_add (1, (GSourceFunc) check_and_take_parameters, param_and_stddev);
		}

		g_free (fitwinparam->paramarray);
		fitwinparam->paramarray = NULL;
		g_free (fitwinparam->stddev);
		fitwinparam->stddev = NULL;
	}
	else
		retval = FIT_EXIT_ERROR;

	g_free (ia);

	fit_cleanup ();
	return retval;
}

void fit (gint *ia)
{
	GladeXML *xmlfit;

	/* Return if no data is loaded */
	if (!glob->data)
	{
		dialog_message ("Please import a spectrum first.");
		return;
	}

	/* Stop any background calculations */
	visualize_stop_background_calc ();

	/* Show the fit_progress window */
	xmlfit = glade_xml_new (GLADEFILE, "fit_progress", NULL);
	glade_xml_signal_autoconnect (xmlfit);

	/* Prepare the data pass through variable */
	glob->fitwindow.xmlfit = xmlfit; 

	/* Fork the actual fit into another process */
	glob->fitwindow.fit_GThread = 
		g_thread_create ((GThreadFunc) start_fit, (gpointer) ia, FALSE, NULL);
	
	if (!glob->fitwindow.fit_GThread)
	{
		fit_cleanup ();
		perror ("fork failed");
		exit (-1);
	}
}
