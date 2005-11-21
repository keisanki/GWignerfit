#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#ifndef NO_ZLIB
#include <zlib.h>
#endif

#include "structs.h"
#include "visualize.h"
#include "helpers.h"
#include "resonancelist.h"
#include "numeric.h"
#include "overlay.h"
#include "fourier.h"
#include "spectral.h"
#include "fcomp.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

/* Finds an absolute maximum or minimum */
int FindResonance (DataVector *d, char type) 
{
	int i, posofres;
	double val = -100;

	/* type == -1: Looking for minimum
	 * type == +1: Looking for maximum
	 */
	
	posofres = -1;
	for (i=0; i<d->len; i++) {
		if (d->y[i].abs*type > val) {
			val = d->y[i].abs*type;
			posofres = i;
		}
	}

	return posofres;
}

/* Determines the spectrum type from the absolute value. */
char IsReflectionSpectrum (DataVector *d) 
{
	int i;
	double offset = 0, min, max;

	for (i=0; i<d->len; i++)
		if ((i < 10) || (i > d->len-11)) offset += d->y[i].abs/20;

	min = d->y[FindResonance(d, -1)].abs;
	max = d->y[FindResonance(d, +1)].abs;

	/* Check for transmission signature */
	if ((offset - min) < (max - offset)) return 0;
	
	/* Seems to be a reflection measurement */
	return 1;
}

double NormalisePhase (double p)
{
	p = fmod(p, 2*M_PI);
	if (p > M_PI) p -= 2*M_PI;

	return p;
}

static void GetTauByFFT (DataVector *d, double *tau, double *phase)
{
	DataVector *fft;
	gdouble max;
	gint i;
	
	if (*tau)
		/* Do nothing if tau is already non-zero */
		return;

	if (!*phase)
		*phase = 0.0;

	/* Use the last datapoints for the fft for a better SNR */
	/* calculate the fft */
	fft = fourier_gen_dataset (
			d, 
			d->len > 8192 ? d->x[d->len-8192] : d->x[0], 
			d->x[d->len-1]);

	if (!fft)
	{
		*tau = 0.0;
		return;
	}

	/* find the position of the fft maximum */
	max = 0;
	for (i=0; i<fft->len; i++)
		if (fft->y[i].abs > max) max = fft->y[i].abs;

	/* find the time that corresponds to 0.2 * this maximum */
	i = 0;
	while (fft->y[i].abs < 0.2 * max)
		i++;

	/* assign a non negative value to tau */
	*tau = fft->x[i];
	if (*tau < 0.0) *tau = 0.0;

	free_datavector (fft);
}

static void GetPhaseAndScale (DataVector *d, double *phase, double *scale) 
{
	gint j;
	gdouble realoff = 0, imgoff = 0;
	
	for (j=0; j<d->len; j++)
	{
		if ((j < 10) || (j > d->len-11))
		{
			realoff += d->y[j].re/20;
			imgoff  += d->y[j].im/20;
		}
	}
	
	if (!*phase) *phase = atan2(imgoff, realoff);
	if (!*scale) *scale = realoff / cos(*phase);
}

void calculate_global_paramters (DataVector *d, GlobalParam *gparam)
{
	if (glob->IsReflection)
	{
		GetPhaseAndScale (d, &gparam->phase, &gparam->scale);
		glob->gparam->tau = 0.0;
	}
	else
	{
		GetTauByFFT (d, &gparam->tau, &gparam->phase); 
		glob->gparam->scale = 1.0;
	}
}

/* Returns TRUE if the first non-comment, non-empty file contains three doubles */
gboolean is_datafile (gchar *filename)
{
	gchar dataline[200];
	gdouble a=0, b, c;
#ifdef NO_ZLIB
	FILE *datafile;

	if (g_file_test (filename, G_FILE_TEST_IS_DIR))
		return FALSE;
	
	datafile = fopen (filename, "r");
#else
	gzFile *datafile;

	if (g_file_test (filename, G_FILE_TEST_IS_DIR))
		return FALSE;
	
	datafile = gzopen (filename, "r");
#endif

	if (!datafile)
	{
		/* file does not exist */
		return FALSE;
	}

#ifdef NO_ZLIB
	while (!feof (datafile)) {
		if (!(fgets (dataline, 199, datafile)))
			continue;
#else
	while (!gzeof (datafile)) {
		if (!(gzgets (datafile, dataline, 199)))
			continue;
#endif

		if ((dataline[0] == '#') || (dataline[0] == '\r') || (dataline[0] == '\n'))
			continue;

		if (sscanf (dataline, "%lf %lf %lf", &a, &b, &c) != 3) 
		{
			/* A line without data and not a comment */
#ifdef NO_ZLIB
			fclose (datafile);
#else
			gzclose (datafile);
#endif
			return FALSE;
		}
		else
			break;
	}
	
#ifdef NO_ZLIB
	fclose (datafile);
#else
	gzclose (datafile);
#endif

	if (a)
		return TRUE;
	else
		return FALSE;
}

/* Make the x-values unique to save some memory */
gboolean make_unique_dataset (DataVector *data)
{
	GPtrArray *othersets;
	DataVector *testvec;
	gdouble *found;
	guint setpos, i;

	g_return_val_if_fail (data, FALSE);

	othersets = get_unique_frqs ();
	found = NULL;
	setpos = 0;
	while ((!found) && (setpos < othersets->len))
	{
		testvec = (DataVector *) g_ptr_array_index (othersets, setpos);
		if (data->len == testvec->len)
		{
			i = 0;
			while ((i < data->len) && (testvec->x[i] == data->x[i]))
				i++;
			if (i == data->len)
				found = testvec->x;
		}
		setpos++;
	}
	g_ptr_array_free (othersets, TRUE);

	if (found)
	{
		/* The new frequency dataset is already present, use the old one */
		g_free (data->x);
		data->x = found;
		return TRUE;
	}

	return FALSE;
}

DataVector *import_datafile (gchar *filename, gboolean interactive)
{
	gchar dataline[200], *basename;
	DataVector *data, *testvec;
#ifdef NO_ZLIB
	FILE *datafile;
#else
	gzFile *datafile;
	gchar *tmpname = NULL;
#endif
	gdouble fmin, fmax;
	gint numpoints;
	gboolean data_is_fft = FALSE;
	
	ComplexDouble y;
	gdouble f;

	if (!filename)
		return NULL;

	if (g_file_test (filename, G_FILE_TEST_IS_DIR))
	{
		basename = g_path_get_basename (filename);
		dialog_message ("Cannot open '%s' as a datafile, this is a directory.", basename);
		g_free (basename);
		return NULL;
	}

#ifdef NO_ZLIB
	datafile = fopen (filename, "r");
	if (datafile == NULL) {
		dialog_message ("Error: Could not open file %s.", filename);
		return NULL;
	}
#else
	datafile = gzopen (filename, "r");
	if ((datafile == NULL) && (interactive)) {
		dialog_message ("Error: Could not open file %s.", filename);
		return NULL;
	}
	else if (datafile == NULL)
	{
		/* Perhaps the user (un)compressed the file */
		if (g_str_has_suffix (filename, ".dat"))
			/* Try with suffix .gz */
			tmpname = g_strdup_printf ("%s.gz", filename);
		else if (g_str_has_suffix (filename, ".gz"))
		{
			/* Try without suffix .gz */
			tmpname = g_strdup_printf ("%s", filename);
			tmpname[strlen (tmpname)-3] = '\0';
		}

		datafile = gzopen (tmpname, "r");
		if (datafile == NULL)
		{
			g_free (tmpname);
			dialog_message ("Error: Could not open file %s.", filename);
			return NULL;
		}

		/* It is not my task to free filename here. */
		filename = tmpname;
	}
#endif

	/* Check dataset and count number of points */
	numpoints = 0;
#ifdef NO_ZLIB
	while (!feof (datafile)) {
		if (!(fgets (dataline, 199, datafile)))
			continue;
#else
	while (!gzeof (datafile)) {
		if (!(gzgets (datafile, dataline, 199)))
			continue;
#endif

		if ((dataline[0] == '\r') || (dataline[0] == '\n'))
			continue;

		if (dataline[0] == '#')
		{
			if (g_str_has_prefix (dataline, "# FFT of '"))
				/* Looks like we're importing FFT data */
				data_is_fft = TRUE;

			if ((g_str_has_prefix (dataline, "# Source frequency range: ")) &&
			    (data_is_fft))
			{
				/* This IS a FFT dataset, recover the original
				 * start and stop frequency. */
				if (sscanf (dataline, "# Source frequency range: %lf - %lf GHz",
							&fmin, &fmax) == 2)
				{
					fmin *= 1e9;
					fmax *= 1e9;
				}
				else
				{
					data_is_fft = FALSE;
				}
			}
			continue;
		}

		if (sscanf (dataline, "%lf %lf %lf", &f, &y.re, &y.im) != 3) 
		{
			/* A line without data and not a comment */
#ifdef NO_ZLIB
			fclose (datafile);
#else
			gzclose (datafile);
			g_free (tmpname);
#endif
			dialog_message("Error: Could not parse dataset #%d.", numpoints+1);
			return NULL;
		} else {
			numpoints++;
		}
	}

	if ((data_is_fft) && (interactive))
	{
		if (dialog_question ("You are trying to import what looks like a time domain spectrum. Should the import be continued and a reverse Fourier Transform be applied?") == GTK_RESPONSE_NO)
		{
#ifdef NO_ZLIB
			fclose (datafile);
#else
			gzclose (datafile);
			g_free (tmpname);
#endif
			statusbar_message("Import data aborted");
			return NULL;
		}
	}

	data = new_datavector (numpoints);
	data->file = g_strdup (filename);

#ifdef NO_ZLIB
	rewind (datafile);
#else
	g_free (tmpname);
	gzrewind (datafile);
#endif

	/* Read the datafile for real now */
	numpoints = 0;
#ifdef NO_ZLIB
	while (!feof (datafile)) {
		if (!(fgets (dataline, 199, datafile)))
			continue;
#else
	while (!gzeof (datafile)) {
		if (!(gzgets (datafile, dataline, 199)))
			continue;
#endif

		if ((dataline[0] == '#') || (dataline[0] == '\r') || (dataline[0] == '\n'))
			/* Ignore comments and empty lines. */
			continue;

		if (sscanf(dataline, "%lf %lf %lf", &f, &y.re, &y.im) != 3) 
		{
#ifdef NO_ZLIB
			fclose (datafile);
#else
			gzclose (datafile);
#endif
			dialog_message("Error: Could not parse dataset #%d.", numpoints+1);
			free_datavector (data);
			return NULL;
		} else {
			y.abs = sqrt(y.re*y.re + y.im*y.im);
			if (numpoints < data->len)
			{
				data->x[numpoints] = f;
				data->y[numpoints] = y;
				numpoints++;
			}
		}
	}

#ifdef NO_ZLIB
	fclose (datafile);
#else
	gzclose (datafile);
#endif

	if (data_is_fft)
	{
		/* Some sanity checks */
		if ((data->x[0] != -data->x[numpoints-1]) ||
		    (data->x[numpoints/2] != 0.0))
		{
			dialog_message ("Error: Inverse FFT failed. The time data is not centered arount t=0.");
			free_datavector (data);
			return NULL;
		}

		/* Do the reverse transform of the FFT data */
		testvec = fourier_inverse_transform (data, fmin, fmax);
		free_datavector (data);
		if ((!testvec) || (testvec->len < 2))
		{
			dialog_message ("Error: Inverse FFT failed due to an unknown reason.");
			free_datavector (testvec);
			return NULL;
		}

		data = testvec;
	}

	/* Check for uniqueness of dataset */
	make_unique_dataset (data);

	return data;
}

void set_new_main_data (DataVector *newdata, gboolean called_from_open)
{
	gchar *title, *basename;

	glob->data = newdata;

	if (!called_from_open) 
	{
		/* This is needed as we may be called from network.c */
		g_free (glob->path);
		glob->path = g_path_get_dirname (newdata->file);
	}

	glob->noise = -1;

	if (!glob->gparam->min)
		glob->gparam->min = glob->data->x[0];
	if (!glob->gparam->max)
		glob->gparam->max = glob->data->x[glob->data->len - 1];

	/* Determine spectrum type */
	glob->IsReflection = IsReflectionSpectrum (glob->data);

	if (!called_from_open) 
	{
		calculate_global_paramters (glob->data, glob->gparam);

		basename = g_path_get_basename (glob->data->file);
		title = g_strdup_printf ("(%s) - GWignerFit", basename);
		gtk_window_set_title (
				GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")), 
				title);
		g_free (title);
		g_free (basename);

		g_free (glob->resonancefile);
		glob->resonancefile = NULL;
		g_free (glob->section);
		glob->section = NULL;
	}

	show_global_parameters (glob->gparam);

	on_load_spectrum ();
	visualize_update_min_max (0);
	visualize_draw_data ();
	visualize_theory_graph ();

	disable_undo ();

	if (glob->IsReflection)
	{
		gtk_check_menu_item_set_active (
			GTK_CHECK_MENU_ITEM (glade_xml_get_widget (gladexml, "reflection")),
			TRUE
		);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "tauentry"), FALSE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "tau_check"), FALSE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "label8"), FALSE);

		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scaleentry"), TRUE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scale_check"), TRUE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "label7"), TRUE);

		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (gladexml, "tau_check")), 
			FALSE
		);
	}
	else
	{
		gtk_check_menu_item_set_active (
			GTK_CHECK_MENU_ITEM (glade_xml_get_widget (gladexml, "transmission")),
			TRUE
		);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "tauentry"), TRUE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "tau_check"), TRUE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "label8"), TRUE);

		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scaleentry"), FALSE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scale_check"), FALSE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "label7"), FALSE);

		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (gladexml, "scale_check")), 
			FALSE
		);

		/* As the phase is redundant do not include it in a fit by default. */
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (gladexml, "phase_check")), 
			FALSE
		);
	}
}

gboolean read_datafile (gchar *selected_filename, gboolean called_from_open) 
{
	DataVector *newdata;

	/* If called_from_open is TRUE, then:
	 * - do not ask about fourier transformation on import_datafile()
	 * - do not update glob->path
	 * - do not set the title based on the datafile
	 */

	newdata = import_datafile (selected_filename, !called_from_open);

	if (newdata)
	{
		visualize_stop_background_calc ();
		overlay_remove_all ();
		visualize_newgraph ();

		if ((glob->data) && (glob->data->x == newdata->x))
			/* I must not free the x dataset as it is the same
			 * for the newdata set */
			glob->data->x = NULL;

		free_datavector   (glob->data);
		set_new_main_data (newdata, called_from_open);
		
		statusbar_message("Read %d datapoints", glob->data->len);
	}
	else
	{
		return FALSE;
	}

	return TRUE;
}

#define read_resonancefile_cleanup fclose (datafile); \
				  g_free (line); \
				  g_free (text); \
				  g_free (datafilename); \
				  g_free (command); \
				  g_ptr_array_free (ovrlays, TRUE); \
				  g_array_free (stddev, TRUE); \
				  set_busy_cursor (FALSE);

gint read_resonancefile (gchar *selected_filename, const gchar *label)
{
	gchar *line, *command, *text, *datafilename = NULL;
	FILE *datafile;
	gdouble min, max, frq, wid, amp, phas, tau;
	gdouble frqerr, widerr, amperr, phaserr;
	gdouble tauerr=0.0, scaleerr=0.0, gphaseerr=0.0;
	gint numres=0, pos=0, flag=0, i, ovrlaynum=0;
	Resonance *resonance=NULL;
	FourierComponent *fcomp=NULL;
	GPtrArray *ovrlays;
	GArray *stddev;

	datafile = fopen (selected_filename, "r");

	if (datafile == NULL) {
		dialog_message("Error: Could not open file %s.", selected_filename);
		return -1;
	}

	visualize_stop_background_calc ();
	on_spectral_close_activate (NULL, NULL);
	set_busy_cursor (TRUE);

	disable_undo ();
	clear_resonancelist ();
	fcomp_purge ();

	g_ptr_array_foreach (glob->param, (GFunc) g_free, NULL);
	g_ptr_array_free (glob->param, TRUE);
	glob->param = g_ptr_array_new ();

	g_ptr_array_foreach (glob->fcomp->data, (GFunc) g_free, NULL);
	g_ptr_array_free (glob->fcomp->data, TRUE);
	glob->fcomp->data = g_ptr_array_new ();
	glob->fcomp->numfcomp = 0;

	line = g_new0 (gchar, 256);
	text = g_new0 (gchar, 256);
	command = g_new0 (gchar, 256);
	ovrlays = g_ptr_array_new ();
	stddev = g_array_new (FALSE, TRUE, sizeof (gdouble));

	numres = 0;
	glob->gparam->tau = 0;
	glob->gparam->scale = 1;

	while (!feof(datafile)) {
		fgets(line, 255, datafile);
		if ((strlen(line)>0) && (line[strlen(line)-1] == '\n')) 
			line[strlen(line)-1] = '\0'; /* strip final \n */
		if ((strlen(line)>0) && (line[strlen(line)-1] == '\r')) 
			line[strlen(line)-1] = '\0'; /* strip final \r */
		pos++;

		if (strlen(line) > 254) {
			dialog_message ("Error: Line %i in '%s' too long!\n", pos, selected_filename);
			read_resonancefile_cleanup;
			return -1;
		}

		/* Stop parsing after the current section */
		if ((flag) && ((*line == '$') || (*line == '='))) break;

		if ((*line == '$') && (strncmp(line+1, label, 254) == 0)) {
			/* Found a matching "Stefan Bittner" style section */
			flag = 1;
			continue;
		}
		if ((*line == '=') && (strncmp(line+1, label, 254) == 0)) {
			/* Found a matching "Florian Schaefer" style section */
			flag = 10;
			continue;
		}

		/* Comment lines start with either '%' or '#' */
		if ((*line == '%') || (*line == '#') || (strlen(line) == 0)) continue;

		/* Actually parse the section */

		/* Begin "Stefan Bittner" style */
		if (flag == 2) {
			/* Information about each resonance */
			if (sscanf(line, "%lf\t%lf\t%lf\t%lf", &amp, &phas, &wid, &frq) != 4) {
				dialog_message ("Error: Expected AMP PHAS WID FRQ at line %i.\n", pos);
				read_resonancefile_cleanup;
				return -1;
			}
			resonance = g_new (Resonance, 1);
			resonance->frq   = frq * 1e9;
			resonance->width = wid * 1e6;
			resonance->amp   = amp;
			resonance->phase = phas;
			add_resonance_to_list (resonance);
			numres++;
		}
		if (flag == 1) {
			/* The "header" */
			if (sscanf(line, "%lf\t%lf\t%lf", &min, &max, &tau) != 3) {
				dialog_message ("Error: Expected MIN MAX TAU at line %i.\n", pos);
				read_resonancefile_cleanup;
				return -1;
			}
			glob->gparam->tau = tau * 1e-9;
			glob->gparam->min = min * 1e9;
			glob->gparam->max = max * 1e9;
			glob->gparam->scale = 1;
			flag = 2;
		}
		/* End "Stefan Bittner" style */

		/* Begin "Florian Schaefer" style */
		if (flag == 10) {
			i = sscanf(line, "%250s\t%lf\t%lf\t%lf\t%lf%lf\t%lf\t%lf\t%lf", 
					command, &frq, &wid, &amp, &phas, &frqerr, &widerr, &amperr, &phaserr);
			switch (i) {
				case 1: /* Overlay */
				  if (sscanf(line, "%250s\t%250s", command, text) == 2)
				  {
					  /* text may not hold the complete filename if it
					   * contained spaces. */
					  if (!strncmp(command, "file", 200)) 
						  datafilename = g_strdup (line+5);

					  /* Remember overlays for later addition */
					  if ((!strncmp(command, "ovrlay", 200)) && datafilename)
					  {
						g_ptr_array_add (
							ovrlays,
							(gpointer) g_strdup (line+7)
						);
						ovrlaynum++;
					  }
				  }
				  else
				  {
					  dialog_message ("Error: Unrecognized command '%s' in line %i.\n", command, pos);
					  read_resonancefile_cleanup;
					  return -1;
				  }
				  break;
				case 2: /* Global parameter */
				  if (!strncmp(command, "minfrq", 200)) {glob->gparam->min = frq * 1e9;}
				  else if (!strncmp(command, "maxfrq", 200)) {glob->gparam->max = frq * 1e9;}
				  else if (!strncmp(command, "tau", 200)) glob->gparam->tau = frq * 1e-9;
				  else if (!strncmp(command, "scale", 200)) glob->gparam->scale = frq;
				  else if (!strncmp(command, "phase", 200)) glob->gparam->phase = frq / 180*M_PI;
				  else { 
					  dialog_message("Error: Unrecognized command '%s' in line %i.\n", command, pos);
					  read_resonancefile_cleanup;
					  return -1;
				  }
				  break;
				case 3: /* Global parameter with error */
				  if (!strncmp(command, "tau", 200)) 
				  {
					  glob->gparam->tau = frq * 1e-9;
					  tauerr = wid * 1e-9;
				  }
				  else if (!strncmp(command, "scale", 200)) 
				  {
					  glob->gparam->scale = frq;
					  scaleerr = wid;
				  }
				  else if (!strncmp(command, "phase", 200)) 
				  {
					  glob->gparam->phase = frq / 180*M_PI;
					  gphaseerr = wid / 180*M_PI;
				  }
				  else { 
					  dialog_message("Error: Unrecognized command '%s' in line %i.\n", command, pos);
					  read_resonancefile_cleanup;
					  return -1;
				  }
				  break;
				case 4: /* Fourier component */
				  if (!strncmp(command, "fcomp", 200)) 
				  {
					fcomp = g_new (FourierComponent, 1);
					fcomp->amp = frq;
					fcomp->tau = wid / 1e9;
					fcomp->phi = amp / 180*M_PI;
					fcomp_add_component (fcomp, 0);
				  }
				  else { 
					  dialog_message("Error: Unrecognized command '%s' in line %i.\n", command, pos);
					  read_resonancefile_cleanup;
					  return -1;
				  }
				  break;
				case 5: /* Resonance data */
				  if (!strncmp(command, "res", 200)) {
					resonance = g_new (Resonance, 1);
					resonance->frq   = frq * 1e9;
					resonance->width = wid * 1e6;
					resonance->amp   = amp;
					resonance->phase = phas / 180*M_PI;
					add_resonance_to_list (resonance);
					numres++;
				  } else {
					  dialog_message ("Error: Unrecognized command '%s' in line %i.\n", command, pos);
					  read_resonancefile_cleanup;
					  return -1;
				  }
				  break;
				case 9: /* Resonance data with errors */
				  if (!strncmp(command, "res", 200)) {
					resonance = g_new (Resonance, 1);
					resonance->frq   = frq * 1e9;
					resonance->width = wid * 1e6;
					resonance->amp   = amp;
					resonance->phase = phas / 180*M_PI;
					add_resonance_to_list (resonance);

					phaserr *= 1.0/180*M_PI;
					frqerr  *= 1e9;
					widerr  *= 1e6;

					g_array_append_val (stddev, amperr);
					g_array_append_val (stddev, phaserr);
					g_array_append_val (stddev, frqerr);
					g_array_append_val (stddev, widerr);
					
					numres++;
				  } else {
					  dialog_message ("Error: Unrecognized command '%s' in line %i.\n", command, pos);
					  read_resonancefile_cleanup;
					  return -1;
				  }
				  break;
				default: /* Syntax error */
				  dialog_message ("Error: Syntax error in line %i\n", pos);
				  read_resonancefile_cleanup;
				  return -1;
			}
		}
		/* End "Florian Schaefer" style */
	}

	if ((feof(datafile)) && (flag == 0)) {
		dialog_message ("Error: No section '%s' found in '%s'.\n", label, selected_filename);
		read_resonancefile_cleanup;
		return -1;
	}

	fclose (datafile);
	g_free (line);
	g_free (command);
	g_free (text);

	glob->numres = numres;

	if (datafilename) 
	{
		read_datafile (datafilename, TRUE);
		g_free (datafilename);
	}

	/* Uncheck all checkboxes */
	uncheck_res_out_of_frq_win (-2, -1);
	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gladexml, "phase_check")), 
		FALSE
	);
	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gladexml, "scale_check")), 
		FALSE
	);
	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gladexml, "tau_check")), 
		FALSE
	);

	/* Add overlays */
	if (ovrlaynum && glob->data)
	{
		for (i=0; i<ovrlaynum; i++)
		{
			overlay_file (g_ptr_array_index (ovrlays, i));
			g_free (g_ptr_array_index (ovrlays, i));
		}
	}
	g_ptr_array_free (ovrlays, TRUE);

	/* Add parameter errors */
	g_free (glob->stddev);
	glob->stddev = NULL;
	if (stddev->len > 0)
	{
		glob->stddev = g_new0 (gdouble, stddev->len+4);

		for (i=0; i <= stddev->len; i++)
			glob->stddev[i+1] = g_array_index (stddev, gdouble, i);

		glob->stddev[stddev->len+1] = gphaseerr;
		glob->stddev[stddev->len+2] = scaleerr;
		glob->stddev[stddev->len+3] = tauerr;
	}
	g_array_free (stddev, TRUE);

	statusbar_message ("Loaded %i resonances", numres);
	set_busy_cursor (FALSE);

	return numres;
}

#undef read_resonancefile_cleanup

gboolean save_file (gchar *filename, gchar *section, gint exists)
{
	GString *old_file_before, *old_file_after = NULL;
	gchar line[256], *newline;
	GSList *overlays, *overlayspos;
	FILE *datafile;	
	Resonance *res;
	FourierComponent *fcomp;
	gint i;

	newline = g_new0 (gchar, 3);

	if (exists == 2)
	{
		/* File and section already exist */
		datafile = fopen (filename, "r");
		if (!datafile) return FALSE;

		old_file_before = g_string_new ("");
		old_file_after = g_string_new ("");

		/* Copy everything before the relevant section into old_file_before */
		while (!feof (datafile))
		{
			fgets (line, 255, datafile);

			if (strlen(line) > 254)
			{
				dialog_message ("Error: Line is too long!\n");
				fclose (datafile);
				return FALSE;
			}

			if ((line[strlen(line)-2] == '\r') && (*newline == 0)) newline = "\r\n\0";
			else newline = "\n\0\0";

			if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = '\0'; /* strip final \n */
			if (line[strlen(line)-1] == '\r') line[strlen(line)-1] = '\0'; /* strip final \r */

			if ((*line == '=') && (strncmp(line+1, section, 254) == 0))
			{
				/* Found the selected section (Florian style) */
				break;
			}

			g_string_append (old_file_before, line);
			g_string_append (old_file_before, newline);
		}
		if (feof (datafile))
		{
			fclose (datafile);
			return FALSE;
		}
		
		/* Skip the section to be saved */
		while (!feof (datafile))
		{
			fgets (line, 255, datafile);

			if (strlen(line) > 254)
			{
				dialog_message ("Error: Line is too long!\n");
				fclose (datafile);
				return FALSE;
			}

			if ((line[strlen(line)-2] == '\r') && (*newline == 0)) newline = "\r\n\0";
			else newline = "\n\0\0";

			if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = '\0'; /* strip final \n */
			if (line[strlen(line)-1] == '\r') line[strlen(line)-1] = '\0'; /* strip final \r */

			if (*line == '=')
			{
				/* Found the next section (Florian style) */
				g_string_append (old_file_after, line);
				g_string_append (old_file_after, newline);
				break;
			}
		}

		/* Copy everything after the relevant section into old_file_after */
		while (!feof (datafile))
		{
			fgets (line, 255, datafile);

			if (strlen(line) > 254)
			{
				dialog_message ("Error: Line is too long!\n");
				fclose (datafile);
				return FALSE;
			}

			g_string_append (old_file_after, line);
		}

		fclose (datafile);
		datafile = fopen (filename, "w");

		fprintf (datafile, "%s", old_file_before->str);
	}
	else if (exists == 1)
	{
		/* The file exists, but the section does not */
		datafile = fopen (filename, "a");
		if (!datafile) return FALSE;
		newline = "\n\0\0";
	}
	else
	{
		/* Nothing exists */
		datafile = fopen (filename, "w");
		if (!datafile) return FALSE;
		newline = "\n\0\0";
	}

	/* print the section into the datafile */
	fprintf (datafile, "=%s%s", section, newline);
	if (glob->data->file) 
		fprintf (datafile, "file\t%s%s", glob->data->file, newline);

	/* print the overlayed datafiles */
	overlays = overlay_get_filenames ();
	if ((overlays) && (glob->prefs->save_overlays) && 
	    (g_slist_length (overlays) > 0))
	{
		overlayspos = overlays;
		do {
			fprintf (datafile, "ovrlay\t%s%s",
					(gchar *) overlayspos->data,
					newline);
		} while ((overlayspos = g_slist_next (overlayspos)));

		g_slist_free (overlays);
	}
	
	fprintf (datafile, "minfrq\t%11.9f%s", glob->gparam->min/1e9, newline);
	fprintf (datafile, "maxfrq\t%11.9f%s", glob->gparam->max/1e9, newline);
	if (!glob->stddev)
	{
		/* Write the parameters without errors */
		fprintf (datafile, "phase\t%f%s", NormalisePhase(glob->gparam->phase)/M_PI*180, newline);
		
		if ( glob->IsReflection) 
			fprintf (datafile, "scale\t%f%s", glob->gparam->scale  , newline);
		if (!glob->IsReflection) 
			fprintf (datafile, "tau\t%f%s"  , glob->gparam->tau*1e9, newline);
		
		for (i=0; i<glob->numres; i++)
		{
			res = g_ptr_array_index (glob->param, i);
			fprintf (datafile, "res\t%11.9f\t%9f\t%11f\t% 11.6f%s",
					res->frq/1e9, res->width/1e6, res->amp, 
					NormalisePhase(res->phase)/M_PI*180, newline);
		}
		
		/* Write FourierComponents without error estimates */
		for (i=0; i<glob->fcomp->numfcomp; i++)
		{
			fcomp = g_ptr_array_index (glob->fcomp->data, i);
			fprintf (datafile, "fcomp\t%11.9f\t% 9.6f\t% 11.6f%s",
					fcomp->amp, fcomp->tau*1e9, 
					NormalisePhase(fcomp->phi)/M_PI*180,
					newline);
		}
	}
	else
	{
		/* Include the parameter errors */
		fprintf (datafile, "phase\t%f\t%f%s", 
				NormalisePhase(glob->gparam->phase)/M_PI*180, 
				glob->stddev[4*glob->numres+1]/M_PI*180,
				newline);
		if ( glob->IsReflection) 
			fprintf (datafile, "scale\t%f\t%f%s", 
				glob->gparam->scale,
				glob->stddev[4*glob->numres+2],
				newline);
		if (!glob->IsReflection) 
			fprintf (datafile, "tau\t%f\t%f%s", 
				glob->gparam->tau*1e9, 
				glob->stddev[4*glob->numres+3]*1e9,
				newline);
		for (i=0; i<glob->numres; i++)
		{
			res = g_ptr_array_index(glob->param, i);
			fprintf (datafile, "res\t%11.9f\t%9f\t%11f\t% 11.6f\t%12.10f\t%9.7f\t%11f\t% 11.6f%s",
				res->frq/1e9, res->width/1e6, res->amp,
				NormalisePhase(res->phase)/M_PI*180,
				glob->stddev[4*i+3]/1e9,	/* frqerr */
				glob->stddev[4*i+4]/1e6,	/* widerr */
				glob->stddev[4*i+1],		/* amperr */
				glob->stddev[4*i+2]/M_PI*180,	/* phaserr */
				newline);
		}
		
		/* Write FourierComponents without error estimates */
		for (i=0; i<glob->fcomp->numfcomp; i++)
		{
			fcomp = g_ptr_array_index (glob->fcomp->data, i);
			fprintf (datafile, "fcomp\t%11.9f\t% 9.6f\t% 11.6f%s",
					fcomp->amp, fcomp->tau*1e9, 
					NormalisePhase(fcomp->phi)/M_PI*180,
					newline);
		}
	}
	
	/* A newline before the next section */
	fprintf (datafile, "%s", newline);

	if (exists == 2)
	{
		/* Write the rest of the old file */
		fprintf (datafile, "%s", old_file_after->str);
	}

	fclose (datafile);

	return TRUE;
}

gboolean save_file_prepare (gchar *selected_filename) 
{
	gchar *title;
	gchar line[256];
	FILE *datafile;
	gint pos = 0, exists = 0;

	/* This is needed as filename is not aquired through get_filename() */
	g_free (glob->path);
	glob->path = g_path_get_dirname (selected_filename);

	datafile = fopen (selected_filename, "r");
	if (datafile) 
	{
		while (!feof (datafile))
		{
			fgets (line, 255, datafile);
			if (line[strlen(line)-1] == '\n') line[strlen(line)-1] = '\0'; /* strip final \n */
			if (line[strlen(line)-1] == '\r') line[strlen(line)-1] = '\0'; /* strip final \r */
			pos++;

			if (strlen(line) > 254)
			{
				fprintf(stderr, "Error: Line %i in '%s' too long!\n", pos, selected_filename);
				return FALSE;
			}

			if ((*line == '=') && (strncmp(line+1, glob->section, 254) == 0))
			{
				/* Found the selected section (Florian style) */
				exists = 2;
				break;
			}

			if ((*line == '$') || (*line == '=')) 
			{
				/* Found a new section */
				pos++;
			}
		}

		fclose (datafile);
		if (!exists) exists = 1;
	}

	if (exists == 2)
	{
		if (dialog_question ("Section '%s' already exists in '%s', overwrite?", 
				glob->section, g_path_get_basename (selected_filename)) 
				!= GTK_RESPONSE_YES)
			return FALSE;
	}
	else if ((exists == 1) && (glob->prefs->confirm_append))
	{
		if (dialog_question ("File '%s' exists, append section '%s'?", 
				g_path_get_basename (selected_filename), glob->section)
				!= GTK_RESPONSE_YES)
			return FALSE;
	}

	if (save_file (selected_filename, glob->section, exists))
	{
		/* Delete a backup file if one's there. */
		delete_backup ();
		
		g_free (glob->resonancefile);

		glob->resonancefile = g_strdup (selected_filename);
		
		title = g_strdup_printf ("%s:%s - GWignerFit", g_path_get_basename (glob->resonancefile), glob->section);
		gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")), title);
		g_free (title);

		statusbar_message ("Save operation successful");
		unset_unsaved_changes ();
	}

	return TRUE;
}

static double FindFWHM (DataVector *d, int center, double offset) {
	double fwhm1, fwhm2, halfmax;
	int i = center;

	if ((i < 4) || (i > (d->len-5))) return 0;

	offset*=offset;
	halfmax = (pow(d->y[center].abs,2) - offset) / 2 + offset;

	/* estimate FWHM to the left */
	while ( !(((pow(d->y[i].abs,2)<halfmax) && (pow(d->y[i-1].abs,2)>halfmax)) ||
		  ((pow(d->y[i].abs,2)>halfmax) && (pow(d->y[i-1].abs,2)<halfmax))) && (i>1)) i--;
	if (--i != 0) {
		fwhm1 = 2 * (d->x[center] - d->x[i]);
	} else {
		fwhm1 = 1e10;
	}
	if (fwhm1 < 0) fwhm1 = 1e10;

	/* estimate FWHM to the right */
	i = center;
	while ( !(((pow(d->y[i].abs,2)<halfmax) && (pow(d->y[i+1].abs,2)>halfmax)) ||
		  ((pow(d->y[i].abs,2)>halfmax) && (pow(d->y[i+1].abs,2)<halfmax))) && (i<d->len-2)) i++;
	if (++i != 0) {
		fwhm2 = 2 * (d->x[i] - d->x[center]);
	} else {
		fwhm2 = 1e10;
	}
	if (fwhm2 < 0) fwhm2 = 1e10;

	/* return the smaller of the two values */
	if (fwhm1 <  fwhm2) return fwhm1;
	if (fwhm2 <= fwhm1) return fwhm2;
	
	return 0; /* no valid FWHM found */
}

Resonance *find_resonance_at (double frq, DataVector *d)
{
	Resonance *resonance;
	gint res = 0;
	double scale = glob->gparam->scale;
	double alpha = glob->gparam->phase;
	double tau   = glob->gparam->tau;

	resonance = g_new (Resonance, 1);

	while ((frq > d->x[res]) && (res < d->len)) res++;
	if (res == d->len) 
	{
		g_free(resonance);
		return NULL;
	}

	resonance->frq = frq;
	
	if ((resonance->width = FindFWHM(d, res, glob->IsReflection ? scale : 0)) == 0) {
		g_free(resonance);
		return NULL;
	}

	if (glob->IsReflection) {
		/* The amplitude and phase are calculated by demanding that the
		 * real and complex value at the resoance should equal the data.
		 */
		resonance->amp = sqrt(
				pow(1-(d->y[res].re*cos(alpha)+d->y[res].im*sin(alpha))/scale,2)+
				pow(d->y[res].im*cos(alpha)-d->y[res].re*sin(alpha),2)/scale/scale
			       ) * resonance->width / 2;

		resonance->phase = atan2(
			pow(d->y[res].im*cos(alpha)-d->y[res].re*sin(alpha),2)/scale/scale,
			pow(1-(d->y[res].re*cos(alpha)+d->y[res].im*sin(alpha))/scale,2)
		       );
		resonance->phase = 0; /* Theory says, phi should be zero. */
	} else {
		resonance->amp = d->y[res].abs * resonance->width / 2 / scale;

		resonance->phase = NormalisePhase(atan2(d->y[res].im, d->y[res].re) 
				+ 2*M_PI*tau*d->x[res] + M_PI/2);
	}

	return resonance;
}

static void silence_region (DataVector *d, gint n, gint pos, gfloat thresh)
{
	gint i;

	i = pos;
	while ( (i < n) && 
		(glob->IsReflection ? d->y[i].abs < thresh : d->y[i].abs > thresh) ) 
	{
		d->y[i].abs = thresh + (glob->IsReflection ? +0.001 : -0.001);
		i++;
	}

	i = pos-1;
	while ( (i > 0) &&
		(glob->IsReflection ? d->y[i].abs < thresh : d->y[i].abs > thresh) ) 
	{
		d->y[i].abs = thresh + (glob->IsReflection ? +0.001 : -0.001);
		i--;
	}
}

gint find_isolated_resonances (gfloat thresh)
{
	gint npoints = 0, i, j, offset = 0, resadded = 0, posfound, oldposfound;
	Resonance *resonance;
	ComplexDouble y;
	DataVector *d;
	gdouble *p;

	/* Find the data inside the frequency range */
	for (i=0; i<glob->data->len; i++)
	{
		if (( glob->data->x[i] > glob->gparam->min ) &&
		    ( glob->data->x[i] < glob->gparam->max )) 
		{
			if (npoints == 0) offset = i;
			npoints++;
		}
		if (glob->data->x[i] >= glob->gparam->max) break;
	}

	if (npoints == 0)
	{
		/* Something has gone terribly wrong */
		dialog_message ("Error: No region for resonance finder found, check your frequency range.");
		return 0;
	}

	d = new_datavector (npoints);

	/* Remove already identified resonances from the data */
	p = g_new (gdouble, TOTALNUMPARAM+1);
	create_param_array (glob->param, glob->fcomp->data, glob->gparam, 
			glob->numres, glob->fcomp->numfcomp, p);
	
	for (i=0; i<npoints; i++)
	{
		d->x[i] = glob->data->x[i+offset];
		y = ComplexWigner(d->x[i], p, TOTALNUMPARAM);
		d->y[i].abs = glob->data->y[i+offset].abs - y.abs + glob->IsReflection;
		d->y[i].re  = glob->data->y[i+offset].re  - y.re 
			     + cos(glob->gparam->phase)*glob->gparam->scale*glob->IsReflection;
		d->y[i].im  = glob->data->y[i+offset].im  - y.im 
			     + sin(glob->gparam->phase)*glob->gparam->scale*glob->IsReflection;
	}

	g_free (p);

	/* and silence the regions - just to be sure */
	for (i=0; i<glob->numres; i++)
	{
		resonance = g_ptr_array_index (glob->param, i);
		j = 0;
		while ((resonance->frq > d->x[j]) && (j < npoints)) j++;
		silence_region (d, npoints, j, thresh);
	}

	/* Identify resonances till everything is below the threshold */
	oldposfound = -1;
	posfound = FindResonance (d, glob->IsReflection ? -1 : +1);
	while (glob->IsReflection ? d->y[posfound].abs < thresh : d->y[posfound].abs > thresh)
	{
		if ((resonance = find_resonance_at (d->x[posfound], d)))
		{
			add_resonance_to_list (resonance);

			p = g_new (gdouble, 4*1+NUM_GLOB_PARAM+3*glob->fcomp->numfcomp+1);

			p[1] = resonance->amp;
			p[2] = resonance->phase;
			p[3] = resonance->frq;
			p[4] = resonance->width;
			p[5] = glob->gparam->phase;
			p[6] = glob->gparam->scale;
			p[7] = glob->gparam->tau;

			/* Silence the resonance _before_ I try to calculate it out */
			silence_region (d, npoints, posfound, thresh);
			
			for (i=0; i<npoints; i++)
			{
				y = ComplexWigner(d->x[i], p, 4*1+NUM_GLOB_PARAM+3*glob->fcomp->numfcomp);
				if ((glob->IsReflection ? d->y[i].abs < thresh : d->y[i].abs > thresh)) 
					/* Update .abs only outside silenced regions */
					d->y[i].abs = d->y[i].abs - y.abs + glob->IsReflection*glob->gparam->scale;
				d->y[i].re = d->y[i].re - y.re + 
					cos(glob->gparam->phase)*glob->gparam->scale*glob->IsReflection;
				d->y[i].im = d->y[i].im - y.im + 
					sin(glob->gparam->phase)*glob->gparam->scale*glob->IsReflection;
			}
			g_free (p);

			resadded++;

			/* In case the whole func needs too much time */
			while (gtk_events_pending ()) gtk_main_iteration ();
		}
		else 
			/* If no FWHM could be determined, silence the region anyway */
			silence_region (d, npoints, posfound, thresh);

		/* Find the next resonance */
		posfound = FindResonance (d, glob->IsReflection ? -1 : +1);

		/* Break if we keep on finding the same resonance */
		if (posfound == oldposfound)
		{
			dialog_message ("Resonance finder stopped due to abnormal behaviour.");
			break;
		}
		oldposfound = posfound;
	}

	free_datavector (d);

	spectral_resonances_changed ();

	return resadded;
}
