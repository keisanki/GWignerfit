#include "nrutil.h"
#include <stdlib.h>
#include <math.h>

#include "structs.h"
#include "helpers.h"
#include "compl_mrqmin.h"

extern GlobalData *glob;

typedef struct {
	DataVector *d;
	double *sig;
	int ndata;
	double *a;
	int *ia;
	int ma;
	double **alpha;
	double *beta;
	double *chisq;
	void (*funcs)(double, double [], ComplexDouble *, ComplexDouble [], int);
} MrqminData;

/* Original nurtils mrqcof function */
static void mrqcof_cal (MrqminData *data)
{
	int k,mfit=0;
	register int m, j, l, i, *ia;
	double sig2i;
	ComplexDouble wt, ymod, dy, *dyda;

	dyda = cdvector (1, data->ma);
	ia   = data->ia;
	
	for (j=1; j<=data->ma; j++)
		if (ia[j])
			mfit++;
	
	for (j=1; j<=mfit; j++)
	{
		for (k=1; k<=j; k++) 
			data->alpha[j][k] = 0.0;
		data->beta[j] = 0.0;
	}
	
	*data->chisq=0.0;
	for (i=0; i<data->ndata; i++)
	{
		(*data->funcs)(data->d->x[i], data->a, &ymod, dyda, data->ma);
		sig2i = 1.0/(data->sig[i]*data->sig[i]);
		dy.re = data->d->y[i].re - ymod.re;
		dy.im = data->d->y[i].im - ymod.im;
		
		for (j=0,l=1; l<=data->ma; l++)
		{
			if (ia[l])
			{
				wt = cc(dyda[l]);
				for (j++,k=0,m=1; m<=l; m++)
					if (ia[m])
						data->alpha[j][++k] += cmulti_re(wt, dyda[m])*sig2i;
				data->beta[j] += cmulti_re(dy, wt)*sig2i;
			}
		}
		*data->chisq += cmulti_re (dy, cc (dy))*sig2i;
	
		/* get out of here if someone canceled the fit */
		if ( !(glob->flag & FLAG_FIT_RUN) )
			break;
	}
	
	for (j=2;j<=mfit;j++)
		for (k=1;k<j;k++)
			data->alpha[k][j] = data->alpha[j][k];
	
	free_cdvector (dyda, 1, data->ma);

	/* Setting the callback function to NULL will be the marker that this
	 * thread is done with its job. mrqcof() waits for this signature. */
	data->funcs = NULL;
}

/* Wrapper for the real mrqcof (which is in mrqcof_cal()) to do the thread
 * handling in between. */
void mrqcof (DataVector *d, double sig[], int ndata, double a[], int ia[],
	int ma, double **alpha, double beta[], double *chisq,
	void (*funcs)(double, double [], ComplexDouble *, ComplexDouble [], int))
{
	int delta, i, j, k;
	MrqminData **data, singledata;
	
	if (glob->smp->num_cpu > 1)
	{
		/* Do SMP calculation */
		data = g_new (MrqminData *, glob->smp->num_cpu);
		delta = ndata / glob->smp->num_cpu;
		
		/* Push calculations into pool */
		for (i=0; i<glob->smp->num_cpu; i++)
		{
			data[i] = g_new (MrqminData, 1);

			data[i]->d     = g_new (DataVector, 1);
			data[i]->d->x  = d->x + i*delta;
			data[i]->d->y  = d->y + i*delta;
			data[i]->sig   = sig;
			data[i]->a     = a;
			data[i]->ia    = ia;
			data[i]->ma    = ma;
			data[i]->alpha = dmatrix (1, ma, 1, ma);
			data[i]->beta  = dvector (1, ma);
			data[i]->chisq = g_new (double, 1);
			data[i]->funcs = funcs;

			/* Take care of rounding errors */
			if (i != glob->smp->num_cpu-1)
				data[i]->ndata = delta;
			else
				data[i]->ndata = ndata - i*delta;

			g_thread_pool_push (
					glob->smp->pool,
					data[i],
					NULL);
		}

		/* Wait for calculation to finish */
		j = 0;
		while (j != glob->smp->num_cpu)
		{
			usleep (10000); /* 10 ms */
			j = 0;
			for (i=0; i<glob->smp->num_cpu; i++)
				if (data[i]->funcs == NULL)
					j++;
		}

		/* Collect calculated data and free data[] */
		for (j=1; j<=ma; j++)
		{
			for (k=1; k<=ma; k++) 
				alpha[j][k] = 0.0;
			beta[j] = 0.0;
		}
		*chisq = 0.0;

		for (i=0; i<glob->smp->num_cpu; i++)
		{
			for (j=1; j<=ma; j++)
			{
				for (k=1; k<=ma; k++)
					alpha[k][j] += data[i]->alpha[k][j];
				beta[j] += data[i]->beta[j];
			}
			*chisq += *data[i]->chisq;
			
			free_dmatrix (data[i]->alpha, 1, ma, 1, ma);
			free_dvector (data[i]->beta, 1, ma);
			g_free (data[i]->chisq);
			g_free (data[i]->d);
			g_free (data[i]);
		}
	}
	else
	{
		/* Do "normal" calculation */

		singledata.d     = d;
		singledata.sig   = sig;
		singledata.ndata = ndata;
		singledata.a     = a;
		singledata.ia    = ia;
		singledata.ma    = ma;
		singledata.alpha = alpha;
		singledata.beta  = beta;
		singledata.chisq = chisq;
		singledata.funcs = funcs;
		
		mrqcof_cal (&singledata);
	}
}

/* Prepare a fit (determine number of CPUs, initialize threads, ...) */
void mrqcof_prepare ()
{
	if (!glob->smp)
	{
		glob->smp = g_new (SMPdata, 1);
		glob->smp->num_cpu = get_num_cpu ();
		glob->smp->pool = NULL;
	}

	if (glob->smp->num_cpu > 1)
		/* Prepare pool for SMP calculation */
		glob->smp->pool = g_thread_pool_new (
				(GFunc) mrqcof_cal,
				NULL,
				glob->smp->num_cpu,
				TRUE,
				NULL);
}

/* Tidy thread system up after a fit has finished */
void mrqcof_cleanup ()
{
	if (glob->smp->pool)
	{
		g_thread_pool_free (glob->smp->pool, TRUE, TRUE);
		glob->smp->pool = NULL;
	}
}
