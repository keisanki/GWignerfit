#include <math.h>
#include "nrutil.h"

/*********************** from nrutils COMPLEX.C ************************/

typedef struct FCOMPLEX {float r,i;} fcomplex;

static fcomplex Complex(float re, float im)
{
	fcomplex c;
	c.r=re;
	c.i=im;
	return c;
}

static fcomplex Cdiv(fcomplex a, fcomplex b)
{
	fcomplex c;
	float r,den;
	if (fabs(b.r) >= fabs(b.i)) {
		r=b.i/b.r;
		den=b.r+r*b.i;
		c.r=(a.r+r*a.i)/den;
		c.i=(a.i-r*a.r)/den;
	} else {
		r=b.r/b.i;
		den=b.i+r*b.r;
		c.r=(a.r*r+a.i)/den;
		c.i=(a.i*r-a.r)/den;
	}
	return c;
}

static fcomplex Cadd(fcomplex a, fcomplex b)
{
	fcomplex c;
	c.r=a.r+b.r;
	c.i=a.i+b.i;
	return c;
}

static fcomplex Cmul(fcomplex a, fcomplex b)
{
	fcomplex c;
	c.r=a.r*b.r-a.i*b.i;
	c.i=a.i*b.r+a.r*b.i;
	return c;
}

fcomplex RCmul(x,a)
fcomplex a;
float x;
{
	fcomplex c;
	c.r=x*a.r;
	c.i=x*a.i;
	return c;
}

/*********************** from nrutils CISI.C ************************/

#define EPS 6.0e-8
#define EULER 0.57721566
#define MAXIT 100
#define PIBY2 1.5707963
#define FPMIN 1.0e-30
#define TMIN 2.0
#define TRUE 1
#define ONE Complex(1.0,0.0)

void cisi(float x, float *ci, float *si)
{
	void nrtexterror(char error_text[]);
	int i,k,odd;
	float a,err,fact,sign,sum,sumc,sums,t,term;
	fcomplex h,b,c,d,del;

	t=fabs(x);
	if (t == 0.0) {
		*si=0.0;
		*ci = -1.0/FPMIN;
		return;
	}
	if (t > TMIN) {
		b=Complex(1.0,t);
		c=Complex(1.0/FPMIN,0.0);
		d=h=Cdiv(ONE,b);
		for (i=2;i<=MAXIT;i++) {
			a = -(i-1)*(i-1);
			b=Cadd(b,Complex(2.0,0.0));
			d=Cdiv(ONE,Cadd(RCmul(a,d),b));
			c=Cadd(b,Cdiv(Complex(a,0.0),c));
			del=Cmul(c,d);
			h=Cmul(h,del);
			if (fabs(del.r-1.0)+fabs(del.i) < EPS) break;
		}
		if (i > MAXIT) nrtexterror("cf failed in cisi");
		h=Cmul(Complex(cos(t),-sin(t)),h);
		*ci = -h.r;
		*si=PIBY2+h.i;
	} else {
		if (t < sqrt(FPMIN)) {
			sumc=0.0;
			sums=t;
		} else {
			sum=sums=sumc=0.0;
			sign=fact=1.0;
			odd=TRUE;
			for (k=1;k<=MAXIT;k++) {
				fact *= t/k;
				term=fact/k;
				sum += sign*term;
				err=term/fabs(sum);
				if (odd) {
					sign = -sign;
					sums=sum;
					sum=sumc;
				} else {
					sumc=sum;
					sum=sums;
				}
				if (err < EPS) break;
				odd=!odd;
			}
			if (k > MAXIT) nrtexterror("maxits exceeded in cisi");
		}
		*si=sums;
		*ci=sumc+log(t)+EULER;
	}
	if (x < 0.0) *si = -(*si);
}
#undef EPS
#undef EULER
#undef MAXIT
#undef PIBY2
#undef FPMIN
#undef TMIN
#undef TRUE
#undef ONE

/*********************** from nrutils TRAPZD.C ************************/

#define FUNC(param,x) ((*func)(param,x))

float trapzd(float (*func)(float,float), float param, float a, float b, int n)
{
	float x,tnm,sum,del;
	static float s;
	int it,j;

	if (n == 1) {
		return (s=0.5*(b-a)*(FUNC(param,a)+FUNC(param,b)));
	} else {
		for (it=1,j=1;j<n-1;j++) it <<= 1;
		tnm=it;
		del=(b-a)/tnm;
		x=a+0.5*del;
		for (sum=0.0,j=1;j<=it;j++,x+=del) sum += FUNC(param,x);
		s=0.5*(s+(b-a)*sum/tnm);
		return s;
	}
}
#undef FUNC

/*********************** from nrutils QSIMP.C ************************/

#define EPS 1.0e-2
#define JMAX 10

float qsimp(float (*func)(float,float), float param, float a, float b)
{
	float trapzd(float (*func)(float,float), float param, float a, float b, int n);
	void nrtexterror(char error_text[]);
	int j;
	float s,st,ost,os;

	ost = os = -1.0e30;
	for (j=1;j<=JMAX;j++) {
		st=trapzd(func,param,a,b,j);
		s=(4.0*st-ost)/3.0;
		if (j > 5)
			if (fabs(s-os) < EPS*fabs(os) ||
				(s == 0.0 && os == 0.0)) return s;
		os=s;
		ost=st;
	}
	nrtexterror("Too many steps in routine qsimp");
	return 0.0;
}
#undef EPS
#undef JMAX
