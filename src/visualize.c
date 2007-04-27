#include <stdio.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <math.h>
#include <string.h>

#include "structs.h"
#include "numeric.h"
#include "processdata.h"
#include "resonancelist.h"
#include "gtkspectvis.h"
#include "helpers.h"
#include "visualize.h"
#include "fourier.h"
#include "spectral.h"
#include "fcomp.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

GtkWidget *NewSpectVis (gchar *widget_name, gchar *string1, gchar *string2, gint int1, gint int2)
{
	GtkWidget *spectvis = gtk_spect_vis_new ();

	gtk_spect_vis_set_axisscale (GTK_SPECTVIS (spectvis), 1e9, 1);
	gtk_widget_show (spectvis);

	return spectvis;
}

void visualize_newgraph ()
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");

	visualize_stop_background_calc ();

	if (glob->data && glob->data->index)
	{
		gtk_spect_vis_data_remove (GTK_SPECTVIS (graph), glob->data->index);
		glob->data->index = 0;
	}
	
	if (glob->theory)
	{
		if (glob->theory->index)
		{
			gtk_spect_vis_data_remove (GTK_SPECTVIS (graph), glob->theory->index);
			glob->theory->index = 0;
		}

		free_datavector (glob->theory);
		glob->theory = NULL;
	}

	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
}

void visualize_draw_data ()
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GdkColor color;

	if (!glob->data) return;

	color.red = 65535;
	color.green = 0;
	color.blue = 0;

	if (!glob->data->index)
	{
		glob->data->index = gtk_spect_vis_data_add (
			GTK_SPECTVIS (graph),
			glob->data->x,
			glob->data->y,
			glob->data->len,
			color, 'l');

		if (glob->prefs->datapoint_marks)
			gtk_spect_vis_set_graphtype (GTK_SPECTVIS (graph), glob->data->index, 'm');

		gtk_spect_vis_zoom_x_all (GTK_SPECTVIS (graph));
	}

	//gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (graph));
	//gtk_spect_vis_redraw (GTK_SPECTVIS (graph));

	fourier_update_main_graphs ();
	fcomp_update_graph ();
}

/* Calculate and draw a (new) theory graph. The "reason" for calling this
 * function is given by type which is just passed through to
 * visualize_handle_viewport_changed(). */
void visualize_theory_graph (gchar *type)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	gint i;

	if (!glob->data) return;

	visualize_stop_background_calc ();

	if (!glob->theory)
	{
		glob->theory = new_datavector (glob->data->len);
		g_free (glob->theory->x);
		glob->theory->x = glob->data->x;
	}

	if (glob->viewdifference)
	{
		visualize_difference_graph ();
		return;
	}
	else
		for (i=0; i<glob->theory->len; i++)
			glob->theory->y[i].abs = -1001;

	visualize_handle_viewport_changed (GTK_SPECTVIS (graph), type);

	fourier_update_main_graphs ();
	fcomp_update_graph ();
}

void visualize_difference_graph ()
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GdkColor color;
	gint i;

	if (glob->theory->len == 0) return;

	color.red = 0;
	color.green = 0;
	color.blue = 65535;
	
	for (i=0; i<glob->theory->len; i++)
	{
		glob->theory->y[i].abs = -1001;
	}

	if (glob->data->index)
	{
		gtk_spect_vis_data_remove (GTK_SPECTVIS (graph), glob->data->index);
		glob->data->index = 0;
	}
	if (glob->theory->index)
	{
		gtk_spect_vis_data_remove (GTK_SPECTVIS (graph), glob->theory->index);
		glob->theory->index = 0;
	}

	color.red = 65535;
	color.green = 0;
	color.blue = 65535;

	glob->theory->index = gtk_spect_vis_data_add (
		GTK_SPECTVIS (graph),
		glob->theory->x,
		glob->theory->y,
		glob->theory->len,
		color, 'l');

	visualize_handle_viewport_changed (GTK_SPECTVIS (graph), "u");
}

void visualize_calculate_difference (ComplexDouble *val, ComplexDouble *diff, guint pos)
{
	diff->abs = glob->data->y[pos].abs - val->abs;
	diff->re  = glob->data->y[pos].re  - val->re;
	diff->im  = glob->data->y[pos].im  - val->im;
}

void visualize_remove_difference_graph ()
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");

	if (glob->theory->index)
	{
		gtk_spect_vis_data_remove (GTK_SPECTVIS (graph), glob->theory->index);
		glob->theory->index = 0;
	}
}

gint visualize_handle_signal_marked (GtkSpectVis *spectvis, gdouble *xval, gdouble *yval)
{
	Resonance *res = NULL;
	DataVector *tmpdata;
	ComplexDouble y;
	gdouble *p, integral, tmp, mean, stddev;
	guint respos, trypoints_low, trypoints_high;
	gchar *text;
	gint i, n;
	
	g_mutex_lock (glob->threads->flaglock);

	if (glob->flag & FLAG_RES_CLICK) 
	{
		/* Add a resonance at the position clicked at */
		glob->flag &= ~FLAG_RES_CLICK;
		g_mutex_unlock (glob->threads->flaglock);

		if (glob->data) 
		{
			if (glob->numres > 0)
			{
				visualize_stop_background_calc ();

				respos = (gtk_spect_vis_get_data_by_uid (spectvis, glob->data->index))->xmin_arraypos;
				while ((*xval > glob->data->x[respos]) && (respos < glob->data->len))
					respos++;
				
				/* Assume that the resonance is smaller than 100MHz */
				trypoints_low = ((gdouble) 5e7 / (glob->data->x[1] - glob->data->x[0]));
				trypoints_high = trypoints_low;
				if ((int) respos - (int) trypoints_low < 0) 
					trypoints_low = respos;
				if (respos + trypoints_high > glob->data->len) 
					trypoints_high = glob->data->len - respos;
				
				tmpdata = new_datavector (trypoints_low + trypoints_high);
				p = g_new (gdouble, TOTALNUMPARAM+1);
				create_param_array (glob->param, glob->fcomp->data, glob->gparam, 
						glob->numres, glob->fcomp->numfcomp, p);
				
				for (i=respos-trypoints_low; i<respos+trypoints_high; i++)
				{
					tmpdata->x[i-respos+trypoints_low] = glob->data->x[i];

					y = ComplexWigner(glob->data->x[i], p, TOTALNUMPARAM);
					tmpdata->y[i-respos+trypoints_low].abs = 
						glob->data->y[i].abs - y.abs + glob->IsReflection*glob->gparam->scale;
					tmpdata->y[i-respos+trypoints_low].re  = 
						glob->data->y[i].re  - y.re + cos(glob->gparam->phase)*glob->gparam->scale*glob->IsReflection;
					tmpdata->y[i-respos+trypoints_low].im  = 
						glob->data->y[i].im  - y.im + sin(glob->gparam->phase)*glob->gparam->scale*glob->IsReflection;
					if (tmpdata->y[i-respos+trypoints_low].abs < 0) tmpdata->y[i-respos+trypoints_low].abs *= -1;
				}

				g_free (p);

				res = find_resonance_at (*xval, tmpdata);
				free_datavector (tmpdata);
			}
			else
			{
				res = find_resonance_at (*xval, glob->data);
			}
		}
		else 
		{
			dialog_message ("Please import some spectrum data first.");
			return 0;
		}
			
		if (res) 
		{
			add_resonance_to_list (res);
			if (glob->theory->index)
				visualize_theory_graph ("u");
			else
				/* Make sure the theory graph is shown */
				gtk_check_menu_item_set_active (
					GTK_CHECK_MENU_ITEM (glade_xml_get_widget (gladexml, "view_theory")),
					TRUE
				);
			set_unsaved_changes ();
			spectral_resonances_changed ();
			statusbar_message ("Added a resonance at %f GHz", res->frq / 1e9);
		}
		else 
			dialog_message("Error: Could not determine FWHM");
		
		return 0;
	}

	if (glob->flag & FLAG_RES_FIND) 
	{
		/* Try to find all resonances above the threshold */
		glob->flag &= ~FLAG_RES_FIND;
		g_mutex_unlock (glob->threads->flaglock);

		if (glob->data) 
		{
			visualize_stop_background_calc ();
			
			statusbar_message ("Processing spectrum, this could take a while...");
			while (gtk_events_pending ()) gtk_main_iteration ();
			statusbar_message ("Added %d resonances ", find_isolated_resonances (*yval));
			if (glob->theory->index)
				visualize_theory_graph ("u");
			else
				/* Make sure the theory graph is shown */
				gtk_check_menu_item_set_active (
					GTK_CHECK_MENU_ITEM (glade_xml_get_widget (gladexml, "view_theory")),
					TRUE
				);
			set_unsaved_changes ();
		}
		else 
			dialog_message ("Please import some spectrum data first.");

		return 0;
	}

	if (glob->flag & FLAG_FRQ_MAX)
	{
		/* Update the maximal frequency */
		if (*xval > glob->gparam->min)
		{
			glob->gparam->max = *xval;
			show_global_parameters (glob->gparam);
			glob->flag &= ~FLAG_FRQ_MAX;

			uncheck_res_out_of_frq_win (glob->gparam->min, glob->gparam->max);
			visualize_update_min_max (TRUE);
			fcomp_update_graph ();
			statusbar_message ("Frequency range adjusted");

			g_mutex_unlock (glob->threads->flaglock);
			set_unsaved_changes ();
			g_mutex_lock (glob->threads->flaglock);
		}
		else
			dialog_message ("The maximal frequency must be higher than the minimal one, choose upper bound again.");
		
		g_mutex_unlock (glob->threads->flaglock);
		return 0;
	}

	if (glob->flag & FLAG_FRQ_MIN)
	{
		/* Update the minimal frequency */
		glob->flag &= ~FLAG_FRQ_MIN;
		if (glob->data) 
		{
			glob->gparam->min = *xval;
			visualize_update_min_max (TRUE);

			glob->flag |= FLAG_FRQ_MAX;
			statusbar_message ("Mark maximal frequency on the graph");
		}
		else
			dialog_message ("Please import some spectrum data first.");

		g_mutex_unlock (glob->threads->flaglock);
		return 0;
	}

	if (glob->flag & FLAG_FRQ_MEAS)
	{
		/* Measure the frequency distance between two points */
		if (glob->data) 
		{
			if (glob->measure == 0)
			{
				glob->measure = *xval;
				statusbar_message ("Mark second frequency on the graph");
			}
			else
			{
				glob->flag &= ~FLAG_FRQ_MEAS;
				glob->measure = fabs (*xval - glob->measure);

				if (glob->measure > 1e9)
				{
					glob->measure /= 1e9;
					text = g_strdup_printf ("GHz");
				}
				else if (glob->measure > 1e6)
				{
					glob->measure /= 1e6;
					text = g_strdup_printf ("MHz");
				}
				else if (glob->measure > 1e3)
				{
					glob->measure /= 1e3;
					text = g_strdup_printf ("kHz");
				}
				else
					text = g_strdup_printf ("Hz");

				dialog_message ("The distance between the two selected points is:\n%.3f %s", 
						glob->measure, text);

				g_free (text);
				glob->measure = 0;
			}
		}
		else
		{
			glob->flag &= ~FLAG_FRQ_MEAS;
			glob->measure = 0;
			dialog_message ("Please import some spectrum data first.");
		}

		g_mutex_unlock (glob->threads->flaglock);
		return 0;
	}

	if (glob->flag & FLAG_FRQ_INT)
	{
		/* Integrate the area under the data curve */
		if (glob->data) 
		{
			if (glob->measure == 0)
			{
				glob->measure = *xval;
				statusbar_message ("Mark second frequency on the graph");
			}
			else
			{
				glob->flag &= ~FLAG_FRQ_INT;

				if (glob->measure > *xval)
				{
					integral = glob->measure;
					glob->measure = *xval;
					*xval = integral;
				}

				integral = 0;
				for (i=0; i<glob->data->len-1; i++)
				{
					if ( (glob->data->x[i] > glob->measure) &&
					     (glob->data->x[i] < *xval))
					{
						integral += 0.5 *
							( glob->data->y[i+1].abs + glob->data->y[i].abs )* 
							( glob->data->x[i+1] - glob->data->x[i] );
					}
				}

				if (integral > 1e9)
				{
					integral /= 1e9;
					text = g_strdup_printf ("GHz");
				}
				else if (integral > 1e6)
				{
					integral /= 1e6;
					text = g_strdup_printf ("MHz");
				}
				else if (integral > 1e3)
				{
					integral /= 1e3;
					text = g_strdup_printf ("kHz");
				}
				else
					text = g_strdup_printf ("Hz");

				dialog_message ("The area under the graph is:\n%.3f %s",
						integral, text);

				g_free (text);
				glob->measure = 0;
			}
		}
		else
		{
			glob->flag &= ~FLAG_FRQ_MEAS;
			glob->measure = 0;
			dialog_message ("Please import some spectrum data first.");
		}

		g_mutex_unlock (glob->threads->flaglock);
		return 0;
	}

	if (glob->flag & FLAG_MRK_NOISE)
	{
		/* Estimate data noise from sample region */
		if (glob->data) 
		{
			if (glob->measure == 0)
			{
				glob->measure = *xval;
				statusbar_message ("Mark second frequency on the graph");
			}
			else
			{
				glob->flag &= ~FLAG_MRK_NOISE;

				if (glob->measure > *xval)
				{
					/* make glob->measure < *xval */
					tmp = glob->measure;
					glob->measure = *xval;
					*xval = tmp;
				}

				n = 0;
				mean = 0;
				/* Calculate mean value */
				for (i=0; i<glob->data->len-1; i++)
				{
					if ( (glob->data->x[i] > glob->measure) &&
					     (glob->data->x[i] < *xval))
					{
						if (glob->viewdifference == FALSE)
							mean += glob->data->y[i].abs - 
								glob->theory->y[i].abs;
						else
							mean += glob->theory->y[i].abs;

						n++;
					}
				}

				if (n == 0)
				{
					dialog_message ("Error: No points in range!");
					g_mutex_unlock (glob->threads->flaglock);
					return 0;
				}

				mean /= (gdouble) n;

				stddev = 0.0;
				/* Calculate stddev */
				for (i=0; i<glob->data->len-1; i++)
				{
					if ( (glob->data->x[i] > glob->measure) &&
					     (glob->data->x[i] < *xval))
					{
						if (glob->viewdifference == FALSE)
							stddev += pow (glob->data->y[i].abs - 
								glob->theory->y[i].abs - mean, 2);
						else
							stddev += pow (glob->theory->y[i].abs - mean, 2);
					}
				}

				glob->noise = sqrt(stddev/( (gdouble)n - 1 ));
				printf("mean: %e, stddev: %e\n", mean, glob->noise);

				glob->measure = 0;
			}
		}
		else
		{
			glob->flag &= ~FLAG_MRK_NOISE;
			glob->measure = 0;
			dialog_message ("Please import some spectrum data first.");
		}

		g_mutex_unlock (glob->threads->flaglock);
		return 0;
	}

	g_mutex_unlock (glob->threads->flaglock);

	/* If this line is reached there is nothing special to be done. The
	 * user has just clicked on the graph. Let's give him some information
	 * about this point he seems to be so interested in.
	 */

	gtk_spect_vis_mark_point (spectvis, *xval, *yval);
	
	return 0;
}

void visualize_update_res_bar (gboolean redraw)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	gint *id, tryid;
	gdouble width;
	Resonance *res;
	GdkColor col;

	if (!glob->data)
		return;

	/* Try to get the selected resonance by the cursor position first. */
	if ((tryid = get_resonance_id_by_cursur ()))
	{
		id = g_new (gint, 1);
		id[0] = tryid;
	}
	else
		id = get_selected_resonance_ids (TRUE);


	/* glob->bars[0] holds the green resonance bar */
	if (glob->bars[0] != 0)
	{
		/* remove an already present bar */
		gtk_spect_vis_remove_bar (GTK_SPECTVIS (graph), glob->bars[0]);
		glob->bars[0] = 0;
	}

	if (id[0] == 0) 
	{
		/* do nothing but a redraw if no resonance is selected */
		if (redraw) 
			gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
		g_free (id);
		return;
	}

	res = g_ptr_array_index (glob->param, id[0]-1);
	g_free (id);

	col.green = 62000;
	col.blue = col.red = 20000;

	width = res->width;
	if (width > 1e9)
	{
		width = 1e9;
		col.red   = 52000;
		col.green = 10000;
		col.blue  = 40000;
	}
	
	glob->bars[0] = gtk_spect_vis_add_bar (GTK_SPECTVIS (graph), res->frq, width, col);

	if (redraw) gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
}

void visualize_update_min_max (gboolean redraw)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GdkColor col;

	if (glob->bars[1] != 0)
	{
		gtk_spect_vis_remove_bar (GTK_SPECTVIS (graph), glob->bars[1]);
		glob->bars[1] = 0;
	}
	if (glob->bars[2] != 0)
	{
		gtk_spect_vis_remove_bar (GTK_SPECTVIS (graph), glob->bars[2]);
		glob->bars[2] = 0;
	}

	col.red   = 65500;
	col.green = 45000;
	col.blue  = 0;
	
	glob->bars[1] = gtk_spect_vis_add_bar (GTK_SPECTVIS (graph), glob->gparam->min, 0, col);
	glob->bars[2] = gtk_spect_vis_add_bar (GTK_SPECTVIS (graph), glob->gparam->max, 0, col);

	spectral_resonances_changed ();
	
	if (redraw) gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
}

void visualize_restore_cursor ()
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	
	gtk_spect_vis_set_visible_cursor (GTK_SPECTVIS (graph));
}

void visualize_background_calc (GtkSpectVisData *data)
{
	guint i;
	double *p;
	gchar *message = NULL;
	ComplexDouble *theoryY;

	g_mutex_lock (glob->threads->theorylock);

	//printf("background start\n");
	if (!glob->theory) return;

	theoryY = glob->theory->y;
	p = g_new (gdouble, TOTALNUMPARAM+1);
	create_param_array (glob->param, glob->fcomp->data, glob->gparam, 
			glob->numres, glob->fcomp->numfcomp, p);

	i = data->xmax_arraypos;
	while (i < glob->theory->len)
	{
		if (theoryY[i].abs == -1001)
		{
			theoryY[i] = ComplexWigner(glob->data->x[i], p, TOTALNUMPARAM);
			if (glob->viewdifference)
				visualize_calculate_difference (&theoryY[i], &theoryY[i], i);

			message = (gchar *) g_async_queue_try_pop (glob->threads->aqueue1);
			if ((message) && (*message == 'b'))
			{
				g_free (p);
				g_mutex_unlock (glob->threads->theorylock);
				g_async_queue_push (glob->threads->aqueue2, "b");
				//printf("background break\n");
				return;
			}
		}

		i++;
	}

	i = data->xmax_arraypos;
	while (i > 0)
	{
		if (theoryY[i].abs == -1001)
		{
			theoryY[i] = ComplexWigner(glob->data->x[i], p, TOTALNUMPARAM);
			if (glob->viewdifference)
				visualize_calculate_difference (&theoryY[i], &theoryY[i], i);

			message = (gchar *) g_async_queue_try_pop (glob->threads->aqueue1);
			if ((message) && (*message == 'b'))
			{
				g_free (p);
				g_mutex_unlock (glob->threads->theorylock);
				g_async_queue_push (glob->threads->aqueue2, "b");
				//printf("background break\n");
				return;
			}
		}

		i--;
	}

	g_free (p);

	g_mutex_unlock (glob->threads->theorylock);

	g_mutex_lock (glob->threads->flaglock);
	glob->flag &= ~FLAG_CAL_RUN;
	g_mutex_unlock (glob->threads->flaglock);

	message = (gchar *) g_async_queue_try_pop (glob->threads->aqueue1);
	if ((message) && (*message == 'b'))
		g_async_queue_push (glob->threads->aqueue2, "b");

	//printf("background ready\n");
}

gboolean visualize_stop_background_calc ()
{
	gboolean retval = FALSE;
	
	g_mutex_lock (glob->threads->flaglock);
	
	/* Is a calculation running? */
	if (glob->flag & FLAG_CAL_RUN)
	{
		g_mutex_unlock (glob->threads->flaglock);
		
		/* Send other thread a b(reak) */
		g_async_queue_push (glob->threads->aqueue1, "b");
		//printf("wait\n");

		/* Block until it has returned a reply */
		g_async_queue_pop (glob->threads->aqueue2);
		//printf("wait - end\n");

		/* Unset the CAL_RUN flag */
		g_mutex_lock (glob->threads->flaglock);
		glob->flag &= ~FLAG_CAL_RUN;
		g_mutex_unlock (glob->threads->flaglock);

		retval = TRUE;
	}
	else
		g_mutex_unlock (glob->threads->flaglock);

	return retval;
}

void visualize_handle_viewport_changed (GtkSpectVis *spectvis, gchar *zoomtype)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GtkSpectVisViewport *view = NULL;
	ComplexDouble *theoryY;
	GdkColor color;
	guint xmina, xmaxa, i = 0;
	gboolean flag = FALSE;
	double *p;

	//printf ("changed\n");

	if (!glob->data) return;

	if (!glob->theory)
	{
		g_mutex_lock (glob->threads->theorylock);
		glob->theory = new_datavector (glob->data->len);
		g_free (glob->theory->x);
		glob->theory->x = glob->data->x;
		g_mutex_unlock (glob->threads->theorylock);
	}

	if ((!glob->theory->index) && (glob->viewtheory)) {
		color.red   = 0;
		color.green = 0;
		color.blue  = 65535;

		glob->theory->index = gtk_spect_vis_data_add (
			GTK_SPECTVIS (graph),
			glob->theory->x,
			glob->theory->y,
			glob->theory->len,
			color, 'l');
	}

	if (glob->theory->index)
	{
		xmina = (gtk_spect_vis_get_data_by_uid (spectvis, glob->theory->index))->xmin_arraypos;
		xmaxa = (gtk_spect_vis_get_data_by_uid (spectvis, glob->theory->index))->xmax_arraypos;
	}
	else
	{
		xmina = 1;
		xmaxa = glob->theory->len-1;
	}

	if ((*zoomtype != 'i') && (*zoomtype != 'I') && (*zoomtype != 'O'))
	{
		visualize_stop_background_calc ();

		view = spectvis->view;

		theoryY = glob->theory->y;
		p = g_new (gdouble, TOTALNUMPARAM+1);
		create_param_array (glob->param, glob->fcomp->data, glob->gparam, 
				glob->numres, glob->fcomp->numfcomp, p);

		g_mutex_lock (glob->threads->theorylock);

		i = xmina;
		if (i > 0) 
			i--;
		while ((i <= xmaxa) && (i < glob->data->len) && (theoryY[i].abs < -1000))
		{
			theoryY[i] = ComplexWigner (glob->data->x[i], p, TOTALNUMPARAM);
			if (glob->viewdifference)
				visualize_calculate_difference (&theoryY[i], &theoryY[i], i);
			flag = TRUE;

			if ((*zoomtype == 'n') && (glob->data->len*glob->numres > 8010*10) && (i % (int)(glob->data->len*0.01) == 0))
				/* A new theory is being calculated -> give some visual feedback */
				status_progressbar_set ((gdouble)i/(gdouble)glob->data->len);

			i++;
		}

		i = xmaxa;
		if (i < glob->data->len - 1) 
			i++;
		while ((i > xmina) && (i >= 0) && (theoryY[i].abs < -1000))
		{
			theoryY[i] = ComplexWigner (glob->data->x[i], p, TOTALNUMPARAM);
			if (glob->viewdifference)
				visualize_calculate_difference (&theoryY[i], &theoryY[i], i);
			flag = TRUE;

			if ((*zoomtype == 'n') && (glob->data->len*glob->numres > 8010*10) && (i % (int)(glob->data->len*0.01) == 0))
				/* A new theory is being calculated -> give some visual feedback */
				status_progressbar_set ((gdouble)i/(gdouble)glob->data->len);
			i--;
		}

		g_free (p);
		g_mutex_unlock (glob->threads->theorylock);
	}

	if (*zoomtype == 'u')
	{
		status_progressbar_set (-1.0);

		if (!glob->theory->index)
		{
			gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (graph));
			gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
		}
	}

//	if ((*zoomtype != 'I') && (*zoomtype != 'O') && (*zoomtype != 'i') && (*zoomtype != 'o'))
	if (flag && glob->theory->index)
		gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (graph));

	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
	
	if (flag && glob->theory->index)
	/* Start calculation only if there had been values to be calculated
	 * in the current viewport */
	{
		g_mutex_lock (glob->threads->flaglock);
		glob->flag |= FLAG_CAL_RUN;
		g_mutex_unlock (glob->threads->flaglock);

		g_thread_pool_push (
				glob->threads->pool, 
				gtk_spect_vis_get_data_by_uid (spectvis, glob->theory->index), 
				NULL);
	}
	//printf ("changed - ready\n");
}

void visualize_zoom_to_frequency_range (gdouble min, gdouble max)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GtkSpectVis *spectvis;
	ComplexDouble *theoryY;
	gboolean flag = FALSE;
	gdouble *p;
	guint i, xmina, xmaxa;

	g_return_if_fail (GTK_IS_SPECTVIS (graph));
	spectvis = GTK_SPECTVIS (graph);

	if (!glob->data) return;

	if (glob->theory == NULL)
	{
		g_mutex_lock (glob->threads->theorylock);
		glob->theory = new_datavector (glob->data->len);
		g_mutex_unlock (glob->threads->theorylock);
	}

	if (visualize_stop_background_calc ())
		flag = TRUE;

	gtk_spect_vis_zoom_x_to (spectvis, min - (max-min)*0.02, max + (max-min)*0.02);

	xmina = (gtk_spect_vis_get_data_by_uid (spectvis, glob->data->index))->xmin_arraypos;
	xmaxa = (gtk_spect_vis_get_data_by_uid (spectvis, glob->data->index))->xmax_arraypos;

	/* Recalculate the theory values where necessary */
	theoryY = glob->theory->y;
	p = g_new (gdouble, TOTALNUMPARAM+1);
	create_param_array (glob->param, glob->fcomp->data, glob->gparam, 
			glob->numres, glob->fcomp->numfcomp, p);

	g_mutex_lock (glob->threads->theorylock);
	i = xmina;
	if (i > 0) i--;
	while (i <= xmaxa)
	{
		if (theoryY[i].abs == -1001)
		{
			theoryY[i] = ComplexWigner(glob->data->x[i], p, TOTALNUMPARAM);
			if (glob->viewdifference)
				visualize_calculate_difference (&theoryY[i], &theoryY[i], i);
		}

		i++;
	}
	g_mutex_unlock (glob->threads->theorylock);

	gtk_spect_vis_zoom_y_all (spectvis);
	gtk_spect_vis_redraw (spectvis);

	if (flag && glob->theory->index)
	/* Start calculation only if one has been running before */
	{
		g_mutex_lock (glob->threads->flaglock);
		glob->flag |= FLAG_CAL_RUN;
		g_mutex_unlock (glob->threads->flaglock);

		g_thread_pool_push (glob->threads->pool, gtk_spect_vis_get_data_by_uid (spectvis, glob->theory->index), NULL);
	}
}
