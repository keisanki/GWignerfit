#include "structs.h"
#include <glib.h>

#ifndef _WIGNER_NUMERIC_H_
#define _WIGNER_NUMERIC_H_

void DeriveComplexWigner (double x, double a[], ComplexDouble *yfit, ComplexDouble dyda[], int ma);

ComplexDouble ComplexWigner (double x, double a[], int ma);

void create_param_array (GPtrArray *param, GlobalParam *gparam, gint numres, double *p);

void create_param_structs (GPtrArray *param, GlobalParam *gparam, double *p, gint numres);

void fit (gint *ia);

void CheckAmplitudes (gdouble p[], gint numres);

void finishup_fit ();

gboolean fit_nrerror (gchar error_text[]);
#endif
