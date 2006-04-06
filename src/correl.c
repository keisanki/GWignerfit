#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glade/glade.h>
#include <stdio.h>
#include <math.h>

#include "gtkspectvis.h"
#include "structs.h"
#include "helpers.h"
#include "export.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

/* Calculate the autocorrelcation function */
static void correl_cal_correl ()
{
	CorrelWin *correl;
	ComplexDouble val1, val2;
	guint eps, epsmax;
	gdouble recorr, imcorr, sqsum=1.0, amp;
	gdouble gamma=0.0, deltaf;
	gdouble x1, x2, y1, y2;
	guint i=0, start, num, granularity;
	GtkWidget *label;
	gchar text[20];
	GtkSpectVis *graph;
	GdkColor color;

	g_return_if_fail (glob->correl);
	correl = glob->correl;

	granularity = gtk_spin_button_get_value_as_int (
			GTK_SPIN_BUTTON (
				glade_xml_get_widget (glob->correl->xmlcorrel, "corr_spinner")
			));

	/* Find start position and number of points. */
	while ((i < glob->data->len) && 
	       (glob->data->x[i] < correl->min))
		i += granularity;
	start = i;
	if (start == glob->data->len)
	{
		dialog_message ("Cannot calculate correlation function as there "
				"are no datapoints in the given frequency range.");
		return;
	}

	num = 0;
	while ((start+num*granularity < glob->data->len) && 
	       (glob->data->x[start+num*granularity] <= correl->max))
		num++;

	/* Disable the frequency entries */
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->correl->xmlcorrel, "correl_start_entry"),
		FALSE);
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->correl->xmlcorrel, "correl_stop_entry"),
		FALSE);

	g_return_if_fail (num > 2);

	deltaf = glob->data->x[granularity] - glob->data->x[0];
	epsmax = num/2;
	if (epsmax*deltaf > 500e6)
		epsmax = 500e6/deltaf;

	/* Prepare new DataVector. */
	if (correl->data)
		free_datavector (correl->data);
	correl->data = new_datavector (epsmax);
	
	/* Calculate correlation function */
	for (eps=0; eps<epsmax; eps++)
	{
		recorr = 0;
		imcorr = 0;
		for (i=start; i+eps*granularity < start+num*granularity; i+=granularity)
		{
			val1 = glob->data->y[i];
			val2 = glob->data->y[i+eps*granularity];
			recorr += val1.re*val2.re + val1.im*val2.im;
			imcorr += val1.im*val2.re - val1.re*val2.im;
		}

		if (eps == 0)
			sqsum = (recorr + imcorr)/(gdouble)num;
		
		recorr /= (gdouble)(num-eps)*sqsum;
		imcorr /= (gdouble)(num-eps)*sqsum;
		amp = recorr*recorr + imcorr*imcorr;

		/* Interpolate correlation length */
		if ((amp < 0.5) && (gamma == 0.0))
		{
			x1 = (eps-1) * deltaf;
			x2 =  eps    * deltaf;
			y1 = correl->data->y[eps-1].abs;
			y2 = amp;
			
			gamma = (x1*(.5-y2)+x2*(y1-.5))/(y1-y2);
		}

		correl->data->x[eps]      = eps*deltaf;
		correl->data->y[eps].re   = recorr;
		correl->data->y[eps].im   = imcorr;
		correl->data->y[eps].abs  = amp;

		/* Update progress bar */
		if (!((eps*1000/epsmax) % 10))
		{
			snprintf (text, 20, "%d %%", (gint) ((gdouble)eps / (gdouble)epsmax * 100.0));
			gtk_progress_bar_set_fraction (
				GTK_PROGRESS_BAR (glade_xml_get_widget (correl->xmlcorrel, "corr_progress")),
				(gdouble)eps / (gdouble)epsmax
				);
			gtk_progress_bar_set_text (
				GTK_PROGRESS_BAR (glade_xml_get_widget (correl->xmlcorrel, "corr_progress")),
				text
				);
			while (gtk_events_pending ()) gtk_main_iteration ();

			/* Someone may have closed the window now */
			if (!glob->correl)
				return;
		}
	}

	gtk_progress_bar_set_fraction (
		GTK_PROGRESS_BAR (glade_xml_get_widget (correl->xmlcorrel, "corr_progress")), 1.0);
	gtk_progress_bar_set_text (
		GTK_PROGRESS_BAR (glade_xml_get_widget (correl->xmlcorrel, "corr_progress")), "100 %");

	/* Enable the frequency entries */
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->correl->xmlcorrel, "correl_start_entry"),
		TRUE);
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->correl->xmlcorrel, "correl_stop_entry"),
		TRUE);

	/* Display new correlation length */
	label = glade_xml_get_widget (correl->xmlcorrel, "correl_length");
	snprintf (text, 20, "%.3f MHz", gamma/1e6);
	gtk_label_set_text (GTK_LABEL (label), text);

	/* Display graph (which has uid 1) */
	graph = GTK_SPECTVIS (glade_xml_get_widget (correl->xmlcorrel, "correl_graph"));
	color.red   = 65535;
	color.green = 0;
	color.blue  = 0;
	gtk_spect_vis_data_remove (graph, 1);
	gtk_spect_vis_data_add (
			graph,
			correl->data->x,
			correl->data->y,
			correl->data->len,
			color, 
			'l');
	gtk_spect_vis_set_axisscale (graph, 1e6, 0);
	gtk_spect_vis_zoom_x_to (graph, 0, 10e6);
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);
}

/* Calculate and display some general spectrum statistics */
static void correl_cal_statistics ()
{
	CorrelWin *correl;
	gdouble re=0.0, im=0.0, phi=0.0, abs=0.0, abssq=0.0;
	ComplexDouble val;
	guint i=0, num=0;
	GtkWidget *label;
	gchar text[20];

	correl = glob->correl;
	if (!correl)
		return;

	while (i < glob->data->len)
	{
		val = glob->data->y[i];
		i++;
		
		if ((glob->data->x[i-1] < correl->min) || 
		    (glob->data->x[i-1] > correl->max))
			continue;

		re    += val.re;
		im    += val.im;
		phi   += atan2 (val.im, val.re);
		abs   += val.abs;
		abssq += val.abs * val.abs;

		num++;
	}

	if (!num)
		return;

	re    /= num;
	im    /= num;
	phi   /= num;
	abs   /= num;
	abssq /= num;

	label = glade_xml_get_widget (correl->xmlcorrel, "correl_abs");
	snprintf (text, 20, "% .3e", abs);
	gtk_label_set_text (GTK_LABEL (label), text);

	label = glade_xml_get_widget (correl->xmlcorrel, "correl_re");
	snprintf (text, 20, "% .3e", re);
	gtk_label_set_text (GTK_LABEL (label), text);

	label = glade_xml_get_widget (correl->xmlcorrel, "correl_im");
	snprintf (text, 20, "% .3e", im);
	gtk_label_set_text (GTK_LABEL (label), text);

	label = glade_xml_get_widget (correl->xmlcorrel, "correl_arg");
	snprintf (text, 20, "% .3e", phi);
	gtk_label_set_text (GTK_LABEL (label), text);

	label = glade_xml_get_widget (correl->xmlcorrel, "correl_abssq");
	snprintf (text, 20, "% .3e", abssq);
	gtk_label_set_text (GTK_LABEL (label), text);

	label = glade_xml_get_widget (correl->xmlcorrel, "correl_reabs");
	snprintf (text, 20, "% .3f %%", re/abs*100.0);
	gtk_label_set_text (GTK_LABEL (label), text);

	label = glade_xml_get_widget (correl->xmlcorrel, "correl_imabs");
	snprintf (text, 20, "% .3f %%", im/abs*100.0);
	gtk_label_set_text (GTK_LABEL (label), text);
}

/* Displays the current frequency range */
static void correl_update_frq ()
{
	GtkWidget *entry;
	gchar text[20];

	snprintf (text, 20, "%f", glob->correl->min / 1e9);
	entry = glade_xml_get_widget (glob->correl->xmlcorrel, "correl_start_entry");
	gtk_entry_set_text (GTK_ENTRY (entry), text);
	snprintf (text, 20, "%f", glob->correl->max / 1e9);
	entry = glade_xml_get_widget (glob->correl->xmlcorrel, "correl_stop_entry");
	gtk_entry_set_text (GTK_ENTRY (entry), text);
}

/* Open the window for the correlation function calculation */
void correl_open_win ()
{
	CorrelWin *correl;

	g_return_if_fail (glob->data || glob->data->len);

	if (glob->correl)
		/* Do nothing if window is already open. */
		return;

	glob->correl = g_new0 (CorrelWin, 1);
	correl = glob->correl;

	correl->xmlcorrel = glade_xml_new (GLADEFILE, "correl_win", NULL);
	glade_xml_signal_autoconnect (correl->xmlcorrel);

	while (gtk_events_pending ()) gtk_main_iteration ();

	correl->min = glob->gparam->min;
	correl->max = glob->gparam->max;

	/* Estimate a reasonable granularity. */
	gtk_spin_button_set_value (
			GTK_SPIN_BUTTON (glade_xml_get_widget (glob->correl->xmlcorrel, "corr_spinner")),
			(correl->max-correl->min)/(glob->data->x[1]-glob->data->x[0])/30000
			);
	
	correl_update_frq ();
	correl_cal_correl ();

	correl_cal_statistics ();
}

/* Close window and tidy up */
void on_correl_close_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if ((!glob->correl) || (!glob->correl->xmlcorrel))
		return;
	
	gtk_widget_destroy (
			glade_xml_get_widget (glob->correl->xmlcorrel, "correl_win")
			);

	//g_free (glob->correl->xmlcorrel);
	glob->correl->xmlcorrel = NULL;

	free_datavector (glob->correl->data);
	glob->correl->data = NULL;

	g_free (glob->correl);
	glob->correl = NULL;
}

/* Handle zooming events */
void correl_handle_viewport_changed (GtkSpectVis *spectvis, gchar *zoomtype)
{
	gtk_spect_vis_redraw (spectvis);
}

/* Mark a point */
gint correl_handle_signal_marked (GtkSpectVis *spectvis, gdouble *xval, gdouble *yval)
{
	gtk_spect_vis_mark_point (spectvis, *xval, *yval);
	return 0;
}

/* Update start frequency */
gboolean correl_minfrq_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter))
		return FALSE;
	
	if (sscanf (gtk_entry_get_text (GTK_ENTRY (entry)), "%lf", &value) != 1)
	{
		dialog_message ("Error: Could not parse your frequency input.");
		correl_update_frq ();
		return TRUE;
	}

	if (value*1e9 >= glob->correl->max)
	{
		dialog_message ("Error: The start frequency must be below the stop frequency.");
		correl_update_frq ();
		return TRUE;
	}

	glob->correl->min = value * 1e9;
	correl_update_frq ();
	correl_cal_statistics ();
	correl_cal_correl ();
	return FALSE;
}

/* Update stop frequency */
gboolean correl_maxfrq_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter))
		return FALSE;
	
	if (sscanf (gtk_entry_get_text (GTK_ENTRY (entry)), "%lf", &value) != 1)
	{
		dialog_message ("Error: Could not parse your frequency input.");
		correl_update_frq ();
		return TRUE;
	}

	if (value*1e9 <= glob->correl->min)
	{
		dialog_message ("Error: The stop frequency must be above the start frequency.");
		correl_update_frq ();
		return TRUE;
	}

	glob->correl->max = value * 1e9;
	correl_update_frq ();
	correl_cal_statistics ();
	correl_cal_correl ();
	return FALSE;
}

/* Switch between normal and logarithmic scale */
gboolean on_corr_log_scale_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *graph;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->correl->xmlcorrel, "correl_graph"));
	g_return_val_if_fail (graph, FALSE);

	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem)))
	{
		gtk_spect_vis_set_displaytype (graph, 'l');
		gtk_spect_vis_set_axisscale (graph, 0, 2);
	}
	else
	{
		gtk_spect_vis_set_displaytype (graph, 'a');
		gtk_spect_vis_set_axisscale (graph, 0, 1);
	}

	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

/* Write all statistics into a file */
gboolean on_corr_statistical_data_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	CorrelWin *correl;
	GtkLabel *label;
	gchar *filename, *defaultname, *date;
	guint i;
	FILE *fh;

	g_return_val_if_fail (glob->correl, FALSE);
	correl = glob->correl;

	defaultname = get_defaultname (NULL);
	filename = get_filename ("Select filename for data", defaultname, 2);
	g_free (defaultname);

	if (!filename)
		return TRUE;

	fh = fopen (filename, "w");
	if (!fh)
	{
		dialog_message ("Error: Could not create file '%s'.", filename);
		g_free (filename);
		return FALSE;
	}

	date = get_timestamp ();
	i = gtk_spin_button_get_value_as_int (
			GTK_SPIN_BUTTON (
				glade_xml_get_widget (correl->xmlcorrel, "corr_spinner")
			));
	fprintf (fh, "# Correlation function calculated with GWignerFit\r\n#\r\n");
	fprintf (fh, "# Date              : %s\r\n", date);
	fprintf (fh, "# Spectrum data file: %s\r\n", glob->data->file);
	fprintf (fh, "# Frequency range   : %f - %f GHz\r\n", 
			correl->min/1e9, correl->max/1e9);
	fprintf (fh, "# Granularity       : %d\r\n#\r\n", i);
	g_free (date);

	fprintf (fh, "# General spectrum statistics:\r\n");
	label = GTK_LABEL (glade_xml_get_widget (correl->xmlcorrel, "correl_abs"));
	fprintf (fh, "# <Abs(S)>         = %s\r\n", gtk_label_get_text (label));
	label = GTK_LABEL (glade_xml_get_widget (correl->xmlcorrel, "correl_arg"));
	fprintf (fh, "# <Arg(S)>         = %s\r\n", gtk_label_get_text (label));
	label = GTK_LABEL (glade_xml_get_widget (correl->xmlcorrel, "correl_re"));
	fprintf (fh, "# <Re(S)>          = %s\r\n", gtk_label_get_text (label));
	label = GTK_LABEL (glade_xml_get_widget (correl->xmlcorrel, "correl_im"));
	fprintf (fh, "# <Im(S)>          = %s\r\n", gtk_label_get_text (label));
	label = GTK_LABEL (glade_xml_get_widget (correl->xmlcorrel, "correl_abssq"));
	fprintf (fh, "# <Abs(S)^2>       = %s\r\n", gtk_label_get_text (label));
	label = GTK_LABEL (glade_xml_get_widget (correl->xmlcorrel, "correl_reabs"));
	fprintf (fh, "# <Re(S)>/<Abs(S)> = %s\r\n", gtk_label_get_text (label));
	label = GTK_LABEL (glade_xml_get_widget (correl->xmlcorrel, "correl_imabs"));
	fprintf (fh, "# <Im(S)>/<Abs(S)> = %s\r\n", gtk_label_get_text (label));

	label = GTK_LABEL (glade_xml_get_widget (correl->xmlcorrel, "correl_length"));
	fprintf (fh, "#\r\n# Correlation length: %s\r\n#\r\n",
			gtk_label_get_text (label));

	fprintf (fh, "# eps [Hz]\t Re(C)\t\t Im(C)\r\n");
	for (i=0; i<correl->data->len; i++)
		fprintf (fh, DATAFRMT, correl->data->x[i], 
				correl->data->y[i].re, correl->data->y[i].im);

	fclose (fh);
	g_free (filename);
	return TRUE;
}

/* Create a postscript from the graph */
gboolean on_corr_graph_as_postscript_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *graph;
	GArray *uids, *legend, *lt;
	gchar *default_footer, *basename;
	gchar *selected_filename, *selected_title, *selected_footer;
	gchar selected_legend;
	gint value;

	basename = g_path_get_basename (glob->data->file);
	default_footer = g_strdup_printf ("file: %s", basename);
	g_free (basename);
	
	if (!postscript_export_dialog (
		NULL,
		NULL,
		default_footer,
		FALSE,
		FALSE,
		&selected_filename,
		&selected_title,
		&selected_footer,
		NULL,
		NULL,
		&selected_legend
	   ))
	{
		g_free (default_footer);
		return FALSE;
	}
	g_free (default_footer);

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->correl->xmlcorrel, "correl_graph"));
	uids   = g_array_new (FALSE, FALSE, sizeof (guint) );
	legend = g_array_new (FALSE, FALSE, sizeof (gchar*));
	lt     = g_array_new (FALSE, FALSE, sizeof (gint)  );
	
	value = 1;
	g_array_append_val (uids, value);
	g_array_append_val (lt, value);

	basename = "Correlation function";
	g_array_append_val (legend, basename);

	if (!gtk_spect_vis_export_ps (graph, uids, selected_filename, selected_title, 
				 "frequency (MHz)", NULL, selected_footer, 
				 legend, selected_legend, lt))
	{
		dialog_message ("Error: Could not create graph. Is gnuplot installed on your system?");
		if (selected_filename[0] != '|')
			unlink (selected_filename);
	}

	g_array_free (uids, TRUE);
	g_array_free (legend, TRUE);
	g_array_free (lt, TRUE);
	g_free (selected_filename);
	g_free (selected_title);
	g_free (selected_footer);

	return TRUE;
}
