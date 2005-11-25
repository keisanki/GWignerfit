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

#define MERGE_FLAG_ADD    (1 << 0)
#define MERGE_FLAG_DEL    (1 << 1)
#define MERGE_FLAG_MARK   (1 << 2)
#define MERGE_FLAG_DELRES (1 << 3)

#define MERGE_NODE_R 0
#define MERGE_NODE_G 0
#define MERGE_NODE_B 65535

#define MERGE_LINK_R 65535/255*200
#define MERGE_LINK_G 65535/255*100
#define MERGE_LINK_B 65535/255*0

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
	GList *link;	/* The position in the links group this node belongs to or NULL */
	Resonance *res;	/* The properties of the resonance */
} MergeNode;

/* Forward declarations */
static gint merge_read_resonances (gchar *selected_filename, const gchar *label, GPtrArray **reslist, gchar **datafilename);
static void merge_zoom_x_all ();
static void merge_automatic_merge ();
static void merge_draw_remove_node (MergeNode *node, gint type);
static GList* merge_delete_link_node (MergeNode *node, gint type);
static gint merge_link_compare (gconstpointer a_in, gconstpointer b_in);
static GList* merge_get_closest (gdouble frq, gint depth);
static gdouble merge_get_minwidth (GList *cand);
static gdouble merge_get_avgfrq (GList *cand);
static void merge_draw_remove_all ();
static gboolean merge_draw_link (GList *link);
static void merge_undisplay_node_selection ();
static gboolean merge_is_id_in_list (guint id, GList *list);
static void merge_draw_remove (GList *link);
static GArray* merge_gather_reslist ();
static void merge_highlight_width (MergeNode *node);
static MergeNode* merge_get_nearnode (gint xpos, gint ypos, gint *xpix, gint *ypix);
void merge_statusbar_message (gchar *format, ...);

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
	glob->merge->selx         = -1;

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
	gtk_tree_view_column_set_expand (column, TRUE);
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

/* Close the merge window (if free_only == FALSE), 
 * and free all allocated memory components */
void merge_purge (gboolean free_only)
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

	merge_highlight_width (NULL);
	
	for (i=0; i<merge->nodelist->len; i++)
	{
		/* Free Resonance nodes */
		curnodelist = g_ptr_array_index (merge->nodelist, i);
		for (j=0; j<curnodelist->len; j++) 
			g_free (((MergeNode *) g_ptr_array_index (curnodelist, j))->res);
		g_ptr_array_foreach (curnodelist, (GFunc) g_free, NULL);
		g_ptr_array_free (curnodelist, TRUE);

		/* Free graph data */
		uid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, i));
		data = gtk_spect_vis_get_data_by_uid (graph, uid);
		if (data)
		{
			g_free (data->X);
			g_free (data->Y);
		}
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
	if ((merge->xmlmerge) && (!free_only))
	{
		gtk_widget_destroy (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));
		gtk_widget_destroy (glade_xml_get_widget (merge->xmlmerge, "merge_win"));
	}

	/* Clear store */
	if (merge->store)
	{
		gtk_list_store_clear (merge->store);
		if (!free_only)
			merge->store = NULL;
	}

	if (!free_only)
	{
		g_free (merge);
		glob->merge = NULL;
	}
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
	
	color.red   = MERGE_NODE_R;
	color.green = MERGE_NODE_G;
	color.blue  = MERGE_NODE_B;
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
	merge_purge (FALSE);
	return TRUE;
}

/* Clear everything so that the user gets its new start. */
gboolean on_merge_new_activate (GtkWidget *button)
{
	MergeWin *merge = glob->merge;
	GtkSpectVis *graph;
	gint i;

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));

	for (i=0; i<merge->graphuid->len; i++)
		gtk_spect_vis_data_remove (graph, 
				GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, i)));

	merge_draw_remove_all ();
	
	merge_purge (TRUE);
	merge_undisplay_node_selection ();

	glob->merge->nodelist     = g_ptr_array_new ();
	glob->merge->datafilename = g_ptr_array_new ();
	glob->merge->graphuid     = g_ptr_array_new ();
	glob->merge->links        = g_ptr_array_new ();

	gtk_spect_vis_redraw (graph);

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
	g_free (name);
	/* reslist must not be freed here */
	
	g_free (section);
	g_free (filename);
	return TRUE;
}

/* Save merged resonance list */
gboolean on_merge_save_activate (GtkWidget *button)
{
	gchar *filename, *datafile, *path = NULL;
	GtkTreeModel *model = GTK_TREE_MODEL (glob->merge->store);
	GtkTreeIter iter;
	GArray *reslist;
	FILE *outfile;
	gchar *date;
	guint i;

	path = get_defaultname (NULL);
	filename = get_filename ("Select file for new resonance list", path, 2);
	g_free (path);

	if (!filename)
		return FALSE;

	/* Compile the merged resonance list */
	reslist = merge_gather_reslist ();

	if (!reslist->len)
	{
		g_free (filename);
		dialog_message ("No resonances to export?!?");
		return FALSE;
	}

	outfile = fopen (filename, "w");
	g_free (filename);

	if (!outfile)
	{
		dialog_message ("Could not open output file for writing.");
		g_array_free (reslist, TRUE);
		return FALSE;
	}

	date = get_timestamp ();
	fprintf (outfile, "# Merged resonance list created with GWignerFit\r\n");
	fprintf (outfile, "# Date: %s\r\n", date);
	fprintf (outfile, "#\r\n# Source datasets:\r\n");
	g_free (date);

	gtk_tree_model_get_iter_first (model, &iter);
	do
	{
		gtk_tree_model_get (model, &iter, MERGE_LIST_COL, &datafile, -1);
		fprintf (outfile, "# %s\r\n", datafile);
	}
	while (gtk_tree_model_iter_next (model, &iter));
	
	fprintf (outfile, "#\r\n# ID\t   f [Hz]\r\n");

	for (i=0; i<reslist->len; i++)
		fprintf (outfile, "%4d\t%13.1f\r\n", i+1, g_array_index (reslist, gdouble, i));

	fclose (outfile);

	merge_statusbar_message ("Exported %d resonance frequencies.", reslist->len);
	
	g_array_free (reslist, TRUE);
	return FALSE;
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

	/* Update the IDs of each link node and remove stale links*/
	reduce = 0;
	for (i=0; i < merge->links->len - reduce; i++)
	{
		linkiter = (GList *) g_ptr_array_index (merge->links, i);
		while (linkiter)
		{
			linknode = (MergeNode *) linkiter->data;

			if (linknode->id == id)
			{
				i--;
				if ((linkiter = merge_delete_link_node (linknode, 0)))
					reduce++;
			}
			else 
			{
				if (linknode->id > id) 
					linknode->id--;

				linkiter = g_list_next (linkiter);
			}
		}
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

	/* Update position of impulse graphs with bigger IDs */
	for (i=id+1; i<merge->graphuid->len; i++)
	{
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
	g_ptr_array_foreach (curnodelist, (GFunc) g_free, NULL);
	g_ptr_array_free (curnodelist, TRUE);

	g_free (g_ptr_array_index (merge->datafilename, id));
	
	g_ptr_array_remove_index (merge->nodelist, id);
	g_ptr_array_remove_index (merge->datafilename, id);
	g_ptr_array_remove_index (merge->graphuid, id);

	/* Add links to graph */
	for (i=0; i<merge->links->len; i++)
	{
		linkiter = (GList *) g_ptr_array_index (merge->links, i);
		merge_draw_link (linkiter);
	}

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
	//GtkWidget *win;
	//gint x, y, xpix, ypix;
	
	glob->merge->flag |= MERGE_FLAG_ADD;
	glob->merge->flag &= ~MERGE_FLAG_DEL;
	glob->merge->flag &= ~MERGE_FLAG_MARK;
	glob->merge->flag &= ~MERGE_FLAG_DELRES;

	//win = glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis");
	//g_return_val_if_fail (gdk_window_get_pointer (win->window, &x, &y, NULL), FALSE);
	//glob->merge->nearnode = merge_get_nearnode (x, y, &xpix, &ypix);
	
	return TRUE;
}

/* Delete a single link via user input */
gboolean on_merge_delete_link_activate (GtkWidget *button)
{
	glob->merge->flag |= MERGE_FLAG_DEL;
	glob->merge->flag &= ~MERGE_FLAG_ADD;
	glob->merge->flag &= ~MERGE_FLAG_MARK;
	glob->merge->flag &= ~MERGE_FLAG_DELRES;
	
	return TRUE;
}

/* Delete all available links */
gboolean on_merge_delete_all_links_activate (GtkWidget *button)
{
	MergeWin *merge = glob->merge;
	GtkSpectVis *graph;
	GPtrArray *nodelist;
	gint i, j;

	if (!merge->nodelist->len)
	{
		dialog_message ("There are no links to be deleted.");
		return FALSE;
	}

	if (dialog_question ("Do you really want to delete all links?") != GTK_RESPONSE_YES)
		return FALSE;

	/* Remove links from graph */
	merge_draw_remove_all ();

	/* Free links between nodes */
	g_ptr_array_foreach (merge->links, (GFunc) g_list_free, NULL);
	g_ptr_array_free (merge->links, TRUE);
	merge->links = g_ptr_array_new ();

	/* Update the IDs of each link node and remove stale links*/
	for (i=0; i<merge->nodelist->len; i++)
	{
		nodelist = g_ptr_array_index (merge->nodelist, i);
		for (j=0; j<nodelist->len; j++)
			((MergeNode *) g_ptr_array_index (nodelist, j))->link = NULL;
	}

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));
	gtk_spect_vis_redraw (graph);

	merge_statusbar_message ("Deleted all links.");
	
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
	merge_statusbar_message ("Automatic merge completed.");

	return TRUE;
}

/* Mark the next selected resonance width by a bar */
gboolean on_merge_highlight_activate (GtkWidget *button)
{
	glob->merge->flag |= MERGE_FLAG_MARK;
	glob->merge->flag &= ~MERGE_FLAG_ADD;
	glob->merge->flag &= ~MERGE_FLAG_DEL;
	glob->merge->flag &= ~MERGE_FLAG_DELRES;
	
	return TRUE;
}

/* Remove the next selected resonance from nodelist */
gboolean on_merge_remove_resonance_activate (GtkWidget *button)
{
	glob->merge->flag |= MERGE_FLAG_DELRES;
	glob->merge->flag &= ~MERGE_FLAG_ADD;
	glob->merge->flag &= ~MERGE_FLAG_MARK;
	glob->merge->flag &= ~MERGE_FLAG_DEL;
	
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
	static MergeNode *node1 = NULL;
	GtkSpectVisData *data;
	GList *newlist = NULL;
	MergeNode *node;
	MergeWin *merge;
	gdouble *X;
	ComplexDouble *Y;
	gint i, uid;
	
	g_return_val_if_fail (glob->merge, 0);
	merge = glob->merge;
	
	if (merge->flag & MERGE_FLAG_ADD)
	{
		node = (MergeNode *) merge->nearnode;
		if (!node)
		{
			merge->flag &= ~MERGE_FLAG_ADD;
			merge_statusbar_message ("You are not close enough to a resonance node point.");
			node1 = NULL;
			return 0;
		}

		if (!node1)
		{
			if ((node->link) && (node->link->prev) && (node->link->next))
			{
				merge->flag &= ~MERGE_FLAG_ADD;
				merge_statusbar_message ("No free links on this node left.");
			}
			else
				node1 = node;
			return 0;
		}
		else
		{
			merge->flag &= ~MERGE_FLAG_ADD;
			if ((node->link) && (node->link->prev) && (node->link->next))
				merge_statusbar_message ("No free links on this node left.");
			else
			{
				/* Add link node1 -> node */
				if (((node ->link) && (merge_is_id_in_list (node1->id, node ->link))) ||
				    ((node1->link) && (merge_is_id_in_list (node ->id, node1->link))) ||
				    (node1->id == node->id))
				{
					merge_statusbar_message ("Forbidden, two resonances from the "
							"same list would be in the same link group.");
					node1 = NULL;
					return 0;
				}

				if (node->link && node1->link)
				{
					/* Connect two link lists */
					merge_draw_remove (node ->link);
					merge_draw_remove (node1->link);
					g_ptr_array_remove (merge->links, g_list_first (node ->link));
					g_ptr_array_remove (merge->links, g_list_first (node1->link));
					newlist = g_list_first (node->link);
					newlist = g_list_concat (newlist, g_list_first (node1->link));
				}
				else if (node->link)
				{
					/* Second node already has a list */
					merge_draw_remove (node->link);
					g_ptr_array_remove (merge->links, g_list_first (node->link));
					newlist = g_list_first (node->link);
					newlist = g_list_append (newlist, node1);
					node1->link = g_list_last (newlist);
				}
				else if (node1->link)
				{
					/* First node already has a list */
					merge_draw_remove (node1->link);
					g_ptr_array_remove (merge->links, g_list_first (node1->link));
					newlist = g_list_first (node1->link);
					newlist = g_list_append (newlist, node);
					node->link = g_list_last (newlist);
				}
				else
				{
					/* New list out of node and node1 */
					newlist = g_list_append (NULL, node);
					node->link = g_list_last (newlist);
					newlist = g_list_append (newlist, node1);
					node1->link = g_list_last (newlist);
				}

				newlist = g_list_sort (newlist, merge_link_compare);
				g_ptr_array_add (merge->links, newlist);
				merge_draw_link (newlist);
				gtk_spect_vis_redraw (spectvis);

				/* Update link information of each node */
				while (newlist)
				{
					((MergeNode *) newlist->data)->link = newlist;
					newlist = g_list_next (newlist);
				}
			}
			node1 = NULL;
		}

		return 0;
	}
	else
		if (node1)
			node1 = NULL;

	if (merge->flag & MERGE_FLAG_DEL)
	{
		merge->flag &= ~MERGE_FLAG_DEL;
		node = (MergeNode *) merge->nearnode;
		if (!node)
		{
			merge_statusbar_message ("You are not close enough to a resonance node point.");
			return 0;
		}

		/* Identify link to delete */
		if (*yval - (gdouble) node->id > 0.0)
		{
			/* Link to a node with a bigger id */
			if (!(MergeNode *) g_list_next (node->link))
			{
				merge_statusbar_message ("There is no link here that can be deleted.");
				return 0;
			}
			merge_delete_link_node (node, 1);
		}
		else
		{
			/* Link to a node with a smaller id */
			if (!(MergeNode *) g_list_previous (node->link))
			{
				merge_statusbar_message ("There is no link here that can be deleted.");
				return 0;
			}
			merge_delete_link_node (node, 2);
		}
		
		gtk_spect_vis_redraw (spectvis);
		merge->nearnode = NULL;

		return 0;
	}

	if (merge->flag & MERGE_FLAG_MARK)
	{
		merge->flag &= ~MERGE_FLAG_MARK;
		node = (MergeNode *) merge->nearnode;

		merge_highlight_width (node);

		return 0;
	}

	if (merge->flag & MERGE_FLAG_DELRES)
	{
		merge->flag &= ~MERGE_FLAG_DELRES;
		node = (MergeNode *) merge->nearnode;
		if (!node)
		{
			merge_statusbar_message ("You are not close enough to a resonance node point.");
			return 0;
		}

		/* Update a possible link */
		if (node->link)
			merge_delete_link_node (node, 0);

		/* Get graph data */
		uid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, node->id-1));
		data = gtk_spect_vis_get_data_by_uid (spectvis, uid);

		/* Remove datapoint from graph data */
		X = g_new (gdouble, data->len - 1);
		Y = g_new (ComplexDouble, data->len - 1);
		for (i=0; i<data->len - 1; i++)
		{
			if (fabs (data->X[i] - node->res->frq) < 1e-10)
			{
				data->X[i] = -1;
				i--;
				continue;
			}
			
			X[i] = data->X[i];
			Y[i] = data->Y[i];
		}
		g_free (data->X);
		g_free (data->Y);
		gtk_spect_vis_data_update (spectvis, uid, X, Y, data->len - 1);

		gtk_spect_vis_redraw (spectvis);

		/* Remove node from nodelist */
		g_free (node->res);
		g_ptr_array_remove (
			g_ptr_array_index (merge->nodelist, node->id-1),
			node);

		return 0;
	}
	
	gtk_spect_vis_mark_point (spectvis, *xval, *yval);

	return 0;
}

/* Keep track of the cursor to find interesting points */
gboolean merge_motion_notify (GtkWidget *widget, GdkEventMotion *event)
{
	GtkSpectVis *graph;
	MergeWin *merge = glob->merge;
	MergeNode *nearnode;
	gint xpix, ypix;

	if (!merge->flag)
		return FALSE;

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));
	g_return_val_if_fail (graph, FALSE);

	/* Get a node nearby */
	nearnode = merge_get_nearnode (event->x, event->y, &xpix, &ypix);

	if (!nearnode)
	{
		merge_undisplay_node_selection ();
		merge->nearnode = NULL;
		return FALSE;
	}

	if ((!merge->nearnode) || (merge->nearnode != nearnode))
	{
		merge_undisplay_node_selection ();
		gdk_draw_rectangle (widget->window, graph->cursorgc, FALSE, xpix-3, ypix-3, 6, 6);
		merge->selx = xpix;
		merge->sely = ypix;
	}

	/* remember closest node */
	merge->nearnode = nearnode;

	return FALSE;
}

/* Undisplay the selection box when leaving the area */
gboolean merge_leave_notify (GtkWidget *widget, GdkEventMotion *event)
{
	merge_undisplay_node_selection ();
	return FALSE;
}

gboolean merge_handle_button_press (GtkWidget *widget, GdkEventButton *event)
{
	MergeNode *nearnode;
	gint xpix, ypix;

	g_return_val_if_fail (GTK_IS_SPECTVIS (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	if ((event->button == 1) && (event->state & GDK_CONTROL_MASK))
	{
		nearnode = merge_get_nearnode (event->x, event->y, &xpix, &ypix);
		merge_highlight_width (nearnode);
		return TRUE;
	}

	return FALSE;
}

/* Undisplay the selection box */
static void merge_undisplay_node_selection ()
{
	MergeWin *merge;
	GtkSpectVis *graph;

	if (!glob->merge)
		return;
	merge = glob->merge;

	if (merge->selx < 0)
		return;
	
	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));

	gdk_draw_rectangle (GTK_WIDGET (graph)->window, graph->cursorgc, FALSE, 
			merge->selx-3, merge->sely-3, 6, 6);

	merge->selx = -1;
}
/* Returns the node close to the cursor or NULL */
static MergeNode* merge_get_nearnode (gint xpos, gint ypos, gint *xpix, gint *ypix)
{
	MergeWin *merge = glob->merge;
	GPtrArray *nodes;
	GtkSpectVis *graph;
	GtkSpectVisViewport *view;
	gint i, diff, lastdiff, id = -1;
	gdouble pos;

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));
	g_return_val_if_fail (graph, NULL);
	view = graph->view;

	if ((!view) || (!merge->nodelist->len))
		return NULL;

	/* Get a nodelist id */
	for (pos=0.75; pos < (gdouble)merge->nodelist->len + 0.6; pos+=0.5)
	{
		*ypix = view->graphboxyoff - (gdouble) view->graphboxheight / (view->ymax - view->ymin) * (pos - view->ymin);
		if (abs (*ypix - ypos) < 4)
		{
			id = (gint) floor (pos+0.5) - 1;
			break;
		}
	}

	if (id == -1)
		return NULL;

	/* Get the number of the node in the list */
	nodes = g_ptr_array_index (merge->nodelist, id);
	i = 0;
	diff = 65000;
	lastdiff = 65001;
	while ((lastdiff >= diff) && (i < nodes->len))
	{
		lastdiff = diff;
		*xpix = view->graphboxxoff + (gdouble) view->graphboxwidth / (view->xmax - view->xmin) 
			* (((MergeNode *) g_ptr_array_index (nodes, i))->res->frq - view->xmin);
		diff = abs (*xpix - xpos);

		if (diff < 4)
			break;

		i++;
	}
	
	if (diff >= 4)
		return NULL;

	/* return closest node */
	return (MergeNode *) g_ptr_array_index (nodes, i);
}

/********** Basic link and node handling *************************************/

/* Takes a node and removes links associated with it, determined by type.
 * type == 0: remove both links and reconnect other nodes
 * type == 1: remove link to next node, do not reconnect
 * type == 2: remove link to previous node, no not reconnect
 * Returns the next GList element or NULL if this is not possible. */
static GList* merge_delete_link_node (MergeNode *node, gint type)
{
	GList *linkiter, *newlist1, *nextiter, *previter, *newlist2 = NULL;
	
	g_return_val_if_fail (node, FALSE);

	if (node->link)
	{
		/* Remove node link from graph */
		merge_draw_remove_node (node, type);

		/* Remove link from glob->merge->links as the start may change */
		newlist1 = g_list_first (node->link);
		g_ptr_array_remove (glob->merge->links, newlist1);
		
		/* Find position of node in list (node->link is the head) */
		linkiter = node->link;
		nextiter = g_list_next (linkiter);
		previter = g_list_previous (linkiter);

		if (type == 0)
		{
			/* Remove node from list */
			newlist1 = g_list_delete_link (g_list_first (node->link), linkiter);
			node->link = NULL;
		}
		if ((type == 1) && nextiter)
		{
			/* Cut link to next node */
			linkiter->next = NULL;
			nextiter->prev = NULL;
			newlist2 = nextiter;
		}
		if ((type == 2) && previter)
		{
			/* Cut link to previous node */
			previter->next = NULL;
			linkiter->prev = NULL;
			newlist2 = linkiter;
		}
		
		if (newlist2)
		{
			/* Handle second part of the list */
			if (g_list_length (newlist2) < 2)
			{
				/* The second half of the old list is now too short */
				((MergeNode *) newlist2->data)->link = NULL;
				g_list_free (newlist2);
				nextiter = NULL;
			}
			else
			{
				/* Add new list to merge->links */
				g_ptr_array_add (glob->merge->links, newlist2);

				/* Update link information of each node */
				linkiter = newlist2;
				while (linkiter)
				{
					((MergeNode *) linkiter->data)->link = linkiter;
					linkiter = g_list_next (linkiter);
				}
			}
		}

		/* Has the first list become too short? */
		if (g_list_length (newlist1) < 2)
		{
			/* Yes, only one node left -> delete it*/
			((MergeNode *) newlist1->data)->link = NULL;
			g_list_free (newlist1);
			if (type == 0)
				nextiter = NULL;
		}
		else
		{
			/* Add new list to merge->links */
			g_ptr_array_add (glob->merge->links, newlist1);

			/* Update link information of each node */
			linkiter = newlist1;
			while (linkiter)
			{
				((MergeNode *) linkiter->data)->link = linkiter;
				linkiter = g_list_next (linkiter);
			}
		}

	}
	else
		return NULL;

	return nextiter;
}

/* Removes graphical links associated with a given node and reconnects
 * neighbouring nodes instead if type == 0. */
static void merge_draw_remove_node (MergeNode *node, gint type)
{
	GList *curiter, *previter, *nextiter;
	MergeNode *prevnode = NULL, *nextnode = NULL;
	GtkSpectVis *graph;
	GdkColor color;
	gdouble *X, *Y;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));

	curiter  = node->link;
	previter = g_list_previous (curiter);
	nextiter = g_list_next (curiter);

	if (node->guid1 && (type != 2))
	{
		/* Remove graph to next node */
		gtk_spect_vis_polygon_remove (graph, node->guid1, TRUE);
		nextnode = (MergeNode *) nextiter->data;
		nextnode->guid2 = 0;
		node->guid1 = 0;
	}
	if (node->guid2 && (type != 1))
	{
		/* Remove graph to previous node */
		gtk_spect_vis_polygon_remove (graph, node->guid2, TRUE);
		prevnode = (MergeNode *) previter->data;
		prevnode->guid1 = 0;
		node->guid2 = 0;
	}

	if (prevnode && nextnode && (type == 0))
	{
		/* Node is in the middle of a link -> reconnect */
		color.red   = MERGE_LINK_R;
		color.green = MERGE_LINK_G;
		color.blue  = MERGE_LINK_B;
		
		X = g_new (gdouble, 2);
		Y = g_new (gdouble, 2);

		X[0] = prevnode->res->frq;
		X[1] = nextnode->res->frq;
		Y[0] = prevnode->id + 0.25;
		Y[1] = nextnode->id - 0.25;

		prevnode->guid1 = gtk_spect_vis_polygon_add (graph, X, Y, 2, color, 'l');
		nextnode->guid2 = ((MergeNode *) curiter->data)->guid1;
	}
}

/* Remove all links from the graph */
static void merge_draw_remove_all ()
{
	GtkSpectVis *graph;
	gint i;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));
	
	for (i=0; i<glob->merge->links->len; i++)
		merge_draw_remove ((GList *) g_ptr_array_index (glob->merge->links, i));
}

/* Remove one link from the graph */
static void merge_draw_remove (GList *link)
{
	GtkSpectVis *graph;
	MergeNode *node;
	GList *curiter, *previter, *nextiter;
	
	if (!link)
		return;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));
	
	curiter = g_list_first (link);
	while (curiter)
	{
		node = (MergeNode *) curiter->data;

		curiter  = node->link;
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

	color.red   = MERGE_LINK_R;
	color.green = MERGE_LINK_G;
	color.blue  = MERGE_LINK_B;
	
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

/* Compare two gdouble values */
static gint merge_reslist_compare (gconstpointer a_in, gconstpointer b_in)
{
	const gdouble a = *(gdouble *) a_in;
	const gdouble b = *(gdouble *) b_in;
	
	if (a < b)
		return -1;
	else if (a > b)
		return +1;
	else
		return 0;
}

/* Return TRUE if a node with id is found in list */
static gboolean merge_is_id_in_list (guint id, GList *list)
{
	GList *iter;

	if ((!list) || (id == 0))
		return FALSE;

	iter = g_list_first (list);
	while (iter)
	{
		if (id == ((MergeNode *) iter->data)->id)
			return TRUE;
		iter = g_list_next (iter);
	}

	return FALSE;
}

/* Calculates the average resonance frequency from a link list */
static gdouble merge_avg_frq_from_link (GList *list)
{
	gdouble frq = 0.0, len;
	
	g_return_val_if_fail (list, -1);
	
	list = g_list_first (list);
	len = (gdouble) g_list_length (list);
	while (list)
	{
		frq += ((MergeNode *) list->data)->res->frq;
		list = g_list_next (list);
	}
	frq /= len;

	return frq;
}

/* Compile the merges resonance list and return it as a GArray */
static GArray* merge_gather_reslist ()
{
	MergeWin *merge = glob->merge;
	MergeNode *node;
	GPtrArray *list;
	GArray *reslist;
	gdouble frq;
	guint i, j;

	reslist = g_array_new (FALSE, FALSE, sizeof (gdouble));

	for (i=0; i<merge->nodelist->len; i++)
	{
		list = g_ptr_array_index (merge->nodelist, i);
		for (j=0; j<list->len; j++)
		{
			node = (MergeNode *) g_ptr_array_index (list, j);
			if ((node->link) && (node->link->prev))
				/* Node in the middle of a link -> ignore */
				continue;
			
			if (node->link)
				/* Node at the start of a link -> average */
				frq = merge_avg_frq_from_link (node->link);
			else
				/* Single node -> take it */
				frq = node->res->frq;

			g_array_append_val (reslist, frq);
		}
	}

	g_array_sort (reslist, merge_reslist_compare);

	return reslist;
}

/* Mark the width of the resonance at node or disable mark if node == NULL */
static void merge_highlight_width (MergeNode *node)
{
	static guint baruid = 0;
	GtkSpectVis *graph;
	GdkColor color;
	gdouble width;
	
	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));
	g_return_if_fail (graph);

	if (baruid)
	{
		gtk_spect_vis_remove_bar (graph, baruid);
		baruid = 0;
	}

	if (node)
	{
		color.green = 62000;
		color.blue  = color.red = 20000;

		width = node->res->width / 1.5;
		if (width > 1e9)
		{
			width = 1e9;
			color.red   = 52000;
			color.green = 10000;
			color.blue  = 40000;
		}

		baruid = gtk_spect_vis_add_bar (graph, node->res->frq, width, color);
	}

	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
}

/********** Automatic merger *************************************************/

/* Does the main automatic merge voodoo */
static void merge_automatic_merge ()
{
	MergeWin *merge;
	GPtrArray *curnodelist, *trynodelist;
	GList *candidates, *canditer, *itertmp;
	Resonance *res;
	gint i, j, k, l;
	gdouble minwidth, avgfrq, deltafrq, diff;
	gboolean flag = FALSE;
	MergeNode *node;

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
			
			/* Remove resonances that have another nearer neighbour in the other datasets */
			canditer = candidates;
			while (canditer)
			{
				node = (MergeNode *) canditer->data;

				/* Get closest frq difference in candidates list */
				deltafrq = 1e50;
				itertmp = candidates;
				do {
					if (itertmp == canditer)
						continue;
					
					diff = fabs (node->res->frq - 
							((MergeNode *) itertmp->data)->res->frq);
					
					if (diff < deltafrq)
						deltafrq = diff;
				} while ((itertmp = g_list_next (itertmp)));
				deltafrq -= 1e-10; /* prevent rounding errors */

				/* Is there another closer neighbour? */
				for (k=0; k<merge->nodelist->len; k++)
				{
					if (k+1 == node->id)
						continue;
					
					trynodelist = (GPtrArray *) g_ptr_array_index (merge->nodelist, k);
					for (l=0; l<trynodelist->len; l++)
					{
						if (fabs (node->res->frq - ((MergeNode *) g_ptr_array_index (trynodelist, l))->res->frq) < deltafrq)
						{
							deltafrq = -1.0;
							l = trynodelist->len;
							k = merge->nodelist->len;
						}
					}
				}

				if (deltafrq < -0.5)
				{
					/* Found another resonance which lies closer */
					itertmp = canditer;
					canditer = g_list_next (canditer);
					candidates = g_list_delete_link (candidates, itertmp);
				}
				else
					canditer = g_list_next (canditer);
				
			}

			if ((candidates) && (g_list_length (candidates) > 1))
			{
				minwidth = merge_get_minwidth (candidates) * 0.5;
				avgfrq   = merge_get_avgfrq   (candidates);

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
							/* Need to recalculate minwidth */
							minwidth = merge_get_minwidth (candidates) * 0.5;

						avgfrq = merge_get_avgfrq (candidates);
					}
				}
				while ((flag) || (canditer = g_list_next (canditer)));
			}

			if ((candidates) && (g_list_length (candidates) > 1))
			{
				/* Mark candidates as linked */
				canditer = candidates;
				do 
					((MergeNode *) canditer->data)->link = canditer;
				while ((canditer = g_list_next (canditer)));

				/* Sort the list */
				candidates = g_list_sort (candidates, merge_link_compare);
				
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
		/* "i" now points to the resonance with a bigger * frequency or to the end of the list */

		if (i < curnodelist->len)
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

/********** Automatic merger *************************************************/

/* Remove the latest message from the statusbar. */
static gint merge_statusbar_message_remove (gpointer data)
{
	GtkWidget *statusbar = glade_xml_get_widget (glob->merge->xmlmerge, "merge_statusbar");
	gtk_statusbar_pop (GTK_STATUSBAR (statusbar), GPOINTER_TO_INT (data));

	return FALSE;
}

/* Add a new message to the statusbar which will be removed after 5 sec. */
void merge_statusbar_message (gchar *format, ...)
{
	va_list ap;
	gchar *message;
	gint context_id;
	GtkWidget *statusbar = glade_xml_get_widget (glob->merge->xmlmerge, "merge_statusbar");

	va_start(ap, format);
	message = g_strdup_vprintf (format, ap);
	va_end(ap);

	context_id = gtk_statusbar_get_context_id (GTK_STATUSBAR (statusbar), message);
	gtk_statusbar_push (GTK_STATUSBAR (statusbar), context_id, message);

	g_free (message);

	g_timeout_add (5*1000, merge_statusbar_message_remove, GINT_TO_POINTER (context_id));
}
