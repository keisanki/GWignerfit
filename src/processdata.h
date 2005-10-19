#ifndef _PROCESSDATA_H_
#define _PROCESSDATA_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include "structs.h"

int FindResonance (DataVector *d, char type);
	
char IsReflectionSpectrum (DataVector *d);

gboolean make_unique_dataset (DataVector *data);

DataVector *import_datafile (gchar *filename, gboolean interactive);

void set_new_main_data (DataVector *data, gboolean called_from_open);

void read_datafile (gchar *selected_filename, gint called_from_open);

gboolean save_file (gchar *filename, gchar *section, gint exists);

gboolean save_file_prepare (gchar *selected_filename);

gboolean is_datafile (gchar *filename);

gint read_resonancefile (const gchar *selected_filename, const gchar *label);

double NormalisePhase(double p);

void calculate_global_paramters (DataVector *d, GlobalParam *gparam);

Resonance *find_resonance_at (double frq, DataVector *d);

gint find_isolated_resonances (gfloat thresh);

#endif
