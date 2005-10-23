#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "structs.h"
#include "helpers.h"
#include "processdata.h"
#include "overlay.h"

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

/* Callback for Select buttons */
static gboolean cal_choose_file (GtkButton *button, gpointer user_data)
{
	gchar *filename, *defaultname, check;

	check = 1;
	if (button == GTK_BUTTON (glade_xml_get_widget (glob->calwin->xmlcal, "cal_out_but")))
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

	return TRUE;
}

/* Callback when changeing the calibration type */
void cal_data_type_toggled (GtkToggleButton *togglebutton, gpointer user_data) 
{
	GladeXML *xmlcal = glob->calwin->xmlcal;
	gboolean state;

	/* state == TRUE for reflection */
	state = gtk_toggle_button_get_active (togglebutton);
	
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_open_label" ),  state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_short_label"),  state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_load_label" ),  state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_thru_label" ), !state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_isol_label" ), !state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_open_entry" ),  state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_short_entry"),  state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_load_entry" ),  state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_thru_entry" ), !state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_isol_entry" ), !state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_open_but"   ),  state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_short_but"  ),  state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_load_but"   ),  state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_thru_but"   ), !state);
	gtk_widget_set_sensitive (glade_xml_get_widget (xmlcal, "cal_isol_but"   ), !state);

	glob->calwin->data_is_refl = state;
}

/* Takes the filenames from the TextEntries and stores them in the
 * CalWin struct with proper error checking */
static gboolean cal_check_entries ()
{
	CalWin *calwin = glob->calwin;
	gchar *file;
	
	if ((file = cal_get_valid_file ("cal_in_entry")))
		calwin->in_file = file;
	else
	{
		dialog_message ("Could not open file with input data.");
		return FALSE;
	}
		
	file = g_strdup (gtk_entry_get_text (GTK_ENTRY 
			(glade_xml_get_widget (glob->calwin->xmlcal, "cal_out_entry"))));
	if (!file_is_writeable (file))
		return FALSE;
	calwin->out_file = file;

	if (calwin->data_is_refl)
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
static void cal_update_progress (gfloat fraction)
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

/* Adds two complex numbers (a+b) */
ComplexDouble c_add (ComplexDouble a, ComplexDouble b)
{
	ComplexDouble c;

	c.re = a.re + b.re;
	c.im = a.im + b.im;
	c.abs = 0.0;

	return c;
}

/* Substracts two complex numbers (a-b) */
ComplexDouble c_sub (ComplexDouble a, ComplexDouble b)
{
	ComplexDouble c;

	c.re = a.re - b.re;
	c.im = a.im - b.im;
	c.abs = 0.0;

	return c;
}

/* Multiplies two complex numbers (a*b)*/
ComplexDouble c_mul (ComplexDouble a, ComplexDouble b)
{
	ComplexDouble c;

	c.re = a.re*b.re - a.im*b.im;
	c.im = a.re*b.im + b.re*a.im;
	c.abs = 0.0;

	return c;
}

/* Divides two complex numbers (a/b) */
ComplexDouble c_div (ComplexDouble a, ComplexDouble b)
{
	ComplexDouble c, num;
	gdouble denom;

	denom = b.re*b.re + b.im*b.im;

	b.im *= -1;
	num = c_mul (a, b);
	
	c.re = num.re / denom;
	c.im = num.im / denom;
	c.abs = 0.0;

	return c;
}

/* Calculates exp(i*a) for a real a */
ComplexDouble c_exp (gdouble a)
{
	ComplexDouble c;

	c.re = cos (a);
	c.im = sin (a);
	c.abs = 0.0;

	return c;
}

/* Calibrate a reflection spectrum with the given data */
static DataVector* cal_reflection (DataVector *in, DataVector *opn, DataVector *shrt, DataVector *load)
{
	DataVector *out;
	ComplexDouble edf, esf, erf;
	gdouble alphaO, gammaO, alphaS, C;
	guint i;

	out = new_datavector (in->len);
	g_free (out->x);
	out->x = in->x;

	for (i=0; i<in->len; i++)
	{
		alphaO = 2*M_PI*in->x[i] * glob->prefs->cal_tauO;
		alphaS = 2*M_PI*in->x[i] * glob->prefs->cal_tauS;
		C =   glob->prefs->cal_C0
		    + glob->prefs->cal_C1 + 165.78e-27 * in->x[i] 
		    + glob->prefs->cal_C2 + -3.54e-36 * in->x[i]*in->x[i]
		    + glob->prefs->cal_C3 + 0.07e-45 * in->x[i]*in->x[i]*in->x[i];
		gammaO = 2*atan (2*M_PI*in->x[i]*C*50);
		
		edf = load->y[i];

		esf = c_mul (c_sub (opn->y[i], load->y[i]), 
		             c_exp (alphaO+gammaO));
		esf = c_add (esf, 
		             c_mul (c_sub (shrt->y[i], load->y[i]), 
		                    c_exp (alphaS)));
		esf.re *= -1.0;
		esf.im *= -1.0;
		esf = c_div (esf, c_sub (shrt->y[i], opn->y[i]));
		
		erf = c_mul (c_sub (opn->y[i], load->y[i]),
		             c_sub (shrt->y[i], load->y[i]));
		erf = c_mul (erf,
		             c_add (c_exp (alphaO+gammaO), c_exp (alphaS)));
		erf = c_div (erf,
		             c_sub (shrt->y[i], opn->y[i]));

		out->y[i] = c_div (c_sub (in->y[i], edf),
		                   c_add (erf,
		                          c_mul (esf,
		                                 c_sub (in->y[i], edf)
						)
					 )
				  );

		out->y[i].abs = sqrt (out->y[i].re*out->y[i].re + out->y[i].im*out->y[i].im);

		if (i % (in->len / 100) == 0)
			cal_update_progress ((gfloat)i / (gfloat)in->len);
	}

	return out;
}

/* Calibrate a transmission spectrum with the given data */
static DataVector* cal_transmission (DataVector *in, DataVector *thru, DataVector *isol)
{
	DataVector *out;
	ComplexDouble numerator, denominator;
	guint i;

	out = new_datavector (in->len);
	g_free (out->x);
	out->x = in->x;

	for (i=0; i<in->len; i++)
	{
		if (isol)
		{
			numerator   = c_sub (in->y[i], isol->y[i]);
			denominator = c_sub (thru->y[i], isol->y[i]);
		}
		else
		{
			numerator   = in->y[i];
			denominator = thru->y[i];
		}

		out->y[i] = c_div (numerator, denominator);
		out->y[i].abs = sqrt (out->y[i].re*out->y[i].re + out->y[i].im*out->y[i].im);

		if (i % (in->len / 100) == 0)
			cal_update_progress ((gfloat)i / (gfloat)in->len);
	}

	return out;
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

	in = out = a = b = c = NULL;

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

	/* Read main datafile */
	if (!(in = import_datafile (calwin->in_file, TRUE)))
		return FALSE;

	if (calwin->data_is_refl)
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
			dialog_message ("Error: Datasets do not have the same lengths.");
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
			dialog_message ("Error: Datasets do not have the same lengths.");
			return FALSE;
		}

		gtk_widget_set_sensitive (
			glade_xml_get_widget (calwin->xmlcal, "cal_progress_frame" ), 
			TRUE);
		out = cal_reflection (in, a, b, c);
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
			dialog_message ("Error: Datasets do not have the same lengths.");
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
			dialog_message ("Error: Datasets do not have the same frequencies.");
			return FALSE;
		}

		out = cal_transmission (in, a, b);
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
		return FALSE;
	}
	text = g_strdup_printf ("Writing output file ...");
	gtk_progress_bar_set_text (
		GTK_PROGRESS_BAR (glade_xml_get_widget (calwin->xmlcal, "cal_progress")),
		text);
	g_free (text);
	while (gtk_events_pending ()) gtk_main_iteration ();

	if (calwin->data_is_refl)
	{
		fprintf (fh, "# Reflection spectrum calibrated by GWignerFit\r\n#\r\n");
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
		fprintf (fh, "# Uncalibrated input file : %s\r\n", in->file);
		fprintf (fh, "# Thru data file          : %s\r\n", a->file);
		fprintf (fh, "# Isolation data file     : %s\r\n", b ? b->file : "(omitted)");
	}
	fprintf (fh, DATAHDR);
	for (i=0; i<out->len; i++)
		fprintf (fh, DATAFRMT, out->x[i], out->y[i].re, out->y[i].im);
	fclose (fh);
	cal_update_progress (-1.0);

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

/* Opens the calibration GUI dialog. */
void cal_open_win ()
{
	CalWin *calwin;
	GtkWidget *dialog;
	gint result;

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
		calwin->data_is_refl = TRUE;
	}

	calwin->xmlcal = glade_xml_new (GLADEFILE, "cal_dialog", NULL);

	/* Set remembered or default filenames */
	if ((glob->data) && (!calwin->in_file))
	{
		cal_set_text ("cal_in_entry", glob->data->file);
		calwin->data_is_refl = glob->IsReflection;
	}

	cal_set_text ("cal_in_entry", calwin->in_file);
	cal_set_text ("cal_out_entry", calwin->out_file);
	cal_set_text ("cal_open_entry", calwin->open_file);
	cal_set_text ("cal_short_entry", calwin->short_file);
	cal_set_text ("cal_load_entry", calwin->load_file);
	cal_set_text ("cal_thru_entry", calwin->thru_file);
	cal_set_text ("cal_isol_entry", calwin->isol_file);

	if (!calwin->data_is_refl)
	{
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (calwin->xmlcal, "cal_refl_radio")),
			FALSE);
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (
				glade_xml_get_widget (calwin->xmlcal, "cal_trans_radio")),
			TRUE);
	}
	cal_data_type_toggled (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (calwin->xmlcal, "cal_refl_radio")),
		NULL);

	/* Enable SelectFilname buttons */
	g_signal_connect (
		G_OBJECT (glade_xml_get_widget (calwin->xmlcal, "cal_in_but")), 
		"clicked",
		(GCallback) cal_choose_file, 
		glade_xml_get_widget (calwin->xmlcal, "cal_in_entry"));
	g_signal_connect (
		G_OBJECT (glade_xml_get_widget (calwin->xmlcal, "cal_out_but")), 
		"clicked",
		(GCallback) cal_choose_file, 
		glade_xml_get_widget (calwin->xmlcal, "cal_out_entry"));
	g_signal_connect (
		G_OBJECT (glade_xml_get_widget (calwin->xmlcal, "cal_open_but")), 
		"clicked",
		(GCallback) cal_choose_file, 
		glade_xml_get_widget (calwin->xmlcal, "cal_open_entry"));
	g_signal_connect (
		G_OBJECT (glade_xml_get_widget (calwin->xmlcal, "cal_short_but")), 
		"clicked",
		(GCallback) cal_choose_file, 
		glade_xml_get_widget (calwin->xmlcal, "cal_short_entry"));
	g_signal_connect (
		G_OBJECT (glade_xml_get_widget (calwin->xmlcal, "cal_load_but")), 
		"clicked",
		(GCallback) cal_choose_file, 
		glade_xml_get_widget (calwin->xmlcal, "cal_load_entry"));
	g_signal_connect (
		G_OBJECT (glade_xml_get_widget (calwin->xmlcal, "cal_thru_but")), 
		"clicked",
		(GCallback) cal_choose_file, 
		glade_xml_get_widget (calwin->xmlcal, "cal_thru_entry"));
	g_signal_connect (
		G_OBJECT (glade_xml_get_widget (calwin->xmlcal, "cal_isol_but")), 
		"clicked",
		(GCallback) cal_choose_file, 
		glade_xml_get_widget (calwin->xmlcal, "cal_isol_entry"));

	dialog = glade_xml_get_widget (calwin->xmlcal, "cal_dialog");
	glade_xml_signal_autoconnect (calwin->xmlcal);

	while (1)
	{
		result = gtk_dialog_run (GTK_DIALOG (dialog));

		if (result != GTK_RESPONSE_OK)
			break;

		if (!cal_check_entries ())
			continue;

		if (cal_do_calibration ())
			break;
	}

	gtk_widget_destroy (dialog);
	calwin->xmlcal = NULL;
}
