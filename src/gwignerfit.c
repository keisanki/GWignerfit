/* GWignerFit
 * Copyright (C) 2004-2007 Florian Schaefer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <locale.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "preferences.h"
#include "resonancelist.h"
#include "processdata.h"
#include "visualize.h"
#include "structs.h"
#include "dnd.h"
#include "overlay.h"
#include "helpers.h"

GlobalData *glob = NULL;
GladeXML *gladexml;

void parse_commandline (gint argc, char *argv[])
{
	gchar *filename, *dir;
	guint i = 1;
	
	while (argv[i]) {
		if  (!g_path_is_absolute (argv[i]))
		{
			/* find the absolute location of the file */
			dir = g_get_current_dir();
			filename = g_build_filename (dir, argv[i], NULL);
			g_free (dir);
		}
		else
			filename = g_strdup (argv[i]);

		if (is_datafile (filename))
		{
			/* might be a datafile or the FFT of one */
			if (!glob->data)
				read_datafile (filename, FALSE);
			else
				overlay_file (filename);
		}
		else if (g_str_has_suffix (filename, ".gwf"))
		{
			/* might be a GWignerFit resonance file */
			load_gwf_resonance_file (filename);
		}
		g_free (filename);

		i++;
	}
}

void set_icons ()
{
	GList *icon_list = NULL;
	GdkPixbuf *icon;
	gint i;
	gchar *full_name;
	const gchar *icon_names[] = {
		"gwignerfit-48x48.png",
		"gwignerfit-16x16.png"
	};

	for (i=0; i<2; i++)
	{
		full_name = g_strdup_printf ("%s%c%s", 
				ICONPATH, G_DIR_SEPARATOR, icon_names[i]);
		
		icon = gdk_pixbuf_new_from_file (full_name, NULL);
		if (icon)
			icon_list = g_list_append (icon_list, icon);
		else
			g_warning ("Could not set icon.");

		g_free (full_name);
	}

	gtk_window_set_default_icon_list (icon_list);
	gtk_window_set_icon_list (
			GTK_WINDOW (glade_xml_get_widget (gladexml, "mainwindow")),
			icon_list);

	g_object_unref (icon);
}

gint main (gint argc, char *argv[]) 
{
	gint i;
	
	/* Prepare Glib thread support and Gtk */
	g_thread_init (NULL);
	gtk_init (&argc, &argv);

	/* Initialize the glade system */
	glade_init();
	gladexml = glade_xml_new (GLADEFILE, "mainwindow", NULL);
	if (!gladexml) {
		g_warning("something bad happened while creating the interface");
		return 1;
	}
	glade_xml_signal_autoconnect (gladexml);
	
	/* The decimal delimiter should be '.' */
	setlocale (LC_NUMERIC, "C");
	/* And the stock buttons should have English labels */
	setlocale (LC_MESSAGES, "C");

	/* Set a bunch of icons for this program */
	set_icons ();

	/* Initialize the global variable */
	glob = g_new(GlobalData, 1);
	glob->data = NULL;
	glob->theory = NULL;
	glob->noise = -1;
	glob->numres = 0;
	glob->resonancefile = NULL;
	glob->section = NULL;
	glob->path = NULL;
	glob->flag = 0;
	glob->viewdifference = FALSE;
	glob->viewtheory = FALSE;
	glob->param = g_ptr_array_new ();
	glob->oldparam = NULL;
	glob->gparam = g_new0 (GlobalParam, 1);
	glob->stddev = NULL;
	glob->fitwindow.fit_GThread = NULL;
	glob->bars = g_new0 (guint, 3);
	glob->overlaystore = NULL;
	glob->overlayspectra = NULL;
	glob->overlaytreeview = NULL;
	glob->fft = NULL;
	glob->spectral = NULL;
	glob->netwin = NULL;
	glob->calwin = NULL;
	glob->fcomp = g_new0 (FourierCompWin, 1);
	glob->fcomp->data = g_ptr_array_new ();
	glob->fcomp->quotient = NULL;
	glob->fcomp->theo = NULL;
	glob->merge = NULL;
	glob->commentxml = NULL;
	glob->comment = NULL;
	glob->smp = NULL;
	glob->correl = NULL;

	/* Set the preferences to the default values */
	glob->prefs = NULL;
	prefs_set_default ();
	prefs_load (glob->prefs);

	/* Now that the prefs are loaded: adjust the priority */
	adjustpriority ();

	/* Initialize the thread system */
	glob->threads = g_new (ThreadStuff, 1);
	glob->threads->theorylock = g_mutex_new ();
	glob->threads->flaglock   = g_mutex_new ();
	glob->threads->fitwinlock = g_mutex_new ();
	glob->threads->numcpu  = get_num_cpu ();
	glob->threads->aqueue1 = g_async_queue_new ();
	glob->threads->aqueue2 = g_async_queue_new ();
	glob->threads->theopool = NULL;
	glob->threads->pool = g_thread_pool_new (
			(GFunc) visualize_background_calc,
			NULL,
			1,
			TRUE,
			NULL);

	/* Set up drag'n'drop */
	dnd_init (glade_xml_get_widget (gladexml, "mainwindow"));
	
	/* Create an empty resonancelist */
	set_up_resonancelist ();

	/* Parse the commandline */
	parse_commandline (argc, argv);

	/* Let the show begin... */
	gtk_main ();

	/* Tidy up thread system */
	visualize_stop_background_calc ();
	g_async_queue_unref (glob->threads->aqueue1);
	g_async_queue_unref (glob->threads->aqueue2);
	g_mutex_free (glob->threads->flaglock);
	g_mutex_free (glob->threads->theorylock);
	g_mutex_free (glob->threads->fitwinlock);
	g_thread_pool_free (glob->threads->pool, TRUE, TRUE);
	if (glob->threads->theopool)
		g_thread_pool_free (glob->threads->theopool, TRUE, TRUE);

	g_free (glob->smp);

	/* Give the memory free */
	gtk_widget_destroy (glade_xml_get_widget (gladexml, "mainwindow"));
	if (glob->overlayspectra)
	{
		for (i=0; i<glob->overlayspectra->len; i++)
		{
			free_datavector ((DataVector *) g_ptr_array_index (glob->overlayspectra, i));
			glob->overlayspectra->pdata[i] = NULL;
		}
		gtk_list_store_clear (glob->overlaystore);
		glob->overlaystore = NULL;
		g_ptr_array_free (glob->overlayspectra, TRUE);
	}
	free_datavector (glob->theory);
	glob->theory = NULL;
	free_datavector (glob->data);
	g_ptr_array_foreach (glob->param, (GFunc) g_free, NULL);
	g_ptr_array_free (glob->param, TRUE);
	g_free (glob->prefs);
	g_free (glob->oldparam);
	g_free (glob->gparam);
	g_free (glob->stddev);
	g_free (glob->resonancefile);
	g_free (glob->path);
	g_free (glob->bars);
	g_free (glob->fft);
	g_free (glob->spectral);
	if (glob->netwin)
	{
		//g_free (glob->netwin->xmlnet);
		g_free (glob->netwin->host);
		g_free (glob->netwin->path);
		g_free (glob->netwin->file);
		g_free (glob->netwin);
	}

	return 0;
}
