#include "nrutil.h"
#include <stdlib.h>
#include <math.h>

#include "structs.h"

#define NR_END 1
#define FREE_ARG char*

ComplexDouble *cdvector(long nl, long nh)
{
	ComplexDouble *v;

	v=(ComplexDouble *)malloc((size_t) ((nh-nl+1+NR_END)*sizeof(ComplexDouble)));
	if (!v) nrerror("allocation failure in cdvector()");
	return v-nl+NR_END;
}

void free_cdvector(ComplexDouble *v, long nl, long nh)
{
	free((FREE_ARG) (v+nl-NR_END));
}

inline ComplexDouble cc(ComplexDouble x) {
	x.im *= -1;

	return x;
}

inline double cmulti_re(ComplexDouble a, ComplexDouble b) {
	// ComplexDouble c;

	// c.re = a.re*b.re - a.im*b.im;
	// c.im = a.re*b.im + a.im*b.re;
	// c.abs = a.abs*b.abs;

	return a.re*b.re - a.im*b.im;
}

#define NRANSI
#define SWAP(a,b) {temp=(a);(a)=(b);(b)=temp;}

void gaussj(double **a, int n, double **b, int m)
{
	int *indxc,*indxr,*ipiv;
	int i,icol,irow,j,k,l,ll;
	double big,dum,pivinv,temp;

	indxc=ivector(1,n);
	indxr=ivector(1,n);
	ipiv=ivector(1,n);
	icol=0;
	irow=0;
	for (j=1;j<=n;j++) ipiv[j]=0;
	for (i=1;i<=n;i++) {
		big=0.0;
		for (j=1;j<=n;j++)
			if (ipiv[j] != 1)
				for (k=1;k<=n;k++) {
					if (ipiv[k] == 0) {
						if (fabs(a[j][k]) >= big) {
							big=fabs(a[j][k]);
							irow=j;
							icol=k;
						}
					} else if (ipiv[k] > 1) nrerror("gaussj: Singular Matrix-1");
				}
		++(ipiv[icol]);
		if (irow != icol) {
			for (l=1;l<=n;l++) SWAP(a[irow][l],a[icol][l])
			for (l=1;l<=m;l++) SWAP(b[irow][l],b[icol][l])
		}
		indxr[i]=irow;
		indxc[i]=icol;
		if (a[icol][icol] == 0.0) nrerror("gaussj: Singular Matrix-2");
		pivinv=1.0/a[icol][icol];
		a[icol][icol]=1.0;
		for (l=1;l<=n;l++) a[icol][l] *= pivinv;
		for (l=1;l<=m;l++) b[icol][l] *= pivinv;
		for (ll=1;ll<=n;ll++)
			if (ll != icol) {
				dum=a[ll][icol];
				a[ll][icol]=0.0;
				for (l=1;l<=n;l++) a[ll][l] -= a[icol][l]*dum;
				for (l=1;l<=m;l++) b[ll][l] -= b[icol][l]*dum;
			}
	}
	for (l=n;l>=1;l--) {
		if (indxr[l] != indxc[l])
			for (k=1;k<=n;k++)
				SWAP(a[k][indxr[l]],a[k][indxc[l]]);
	}
	free_ivector(ipiv,1,n);
	free_ivector(indxr,1,n);
	free_ivector(indxc,1,n);
}

void covsrt(double **covar, int ma, int ia[], int mfit)
{
	int i,j,k;
	double temp;

	for (i=mfit+1;i<=ma;i++)
		for (j=1;j<=i;j++) covar[i][j]=covar[j][i]=0.0;
	k=mfit;
	for (j=ma;j>=1;j--) {
		if (ia[j]) {
			for (i=1;i<=ma;i++) SWAP(covar[i][k],covar[i][j])
			for (i=1;i<=ma;i++) SWAP(covar[k][i],covar[j][i])
			k--;
		}
	}
}
#undef SWAP

void mrqcof(DataVector *d, double sig[], int ndata, double a[], int ia[],
	int ma, double **alpha, double beta[], double *chisq,
	void (*funcs)(double, double [], ComplexDouble *, ComplexDouble [], int))
{
	int i,j,k,l,m,mfit=0;
	double sig2i;
	ComplexDouble wt, ymod, dy, *dyda;

	dyda=cdvector(1,ma);
	for (j=1;j<=ma;j++)
		if (ia[j]) mfit++;
	for (j=1;j<=mfit;j++) {
		for (k=1;k<=j;k++) alpha[j][k]=0.0;
		beta[j]=0.0;
	}
	*chisq=0.0;
	for (i=0;i<ndata;i++) {
		(*funcs)(d->x[i],a,&ymod,dyda,ma);
		sig2i=1.0/(sig[i]*sig[i]);
		dy.re=d->y[i].re - ymod.re;
		dy.im=d->y[i].im - ymod.im;
//	printf("sig2i %e, dy.re %e, dy.im %e, y.re %e, ymod.re %e \n", sig2i, dy.re, dy.im, d[i].y.re ,ymod.re);
		for (j=0,l=1;l<=ma;l++) {
			if (ia[l]) {
				wt=cc(dyda[l]);
				for (j++,k=0,m=1;m<=l;m++)
					if (ia[m]) alpha[j][++k] += cmulti_re(wt, dyda[m])*sig2i;
				beta[j] += cmulti_re(dy, wt)*sig2i;
			}
		}
		*chisq += cmulti_re(dy, cc(dy))*sig2i;
	
		/* get out of here if someone canceled the fit */
		extern GlobalData *glob;
		if ( !(glob->flag & FLAG_FIT_RUN) )
			break;
	}
	for (j=2;j<=mfit;j++)
		for (k=1;k<j;k++) alpha[k][j]=alpha[j][k];
	free_cdvector(dyda,1,ma);
}

void mrqmin(DataVector *d, double sig[], int ndata, double a[], int ia[],
	int ma, double **covar, double **alpha, double *chisq,
	void (*funcs)(double, double [], ComplexDouble *, ComplexDouble [], int), double *alamda)
{
	int j,k,l;
	static int mfit;
	static double ochisq,*atry,*beta,*da,**oneda;
	if (*alamda < 0.0) {
		atry=dvector(1,ma);
		beta=dvector(1,ma);
		da=dvector(1,ma);
		for (mfit=0,j=1;j<=ma;j++)
			if (ia[j]) mfit++;
		oneda=dmatrix(1,mfit,1,1);
		*alamda=0.001;
		mrqcof(d,sig,ndata,a,ia,ma,alpha,beta,chisq,funcs);
		ochisq=(*chisq);
		for (j=1;j<=ma;j++) atry[j]=a[j];
	}
	for (j=1;j<=mfit;j++) {
		for (k=1;k<=mfit;k++) covar[j][k]=alpha[j][k];
		covar[j][j]=alpha[j][j]*(1.0+(*alamda));
		oneda[j][1]=beta[j];
	}
	gaussj(covar,mfit,oneda,1);
	for (j=1;j<=mfit;j++) da[j]=oneda[j][1];
	if (*alamda == 0.0) {
		covsrt(covar,ma,ia,mfit);
		covsrt(alpha,ma,ia,mfit);
		free_dmatrix(oneda,1,mfit,1,1);
		free_dvector(da,1,ma);
		free_dvector(beta,1,ma);
		free_dvector(atry,1,ma);
		return;
	}
	for (j=0,l=1;l<=ma;l++)
		if (ia[l]) atry[l]=a[l]+da[++j];
	mrqcof(d,sig,ndata,atry,ia,ma,covar,da,chisq,funcs);
	if (*chisq < ochisq) {
		*alamda *= 0.1;
		ochisq=(*chisq);
		for (j=1;j<=mfit;j++) {
			for (k=1;k<=mfit;k++) alpha[j][k]=covar[j][k];
			beta[j]=da[j];
		}
		for (l=1;l<=ma;l++) a[l]=atry[l];
	} else {
		*alamda *= 10.0;
		*chisq=ochisq;
	}
}
#undef NRANSI
