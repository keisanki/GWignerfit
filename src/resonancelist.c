#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "structs.h"
#include "visualize.h"
#include "helpers.h"
#include "processdata.h"
#include "spectral.h"
#include "fcomp.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

/* Forward declarations */
void uncheck_res_out_of_frq_win (gdouble min, gdouble max);
GtkTreeIter* get_selected_resonance_iters (gint *numiters);

enum {
	ID_COL = 0, 
	FRQ_COL, WID_COL, AMP_COL, PHAS_COL,
	FIT_FRQ_COL, FIT_WID_COL, FIT_AMP_COL, FIT_PHAS_COL,
	N_COLUMNS
};

void clear_resonancelist ()
{
	GtkWidget *entry;

	gtk_list_store_clear (glob->store);
	glob->numres = 0;

	entry = glade_xml_get_widget (gladexml, "phaseentry");
	gtk_entry_set_text (GTK_ENTRY (entry), "");

	entry = glade_xml_get_widget (gladexml, "scaleentry");
	gtk_entry_set_text (GTK_ENTRY (entry), "");

	entry = glade_xml_get_widget (gladexml, "tauentry");
	gtk_entry_set_text (GTK_ENTRY (entry), "");

	entry = glade_xml_get_widget (gladexml, "minfrqentry");
	gtk_entry_set_text (GTK_ENTRY (entry), "");

	entry = glade_xml_get_widget (gladexml, "maxfrqentry");
	gtk_entry_set_text (GTK_ENTRY (entry), "");
}

static gboolean cell_edited_callback (GtkCellRendererText *cell, const gchar *path, gchar *text, gpointer data)
{
	guint column = GPOINTER_TO_UINT(data);
	gint id;
	gdouble value;
	GtkTreeIter iter;
	Resonance *res;

	if (sscanf(text, "%lf", &value) != 1) return FALSE;

	gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (glob->store), &iter, path);
	gtk_list_store_set (glob->store, &iter, column, value, -1);

	gtk_tree_model_get (GTK_TREE_MODEL (glob->store), &iter, ID_COL, &id, -1);
	res = g_ptr_array_index(glob->param, id-1);
	switch (column) 
	{
		case FRQ_COL:
			res->frq = value * 1e9;
			spectral_resonances_changed ();
			break;
		case WID_COL:
			res->width = value * pow(10, glob->prefs->widthunit);
			break;
		case AMP_COL:
			res->amp = value;
			break;
		case PHAS_COL:
			res->phase = value / 180*M_PI;
			break;
	}
	
	visualize_theory_graph (0);
	set_unsaved_changes ();

	return FALSE;
}

/* Update the model if a checkbox in the resonance list has been toggled */
static void cell_toggle_callback (GtkCellRendererToggle *cell, gchar *path, gpointer data)
{
	gboolean column = GPOINTER_TO_UINT(data);
	gboolean curstate;
	GtkTreeIter iter;

	gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (glob->store), &iter, path);

	gtk_tree_model_get (GTK_TREE_MODEL (glob->store), &iter, column, &curstate, -1);
	gtk_list_store_set (glob->store, &iter, column, !curstate, -1);

	if (column == FIT_FRQ_COL)
		spectral_resonances_changed ();
}

/* Check or uncheck the boxes if a column header has been clicked */
static void column_clicked_callback (GtkTreeViewColumn *column, gpointer user_data)
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	gboolean col = GPOINTER_TO_UINT(user_data);
	GtkTreeIter iter;
	gint active = 0, inactive = 0;
	gdouble frq;
	gboolean fit;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do {
			gtk_tree_model_get (model, &iter, 
					col, &fit, 
					FRQ_COL, &frq,
					-1);

			frq *= 1e9; /* GHz -> Hz */

			if ((frq > glob->gparam->min) && (frq < glob->gparam->max))
			{
				/* Count only entries within the frequency window */
				if (fit) 
					active++; 
				else 
					inactive++;
			}
		} while (gtk_tree_model_iter_next(model, &iter));
	}
	else
		return;

	if (inactive > active) 
		fit = TRUE; 
	else 
		fit = FALSE;

	gtk_tree_model_get_iter_first (model, &iter);
	
	do
		gtk_list_store_set (glob->store, &iter, col, fit, -1);
	while (gtk_tree_model_iter_next(model, &iter));

	if (fit == TRUE)
		/* Uncheck all boxes outside the frequency window */
		uncheck_res_out_of_frq_win (glob->gparam->min, glob->gparam->max);
	
	if (col == FIT_FRQ_COL)
		spectral_resonances_changed ();
}

/* Check or uncheck all resonance parameters for fitting depending on 'type' */
void resonance_check_all (gboolean type)
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	GtkTreeIter iter;

	if (glob->numres == 0) return;

	gtk_tree_model_get_iter_first (model, &iter);
	
	do
		gtk_list_store_set (glob->store, &iter,
				FIT_FRQ_COL, type,
				FIT_WID_COL, type,
				FIT_AMP_COL, type,
				FIT_PHAS_COL, type,
				-1);
	while (gtk_tree_model_iter_next(model, &iter));

	spectral_resonances_changed ();
}

/* Check or uncheck all resonances in the selected row */
void resonance_toggle_row ()
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	GtkTreeIter *iters;
	gint numchecked = 0, i, numiters;
	gboolean frq, wid, amp, phas, type;

	g_return_if_fail (GTK_IS_TREE_MODEL (model));
	iters = get_selected_resonance_iters (&numiters);

	/* Decide whether to check or to uncheck. */
	i = 0;
	while (i < numiters)
	{
		gtk_tree_model_get (model, iters+i,
				FIT_FRQ_COL, &frq,
				FIT_WID_COL, &wid,
				FIT_AMP_COL, &amp,
				FIT_PHAS_COL, &phas,
				-1);

		if (frq)  numchecked++;
		if (wid)  numchecked++;
		if (amp)  numchecked++;
		if (phas) numchecked++;
		i++;
	}
	if (numchecked > 2*numiters - 1)
		type = FALSE;
	else
		type = TRUE;

	/* Do it */
	i = 0;
	while (i < numiters)
	{
		gtk_list_store_set (glob->store, iters+i,
				FIT_FRQ_COL, type,
				FIT_WID_COL, type,
				FIT_AMP_COL, type,
				FIT_PHAS_COL, type,
				-1);

		i++;
	}

	g_free (iters);
}

/* Calculate the value for the Q column */
void calculate_q_func (GtkTreeViewColumn *col, GtkCellRenderer *renderer, 
		       GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
	gint id;
	gchar buf[20];
	Resonance *res;

	gtk_tree_model_get (model, iter, ID_COL, &id, -1);

	if ((id < 1) || (id > glob->numres)) return;
	res = g_ptr_array_index(glob->param, id-1);

	g_snprintf (buf, sizeof(buf), "%.1f", res->frq/res->width);

	g_object_set (renderer, "text", buf, NULL);
}
#define column_separator	renderer = gtk_cell_renderer_text_new (); \
				g_object_set (renderer, "background", "darkgrey", "background-set", TRUE, "width", 1, NULL); \
				gtk_tree_view_column_pack_end (column, renderer, FALSE);

/* Create a liststore and populate the treeview with columns */
void set_up_resonancelist () 
{
	GtkTreeView *treeview;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer, *togglerenderer;

	treeview = GTK_TREE_VIEW (glade_xml_get_widget (gladexml, "restreeview"));
	glob->store = gtk_list_store_new (N_COLUMNS,
			G_TYPE_UINT,
			G_TYPE_DOUBLE,
			G_TYPE_DOUBLE,
			G_TYPE_DOUBLE,
			G_TYPE_DOUBLE,
			G_TYPE_BOOLEAN,
			G_TYPE_BOOLEAN,
			G_TYPE_BOOLEAN,
			G_TYPE_BOOLEAN);

	gtk_tree_view_set_model (
			treeview,
			GTK_TREE_MODEL (glob->store));

	renderer = gtk_cell_renderer_text_new ();

	/* ID_COL */
	column = gtk_tree_view_column_new_with_attributes (
			"#", renderer,
			"text", ID_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, FALSE);
	gtk_tree_view_column_set_sort_column_id (column, ID_COL);
	gtk_tree_view_append_column (treeview, column);
	column_separator;

	/* FRQ_COL */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set(renderer, "editable", TRUE, NULL);
	g_signal_connect(renderer, "edited", (GCallback) cell_edited_callback, GUINT_TO_POINTER(FRQ_COL));

	column = gtk_tree_view_column_new_with_attributes (
			"frq [GHz]", renderer,
			"text", FRQ_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, FRQ_COL);
	gtk_tree_view_append_column (treeview, column);

	/* FIT_FRQ_COL */
	togglerenderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (togglerenderer, "toggled", G_CALLBACK (cell_toggle_callback), GUINT_TO_POINTER(FIT_FRQ_COL));
	column = gtk_tree_view_column_new_with_attributes (
			"", togglerenderer,
			"active", FIT_FRQ_COL,
			NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_clickable (column, TRUE);
	g_signal_connect (column, "clicked", G_CALLBACK (column_clicked_callback), GUINT_TO_POINTER(FIT_FRQ_COL));
	column_separator;

	/* WID_COL */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set(renderer, "editable", TRUE, NULL);
	g_signal_connect(renderer, "edited", (GCallback) cell_edited_callback, GUINT_TO_POINTER(WID_COL));

	column = gtk_tree_view_column_new_with_attributes (
			"width", renderer,
			"text", WID_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, WID_COL);
	gtk_tree_view_append_column (treeview, column);
	if (glob->prefs->widthunit == 6)
		gtk_tree_view_column_set_title (column, "width [MHz]");
	if (glob->prefs->widthunit == 3)
		gtk_tree_view_column_set_title (column, "width [kHz]");

	/* FIT_WID_COL */
	togglerenderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (togglerenderer, "toggled", G_CALLBACK (cell_toggle_callback), GUINT_TO_POINTER(FIT_WID_COL));
	column = gtk_tree_view_column_new_with_attributes (
			"", togglerenderer,
			"active", FIT_WID_COL,
			NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_clickable (column, TRUE);
	g_signal_connect (column, "clicked", G_CALLBACK (column_clicked_callback), GUINT_TO_POINTER(FIT_WID_COL));
	column_separator;

	/* AMP_COL */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set(renderer, "editable", TRUE, NULL);
	g_signal_connect(renderer, "edited", (GCallback) cell_edited_callback, GUINT_TO_POINTER(AMP_COL));

	column = gtk_tree_view_column_new_with_attributes (
			"amplitude", renderer,
			"text", AMP_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, AMP_COL);
	gtk_tree_view_append_column (treeview, column);

	/* FIT_AMP_COL */
	togglerenderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (togglerenderer, "toggled", G_CALLBACK (cell_toggle_callback), GUINT_TO_POINTER(FIT_AMP_COL));
	column = gtk_tree_view_column_new_with_attributes (
			"", togglerenderer,
			"active", FIT_AMP_COL,
			NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_clickable (column, TRUE);
	g_signal_connect (column, "clicked", G_CALLBACK (column_clicked_callback), GUINT_TO_POINTER(FIT_AMP_COL));
	column_separator;

	/* PHAS_COL */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set(renderer, "editable", TRUE, NULL);
	g_signal_connect(renderer, "edited", (GCallback) cell_edited_callback, GUINT_TO_POINTER(PHAS_COL));

	column = gtk_tree_view_column_new_with_attributes (
			"phase", renderer,
			"text", PHAS_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, PHAS_COL);
	gtk_tree_view_append_column (treeview, column);

	/* FIT_PHAS_COL */
	togglerenderer = gtk_cell_renderer_toggle_new ();
	g_signal_connect (togglerenderer, "toggled", G_CALLBACK (cell_toggle_callback), GUINT_TO_POINTER(FIT_PHAS_COL));
	column = gtk_tree_view_column_new_with_attributes (
			"", togglerenderer,
			"active", FIT_PHAS_COL,
			NULL);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_clickable (column, TRUE);
	g_signal_connect (column, "clicked", G_CALLBACK (column_clicked_callback), GUINT_TO_POINTER(FIT_PHAS_COL));
	column_separator;

	/* Q_COL */
	renderer = gtk_cell_renderer_text_new ();
	g_object_set(renderer, "editable", FALSE, NULL);

	column = gtk_tree_view_column_new_with_attributes (
			"Q", renderer,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_append_column (treeview, column);
	gtk_tree_view_column_set_cell_data_func (
			column,
			renderer,
			calculate_q_func,
			NULL, NULL);

	/* Set selection mode to multiple */
	gtk_tree_selection_set_mode (
			gtk_tree_view_get_selection (treeview),
			GTK_SELECTION_MULTIPLE);
}

#undef column_separator

void add_resonance_to_list (Resonance *res)
{
	GtkTreeModel *model;
	gint curid, id;
	GtkTreeIter iter, sibling;
	gboolean found = FALSE;
	
	g_return_if_fail (glob->param);
	g_return_if_fail (glob->store);
	model = GTK_TREE_MODEL (glob->store);
	
	/* Add res to glob->param and sort if necessary */
	g_ptr_array_add (glob->param, res);
	if (glob->prefs->sortparam)
	{
		g_ptr_array_sort (glob->param, param_compare);

		/* Retrieve the position of *res */
		id = 0;
		while ((Resonance *) g_ptr_array_index (glob->param, id) != res)
			id++;
		id++;
	}
	else
		id = glob->param->len;

	glob->numres++;

	if (id < glob->numres)
	{
		/* id is set -> the new resonance should have this ID.
		 * Increase the ID the of all other resonance above the
		 * new one by one. */
		if (gtk_tree_model_get_iter_first (model, &sibling))
			do 
			{
				gtk_tree_model_get (model, &sibling, ID_COL, &curid, -1);
				if (curid >= id)
				{
					gtk_list_store_set (glob->store, &sibling, ID_COL, ++curid, -1);
					if (!found)
					{
						gtk_list_store_insert_before (glob->store, &iter, &sibling);
						found = TRUE;
					}
				}
			}
			while (gtk_tree_model_iter_next (model, &sibling));
	}

	if (!found)
		gtk_list_store_append (glob->store, &iter);

	gtk_list_store_set (glob->store, &iter, 
			ID_COL, id, 
			FRQ_COL, res->frq / 1e9,
			WID_COL, res->width / pow (10, glob->prefs->widthunit),
			AMP_COL, res->amp,
			PHAS_COL, NormalisePhase (res->phase) / M_PI*180,
			FIT_FRQ_COL, 1,
			FIT_WID_COL, 1,
			FIT_AMP_COL, 1,
			FIT_PHAS_COL, 1,
			-1);
}

void show_global_parameters (GlobalParam *gparam)
{
	GtkWidget *entry;
	gchar text[20];

	entry = glade_xml_get_widget (gladexml, "phaseentry");
	snprintf(text, 20, "%f", NormalisePhase(gparam->phase) / M_PI*180);
	gtk_entry_set_text (GTK_ENTRY (entry), text);

	entry = glade_xml_get_widget (gladexml, "scaleentry");
	snprintf(text, 20, "%f", gparam->scale);
	gtk_entry_set_text (GTK_ENTRY (entry), text);

	entry = glade_xml_get_widget (gladexml, "tauentry");
	snprintf(text, 20, "%f", gparam->tau / 1e-9);
	gtk_entry_set_text (GTK_ENTRY (entry), text);

	entry = glade_xml_get_widget (gladexml, "minfrqentry");
	snprintf(text, 20, "%f", gparam->min / 1e9);
	gtk_entry_set_text (GTK_ENTRY (entry), text);

	entry = glade_xml_get_widget (gladexml, "maxfrqentry");
	snprintf(text, 20, "%f", gparam->max / 1e9);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
}

void update_resonance_list (GPtrArray *param)
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	GtkTreeIter iter;
	Resonance *res;
	gint id, i = 0;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter, ID_COL, &id, -1);
			if (id < 1)
			{
				dialog_message ("Error: Could not determine resonance ID, update_resonance_list canceled. This error should NOT happen!");
				return;
			}
			res = g_ptr_array_index(param, id-1);
			gtk_list_store_set (glob->store, &iter, 
					FRQ_COL, res->frq / 1e9,
					WID_COL, res->width / pow(10, glob->prefs->widthunit),
					AMP_COL, res->amp,
					PHAS_COL, NormalisePhase(res->phase) / M_PI*180,
					-1);
			i++;
		}
		while (gtk_tree_model_iter_next(model, &iter));

		spectral_resonances_changed ();
	}
}

/* Return a zero terminated array of all selected resonance IDs */
gint* get_selected_resonance_ids (gboolean failok)
{
	GtkTreeSelection *selection;
	GtkTreeModel     *model;
	GtkTreeIter       iter;
	GList            *sellist, *listiter;
	gint             *id = NULL, i = 0;

	selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (
				glade_xml_get_widget (gladexml, "restreeview")
			)
		);

	model   = GTK_TREE_MODEL (glob->store);
	sellist = gtk_tree_selection_get_selected_rows (selection, NULL);
	id      = g_new0 (gint, g_list_length (sellist)+1);

	if (g_list_length (sellist))
	{
		listiter = sellist;
		while (listiter)
		{
			gtk_tree_model_get_iter (model, &iter, listiter->data);
			gtk_tree_model_get (model, &iter, ID_COL, id+i, -1);

			listiter = g_list_next (listiter);
			i++;
		}
	}
	else
	{
		if (!failok) 
			dialog_message ("No row selected.");
	}

	g_list_foreach (sellist, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (sellist);

	return id;
}

/* Returns an array of all selected resonance iters. The number of elements are
 * returned via numiters. */
GtkTreeIter* get_selected_resonance_iters (gint *numiters)
{
	GtkTreeSelection *selection;
	GtkTreeIter      *iters;
	GtkTreeModel     *model;
	GList            *sellist, *listiter;
	gint              i=0;

	selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (
				glade_xml_get_widget (gladexml, "restreeview")
			)
		);

	model   = GTK_TREE_MODEL (glob->store);
	sellist = gtk_tree_selection_get_selected_rows (selection, NULL);
	iters   = g_new0 (GtkTreeIter, g_list_length (sellist)+1);

	*numiters = g_list_length (sellist);
	if (*numiters)
	{
		listiter = sellist;
		while (listiter)
		{
			gtk_tree_model_get_iter (model, iters + i, listiter->data);

			listiter = g_list_next (listiter);
			i++;
		}
	}

	g_list_foreach (sellist, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (sellist);

	return iters;
}

gint get_resonance_id_by_cursur ()
{
	GtkTreeView  *treeview;
	GtkTreeModel *model;
	GtkTreePath  *path;
	GtkTreeIter   iter;
	gint          id = 0;

	treeview = GTK_TREE_VIEW (glade_xml_get_widget (gladexml, "restreeview"));
	gtk_tree_view_get_cursor (treeview, &path, NULL);
	if (!path)
		return 0;
	
	model = GTK_TREE_MODEL (glob->store);
	gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_model_get (model, &iter, ID_COL, &id, -1);
	gtk_tree_path_free (path);

	return id;
}

void remove_resonance (GtkTreeIter iter)
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	gint id = -1, curid;

	gtk_tree_model_get (model, &iter, ID_COL, &id, -1);

	if (id > 0)
	{
		gtk_list_store_remove (glob->store, &iter);

		g_ptr_array_remove_index (glob->param, id-1);
		glob->numres--;
		statusbar_message ("Removed resonance #%d", id);

		if (gtk_tree_model_get_iter_first (model, &iter))
		{
			gtk_tree_model_get (model, &iter, ID_COL, &curid, -1);
			if (curid > id)
			{
				curid--;
				gtk_list_store_set (glob->store, &iter, ID_COL, curid, -1);
			}
			while (gtk_tree_model_iter_next(model, &iter))
			{
				gtk_tree_model_get (model, &iter, ID_COL, &curid, -1);
				if (curid > id)
				{
					curid--;
					gtk_list_store_set (glob->store, &iter, ID_COL, curid, -1);
				}
			}
		}

		spectral_resonances_changed ();
	}
	else
	{
		dialog_message ("Error: Could not determine resonance ID.");
	}
}

void remove_selected_resonance ()
{
	GtkTreeIter *iters;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GList *pathlist;
	gint numiters, i=0;

	selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW (
			glade_xml_get_widget (gladexml, "restreeview")));

	if (gtk_tree_selection_count_selected_rows (selection) == 0)
	{
		dialog_message ("No row selected.");
		return;
	}
	
	/* Get path of selected resonances */
	model     = GTK_TREE_MODEL (glob->store);
	pathlist  = gtk_tree_selection_get_selected_rows (selection, &model);

	iters = get_selected_resonance_iters (&numiters);
	while (i < numiters)
	{
		remove_resonance (iters[i]);
		i++;
	}
	g_free (iters);

	/* Select the old path -> the next resonance in the list */
	gtk_tree_selection_select_path (selection, (GtkTreePath *) pathlist->data);

	/* Tidy up */
	g_list_foreach (pathlist, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (pathlist);
	
	visualize_update_res_bar (0);
	visualize_theory_graph ();
	set_unsaved_changes ();
}

GtkTreeIter *get_res_iter_by_id (gint wanted_id)
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	GtkTreeIter *iter;
	gint id;

	iter = g_new (GtkTreeIter, 1);

	if (gtk_tree_model_get_iter_first (model, iter))
	{
		do {
			gtk_tree_model_get (model, iter, 
					ID_COL, &id, 
					-1);
		} while ((wanted_id != id) &&
			(gtk_tree_model_iter_next(model, iter)));
	}
	else
		return NULL;

	if (wanted_id == id)
		return iter;
	else
		return NULL;
}

/* Select the resonance with the given id and center the view around it */
gboolean select_res_by_id (gint id)
{
	GtkTreeSelection *selection;
	GtkTreeViewColumn *column;
	GtkTreeView *treeview;
	GtkTreePath *path;
	GtkTreeIter *iter;

	iter = get_res_iter_by_id (id);

	if (!iter)
		return FALSE;

	treeview = GTK_TREE_VIEW (glade_xml_get_widget (gladexml, "restreeview"));
	selection = gtk_tree_view_get_selection (treeview);

	if (!selection)
		return FALSE;
	
	/* Select row with resonance */
	gtk_tree_selection_unselect_all (selection);
	gtk_tree_selection_select_iter (selection, iter);

	/* Scroll to row in resonance list */
	column = gtk_tree_view_get_column (treeview, ID_COL);
	path = gtk_tree_model_get_path (GTK_TREE_MODEL (glob->store), iter);
	gtk_tree_view_scroll_to_cell (treeview, path, column, TRUE, 0.5, 0);

	visualize_update_res_bar (TRUE);

	g_free (iter);
	return TRUE;
}

void uncheck_res_out_of_frq_win (gdouble min, gdouble max)
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	GtkTreeIter iter;
	gdouble frq;
	gint id;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		gtk_tree_model_get (model, &iter, ID_COL, &id, -1);
		id--;

		frq = ((Resonance *) g_ptr_array_index(glob->param, id))->frq;
		if ((frq <= min) || (frq >= max))
			gtk_list_store_set (glob->store, &iter, 
					FIT_FRQ_COL, FALSE,
					FIT_WID_COL, FALSE,
					FIT_AMP_COL, FALSE,
					FIT_PHAS_COL, FALSE,
					-1);
	
		while (gtk_tree_model_iter_next(model, &iter))
		{
			gtk_tree_model_get (model, &iter, ID_COL, &id, -1);
			id--;

			frq = ((Resonance *) g_ptr_array_index(glob->param, id))->frq;
			if ((frq <= min) || (frq >= max))
				gtk_list_store_set (glob->store, &iter, 
						FIT_FRQ_COL, FALSE,
						FIT_WID_COL, FALSE,
						FIT_AMP_COL, FALSE,
						FIT_PHAS_COL, FALSE,
						-1);
		}
	}
}

/* Takes all checkboxes and build up the freeparam array for the mrq
 * algorithm. The memory for the
 * 4*glob->numres+NUM_GLOB_PARAM+1+3*glob->fcomp->numfcomp 
 * array needs to be allocated before calling what_to_fit (). */
void what_to_fit (gint *ia)
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	GtkTreeIter iter;
	gboolean fit;
	gint i, id;

	for (i=0; i<TOTALNUMPARAM+1; i++)
		ia[i] = 0;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		gtk_tree_model_get (model, &iter, ID_COL, &id, -1);
		id--;
	
		gtk_tree_model_get (model, &iter, FIT_AMP_COL, &fit, -1);
		if (fit) ia[4*id+1] = 1;

		gtk_tree_model_get (model, &iter, FIT_PHAS_COL, &fit, -1);
		if (fit) ia[4*id+2] = 1;

		gtk_tree_model_get (model, &iter, FIT_FRQ_COL, &fit, -1);
		if (fit) ia[4*id+3] = 1;

		gtk_tree_model_get (model, &iter, FIT_WID_COL, &fit, -1);
		if (fit) ia[4*id+4] = 1;

		while (gtk_tree_model_iter_next (model, &iter))
		{
			gtk_tree_model_get (model, &iter, ID_COL, &id, -1);
			id--;
		
			gtk_tree_model_get (model, &iter, FIT_AMP_COL, &fit, -1);
			if (fit) ia[4*id+1] = 1;

			gtk_tree_model_get (model, &iter, FIT_PHAS_COL, &fit, -1);
			if (fit) ia[4*id+2] = 1;

			gtk_tree_model_get (model, &iter, FIT_FRQ_COL, &fit, -1);
			if (fit) ia[4*id+3] = 1;

			gtk_tree_model_get (model, &iter, FIT_WID_COL, &fit, -1);
			if (fit) ia[4*id+4] = 1;
		}
	}
	
	if (gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (
				gladexml, "phase_check"))
		)) ia[4*glob->numres+3*glob->fcomp->numfcomp+1] = 1;
	
	if (gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (
				gladexml, "scale_check"))
		)) ia[4*glob->numres+3*glob->fcomp->numfcomp+2] = glob->IsReflection ? 1 : 0;
	
	if (gtk_toggle_button_get_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (
				gladexml, "tau_check"))
		)) ia[4*glob->numres+3*glob->fcomp->numfcomp+3] = glob->IsReflection ? 0 : 1;

	//if (glob->IsReflection) ia[4*glob->numres+3*glob->fcomp->numfcomp+3] = 0;

	/* Fit FourierComponents */
	fcomp_what_to_fit (ia + (4*glob->numres+1));

	ia[0] = 0;
	for (i = 1; i<TOTALNUMPARAM+1; i++)
		if (ia[i]) ia[0]++;
}

/* Update the width column of the resonance list to reflect the current
 * setting of glob->prefs->widthunit.
 */
void reslist_update_widthunit ()
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	GPtrArray *param = glob->param;
	GtkTreeViewColumn *column;
	GtkTreeIter iter;
	Resonance *res;
	gint id;

	column = gtk_tree_view_get_column (
			GTK_TREE_VIEW (glade_xml_get_widget (gladexml, "restreeview")),
			WID_COL + 1);

	g_return_if_fail (column != NULL);
	g_return_if_fail (glob->store != NULL);

	if (glob->prefs->widthunit == 6)
		gtk_tree_view_column_set_title (column, "width [MHz]");
	else if (glob->prefs->widthunit == 3)
		gtk_tree_view_column_set_title (column, "width [kHz]");
	else
		gtk_tree_view_column_set_title (column, "width");

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		gtk_tree_model_get (model, &iter, ID_COL, &id, -1);
		if (id < 1)
		{
			dialog_message ("Error: Could not determine resonance ID, reslist_update_widthunit canceled. This error should NOT happen!");
			return;
		}
		res = g_ptr_array_index(param, id-1);
		gtk_list_store_set (glob->store, &iter, 
				WID_COL, res->width / pow(10, glob->prefs->widthunit),
				-1);
		while (gtk_tree_model_iter_next(model, &iter))
		{
			gtk_tree_model_get (model, &iter, ID_COL, &id, -1);
			if (id < 1)
			{
				dialog_message ("Error: Could not determine resonance ID, reslist_update_widthunit canceled. This error should NOT happen!");
				return;
			}
			res = g_ptr_array_index(param, id-1);
			gtk_list_store_set (glob->store, &iter, 
					WID_COL, res->width / pow(10, glob->prefs->widthunit),
					-1);
		}
	}
}

gboolean import_resonance_list (gchar *filename)
{
	Resonance *res = NULL;
	gchar dataline[100];
	FILE *datafile;
	gdouble col1, col2, oldcol1, frq, is_in_ghz = 1.0;
	gboolean numbercol = FALSE;
	gint numres, i;

	if (!filename)
		return FALSE;

	datafile = fopen (filename, "r");
	if (datafile == NULL) {
		dialog_message ("Error: Could not open file %s.", filename);
		return FALSE;
	}

	numres = 0;
	oldcol1 = -1.0;
	col2 = 0.0;
	while (!feof (datafile)) {
		if (!(fgets (dataline, 99, datafile)))
			continue;

		if ((dataline[0] == '#') || (dataline[0] == '\n') || (dataline[0] == '\r'))
			continue;

		if (sscanf (dataline, "%lf %lf", &col1, &col2) == 0) 
		{
			/* A line without data and not a comment */
			fclose (datafile);
			dialog_message ("Error: Could not parse dataset #%d.", numres+1);
			return FALSE;
		} else {
			numres++;

			if ((oldcol1 >= 0) && (col2))
			{
				if ((col1 == oldcol1 + 1) || (col1 == oldcol1))
					/* First column contains number of resonance */
					numbercol = TRUE;
				else if (numbercol)
				{
					/* What is this first column? */
					fclose (datafile);
					dialog_message ("Error: Could not determine format of 1st column.");
					return FALSE;
				}
			}
			oldcol1 = col1;

			if ((((numbercol) && (col2 < 200)) || ((!numbercol) && (col1 < 200))) && (numres > 1))
				is_in_ghz = 1e9;
		}
	}

	rewind (datafile);
	i = 0;
	while (!feof (datafile)) {
		if (!(fgets (dataline, 99, datafile)))
			continue;

		if ((dataline[0] == '#') || (dataline[0] == '\n') || (dataline[0] == '\r'))
			continue;

		if (i == numres)
			continue;

		sscanf (dataline, "%lf %lf", &col1, &col2);

		if (numbercol)
			frq = col2 * is_in_ghz;
		else
			frq = col1 * is_in_ghz;

		res = g_new0 (Resonance, 1);
		res->frq   = frq;
		res->width = 0.3e6;
		res->amp   = 1e4;
		add_resonance_to_list (res);

		i++;
	}

	fclose (datafile);
	statusbar_message ("Imported %i resonance frequencies", numres);
	spectral_resonances_changed ();
	return TRUE;
}
