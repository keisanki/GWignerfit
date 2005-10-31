#include "structs.h"
#include <glib.h>

#ifndef _WIGNER_NUMERIC_H_
#define _WIGNER_NUMERIC_H_

void DeriveComplexWigner (double x, double a[], ComplexDouble *yfit, ComplexDouble dyda[], int ma);

ComplexDouble ComplexWigner (double x, double a[], int ma);

void create_param_array (GPtrArray *param, GPtrArray *fcomp, GlobalParam *gparam, gint numres, gint numfcomp, double *p);

void create_param_structs (GPtrArray *param, GPtrArray *fcomp, GlobalParam *gparam, double *p, gint numres, gint numfcomp);

void fit (gint *ia);

void CheckAmplitudes (gdouble p[], gint numres);

void finishup_fit ();

gboolean fit_nrerror (gchar error_text[]);
#endif
