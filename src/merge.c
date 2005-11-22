#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gdk/gdkkeysyms.h>

#include "structs.h"
#include "helpers.h"
#include "processdata.h"
#include "gtkspectvis.h"

#define MERGE_FLAG_ADD (1 << 0)
#define MERGE_FLAG_DEL (1 << 1)

extern GlobalData *glob;
extern GladeXML *gladexml;

/* Columns in merge_treeview */
enum {
	MERGE_ID_COL = 0,
	MERGE_LIST_COL,
	MERGE_DATAFILE_COL,
	MERGE_N_COLUMNS
};

/* Each resonance is denoted by such a node */
typedef struct
{
	guint id;	/* The id of the resonance list set */
	guint num;	/* The number of the resonance in the list */
	guint guid1;	/* The uid of the first GtkSpectVis graph link */
	guint guid2;	/* The uid of the second GtkSpectVis graph link */
	GList *link;	/* The links group this node belongs to or NULL */
	Resonance *res;	/* The properties of the resonance */
} MergeNode;

/* Forward declarations */
static gint merge_read_resonances (gchar *selected_filename, const gchar *label, GPtrArray **reslist, gchar **datafilename);
static void merge_zoom_x_all ();
static void merge_automatic_merge ();
static void merge_draw_remove_node (MergeNode *node);
static GList* merge_delete_link_node (MergeNode *node);
static gint merge_link_compare (gconstpointer a_in, gconstpointer b_in);
static GList* merge_get_closest (gdouble frq, gint depth);
static gdouble merge_get_minwidth (GList *cand);
static gdouble merge_get_avgfrq (GList *cand);
static void merge_draw_remove_all ();
static gboolean merge_draw_link (GList *link);

/* Display number of resonances in list on treeview */
void merge_show_numres (GtkTreeViewColumn *col, GtkCellRenderer *renderer, 
		        GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
	gint id;
	gchar *buf;
	GPtrArray *curnodelist;

	gtk_tree_model_get (model, iter, MERGE_ID_COL, &id, -1);

	if ((id < 1) || (id > glob->merge->nodelist->len)) 
		return;
	
	curnodelist = g_ptr_array_index (glob->merge->nodelist, id-1);

	buf = g_strdup_printf ("%d", curnodelist->len);

	g_object_set (renderer, "text", buf, NULL);
	g_free (buf);
}

/* Display the merge_win and prepare the liststore. */
void merge_open_win ()
{
	GtkSpectVis *graph;
	GladeXML *xmlmerge;
	GtkWidget *treeview;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	if (glob->merge)
		return;

	glob->merge = g_new0 (MergeWin, 1);

	glob->merge->nodelist     = g_ptr_array_new ();
	glob->merge->datafilename = g_ptr_array_new ();
	glob->merge->graphuid     = g_ptr_array_new ();
	glob->merge->links        = g_ptr_array_new ();

	xmlmerge = glade_xml_new (GLADEFILE, "merge_win", NULL);
	glade_xml_signal_autoconnect (xmlmerge);
	glob->merge->xmlmerge = xmlmerge;

	treeview = glade_xml_get_widget (xmlmerge, "merge_treeview");

	glob->merge->store = gtk_list_store_new (MERGE_N_COLUMNS,
			G_TYPE_UINT,
			G_TYPE_STRING,
			G_TYPE_STRING);

	gtk_tree_view_set_model (
			GTK_TREE_VIEW (treeview),
			GTK_TREE_MODEL (glob->merge->store));

	/* ID_COL */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
			"#", renderer,
			"text", MERGE_ID_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, -1);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* LIST_COL */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
			"resonance list", renderer,
			"text", MERGE_LIST_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, -1);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* NUMRES_COL */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
			"number of resonances", renderer,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, -1);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);
	gtk_tree_view_column_set_cell_data_func (
			column,
			renderer,
			merge_show_numres,
			NULL, NULL);

	/* Prepare the graph */
	graph = GTK_SPECTVIS (glade_xml_get_widget (xmlmerge, "merge_spectvis"));
	gtk_spect_vis_set_axisscale (graph, 1e9, 1);
}

/* Close the merge window, and free all allocated memory components */
void merge_purge ()
{
	MergeWin *merge;
	GPtrArray *curnodelist;
	GtkSpectVisData *data;
	GtkSpectVis *graph;
	gint i, j, uid;
	
	if (!glob->merge)
		return;

	merge = glob->merge;

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));

	/* Free polygon data by removing it */
	merge_draw_remove_all ();
	
	for (i=0; i<merge->nodelist->len; i++)
	{
		/* Free Resonance nodes */
		curnodelist = g_ptr_array_index (merge->nodelist, i);
		for (j=0; j<curnodelist->len; j++) 
			g_free (((MergeNode *) g_ptr_array_index (curnodelist, j))->res);
		g_ptr_array_free (curnodelist, TRUE);

		/* Free graph data */
		uid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, i));
		data = gtk_spect_vis_get_data_by_uid (graph, uid);
		g_free (data->X);
		g_free (data->Y);
	}
	g_ptr_array_free (merge->nodelist, TRUE);

	/* Free arrays with filenames and uids */
	g_ptr_array_foreach (merge->datafilename, (GFunc) g_free, NULL);
	g_ptr_array_free (merge->datafilename, TRUE);
	g_ptr_array_free (merge->graphuid, TRUE);

	/* Free links between nodes */
	g_ptr_array_foreach (merge->links, (GFunc) g_list_free, NULL);
	g_ptr_array_free (merge->links, TRUE);
	
	/* Destroy widgets */
	if (merge->xmlmerge)
	{
		gtk_widget_destroy (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));
		gtk_widget_destroy (glade_xml_get_widget (merge->xmlmerge, "merge_win"));
	}

	/* Clear store */
	if (merge->store)
	{
		gtk_list_store_clear (merge->store);
		merge->store = NULL;
	}

	g_free (merge);
	glob->merge = NULL;
}

/* Add a *reslist and a *datafilename to the graph, treeview and struct */
static void merge_add_reslist (GPtrArray *reslist, gchar *datafilename, gchar *name)
{
	MergeWin *merge = glob->merge;
	MergeNode *node;
	GPtrArray *curnodelist;
	GtkTreeIter iter;
	GtkSpectVis *graph;
	GdkColor color;
	gdouble *x;
	ComplexDouble *y;
	gint i, uid;
	
	g_return_if_fail (reslist);

	/* Add to pointer arrays */
	g_ptr_array_add (merge->datafilename, datafilename);

	/* Create new nodes */
	curnodelist = g_ptr_array_new ();
	for (i=0; i<reslist->len; i++)
	{
		node = g_new (MergeNode, 1);
		node->id    = merge->nodelist->len + 1;
		node->num   = i;
		node->link  = NULL;
		node->guid1 = 0;
		node->guid2 = 0;
		node->res   = (Resonance *) g_ptr_array_index (reslist, i);
		g_ptr_array_add (curnodelist, node);
	}
	g_ptr_array_add (merge->nodelist, curnodelist);

	/* Add to liststore */
	gtk_list_store_append (merge->store, &iter);
	gtk_list_store_set (merge->store, &iter,
			MERGE_ID_COL,       merge->nodelist->len,
			MERGE_LIST_COL,     name,
			MERGE_DATAFILE_COL, datafilename,
			-1);

	/* Add to graph */
	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));

	x = g_new (gdouble, reslist->len);
	y = g_new (ComplexDouble, reslist->len);

	for (i=0; i<reslist->len; i++)
	{
		x[i] = ((Resonance *) g_ptr_array_index (reslist, i))->frq;
		y[i].re = merge->nodelist->len - 0.25;
		y[i].im = merge->nodelist->len + 0.25;
	}
	
	color.red   = 0;
	color.green = 0;
	color.blue  = 65535;
	uid = gtk_spect_vis_data_add (
			graph,
			x,
			y,
			reslist->len,
			color, 'l');

	gtk_spect_vis_set_graphtype (graph, uid, 'i');
	g_ptr_array_add (merge->graphuid, GINT_TO_POINTER(uid));

	gtk_spect_vis_zoom_y_all (graph);
	merge_zoom_x_all ();
	gtk_spect_vis_redraw (graph);
}

/* Reads *label in *selected_filename and adds all resonances to **reslist and
 * the name of the datafile to **datafilename.
 * Returns the number of resonances found of -1 on failure.
 */
#define merge_resonancefile_cleanup	g_free (line);\
					g_free (text);\
					g_free (command);

static gint merge_read_resonances (gchar *selected_filename, const gchar *label, GPtrArray **reslist, gchar **datafilename)
{
	gchar *line, *command, *text;
	FILE *datafile;
	gdouble frq, wid, amp, phas;
	gdouble frqerr, widerr, amperr, phaserr;
	gint numres=0, pos=0, flag=0, i;
	Resonance *resonance=NULL;

	*datafilename = NULL;
	datafile = fopen (selected_filename, "r");

	if (datafile == NULL) {
		dialog_message("Error: Could not open file %s.", selected_filename);
		return -1;
	}

	line = g_new0 (gchar, 256);
	text = g_new0 (gchar, 256);
	command = g_new0 (gchar, 256);

	*reslist = g_ptr_array_new ();

	while (!feof(datafile)) {
		fgets(line, 255, datafile);
		if ((strlen(line)>0) && (line[strlen(line)-1] == '\n')) 
			line[strlen(line)-1] = '\0'; /* strip final \n */
		if ((strlen(line)>0) && (line[strlen(line)-1] == '\r')) 
			line[strlen(line)-1] = '\0'; /* strip final \r */
		pos++;

		if (strlen(line) > 254) {
			dialog_message ("Error: Line %i in '%s' too long!\n", pos, selected_filename);
			merge_resonancefile_cleanup;
			return -1;
		}

		/* Stop parsing after the current section */
		if ((flag) && ((*line == '$') || (*line == '='))) break;

		if ((*line == '$') && (strncmp(line+1, label, 254) == 0)) {
			/* Found a matching "Stefan Bittner" style section */
			dialog_message ("Sefan Bittner style section is not supported.");
			merge_resonancefile_cleanup;
			return -1;
		}
		if ((*line == '=') && (strncmp(line+1, label, 254) == 0)) {
			/* Found a matching "Florian Schaefer" style section */
			flag = 10;
			continue;
		}

		/* Comment lines start with either '%' or '#' */
		if ((*line == '%') || (*line == '#') || (strlen(line) == 0)) continue;

		/* Actually parse the section */

		/* Begin "Florian Schaefer" style */
		if (flag == 10) {
			i = sscanf(line, "%250s\t%lf\t%lf\t%lf\t%lf%lf\t%lf\t%lf\t%lf", 
					command, &frq, &wid, &amp, &phas, &frqerr, &widerr, &amperr, &phaserr);
			switch (i) {
				case 1: /* Datafile */
				  if (sscanf(line, "%250s\t%250s", command, text) == 2)
				  {
					  /* text may not hold the complete filename if it
					   * contained spaces. */
					  if (!strncmp(command, "file", 200)) 
						  *datafilename = g_strdup (line+5);
				  }
				  break;
				case 5: /* Resonance data */
				  if (!strncmp(command, "res", 200)) {
					resonance = g_new (Resonance, 1);
					resonance->frq   = frq * 1e9;
					resonance->width = wid * 1e6;
					resonance->amp   = amp;
					resonance->phase = phas / 180*M_PI;
					g_ptr_array_add (*reslist, resonance);
					numres++;
				  } else {
					  dialog_message ("Error: Unrecognized command '%s' in line %i.\n", command, pos);
					  merge_resonancefile_cleanup;
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
					g_ptr_array_add (*reslist, resonance);
					numres++;
				  } else {
					  dialog_message ("Error: Unrecognized command '%s' in line %i.\n", command, pos);
					  merge_resonancefile_cleanup;
					  return -1;
				  }
			}
		}
		/* End "Florian Schaefer" style */
	}

	if ((feof(datafile)) && (flag == 0)) {
		dialog_message ("Error: No section '%s' found in '%s'.\n", label, selected_filename);
		merge_resonancefile_cleanup;
		return -1;
	}

	fclose (datafile);
	merge_resonancefile_cleanup;

	return numres;
}

#undef merge_resonancefile_cleanup

/* Zoom x so that all resonances can be seen */
static void merge_zoom_x_all ()
{
	GtkSpectVis *graph;
	gdouble min, max;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));
	
	gtk_spect_vis_zoom_x_all (graph);
	min = graph->view->xmin;
	max = graph->view->xmax;
	gtk_spect_vis_zoom_x_to (graph, min - (max-min)*0.02, max + (max-min)*0.02);
}

/********** Callbacks from the merge window **********************************/

/* Close the merge_win */
gboolean on_merge_close_activate (GtkWidget *button)
{
	merge_purge ();
	return TRUE;
}

/* Ask for a resonance list and add it */
gboolean on_merge_add_list_activate (GtkWidget *button)
{
	gchar *filename, *section = NULL, *path = NULL;
	gchar *name, *datafilename = NULL;
	GPtrArray *reslist = NULL;

	path = get_defaultname (NULL);
	filename = get_filename ("Select file with resonance list", path, 1);
	g_free (path);

	if (!filename)
		return FALSE;

	if (select_section_dialog (filename, NULL, &section))
	{
		/* select_section_dialog returned TRUE -> something went wrong */
		g_free (filename);
		return FALSE;
	}

	if ((section) && (merge_read_resonances (filename, section, &reslist, &datafilename) < 0))
	{
		g_free (filename);
		g_free (section);
		g_ptr_array_free (reslist, TRUE);
		g_free (datafilename);
		return FALSE;
	}

	/* sort reslist */
	g_ptr_array_sort (reslist, param_compare);

	name = g_strdup_printf ("%s:%s", filename, section);
	merge_add_reslist (reslist, datafilename, name);
	/* reslist must not be freed here */
	
	g_free (section);
	g_free (filename);
	return TRUE;
}

/* Remove the selected resonance list */
gboolean on_merge_remove_list_activate (GtkWidget *button)
{
	MergeWin *merge = glob->merge;
	GtkTreeIter iter;
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GList *pathlist;
	GPtrArray *curnodelist;
	gint id = -1, curid, uid, i, j, reduce;
	GtkSpectVis *graph;
	GtkSpectVisData *data;
	GList *linkiter;
	MergeNode *linknode;

	selection = gtk_tree_view_get_selection ( GTK_TREE_VIEW (
			glade_xml_get_widget (merge->xmlmerge, "merge_treeview")));

	if (gtk_tree_selection_count_selected_rows (selection) == 0)
	{
		dialog_message ("No row selected.");
		return FALSE;
	}

	/* Get path of selected row */
	model     = GTK_TREE_MODEL (merge->store);
	pathlist  = gtk_tree_selection_get_selected_rows (selection, &model);

	/* Remove the row */
	gtk_tree_model_get_iter (model, &iter, (GtkTreePath *) pathlist->data);
	gtk_tree_model_get (model, &iter, MERGE_ID_COL, &id, -1);
	if (id < 1)
	{
		dialog_message ("Error: Could not determine resonance ID.");
		return FALSE;
	}
	gtk_list_store_remove (merge->store, &iter);

	/* Change ID's of other rows */
	if (gtk_tree_model_get_iter_first (model, &iter))
		do
		{
			gtk_tree_model_get (model, &iter, MERGE_ID_COL, &curid, -1);
			if (curid > id)
			{
				curid--;
				gtk_list_store_set (merge->store, &iter, MERGE_ID_COL, curid, -1);
			}
		}
		while (gtk_tree_model_iter_next(model, &iter));

	/* Select the old path -> the next resonance in the list */
	gtk_tree_selection_select_path (selection, (GtkTreePath *) pathlist->data);

	/* Tidy up */
	g_list_foreach (pathlist, (GFunc) gtk_tree_path_free, NULL);
	g_list_free (pathlist);
	
	/* Remove all links from the graph */
	merge_draw_remove_all ();

	/* Update the iIDs of each link node */
	for (i=0; i<merge->links->len; i++)
	{
		linkiter = (GList *) g_ptr_array_index (merge->links, i);
		while (linkiter)
		{
			linknode = (MergeNode *) linkiter->data;

			if (linknode->id == id) linknode->id = 0;
			if (linknode->id >  id) linknode->id--;

			linkiter = g_list_next (linkiter);
		}
	}

	/* Remove stale links */
	reduce = 0;
	for (i=0; i<merge->links->len; i++)
	{
		linkiter = (GList *) g_ptr_array_index (merge->links, i);
		while (linkiter)
		{
			linknode = (MergeNode *) linkiter->data;

			if (linknode->id == 0)
			{
				i--;
				if ((linkiter = merge_delete_link_node (linknode)))
					reduce++;
			}
			else 
				linkiter = g_list_next (linkiter);
		}
	}

	/* Add links to graph */
	for (i=0; i<merge->links->len; i++)
	{
		linkiter = (GList *) g_ptr_array_index (merge->links, i);
		merge_draw_link (linkiter);
	}

	/* id will now be the "real" position in the pointer arrays */
	id--;

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));

	/* Remove graph and free data */
	uid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, id));
	data = gtk_spect_vis_get_data_by_uid (graph, uid);
	g_free (data->X);
	g_free (data->Y);
	gtk_spect_vis_data_remove (graph, uid);

	for (i=id+1; i<merge->graphuid->len; i++)
	{
		/* Update position of graphs with bigger id */
		uid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, i));
		data = gtk_spect_vis_get_data_by_uid (graph, uid);

		for (j=0; j<data->len; j++)
		{
			data->Y[j].re -= 1.0;
			data->Y[j].im -= 1.0;
		}
	}
	
	/* free data */
	curnodelist = g_ptr_array_index (merge->nodelist, id);
	for (i=0; i<curnodelist->len; i++) 
		g_free (((MergeNode *) g_ptr_array_index (curnodelist, i))->res);
	g_ptr_array_free (curnodelist, TRUE);

	g_free (g_ptr_array_index (merge->datafilename, id));
	
	g_ptr_array_remove_index (merge->nodelist, id);
	g_ptr_array_remove_index (merge->datafilename, id);
	g_ptr_array_remove_index (merge->graphuid, id);

	/* Update graph */
	if (merge->graphuid->len)
	{
		merge_zoom_x_all ();
		gtk_spect_vis_zoom_y_all (graph);
	}
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

/* Add a single link via user input */
gboolean on_merge_add_link_activate (GtkWidget *button)
{
	glob->merge->flag |= MERGE_FLAG_ADD;
	glob->merge->flag &= ~MERGE_FLAG_DEL;
	
	return TRUE;
}

/* Delete a single link via user input */
gboolean on_merge_delete_link_activate (GtkWidget *button)
{
	glob->merge->flag |= MERGE_FLAG_DEL;
	glob->merge->flag &= ~MERGE_FLAG_DEL;
	
	return TRUE;
}

/* Remove the selected resonance list */
gboolean on_merge_find_links_activate (GtkWidget *button)
{
	if (glob->merge->nodelist->len < 2)
	{
		dialog_message ("Please add at least two resonance lists.");
		return FALSE;
	}
	
	merge_automatic_merge ();

	return TRUE;
}

/* Zooming, etc. */
void merge_handle_viewport_changed (GtkSpectVis *spectvis, gchar *zoomtype)
{
	if (zoomtype == "a")
		merge_zoom_x_all ();
	
	gtk_spect_vis_redraw (spectvis);
}

/* Handle click on graph */
gint merge_handle_value_selected (GtkSpectVis *spectvis, gdouble *xval, gdouble *yval)
{
	g_return_val_if_fail (glob->merge, 0);
	
	if (glob->merge->flag & MERGE_FLAG_ADD)
	{
		printf ("add\n");

		glob->merge->flag &= ~MERGE_FLAG_ADD;
		return 0;
	}

	if (glob->merge->flag & MERGE_FLAG_DEL)
	{
		printf ("delete\n");

		glob->merge->flag &= ~MERGE_FLAG_DEL;
		return 0;
	}
	
	gtk_spect_vis_mark_point (spectvis, *xval, *yval);

	return 0;
}

/********** Basic link and node handling *************************************/

/* Takes a node and removes all links associated with it.
 * Returns the next GList element of NULL if the list has been deleted. */
static GList* merge_delete_link_node (MergeNode *node)
{
	GList *linkiter, *newlisthead, *nextiter;
	
	g_return_val_if_fail (node, FALSE);

	if (node->link)
	{
		/* Remove node link from graph */
		merge_draw_remove_node (node);

		/* Remove link from glob->merge->links as the start may change */
		g_ptr_array_remove (glob->merge->links, node->link);
		
		/* Find position of node in list (node->link is the head) */
		linkiter = g_list_find (node->link, node);
		nextiter = g_list_next (linkiter);
		
		/* Remove node from list */
		newlisthead = g_list_delete_link (node->link, linkiter);
		g_return_val_if_fail (g_list_length (newlisthead), FALSE);

		/* Has the list become too short? */
		if (g_list_length (newlisthead) == 1)
		{
			/* Yes, only one node left -> delete it*/
			g_list_free (newlisthead);

			return NULL;
		}
		else
		{
			/* Add new list to merge->links */
			g_ptr_array_add (glob->merge->links, newlisthead);

			/* Update link information of each node */
			linkiter = newlisthead;
			while (linkiter)
			{
				((MergeNode *) linkiter->data)->link = newlisthead;

				linkiter = g_list_next (linkiter);
			}
		}

	}
	else
		return NULL;

	return nextiter;
}

/* Removes graphical links associated with a given node and reconnects
 * neighbouring nodes instead */
static void merge_draw_remove_node (MergeNode *node)
{
	GList *curiter, *previter, *nextiter;
	GtkSpectVis *graph;
	GdkColor color;
	gdouble *X, *Y;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));

	curiter  = g_list_find (node->link, node);
	previter = g_list_previous (curiter);
	nextiter = g_list_next (curiter);

	if (node->guid1)
	{
		gtk_spect_vis_polygon_remove (graph, node->guid1, TRUE);
		((MergeNode *) nextiter->data)->guid2 = 0;
	}
	if (node->guid2)
	{
		gtk_spect_vis_polygon_remove (graph, node->guid2, TRUE);
		((MergeNode *) previter->data)->guid1 = 0;
	}

	node->guid1 = node->guid2 = 0;

	if (previter && nextiter)
	{
		/* Node is in the middle of a link -> reconnect */
		color.red   = 65535/255*228;
		color.green = 65535/255*22;
		color.blue  = 65535/255*172;
		
		X = g_new (gdouble, 2);
		Y = g_new (gdouble, 2);

		X[0] = ((MergeNode *) previter->data)->res->frq;
		X[1] = ((MergeNode *) nextiter->data)->res->frq;
		Y[0] = ((MergeNode *) previter->data)->id + 0.25;
		Y[1] = ((MergeNode *) nextiter->data)->id - 0.25;

		((MergeNode *) curiter->data )->guid1 = gtk_spect_vis_polygon_add (graph, X, Y, 2, color, 'l');
		((MergeNode *) nextiter->data)->guid2 = ((MergeNode *) curiter->data)->guid1;
	}
}

/* Remove all links from the graph */
static void merge_draw_remove_all ()
{
	GtkSpectVis *graph;
	GList *curiter, *previter, *nextiter;
	MergeNode *node;
	gint i;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));
	
	for (i=0; i<glob->merge->links->len; i++)
	{
		curiter = (GList *) g_ptr_array_index (glob->merge->links, i);
		while (curiter)
		{
			node = (MergeNode *) curiter->data;

			curiter  = g_list_find (node->link, node);
			previter = g_list_previous (curiter);
			nextiter = g_list_next (curiter);

			if (node->guid1)
			{
				gtk_spect_vis_polygon_remove (graph, node->guid1, TRUE);
				((MergeNode *) nextiter->data)->guid2 = 0;
			}
			if (node->guid2)
			{
				gtk_spect_vis_polygon_remove (graph, node->guid2, TRUE);
				((MergeNode *) previter->data)->guid1 = 0;
			}

			node->guid1 = node->guid2 = 0;
			
			curiter = g_list_next (curiter);
		}
	}
}

/* Takes a GList with node link information and adds it to the graph */
static gboolean merge_draw_link (GList *link)
{
	MergeNode *curnode, *nextnode;
	GtkSpectVis *graph;
	GList *nextlink;
	GdkColor color;
	gdouble *X, *Y;

	if (!link)
		return FALSE;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));

	color.red   = 65535/255*228;
	color.green = 65535/255*22;
	color.blue  = 65535/255*172;
	
	while ((link) && (nextlink = g_list_next (link)))
	{
		curnode  = (MergeNode *) link->data;
		nextnode = (MergeNode *) nextlink->data;

		if (curnode->guid1)
			gtk_spect_vis_polygon_remove (graph, curnode->guid1, TRUE);
		if (nextnode->guid2)
			gtk_spect_vis_polygon_remove (graph, nextnode->guid2, TRUE);
		
		X = g_new (gdouble, 2);
		Y = g_new (gdouble, 2);

		X[0] = curnode ->res->frq;
		X[1] = nextnode->res->frq;
		Y[0] = curnode ->id + 0.25;
		Y[1] = nextnode->id - 0.25;

		curnode ->guid1 = gtk_spect_vis_polygon_add (graph, X, Y, 2, color, 'l');
		nextnode->guid2 = curnode->guid1;

		link = g_list_next (link);
	}

	return TRUE;
}

/* Compare two MergeNode structs by their id */
static gint merge_link_compare (gconstpointer a_in, gconstpointer b_in)
{
	const MergeNode *a = (MergeNode *) a_in;
	const MergeNode *b = (MergeNode *) b_in;
	
	if (a->id < b->id)
		return -1;
	else if (a->id > b->id)
		return +1;
	else
		return 0;
}

/********** Automatic merger *************************************************/

/* Does the main automatic merge voodoo */
static void merge_automatic_merge ()
{
	MergeWin *merge;
	GPtrArray *curnodelist;
	GList *candidates, *canditer, *itertmp;
	Resonance *res;
	gint i, j;
	gdouble minwidth, avgfrq;
	gboolean flag = FALSE;

	g_return_if_fail (glob->merge);
	merge = glob->merge;

	/* Go through all datasets */
	for (i=0; i<merge->nodelist->len; i++)
	{
		curnodelist = (GPtrArray *) g_ptr_array_index (merge->nodelist, i);

		/* Go through all resonances of dataset */
		for (j=0; j<curnodelist->len; j++)
		{
			/* Compile a list of closest neighbours of "later" datasets */
			candidates = merge_get_closest (
				((MergeNode *) g_ptr_array_index (curnodelist, j))->res->frq, i);

			if (!candidates)
				continue;

			/* Preprend current node to list */
			candidates = g_list_insert (candidates, g_ptr_array_index (curnodelist, j), 0);
			
	/*
	printf ("new link candidate: ");
			canditer = candidates;
			do {
	printf("%d, %d, %e\t", 
				((MergeNode *) canditer->data)->id,
				((MergeNode *) canditer->data)->num,
				((MergeNode *) canditer->data)->res->frq);
			} while ((canditer = g_list_next (canditer)));
	printf ("\n");
	*/

			minwidth = merge_get_minwidth (candidates) * 0.5;
			avgfrq   = merge_get_avgfrq   (candidates);
	//printf("minwidth %e, avgfrq %e\n", minwidth, avgfrq);

			/* Get close lying, unlinked resonances from the candidates */
			canditer = candidates;
			do {
				res  = ((MergeNode *) canditer->data)->res;
				flag = FALSE;

				if ((fabs (res->frq - avgfrq) > minwidth) ||
				    (((MergeNode *) canditer->data)->link))
				{
					/* Too far away or already linked */
					itertmp    = canditer;
					canditer   = g_list_next (canditer);
					candidates = g_list_delete_link (candidates, itertmp);

					if ((!canditer) || (!candidates))
						break;

					flag = TRUE; /* We are already at the next link */

					if (res->width == minwidth)
					{
						/* Need to recalculate minwidth */
						minwidth = merge_get_minwidth (candidates) * 0.5;
					}

					avgfrq = merge_get_avgfrq (candidates);
				}
			}
			while ((flag) || (canditer = g_list_next (canditer)));

			if ((candidates) && (g_list_length (candidates) > 1))
			{
				/* Mark candidates as linked */
	//printf ("new link: ");
				canditer = candidates;
				((MergeNode *) canditer->data)->link = candidates;
				do 
				{
					((MergeNode *) canditer->data)->link = candidates;
	/*
	printf("%d, %d\t", 
			((MergeNode *) canditer->data)->id,
			((MergeNode *) canditer->data)->num);
	*/
				}
				while ((canditer = g_list_next (canditer)));
	//printf ("\n");

				/* Sort the list */
				g_list_sort (candidates, merge_link_compare);
				
				/* Add candidates to links list */
				g_ptr_array_add (merge->links, candidates);
				merge_draw_link (candidates);
			}
			else if (candidates)
			{
				/* A single node is no link */
				g_list_free (candidates);
			}
		}
	}

	gtk_spect_vis_redraw (
		GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis")));
}

/* Searches for resonances closest to *res in datasets below depth and stores
 * them in *cand if they aren't linked yet. */
static GList* merge_get_closest (gdouble frq, gint depth)
{
	MergeWin *merge = glob->merge;
	GList *cand = NULL;
	GPtrArray *curnodelist;
	gint i;
	gdouble diff;
	
	for (depth++; depth < merge->nodelist->len; depth++)
	{
		curnodelist = (GPtrArray *) g_ptr_array_index (merge->nodelist, depth);

		i = 0;
		while ((i < curnodelist->len) &&
		       (((MergeNode *) g_ptr_array_index (curnodelist, i))->res->frq < frq))
			i++;
		/* "i" now points to the resonance with a bigger 
		 * frequency or to the end of the list */

		if (i < curnodelist->len) //((MergeNode *) g_ptr_array_index (curnodelist, i))->res->frq >= frq)
		{
			/* OK, we're really at a larger (or equal) frequency. */

			if (i > 0)
			{
				/* Might the frequency before this one be closer? */
				diff = ((MergeNode *) g_ptr_array_index (curnodelist, i))->res->frq - frq;

				if (diff > frq - ((MergeNode *) g_ptr_array_index (curnodelist, i-1))->res->frq)
					i--;	/* Yep */
			}
			
			/* Add node only if it isn't already linked */
			if (!( ((MergeNode *) g_ptr_array_index (curnodelist, i))->link ))
				cand = g_list_append (cand, g_ptr_array_index (curnodelist, i));
		}
	}

	return cand;
}

/* Return the smallest width in a Resonance set */
static gdouble merge_get_minwidth (GList *cand)
{
	gdouble width = 1e50;

	if (!cand)
		return -1.0;

	do {
		if (((MergeNode *) cand->data)->res->width < width)
			width = ((MergeNode *) cand->data)->res->width;
	} 
	while ((cand = g_list_next (cand)));

	return width;
}

/* Return the average frequency of a Resonance set */
static gdouble merge_get_avgfrq (GList *cand)
{
	gdouble frq = 0;
	gint len;
	
	if (!cand)
		return -1.0;

	len = g_list_length (cand);

	do {
		frq += ((MergeNode *) cand->data)->res->frq;
	} 
	while ((cand = g_list_next (cand)));

	return frq/(gdouble) len;
}
