#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gdk/gdkkeysyms.h>

#include "structs.h"
#include "helpers.h"
#include "processdata.h"
#include "gtkspectvis.h"
#include "fourier.h"
#include "visualize.h"
#include "calibrate_offline.h"
#include "numeric.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

/* Forward declarations */
void fcomp_update_graph ();

/* Columns in fcomptreeview */
enum {
	FCOMP_ID_COL = 0,
	FCOMP_TAU_COL, FCOMP_AMP_COL, FCOMP_PHI_COL,
	FCOMP_FIT_TAU_COL, FCOMP_FIT_AMP_COL, FCOMP_FIT_PHI_COL,
	FCOMP_N_COLUMNS
};

static gboolean fcomp_cell_edited_callback (GtkCellRendererText *cell, const gchar *path, gchar *text, gpointer data)
{
	guint column = GPOINTER_TO_UINT(data);
	gint id;
	gdouble value;
	GtkTreeIter iter;
	FourierComponent *fcomp;

	if (sscanf (text, "%lf", &value) != 1)
		return FALSE;

	gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (glob->fcomp->store), &iter, path);
	gtk_list_store_set (glob->fcomp->store, &iter, column, value, -1);

	gtk_tree_model_get (GTK_TREE_MODEL (glob->fcomp->store), &iter, FCOMP_ID_COL, &id, -1);
	fcomp = g_ptr_array_index (glob->fcomp->data, id-1);

	switch (column)
	{
		case FCOMP_AMP_COL:
			fcomp->amp = value;
			break;
		case FCOMP_TAU_COL:
			fcomp->tau = value / 1e9;
			break;
		case FCOMP_PHI_COL:
			fcomp->phi = value / 180*M_PI;
			break;
	}

	visualize_theory_graph ("u");
	fcomp_update_graph ();
	set_unsaved_changes ();

	return FALSE;
}

/* Update the model if a checkbox in the list has been toggled */
static void fcomp_cell_toggle_callback (GtkCellRendererToggle *cell, gchar *path, gpointer data)
{
	gboolean column = GPOINTER_TO_UINT(data);
	gboolean curstate;
	GtkTreeIter iter;

	gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (glob->fcomp->store), &iter, path);

	gtk_tree_model_get (GTK_TREE_MODEL (glob->fcomp->store), &iter, column, &curstate, -1);
	gtk_list_store_set (glob->fcomp->store, &iter, column, !curstate, -1);
}

/* Check or uncheck the boxes if a column header has been clicked */
static void fcomp_column_clicked_callback (GtkTreeViewColumn *column, gpointer user_data)
{
	GtkTreeModel *model;
	gboolean col = GPOINTER_TO_UINT(user_data);
	GtkTreeIter iter;
	gint active = 0, inactive = 0;
	gboolean fit;

	g_return_if_fail (GTK_IS_TREE_MODEL (glob->fcomp->store));
	model = GTK_TREE_MODEL (glob->fcomp->store);

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do {
			gtk_tree_model_get (model, &iter, col, &fit, -1);
			if (fit)
				active++;
			else
				inactive++;
		} while (gtk_tree_model_iter_next (model, &iter));
	}
	else
		return;

	if (inactive > active)
		fit = TRUE;
	else
		fit = FALSE;

	gtk_tree_model_get_iter_first (model, &iter);

	do
		gtk_list_store_set (glob->fcomp->store, &iter, col, fit, -1);
	while (gtk_tree_model_iter_next (model, &iter));
}

/* Add a FourierComponent to the PtrArray (if id>0) and the ListStore */
void fcomp_add_component (FourierComponent *fcomp, gint id)
{
	GtkTreeIter iter;

	g_return_if_fail (fcomp);
	g_return_if_fail (glob->fcomp->data);

	if (!id)
	{
		id = glob->fcomp->data->len + 1;
		g_return_if_fail (id-1 == glob->fcomp->numfcomp);

		g_ptr_array_add (glob->fcomp->data, fcomp);
		glob->fcomp->numfcomp++;
	}

	if (glob->fcomp->store)
	{
		gtk_list_store_append (glob->fcomp->store, &iter);

		gtk_list_store_set (glob->fcomp->store, &iter,
				FCOMP_ID_COL, id,
				FCOMP_AMP_COL, fcomp->amp,
				FCOMP_TAU_COL, fcomp->tau * 1e9,
				FCOMP_PHI_COL, NormalisePhase(fcomp->phi) / M_PI*180,
				FCOMP_FIT_AMP_COL, 1,
				FCOMP_FIT_TAU_COL, 1,
				FCOMP_FIT_PHI_COL, 1,
				-1);
	}
}

/* Update the ListStore with the current glob->fcomp->data values */
void fcomp_update_list ()
{
	GtkTreeIter iter;
	gint id;
	GtkTreeModel *model;
	FourierComponent *f;

	if (!glob->fcomp->store)
		return;

	if (!glob->fcomp->xmlfcomp)
		return;

	model = GTK_TREE_MODEL (glob->fcomp->store);

	if (gtk_tree_model_get_iter_first (model, &iter))
		do
		{
			id = 0;
			gtk_tree_model_get (model, &iter, FCOMP_ID_COL, &id, -1);
			if ((id > 0) && (id <= glob->fcomp->numfcomp))
			{
				f = (FourierComponent *) g_ptr_array_index (glob->fcomp->data, id-1);
				gtk_list_store_set (glob->fcomp->store, &iter,
						FCOMP_AMP_COL, f->amp,
						FCOMP_TAU_COL, f->tau * 1e9,
						FCOMP_PHI_COL, NormalisePhase(f->phi) / M_PI*180,
						-1);
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
}

/* Display the fcomp_win and prepare the liststore. */
void fcomp_open_win ()
{
	GtkSpectVis *graph;
	GladeXML *xmlfcomp;
	GtkWidget *treeview;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer, *togglerenderer;
	gint i;

	if (glob->fcomp->store)
		return;

	xmlfcomp = glade_xml_new (GLADEFILE, "fcomp_win", NULL);
	glade_xml_signal_autoconnect (xmlfcomp);
	glob->fcomp->xmlfcomp = xmlfcomp;

	treeview = glade_xml_get_widget (xmlfcomp, "fcomp_treeview");

	glob->fcomp->store = gtk_list_store_new (FCOMP_N_COLUMNS,
			G_TYPE_UINT,
			G_TYPE_DOUBLE,
			G_TYPE_DOUBLE,
			G_TYPE_DOUBLE,
			G_TYPE_BOOLEAN,
			G_TYPE_BOOLEAN,
			G_TYPE_BOOLEAN);

	gtk_tree_view_set_model (
			GTK_TREE_VIEW (treeview),
			GTK_TREE_MODEL (glob->fcomp->store));
	/* ID_COL */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
			"#", renderer,
			"text", FCOMP_ID_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, FALSE);
	gtk_tree_view_column_set_sort_column_id (column, FCOMP_ID_COL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	//column_separator;

	/* TAU_COL */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", (GCallback) fcomp_cell_edited_callback,
	                  GUINT_TO_POINTER(FCOMP_TAU_COL));

	column = gtk_tree_view_column_new_with_attributes (
			"time [ns]", renderer,
			"text", FCOMP_TAU_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, FCOMP_TAU_COL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* FIT_TAU_COL */
	togglerenderer = gtk_cell_renderer_toggle_new ();
	g_object_set (togglerenderer, "xalign", 0.0, NULL);
	g_signal_connect (togglerenderer, "toggled", G_CALLBACK (fcomp_cell_toggle_callback),
	                  GUINT_TO_POINTER(FCOMP_FIT_TAU_COL));
	column = gtk_tree_view_column_new_with_attributes (
			"", togglerenderer,
			"active", FCOMP_FIT_TAU_COL,
			NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	gtk_tree_view_column_set_clickable (column, TRUE);
	g_signal_connect (column, "clicked", G_CALLBACK (fcomp_column_clicked_callback),
			GUINT_TO_POINTER(FCOMP_FIT_TAU_COL));
	//column_separator;

	/* AMP_COL */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", (GCallback) fcomp_cell_edited_callback,
	                  GUINT_TO_POINTER(FCOMP_AMP_COL));

	column = gtk_tree_view_column_new_with_attributes (
			"amplitude", renderer,
			"text", FCOMP_AMP_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, FCOMP_AMP_COL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* FIT_AMP_COL */
	togglerenderer = gtk_cell_renderer_toggle_new ();
	g_object_set (togglerenderer, "xalign", 0.0, NULL);
	g_signal_connect (togglerenderer, "toggled", G_CALLBACK (fcomp_cell_toggle_callback),
	                  GUINT_TO_POINTER(FCOMP_FIT_AMP_COL));
	column = gtk_tree_view_column_new_with_attributes (
			"", togglerenderer,
			"active", FCOMP_FIT_AMP_COL,
			NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	gtk_tree_view_column_set_clickable (column, TRUE);
	g_signal_connect (column, "clicked", G_CALLBACK (fcomp_column_clicked_callback),
			GUINT_TO_POINTER(FCOMP_FIT_AMP_COL));
	//column_separator;

	/* PHI_COL */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set (renderer, "editable", TRUE, NULL);
	g_signal_connect (renderer, "edited", (GCallback) fcomp_cell_edited_callback,
	                  GUINT_TO_POINTER(FCOMP_PHI_COL));

	column = gtk_tree_view_column_new_with_attributes (
			"phase [deg]", renderer,
			"text", FCOMP_PHI_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, FCOMP_PHI_COL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* FIT_PHI_COL */
	togglerenderer = gtk_cell_renderer_toggle_new ();
	g_object_set (togglerenderer, "xalign", 0.0, NULL);
	g_signal_connect (togglerenderer, "toggled", G_CALLBACK (fcomp_cell_toggle_callback),
	                  GUINT_TO_POINTER(FCOMP_FIT_PHI_COL));
	column = gtk_tree_view_column_new_with_attributes (
			"", togglerenderer,
			"active", FCOMP_FIT_PHI_COL,
			NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	gtk_tree_view_column_set_clickable (column, TRUE);
	g_signal_connect (column, "clicked", G_CALLBACK (fcomp_column_clicked_callback),
			GUINT_TO_POINTER(FCOMP_FIT_PHI_COL));

	/* A dummy column */
	column = gtk_tree_view_column_new ();
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* Populate the store */
	gtk_list_store_clear (glob->fcomp->store);
	for (i=0; i<glob->fcomp->numfcomp; i++)
		fcomp_add_component (
			(FourierComponent *) g_ptr_array_index (glob->fcomp->data, i),
			i+1);

	/* Prepare the graph */
	graph = GTK_SPECTVIS (glade_xml_get_widget (xmlfcomp, "fcomp_spectvis"));
	gtk_spect_vis_set_axisscale (graph, 1e-9, 1);
	fcomp_update_graph ();
}

/* Delete a Fourier component from the ListStore and from the array */
void fcomp_remove_selected ()
{
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GList *pathlist;
	gint id = -1, curid;

	selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW (
			glade_xml_get_widget (glob->fcomp->xmlfcomp, "fcomp_treeview")));

	if (gtk_tree_selection_count_selected_rows (selection) == 0)
	{
		dialog_message ("No row selected.");
		return;
	}

	/* Get path of selected row */
	model     = GTK_TREE_MODEL (glob->fcomp->store);
	pathlist  = gtk_tree_selection_get_selected_rows (selection, &model);

	/* Remove it */
	gtk_tree_model_get_iter (model, &iter, (GtkTreePath *) pathlist->data);
	gtk_tree_model_get (model, &iter, FCOMP_ID_COL, &id, -1);
	if (id < 1)
	{
		dialog_message ("Error: Could not determine resonance ID.");
		return;
	}
	gtk_list_store_remove (glob->fcomp->store, &iter);
	g_ptr_array_remove_index (glob->fcomp->data, id-1);
	glob->fcomp->numfcomp--;

	/* Change ID's of other rows */
	if (gtk_tree_model_get_iter_first (model, &iter))
		do
		{
			gtk_tree_model_get (model, &iter, FCOMP_ID_COL, &curid, -1);
			if (curid > id)
			{
				curid--;
				gtk_list_store_set (glob->fcomp->store, &iter, FCOMP_ID_COL, curid, -1);
			}
		}
		while (gtk_tree_model_iter_next(model, &iter));

	/* Select the old path -> the next resonance in the list */
	gtk_tree_selection_select_path (selection, (GtkTreePath *) pathlist->data);

	/* Tidy up */
	g_list_foreach (pathlist, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (pathlist);

	/* Redraw */
	visualize_theory_graph ("u");
	fcomp_update_graph ();
	set_unsaved_changes ();
}

gboolean fcomp_key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	g_return_val_if_fail (event != NULL, FALSE);

	if (event->keyval == GDK_Delete)
		fcomp_remove_selected ();

	return FALSE;
}

/* Close the fcomp window, and delete all Fourier components */
void fcomp_purge ()
{
	if (glob->fcomp->xmlfcomp)
	{
		gtk_widget_destroy (glade_xml_get_widget (glob->fcomp->xmlfcomp, "fcomp_spectvis"));
		gtk_widget_destroy (glade_xml_get_widget (glob->fcomp->xmlfcomp, "fcomp_win"));
	}

	if (glob->fcomp->store)
	{
		gtk_list_store_clear (glob->fcomp->store);
		glob->fcomp->store = NULL;
	}

	free_datavector (glob->fcomp->quotient);
	free_datavector (glob->fcomp->theo);

	glob->fcomp->xmlfcomp = NULL;
	glob->fcomp->quotient = NULL;
	glob->fcomp->theo = NULL;

	g_ptr_array_foreach (glob->fcomp->data, (GFunc) g_free, NULL);
	g_ptr_array_free (glob->fcomp->data, TRUE);
	glob->fcomp->data = g_ptr_array_new ();
	glob->fcomp->numfcomp = 0;
}

/* Close the fcomp_win and tidy up. */
gboolean fcomp_win_close (GtkWidget *button)
{
	if (!glob->fcomp->xmlfcomp)
		return TRUE;

	gtk_widget_destroy (glade_xml_get_widget (glob->fcomp->xmlfcomp, "fcomp_spectvis"));
	gtk_widget_destroy (glade_xml_get_widget (glob->fcomp->xmlfcomp, "fcomp_win"));

	gtk_list_store_clear (glob->fcomp->store);
	free_datavector (glob->fcomp->quotient);
	free_datavector (glob->fcomp->theo);

	glob->fcomp->store = NULL;
	glob->fcomp->xmlfcomp = NULL;
	glob->fcomp->quotient = NULL;
	glob->fcomp->theo = NULL;

	return TRUE;
}

/* Zooming, etc. */
void fcomp_handle_viewport_changed (GtkSpectVis *spectvis, gchar *zoomtype)
{
	gtk_spect_vis_redraw (spectvis);
}

/* Add a fourier component or mark the position */
gint fcomp_handle_signal_marked (GtkSpectVis *spectvis, gdouble *xval, gdouble *yval)
{
	FourierComponent *newcomp;
	GtkSpectVisData *data;
	gdouble norm;
	gint pos;

	if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (
				glob->fcomp->xmlfcomp, "fcomp_addcheck"))))
	{
		/* Add a fourier component */
		newcomp = g_new (FourierComponent, 1);
		data = gtk_spect_vis_get_data_by_uid (spectvis, 1);
		g_return_val_if_fail (data, 0);

		pos = 0;
		while ((pos < data->len-1) && (data->X[pos] < *xval))
			pos++;

		/* Is the other value nearer to *xval? */
		if ((pos > 0) && (data->X[pos]-*xval > *xval-data->X[pos-1]))
			pos--;

		newcomp->tau = data->X[pos];
		newcomp->phi = atan2 (data->Y[pos].im, data->Y[pos].re);
		newcomp->amp = data->Y[pos].abs/(2*M_PI*(glob->gparam->max - glob->gparam->min));

		norm = 1.0;
		for (pos=0; pos<glob->fcomp->numfcomp; pos++)
			norm += ((FourierComponent *) g_ptr_array_index (glob->fcomp->data, pos))->amp;

		newcomp->amp = norm * newcomp->amp / (1.0-newcomp->amp);
		//FIXME: This calculation is an ugly hack!!!
		newcomp->amp *= (2*2*M_PI)*1e5;

		fcomp_add_component (newcomp, 0);
		fcomp_update_graph ();
		visualize_theory_graph ("u");
	}
	else
	{
		/* Mark position */
		gtk_spect_vis_mark_point (spectvis, *xval, *yval);
	}

	return 0;
}

/* Show the graph */
void fcomp_update_graph ()
{
	GtkSpectVis *graph;
	DataVector *quotient, *ftheo;
	FourierComponent *set;
	guint start, stop, i, j;
	double *pquot, factor, omega;
	gboolean redraw = FALSE;
	GdkColor color;

	if ((!glob->numres) && (!glob->IsReflection))
		return;
	if (!glob->data)
		return;
	if (!glob->fcomp->store)
		return;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->fcomp->xmlfcomp, "fcomp_spectvis"));
	g_return_if_fail (graph);

	/* start and stop corresponding to gparam->min and gparam->max */
	start = 0;
	while ((glob->data->x[start] < glob->gparam->min) && (start < glob->data->len-2))
		start++;

	stop = glob->data->len - 1;
	while ((glob->data->x[stop] > glob->gparam->max) && (stop > 0))
		stop--;

	if (stop <= start)
		return;

	/* quotient will be the graph of one minus the quotient without the fourier component */
	quotient = new_datavector (stop-start+1);
	pquot = g_new (double, TOTALNUMPARAM+1);
	create_param_array (glob->param, glob->fcomp->data, glob->gparam,
			glob->numres, glob->fcomp->numfcomp, pquot);
	/* set all fourier amplitudes to zero */
	for (i=0; i<glob->fcomp->numfcomp; i++)
		pquot[4*glob->numres+3*i+1] = 0.0;

	for (i=start; i<=stop; i++)
	{
		quotient->x[i-start] = glob->data->x[i];
		quotient->y[i-start] = c_div (
				glob->data->y[i],
				ComplexWigner (glob->data->x[i], pquot, TOTALNUMPARAM));
		quotient->y[i-start].re -= 1.0;
	}
	g_free (pquot);


	/* ftheo will be the graph of just the fourier component term */
	ftheo = new_datavector (stop-start+1);
	g_free (ftheo->x);
	ftheo->x = quotient->x;
	/* I cannot use ComplexWigner here, as IsReflection may not be set */
	for (i=0; i<stop-start+1; i++)
	{
		ftheo->y[i].re = 0.0; /* not 1.0 here! */
		ftheo->y[i].im = 0.0;
		factor = 1.0;
		omega  = 2*M_PI*ftheo->x[i];

		for (j=0; j<glob->fcomp->numfcomp; j++)
		{
			set = (FourierComponent *) g_ptr_array_index (glob->fcomp->data, j);
			ftheo->y[i].re -= set->amp * cos(-omega * set->tau + set->phi);
			ftheo->y[i].im -= set->amp * sin(-omega * set->tau + set->phi);
			factor += fabs(set->amp);
		}

		ftheo->y[i].re /= factor;
		ftheo->y[i].im /= factor;
	}

	/* Remove old graphs if present */
	if (glob->fcomp->quotient)
	{
		gtk_spect_vis_data_remove (graph, 1);
		free_datavector (glob->fcomp->quotient);
		redraw = TRUE;
	}
	if (glob->fcomp->theo)
	{
		gtk_spect_vis_data_remove (graph, 2);
		glob->fcomp->theo->x = NULL;
		free_datavector (glob->fcomp->theo);
	}

	/* Calculate time domain graphs */
	glob->fcomp->quotient = fourier_gen_dataset (
			quotient, quotient->x[0], quotient->x[quotient->len-1]);
	glob->fcomp->theo = fourier_gen_dataset (
			ftheo, ftheo->x[0], ftheo->x[ftheo->len-1]);

	/* "data" graph */
	color.red = 65535;
	color.green = 0;
	color.blue = 0;
	g_return_if_fail (
		gtk_spect_vis_data_add (
			graph,
			glob->fcomp->quotient->x,
			glob->fcomp->quotient->y,
			glob->fcomp->quotient->len,
			color, 'l')
		== 1);

	/* "theory" graph */
	color.red = 0;
	color.green = 0;
	color.blue = 65535;
	g_return_if_fail (
		gtk_spect_vis_data_add (
			graph,
			glob->fcomp->theo->x,
			glob->fcomp->theo->y,
			glob->fcomp->theo->len,
			color, 'l')
		== 2);

	if (!redraw)
		gtk_spect_vis_zoom_y_all (graph);

	gtk_spect_vis_redraw (graph);
}

void fcomp_what_to_fit (gint *ia)
{
	GtkTreeIter iter;
	gint i, id;
	GtkTreeModel *model;

	for (i=0; i<glob->fcomp->numfcomp; i++)
		ia[i] = 0;

	if (!glob->fcomp->xmlfcomp)
		return;

	model = GTK_TREE_MODEL (glob->fcomp->store);

	if (gtk_tree_model_get_iter_first (model, &iter))
		do
		{
			id = 0;
			gtk_tree_model_get (model, &iter, FCOMP_ID_COL, &id, -1);
			if (id > 0)
			{
				id--;
				gtk_tree_model_get (model, &iter,
						FCOMP_FIT_AMP_COL, ia + 3*id+0,
						FCOMP_FIT_TAU_COL, ia + 3*id+1,
						FCOMP_FIT_PHI_COL, ia + 3*id+2,
						-1);
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
}
