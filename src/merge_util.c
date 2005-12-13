#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "merge.h"
#include "structs.h"
#include "helpers.h"
#include "processdata.h"
#include "gtkspectvis.h"

#define MERGE_NODE_R 0
#define MERGE_NODE_G 0
#define MERGE_NODE_B 65535

#define MERGE_LINK_R 65535/255*200
#define MERGE_LINK_G 65535/255*100
#define MERGE_LINK_B 65535/255*0

#define MERGE_CATCH_RANGE 5

extern GlobalData *glob;

/* Forward declarations */
static gboolean merge_add_spect_graph (gchar *datafilename, gint uid);

/********** Basic handling of the node graph *********************************/

/* Zoom x so that all resonances can be seen */
void merge_zoom_x_all ()
{
	GtkSpectVis *graph;
	gdouble min, max;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));
	
	gtk_spect_vis_zoom_x_all (graph);
	min = graph->view->xmin;
	max = graph->view->xmax;
	gtk_spect_vis_zoom_x_to (graph, min - (max-min)*0.02, max + (max-min)*0.02);
}

/* Undisplay the selection box */
void merge_undisplay_node_selection ()
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
			merge->selx-MERGE_CATCH_RANGE+1, merge->sely-MERGE_CATCH_RANGE+1,
			2*MERGE_CATCH_RANGE-2, 2*MERGE_CATCH_RANGE-2);

	merge->selx = -1;
}

/* Display the selection box */
void merge_display_node_selection (gint xpix, gint ypix)
{
	MergeWin *merge;
	GtkWidget *widget;

	if (!glob->merge)
		return;
	merge = glob->merge;

	if (merge->selx > 0)
		merge_undisplay_node_selection ();

	widget = glade_xml_get_widget (merge->xmlmerge, "merge_spectvis");

	gdk_draw_rectangle (widget->window, GTK_SPECTVIS (widget)->cursorgc, FALSE, 
			xpix-MERGE_CATCH_RANGE+1, ypix-MERGE_CATCH_RANGE+1, 
			2*MERGE_CATCH_RANGE-2, 2*MERGE_CATCH_RANGE-2);
	merge->selx = xpix;
	merge->sely = ypix;
}

/* Returns the node at (xpix, ypix) close to the cursor at (x, y) or NULL.
 * If endonly == TURE, react only if close to the end of a node. */
MergeNode* merge_get_nearnode (gint xpos, gint ypos, gint *xpix, gint *ypix, gboolean endonly)
{
	MergeWin *merge = glob->merge;
	GPtrArray *nodes;
	GtkSpectVis *graph;
	GtkSpectVisViewport *view;
	gint i, diff, lastdiff, ypix2, id = -1;
	gdouble pos;

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));
	g_return_val_if_fail (graph, NULL);
	view = graph->view;

	if ((!view) || (!merge->nodelist->len))
		return NULL;

	if ((xpos < 0) || (ypos < 0))
	{
		/* Get cursor position */
		if (!(gdk_window_at_pointer (&xpos, &ypos)))
			return NULL;
	}

	/* Get a nodelist id */
	if (endonly)
	{
		/* Be only sensitive for node ends */
		for (pos=0.75; pos < (gdouble)merge->nodelist->len + 0.6; pos+=0.5)
		{
			*ypix = view->graphboxyoff - (gdouble) view->graphboxheight / (view->ymax - view->ymin) * (pos - view->ymin);
			if (abs (*ypix - ypos) < MERGE_CATCH_RANGE)
			{
				id = (gint) floor (pos+0.5) - 1;
				break;
			}
		}
	}
	else
	{
		/* Anywhere near the node is good */
		for (pos=0.75; pos < (gdouble)merge->nodelist->len + 0.6; pos+=1.0)
		{
			*ypix = view->graphboxyoff - (gdouble) view->graphboxheight / (view->ymax - view->ymin) * (pos   - view->ymin);
			ypix2 = view->graphboxyoff - (gdouble) view->graphboxheight / (view->ymax - view->ymin) *(pos+0.5- view->ymin);
			if ((ypos < *ypix + MERGE_CATCH_RANGE) && (ypos > ypix2 - MERGE_CATCH_RANGE))
			{
				id = (gint) floor (pos+0.5) - 1;
				/* Take middle of node as y position */
				*ypix = view->graphboxyoff - (gdouble) view->graphboxheight / 
					(view->ymax - view->ymin) * (floor (pos+0.5) - view->ymin);
				break;
			}
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

		if (diff < MERGE_CATCH_RANGE)
			break;

		i++;
	}
	
	if (diff >= MERGE_CATCH_RANGE)
		return NULL;

	/* return closest node */
	return (MergeNode *) g_ptr_array_index (nodes, i);
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
		nextnode->guid2 = prevnode->guid1;
	}
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

/* Remove all links from the graph */
void merge_draw_remove_all ()
{
	GtkSpectVis *graph;
	gint i;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));
	
	for (i=0; i<glob->merge->links->len; i++)
		merge_draw_remove ((GList *) g_ptr_array_index (glob->merge->links, i));
}

/* Takes a GList with node link information and adds it to the graph */
gboolean merge_draw_link (GList *link)
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

/* Mark the width of the resonance at node or disable mark if node == NULL */
void merge_highlight_width (MergeNode *node)
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

		width = node->res->width / 3.0;
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

void merge_show_resonance_info (MergeNode *node)
{
	GtkLabel *label;
	gchar *text;

	label = GTK_LABEL (glade_xml_get_widget (glob->merge->xmlmerge, "merge_info_label"));
	
	if (node)
		text = g_strdup_printf ("frequency = %.6f GHz, width = %.3f %s", 
				node->res->frq / 1e9, 
				node->res->width / pow (10, glob->prefs->widthunit),
				glob->prefs->widthunit == 6 ? "MHz" : "kHz");
	else
		text = g_strdup_printf ("(none)");

	gtk_label_set_text (label, text);
	g_free (text);
}

/********** Basic link and node handling *************************************/

/* Add a *reslist and a *datafilename to the graph, treeview and struct */
void merge_add_reslist (GPtrArray *reslist, gchar *datafilename, gchar *name)
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
	gchar *shortname;
	
	g_return_if_fail (reslist);

	/* Add to pointer arrays */
	g_ptr_array_add (merge->datafilename, datafilename);

	/* Remember original length of this dataset */
	g_ptr_array_add (merge->origlen, GINT_TO_POINTER (reslist->len));

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

	shortname = g_path_get_basename (name);

	/* Add to liststore */
	gtk_list_store_append (merge->store, &iter);
	gtk_list_store_set (merge->store, &iter,
			MERGE_ID_COL,        merge->nodelist->len,
			MERGE_LIST_COL,      name,
			MERGE_SHORTNAME_COL, shortname,
			MERGE_DATAFILE_COL,  datafilename,
			-1);

	g_free (shortname);

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

	/* Add to spectra graph */
	merge_add_spect_graph (datafilename, uid);
}

/* Takes a node and removes links associated with it, determined by type.
 * type == 0: remove both links and reconnect other nodes
 * type == 1: remove link to next node, do not reconnect
 * type == 2: remove link to previous node, no not reconnect
 * Returns the next GList element or NULL if this is not possible. */
GList* merge_delete_link_node (MergeNode *node, gint type)
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

/* Compare two MergeNode structs by their id */
gint merge_link_compare (gconstpointer a_in, gconstpointer b_in)
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

/* Add a link between two nodes */
void merge_link_two_nodes (MergeNode *node1, MergeNode *node2)
{
	GtkSpectVis *graph;
	MergeWin *merge = glob->merge;
	GList *newlist = NULL;

	graph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spectvis"));

	if (node2->link && node1->link)
	{
		/* Connect two link lists */
		merge_draw_remove (node1->link);
		merge_draw_remove (node2->link);
		g_ptr_array_remove (merge->links, g_list_first (node1->link));
		g_ptr_array_remove (merge->links, g_list_first (node2->link));
		newlist = g_list_first (node1->link);
		newlist = g_list_concat (newlist, g_list_first (node2->link));
	}
	else if (node2->link)
	{
		/* Second node already has a list */
		merge_draw_remove (node2->link);
		g_ptr_array_remove (merge->links, g_list_first (node2->link));
		newlist = g_list_first (node2->link);
		newlist = g_list_append (newlist, node1);
		node1->link = g_list_last (newlist);
	}
	else if (node1->link)
	{
		/* First node already has a list */
		merge_draw_remove (node1->link);
		g_ptr_array_remove (merge->links, g_list_first (node1->link));
		newlist = g_list_first (node1->link);
		newlist = g_list_append (newlist, node2);
		node2->link = g_list_last (newlist);
	}
	else
	{
		/* New list out of node2 and node1 */
		newlist = g_list_append (NULL, node1);
		node1->link = g_list_last (newlist);
		newlist = g_list_append (newlist, node2);
		node2->link = g_list_last (newlist);
	}

	newlist = g_list_sort (newlist, merge_link_compare);
	g_ptr_array_add (merge->links, newlist);
	merge_draw_link (newlist);
	gtk_spect_vis_redraw (graph);

	/* Update link information of each node */
	while (newlist)
	{
		((MergeNode *) newlist->data)->link = newlist;
		newlist = g_list_next (newlist);
	}
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
gboolean merge_is_id_in_list (guint id, GList *list)
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
GArray* merge_gather_reslist ()
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

/* Remove the given MergeNode from the nodelist and graph */
gboolean merge_delres (MergeNode *node)
{
	gint uid, i, j;
	MergeWin *merge = glob->merge;
	GtkSpectVis *graph;
	GtkSpectVisData *data;
	gdouble *X;
	ComplexDouble *Y;
	
	g_return_val_if_fail (node, FALSE);

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->merge->xmlmerge, "merge_spectvis"));

	/* Get graph data */
	uid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, node->id-1));
	data = gtk_spect_vis_get_data_by_uid (graph, uid);

	/* Cannot delete last node */
	if (data->len == 1)
	{
		dialog_message ("You cannot remove the last node of a resonance list. "
				"Remove the resonance list instead.");
		return FALSE;
	}
	
	/* Update a possible link */
	if (node->link)
		merge_delete_link_node (node, 0);

	/* Remove datapoint from graph data */
	X = g_new (gdouble, data->len - 1);
	Y = g_new (ComplexDouble, data->len - 1);
	j = 0;
	for (i=0; i<data->len; i++)
	{
		if (fabs (data->X[i] - node->res->frq) < 1e-10)
		{
			j++;
			continue;
		}

		X[i-j] = data->X[i];
		Y[i-j] = data->Y[i];
	}
	g_free (data->X);
	g_free (data->Y);
	gtk_spect_vis_data_update (graph, uid, X, Y, data->len - 1);

	gtk_spect_vis_redraw (graph);

	/* Remove node from nodelist */
	g_free (node->res);
	g_ptr_array_remove (
			g_ptr_array_index (merge->nodelist, node->id-1),
			node);

	/*TODO: Update the treeview */

	return TRUE;
}

/********** Statusbar stuff **************************************************/

/* Remove the latest message from the statusbar. */
static gboolean merge_statusbar_message_remove (gpointer data)
{
	GtkWidget *statusbar;

	if (!glob->merge)
		return FALSE;
	
	statusbar = glade_xml_get_widget (glob->merge->xmlmerge, "merge_statusbar");
	gtk_statusbar_pop (GTK_STATUSBAR (statusbar), GPOINTER_TO_INT (data));

	return FALSE;
}

/* Add a new message to the statusbar which will be removed after 5 sec. */
void merge_statusbar_message (gchar *format, ...)
{
	va_list ap;
	gchar *message;
	gint context_id;
	GtkWidget *statusbar;

	if (!glob->merge)
		return;
	
	statusbar = glade_xml_get_widget (glob->merge->xmlmerge, "merge_statusbar");

	va_start(ap, format);
	message = g_strdup_vprintf (format, ap);
	va_end(ap);

	context_id = gtk_statusbar_get_context_id (GTK_STATUSBAR (statusbar), message);
	gtk_statusbar_push (GTK_STATUSBAR (statusbar), context_id, message);

	g_free (message);

	g_timeout_add (5*1000, merge_statusbar_message_remove, GINT_TO_POINTER (context_id));
}

/********** The spectrum graph ***********************************************/

/* Load spectrum in datafilename and add it to the spectrum graph */
static gboolean merge_add_spect_graph (gchar *datafilename, gint uid)
{
	GtkSpectVis *spectgraph;
	MergeWin *merge;
	GdkColor color;
	DataVector *data;
	gint index;

	g_return_val_if_fail (datafilename, FALSE);
	g_return_val_if_fail (uid, FALSE);
	g_return_val_if_fail (glob->merge, FALSE);

	merge = glob->merge;
	spectgraph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spect_graph"));

	data = import_datafile (datafilename, FALSE);
	if (!data)
		return FALSE;

	color.red   = 45000;
	color.green = 45000;
	color.blue  = 45000;

	index = gtk_spect_vis_data_add (
			spectgraph,
			data->x,
			data->y,
			data->len,
			color, 'l');

	g_return_val_if_fail (gtk_spect_vis_request_id (spectgraph, index, uid), FALSE);

	if (glob->prefs->datapoint_marks)
		gtk_spect_vis_set_graphtype (spectgraph, index, 'm');

	gtk_spect_vis_zoom_x_all (spectgraph);
	gtk_spect_vis_zoom_y_all (spectgraph);
	gtk_spect_vis_redraw (spectgraph);

	return TRUE;
}

/* Remove a spectrum with uid from the spectrum graph */
gboolean merge_remove_spect_graph (gint uid)
{
	GtkSpectVis *spectgraph;
	MergeWin *merge;
	GtkSpectVisData *data;
	
	g_return_val_if_fail (uid, FALSE);
	g_return_val_if_fail (glob->merge, FALSE);

	merge = glob->merge;
	spectgraph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spect_graph"));

	data = gtk_spect_vis_get_data_by_uid (spectgraph, uid);
	if (!data)
		return FALSE;

	g_free (data->X);
	g_free (data->Y);

	gtk_spect_vis_data_remove (spectgraph, uid);
	gtk_spect_vis_redraw (spectgraph);

	return TRUE;
}

/* Zooms the spectrum graph around node, highlighting it and marking the
 * resonance with a bar. If node == NULL, remove the bar. */
gboolean merge_spect_graph_show_node (MergeNode *node)
{
	static guint baruid = 0;
	static guint lastgraphuid = 0;
	GtkSpectVis *spectgraph;
	MergeWin *merge;
	GdkColor color;
	gdouble width;
	
	g_return_val_if_fail (glob->merge, FALSE);

	merge = glob->merge;
	spectgraph = GTK_SPECTVIS (glade_xml_get_widget (merge->xmlmerge, "merge_spect_graph"));

	if (baruid)
	{
		gtk_spect_vis_remove_bar (spectgraph, baruid);
		baruid = 0;
		if (lastgraphuid)
		{
			color.red   = 45000;
			color.green = 45000;
			color.blue  = 45000;

			gtk_spect_vis_set_data_color (spectgraph, lastgraphuid, color);

			lastgraphuid = 0;
		}

		if (!node)
			gtk_spect_vis_redraw (spectgraph);
	}

	if (node)
	{	

		/* Draw bar */
		width = node->res->width / 3.0;
		if (width > 1e9)
		{
			width = 1e9;
			color.red   = 52000;
			color.green = 10000;
			color.blue  = 40000;
		}
		else
		{
			color.blue  = color.red = 20000;
			color.green = 62000;
		}

		baruid = gtk_spect_vis_add_bar (spectgraph, node->res->frq, width, color);
		gtk_spect_vis_zoom_x_to (spectgraph,
				node->res->frq - 3*width,
				node->res->frq + 3*width);
		gtk_spect_vis_zoom_y_all (spectgraph);

		/* Change color of graph */
		color.red   = 45000 / 2;
		color.green = 45000 / 2;
		color.blue  = 45000 / 2;

		lastgraphuid = GPOINTER_TO_INT (g_ptr_array_index (merge->graphuid, node->id - 1));
		gtk_spect_vis_set_data_color (spectgraph, lastgraphuid, color);

		gtk_spect_vis_redraw (spectgraph);
	}

	return TRUE;
}

/* Mark a position in the spectrum graph */
gint merge_spect_handle_value_selected (GtkSpectVis *spectvis, gdouble *xval, gdouble *yval)
{
	gtk_spect_vis_mark_point (spectvis, *xval, *yval);

	return 0;
}
