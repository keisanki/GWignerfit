#include <gtk/gtk.h>
#include <glade/glade.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

#include "gtkspectvis.h"
#include "overlay.h"
#include "structs.h"
#include "helpers.h"
#include "export.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

/*
 * FFT routine taken from Numerical Receipes
 */
#define SWAP(a,b) tempr=(a);(a)=(b);(b)=tempr
static void four1 (gdouble data[], unsigned long nn, int isign)
{
	unsigned long n,mmax,m,j,istep,i;
	gdouble wtemp,wr,wpr,wpi,wi,theta;
	gdouble tempr,tempi;

	n = nn << 1;
	j = 1;
	for (i=1; i<n; i+=2)
	{
		if (j > i)
		{
			SWAP(data[j],data[i]);
			SWAP(data[j+1],data[i+1]);
		}
		m = n >> 1;
		while (m >= 2 && j > m)
		{
			j -= m;
			m >>= 1;
		}
		j += m;
	}
	mmax = 2;
	while (n > mmax)
	{
		istep = mmax << 1;
		theta = isign*(6.28318530717959/mmax);
		wtemp = sin (0.5*theta);
		wpr = -2.0*wtemp*wtemp;
		wpi = sin (theta);
		wr = 1.0;
		wi = 0.0;
		for (m=1; m<mmax; m+=2)
		{
			for (i=m; i<=n; i+=istep)
			{
				j = i+mmax;
				tempr = wr*data[j]-wi*data[j+1];
				tempi = wr*data[j+1]+wi*data[j];
				data[j]    = data[i]-tempr;
				data[j+1]  = data[i+1]-tempi;
				data[i]   += tempr;
				data[i+1] += tempi;
			}
			wr = (wtemp=wr)*wpr-wi*wpi+wr;
			wi = wi*wpr+wtemp*wpi+wi;
		}
		mmax = istep;
	}
}
#undef SWAP

static gdouble fourier_window_data (guint n, guint numpoints)
{
	gdouble w = 1.0;

	if (!glob->fft) return 1.0;

	switch (glob->fft->windowing)
	{
		case 1:
			/* Rectangle */
			w = 1.0;
			break;
		case 2:
			/* Hamming */
			w = 0.54 - 0.46 * cos(2*M_PI*n/(numpoints-1));
			break;
		case 3:
			/* Blackman */
			w = 0.42 - 0.5 * cos(2*M_PI*n/(numpoints-1)) + 0.08* cos(4*M_PI*n/(numpoints-1));
			break;
	}

	return w;
}

DataVector* fourier_gen_dataset (DataVector *source, gdouble startfrq, gdouble endfrq)
{
	DataVector *fft;
	gdouble *data, re, im, c, s;
	gdouble w;
	guint i, n, sourceoff, sourceend;
	guint numpoints;

	/* numpoints: number of points in source to be transformed
	 * n: number of points to be in fft (odd number)
	 */

	sourceoff = 0;
	while ( (sourceoff < source->len) && (source->x[sourceoff] < startfrq) )
		sourceoff++;

	sourceend = 0;
	while ( (sourceend < source->len) && (source->x[sourceend] <= endfrq) )
		sourceend++;

	numpoints = sourceend - sourceoff;

	if (numpoints < 2) return NULL;

	n = pow(2, (gint) ceil (log (numpoints)/log (2))) + 1;
	fft = new_datavector (n);

	/* Set up the time axis */
	for (i=0; i<n; i++)
	{
		/* w = - n/2  ... + n/2 */
		w = (gdouble)i - (gdouble)(n-1) / 2.0;
		fft->x[i] = w / ((gdouble)(n-1) * (source->x[1] - source->x[0]));
	}

	/* The data */
	data = g_new (gdouble, 2*n-1);
	for (i=0; i<numpoints; i++)
	{
		data[2*i+1] = source->y[i+sourceoff].re;
		data[2*i+2] = source->y[i+sourceoff].im;

		/* Window the data */
		w = fourier_window_data (i, numpoints);
		data[2*i+1] *= w;
		data[2*i+2] *= w;
	}

	/* Pad with zeros */
	for (i=2*numpoints; i<2*n-1; i++)
		data[i] = 0;

	four1 (data, n-1, +1);

	startfrq *= -2*M_PI;
	
	/* Negative times */
	for (i=0; i<(n-1)/2; i++)
	{
		re = data[2*i+(n-1)+1];
		im = data[2*i+(n-1)+2];
		c = cos(startfrq*fft->x[i]);
		s = sin(startfrq*fft->x[i]);

		fft->y[i].re = re*c + im*s;
		fft->y[i].im = im*c - re*s;
		fft->y[i].abs = sqrt (fft->y[i].re*fft->y[i].re + fft->y[i].im*fft->y[i].im);
	}

	/* Positive times */
	for (i=(n-1)/2; i<n; i++)
	{
		re = data[2*(i-(n-1)/2)+1];
		im = data[2*(i-(n-1)/2)+2];
		c = cos(startfrq*fft->x[i]);
		s = sin(startfrq*fft->x[i]);

		fft->y[i].re = re*c + im*s;
		fft->y[i].im = im*c - re*s;
		fft->y[i].abs = sqrt (fft->y[i].re*fft->y[i].re + fft->y[i].im*fft->y[i].im);
	}
	g_free (data);

	return fft;
}

void fourier_update_main_graphs ()
{
	FFTWindow *fft = glob->fft;
	DataVector *fftvec, *oldvec;
	gdouble startfrq, stopfrq;
	GtkSpectVis *graph;
	GdkColor color;

	if ((!fft) || (!glob->data) || (!glob->theory)) return;

	graph = GTK_SPECTVIS (glade_xml_get_widget (fft->xmlfft, "fft_spectvis"));

	/* TODO:
	 * This gets called, when the frequency window changes, too. This functions
	 * ignores this however. The correct behavior would be to update the FFt 
	 * frequency window, too. */
	
	/* FFT of data graph */
	startfrq = fft->fmin >= 0    ? fft->fmin : glob->data->x[0];
	stopfrq  = fft->fmax <  1e49 ? fft->fmax : glob->data->x[glob->data->len-1];
	if (!gtk_spect_vis_get_data_by_uid (graph, glob->data->index))
	{
		/* Graph needs to be added */
		fftvec = fourier_gen_dataset (glob->data, startfrq, stopfrq);
		if ((fftvec) && (glob->data->index))
		{
			g_ptr_array_add (fft->data, fftvec);

			color.red = 65535;
			color.green = 0;
			color.blue = 0;
			fftvec->index = gtk_spect_vis_data_add (
						graph,
						fftvec->x,
						fftvec->y,
						fftvec->len,
						color, 'l');

			g_return_if_fail (gtk_spect_vis_request_id (graph, fftvec->index, glob->data->index));
		}

		/* Zoom from 0ns till 1000ns */
		if (fftvec)
			gtk_spect_vis_zoom_x_to (graph, 0, 1000e-9);
	}
	else
	{
		/* Graph needs to be updated */
		oldvec = (DataVector *) g_ptr_array_index (fft->data, 0);
		fftvec = fourier_gen_dataset (glob->data, startfrq, stopfrq);
		g_free (oldvec->x);
		g_free (oldvec->y);

		if (fftvec)
		{
			oldvec->x = fftvec->x;
			oldvec->y = fftvec->y;
			oldvec->len = fftvec->len;
			g_free (fftvec);
			gtk_spect_vis_data_update (graph, oldvec->index, oldvec->x, oldvec->y, oldvec->len);
		}
		else
		{
			oldvec->x = NULL;
			oldvec->y = NULL;
			gtk_spect_vis_data_remove (graph, glob->data->index);
			oldvec->index = 0;
		}
	}

	/* FFT of theory graph */
	startfrq = fft->fmin >= 0    ? fft->fmin : glob->theory->x[0];
	stopfrq  = fft->fmax <  1e49 ? fft->fmax : glob->theory->x[glob->data->len-1];
	if (!gtk_spect_vis_get_data_by_uid (graph, glob->theory->index))
	{
		fftvec = fourier_gen_dataset (glob->theory, startfrq, stopfrq);
		if ((fftvec) && (glob->theory->index))
		{
			g_ptr_array_add (fft->data, fftvec);

			color.red = 0;
			color.green = 0;
			color.blue = 65535;
			fftvec->index = gtk_spect_vis_data_add (
						graph,
						fftvec->x,
						fftvec->y,
						fftvec->len,
						color, 'l');

			g_return_if_fail (gtk_spect_vis_request_id (graph, fftvec->index, glob->theory->index));
		}

		/* Zoom from 0ns till 1000ns */
		if (fftvec)
			gtk_spect_vis_zoom_x_to (graph, 0, 1000e-9);
	}
	else
	{
		oldvec = (DataVector *) g_ptr_array_index (fft->data, 1);
		fftvec = fourier_gen_dataset (glob->theory, startfrq, stopfrq);
		g_free (oldvec->x);
		g_free (oldvec->y);

		if (fftvec)
		{
			oldvec->x = fftvec->x;
			oldvec->y = fftvec->y;
			oldvec->len = fftvec->len;
			g_free (fftvec);
			gtk_spect_vis_data_update (graph, oldvec->index, oldvec->x, oldvec->y, oldvec->len);
		}
		else
		{
			oldvec->x = NULL;
			oldvec->y = NULL;
			gtk_spect_vis_data_remove (graph, glob->theory->index);
			oldvec->index = 0;
		}
	}

	if (fftvec)
	{
		gtk_spect_vis_zoom_y_all (graph);
		gtk_spect_vis_redraw (graph);
	}
}

void fourier_update_overlay_graphs (gint uid, gboolean redraw)
{
	FFTWindow *fft = glob->fft;
	GtkSpectVis *spectrumgraph;
	GtkSpectVisData *spectdata;
	DataVector source[1], *transformed;
	GtkSpectVis *graph;
	GdkColor color;
	guint i;

	/* uid < 0: remove graph with spectrumgraph |uid|
	 * uid > 0: add graph with spectrumgraph uid
	 */

	if (fft == NULL) return;
	
	g_return_if_fail (uid != 0);

	graph = GTK_SPECTVIS (glade_xml_get_widget (fft->xmlfft, "fft_spectvis"));
	spectrumgraph = GTK_SPECTVIS (glade_xml_get_widget (gladexml, "graph"));
	
	if (uid > 0)
	{
		/* Graph needs to be added */

		spectdata = gtk_spect_vis_get_data_by_uid (spectrumgraph, uid);
		g_return_if_fail (spectdata);

		source->x = spectdata->X;
		source->y = spectdata->Y;
		source->len = spectdata->len;
		source->index = uid;

		/* FFT of data graph */
		transformed = fourier_gen_dataset (source, fft->fmin, fft->fmax);

		if (!transformed)
			return;
		
		overlay_get_color (&color, FALSE, source->index, NULL);

		transformed->index = gtk_spect_vis_data_add (
					graph,
					transformed->x,
					transformed->y,
					transformed->len,
					color, 'f');

		/* Sync uids with main graph */
		g_return_if_fail (gtk_spect_vis_request_id (graph, transformed->index, source->index));
		transformed->index = source->index;

		g_ptr_array_add (fft->data, transformed);
	}

	if (uid < 0)
	{
		/* Graph needs to be removed */
		uid *= -1;

		gtk_spect_vis_data_remove (graph, uid);

		i = 0;
		while ((i < fft->data->len) &&
		       (((DataVector *) g_ptr_array_index (fft->data, i))->index != uid))
			i++;
		if (i == fft->data->len)
			return;
		
		free_datavector ((DataVector *) g_ptr_array_index (fft->data, i));
		g_ptr_array_remove_index (fft->data, i);
	}

	if (redraw)
	{
		gtk_spect_vis_zoom_y_all (graph);
		gtk_spect_vis_redraw (graph);
	}
}

static void fourier_add_overlay_graphs ()
{
	GtkSpectVis *graph;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint id;

	if (glob->overlaystore == NULL) return;
	
	model = GTK_TREE_MODEL (glob->overlaystore);

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		gtk_tree_model_get (model, &iter, 0, &id, -1);
		fourier_update_overlay_graphs (id, FALSE);

		while (gtk_tree_model_iter_next (model, &iter))
		{
			gtk_tree_model_get (model, &iter, 0, &id, -1);
			fourier_update_overlay_graphs (id, FALSE);
		}
	}

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis"));
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);
}

void fourier_set_color (gint mainuid, GdkColor color)
{
	GtkSpectVis *graph;

	if (glob->fft == NULL) return;

	g_return_if_fail (mainuid > 0);
	
	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis"));
	gtk_spect_vis_set_data_color (graph, mainuid, color);
	gtk_spect_vis_redraw (graph);
}

void fourier_open_win (gboolean window)
{
	GtkSpectVis *graph;
	FFTWindow *fft;

	g_return_if_fail (glob->data);

	if (glob->fft == NULL)
	{
		glob->fft = fft = g_new0 (FFTWindow, 1);
		
		if (window)
		{
			/* Take only the fourier transform of the frequency window */
			fft->fmin = glob->gparam->min;
			fft->fmax = glob->gparam->max;
		}
		else
		{
			/* Take the fourier transform of everything */
			fft->fmin = -1e50;
			fft->fmax = +1e50;
		}

		fft->xmlfft = glade_xml_new (GLADEFILE, "fft_window", NULL);
		glade_xml_signal_autoconnect (fft->xmlfft);

		graph = GTK_SPECTVIS (glade_xml_get_widget (fft->xmlfft, "fft_spectvis"));

		/* x-axis is in nanoseconds */
		gtk_spect_vis_set_axisscale (graph, 1e-9, 1);

		fft->data = g_ptr_array_new ();
		fft->windowing = 1;

		fourier_update_main_graphs ();
		fourier_add_overlay_graphs ();
	}
}

void on_fft_close_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	gint i;
	
	if (glob->fft == NULL) return;
	
	gtk_widget_destroy (
			glade_xml_get_widget (glob->fft->xmlfft, "fft_window")
			);

	if (glob->fft->data)
	{
		for (i=0; i<glob->fft->data->len; i++)
			free_datavector ((DataVector *) g_ptr_array_index (glob->fft->data, i));
		g_ptr_array_free (glob->fft->data, TRUE);
	}

	g_free (glob->fft->xmlfft);
	g_free (glob->fft);

	glob->fft = NULL;
}

void fourier_handle_viewport_changed (GtkSpectVis *spectvis, gchar *zoomtype)
{
	if (*zoomtype == 'a')
	{
		gtk_spect_vis_zoom_x_to (spectvis, 0, 1000e-9);
	}
		
	gtk_spect_vis_redraw (spectvis);
}

gboolean fourier_view_absolute_part (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *graph = GTK_SPECTVIS (
			glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis")
			);
	
	gtk_spect_vis_set_displaytype (graph, 'a');
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

gboolean fourier_view_real_part (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *graph = GTK_SPECTVIS (
			glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis")
			);
	
	gtk_spect_vis_set_displaytype (graph, 'r');
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

gboolean fourier_view_imaginary_part (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *graph = GTK_SPECTVIS (
			glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis")
			);
	
	gtk_spect_vis_set_displaytype (graph, 'i');
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

gboolean fourier_view_phase_part (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *graph = GTK_SPECTVIS (
			glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis")
			);
	
	gtk_spect_vis_set_displaytype (graph, 'p');
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

gboolean fourier_view_log_power (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *graph = GTK_SPECTVIS (
			glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis")
			);
	
	gtk_spect_vis_set_displaytype (graph, 'l');
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

gboolean fourier_abscissa_in_ns (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *graph = GTK_SPECTVIS (glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis"));

	gtk_spect_vis_set_axisscale (graph, 1e-9, 1);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

gboolean fourier_abscissa_in_meter (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *graph = GTK_SPECTVIS (glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis"));

	gtk_spect_vis_set_axisscale (graph, 1.0/C0, 1);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

gboolean on_fourier_window_change (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint id;

	/* Change windowing type */
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (
				glade_xml_get_widget (glob->fft->xmlfft, "rectangular_window")
				)))
		glob->fft->windowing = 1;

	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (
				glade_xml_get_widget (glob->fft->xmlfft, "hamming_window")
				)))
		glob->fft->windowing = 2;

	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (
				glade_xml_get_widget (glob->fft->xmlfft, "blackman_window")
				)))
		glob->fft->windowing = 3;

	if (glob->overlaystore)
	{
		/* Update overlay graphs without redraw */
		model = GTK_TREE_MODEL (glob->overlaystore);

		if (gtk_tree_model_get_iter_first (model, &iter))
		{
			gtk_tree_model_get (model, &iter, 0, &id, -1);
			fourier_update_overlay_graphs (-id, FALSE);
			fourier_update_overlay_graphs (id, FALSE);

			while (gtk_tree_model_iter_next (model, &iter))
			{
				gtk_tree_model_get (model, &iter, 0, &id, -1);
				fourier_update_overlay_graphs (-id, FALSE);
				fourier_update_overlay_graphs (id, FALSE);
			}
		}
	}

	/* Update data and theory graph */
	fourier_update_main_graphs ();
	
	return TRUE;
}

static void fourier_export_data (DataVector *dataset, gchar *expln)
{
	gchar *filename, *path=NULL, *name;
	gint i;
	FILE *datafile;

	g_return_if_fail (glob->data);

	if (glob->path)
		path = g_strdup_printf("%s%c", glob->path, G_DIR_SEPARATOR);

	filename = get_filename ("Select filename for export", path, 2);
	g_free (path);

	if (!filename)
		return;

	/* Write data start */
	datafile = fopen (filename, "w");

	name = g_path_get_basename (glob->data->file);
	fprintf (datafile, "# FFT of '%s' %s\n", name, expln);
	g_free (name);

	if (glob->fft->fmin >= 0.0)
		fprintf (datafile, "# Source frequency range: %.9f - %.9f GHz\n", 
			glob->fft->fmin/1e9, glob->fft->fmax/1e9);
	else
		fprintf (datafile, "# Source frequency range: %.9f - %.9f GHz\n", 
			glob->data->x[0]/1e9, glob->data->x[glob->data->len-1]/1e9);
	
/*	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (
			glade_xml_get_widget (glob->fft->xmlfft, "abscissa_ns")
			)))
*/	fprintf (datafile, "# t[ns]\t\t\tRe\t\tIm\n");
/*
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (
			glade_xml_get_widget (glob->fft->xmlfft, "abscissa_m")
			)))
	fprintf (datafile, "# t[m]\t\t\tRe\t\tIm\n");
*/		
	for (i=0; i<dataset->len; i++)
	{
		fprintf (datafile, "%12.6f\t%12.6f\t%12.6f\n",
//				glob->fft->times[i] / graph->xAxisScale, 
				dataset->x[i] / 1e-9, 
				dataset->y[i].re,
				dataset->y[i].im);
	}

	fclose (datafile);
	/* Write data end */

	g_free (filename);
	dialog_message ("Export successful.");
}

void on_fourier_export_meas_data (GtkMenuItem *menuitem, gpointer user_data)
{
	fourier_export_data (g_ptr_array_index (glob->fft->data, 0), "measured data");
}

void on_fourier_export_theo_data (GtkMenuItem *menuitem, gpointer user_data)
{
	fourier_export_data (g_ptr_array_index (glob->fft->data, 1), "theory graph");
}

void on_fft_measure_distance_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if (!glob->data) 
	{
		dialog_message ("Please import some spectrum data first.");
		return;
	}

	g_mutex_lock (glob->threads->flaglock);
	glob->flag |= FLAG_FRQ_MEAS;
	glob->flag &= ~FLAG_RES_FIND;
	glob->flag &= ~FLAG_RES_CLICK;
	glob->flag &= ~FLAG_FRQ_MIN;
	glob->flag &= ~FLAG_FRQ_MAX;
	glob->flag &= ~FLAG_FRQ_INT;
	glob->flag &= ~FLAG_MRK_NOISE;
	g_mutex_unlock (glob->threads->flaglock);
}

/* Create a postscript from the graph */
gboolean on_fourier_export_ps (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkSpectVis *spectvis;
	GArray *uids, *legend, *lt;
	gchar *default_footer=NULL, *basename;
	gchar *selected_filename, *selected_title, *selected_footer;
	gboolean selected_theory, selected_overlay;
	gchar selected_legend;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint overlayid;
	gint linetype;
	gchar *str;

	spectvis = GTK_SPECTVIS (glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis"));
	g_return_val_if_fail (spectvis, FALSE);

	/* Put some default text into the GtkEntry fields */
	if (glob->section)
	{
		basename = g_path_get_basename (glob->resonancefile);
		default_footer = g_strdup_printf ("file: %s (%s)", basename, glob->section);
		g_free (basename);
	}
	else if ((glob->data) && (glob->data->file))
	{
		basename = g_path_get_basename (glob->data->file);
		default_footer = g_strdup_printf ("file: %s", basename);
		g_free (basename);
	}
	
	if (!postscript_export_dialog (
		NULL,
		NULL,
		default_footer,
		TRUE,
		glob->overlaystore ? TRUE : FALSE,
		&selected_filename,
		&selected_title,
		&selected_footer,
		&selected_theory,
		&selected_overlay,
		&selected_legend
	   ))
	{
		g_free (default_footer);
		return FALSE;
	}
	g_free (default_footer);

	uids   = g_array_new (FALSE, FALSE, sizeof (guint) );
	legend = g_array_new (FALSE, FALSE, sizeof (gchar*));
	lt     = g_array_new (FALSE, FALSE, sizeof (gint)  );

	if (selected_overlay && glob->overlaystore)
	{
		/* Include the overlay graphs */
		model = GTK_TREE_MODEL (glob->overlaystore);

		if (gtk_tree_model_get_iter_first (model, &iter))
		{
			gtk_tree_model_get (model, &iter, 0, &overlayid, -1);
			g_array_append_val (uids, overlayid);

			/* Set the linetype */
			/* g_array_append_val needs a variable as second parameter */
			linetype = 9;
			g_array_append_val (lt, linetype);

			str = g_strdup_printf ("overlay");
			g_array_append_val (legend, str);
			str = "";

			while (gtk_tree_model_iter_next (model, &iter))
			{
				gtk_tree_model_get (model, &iter, 0, &overlayid, -1);
				g_array_append_val (uids, overlayid);
				g_array_append_val (legend, str);
				g_array_append_val (lt, linetype);
			}
		}
	}
	
	/* Always include the data graph */;
	g_array_append_val (uids, glob->data->index);
	str = g_strdup_printf ("data");
	g_array_append_val (legend, str);

	linetype = 1;
	g_array_append_val (lt, linetype);

	if (selected_theory)
	{
		/* Include the theory graph */
		g_array_append_val (uids, glob->theory->index);
		str = g_strdup_printf ("theory");
		g_array_append_val (legend, str);

		linetype = 3;
		g_array_append_val (lt, linetype);
	}

	/* Export! */
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (
				glade_xml_get_widget (glob->fft->xmlfft, "abscissa_ns")
				)))
		str = "time [ns]";
	else
		str = "length [m]";

	if (!gtk_spect_vis_export_ps (spectvis, uids, selected_filename, selected_title, 
				 str, NULL, selected_footer, legend, selected_legend, lt))
	{
		dialog_message ("Error: Could not create graph. Is gnuplot installed on your system?");
		if (selected_filename[0] != '|')
			unlink (selected_filename);
	}

	/* Tidy up */
	g_array_free (uids, TRUE);
	g_array_free (legend, TRUE);
	g_array_free (lt, TRUE);
	g_free (selected_filename);
	g_free (selected_title);
	g_free (selected_footer);

	return TRUE;
}
#if 0
gboolean on_fourier_export_ps (GtkMenuItem *menuitem, gpointer user_data)
{
	/* This function is mostly stolen from export_graph_ps(). */
	
	const gchar *filename, *title, *footer;
	const gchar *filen, *path;
	GArray *uids, *legend, *lt;
	GtkSpectVis *spectvis;
	GladeXML *xmldialog;
	GtkWidget *dialog;
	GtkToggleButton *toggle;
	gboolean status, showlegend = FALSE;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint overlayid;
	gint linetype;
	gchar *str, *basename, pos;
	FILE *file;

	spectvis = GTK_SPECTVIS (glade_xml_get_widget (glob->fft->xmlfft, "fft_spectvis"));
	g_return_val_if_fail (spectvis, FALSE);

	/* Load the widgets */
	xmldialog = glade_xml_new (GLADEFILE, "export_ps_dialog", NULL);
	dialog = glade_xml_get_widget (xmldialog, "export_ps_dialog");

	/* Connect the select filename button */
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "button10")), 
			"clicked",
			(GCallback) export_select_filename,
			xmldialog);

	/* Put some default text into the GtkEntry fields */
	if (glob->section)
	{
		basename = g_path_get_basename (glob->resonancefile);
		str = g_strdup_printf ("file: %s (%s)", basename, glob->section);
		g_free (basename);

		gtk_entry_set_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_footer_entry")),
				str);
	}
	else if (glob->data->file)
	{
		basename = g_path_get_basename (glob->data->file);
		str = g_strdup_printf ("file: %s", basename);
		g_free (basename);

		gtk_entry_set_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_footer_entry")),
				str);
	}

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
	{
		path = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_path_entry")));
		filen = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_filename_entry")));
		if (path)
			filename = g_build_filename (path, filen, NULL);
		else
			filename = g_strdup (filen);

		title = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_title_entry")));
		footer = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_footer_entry")));

		/* Does the file already exist? */
		file = fopen (filename, "r");
		if (file)
		{
			basename = g_path_get_basename (filename);

			if (dialog_question ("File '%s' already exists, overwrite?", basename)
					!= GTK_RESPONSE_YES)
			{
				/* File exists, do not overwrite it. */
				fclose (file);
				g_free (basename);
				gtk_widget_destroy (dialog);
				statusbar_message ("Postscript export canceled by user");

				return FALSE;
			}

			fclose (file);
			g_free (basename);

			if (unlink (filename))
			{
				/* Something went wrong during file deletion */
				gtk_widget_destroy (dialog);
				dialog_message ("Cannot delete file %s.", filename);
				statusbar_message ("Postscript export canceled");
			}
		}
		else if (filename[0] != '|')
		{
			/* Is the file writeable? */

			/* Do this check only, if the first char isn't a pipe.
			 * If it is, the user wants to pass the ps to another
			 * application such as lpr.
			 */

			file = fopen (filename, "w");
			if (!file)
			{
				/* Cannot write to file -> abort */
				gtk_widget_destroy (dialog);
				dialog_message ("Cannot write to file %s.", filename);
				statusbar_message ("Postscript export canceled");

				return FALSE;
			}
			fclose (file);
		}

		uids = g_array_new (FALSE, FALSE, sizeof (guint));
		legend = g_array_new (FALSE, FALSE, sizeof (gchar*));
		lt = g_array_new (FALSE, FALSE, sizeof (gint));

		/* Should a legend be displayed? */
		toggle = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_legend_check"));
		if (gtk_toggle_button_get_active (toggle))
			showlegend = TRUE;

		/* And where? */
		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_top_button"))))
			pos = 't';
		else
			pos = 'b';
		
		toggle = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_overlay_check"));
		if ((gtk_toggle_button_get_active (toggle)) && (glob->overlaystore))
		{
			/* Include the overlay graphs */
			model = GTK_TREE_MODEL (glob->overlaystore);

			if (gtk_tree_model_get_iter_first (model, &iter))
			{
				gtk_tree_model_get (model, &iter, 0, &overlayid, -1);
				g_array_append_val (uids, overlayid);

				/* Set the linetype */
				/* g_array_append_val needs a variable as second parameter */
				linetype = 9;
				g_array_append_val (lt, linetype);

				str = "overlay";
				g_array_append_val (legend, str);
				str = "";

				while (gtk_tree_model_iter_next (model, &iter))
				{
					gtk_tree_model_get (model, &iter, 0, &overlayid, -1);
					g_array_append_val (uids, overlayid);
					g_array_append_val (legend, str);
					g_array_append_val (lt, linetype);
				}
			}
		}
		
		/* Always include the data graph */;
		g_array_append_val (uids, glob->data->index);
		if (showlegend)
			str = g_strdup_printf ("data");
		else
			str = "";
		g_array_append_val (legend, str);

		linetype = 1;
		g_array_append_val (lt, linetype);

		toggle = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_theory_check"));
		if (gtk_toggle_button_get_active (toggle))
		{
			/* Include the theory graph */
			g_array_append_val (uids, glob->theory->index);
			if (showlegend)
				str = g_strdup_printf ("theory");
			else
				str = "";
			g_array_append_val (legend, str);

			linetype = 3;
			g_array_append_val (lt, linetype);
		}

		/* Export! */
		if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (
					glade_xml_get_widget (glob->fft->xmlfft, "abscissa_ns")
					)))
			status = gtk_spect_vis_export_ps (spectvis, uids, filename, title, 
						 "time [ns]", NULL, footer, 
						 legend, pos, lt);
		else
			status = gtk_spect_vis_export_ps (spectvis, uids, filename, title, 
						 "length [m]", NULL, footer, 
						 legend, pos, lt);

		if (!status)
		{
			dialog_message ("Error: Could not create graph. Is gnuplot installed on your system?");
			if (filename[0] != '|')
				unlink (filename);
		}

		/* Tidy up */
		g_array_free (uids, TRUE);
		g_array_free (legend, TRUE);
		g_array_free (lt, TRUE);
	}

	gtk_widget_destroy (dialog);
	statusbar_message ("Postscript export successful");

	return TRUE;
}
#endif

gint fourier_handle_value_selected (GtkSpectVis *spectvis, gdouble *xval, gdouble *yval)
{
	static gdouble measure = 0;
	gchar *text;

	g_return_val_if_fail (glob->fft, 0);

	if (glob->flag & FLAG_FRQ_MEAS)
	{
		/* Measure the distance between two points */
		g_mutex_lock (glob->threads->flaglock);

		if (glob->fft->data) 
		{
			if (measure == 0)
			{
				measure = *xval;
			}
			else
			{
				glob->flag &= ~FLAG_FRQ_MEAS;
				measure = fabs (*xval - measure);

				if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (
						glade_xml_get_widget (glob->fft->xmlfft, "abscissa_ns")
						)))
					text = g_strdup_printf ("ns");
				else
					text = g_strdup_printf ("m");

				measure /= spectvis->xAxisScale;

				dialog_message ("The distance between the two selected points is:\n%.3f %s", 
						measure, text);

				g_free (text);
				measure = 0;
			}
		}
		else
		{
			glob->flag &= ~FLAG_FRQ_MEAS;
			measure = 0;
			dialog_message ("Please import some spectrum data first.");
		}

		g_mutex_unlock (glob->threads->flaglock);
		return 0;
	}
	else
		gtk_spect_vis_mark_point (spectvis, *xval, *yval);

	return 0;
}

/* Does a inverted fourier transform:
 * *d:   x is time in ns, y is complex time domain information
 * n:    number of points in *d, n-1 has to be a power of 2
 * fmin: Frequency offset
 * fmax: Frequency cutoff
 *
 * returns: the DataVector* of the frequency domain dataset
 */
DataVector* fourier_inverse_transform (DataVector *d, gdouble fmin, gdouble fmax)
{
	DataVector *ifft_data;
	gdouble *data;
	gdouble frqdelta, offset, c, s, re, im;
	gint i, frqpoints;
	gchar *filename = NULL;
	guint n;

	g_return_val_if_fail (d, NULL);

	/* At least some sanity checks */
	g_return_val_if_fail (d->x[0] == -d->x[d->len-1], NULL);
	g_return_val_if_fail (d->x[d->len/2] == 0, NULL);

	/* First and last datapoint are redundant,
	 * numpoints should now be a power of 2. */
	n = d->len -1;

	/* Prepare a vector for the inverse fft */
	data = g_new (gdouble, 2*n+1);
	offset = -2*M_PI*fmin;

	/* Negative times */
	for (i=0; i<n/2; i++)
	{
		c = cos (offset * d->x[i]/1e9);
		s = sin (offset * d->x[i]/1e9);
		re = d->y[i].re / (c*c + s*s);
		im = d->y[i].im / (c*c + s*s);
		
		data[2*i+n+1] = c*re - s*im;
		data[2*i+n+2] = s*re + c*im;
	}

	/* Positive times */
	for (i=n/2; i<n; i++)
	{
		c = cos (offset * d->x[i]/1e9);
		s = sin (offset * d->x[i]/1e9);
		re = d->y[i].re / (c*c + s*s);
		im = d->y[i].im / (c*c + s*s);
		
		data[2*(i-n/2)+1] = c*re - s*im;
		data[2*(i-n/2)+2] = s*re + c*im;
	}

	four1 (data, n, -1);

	frqdelta = -1.0/(2.0*d->x[0]*1e-9);
	frqpoints = (gint)((fmax-fmin)/frqdelta);

	if (d->file)
		filename = g_strdup (d->file);

	ifft_data = new_datavector (frqpoints+1);
	ifft_data->file = filename;

	for (i=0; i<=frqpoints; i++)
	{
		ifft_data->x[i] = fmin + (gdouble)i * frqdelta;
		ifft_data->y[i].re = data[2*i+1]/n;
		ifft_data->y[i].im = data[2*i+2]/n;
		ifft_data->y[i].abs = sqrt (ifft_data->y[i].re*ifft_data->y[i].re + 
					    ifft_data->y[i].im*ifft_data->y[i].im);
	}

	return ifft_data;
}
