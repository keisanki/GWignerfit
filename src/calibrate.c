#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "structs.h"
#include "helpers.h"
#include "processdata.h"
#include "overlay.h"
#include "calibrate_offline.h"
#include "calibrate_vna.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

/* Sets the TextEntry with the GladeXML name *entry to *text */
static void cal_set_text (gchar *entry, gchar *text)
{
	if (!text)
		return;

	gtk_entry_set_text (
		GTK_ENTRY (
			glade_xml_get_widget (glob->calwin->xmlcal, entry)),
		text);

	gtk_editable_set_position (
		GTK_EDITABLE (
			glade_xml_get_widget (glob->calwin->xmlcal, entry)),
		-1);
}

/* Retrieves the filename from the TextEntry names *entry and
 * checks it for validity */
static gchar* cal_get_valid_file (gchar *entry)
{
	gchar *text;
	FILE *fileh;
	
	g_return_val_if_fail (entry, NULL);

	text = g_strdup (gtk_entry_get_text (GTK_ENTRY 
			(glade_xml_get_widget (glob->calwin->xmlcal, entry))));

	if (text == NULL)
		return NULL;

	if (g_file_test (text, G_FILE_TEST_IS_DIR))
	{
		g_free (text);
		return NULL;
	}

	fileh = fopen (text, "r");
	if (!fileh)
	{
		g_free (text);
		return NULL;
	}
	fclose (fileh);

	return text;
}

/* Try to guess filenames for full 2-port calibration */
static void cal_fill_full_filenames (gchar *filename)
{
	gchar *pos, *prefix, *suffix, *newname;
	gchar *entries[] = {"cal_full_s11short_entry", "cal_full_s11load_entry", 
		            "cal_full_s22open_entry", "cal_full_s22short_entry", "cal_full_s22load_entry",
			    "cal_full_s11thru_entry", "cal_full_s12thru_entry", "cal_full_s21thru_entry", "cal_full_s22thru_entry"};
	gchar *innames[] = {"short_S11", "load_S11", "open_S22", "short_S22", "load_S22",
		            "thru_S11", "thru_S12", "thru_S21", "thru_S22"};
	gint i;

	g_return_if_fail (filename);
	g_return_if_fail (glob->calwin);
	g_return_if_fail (glob->calwin->xmlcal);

	if (!(pos = g_strrstr (filename, "open_S11")))
		return;

	pos[0] = '\0';
	prefix = g_strdup (filename);
	suffix = g_strdup (pos+8);
	pos[0] = 'o';

	for (i=0; i<9; i++)
	{
		newname = g_strdup_printf ("%s%s%s", prefix, innames[i], suffix);
		gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (glob->calwin->xmlcal, entries[i])), newname);
		g_free (newname);
	}

	g_free (prefix);
	g_free (suffix);
}

/* Try to guess filenames for TRL calibration */
static void cal_fill_trl_filenames (gchar *filename)
{
	gchar *pos, *prefix, *suffix, *newname;
	gchar *entries[] = {"cal_trl_s11thru_entry", "cal_trl_s12thru_entry", "cal_trl_s21thru_entry", "cal_trl_s22thru_entry", 
		            "cal_trl_a1thru_entry" , "cal_trl_a2thru_entry",
		            "cal_trl_s11refl_entry", "cal_trl_s22refl_entry",
			    "cal_trl_s11line_entry", "cal_trl_s12line_entry", "cal_trl_s21line_entry", "cal_trl_s22line_entry", 
			    "cal_trl_a1line_entry", "cal_trl_a2line_entry"};
	gchar *innames[] = {"thru_S11", "thru_S12", "thru_S21", "thru_S22", "thru_Sa1", "thru_Sa2",
		            "refl_S11", "refl_S22",
			    "line_S11", "line_S12", "line_S21", "line_S22", "line_Sa1", "line_Sa2"};
	gint i;

	g_return_if_fail (filename);
	g_return_if_fail (glob->calwin);
	g_return_if_fail (glob->calwin->xmlcal);

	if (!(pos = g_strrstr (filename, "thru_S11")))
		return;

	pos[0] = '\0';
	prefix = g_strdup (filename);
	suffix = g_strdup (pos+8);
	pos[0] = 'o';

	for (i=0; i<14; i++)
	{
		newname = g_strdup_printf ("%s%s%s", prefix, innames[i], suffix);
		gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (glob->calwin->xmlcal, entries[i])), newname);
		g_free (newname);
	}

	g_free (prefix);
	g_free (suffix);
}

/* Callback for Select buttons */
static gboolean cal_choose_file (GtkButton *button, gpointer user_data)
{
	gchar *filename, *defaultname, check;

	check = 1;
	if (
		(button == GTK_BUTTON (glade_xml_get_widget (glob->calwin->xmlcal, "cal_out_but"))) ||
		(button == GTK_BUTTON (glade_xml_get_widget (glob->calwin->xmlcal, "cal_trans_out_but"))) ||
		(button == GTK_BUTTON (glade_xml_get_widget (glob->calwin->xmlcal, "cal_full_s11out_but"))) ||
		(button == GTK_BUTTON (glade_xml_get_widget (glob->calwin->xmlcal, "cal_full_s12out_but"))) ||
		(button == GTK_BUTTON (glade_xml_get_widget (glob->calwin->xmlcal, "cal_full_s21out_but"))) ||
		(button == GTK_BUTTON (glade_xml_get_widget (glob->calwin->xmlcal, "cal_full_s22out_but")))
	   )
		check = 2;

	defaultname = get_defaultname (NULL);
	filename = get_filename ("Select datafile", defaultname, check);
	g_free (defaultname);

	if (!filename)
		return FALSE;

	gtk_entry_set_text (
		GTK_ENTRY (user_data),
		filename);

	gtk_editable_set_position (
		GTK_EDITABLE (user_data),
		-1);

	/* Try to guess filenames for full 2-port calibration */
	if (button == GTK_BUTTON (glade_xml_get_widget (glob->calwin->xmlcal, "cal_full_s11open_but")))
		cal_fill_full_filenames (filename);

	/* Try to guess filenames for trl calibration */
	if (button == GTK_BUTTON (glade_xml_get_widget (glob->calwin->xmlcal, "cal_trl_s11thru_but")))
		cal_fill_trl_filenames (filename);

	return TRUE;
}

/* Callback when changeing the calibration type notebook */
void cal_caltype_changed (GtkNotebook *notebook, gpointer user_data) 
{
	GladeXML *xmlcal = glob->calwin->xmlcal;

	glob->calwin->calib_type = gtk_notebook_get_current_page (notebook);

	switch (glob->calwin->calib_type)
	{
		case 0:
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_offline_radio" ), TRUE);
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_online_radio" ), TRUE);
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_proxy_label" ), TRUE);
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_proxy_entry" ), TRUE);
			break;
		case 1:
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_offline_radio" ), TRUE);
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_online_radio" ), FALSE);
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_proxy_label" ), FALSE);
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_proxy_entry" ), FALSE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmlcal, "cal_offline_radio")), TRUE);
			break;
		case 2: case 3:
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_offline_radio" ), FALSE);
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_online_radio" ), TRUE);
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_proxy_label" ), TRUE);
			gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_proxy_entry" ), TRUE);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmlcal, "cal_online_radio")), TRUE);
			break;
	}
}

/* Callback when changeing the calibration method */
void cal_method_toggled (GtkToggleButton *togglebutton, gpointer user_data) 
{
	GladeXML *xmlcal = glob->calwin->xmlcal;
	gboolean state;

	/* state == TRUE for offline calibration */
	state = gtk_toggle_button_get_active (togglebutton);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_proxy_hbox" ), !state);

	glob->calwin->offline = state;
}

/* For "full 2-port" and "TRL" cal: Takes the filenames from the TextEntries
 * and stores them in the CalWin struct with proper error checking. */
static gboolean cal_check_fullcal_entries (CalWin *calwin)
{
	gint i;
	gchar *file, *entryname;
	gchar *innames[] = {"full_s11in", "full_s12in", "full_s21in", "full_s22in",
			    "full_s11open", "full_s11short", "full_s11load",
			    "full_s22open", "full_s22short", "full_s22load",
			    "full_s11thru", "full_s12thru", "full_s21thru", "full_s22thru",
			    "trl_s11in", "trl_s12in", "trl_s21in", "trl_s22in",
			    "trl_s11thru", "trl_s12thru", "trl_s21thru", "trl_s22thru", "trl_a1thru", "trl_a2thru",
			    "trl_s11refl", "trl_s22refl",
			    "trl_s11line", "trl_s12line", "trl_s21line", "trl_s22line", "trl_a1line", "trl_a2line",
			    NULL};
	gchar *outnames[] = {"full_s11out", "full_s12out", "full_s21out", "full_s22out",
			    "trl_s11out", "trl_s12out", "trl_s21out", "trl_s22out",
			    NULL};

	/* Check input files */
	i = calwin->calib_type == 2 ? 0 : 14;
	while (innames[i] && (i < (calwin->calib_type == 2 ? 14 : 32)))
	{
		entryname = g_strconcat ("cal_", innames[i], "_entry", NULL);
		file = cal_get_valid_file (entryname);
		g_free (entryname);

		if (!file)
		{
			dialog_message ("Could not open file with \"%s\" data.", innames[i]);
			return FALSE;
		}

		calwin->full_filenames[i] = file;

		i++;
	}

	/* Check output files */
	i = calwin->calib_type == 2 ? 0 : 4;
	while (outnames[i])
	{
		entryname = g_strconcat ("cal_", outnames[i], "_entry", NULL);
		file = g_strdup (gtk_entry_get_text (GTK_ENTRY 
				(glade_xml_get_widget (glob->calwin->xmlcal, entryname))));
		g_free (entryname);

		if (!file_is_writeable (file))
			return FALSE;

		if (!strlen(file))
		{
			dialog_message ("Please input a name for the output \"%s\" data file.", outnames[i]);
			return FALSE;
		}

		/* Output filenames come last */
		calwin->full_filenames[i+32] = file;

		i++;
	}

	return TRUE;
}

/* For "simple" calibrations: Takes the filenames from the TextEntries and
 * stores them in the CalWin struct with proper error checking */
static gboolean cal_check_entries ()
{
	CalWin *calwin = glob->calwin;
	gchar *file;

	if (!calwin->offline)
	{
		calwin->proxyhost = g_strdup (gtk_entry_get_text (GTK_ENTRY 
				(glade_xml_get_widget (calwin->xmlcal, "cal_proxy_entry"))));
		if (!calwin->proxyhost[0])
		{
			g_free (calwin->proxyhost);
			calwin->proxyhost = NULL;
			dialog_message ("Please enter a Ieee488Proxy hostname.");
			return FALSE;
		}
	}

	if (calwin->calib_type >= 2)
		/* Do bells and whistles for full 2-port and TRL cal somewhere else */
		return cal_check_fullcal_entries (calwin);

	if (   ((calwin->calib_type == 0 ) && (file = cal_get_valid_file ("cal_in_entry"))) 
	    || (file = cal_get_valid_file ("cal_trans_in_entry")))
		calwin->in_file = file;
	else
	{
		dialog_message ("Could not open file with input data.");
		return FALSE;
	}
		
	if (calwin->calib_type == 0)
		file = g_strdup (gtk_entry_get_text (GTK_ENTRY 
				(glade_xml_get_widget (glob->calwin->xmlcal, "cal_out_entry"))));
	else
		file = g_strdup (gtk_entry_get_text (GTK_ENTRY 
				(glade_xml_get_widget (glob->calwin->xmlcal, "cal_trans_out_entry"))));

	if (!strlen (file))
	{
		dialog_message ("Please input a name for the output data file.");
		return FALSE;
	}
	
	if (!file_is_writeable (file))
		return FALSE;
	calwin->out_file = file;

	if (calwin->calib_type == 0)
	{
		if ((file = cal_get_valid_file ("cal_open_entry")))
			calwin->open_file = file;
		else
		{
			dialog_message ("Could not open file with open standard data.");
			return FALSE;
		}
		
		if ((file = cal_get_valid_file ("cal_short_entry")))
			calwin->short_file = file;
		else
		{
			dialog_message ("Could not open file with short standard data.");
			return FALSE;
		}

		if ((file = cal_get_valid_file ("cal_load_entry")))
			calwin->load_file = file;
		else
		{
			dialog_message ("Could not open file with load standard data.");
			return FALSE;
		}
	}
	else
	{
		if ((file = cal_get_valid_file ("cal_thru_entry")))
			calwin->thru_file = file;
		else
		{
			dialog_message ("Could not open file with thru data.");
			return FALSE;
		}
		
		/* Isolation data is optional */
		file = g_strdup (gtk_entry_get_text (GTK_ENTRY 
				(glade_xml_get_widget (glob->calwin->xmlcal, "cal_isol_entry"))));
		if (*file)
		{
			if ((file = cal_get_valid_file ("cal_isol_entry")))
				calwin->isol_file = file;
			else
			{
				dialog_message ("Could not open file with isolation data.");
				return FALSE;
			}
		}
		else
			calwin->isol_file = NULL;
	}

	return TRUE;
}

/* Sets the ProgressBar to fraction and runs the main loop */
void cal_update_progress (gfloat fraction)
{
	gchar *text;
	
	if (fraction >= 0.0)
	{
		gtk_progress_bar_set_fraction (
			GTK_PROGRESS_BAR (glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress")),
			fraction);

		text = g_strdup_printf ("%.0f %%", fraction * 100.0);
		gtk_progress_bar_set_text (
			GTK_PROGRESS_BAR (glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress")),
			text);
		g_free (text);
	}
	else
	{
		/* fraction < 0 -> disable ProgressBar */
		gtk_progress_bar_set_fraction (
			GTK_PROGRESS_BAR (glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress")),
			0.0);

		gtk_progress_bar_set_text (
			GTK_PROGRESS_BAR (glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress")),
			" ");

		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress_frame" ), 
			FALSE);
	}

	while (gtk_events_pending ()) gtk_main_iteration ();
}

/* Update time estimates and progress bar */
gboolean cal_show_time_estimates ()
{
	gchar *text;
	GTimeVal curtime;
	gint h1, m1, s1;
	gint h2, m2, s2;
	gint h3, m3, s3;
	glong timeleft;
	GtkProgressBar *pbar;
	gdouble fraction;

	if ((!glob->calwin) || (!glob->calwin->xmlcal))
		return FALSE;

	g_get_current_time (&curtime);
	
	sec_to_hhmmss (curtime.tv_sec - glob->calwin->start_t.tv_sec, &h1, &m1, &s1);
	sec_to_hhmmss (glob->calwin->estim_t, &h2, &m2, &s2);

	timeleft = glob->calwin->estim_t - (curtime.tv_sec - glob->calwin->start_t.tv_sec);
	if (timeleft < 0)
		timeleft = 0;
	sec_to_hhmmss (timeleft, &h3, &m3, &s3);

	text = g_strdup_printf ("%02d:%02d:%02d of %02d:%02d:%02d (%02d:%02d:%02d left)",
		h1, m1, s1, h2, m2, s2, h3, m3, s3);
	gtk_label_set_text (
		GTK_LABEL (glade_xml_get_widget (glob->calwin->xmlcal, "cal_timeest_label")),
		text);
	g_free (text);

	if (glob->calwin->estim_t > 0)
	{
		pbar = GTK_PROGRESS_BAR (glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress"));

		fraction = 1.0 - (gdouble) timeleft / (gdouble) glob->calwin->estim_t;
		if (fraction > gtk_progress_bar_get_fraction (pbar))
		{
			/* Only update progress bar if there is real progress */
			gtk_progress_bar_set_fraction (pbar, fraction);
			//text = g_strdup_printf ("%.0f %%", fraction * 100.0);
			//gtk_progress_bar_set_text (pbar, text);
			//g_free (text);
		}
	}

	if (! (glob->flag & FLAG_VNA_CAL))
		return FALSE;

	/* This timeout will not be deleted. */
	return TRUE;
}

/* Recalculate ETA based on the current and start time */
void cal_update_time_estimates (int *windone, int *winleft)
{
	GTimeVal curtime, difftime;

	if (*winleft)
	{
		/* Recalcualte ETA (only if we aren't finished yet) */
		g_get_current_time (&curtime);
		difftime.tv_sec  = curtime.tv_sec  -  glob->calwin->start_t.tv_sec;
		difftime.tv_usec = curtime.tv_usec -  glob->calwin->start_t.tv_usec;
		(*windone)++;	/* Frequency windows already measured */
		(*winleft)--;	/* Those left to be measured */

		glob->calwin->estim_t = curtime.tv_sec - glob->calwin->start_t.tv_sec
			+ (glong)( (float)difftime.tv_sec  * ((float)*winleft/(float)*windone))
			+ (glong)(((float)difftime.tv_usec * ((float)*winleft/(float)*windone))/1e6);
	}
}

/* For full 2-port and TRL calibration: Takes the filenames out of the CalWin
 * struct and reads those files with proper error checking. Calls the actual
 * calibration routines afterwards. */
static gboolean cal_do_full_calibration (CalWin *calwin)
{
	DataVector **data, **outdata;
	gint i, j, inlength = 14, inoffset = 0, outoffset = 0;
	FILE *fh;

	if (calwin->calib_type == 3)
	{
		/* We will do a TRL calibration -> the relevant data is
		 * at another position in the full_filenames array */
		inlength  = 18;
		inoffset  = 14;
		outoffset = 4;
	}

	/* Will hold the actual input spectrum data */
	data = g_new0 (DataVector*, inlength);

	/* Read and check input files */
	for (i=0; i<inlength; i++)
	{
		data[i] = import_datafile (calwin->full_filenames[i+inoffset], TRUE);

		if (!data[i])
		{
			for (j=0; j<i; j++) free_datavector (data[j]);
			g_free (data);
			cal_update_progress (-1.0);
			return FALSE;
		}

		/* Check lengths */
		if (data[i]->len != data[0]->len)
		{
			for (j=0; j<i; j++) free_datavector (data[j]);
			g_free (data);
			cal_update_progress (-1.0);
			dialog_message ("Error: Data sets do not have the same lengths.");
			return FALSE;
		}

		/* Check frequencies */
		if (i > 0)
		{
			j = 0;
			while ((j<data[0]->len) && (data[i]->x[j] == data[0]->x[j]))
				j++;

			if (j != data[0]->len)
			{
				for (j=0; j<i; j++) free_datavector (data[j]);
				g_free (data);
				cal_update_progress (-1.0);
				dialog_message ("Error: Data sets do not have the same frequencies.");
				return FALSE;
			}
		}

		cal_update_progress ((gfloat)(i+1)/(gfloat) inlength);
		while (gtk_events_pending ()) gtk_main_iteration ();
	}

	cal_update_progress (0.0);
	while (gtk_events_pending ()) gtk_main_iteration ();

	/* Reserve memory for calibrated data */
	outdata = g_new0 (DataVector*, 4);
	for (i=0; i<4; i++)
	{
		outdata[i] = new_datavector (data[0]->len);
		outdata[i]->file = g_strdup (calwin->full_filenames[i+32+outoffset]);
	}

	/* Write output data headers */
	for (j=0; j<4; j++)
	{
		fh = fopen (calwin->full_filenames[j+32+outoffset], "w");
		if (!fh)
		{
			dialog_message ("Error: Could not open output file.");
			for (j=0; j<4; j++) free_datavector (outdata[j]);
			g_free (outdata);
			return FALSE;
		}

		fprintf (fh, "# Spectrum calibrated by GWignerFit\r\n#\r\n");
		if (calwin->calib_type == 2)
			fprintf (fh, "# Calibration type: online full two-port calibration\r\n");
		else
			fprintf (fh, "# Calibration type: online TRL calibration\r\n");
		switch (j)
		{
			case 0:
				fprintf (fh, "# S-matrix element: S11\r\n");
				break;
			case 1:
				fprintf (fh, "# S-matrix element: S12\r\n");
				break;
			case 2:
				fprintf (fh, "# S-matrix element: S21\r\n");
				break;
			case 3:
				fprintf (fh, "# S-matrix element: S22\r\n");
				break;
		}
		fprintf (fh, "#\r\n# Uncalibrated input files:\r\n");
		fprintf (fh, "#   S11: %s\r\n", calwin->full_filenames[0+inoffset]);
		fprintf (fh, "#   S12: %s\r\n", calwin->full_filenames[1+inoffset]);
		fprintf (fh, "#   S21: %s\r\n", calwin->full_filenames[2+inoffset]);
		fprintf (fh, "#   S22: %s\r\n", calwin->full_filenames[3+inoffset]);

		fprintf (fh, "# Calibration standard files:\r\n");
		for (i=4; i<inlength; i++)
			fprintf (fh, "#    %2d: %s\r\n", i-3, calwin->full_filenames[i+inoffset]);
		fprintf (fh, DATAHDR);
		fclose (fh);
	}
	
	/* Do the calibration */
	cal_vna_full_calibrate (data, outdata, calwin->proxyhost, calwin->calib_type);

	/* Free calibration input data */
	for (j=0; j<14; j++) 
		free_datavector (data[j]);
	g_free (data);

	/* Free output data if calibration did not finish */
	if (!outdata[0]->file)
	{
		for (j=0; j<4; j++) free_datavector (outdata[j]);
		g_free (outdata);
		return FALSE;
	}

	/* Give the proper filenames */
	outdata[0]->file = g_strdup (calwin->full_filenames[32+outoffset]);
	outdata[1]->file = g_strdup (calwin->full_filenames[33+outoffset]);
	outdata[2]->file = g_strdup (calwin->full_filenames[34+outoffset]);
	outdata[3]->file = g_strdup (calwin->full_filenames[35+outoffset]);

	cal_update_progress (-1.0);

	/* Add calibrated graphs to display */
	if (!glob->data)
		/* Add as main graph */
		set_new_main_data (outdata[0], FALSE);
	else
		/* Add as overlay */
		overlay_add_data (outdata[0]);
	overlay_add_data (outdata[1]);
	overlay_add_data (outdata[2]);
	overlay_add_data (outdata[3]);

	return TRUE;
}

/* Takes the filenames out of the CalWin struct and reads those
 * files with proper error checking. Calls the actual 
 * calibration routines afterwards. */
#define free_memory free_datavector (in); \
		    free_datavector (a);  \
		    free_datavector (b);  \
		    free_datavector (c);
static gboolean cal_do_calibration ()
{
	CalWin *calwin;
	DataVector *in, *a, *b, *c;
	DataVector *out;
	gchar *text;
	guint i;
	FILE *fh;

	g_return_val_if_fail (glob->calwin, FALSE);
	calwin = glob->calwin;

	/* Enable and set progress bar */
	text = g_strdup_printf ("Reading datafiles ...");
	gtk_progress_bar_set_text (
		GTK_PROGRESS_BAR (glade_xml_get_widget (calwin->xmlcal, "cal_progress")),
		text);
	g_free (text);
	gtk_widget_set_sensitive (
		glade_xml_get_widget (calwin->xmlcal, "cal_progress_frame" ), 
		TRUE);
	while (gtk_events_pending ()) gtk_main_iteration ();

	if (calwin->calib_type >= 2)
		/* Do bells and whistles for full 2-port and TRL cal somewhere else */
		return cal_do_full_calibration (calwin);

	in = out = a = b = c = NULL;

	/* Read main datafile */
	if (!(in = import_datafile (calwin->in_file, TRUE)))
		return FALSE;

	if (calwin->calib_type == 0)
	{
		/* Calibrate reflection data */

		if (!(a = import_datafile (calwin->open_file, TRUE)))
		{
			free_memory;
			return FALSE;
		}
		if (!(b  = import_datafile (calwin->short_file, TRUE)))
		{
			free_memory;
			return FALSE;
		}
		if (!(c  = import_datafile (calwin->load_file, TRUE)))
		{
			free_memory;
			return FALSE;
		}

		/* Check lengths */
		if ((in->len != a->len) || (in->len != b->len) || (in->len != c->len))
		{
			free_memory;
			dialog_message ("Error: Data sets do not have the same lengths.");
			return FALSE;
		}

		/* Check frequency values */
		i = 0;
		while ((i < in->len) &&
		       (in->x[i] == a->x[i]) &&
		       (in->x[i] == b->x[i]) &&
		       (in->x[i] == c->x[i]))
			i++;
		if (i != in->len)
		{
			free_memory;
			dialog_message ("Error: Data sets do not have the same lengths.");
			return FALSE;
		}

		gtk_widget_set_sensitive (
			glade_xml_get_widget (calwin->xmlcal, "cal_progress_frame" ), 
			TRUE);

		if (calwin->offline)
			out = cal_reflection (in, a, b, c);
		else
			out = cal_vna_calibrate (in, a, b, c, calwin->proxyhost, calwin->calib_type);
	}
	else
	{
		/* Calibrate transmission data */

		if (!(a  = import_datafile (calwin->thru_file, TRUE)))
		{
			free_memory;
			return FALSE;
		}
		if (calwin->isol_file)
		{
			if (!(b  = import_datafile (calwin->isol_file, TRUE)))
			{
				free_memory;
				return FALSE;
			}
		}
		else
			b = NULL;

		if ((in->len != a->len) || ((b) && (in->len != b->len)))
		{
			free_memory;
			dialog_message ("Error: Data sets do not have the same lengths.");
			return FALSE;
		}

		i = 0;
		while ((i < in->len) &&
		       (in->x[i] == a->x[i]) &&
		       ((!b) || (in->x[i] == b->x[i])))
			i++;
		if (i != in->len)
		{
			free_memory;
			dialog_message ("Error: Data sets do not have the same frequencies.");
			return FALSE;
		}

		if (calwin->offline)
			out = cal_transmission (in, a, b);
		else
			out = cal_vna_calibrate (in, a, b, NULL, calwin->proxyhost, calwin->calib_type);
	}

	cal_update_progress (-1.0);

	if (!out)
	{
		free_memory;
		return FALSE;
	}

	out->file = g_strdup (calwin->out_file);

	/* Write calibrated file to disk */
	fh = fopen (out->file, "w");
	if (!fh)
	{
		dialog_message ("Error: Could not open output file.");
		free_memory;
		free_datavector (out);
		return FALSE;
	}
	text = g_strdup_printf ("Writing output file ...");
	gtk_progress_bar_set_text (
		GTK_PROGRESS_BAR (glade_xml_get_widget (calwin->xmlcal, "cal_progress")),
		text);
	g_free (text);
	while (gtk_events_pending ()) gtk_main_iteration ();

	if (calwin->offline)
	{
		if (calwin->calib_type == 0)
		{
			fprintf (fh, "# Reflection spectrum calibrated by GWignerFit\r\n#\r\n");
			fprintf (fh, "# Calibration type        : offline calibration\r\n");
			fprintf (fh, "# Uncalibrated input file : %s\r\n", in->file);
			fprintf (fh, "# Open standard data file : %s\r\n", a->file);
			fprintf (fh, "# Short standard data file: %s\r\n", b->file);
			fprintf (fh, "# Load standard data file : %s\r\n", c->file);
			fprintf (fh, "#\r\n# Calibration standard characteristics:\r\n");
			fprintf (fh, "# Open standard time delay : %e sec\r\n", glob->prefs->cal_tauO);
			fprintf (fh, "# Short standard time delay: %e sec\r\n", glob->prefs->cal_tauS);
			fprintf (fh, "# Open standard capacity coefficients:\r\n");
			fprintf (fh, "#   C0 = %e, C1 = %e, C2 = %e, C3 = %e\r\n",
					glob->prefs->cal_C0, glob->prefs->cal_C1, 
					glob->prefs->cal_C2, glob->prefs->cal_C3);
		}
		else
		{
			fprintf (fh, "# Transmission spectrum calibrated by GWignerFit\r\n#\r\n");
			fprintf (fh, "# Calibration type        : offline calibration\r\n");
			fprintf (fh, "# Uncalibrated input file : %s\r\n", in->file);
			fprintf (fh, "# Thru data file          : %s\r\n", a->file);
			fprintf (fh, "# Isolation data file     : %s\r\n", b ? b->file : "(omitted)");
		}
	}
	else
	{
		fprintf (fh, "# Reflection spectrum calibrated by GWignerFit\r\n#\r\n");
		fprintf (fh, "# Calibration type        : online one-port calibration\r\n");
		fprintf (fh, "# Uncalibrated input file : %s\r\n", in->file);
		fprintf (fh, "# Open standard data file : %s\r\n", a->file);
		fprintf (fh, "# Short standard data file: %s\r\n", b->file);
		fprintf (fh, "# Load standard data file : %s\r\n", c->file);
	}
	fprintf (fh, DATAHDR);
	for (i=0; i<out->len; i++)
		fprintf (fh, DATAFRMT, out->x[i], out->y[i].re, out->y[i].im);
	fclose (fh);
	cal_update_progress (-1.0);

	/* Free in, a, b, c */
	free_memory;

	/* Add calibrated graph to display */
	if (!glob->data)
		/* Add as main graph */
		set_new_main_data (out, FALSE);
	else
		/* Add as overlay */
		overlay_add_data (out);

	return TRUE;
}
#undef free_memory

/* Enable SelectFilname buttons */
static void cal_connect_select_but (CalWin *calwin)
{
	gint i;
	gchar *butname, *entryname;
	gchar *names[] = {"in", "out", "trans_in", "trans_out", "open", "short", "load", "thru", "isol", 
	                  "full_s11in", "full_s12in", "full_s21in", "full_s22in",
			  "full_s11open", "full_s11short", "full_s11load",
			  "full_s22open", "full_s22short", "full_s22load",
	                  "full_s11thru", "full_s12thru", "full_s21thru", "full_s22thru",
	                  "full_s11out", "full_s12out", "full_s21out", "full_s22out",
	                  "trl_s11in", "trl_s12in", "trl_s21in", "trl_s22in",
	                  "trl_s11out", "trl_s12out", "trl_s21out", "trl_s22out",
			  "trl_s11thru", "trl_s12thru", "trl_s21thru", "trl_s22thru", "trl_a1thru", "trl_a2thru",
			  "trl_s11refl", "trl_s22refl",
			  "trl_s11line", "trl_s12line", "trl_s21line", "trl_s22line", "trl_a1line", "trl_a2line",
	                  NULL};

	i = 0;
	while (names[i])
	{
		butname   = g_strconcat ("cal_", names[i], "_but",   NULL);
		entryname = g_strconcat ("cal_", names[i], "_entry", NULL);

		g_signal_connect (
			G_OBJECT (glade_xml_get_widget (calwin->xmlcal, butname)), 
			"clicked",
			(GCallback) cal_choose_file, 
			glade_xml_get_widget (calwin->xmlcal, entryname));

		g_free (butname);
		g_free (entryname);

		i++;
	}
}

/* Restore entries for full 2-port and TRL calibrations */
static void cal_set_full_texts (CalWin *calwin)
{
	gint i;
	gchar *entryname;
	gchar *names[] = {"full_s11in", "full_s12in", "full_s21in", "full_s22in",
			  "full_s11open", "full_s11short", "full_s11load",
			  "full_s22open", "full_s22short", "full_s22load",
	                  "full_s11thru", "full_s12thru", "full_s21thru", "full_s22thru",
			  "trl_s11in", "trl_s12in", "trl_s21in", "trl_s22in",
			  "trl_s11thru", "trl_s12thru", "trl_s21thru", "trl_s22thru", "trl_a1thru", "trl_a2thru",
			  "trl_s11refl", "trl_s22refl",
			  "trl_s11line", "trl_s12line", "trl_s21line", "trl_s22line", "trl_a1line", "trl_a2line",
	                  "full_s11out", "full_s12out", "full_s21out", "full_s22out",
			  "trl_s11out", "trl_s12out", "trl_s21out", "trl_s22out",
	                  NULL};

	i = 0;
	while (names[i])
	{
		entryname = g_strconcat ("cal_", names[i], "_entry", NULL);
		cal_set_text (entryname, calwin->full_filenames[i]);
		g_free (entryname);
		i++;
	}
}

/* Opens the calibration GUI dialog. */
void cal_open_win ()
{
	CalWin *calwin;
	GtkWidget *dialog;
	gint i, result;

	/* Prepare the structure */
	if (glob->calwin)
		calwin = glob->calwin;
	else
	{
		calwin = g_new (CalWin, 1);
		glob->calwin = calwin;

		calwin->in_file = NULL;
		calwin->out_file = NULL;
		calwin->open_file = NULL;
		calwin->short_file = NULL;
		calwin->load_file = NULL;
		calwin->thru_file = NULL;
		calwin->isol_file = NULL;
		calwin->calib_type = 0;
		calwin->offline = TRUE;
		calwin->proxyhost = NULL;

		for (i=0; i<40; i++)
			calwin->full_filenames[i] = NULL;
	}

	calwin->xmlcal = glade_xml_new (GLADEFILE, "cal_dialog", NULL);

	/* Set remembered or default filenames */
	if ((glob->data) && (!calwin->in_file))
	{
		if (glob->IsReflection)
			cal_set_text ("cal_in_entry", glob->data->file);
		else
			cal_set_text ("cal_trans_in_entry", glob->data->file);
		calwin->calib_type = glob->IsReflection ? 0 : 1;
	}

	gtk_notebook_set_current_page (
		GTK_NOTEBOOK (glade_xml_get_widget (calwin->xmlcal, "calibrate_notebook")),
		calwin->calib_type);

	switch (calwin->calib_type)
	{
		case 0:	/* simple reflection */
			cal_set_text ("cal_in_entry", calwin->in_file);
			cal_set_text ("cal_out_entry", calwin->out_file);
			break;
		case 1:	/* simple transmission */
			cal_set_text ("cal_trans_in_entry", calwin->in_file);
			cal_set_text ("cal_trans_out_entry", calwin->out_file);
			break;
		case 2:	/* full 2-port */
			break;
		case 3:	/* TRL */
			break;
	}

	cal_set_text ("cal_open_entry", calwin->open_file);
	cal_set_text ("cal_short_entry", calwin->short_file);
	cal_set_text ("cal_load_entry", calwin->load_file);
	cal_set_text ("cal_thru_entry", calwin->thru_file);
	cal_set_text ("cal_isol_entry", calwin->isol_file);
	cal_set_full_texts (calwin);

	if (!calwin->offline)
	{
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (calwin->xmlcal, "cal_offline_radio")),
			FALSE);
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (calwin->xmlcal, "cal_online_radio")),
			TRUE);
		gtk_widget_set_sensitive (
			glade_xml_get_widget (calwin->xmlcal, "cal_proxy_hbox" ),
			TRUE);
	}
	cal_set_text ("cal_proxy_entry", calwin->proxyhost);

	/* Enable SelectFilname buttons */
	cal_connect_select_but (calwin);

	dialog = glade_xml_get_widget (calwin->xmlcal, "cal_dialog");
	glade_xml_signal_autoconnect (calwin->xmlcal);

	while (1)
	{
		result = gtk_dialog_run (GTK_DIALOG (dialog));

		if (result != GTK_RESPONSE_OK)
			/* Calibration canceled */
			break;

		if (!cal_check_entries ())
			/* Some entries are not valid */
			continue;

		if (cal_do_calibration ())
			break;
	}

	gtk_widget_destroy (dialog);
	calwin->xmlcal = NULL;
}
