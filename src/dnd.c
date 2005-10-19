/*
 *  dnd.c
 *  This file is originally part of Leafpad
 *
 *  Leafpad Copyright (C) 2004 Tarot Osuji
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <gtk/gtk.h>

#include "processdata.h"
#include "overlay.h"

#define DV(x)

extern GlobalData *glob;

static void dnd_handle_file (gchar *filename)
{
	if (is_datafile (filename))
	{
		/* might be a datafile */
		if (!glob->data)
			/* import as main data */
			read_datafile (filename, FALSE);
		else
			/* try to overlay data */
			overlay_file (filename);
	}
	else
	{
		/* might be a GWignerFit resonance file */
	}
}

static void dnd_drag_data_received_handler(GtkWidget *widget,
	GdkDragContext *context, gint x, gint y,
	GtkSelectionData *selection_data, guint info, guint time)
{
	gchar **files;
/*	gchar **strs; */
	gchar *filename;
	gchar *comline;
	gint i;
	
DV({	
	g_print("time                      = %d\n", time);
	g_print("selection_data->selection = %s\n", gdk_atom_name (selection_data->selection));
	g_print("selection_data->target    = %s\n", gdk_atom_name (selection_data->target));
	g_print("selection_data->type      = %s\n", gdk_atom_name (selection_data->type));
	g_print("selection_data->format    = %d\n", selection_data->format);
	g_print("selection_data->data      = %s\n", selection_data->data);
	g_print("selection_data->length    = %d\n", selection_data->length);
});	
	
	if (selection_data->data && g_strstr_len ((gchar *) selection_data->data, 5, "file:"))
	{
		files    = g_strsplit ((gchar *) selection_data->data, "\n" , 0);

		i = 0;
		while ((files[i]) && (files[i][0] == 'f'))
		{
			/* Get filename in correct encoding */
			filename = g_filename_from_uri (files[i], NULL, NULL);
#if 0
			if (g_strrstr (filename, " "))
			{
				/* Escape spaces in the filename */
				strs = g_strsplit (filename, " ", -1);
				g_free (filename);
				filename = g_strjoinv ("\\ ", strs);
				g_strfreev (strs);
			}
#endif
			/* Remove leading and trailing whitespace from filename */
			comline = g_strdup_printf ("%s", g_strstrip(filename));
			g_free (filename);
			DV(g_print("[%s]\n", comline));

			/* Do something with comline */
			dnd_handle_file (comline);
			g_free (comline);

			i++;
		}

		g_strfreev (files);
	}
}

static GtkTargetEntry drag_types[] =
{
	{ "text/uri-list", 0, 0 }
};

static gint n_drag_types = sizeof(drag_types) / sizeof(drag_types[0]);

void dnd_init(GtkWidget *widget)
{
//	GtkWidget *w = gtk_widget_get_ancestor(widget, GTK_TYPE_CONTAINER);
//	GtkWidget *w = gtk_widget_get_parent(widget);
//	GtkWidget *w = gtk_widget_get_parent(gtk_widget_get_parent(gtk_widget_get_parent(widget)));
	
//	g_print(gtk_widget_get_name(w));
	gtk_drag_dest_set (widget, GTK_DEST_DEFAULT_ALL,
		drag_types, n_drag_types, GDK_ACTION_COPY);
	g_signal_connect (G_OBJECT(widget), "drag_data_received",
		G_CALLBACK (dnd_drag_data_received_handler), NULL);
}
