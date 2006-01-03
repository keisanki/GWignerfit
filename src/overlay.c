#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "structs.h"
#include "helpers.h"
#include "processdata.h"
#include "callbacks.h"
#include "gtkspectvis.h"
#include "fourier.h"
#include "visualize.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

static void overlay_selection_changed (GtkTreeSelection *treeselection, GtkTreeModel *model);

/* Columns in overlaytreeview */
enum {
	OVERLAY_UID,
	OVERLAY_FILE_COL,
	OVERLAY_COLOR_R,
	OVERLAY_COLOR_G,
	OVERLAY_COLOR_B,
	N_COLUMNS
};

/* Render a pixmap with the overlay color for the treeview */
void overlay_color_column (GtkTreeViewColumn *col, GtkCellRenderer *renderer, 
			   GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
	GdkPixbuf *pixbuf = NULL;
	GdkPixmap *pixmap = NULL;
	GtkWidget *widget;
	GdkColor color;
	GdkGC *gc;

	widget = glade_xml_get_widget (gladexml, "mainwindow");

	gc = gdk_gc_new (widget->window);
	gtk_tree_model_get (model, iter, 
			OVERLAY_COLOR_R, &color.red, 
			OVERLAY_COLOR_G, &color.green, 
			OVERLAY_COLOR_B, &color.blue, 
			-1);
	gdk_gc_set_rgb_fg_color (gc, &color);

	pixmap = gdk_pixmap_new (widget->window, 41, 9, -1);
	gdk_draw_rectangle (pixmap, gc, TRUE, 0, 0, 40, 8);
	gdk_draw_line (pixmap, widget->style->black_gc,  0, 0, 40, 0);
	gdk_draw_line (pixmap, widget->style->black_gc, 40, 0, 40, 8);
	gdk_draw_line (pixmap, widget->style->black_gc, 40, 8,  0, 8);
	gdk_draw_line (pixmap, widget->style->black_gc,  0, 8,  0, 0);

	pixbuf = gdk_pixbuf_get_from_drawable (NULL, pixmap, NULL, 0, 0, 0, 0, 41, 9);

	g_object_set (renderer, "pixbuf", pixbuf, NULL);

	g_object_unref (pixbuf);
	g_object_unref (pixmap);
	g_object_unref (gc);
}

/* Return the GtkTreeIter for a given graph uid */
static gboolean overlay_get_iter_by_uid (guint uid, GtkTreeIter *iter)
{
	GtkTreeModel *model;
	guint id;

	g_return_val_if_fail (uid > 0, FALSE);
	g_return_val_if_fail (iter, FALSE);

	model = GTK_TREE_MODEL (glob->overlaystore);

	if (gtk_tree_model_get_iter_first (model, iter))
		do
		{
			gtk_tree_model_get (model, iter, OVERLAY_UID, &id, -1);
			if (id == uid)
				return TRUE;
		}
		while (gtk_tree_model_iter_next (model, iter));

	return FALSE;
}

/* Get the color of an overlay with the given uid _or_ iter */
gboolean overlay_get_color (GdkColor *color, gboolean selected, guint uid, GtkTreeIter *iter)
{
	GtkTreeIter getiter;
	
	g_return_val_if_fail (uid || iter, FALSE);
	g_return_val_if_fail (color, FALSE);

	if (uid)
		/* uid given -> get iter */
		g_return_val_if_fail (overlay_get_iter_by_uid (uid, &getiter), FALSE);
	else
		/* iter given -> use it */
		getiter = *iter;

	gtk_tree_model_get (GTK_TREE_MODEL (glob->overlaystore), &getiter, 
			OVERLAY_COLOR_R, &color->red,
			OVERLAY_COLOR_G, &color->green,
			OVERLAY_COLOR_B, &color->blue,
			-1);

	if (selected)
	{
		color->red /= 2;
		color->green /= 2;
		color->blue /= 2;
	}

	return TRUE;
}

/* Set the color of the overlay with the given uid */
gboolean overlay_set_color (guint uid, GdkColor color)
{
	GtkSpectVis *graph = GTK_SPECTVIS (glade_xml_get_widget (gladexml, "graph"));
	GtkTreeIter iter;

	g_return_val_if_fail (uid, FALSE);
	g_return_val_if_fail (overlay_get_iter_by_uid (uid, &iter), FALSE);

	gtk_list_store_set (glob->overlaystore, &iter,
			OVERLAY_COLOR_R, color.red,
			OVERLAY_COLOR_G, color.green,
			OVERLAY_COLOR_B, color.blue,
			-1);

	gtk_spect_vis_set_data_color (graph, uid, color);

	return TRUE;
}

/* Change the color of a overlay */
gboolean overlay_color_change (GtkWidget *widget, GdkEvent *event)
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->overlaystore);
	GtkColorSelection *colorsel;
	GList *listselection, *listiter;
	GtkTreeIter iter;
	GtkWidget *dialog;
	GdkColor color;
	gint result, num;

	if ((event->type == GDK_2BUTTON_PRESS) &&
	    (((GdkEventButton *) event)->button == 1))
	{
		listselection = gtk_tree_selection_get_selected_rows (
					gtk_tree_view_get_selection (glob->overlaytreeview),
					NULL);

		num = g_list_length (listselection);

		if (num == 0)
		{
			dialog_message ("Please select an overlayed dataset first.");
			return FALSE;
		}

		gtk_tree_model_get_iter (model, &iter, listselection->data);
		overlay_get_color (&color, FALSE, 0, &iter);

		dialog = gtk_color_selection_dialog_new ("Select color for overlay...");
		
		colorsel = GTK_COLOR_SELECTION (GTK_COLOR_SELECTION_DIALOG (dialog)->colorsel);
		gtk_color_selection_set_current_color (colorsel, &color);
		
		result = gtk_dialog_run (GTK_DIALOG (dialog));
		if (result == GTK_RESPONSE_OK)
		{
			gtk_color_selection_get_current_color (colorsel, &color);

			listiter = listselection;
			do
			{
				gtk_tree_model_get_iter (model, &iter, listiter->data);

				gtk_list_store_set (glob->overlaystore, &iter,
						OVERLAY_COLOR_R, color.red,
						OVERLAY_COLOR_G, color.green,
						OVERLAY_COLOR_B, color.blue,
						-1);
			}
			while ((listiter = g_list_next (listiter)));

			overlay_selection_changed (
					gtk_tree_view_get_selection (glob->overlaytreeview), 
					model);
		}

		g_list_foreach (listselection, (GFunc) gtk_tree_path_free, NULL);
		g_list_free (listselection);
		gtk_widget_destroy (dialog);
	}

	return FALSE;
}

/* Display the overlaywin and prepare the liststore if necessary. */
void overlay_show_window ()
{
	GladeXML *xmloverlay;
	GtkWidget *treeview;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	if (glob->overlaytreeview != NULL) return;

	xmloverlay = glade_xml_new (GLADEFILE, "overlaywin", NULL);
	glade_xml_signal_autoconnect (xmloverlay);

	treeview = glade_xml_get_widget (xmloverlay, "overlaytreeview");
	glob->overlaytreeview = GTK_TREE_VIEW (treeview);

	if (!glob->overlaystore)
		glob->overlaystore = gtk_list_store_new (N_COLUMNS,
				G_TYPE_UINT,
				G_TYPE_STRING,
				G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

	g_signal_connect(
			G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview))),
			"changed",
			(GCallback) overlay_selection_changed, GTK_TREE_MODEL (glob->overlaystore));

	gtk_tree_view_set_model (
			GTK_TREE_VIEW (treeview),
			GTK_TREE_MODEL (glob->overlaystore));

	gtk_tree_selection_set_mode (
			gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview)),
			GTK_SELECTION_MULTIPLE);

	renderer = gtk_cell_renderer_pixbuf_new ();
	column = gtk_tree_view_column_new_with_attributes (
			"Color", renderer,
			NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	gtk_tree_view_column_set_cell_data_func (
			column,
			renderer,
			overlay_color_column,
			NULL, NULL);
	gtk_tree_view_column_set_min_width (
			column,
			56);

	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
			"Filename", renderer,
			"text", OVERLAY_FILE_COL,
			NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
}

/* Close the overlaywin, keeping the liststore. */
gboolean overlay_close_window (GtkWidget *button)
{
	/* Unmark all graphs */
	gtk_tree_selection_unselect_all (
			gtk_tree_view_get_selection (glob->overlaytreeview)
			);

	gtk_widget_destroy (gtk_widget_get_toplevel (button));

	glob->overlaytreeview = NULL;

	return TRUE;
}

/* Adds a filename with a given uid to the list. */
static void overlay_add_to_list (gchar *filename, guint uid, GdkColor color)
{
	GtkTreeIter iter;
	gchar *basename;

	g_return_if_fail (filename != NULL);
	g_return_if_fail (uid > 0);

	if (!glob->overlaystore)
		glob->overlaystore = gtk_list_store_new (N_COLUMNS,
				G_TYPE_UINT,
				G_TYPE_STRING,
				G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

	gtk_list_store_append (glob->overlaystore, &iter);

	basename = g_path_get_basename (filename);
	gtk_list_store_set (glob->overlaystore, &iter,
			OVERLAY_UID, uid,
			OVERLAY_FILE_COL, basename,
			OVERLAY_COLOR_R, color.red,
			OVERLAY_COLOR_G, color.green,
			OVERLAY_COLOR_B, color.blue,
			-1);
	g_free (basename);
}

/* Remove entry with given uid from the list. */
static gboolean overlay_remove_from_list (guint uid)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint id;

	g_return_val_if_fail (uid > 0, FALSE);

	model = GTK_TREE_MODEL (glob->overlaystore);

	if (gtk_tree_model_get_iter_first (model, &iter))
		do
		{
			gtk_tree_model_get (model, &iter, OVERLAY_UID, &id, -1);
			if (id == uid)
			{
				gtk_list_store_remove (glob->overlaystore, &iter);
				return TRUE;
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));

	return FALSE;
}

/* Takes a DataVector, adds it to the store and updates the graph */
gboolean overlay_add_data (DataVector *overlaydata)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GdkColor color;

	g_return_val_if_fail (glob->data, FALSE);
	g_return_val_if_fail (overlaydata, FALSE);

	if (glob->overlayspectra == NULL)
		glob->overlayspectra = g_ptr_array_new ();

	color.red   = 45000;
	color.green = 45000;
	color.blue  = 45000;

	overlaydata->index = gtk_spect_vis_data_add (
			GTK_SPECTVIS (graph),
			overlaydata->x,
			overlaydata->y,
			overlaydata->len,
			color, 'f');

	if (glob->prefs->datapoint_marks)
		gtk_spect_vis_set_graphtype (GTK_SPECTVIS (graph), overlaydata->index, 'm');

	if (overlaydata->index > 0)
	{
		overlay_add_to_list (overlaydata->file, overlaydata->index, color);

		/* add data */
		g_ptr_array_add (glob->overlayspectra, overlaydata);

		/* add graph to fourier window */
		fourier_update_overlay_graphs (overlaydata->index, TRUE);

		gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
	}
	else
		return FALSE;
	
	return TRUE;
}

/* Takes a filename, checks if it is a valid datafile with valid frequency
 * entries, adds it to the store and updates the graph.
 */
gboolean overlay_file (gchar *filename)
{
	DataVector *overlaydata;

	g_return_val_if_fail (glob->data, FALSE);
	g_return_val_if_fail (filename, FALSE);

	overlaydata = import_datafile (filename, FALSE);

	if (!overlaydata)
		return FALSE;

	if (!overlay_add_data (overlaydata))
	{
		dialog_message ("Error: Could not add file '%s' to graph.", filename);
		free_datavector (overlaydata);
	}

	/* This is needed, as filename has not been aquired by get_filename() */
	g_free (glob->path);
	glob->path = g_path_get_dirname (filename);
	
	return TRUE;
}
#if 0
/* Takes the selected files in the given file_selection and calls overlay_file
 * for each filename.
 */
static gboolean overlay_add_files (GtkWidget *widget, gpointer user_data) 
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	gchar **selections;
	guint i = 0;

	g_return_val_if_fail (glob->data, FALSE);

	selections = gtk_file_selection_get_selections (GTK_FILE_SELECTION (user_data));

	if (glob->overlayspectra == NULL)
		glob->overlayspectra = g_ptr_array_new ();

	while (selections[i] != NULL)
		overlay_file (selections[i++]);

	g_strfreev (selections);

	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
	
	return FALSE;
}
#endif

/* Returns the DataVector for the given uid */
DataVector *overlay_get_data_by_uid (guint uid)
{
	guint pos = 0;
	DataVector *vec;

	g_return_val_if_fail (glob->overlayspectra, NULL);
	g_return_val_if_fail (uid, NULL);
	
	while ((pos < glob->overlayspectra->len) &&
	       (((DataVector *) g_ptr_array_index (glob->overlayspectra, pos))->index != uid))
		pos++;

	g_return_val_if_fail (pos != glob->overlayspectra->len, NULL);

	vec = (DataVector *) g_ptr_array_index (glob->overlayspectra, pos);

	return vec;
}

/* Removes the files selected in the overlaytreeview from the store
 * and updates the graph accordingly.
 */
void overlay_remove_files (GtkWidget *widget, gpointer user_data)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GList *listselection, *listelement;
	GtkTreeIter iter;
	DataVector *data;
	guint num, *uids = NULL;

	listselection = gtk_tree_selection_get_selected_rows (
				gtk_tree_view_get_selection (glob->overlaytreeview),
				NULL);

	num = g_list_length (listselection);

	if (num > 0)
	{
		uids = g_new0 (guint, num);

		/* get uids of selected graphs */
		num = 0;
		listelement = listselection;
		while (listelement != NULL)
		{
			gtk_tree_model_get_iter (
					GTK_TREE_MODEL (glob->overlaystore),
					&iter,
					listelement->data);
			
			gtk_tree_model_get (GTK_TREE_MODEL (glob->overlaystore),
					&iter, OVERLAY_UID, &uids[num], -1);

			num++;
			listelement = g_list_next (listelement);
		}

		/* remove selected uids */
		while (num > 0)
		{
			num--;

			data = overlay_get_data_by_uid (uids[num]);

			if (data)
			{
				/* remove graph from fourier window */
				fourier_update_overlay_graphs (-uids[num], TRUE);

				/* remove entry from treestore */
				overlay_remove_from_list (uids[num]);

				gtk_spect_vis_data_remove (GTK_SPECTVIS (graph), uids[num]);
				free_datavector (data);

				g_ptr_array_remove (glob->overlayspectra, data);
			}
		}

		g_free (uids);
	}

	g_list_foreach (listselection, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (listselection);

	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
}

/* Open a file_selection dialog to query the files to be added. */
void overlay_file_selection ()
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GtkWidget *filew;
	gchar *defaultname;
	GSList *filenames;

	filew = gtk_file_chooser_dialog_new ("Select datafiles",
			NULL,
			GTK_FILE_CHOOSER_ACTION_OPEN,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
			NULL);

	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (filew), TRUE);
	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (filew), TRUE);

	defaultname = get_defaultname (NULL);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (filew), defaultname);
	g_free (defaultname);

	if (gtk_dialog_run (GTK_DIALOG (filew)) == GTK_RESPONSE_ACCEPT)
	{
		filenames = gtk_file_chooser_get_filenames (GTK_FILE_CHOOSER (filew));

		if (glob->overlayspectra == NULL)
			glob->overlayspectra = g_ptr_array_new ();

		g_slist_foreach (filenames, (GFunc) overlay_file, NULL);

		g_slist_foreach (filenames, (GFunc) g_free, NULL);
		g_slist_free (filenames);
		
		gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
	}

	gtk_widget_destroy (filew);

#if 0
	filew = gtk_file_selection_new ("Select datafile");
	gtk_file_selection_set_select_multiple (GTK_FILE_SELECTION (filew), TRUE);

	filename = get_defaultname (NULL);
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (filew), filename);
	g_free (filename);

	g_signal_connect (
			G_OBJECT (GTK_FILE_SELECTION (filew)->ok_button), 
			"clicked", 
			G_CALLBACK (overlay_add_files), 
			(gpointer) filew);

	g_signal_connect_swapped (
			G_OBJECT (GTK_FILE_SELECTION (filew)->ok_button), 
			"clicked", 
			G_CALLBACK (on_close_dialog), 
			G_OBJECT (filew));
	
	g_signal_connect_swapped (
			G_OBJECT (GTK_FILE_SELECTION (filew)->cancel_button), 
			"clicked", 
			G_CALLBACK (on_close_dialog), 
			G_OBJECT (filew));

	g_signal_connect (
			G_OBJECT (filew), 
			"destroy", 
			G_CALLBACK (on_close_dialog), 
			NULL);

	gtk_widget_show (filew);
#endif
}

/* Removes all overlayed data from the graph and clears the store. */
void overlay_remove_all ()
{
	GtkSpectVis *graph = GTK_SPECTVIS (glade_xml_get_widget (gladexml, "graph"));
	GtkTreeIter iter;
	DataVector *data;
	GtkTreeModel *model;
	guint uid;

	if (glob->overlaystore == NULL) return;

	model = GTK_TREE_MODEL (glob->overlaystore);

	if (gtk_tree_model_get_iter_first (model, &iter))
		do
		{
			gtk_tree_model_get (model, &iter, OVERLAY_UID, &uid, -1);
			if (uid > 0)
			{
				data = overlay_get_data_by_uid (uid);
				gtk_spect_vis_data_remove (graph, uid);
				free_datavector (data);
				g_ptr_array_remove (glob->overlayspectra, data);

				/* remove graph from fourier window */
				fourier_update_overlay_graphs (-uid, TRUE);
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));

	gtk_list_store_clear (glob->overlaystore);

	gtk_spect_vis_redraw (graph);
}

/* Mark the currently selected datasets in the graph */
static void overlay_selection_changed (GtkTreeSelection *treeselection, GtkTreeModel *model)
{
	GtkSpectVis *graph = GTK_SPECTVIS (glade_xml_get_widget (gladexml, "graph"));
	GtkTreeIter iter;
	guint uid;
	GdkColor color;

	g_return_if_fail (model != NULL);

	if (gtk_tree_model_get_iter_first (model, &iter))
		do
		{
			gtk_tree_model_get (model, &iter, OVERLAY_UID, &uid, -1);
			
			if (uid > 0)
			{
				overlay_get_color (&color, 
						gtk_tree_selection_iter_is_selected (treeselection, &iter), 
						uid, NULL);

				gtk_spect_vis_set_data_color (graph, uid, color);
				fourier_set_color (uid, color);
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));

	gtk_spect_vis_redraw (graph);
}

/* Exchanges the currently active datafile with the selected one. */
void overlay_swap_files (GtkWidget *widget, gpointer user_data)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GList *listselection;
	DataVector *overlaydata = NULL;
	ComplexDouble *tmpdvec;
	GtkTreeIter iter;
	guint num, uid = 0, tmplen;
	gchar *basename, *tmpname;
	gdouble *tmpdouble;
	GdkColor color;

	listselection = gtk_tree_selection_get_selected_rows (
				gtk_tree_view_get_selection (glob->overlaytreeview),
				NULL);

	num = g_list_length (listselection);

	if (num == 0)
	{
		dialog_message ("Please select an overlayed dataset first.");
		return;
	}

	if (num > 1)
	{
		dialog_message ("You may only select one dataset for this operation.");
		return;
	}

	/* OK, if we're here, only one element is selected and listselection
	 * holds the position, let's get the iter. */

	gtk_tree_model_get_iter (
			GTK_TREE_MODEL (glob->overlaystore),
			&iter,
			listselection->data);

	gtk_tree_model_get (GTK_TREE_MODEL (glob->overlaystore),
			&iter, 
			OVERLAY_UID, &uid,
			-1);
	g_return_if_fail (uid);

	/* Get the selected dataset */
	overlaydata = overlay_get_data_by_uid (uid);
	g_return_if_fail (overlaydata);

	/* Exchange them */
	if (glob->data->index)
		gtk_spect_vis_data_update (
			GTK_SPECTVIS (graph),
			glob->data->index,
			overlaydata->x,
			overlaydata->y,
			overlaydata->len);

	gtk_spect_vis_data_update (
		GTK_SPECTVIS (graph),
		overlaydata->index,
		glob->data->x,
		glob->data->y,
		glob->data->len);

	/* Exchange the uids for a running measurement */
	if ((glob->netwin) && (glob->netwin->index == overlaydata->index))
		glob->netwin->index = glob->data->index;
	else if ((glob->netwin) && (glob->netwin->index == glob->data->index))
		glob->netwin->index = overlaydata->index;
	
	tmpdouble = overlaydata->x;
	overlaydata->x = glob->data->x;
	glob->data->x = tmpdouble;
	
	tmpdvec = overlaydata->y;
	overlaydata->y = glob->data->y;
	glob->data->y = tmpdvec;

	tmplen = overlaydata->len;
	overlaydata->len = glob->data->len;
	glob->data->len = tmplen;
	
	tmpname = overlaydata->file;
	overlaydata->file = glob->data->file;
	glob->data->file = tmpname;

	if (overlaydata->x != glob->data->x)
	{
		/* Do not free theory->x as it is used in overlaydata->x */
		glob->theory->x = glob->data->x;

		g_free (glob->theory->y);
		glob->theory->y = g_new (ComplexDouble, glob->data->len);
		glob->theory->len = glob->data->len;

		gtk_spect_vis_data_update (
			GTK_SPECTVIS (graph),
			glob->theory->index,
			glob->theory->x,
			glob->theory->y,
			glob->theory->len);
	}

	/* Update theory _and_ fourier main graphs */
	visualize_theory_graph ();

	/* Delete the backup file */
	delete_backup ();

	/* Update name in list */
	basename = g_path_get_basename (overlaydata->file);
	gtk_list_store_set (glob->overlaystore, &iter,
			OVERLAY_FILE_COL, basename,
			OVERLAY_UID, overlaydata->index,
			-1);
	g_free (basename);

	if (!glob->resonancefile)
	{
		/* The title should hold the filename, change it. */
		basename = g_filename_display_basename (glob->data->file);

		g_mutex_lock (glob->threads->flaglock);
		if ((glob->flag & FLAG_CHANGES))
			tmpname = g_strdup_printf ("*(%s) - GWignerFit", basename);
		else
			tmpname = g_strdup_printf ("(%s) - GWignerFit", basename);
		g_mutex_unlock (glob->threads->flaglock);
		
		gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")), tmpname);

		g_free (tmpname);
		g_free (basename);
	}
	else
		set_unsaved_changes ();

	/* Tidy up */
	g_list_foreach (listselection, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (listselection);

	/* Redraw everything */
	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));

	fourier_update_overlay_graphs (-uid, FALSE);
	fourier_update_overlay_graphs ( uid, TRUE);

	overlay_get_color (&color, TRUE, uid, NULL);
	fourier_set_color (uid, color);
}

/* Return a list of id, filename, id, filename, ... */
GSList* overlay_get_filenames ()
{
	GSList *list = NULL;
	guint i;

	if (!glob->overlaystore) return NULL;

	for (i=0; i<glob->overlayspectra->len; i++)
	{
		list = g_slist_append (
				list, 
				GUINT_TO_POINTER (((DataVector *) g_ptr_array_index (glob->overlayspectra, i))->index)
			);
		list = g_slist_append (
				list, 
				((DataVector *) g_ptr_array_index (glob->overlayspectra, i))->file
			);
	}
	
	return list;
}
