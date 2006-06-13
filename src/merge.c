#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gdk/gdkkeysyms.h>

#include "merge.h"
#include "merge_util.h"
#include "structs.h"
#include "helpers.h"
#include "processdata.h"
#include "gtkspectvis.h"
#include "callbacks.h"
#include "loadsave.h"

#define MERGE_FLAG_ADD    (1 << 0)
#define MERGE_FLAG_DEL    (1 << 1)
#define MERGE_FLAG_MARK   (1 << 2)
#define MERGE_FLAG_DELRES (1 << 3)
#define MERGE_FLAG_MEAS   (1 << 4)

extern GlobalData *glob;

/* Forward declarations */
static gint merge_read_resonances (gchar *selected_filename, const gchar *label, GPtrArray **reslist, gchar **datafilename);
static void merge_automatic_merge ();
static GList* merge_get_closest (gdouble frq, gint depth);
static gdouble merge_get_minwidth (GList *cand);
static gdouble merge_get_avgfrq (GList *cand);
static gboolean merge_load_file_helper (gchar *text);
static gboolean merge_load_del_helper (gchar *text);
static gboolean merge_load_link_helper (gchar **tokens);

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
	glob->merge->origlen      = g_ptr_array_new ();
	glob->merge->links        = g_ptr_array_new ();
	glob->merge->spectra      = g_ptr_array_new ();
	glob->merge->selx         = -1;

	xmlmerge = glade_xml_new (GLADEFILE, "merge_win", NULL);
	glade_xml_signal_autoconnect (xmlmerge);
	glob->merge->xmlmerge = xmlmerge;

	treeview = glade_xml_get_widget (xmlmerge, "merge_treeview");

	glob->merge->store = gtk_list_store_new (MERGE_N_COLUMNS,
			G_TYPE_UINT,
			G_TYPE_STRING,
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

	/* SHORTNAME_COL */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
			"resonance list", renderer,
			"text", MERGE_SHORTNAME_COL,
			NULL);
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_column_set_sort_column_id (column, -1);
	gtk_tree_view_column_set_expand (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	/* NUMRES_COL */
	renderer = gtk_cell_renderer_text_new ();
	column = gtk_tree_view_column_new_with_attributes (
			"resonances", renderer,
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
	GtkSpectVis *graph, *spectgraph;
	gint i, j, uid;
	
	if (!glob->merge)
		return;

	merge = glob->merge;

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));
	spectgraph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spect_graph"));

	/* Free polygon data by removing it */
	merge_draw_remove_all ();

	merge_highlight_width (NULL);
	merge_spect_graph_show_node (NULL);
	merge_show_resonance_info (NULL);
	
	for (i=0; i<merge->nodelist->len; i++)
	{
		/* Free Resonance nodes */
		curnodelist = g_ptr_array_index (merge->nodelist, i);
		for (j=0; j<curnodelist->len; j++) 
			g_free (((MergeNode *) g_ptr_array_index (curnodelist, j))->res);
		g_ptr_array_foreach (curnodelist, (GFunc) g_free, NULL);
		g_ptr_array_free (curnodelist, TRUE);

		/* Free node graph data */
		uid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, i));
		data = gtk_spect_vis_get_data_by_uid (graph, uid);
		if (data)
		{
			g_free (data->X);
			g_free (data->Y);
		}

		/* Free spectra graph data */
		data = gtk_spect_vis_get_data_by_uid (spectgraph, uid);
		if (data)
		{
			g_free (data->X);
			g_free (data->Y);
		}
	}
	g_ptr_array_free (merge->nodelist, TRUE);

	/* Free arrays with filenames, uids and origlen*/
	g_ptr_array_foreach (merge->datafilename, (GFunc) g_free, NULL);
	g_ptr_array_free (merge->datafilename, TRUE);
	g_ptr_array_free (merge->graphuid, TRUE);
	g_ptr_array_free (merge->origlen, TRUE);

	/* Free links between nodes */
	g_ptr_array_foreach (merge->links, (GFunc) g_list_free, NULL);
	g_ptr_array_free (merge->links, TRUE);
	
	/* Destroy widgets */
	if ((merge->xmlmerge) && (!free_only))
	{
		gtk_widget_destroy (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));
		gtk_widget_destroy (glade_xml_get_widget (merge->xmlmerge, "merge_spect_graph"));
		gtk_widget_destroy (glade_xml_get_widget (merge->xmlmerge, "merge_win"));
	}

	/* Clear store */
	if (merge->store)
	{
		gtk_list_store_clear (merge->store);
		if (!free_only)
			merge->store = NULL;
	}

	g_free (merge->savefile);
	merge->savefile = NULL;
	g_free (merge->section);
	merge->section = NULL;

	if (!free_only)
	{
		g_free (merge);
		glob->merge = NULL;
	}
}

/********** Load and save operations *****************************************/

/* Reads *label in *selected_filename and adds all resonances to **reslist and
 * the name of the datafile to **datafilename.
 * Returns the number of resonances found or -1 on failure.
 */
#define merge_resonancefile_cleanup	g_ptr_array_foreach (lines, (GFunc) g_free, NULL); \
	                        	g_ptr_array_free (lines, TRUE); \
					g_free (text);\
					g_free (command);

static gint merge_read_resonances (gchar *selected_filename, const gchar *label, GPtrArray **reslist, gchar **datafilename)
{
	gchar *line, *command, *text;
	gdouble frq, wid, amp, phas;
	gdouble frqerr, widerr, amperr, phaserr;
	gint numres=0, pos=0, i;
	Resonance *resonance=NULL;
	GPtrArray *lines;

	/* Get section */
	lines = ls_read_section (selected_filename, (gchar *) label, '=');

	if (!lines)
		return -1;

	if (lines->len == 0)
	{
		g_ptr_array_free (lines, TRUE);
		return -1;
	}

	text = g_new0 (gchar, 256);
	command = g_new0 (gchar, 256);

	*reslist = g_ptr_array_new ();

	for (pos=0; pos<lines->len; pos++)
	{
		line = (gchar *) g_ptr_array_index (lines, pos);

		/* Comment lines start with either '%' or '#' */
		if ((*line == '%') || (*line == '#') || (strlen(line) == 0))
			continue;

		/* Actually parse the section */

		/* Begin "Florian Schaefer" style */
		if (g_str_has_prefix (line, "file"))
		{
			if (sscanf(line, "%250s\t%250s", command, text) == 2)
			{
				/* text may not hold the complete filename if it
				 * contained spaces. */
				if (!strncmp(command, "file", 200)) 
					*datafilename = filename_make_absolute (line+5, selected_filename);
			}

			continue;
		}
		
		i = sscanf(line, "%250s\t%lf\t%lf\t%lf\t%lf%lf\t%lf\t%lf\t%lf", 
				command, &frq, &wid, &amp, &phas, &frqerr, &widerr, &amperr, &phaserr);
		switch (i) {
			case 1:
			  break;
			case 5: /* Resonance data */

			  /* line with a timestamp? */
			  if (!strncmp(command, "date", 200)) 
			  {
				  /* nothing to be done yet */
				  break;
			  }
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
		/* End "Florian Schaefer" style */
	}

	merge_resonancefile_cleanup;

	return numres;
}
#undef merge_resonancefile_cleanup

/* Store all link information into a file */
void merge_save_links (FILE *datafile, gchar *filename, gchar *section, gchar *newline)
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->merge->store);
	GtkTreeIter iter;
	MergeWin *merge = glob->merge;
	GPtrArray *list;
	gchar *text, *name;
	gint i, j, k, origlen, curnum, nextnum;
	GList *linkiter;

	/* Write section name */
	fprintf (datafile, ">%s%s", section, newline);

	text = get_timestamp ();
	fprintf (datafile, "date\t%s%s", text, newline);
	g_free (text);

	/* Include gwf-files involved */
	gtk_tree_model_get_iter_first (model, &iter);
	do
	{
		gtk_tree_model_get (model, &iter, MERGE_LIST_COL, &text, -1);
		if (glob->prefs->relative_paths)
		{
			if (!(name = filename_make_relative (text, filename)) )
				name = g_strdup (text);
			fprintf (datafile, "file\t%s%s", name, newline);
			g_free (name);
		}
		else
			fprintf (datafile, "file\t%s%s", text, newline);
	}
	while (gtk_tree_model_iter_next (model, &iter));

	/* Inform about deleted resonances */
	for (i=0; i<merge->nodelist->len; i++)
	{
		list = g_ptr_array_index (merge->nodelist, i);
		
		curnum  = ((MergeNode *) g_ptr_array_index (list, 0))->num;
		if (curnum > 0)
			/* Deleted at the beginning */
			for (j=0; j<curnum; j++)
				fprintf (datafile, "del\t%d,%d%s", i+1, j, newline);
		
		for (j=0; j<list->len-1; j++)
		{
			k=0;
			curnum  = ((MergeNode *) g_ptr_array_index (list, j))->num;
			nextnum = ((MergeNode *) g_ptr_array_index (list, j+1))->num;
			while (curnum + 1 + k < nextnum)
			{
				/* Deleted in the middle */
				k++;
				fprintf (datafile, "del\t%d,%d%s", i, curnum+k, newline);
			}
		}

		curnum  = ((MergeNode *) g_ptr_array_index (list, list->len-1))->num;
		origlen = GPOINTER_TO_INT (g_ptr_array_index (merge->origlen, i));
		if (curnum != origlen)
			/* Deleted at the end */
			for (j=curnum+1; j<origlen; j++)
				fprintf (datafile, "del\t%d,%d%s", i, j, newline);
	}

	/* Save links */
	for (i=0; i<merge->links->len; i++)
	{
		fprintf (datafile, "link\t");
		linkiter = (GList *) g_ptr_array_index (merge->links, i);
		while (linkiter)
		{
			fprintf (datafile, "%d,%d", 
					((MergeNode *) linkiter->data)->id - 1, 
					((MergeNode *) linkiter->data)->num);
			linkiter = g_list_next (linkiter);
			if (linkiter)
				fprintf (datafile, " ");
		}
		fprintf (datafile, "%s", newline);
	}
}

#define merge_load_links_cleanup	g_strfreev (tokens); \
					g_ptr_array_foreach (lines, (GFunc) g_free, NULL); \
					g_ptr_array_free (lines, TRUE);

/* Restore all link information from a file */
static gboolean merge_load_links (gchar *selected_filename, const gchar *label)
{
	GPtrArray *lines;
	gchar *line, *text1, *text2;
	gchar **tokens;
	gint pos;

	/* Get section */
	lines = ls_read_section (selected_filename, (gchar *) label, '>');

	if (!lines)
		return FALSE;

	if (lines->len == 0)
	{
		g_ptr_array_free (lines, TRUE);
		return FALSE;
	}

	for (pos=0; pos<lines->len; pos++)
	{
		line = (gchar *) g_ptr_array_index (lines, pos);

		/* Comment lines start with either '%' or '#' */
		if ((*line == '%') || (*line == '#') || (strlen (line) == 0)) continue;

		tokens = g_strsplit_set (line, " \t", -1);

		if (!(tokens+1))
		{
			dialog_message ("Could not parse line %d of section.", pos+1);
			merge_load_links_cleanup;
			return FALSE;
		}
		
		if (!(strcmp (tokens[0], "file")))
		{
			text1 = g_strjoinv (" ", tokens+1);
			text2 = filename_make_absolute (text1, selected_filename);
			if (!merge_load_file_helper (text2))
			{
				dialog_message ("Error while processing line %d of section.", pos+1);
				//merge_load_links_cleanup;
				//g_free (text);
				//return FALSE;
			}
			g_free (text1);
			g_free (text2);
		}

		if (!(strcmp (tokens[0], "del")))
		{
			if ((!tokens[1]) || (tokens[1] && tokens[2]))
			{
				dialog_message ("Could not parse line %d of section.", pos+1);
				merge_load_links_cleanup;
				return FALSE;
			}
			if (!merge_load_del_helper (tokens[1]))
			{
				dialog_message ("Error while processing line %d of section.", pos+1);
				//merge_load_links_cleanup;
				//return FALSE;
			}
		}

		if (!(strcmp (tokens[0], "link")))
		{
			if (!merge_load_link_helper (tokens+1))
			{
				dialog_message ("Error while processing line %d of section.", pos+1);
				//merge_load_links_cleanup;
				//return FALSE;
			}
		}

		if (!(strcmp(tokens[0], "date")))
		{
			/* do nothing yet */
		}

		g_strfreev (tokens);
	}

	return TRUE;
}
#undef merge_load_links_cleanup

/* Helper for merge_load_links() to parse "file" commands */
static gboolean merge_load_file_helper (gchar *text)
{
	gchar *filename, *section;
	gchar *datafilename = NULL;
	GPtrArray *reslist = NULL;
	gint i;
	
	g_return_val_if_fail (text, FALSE);

	/* Separate the section name */
	for (i=strlen(text)-1; i>0; i--)
	{
		if (text[i] == ':')
			break;
	}

	if ((text[i] != ':') || (i == strlen(text)-1))
		return FALSE;

	section = text + i+1;
	text[i] = '\0';
	filename = text;

	if (merge_read_resonances (filename, section, &reslist, &datafilename) < 0)
	{
		g_free (datafilename);
		g_ptr_array_free (reslist, TRUE);
		return FALSE;
	}

	/* Error, no resonances found in section */
	if (reslist->len == 0)
	{
		dialog_message ("Cannot add the empty resonance list %s:%s.", filename, section);
		g_free (datafilename);
		g_ptr_array_free (reslist, TRUE);
		return FALSE;
	}

	text[i] = ':';
	g_ptr_array_sort (reslist, param_compare);
	merge_add_reslist (reslist, datafilename, g_strdup (text));

	return TRUE;
}

/* Helper for merge_load_links() to parse "del" commands */
static gboolean merge_load_del_helper (gchar *text)
{
	gint id, num, i;
	GPtrArray *nodes;
	MergeNode *node = NULL;
	
	g_return_val_if_fail (text, FALSE);
	g_return_val_if_fail (glob->merge->nodelist, FALSE);

	if (sscanf (text, "%d,%d", &id, &num) != 2)
		return FALSE;

	if (id >= glob->merge->nodelist->len)
		return FALSE;

	nodes = g_ptr_array_index (glob->merge->nodelist, id);

	for (i=0; i<nodes->len; i++)
	{
		node = (MergeNode *) g_ptr_array_index (nodes, i);
		if (node->num == num)
			break;
	}

	if ((!node) || (node->num != num))
		return FALSE;

	merge_delres (node);

	return TRUE;
}

/* Helper for merge_load_links() to parse "link" commands */
static gboolean merge_load_link_helper (gchar **tokens)
{
	gint id, num, i, j;
	GPtrArray *nodes;
	MergeNode *node = NULL;
	GList *link = NULL;

	g_return_val_if_fail (tokens, FALSE);
	g_return_val_if_fail (glob->merge->nodelist, FALSE);
	g_return_val_if_fail (glob->merge->links, FALSE);

	i = 0;
	while (tokens[i])
	{

		if (sscanf (tokens[i], "%d,%d", &id, &num) != 2)
			return FALSE;

		if (id >= glob->merge->nodelist->len)
			return FALSE;

		nodes = g_ptr_array_index (glob->merge->nodelist, id);

		/* find correct node */
		for (j=0; j<nodes->len; j++)
		{
			node = (MergeNode *) g_ptr_array_index (nodes, j);
			if (node->num == num)
				break;
		}

		if ((!node) || (node->num != num) || (node->link))
			return FALSE;

		link = g_list_append (link, node);
		
		i++;
	}

	if (g_list_length (link) < 2)
	{
		g_list_foreach (link, (GFunc) g_free, NULL);
		g_list_free (link);
		return FALSE;
	}

	link = g_list_sort (link, merge_link_compare);
	merge_draw_link (link);

	g_ptr_array_add (glob->merge->links, link);

	while (link)
	{
		((MergeNode *) link->data)->link = link;
		link = g_list_next (link);
	}

	return TRUE;
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
	GtkSpectVis *graph, *spectgraph;
	gint i, uid;

	graph      = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));
	spectgraph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spect_graph"));

	for (i=0; i<merge->graphuid->len; i++)
	{
		uid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, i));
		gtk_spect_vis_data_remove (graph, uid);	     /* remove from node graph */
		gtk_spect_vis_data_remove (spectgraph, uid); /* remove from spectrum graph */
	}

	merge_draw_remove_all ();
	
	merge_purge (TRUE);
	merge_undisplay_node_selection ();

	glob->merge->nodelist     = g_ptr_array_new ();
	glob->merge->datafilename = g_ptr_array_new ();
	glob->merge->graphuid     = g_ptr_array_new ();
	glob->merge->origlen      = g_ptr_array_new ();
	glob->merge->links        = g_ptr_array_new ();

	gtk_spect_vis_redraw (graph);

	return TRUE;
}

/* Ask for a resonance list and add it */
gboolean on_merge_add_list_activate (GtkWidget *button)
{
	gchar *filename, *section = NULL, *path = NULL;
	gchar *name, *datafilename = NULL;
	GList *sections = NULL;
	GPtrArray *reslist = NULL;

	/* Get gwf filename */
	path = get_defaultname (NULL);
	filename = get_filename ("Select file with resonance list", path, 1);
	g_free (path);

	if (!filename)
		return FALSE;

	/* Get section in file */
	sections = ls_get_sections (filename, "=");
	if (!sections)
	{
		g_free (filename);
		dialog_message ("No appropriate sections found in this file.");
		return FALSE;
	}
	
	section = ls_select_section (sections, glob->section);
	g_list_foreach (sections, (GFunc) g_free, NULL);
	g_list_free (sections);

	if (!section)
	{
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

	/* At least one resonance is needed */
	if (reslist->len == 0)
	{
		g_free (filename);
		g_free (section);
		g_ptr_array_free (reslist, TRUE);
		g_free (datafilename);
		dialog_message ("Cannot add an empty resonance list.");
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

/* Open a whole link setup */
gboolean on_merge_open_activate (GtkWidget *button)
{
	gchar *path = NULL;
	gchar *filename = NULL;
	gchar *section = NULL;
	GList *sections = NULL;
	GtkSpectVis *graph;

	/* Get filename */
	path = get_defaultname (NULL);
	filename = get_filename ("Select resonancefile", path, 1);
	g_free (path);
	if (!filename)
		return FALSE;

	/* Get list of sections in file */
	sections = ls_get_sections (filename, ">");
	if (!sections)
	{
		g_free (filename);
		dialog_message ("No appropriate sections found in this file.");
		return FALSE;
	}
	
	/* Get final section */
	section = ls_select_section (sections, glob->merge->section);
	g_list_foreach (sections, (GFunc) g_free, NULL);
	g_list_free (sections);
	if (!section)
	{
		g_free (filename);
		return FALSE;
	}

	/* Tabula rasa */
	on_merge_new_activate (NULL);

	/* Read file */
	if (merge_load_links (filename, section))
	{
		g_free (glob->merge->savefile);
		glob->merge->savefile = filename;
		g_free (glob->merge->section);
		glob->merge->section = section;

		graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));
		gtk_spect_vis_redraw (graph);
		glob->merge->selx = -1;
		
		merge_statusbar_message ("Session file restored");
	}
	else
	{
		g_free (filename);
		g_free (section);
		merge_statusbar_message ("Could not load session file");
	}

	return FALSE;
}

/* Save whole link setup */
gboolean on_merge_save_as_activate (GtkWidget *button)
{
	gchar *defaultname, *filename, *newsection;

	if (!glob->merge->nodelist->len)
	{
		dialog_message ("There is nothing to be saved.");
		return FALSE;
	}

	newsection = ls_input_section (glob->merge->section);
	if (!newsection)
		return FALSE;

	if (glob->merge->savefile)
		defaultname = g_strdup (glob->merge->savefile);
	else
		defaultname = get_defaultname (".gwf");

	filename = get_filename ("Select link session file", defaultname, 0);
	g_free (defaultname);

	if (!filename)
		return FALSE;

	if (ls_save_file (filename, newsection, '>', merge_save_links))
	{
		/* success */
		merge_statusbar_message ("Save operation successful");
		g_free (glob->merge->section);
		g_free (glob->merge->savefile);
		glob->merge->section = newsection;
		glob->merge->savefile = filename;
	}
	else
	{
		/* failure */
		merge_statusbar_message ("Save operation aborted");
		g_free (newsection);
		g_free (filename);
	}

	return TRUE;
}

/* Save whole link setup (under an old name) */
gboolean on_merge_save_activate (GtkWidget *button)
{
	if (!glob->merge->nodelist->len)
	{
		dialog_message ("There is nothing to be saved.");
		return FALSE;
	}
	
	if (!glob->merge->section)
		on_merge_save_as_activate (NULL);
	else 
		if (ls_save_file_exec (glob->merge->savefile, glob->merge->section, '>', 2, merge_save_links))
			merge_statusbar_message ("Save operation successful");

	return TRUE;
}

/* Export merged resonance list */
gboolean on_merge_export_activate (GtkWidget *button)
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
	gint id = -1, curid, uid, i, j;
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
	for (i=0; i < merge->nodelist->len; i++)
	{
		curnodelist =  g_ptr_array_index (merge->nodelist, i);
		for (j=0; j<curnodelist->len; j++)
		{
			linknode = (MergeNode *) g_ptr_array_index (curnodelist, j);

			if (linknode->id == id)
				merge_delete_link_node (linknode, 0);
			else if (linknode->id > id) 
					linknode->id--;
		}
	}

	/* id will now be the "real" position in the pointer arrays */
	id--;

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));

	/* Remove graph and free data */
	uid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, id));
	data = gtk_spect_vis_get_data_by_uid (graph, uid);
	if (data)
	{
		g_free (data->X);
		g_free (data->Y);
	}
	gtk_spect_vis_data_remove (graph, uid);

	/* Remove spectrum graph */
	merge_remove_spect_graph (uid);

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
	g_ptr_array_remove_index (merge->origlen, id);

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
	glob->merge->selx = -1;

	return TRUE;
}

/* Add a single link via user input */
gboolean on_merge_add_link_activate (GtkWidget *button)
{
	gint xpix, ypix;
	
	glob->merge->flag |= MERGE_FLAG_ADD;
	glob->merge->flag &= ~MERGE_FLAG_DEL;
	glob->merge->flag &= ~MERGE_FLAG_MARK;
	glob->merge->flag &= ~MERGE_FLAG_DELRES;
	glob->merge->flag &= ~MERGE_FLAG_MEAS;

	glob->merge->nearnode = merge_get_nearnode (-1, -1, &xpix, &ypix, TRUE);
	if (glob->merge->nearnode)
		merge_display_node_selection (xpix, ypix);
	
	return TRUE;
}

/* Delete a single link via user input */
gboolean on_merge_delete_link_activate (GtkWidget *button)
{
	gint xpix, ypix;
	
	glob->merge->flag |= MERGE_FLAG_DEL;
	glob->merge->flag &= ~MERGE_FLAG_ADD;
	glob->merge->flag &= ~MERGE_FLAG_MARK;
	glob->merge->flag &= ~MERGE_FLAG_DELRES;
	glob->merge->flag &= ~MERGE_FLAG_MEAS;

	glob->merge->nearnode = merge_get_nearnode (-1, -1, &xpix, &ypix, TRUE);
	if (glob->merge->nearnode)
		merge_display_node_selection (xpix, ypix);
	
	return TRUE;
}

/* Add a single link via user input */
gboolean on_merge_measure_distance_activate (GtkWidget *button)
{
	glob->merge->flag |= MERGE_FLAG_MEAS;
	glob->merge->flag &= ~MERGE_FLAG_ADD;
	glob->merge->flag &= ~MERGE_FLAG_DEL;
	glob->merge->flag &= ~MERGE_FLAG_MARK;
	glob->merge->flag &= ~MERGE_FLAG_DELRES;

	merge_statusbar_message ("Mark first frequency on the graph");
	
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
	glob->merge->selx = -1;

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
	glob->merge->flag &= ~MERGE_FLAG_MEAS;
	
	return TRUE;
}

/* Remove the next selected resonance from nodelist */
gboolean on_merge_remove_resonance_activate (GtkWidget *button)
{
	gint xpix, ypix;

	glob->merge->flag |= MERGE_FLAG_DELRES;
	glob->merge->flag &= ~MERGE_FLAG_ADD;
	glob->merge->flag &= ~MERGE_FLAG_MARK;
	glob->merge->flag &= ~MERGE_FLAG_DEL;
	glob->merge->flag &= ~MERGE_FLAG_MEAS;

	glob->merge->nearnode = merge_get_nearnode (-1, -1, &xpix, &ypix, FALSE);
	if (glob->merge->nearnode)
		merge_display_node_selection (xpix, ypix);
	
	return TRUE;
}

/* Zooming, etc. */
void merge_handle_viewport_changed (GtkSpectVis *spectvis, gchar *zoomtype)
{
	if (*zoomtype == 'a')
		merge_zoom_x_all ();
	
	gtk_spect_vis_redraw (spectvis);
	glob->merge->selx = -1;
}

/* Handle click on graph */
gint merge_handle_value_selected (GtkSpectVis *spectvis, gdouble *xval, gdouble *yval)
{
	static MergeNode *node1 = NULL;
	static gdouble measure = -1.0;
	MergeNode *node;
	MergeWin *merge;
	gchar *text;
	
	g_return_val_if_fail (glob->merge, 0);
	merge = glob->merge;
	
	if (merge->flag & MERGE_FLAG_ADD)
	{
		node = (MergeNode *) merge->nearnode;
		if (!node)
		{
			merge->flag &= ~MERGE_FLAG_ADD;
			merge_undisplay_node_selection ();
			merge_statusbar_message ("You are not close enough to a resonance node point.");
			node1 = NULL;
			return 0;
		}

		if (!node1)
		{
			if ((node->link) && (node->link->prev) && (node->link->next))
			{
				merge->flag &= ~MERGE_FLAG_ADD;
				merge_undisplay_node_selection ();
				merge_statusbar_message ("No free links on this node left.");
			}
			else
				node1 = node;

			return 0;
		}
		else
		{
			merge->flag &= ~MERGE_FLAG_ADD;
			merge_undisplay_node_selection ();
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
				merge_link_two_nodes (node, node1);
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
		merge_undisplay_node_selection ();
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
		merge_undisplay_node_selection ();
		node = (MergeNode *) merge->nearnode;

		merge_highlight_width (node);
		merge_spect_graph_show_node (node);
		merge_show_resonance_info (node);
		return 0;
	}

	if (merge->flag & MERGE_FLAG_DELRES)
	{
		merge->flag &= ~MERGE_FLAG_DELRES;
		merge_undisplay_node_selection ();
		node = (MergeNode *) merge->nearnode;
		if (!node)
		{
			merge_statusbar_message ("You are not close enough to a resonance node point.");
			return 0;
		}

		merge_delres (node);
		merge_highlight_width (NULL);
		merge_spect_graph_show_node (NULL);
		merge_show_resonance_info (NULL);
		return 0;
	}

	if (merge->flag & MERGE_FLAG_MEAS)
	{
		if (measure < 0.0)
		{
			measure = *xval;
			merge_statusbar_message ("Mark second frequency on the graph");
			return 0;
		}
		else
		{
			merge->flag &= ~MERGE_FLAG_DELRES;
			measure =  fabs (*xval - measure);

			if (measure > 1e9)
			{
				measure /= 1e9;
				text = g_strdup_printf ("GHz");
			}
			else if (measure > 1e6)
			{
				measure /= 1e6;
				text = g_strdup_printf ("MHz");
			}
			else if (measure > 1e3)
			{
				measure /= 1e3;
				text = g_strdup_printf ("kHz");
			}
			else
				text = g_strdup_printf ("Hz");

			dialog_message ("The distance between the two selected points is:\n%.3f %s", 
					measure, text);

			g_free (text);
			measure = -1.0;
			return 0;
		}
	}
	else
	{
		merge->flag &= ~MERGE_FLAG_DELRES;
		measure = -1.0;
		return 0;
	}
	
	gtk_spect_vis_mark_point (spectvis, *xval, *yval);

	return 0;
}

/* Keep track of the cursor to find interesting points */
gboolean merge_motion_notify (GtkWidget *widget, GdkEventMotion *event)
{
	MergeWin *merge = glob->merge;
	MergeNode *nearnode;
	gint xpix, ypix;

	if ((!merge->flag) || (merge->flag & MERGE_FLAG_MEAS))
		return FALSE;

	/* Get a node nearby */
	if ((merge->flag & MERGE_FLAG_ADD) || (merge->flag & MERGE_FLAG_DEL))
		nearnode = merge_get_nearnode (event->x, event->y, &xpix, &ypix, TRUE);
	else
		nearnode = merge_get_nearnode (event->x, event->y, &xpix, &ypix, FALSE);

	if (!nearnode)
	{
		merge_undisplay_node_selection ();
		merge->nearnode = NULL;
		return FALSE;
	}

	if ((!merge->nearnode) || (merge->nearnode != nearnode) || (merge->selx < 0))
		merge_display_node_selection (xpix, ypix);

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

/* Highlight resonance on Ctrl + first mouse button */
gboolean merge_handle_button_press (GtkWidget *widget, GdkEventButton *event)
{
	MergeNode *nearnode;
	gint xpix, ypix;

	g_return_val_if_fail (GTK_IS_SPECTVIS (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	if ((event->button == 1) && (event->state & GDK_CONTROL_MASK))
	{
		nearnode = merge_get_nearnode (event->x, event->y, &xpix, &ypix, FALSE);
		merge_highlight_width (nearnode);
		merge_spect_graph_show_node (nearnode);
		merge_show_resonance_info (nearnode);
		return TRUE;
	}

	return FALSE;
}

/* View spectrum graph in normal or logarithmic scale */
gboolean merge_scale_change (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkCheckMenuItem *item;
	GtkSpectVis *graph;

	if ((!glob->merge) || (!glob->merge->xmlmerge))
		return FALSE;

	item = GTK_CHECK_MENU_ITEM (
		glade_xml_get_widget (glob->merge->xmlmerge, "merge_normal_scale"));
	
	graph = GTK_SPECTVIS (
		glade_xml_get_widget (glob->merge->xmlmerge, "merge_spect_graph"));

	if (gtk_check_menu_item_get_active (item))
		gtk_spect_vis_set_displaytype (graph, 'a');
	else
		gtk_spect_vis_set_displaytype (graph, 'l');
	
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
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

	glob->merge->selx = -1;
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
