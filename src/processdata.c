#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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
#include "loadsave.h"
#include "callbacks.h"
#include "calibrate_offline.h"

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

		if ((dataline[0] == '#') || (dataline[0] == '!') || (dataline[0] == '\r') || (dataline[0] == '\n'))
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

/* Adjust the read S1P data into GWignerFit's standard form */
void adjust_snp_data (DataVector *data, gchar *options)
{
	gdouble frqmultiply = 1e9;
	gchar format = 1;
	gchar *snpoptions;
	gdouble re, im, tmp;
	ComplexDouble R;
	guint i;

	snpoptions = g_ascii_strup (options, strlen (options));

	/* Determine units of frequency */
	if (g_strrstr (snpoptions, " MHZ "))
		frqmultiply = 1e6;
	if (g_strrstr (snpoptions, " KHZ "))
		frqmultiply = 1e3;
	if (g_strrstr (snpoptions, " HZ "))
		frqmultiply = 1.0;

	/* Determine format of complex data */
	if (g_strrstr (snpoptions, " DB "))
		format = 2;
	if (g_strrstr (snpoptions, " MA "))
		format = 3;

	/* Adjust what we have learned so far */
	for (i=0; i<data->len; i++)
	{
		data->x[i] *= frqmultiply;

		if (format != 1)
		{
			if (format == 2)
				/* dB and phase representation */
				tmp = pow (10, data->y[i].re / 20.0);
			else
				/* amplitude and phase representation */
				tmp = data->y[i].re;
			re = tmp * cos (data->y[i].im / 180 * M_PI);
			im = tmp * sin (data->y[i].im / 180 * M_PI);
			data->y[i].re  = re;
			data->y[i].im  = im;
			data->y[i].abs = tmp;
		}
	}

	/* Determine system impedance */
	R.re  = 50;
	R.im  = 0;
	R.abs = 50;
	if (g_strrstr (snpoptions, " R "))
		sscanf (g_strrstr (snpoptions, " R "), " R %lf ", &R.re);

	/* Performe conversion Impedance -> Scattering parameter */
	if (g_strrstr (snpoptions, " Z "))
		for (i=0; i<data->len; i++)
		{
			/* R (Z+R) / (Z-R) */
			data->y[i] = c_mul (R, c_div (c_add (data->y[i], R), c_sub (data->y[i], R)));
			data->y[i].abs = sqrt (data->y[i].re*data->y[i].re+data->y[i].im*data->y[i].im);
		}

	if (g_strrstr (snpoptions, " Y ") || g_strrstr (snpoptions, " H ") || g_strrstr (snpoptions, " G "))
	{
		dialog_message ("Error: Encounterd unknown data format in SNP data file.");
	}

	g_free (snpoptions);
}

gint s2p_sparam_choose ()
{
	GladeXML *xml;
	gint result, retval;

	xml = glade_xml_new (GLADEFILE, "sparam_dialog", NULL);

	/* This should somehow be adjustable in the glade file, too. Somehow... */
	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "sparam_s12_radio")), 
		TRUE);

	result = gtk_dialog_run (GTK_DIALOG (glade_xml_get_widget (xml, "sparam_dialog")));
	if (result == GTK_RESPONSE_OK)
	{
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "sparam_s11_radio"))))
			retval = 1;
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "sparam_s12_radio"))))
			retval = 2;
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "sparam_s21_radio"))))
			retval = 3;
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (xml, "sparam_s22_radio"))))
			retval = 4;
	}
	else
		retval = 0;

	gtk_widget_destroy (glade_xml_get_widget (xml, "sparam_dialog"));
	return retval;
}

/* Tries very hard to retrieve spectrum data from a given filename */
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
	struct stat filestat;
	guint filesize, filebytepos;
	gint scanresult;
	
	ComplexDouble y;
	gdouble f, dummy;

	gint snp = 0;
	gchar *snpoptions = NULL;
	const gchar *sparams[] = {"S11", "S12", "S21", "S22"};

	if (!filename)
		return NULL;

	if (g_file_test (filename, G_FILE_TEST_IS_DIR))
	{
		basename = g_path_get_basename (filename);
		dialog_message ("Cannot open '%s' as a datafile, this is a directory.", basename);
		g_free (basename);
		return NULL;
	}

	/* Handle S1P und S2P files */
	if (g_str_has_suffix (filename, ".s1p") || g_str_has_suffix (filename, ".s1p.gz"))
	{
		snp = -1;
	}
	if (g_str_has_suffix (filename, ".s2p") || g_str_has_suffix (filename, ".s2p.gz"))
	{
		snp = s2p_sparam_choose ();
		if (!snp)
		{
			statusbar_message("Import data aborted");
			return NULL;
		}
	}
	if (g_strrstr (filename, ".s2p:S") || g_strrstr (filename, ".s2p.gz:S"))
	{
		if (g_str_has_suffix (filename, "S11")) snp = 1;
		if (g_str_has_suffix (filename, "S12")) snp = 2;
		if (g_str_has_suffix (filename, "S21")) snp = 3;
		if (g_str_has_suffix (filename, "S22")) snp = 4;
		filename[strlen (filename) - 4] = '\0';
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
		if (g_str_has_suffix (filename, ".dat") || g_str_has_suffix (filename, ".s1p"))
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

	/* Estimate filesize */
	stat (filename, &filestat);
	filesize = (guint) filestat.st_size;
	if (g_str_has_suffix (filename, ".gz"))
		/* Estimate original size of uncompressed file */
		filesize *= 5;

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

		if ((dataline[0] == '\r') || (dataline[0] == '\n') || (dataline[0] == '!'))
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
					data_is_fft = FALSE;
			}
			continue;
		}

		if (filesize > 3*1024*1025)
		{
			/* Show a progress indicator for files > 3 MB */
#ifdef NO_ZLIB
			filebytepos = ftell (datafile);
#else
			filebytepos = gztell (datafile);
#endif
			if (filebytepos % (filesize/100*2*1) < 100)
				/* Progressbar from 0% to 50% in steps of 1% */
				status_progressbar_set ((gdouble)filebytepos/(gdouble)filesize/2.0);
		}

		numpoints++;
#if 0
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
#endif
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

	if (g_path_is_absolute (filename))
		data->file = normalize_path (filename);
	else
	{
		basename = g_get_current_dir ();
		data->file = filename_make_absolute (filename, basename);
		g_free (basename);
	}

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

		if ((dataline[0] == '#') && (snp))
		{
			/* Remember options of s1p or s2p data file */
			g_free (snpoptions);
			snpoptions = g_strdup_printf ("%s ", dataline);
			continue;
		}

		if ((dataline[0] == '#') || (dataline[0] == '!') || (dataline[0] == '\r') || (dataline[0] == '\n'))
			/* Ignore comments and empty lines. */
			continue;

		switch (snp)
		{
			case 2:
				scanresult = sscanf(dataline, "%lf %lf %lf %lf %lf", &f, &dummy, &dummy, &y.re, &y.im) - 2;
				break;
			case 3:
				scanresult = sscanf(dataline, "%lf %lf %lf %lf %lf %lf %lf", &f, &dummy, &dummy, &dummy, &dummy, &y.re, &y.im) - 4;
				break;
			case 4:
				scanresult = sscanf(dataline, "%lf %lf %lf %lf %lf %lf %lf %lf %lf", &f, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &y.re, &y.im) - 6;
				break;
			default:
				scanresult = sscanf(dataline, "%lf %lf %lf", &f, &y.re, &y.im);
				break;
		}
		if (scanresult != 3) 
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

		if ((filesize > 3*1024*1025) && (numpoints % (data->len/100*2*1) == 0))
			/* Progressbar from 50% to 100% in steps of 1% */
			status_progressbar_set (0.5 + (gdouble)numpoints/(gdouble)data->len/2.0);
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

	if (snp && !data_is_fft)
	{
		/* Parse snp options line */
		adjust_snp_data (data, snpoptions);
		if (snp > 0)
		{
			/* Mark selection in filename */
			tmpname = g_strdup_printf ("%s:%s", data->file, sparams[snp-1]);
			g_free (data->file);
			data->file = tmpname;
		}
	}
	g_free (snpoptions);

	status_progressbar_set (-1);

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

		basename = g_filename_display_basename (glob->data->file);
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
	visualize_draw_data ();
	if (!called_from_open)
	{
		visualize_update_min_max (0);
		visualize_theory_graph ("u");
	}

	disable_undo ();

	/* Do not trigger the callback */
	g_signal_handlers_block_by_func (
			glade_xml_get_widget (gladexml, "reflection"),
			on_reflection_activate, NULL);
	g_signal_handlers_block_by_func (
			glade_xml_get_widget (gladexml, "transmission"),
			on_transmission_activate, NULL);

	if (glob->IsReflection)
	{
		gtk_check_menu_item_set_active (
			GTK_CHECK_MENU_ITEM (glade_xml_get_widget (gladexml, "reflection")),
			TRUE
		);

		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scaleentry"), TRUE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scale_check"), TRUE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "label7"), TRUE);
	}
	else
	{
		gtk_check_menu_item_set_active (
			GTK_CHECK_MENU_ITEM (glade_xml_get_widget (gladexml, "transmission")),
			TRUE
		);

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

	g_signal_handlers_unblock_by_func (
			glade_xml_get_widget (gladexml, "reflection"),
			on_reflection_activate, NULL);
	g_signal_handlers_unblock_by_func (
			glade_xml_get_widget (gladexml, "transmission"),
			on_transmission_activate, NULL);
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

#define read_resonancefile_cleanup g_ptr_array_foreach (lines, (GFunc) g_free, NULL); \
				  g_ptr_array_free (lines, TRUE); \
				  g_free (text); \
				  g_free (datafilename); \
				  g_free (command); \
				  g_ptr_array_free (ovrlays, TRUE); \
				  g_array_free (stddev, TRUE); \
				  set_busy_cursor (FALSE);

gint read_resonancefile (gchar *selected_filename, const gchar *label)
{
	gchar *line, *command, *text, *datafilename = NULL;
	gdouble frq, wid, amp, phas;
#if 0
	gdouble tau, min, max;
#endif
	gdouble frqerr, widerr, amperr, phaserr;
	gdouble tauerr=0.0, scaleerr=0.0, gphaseerr=0.0;
	gint numres=0, pos=0, i, ovrlaynum=0, offset;
	guint r, g, b;
	Resonance *resonance=NULL;
	FourierComponent *fcomp=NULL;
	GPtrArray *ovrlays, *lines, *colors;
	GArray *stddev;
	GdkColor *color;
	gchar *comment = NULL, *commenttmp;

	/* Get section */
	lines = ls_read_section (selected_filename, (gchar *) label, '=');

	if (!lines)
		return -1;

	if (lines->len == 0)
	{
		g_ptr_array_free (lines, TRUE);
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

	text = g_new0 (gchar, 256);
	command = g_new0 (gchar, 256);
	ovrlays = g_ptr_array_new ();
	colors  = g_ptr_array_new ();
	stddev = g_array_new (FALSE, TRUE, sizeof (gdouble));

	numres = 0;
	glob->gparam->tau = 0;
	glob->gparam->scale = 1;

	for (pos=0; pos<lines->len; pos++)
	{
		line = (gchar *) g_ptr_array_index (lines, pos);

		/* Comment lines start with either '%' or '#' */
		if ((strlen (line) > 2) && (line[0] == '#') && (line[1] == ' '))
		{
			if (comment)
				commenttmp = g_strdup_printf ("%s%s\n", comment, line+2);
			else
				commenttmp = g_strdup_printf ("%s\n", line+2);
			g_free (comment);
			comment = commenttmp;
		}
		if ((*line == '%') || (*line == '#') || (strlen (line) == 0)) continue;

		/* Actually parse the section */
#if 0
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
#endif
		/* Begin "Florian Schaefer" style */
		if ((g_str_has_prefix (line, "file")) || (g_str_has_prefix (line, "ovrlay")))
		{
			if (sscanf(line, "%250s\t%250s", command, text) == 2)
			{
				/* text may not hold the complete filename if it
				 * contained spaces. */
				if ((!strncmp(command, "file", 200)) && (strlen (line) > 6))
					datafilename = filename_make_absolute (line+5, selected_filename);

				/* Remember overlays for later addition */
				if ((!strncmp(command, "ovrlay", 200)) && datafilename)
				{
					offset = 0;
					color = g_new (GdkColor, 1);

					if ((line[7] == '#') && (strlen(line) > 16) && (line[14] == ' ') &&
							(sscanf (line+8, "%02x%02x%02x", &r, &g, &b) == 3))
					{
						/* Line has color information */
						offset = 8;
						color->red   = (guint) ((gfloat)r/255.0*65535.0);
						color->green = (guint) ((gfloat)g/255.0*65535.0);
						color->blue  = (guint) ((gfloat)b/255.0*65535.0);
					}
					else
					{
						color->red   = 45000;
						color->green = 45000;
						color->blue  = 45000;
					}

					g_ptr_array_add (
						ovrlays,
						(gpointer) filename_make_absolute (line+7+offset, selected_filename)
						);
					g_ptr_array_add (
						colors,
						color
						);
					ovrlaynum++;
				}
			}
			else
			{
				dialog_message ("Error: Unrecognized command '%s' in line %i of section.\n", command, pos);
				read_resonancefile_cleanup;
				return -1;
			}

			continue;
		}
		
		i = sscanf (line, "%250s\t%lf\t%lf\t%lf\t%lf%lf\t%lf\t%lf\t%lf", 
				command, &frq, &wid, &amp, &phas, &frqerr, &widerr, &amperr, &phaserr);

		switch (i) {
			case 1:
			  dialog_message("Error: Unrecognized command '%s' in line %i of section.\n", command, pos);
			  read_resonancefile_cleanup;
			  return -1;
			  break;
			case 2: /* Global parameter */
			  if (!strncmp(command, "minfrq", 200)) {glob->gparam->min = frq * 1e9;}
			  else if (!strncmp(command, "maxfrq", 200)) {glob->gparam->max = frq * 1e9;}
			  else if (!strncmp(command, "tau", 200)) glob->gparam->tau = frq * 1e-9;
			  else if (!strncmp(command, "scale", 200)) glob->gparam->scale = frq;
			  else if (!strncmp(command, "phase", 200)) glob->gparam->phase = frq / 180*M_PI;
			  else { 
				  dialog_message("Error: Unrecognized command '%s' in line %i of section.\n", command, pos);
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
				  dialog_message("Error: Unrecognized command '%s' in line %i of section.\n", command, pos);
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
				  dialog_message("Error: Unrecognized command '%s' in line %i of section.\n", command, pos);
				  read_resonancefile_cleanup;
				  return -1;
			  }
			  break;
			case 5: /* Resonance data */

			  /* line with a timestamp? */
			  if (!strncmp(command, "date", 200)) 
			  {
				  /* nothing to be done yet */
				  break;
			  }
			  
			  if (!strncmp(command, "res", 200)) {
				resonance = g_new (Resonance, 1);
				resonance->frq   = frq * 1e9;
				resonance->width = wid * 1e6;
				resonance->amp   = amp;
				resonance->phase = phas / 180*M_PI;
				add_resonance_to_list (resonance);
				numres++;
			  } else {
				  dialog_message ("Error: Unrecognized command '%s' in line %i of section.\n", command, pos);
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
				  dialog_message ("Error: Unrecognized command '%s' in line %i of section.\n", command, pos);
				  read_resonancefile_cleanup;
				  return -1;
			  }
			  break;
			default: /* Syntax error */
			  dialog_message ("Error: Syntax error in line %i of section.\n", pos);
			  read_resonancefile_cleanup;
			  return -1;
		}
		/* End "Florian Schaefer" style */
	}

	g_ptr_array_foreach (lines, (GFunc) g_free, NULL);
	g_ptr_array_free (lines, TRUE);

	g_free (command);
	g_free (text);

	glob->numres = numres;

	g_free (glob->comment);
	glob->comment = comment;

	if (datafilename) 
	{
		statusbar_message ("Open: Reading spectrum data...");
		while (gtk_events_pending ()) gtk_main_iteration ();
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

	statusbar_message ("Open: Calculating theory graph...");
	while (gtk_events_pending ()) gtk_main_iteration ();
	visualize_theory_graph ("n");

	/* Add overlays */
	if (ovrlaynum && glob->data)
	{
		for (i=0; i<ovrlaynum; i++)
		{
			statusbar_message ("Open: Overlaying spectrum %d of %d...", i, ovrlaynum);
			while (gtk_events_pending ()) gtk_main_iteration ();

			overlay_file (g_ptr_array_index (ovrlays, i));
			g_free (g_ptr_array_index (ovrlays, i));

			/* uid from last glob->overlayspectra entry */
			overlay_set_color (
					((DataVector *) g_ptr_array_index (glob->overlayspectra, glob->overlayspectra->len-1))->index, 
					*(GdkColor *) g_ptr_array_index (colors, i));
			g_free (g_ptr_array_index (colors, i));
		}
	}
	g_ptr_array_free (ovrlays, TRUE);
	g_ptr_array_free (colors, TRUE);

	/* Add parameter errors */
	g_free (glob->stddev);
	glob->stddev = NULL;
	if (stddev->len > 0)
	{
		glob->stddev = g_new0 (gdouble, stddev->len+4);

		for (i=0; i < stddev->len; i++)
			glob->stddev[i+1] = g_array_index (stddev, gdouble, i);

		glob->stddev[stddev->len+1] = gphaseerr;
		glob->stddev[stddev->len+2] = scaleerr;
		glob->stddev[stddev->len+3] = tauerr;
	}
	g_array_free (stddev, TRUE);

	statusbar_message ("Open: Loaded %i resonances", numres);
	set_busy_cursor (FALSE);

	return numres;
}

#undef read_resonancefile_cleanup

/* Takes a filename, asks for the section and load the gwf resonance file.
 * Returns TRUE on success. */
gboolean load_gwf_resonance_file (gchar *filename)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	gchar *title, *basename;
	gchar *section = NULL;
	GList *sections = NULL;

	sections = ls_get_sections (filename, "=$");

	if (!sections)
	{
		dialog_message ("No appropriate sections found in this file.");
		return FALSE;
	}
	
	section = ls_select_section (sections, glob->section);
	g_list_foreach (sections, (GFunc) g_free, NULL);
	g_list_free (sections);

	if (!section)
		return FALSE;

	on_comment_done (NULL, (gpointer) 1);

	/* Resonances may be added, so enable the theory graph */
	gtk_check_menu_item_set_active (
		GTK_CHECK_MENU_ITEM (glade_xml_get_widget (gladexml, "view_theory")),
		TRUE
	);

	if (read_resonancefile (filename, section) >= 0)
	{
		glob->section = section;
		glob->resonancefile = g_strdup (filename);
		visualize_update_res_bar (0);
		show_global_parameters (glob->gparam);

		basename = g_filename_display_basename (glob->resonancefile);
		title = g_strdup_printf ("%s:%s - GWignerFit", basename, glob->section);
		gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")), title);
		g_free (basename);
		g_free (title);

		gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (graph));
		visualize_update_min_max (1);

		spectral_resonances_changed ();
		unset_unsaved_changes ();
	}
	else
	{
		/* No resonances were read but the old parameters have been
		 * deleted by read_resonancefile, get us safely out of here.
		 */
		g_free (section);
		unset_unsaved_changes ();
		on_new_activate (NULL, NULL);
	}

	return TRUE;
}

/* Function to write all resonance information into datafile */
void save_write_section (FILE *datafile, gchar *filename, gchar *section, gchar *newline)
{
	GSList *overlays, *overlayspos;
	GdkColor color;
	gchar *text, *name;
	Resonance *res;
	FourierComponent *fcomp;
	gint i, j;

	/* print the section into the datafile */
	cfprintf (datafile, "=%s%s", section, newline);

	/* print comments */
	if (glob->comment && strlen (glob->comment))
	{
		cfprintf (datafile, "# ");
		j = 0;
		for (i=0; i<strlen (glob->comment); i++)
		{
			if (j > 250)
			{
				cfprintf (datafile, "%c%s# ", glob->comment[i], newline);
				j = 0;
				continue;
			}
			
			if (glob->comment[i] != '\n')
			{
				fputc (glob->comment[i], datafile);
				j++;
			}
			else
			{
				j = 0;
				if (i < strlen (glob->comment)-1)
					cfprintf (datafile, "%s# ", newline);
				else
					cfprintf (datafile, "%s", newline);
			}
		}

		if (j)
			/* The last character has NOT been a '\n'. */
			cfprintf (datafile, "%s", newline);
	}

	text = get_timestamp ();
	cfprintf (datafile, "date\t%s%s", text, newline);
	g_free (text);
	
	if ((glob->data) && (glob->data->file))
	{
		if (glob->prefs->relative_paths)
		{
			if (!(name = filename_make_relative (glob->data->file, filename)) )
				name = g_strdup (glob->data->file);
			cfprintf (datafile, "file\t%s%s", name, newline);
			g_free (name);
		}
		else
			cfprintf (datafile, "file\t%s%s", glob->data->file, newline);
	}

	/* print the overlayed datafiles */
	overlays = overlay_get_filenames ();
	if ((overlays) && (glob->prefs->save_overlays) && 
	    (g_slist_length (overlays) > 0))
	{
		overlayspos = overlays;
		do {
			overlay_get_color (&color, FALSE, GPOINTER_TO_UINT (overlayspos->data), NULL);
			cfprintf (datafile, "ovrlay\t#%02x%02x%02x",
					(guint) (((gfloat) color.red)/65535.0*255.0),
					(guint) (((gfloat) color.green)/65535.0*255.0),
					(guint) (((gfloat) color.blue)/65535.0*255.0));
			overlayspos = g_slist_next (overlayspos);
			g_return_if_fail (overlayspos);

			if (glob->prefs->relative_paths)
			{
				if (!(name = filename_make_relative ((gchar *) overlayspos->data, filename)) )
					name = g_strdup ((gchar *) overlayspos->data);
				cfprintf (datafile, " %s%s", name, newline);
				g_free (name);
			}
			else
				cfprintf (datafile, " %s%s", (gchar *) overlayspos->data, newline);
			
		} while ((overlayspos = g_slist_next (overlayspos)));

		g_slist_free (overlays);
	}
	
	cfprintf (datafile, "minfrq\t%11.9f%s", glob->gparam->min/1e9, newline);
	cfprintf (datafile, "maxfrq\t%11.9f%s", glob->gparam->max/1e9, newline);
	if (!glob->stddev)
	{
		/* Write the parameters without errors */
		cfprintf (datafile, "phase\t%f%s", NormalisePhase(glob->gparam->phase)/M_PI*180, newline);
		
		if ( glob->IsReflection) 
			cfprintf (datafile, "scale\t%f%s", glob->gparam->scale  , newline);
		
		cfprintf (datafile, "tau\t%f%s"  , glob->gparam->tau*1e9, newline);
		
		for (i=0; i<glob->numres; i++)
		{
			res = g_ptr_array_index (glob->param, i);
			cfprintf (datafile, "res\t%11.9f\t%9f\t%11f\t% 11.6f%s",
					res->frq/1e9, res->width/1e6, res->amp, 
					NormalisePhase(res->phase)/M_PI*180, newline);
		}
		
		/* Write FourierComponents without error estimates */
		for (i=0; i<glob->fcomp->numfcomp; i++)
		{
			fcomp = g_ptr_array_index (glob->fcomp->data, i);
			cfprintf (datafile, "fcomp\t%11.9f\t% 9.6f\t% 11.6f%s",
					fcomp->amp, fcomp->tau*1e9, 
					NormalisePhase(fcomp->phi)/M_PI*180,
					newline);
		}
	}
	else
	{
		/* Include the parameter errors */
		cfprintf (datafile, "phase\t%f\t%f%s", 
				NormalisePhase(glob->gparam->phase)/M_PI*180, 
				glob->stddev[4*glob->numres+1]/M_PI*180,
				newline);

		if ( glob->IsReflection) 
			cfprintf (datafile, "scale\t%f\t%f%s", 
				glob->gparam->scale,
				glob->stddev[4*glob->numres+2],
				newline);

		cfprintf (datafile, "tau\t%f\t%f%s", 
			glob->gparam->tau*1e9, 
			glob->stddev[4*glob->numres+3]*1e9,
			newline);

		for (i=0; i<glob->numres; i++)
		{
			res = g_ptr_array_index(glob->param, i);
			cfprintf (datafile, "res\t%11.9f\t%9f\t%11f\t% 11.6f\t%12.10f\t%9.7f\t%11f\t% 11.6f%s",
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
			cfprintf (datafile, "fcomp\t%11.9f\t% 9.6f\t% 11.6f%s",
					fcomp->amp, fcomp->tau*1e9, 
					NormalisePhase(fcomp->phi)/M_PI*180,
					newline);
		}
	}
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
