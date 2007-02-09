#include <gtk/gtk.h>
#include <glade/glade.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>

#ifndef NO_ZLIB
#include <zlib.h>
#endif

#include "structs.h"
#include "helpers.h"
#include "gtkspectvis.h"
#include "processdata.h"
#include "overlay.h"
#include "fourier.h"
#include "calibrate_vna.h"

#define VNA_PORT    8510	/* Port of Ieee488Proxy */
#define VNA_GBIP     "16"	/* GBIP address of network analyzer as string */
#define VNA_GBIP_INT  16 	/* GBIP address of network analyzer as integer */
#define VNA_GREET_LEN 28	/* Length of greeting message */
#define VNA_STAT_LEN  24	/* Length of status message */
#define VNA_RARR_LEN  33	/* Length of rarray 'Bytes read' reply */
#define VNA_CONN_TOUT  5	/* Timeout (in sec) for connect */
#define VNA_RECV_TOUT  3	/* Timeout (in sec) for recv */
#define VNA_ESUCCESS   0	/* Proxy status: Success */
#define VNA_ESYNTAXE   1	/* Proxy status: Illegal Syntax command */
#define VNA_ENOLISTE   2	/* Proxy status: Receive when PC was not a listener */
#define VNA_EQUOTEDS   4	/* Proxy status: A quoted string in LISTEN or TALK */
#define VNA_ETIMEOUT   8	/* Proxy status: Timeout */
#define VNA_EUNKNOWN  16	/* Proxy status: Unknown command */
#define VNA_ESUCCEOI  32	/* Proxy status: Seccess, transfer ended with EOI */

#define DV(x)  			/* For debuggins set DV(x) x */

extern GlobalData *glob;	/* Global variables */
extern GladeXML *gladexml;

static void vna_start ();	/* Forward declaration */

typedef struct
{
	DataVector *dvec;	/* Measured data for graph update */
	gint pos;		/* S-matrix position */
} NetworkGraphUpdateData;

/************************ The main thread **********************************/

/* Close the window */
void on_vna_close_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if ((!glob->netwin) || (!glob->netwin->xmlnet))
		return;

	if (glob->flag & FLAG_VNA_MEAS)
	{
		if (dialog_question ("This will abort your current measurement, close window anyway?") 
	     			!= GTK_RESPONSE_YES)
			return;
	
		glob->flag &= ~FLAG_VNA_MEAS;
		/* Wait for the thread to finish */
		g_thread_join (glob->netwin->vna_GThread);
	}

	gtk_widget_destroy (
		glade_xml_get_widget (glob->netwin->xmlnet, "vna_win")
		);

	glob->netwin->xmlnet = NULL;
}

/* Update the GUI with the data of the netwin struct */
static void network_struct_to_gui ()
{
	NetworkWin *netwin;
	GtkEntry *entry;
	gchar *text;

	g_return_if_fail (glob->netwin);
	netwin = glob->netwin;

	g_return_if_fail (netwin->xmlnet);

	if (netwin->host)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_host_entry"));
		gtk_entry_set_text (entry, netwin->host);
	}

	if (netwin->path)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_path_entry"));
		gtk_entry_set_text (entry, netwin->path);
	}

	if (netwin->file)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_file_entry"));
		gtk_entry_set_text (entry, netwin->file);
	}

	if (netwin->comment)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_comment_entry"));
		gtk_entry_set_text (entry, netwin->comment);
	}

	if (netwin->compress)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_compress_check")),
			TRUE);
	else
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_compress_check")),
			FALSE);

	if (netwin->type == 1)
	{
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_sweep_radio")),
			TRUE);
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_snap_radio")),
			FALSE);
	}
	else
	{
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_sweep_radio")),
			FALSE);
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_snap_radio")),
			TRUE);
	}

	if (netwin->start)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_start_entry"));
		text = g_strdup_printf ("%g", netwin->start / 1e9);
		gtk_entry_set_text (entry, text);
		g_free (text);
	}

	if (netwin->stop)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_stop_entry"));
		text = g_strdup_printf ("%g", netwin->stop / 1e9);
		gtk_entry_set_text (entry, text);
		g_free (text);
	}

	if (netwin->resol)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_res_entry"));
		text = g_strdup_printf ("%g", netwin->resol / 1e3);
		gtk_entry_set_text (entry, text);
		g_free (text);
	}

	if (netwin->param[0] == 'S')
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_s_entry"));
		gtk_entry_set_text (entry, netwin->param);
	}

	if (netwin->avg > 0)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_avg_entry"));
		text = g_strdup_printf ("%ix", netwin->avg);
		gtk_entry_set_text (entry, text);
		g_free (text);
	}

	if (netwin->swpmode == 1)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_stim_entry"));
		gtk_entry_set_text (entry, "ramp mode");
	}
	else
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_stim_entry"));
		gtk_entry_set_text (entry, "step mode");
	}
}

/* Update the netwin struct with the GUI entries after doing some sanity checks */
static int network_gui_to_struct ()
{
	NetworkWin *netwin;
	GtkEntry *entry;
	const gchar *text;
	gdouble val;

	g_return_val_if_fail (glob->netwin, 1);
	netwin = glob->netwin;

	g_return_val_if_fail (netwin->xmlnet, 1);

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_host_entry"));
	text = gtk_entry_get_text (entry);
	if (!strlen(text))
	{
		dialog_message ("Please enter a host name or host IP.");
		return 1;
	}
	g_free (netwin->host);
	netwin->host = g_strdup (text);

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_path_entry"));
	text = gtk_entry_get_text (entry);
	g_free (netwin->path);
	if (strlen (text))
		netwin->path = g_strdup (text);
	else
		netwin->path = NULL;

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_file_entry"));
	text = gtk_entry_get_text (entry);
	if (!strlen(text))
	{
		dialog_message ("Please enter a filename.");
		return 1;
	}
	if (g_strrstr (text, G_DIR_SEPARATOR_S))
	{
		dialog_message ("A filename must not contain the '%c' character.", G_DIR_SEPARATOR);
		return 1;
	}
	if (!g_str_has_suffix (text, ".dat"))
		if (dialog_question ("You output file does not have the suffix '.dat', proceed anyway?")
				!= GTK_RESPONSE_YES)
			return 1;
	g_free (netwin->file);
	netwin->file = g_strdup (text);

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_comment_entry"));
	text = gtk_entry_get_text (entry);
	g_free (netwin->comment);
	if (strlen (text))
		netwin->comment = g_strdup (text);
	else
		netwin->comment = NULL;

	netwin->compress = gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_compress_check")));

	if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_sweep_radio"))))
		netwin->type = 1;
	else
		netwin->type = 2;

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_start_entry"));
	text = gtk_entry_get_text (entry);
	if (sscanf (text, "%lf", &val) != 1)
	{
		dialog_message ("Could not parse start frequency.");
		return 1;
	}
	if (val < 0.045)
	{
		dialog_message ("Start frequency must not be less than 0.045 GHz.");
		return 1;
	}
	netwin->start = val * 1e9;

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_stop_entry"));
	text = gtk_entry_get_text (entry);
	if (sscanf (text, "%lf", &val) != 1)
	{
		dialog_message ("Could not parse stop frequency.");
		return 1;
	}
	if (val > 50.0)
	{
		dialog_message ("Stop frequency must be below 50.0 GHz.");
		return 1;
	}
	netwin->stop = val * 1e9;

	if (netwin->stop <= netwin->start)
	{
		dialog_message ("Stop frequency must be higher than start frequency.");
		return 1;
	}

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_res_entry"));
	text = gtk_entry_get_text (entry);
	if (sscanf (text, "%lf", &val) != 1)
	{
		dialog_message ("Could not parse frequency resolution.");
		return 1;
	}
	val *= 1e3;
	if ((netwin->stop - netwin->start) / val < 3.0)
	{
		dialog_message ("Frequency resolution is too coarse for selected frequency window.");
		return 1;
	}
	netwin->resol = val;

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_s_entry"));
	text = gtk_entry_get_text (entry);
	snprintf (netwin->param, 4, "%s", text);

	if ( ((strlen (netwin->param) == 1) || !(strcmp (netwin->param, "TRL"))) 
	     && (!g_strrstr (netwin->file, "%%")))
	{
		dialog_message ("Output filename does not contain wild-card \"%%%%\".");
		return 1;
	}
	
	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_avg_entry"));
	text = gtk_entry_get_text (entry);
	sscanf (text, "%ix", &netwin->avg);

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_stim_entry"));
	text = gtk_entry_get_text (entry);
	if (text[0] == 'r')	/* _r_amp mode */
		netwin->swpmode = 1;
	else
		netwin->swpmode = 2;

	return 0;
}

/* Open the main measurement dialog */
void network_open_win ()
{
	GladeXML *xmlnet;
	GList *items = NULL;

	if (glob->netwin == NULL)
	{
		glob->netwin = g_new0 (NetworkWin, 1);
		glob->netwin->fullname[0] = NULL;
		glob->netwin->fullname[1] = NULL;
		glob->netwin->fullname[2] = NULL;
		glob->netwin->fullname[3] = NULL;
		glob->netwin->fullname[4] = NULL;
		glob->netwin->fullname[5] = NULL;
		glob->netwin->comment = NULL;
		glob->netwin->type = 1;
		glob->netwin->param[0] = '\0';
		glob->netwin->avg = 0;
		glob->netwin->swpmode = 1;
	}

	if (glob->netwin->xmlnet == NULL)
	{
		xmlnet = glade_xml_new (GLADEFILE, "vna_win", NULL);
		glob->netwin->xmlnet = xmlnet;
		glade_xml_signal_autoconnect (xmlnet);

#ifdef NO_ZLIB
		gtk_widget_hide_all (glade_xml_get_widget (xmlnet, "vna_compress_check"));
#endif

		/* Unselect host entry */
		gtk_entry_select_region (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_host_entry")),
			0, 0);

		/* Set default start, stop and avg values */
		gtk_entry_set_text (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_start_entry")),
			"0.045");
		gtk_entry_set_text (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_stop_entry")),
			"10.000");
		gtk_entry_set_text (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_res_entry")),
			"100");

		/* Set up S-Parameter combo */
		items = g_list_append (items, "S12");
		items = g_list_append (items, "S21");
		items = g_list_append (items, "S11");
		items = g_list_append (items, "S22");
		items = g_list_append (items, "S");
		items = g_list_append (items, "TRL");
		gtk_combo_set_popdown_strings (
			GTK_COMBO (glade_xml_get_widget (xmlnet, "vna_s_combo")),
			items);
		g_list_free (items);
		gtk_entry_set_text (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_s_entry")),
			"S12");
		gtk_entry_set_editable (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_s_entry")),
			FALSE);
		items = NULL;

		/* Set up averaging combo */
		items = g_list_append (items, "1x");
		items = g_list_append (items, "2x");
		items = g_list_append (items, "4x");
		items = g_list_append (items, "8x");
		items = g_list_append (items, "16x");
		items = g_list_append (items, "32x");
		items = g_list_append (items, "64x");
		items = g_list_append (items, "128x");
		items = g_list_append (items, "256x");
		gtk_combo_set_popdown_strings (
			GTK_COMBO (glade_xml_get_widget (xmlnet, "vna_avg_combo")),
			items);
		g_list_free (items);
		gtk_entry_set_text (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_avg_entry")),
			"4x");
		gtk_entry_set_editable (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_avg_entry")),
			FALSE);
		items = NULL;

		/* Set up stimulus combo */
		items = g_list_append (items, "ramp mode");
		items = g_list_append (items, "step mode");
		gtk_combo_set_popdown_strings (
			GTK_COMBO (glade_xml_get_widget (xmlnet, "vna_stim_combo")),
			items);
		g_list_free (items);
		gtk_entry_set_text (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_stim_entry")),
			"ramp mode");
		gtk_entry_set_editable (
			GTK_ENTRY (glade_xml_get_widget (xmlnet, "vna_stim_entry")),
			FALSE);
		items = NULL;
		
		/* Update the GUI with remembered values */
		network_struct_to_gui ();
	}
}

/* The user clicked the start button */
void on_vna_start_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	gchar *filename, *tmpname, *pos;
	gint i, j;

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->xmlnet);
	
	/* Update glob->netwin struct */
	if (network_gui_to_struct ())
		return;

	/* Create filename(s) */
	if (glob->netwin->path)
		tmpname = g_build_filename (glob->netwin->path, glob->netwin->file, NULL);
	else
		tmpname = g_strdup (glob->netwin->file);

	if (glob->netwin->compress)
	{
		filename = g_strdup_printf ("%s.gz", tmpname);
		g_free (tmpname);
	}
	else
		filename = tmpname;

	for (i=0; i<6; i++)
	{
		g_free (glob->netwin->fullname[i]);
		glob->netwin->fullname[i] = NULL;
	}

	/* Check file(s) */
	if ((strlen (glob->netwin->param) != 1) && (strcmp (glob->netwin->param , "TRL")))
	{
		if (!file_is_writeable (filename))
		{
			g_free (filename);
			return;
		}

		glob->netwin->fullname[0] = filename;
	}
	else
	{
		/* Full S-matrix measurement */
		pos = g_strrstr (filename, "%%");
		g_return_if_fail (pos);

		for (i=0; i<2; i++)
			for (j=0; j<2; j++)
			{
				pos[0] = 49+i;
				pos[1] = 49+j;

				if (!file_is_writeable (filename))
				{
					g_free (filename);
					for (i=0; i<6 ; i++)
					{
						g_free (glob->netwin->fullname[i]);
						glob->netwin->fullname[i] = NULL;
					}
					return;
				}

				glob->netwin->fullname[2*i+j] = g_strdup (filename);
			}

		if (!strcmp (glob->netwin->param , "TRL"))
		{
			/* TRL measurement */
			pos[0] = 'a';
			for (i=0; i<2; i++)
			{
				pos[1] = 49+i;
				if (!file_is_writeable (filename))
				{
					g_free (filename);
					for (i=0; i<6 ; i++)
					{
						g_free (glob->netwin->fullname[i]);
						glob->netwin->fullname[i] = NULL;
					}
					return;
				}
				glob->netwin->fullname[4+i] = g_strdup (filename);
			}
		}

		pos[0] = '%';
		pos[1] = '%';
	}

	/* Disable start button and GUI entries */
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->netwin->xmlnet, "vna_start_button"),
		FALSE);
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->netwin->xmlnet, "vna_io_frame"),
		FALSE);
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->netwin->xmlnet, "vna_mode_frame"),
		FALSE);
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->netwin->xmlnet, "vna_settings_frame"),
		FALSE);

	/* Fork the actual measurement into another process */
	glob->netwin->vna_GThread = 
		g_thread_create ((GThreadFunc) vna_start, NULL, TRUE, NULL);
	
	if (!glob->netwin->vna_GThread)
	{
		perror ("fork failed");
		exit (-1);
	}
}

/* Sweep mode has been (de)activated */
void on_vna_sweep_mode_change (GtkToggleButton *toggle, gpointer user_data)
{
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->netwin->xmlnet, "vna_settings_frame"),
		gtk_toggle_button_get_active (toggle));
}

/* Select an output filename */
void on_vna_select_activate (GtkButton *button, gpointer user_data)
{
	gchar *filename, *file, *path = NULL;
	GtkEntry *pathentry, *fileentry;
	
	if (glob->netwin->path)
		path = g_strdup_printf("%s%c", glob->netwin->path, G_DIR_SEPARATOR);
	else if (glob->path)
		path = g_strdup_printf("%s%c", glob->path, G_DIR_SEPARATOR);
	
	filename = get_filename ("Select output filename", path, 0);
	g_free (path);

	if (filename)
	{
		pathentry = GTK_ENTRY (glade_xml_get_widget (glob->netwin->xmlnet, "vna_path_entry"));
		fileentry = GTK_ENTRY (glade_xml_get_widget (glob->netwin->xmlnet, "vna_file_entry"));

		if (g_file_test (filename, G_FILE_TEST_IS_DIR))
		{
			gtk_entry_set_text (pathentry, filename);
			gtk_entry_set_text (fileentry, "");
		}
		else
		{
			file = g_path_get_basename (filename);
			path = g_path_get_dirname (filename);
			gtk_entry_set_text (pathentry, path);
			gtk_entry_set_text (fileentry, file);
			g_free (file);
			g_free (path);
		}
		g_free (filename);
	}
}

/************************ Measurement thread callbacks *********************/

/* Called by vna_thread_exit() to manipulate the GUI */
static gboolean vna_measurement_finished (gpointer data)
{
	if (data)
	{
		dialog_message ((gchar *) data);
		g_free (data);
	}
	
	if ((!glob->netwin) || (!glob->netwin->xmlnet))
		return FALSE;

	if ((glob->netwin) && (glob->netwin->xmlnet))
	{
		/* Enable start button and GUI entries */
		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->netwin->xmlnet, "vna_start_button"),
			TRUE);
		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->netwin->xmlnet, "vna_io_frame"),
			TRUE);
		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->netwin->xmlnet, "vna_mode_frame"),
			TRUE);
		if (glob->netwin->type == 1)
			gtk_widget_set_sensitive (
				glade_xml_get_widget (glob->netwin->xmlnet, "vna_settings_frame"),
				TRUE);
	}

	gtk_label_set_text (
		GTK_LABEL (glade_xml_get_widget (glob->netwin->xmlnet, "vna_netstat_label")), 
		"Not connected.");

	gtk_progress_bar_set_fraction (
		GTK_PROGRESS_BAR (glade_xml_get_widget (glob->netwin->xmlnet, "vna_status_progress")),
		0.0);

	gtk_progress_bar_set_text (
		GTK_PROGRESS_BAR (glade_xml_get_widget (glob->netwin->xmlnet, "vna_status_progress")),
		"");

	return FALSE;
}

/* Called by vna_update_netstat() */
static gboolean vna_set_netstat (gpointer data)
{
	if ((!glob->netwin) || (!glob->netwin->xmlnet))
		return FALSE;

	gtk_label_set_text (
		GTK_LABEL (glade_xml_get_widget (glob->netwin->xmlnet, "vna_netstat_label")), 
		(gchar *) data);

	g_free (data);
	
	return FALSE;
}

/* Update time estimates and progress bar */
static gboolean vna_show_time_estimates ()
{
	gchar *text;
	GTimeVal curtime;
	gint h1, m1, s1;
	gint h2, m2, s2;
	gint h3, m3, s3;
	glong timeleft;
	GtkProgressBar *pbar;
	gdouble fraction;

	if ((!glob->netwin) || (!glob->netwin->xmlnet))
		return FALSE;

	g_get_current_time (&curtime);
	
	sec_to_hhmmss (curtime.tv_sec - glob->netwin->start_t, &h1, &m1, &s1);
	sec_to_hhmmss (glob->netwin->estim_t, &h2, &m2, &s2);

	timeleft = glob->netwin->estim_t - (curtime.tv_sec - glob->netwin->start_t);
	if (timeleft < 0)
		timeleft = 0;
	sec_to_hhmmss (timeleft, &h3, &m3, &s3);

	text = g_strdup_printf ("%02d:%02d:%02d of %02d:%02d:%02d (%02d:%02d:%02d left)",
		h1, m1, s1, h2, m2, s2, h3, m3, s3);
	gtk_label_set_text (
		GTK_LABEL (glade_xml_get_widget (glob->netwin->xmlnet, "vna_timeest_label")),
		text);
	g_free (text);

	if (glob->netwin->estim_t > 0)
	{
		pbar = GTK_PROGRESS_BAR (glade_xml_get_widget (glob->netwin->xmlnet, "vna_status_progress"));

		fraction = 1.0 - (gdouble) timeleft / (gdouble) glob->netwin->estim_t;
		if (fraction > gtk_progress_bar_get_fraction (pbar))
		{
			/* Only update progress bar if there is real progress */
			gtk_progress_bar_set_fraction (pbar, fraction);
			text = g_strdup_printf ("%.0f %%", fraction * 100.0);
			gtk_progress_bar_set_text (pbar, text);
			g_free (text);
		}
	}

	if (! (glob->flag & FLAG_VNA_MEAS))
		return FALSE;

	/* This timeout will not be deleted. */
	return TRUE;
}

/* Display the currently measured data */
static gboolean vna_add_data_to_graph (NetworkGraphUpdateData *data)
{
	GtkSpectVis *spectvis = GTK_SPECTVIS (glade_xml_get_widget (gladexml, "graph"));
	GtkSpectVisData *curdata;
	guint pos;

	g_return_val_if_fail (data, FALSE);
	g_return_val_if_fail (data->dvec, FALSE);

	if (!glob->netwin->index[data->pos])
	{
		/* First call -> add to graph */
		/* dvec must be large enough to hold the whole measurement */

		make_unique_dataset (data->dvec);
		if (!glob->data)
		{
			/* Add as main graph */
			set_new_main_data (data->dvec, FALSE);
		}
		else
		{
			/* Add as overlay */
			overlay_add_data (data->dvec);
		}
		glob->netwin->ydata[data->pos] = data->dvec->y;
		glob->netwin->index[data->pos] = data->dvec->index;
	}
	else
	{
		/* Subsequent call -> update graph */
		/* dvec contains just the new data */
		curdata = gtk_spect_vis_get_data_by_uid (spectvis, glob->netwin->index[data->pos]);
		if ((curdata) && (curdata->Y == glob->netwin->ydata[data->pos]))
		{
			/* OK, the graph seems to be still there. */
			pos = 0;
			while ((pos < curdata->len) && (curdata->X[pos] < data->dvec->x[0]))
				pos++;

			memcpy (curdata->Y+pos, data->dvec->y, 
					(pos+data->dvec->len < curdata->len ? data->dvec->len : curdata->len-pos)
					* sizeof(ComplexDouble));
			/* The above memcpy() is short for:
			for (i=pos; (i-pos<data->dvec->len) && (i<curdata->len); i++)
				curdata->Y[i] = data->dvec->y[i-pos];
			*/
			
			gtk_spect_vis_redraw (spectvis);

			if ((glob->data) && (glob->netwin->index[data->pos] == glob->data->index))
				fourier_update_main_graphs ();
			else
			{
				fourier_update_overlay_graphs (-glob->netwin->index[data->pos], FALSE);
				fourier_update_overlay_graphs ( glob->netwin->index[data->pos], TRUE);
			}
		}
		free_datavector (data->dvec);
	}

	g_free (data);
	
	return FALSE;
}

/************************ The measurement thread ***************************/

int vna_send_cmd (int fd, char *msg, int errmask);

/* Exit the thread cleanly */
static void vna_thread_exit (gchar *format, ...)
{
	va_list ap;
	char *message = NULL;
	gint i;

	if (glob->flag & FLAG_VNA_CAL)
	{
		/* An online calibration is running which has its
		 * own exit function. */
		va_start (ap, format);
		message = g_strdup_vprintf (format, ap);
		va_end (ap);

		cal_vna_exit (message);
		g_free (message);

		return;
	}

	if (glob->netwin)
		for (i=0; i<6 ; i++)
		{
			g_free (glob->netwin->fullname[i]);
			glob->netwin->fullname[i] = NULL;
		}

#ifndef NO_ZLIB
	if (glob->netwin && glob->netwin->gzoutfh[0])
		for (i=0; i<6; i++)
			if (glob->netwin->gzoutfh[i])
			{
				gzclose (glob->netwin->gzoutfh[i]);
				glob->netwin->gzoutfh[i] = NULL;
			}
#endif

	if ((glob->flag & FLAG_VNA_MEAS) && (format))
	{
		va_start (ap, format);
		message = g_strdup_vprintf (format, ap);
		va_end (ap);
	}
	g_timeout_add (1, (GSourceFunc) vna_measurement_finished, message);
	gdk_beep ();

	if ((glob->netwin) && (glob->netwin->sockfd > 0))
	{
		close (glob->netwin->sockfd);
		glob->netwin->sockfd = -1;
	}

	glob->flag &= ~FLAG_VNA_MEAS;
	g_thread_exit (NULL);
}

/* Update the status string */
static void vna_update_netstat (gchar *format, ...)
{
	va_list ap;
	char *message;

	va_start (ap, format);
	message = g_strdup_vprintf (format, ap);
	va_end (ap);

	if (glob->flag & FLAG_VNA_CAL)
		g_timeout_add (1, (GSourceFunc) cal_vna_set_netstat, message);
	else
		g_timeout_add (1, (GSourceFunc) vna_set_netstat, message);
}

/* Sleep for some miliseconds */
static void vna_ms_sleep (glong ms)
{
	/* Convert ms to microseconds */
	ms *= 1000;
	
	while (ms)
	{
		if (ms > 5e5)
		{
			usleep (5e5);
			ms -= 5e5;
		}
		else
		{
			usleep (ms);
			ms = 0;
		}

		if (! (glob->flag & (FLAG_VNA_MEAS | FLAG_VNA_CAL)) )
		{
			if (glob->netwin->sockfd)
			{
				if (glob->netwin->type == 1)
					vna_send_cmd (glob->netwin->sockfd, 
						"MTA LISTEN "VNA_GBIP" DATA 'RAMP;CONT;'", 0);
				vna_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" GTL", 0);
			}
			vna_thread_exit (NULL);
		}
	}
}

/* Receive from socket s len bytes into buf. 
 * You have to make sure, that buf can hold len bytes! 
 * Breaks with timeout error message if failok==0. */
static int vna_receiveall_full (int s, char *buf, int len, int failok)
{
	int total = 0;       /* how many bytes we've received */
	int bytesleft = len; /* how many we have left to receive */
	int n = 0;           /* number of bytes received by recv */
	struct timeval tv;
	fd_set fdset;
	int i;

	FD_ZERO (&fdset);
	FD_SET (s, &fdset);

	while (total < len)
	{
		/* Wait VNA_RECV_TOUT seconds for data */
		for (i=0; i<VNA_RECV_TOUT; i++)
		{
			tv.tv_sec  = 1;
			tv.tv_usec = 0;
			select (s+1, &fdset, NULL, NULL, &tv);
			
			if (FD_ISSET (s, &fdset))
				break;

			if (! (glob->flag & (FLAG_VNA_MEAS | FLAG_VNA_CAL)) )
			{
				if (glob->netwin && glob->netwin->sockfd)
				{
					if (glob->netwin->type == 1)
						vna_send_cmd (glob->netwin->sockfd, 
							"MTA LISTEN "VNA_GBIP" DATA 'RAMP;CONT;'", 0);
					vna_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" GTL", 0);
				}
				vna_thread_exit (NULL);
			}
		}

		if (! FD_ISSET (s, &fdset))
		{
			/* Timeout */
			if (!failok)
			{
				/* Fail with error message and VNA initialization */
				if (glob->netwin && glob->netwin->sockfd)
				{
					if (glob->netwin->type == 1)
						vna_send_cmd (glob->netwin->sockfd, 
							"MTA LISTEN "VNA_GBIP" DATA 'RAMP;CONT;'", 0);
					vna_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" GTL", 0);
				}
				DV(if (len < 128)
					printf ("received (%d bytes): %.*s", len, len, buf);
				else
					printf ("received (%d bytes): [data not shown]\n", len);)
				vna_thread_exit ("Connection to proxy host timed out "
						"(received only %i of %i expected bytes).", total, len);
			}
			else
			{
				/* Fail silently */
				DV(printf ("*** Ignored timeout in vna_receiveall_full().\n");)
				return -1;
			}
		}
		
		/* Receive data */
		n = recv (s, buf+total, bytesleft, 0);
		if (n == -1) 
			vna_thread_exit ("recv: %s", g_strerror (errno));

		total += n;
		bytesleft -= n;
	}

	/*len = total;*/ /* return number actually received here */
	DV(if (len < 128)
		printf ("received (%d bytes): %.*s", len, len, buf);
	else
		printf ("received (%d bytes): [data not shown]\n", len);)

	return n==-1?-1:0; /* return -1 on failure, 0 on success */
}

/* Convenience wrapper for vna_receiveall_full() _with_ timeout */
int vna_receiveall (int s, char *buf, int len)
{
	return vna_receiveall_full (s, buf, len, 0);
}

/* Send a whole buffer to the proxy */
int vna_sendall (int s, char *buf, int len)
{
	int total = 0;       /* how many bytes we've sent */
	int bytesleft = len; /* how many we have left to send */
	int n = 0;

	DV(if (len < 128)
		printf("transmit (%d bytes): %.*s\n", len, len, buf);
	else
		printf("transmit (%d bytes): [data not shown]\n", len);)

	while (total < len)
	{
		n = send (s, buf+total, bytesleft, 0);
		if (n == -1) 
			vna_thread_exit ("Connection to proxy host lost.");
		total += n;
		bytesleft -= n;
	}

	/*len = total;*/ /* return number actually sent here */

	return n==-1?-1:0; /* return -1 on failure, 0 on success */
}

/* Send (must be \0 terminated!) msg to the proxy and return the status message. */
int vna_send_cmd (int fd, char *msg, int errmask)
{
	char reply[VNA_STAT_LEN+1];
	int status = -1;
	int recv_tries = 0;
	
	/* strlen (msg)+1: Transmit tailing \0, too. */
	vna_sendall (fd, msg, strlen (msg)+1);

	/* Get Proxy status reply */
	while ((vna_receiveall_full (fd, reply, VNA_STAT_LEN, 1) == -1) &&
	       (recv_tries < 5))
		recv_tries++;
	if ((recv_tries == 5) && (errmask > 0))
		vna_thread_exit ("Connection to proxy host timed out (did not receive status reply).");

	reply[VNA_STAT_LEN] = '\0';

	if ((sscanf (reply, "* PROXYMSG: Status %d", &status) != 1) && (errmask > 0))
		vna_thread_exit ("Could not parse proxy reply: %s", reply);
	
	if ((errmask > 0) && (status & errmask))
	{
		if ((status & VNA_ETIMEOUT) && (errmask & VNA_ETIMEOUT))
			vna_thread_exit ("Connection between proxy and network analyzer timed out.");
		if ((status & VNA_ESYNTAXE) && (errmask & VNA_ESYNTAXE))
			vna_thread_exit ("Syntax error in network analyzer command.");

		vna_thread_exit ("Network analyzer sent status: %d", status);
	}

	return status;
}

/* Connect to host and return the file descriptor */
int vna_connect (const gchar *host)
{
	int sockfd, errsv, res, valopt;
	struct hostent *he;
	struct sockaddr_in proxy_addr;
	struct in_addr addr;
	long arg;
	fd_set myset;
	struct timeval tv;
	socklen_t lon;
	char buf[VNA_GREET_LEN+1];

	vna_update_netstat ("Connecting to proxy host...");
	
	/* Convert host into addr */
	if (inet_aton (host, &addr) == 0)
	{
		/* host is _not_ an IP */
		if ((he = gethostbyname (host)) == NULL)
		{
			errsv = errno;
			if (errsv == 0)
				/* Could not resolve hostname */
				vna_thread_exit ("Could not resolve hostname, measurement cancelled.");

			/* Unknown error */
			vna_thread_exit ("gethostbyname: %s", g_strerror (errsv));
		}

		addr = *((struct in_addr *)he->h_addr);
	}

	/* Open a socket */
	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
		vna_thread_exit ("socket: %s", g_strerror (errno));

	/* I know, I return the socket, but this way vna_thread_exit can
	 * close the socked by itself if I quit somewhere in this function. */
	if (glob->netwin && (glob->flag & FLAG_VNA_MEAS))
		glob->netwin->sockfd = sockfd;
	else if (glob->calwin && (glob->flag & FLAG_VNA_CAL))
		glob->calwin->sockfd = sockfd;

	/* Set connections details */
	proxy_addr.sin_family = AF_INET;
	proxy_addr.sin_port = htons (VNA_PORT);
	proxy_addr.sin_addr = addr;
	memset (&(proxy_addr.sin_zero), '\0', 8);

	/* Set non-blocking */
	arg = fcntl (sockfd, F_GETFL, NULL); 
	arg |= O_NONBLOCK; 
	fcntl (sockfd, F_SETFL, arg); 

	/* Trying to connect with timeout */
	res = connect (sockfd, (struct sockaddr *)&proxy_addr, sizeof (struct sockaddr)); 

	if (res < 0) 
	{ 
		 if (errno == EINPROGRESS)
		 { 
				tv.tv_sec  = VNA_CONN_TOUT; 
				tv.tv_usec = 0; 
				FD_ZERO (&myset); 
				FD_SET (sockfd, &myset); 
				if (select (sockfd+1, NULL, &myset, NULL, &tv) > 0) { 
					 lon = sizeof (int); 
					 getsockopt (sockfd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon); 
					 if ((valopt) && (valopt == ECONNREFUSED))
						vna_thread_exit ("Connection to proy host refused. "
								"Make sure that Ieee488Proxy is really running.");
					 else if (valopt != 0)
					 	vna_thread_exit( "connect: %s", g_strerror (valopt));
				} 
				else
					 vna_thread_exit ("Timeout while connecting to proxy host.");
		 } 
		 else
				vna_thread_exit ("Error connecting: %s", g_strerror (errno)); 
	} 
	
	/* Set to blocking mode again... */
	arg = fcntl (sockfd, F_GETFL, NULL); 
	arg &= (~O_NONBLOCK); 
	fcntl (sockfd, F_SETFL, arg); 

	/* Get Proxy greetings */
	vna_receiveall (sockfd, buf, VNA_GREET_LEN);
	if (!strncmp (buf, "* PROXYMSG: OK           \r\n", VNA_GREET_LEN))
	{
		vna_update_netstat ("Connection established.");
		return sockfd;
	}
	else if (!strncmp (buf, "* PROXYMSG: Busy         \r\n", VNA_GREET_LEN))
		vna_thread_exit ("Network analyzer is busy, try again later.");
	else if (!strncmp (buf, "* PROXYMSG: Access denied\r\n", VNA_GREET_LEN))
		vna_thread_exit ("Access to Ieee488Proxy is denied. Check proxy settings.");
	else
	{
		/* What the heck? */
		vna_thread_exit ("%s", buf);
	}

	/* We will never reach this point */
	return -1;
}

/* Execute an enter command */
void vna_enter (int sockfd, char *buf, int len, int addr, int errmask)
{
	char *cmd;
	
	cmd = g_strdup_printf ("* PROXYCMD: enter %i %i", len, addr);
	vna_send_cmd (sockfd, cmd, errmask);
	g_free (cmd);

	if (vna_receiveall (sockfd, buf, len))
		vna_thread_exit ("Failed to receive %d bytes for enter().", len);
}

/* Wait for network to report the given status bitmask */
void vna_spoll_wait (int sockfd, int status)
{
	char buf[31];
	int statbyte = 0, counter = 0;

	while (!(statbyte & status) && (counter < 1200))
	{
		buf[0] = '\0';
		vna_send_cmd (sockfd, "* PROXYCMD: spoll "VNA_GBIP, VNA_ETIMEOUT);
		if (vna_receiveall (sockfd, buf, 31))
			vna_thread_exit ("Failed to receive 31 bytes for spoll().");
		if ((sscanf (buf, "* PROXYMSG: spoll result %d", &statbyte) != 1))
			vna_thread_exit ("Could not parse spoll proxy reply: %s", buf);
		usleep (5e5);
		counter++;
	}

	if (counter == 1200)
		vna_thread_exit ("Measurement window did not finish within 5 minutes, something must have gone wrong.");
}

/* Retrieve the measurement points */
ComplexDouble *vna_recv_data (int sockfd, int points)
{
	ComplexDouble *data;
	char read[VNA_RARR_LEN+1];
	char buf[6408], *cmd;
	int nread, i;

	g_return_val_if_fail (glob->netwin || glob->calwin, NULL);
	g_return_val_if_fail (sockfd > 0, NULL);

	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FORM5;OUTPDATA;'", VNA_ETIMEOUT);
	vna_send_cmd (sockfd, "MLA TALK "VNA_GBIP, VNA_ETIMEOUT);

	/* Read the header */
	vna_send_cmd (sockfd, "* PROXYCMD: rarray 4", VNA_ETIMEOUT | VNA_ENOLISTE);
	vna_receiveall (sockfd, read, VNA_RARR_LEN);
	read[VNA_RARR_LEN] = '\0';
	sscanf (read, "* PROXYMSG: Bytes read %d", &nread);
	g_return_val_if_fail (nread == 4, NULL);
	vna_receiveall (sockfd, buf, 4);
	/* Header is 0x23 0x41 0x08 0x19, 0x08 0x19 is 801*8 bytes */

	/* Read the data (e.g. 801*4*2 bytes = 6408 bytes) */
	cmd = g_strdup_printf ("* PROXYCMD: rarray %i", points*8);
	vna_send_cmd (sockfd, cmd, VNA_ETIMEOUT);
	g_free (cmd);
	vna_receiveall (sockfd, read, VNA_RARR_LEN);
	read[VNA_RARR_LEN] = '\0';
	sscanf (read, "* PROXYMSG: Bytes read %d", &nread);
	g_return_val_if_fail (nread == points*8, NULL);
	vna_receiveall (sockfd, buf, points*8);

	data = g_new (ComplexDouble, points);
	for (i=0; i<points; i++)
	{
		data[i].re = (gdouble) *((gfloat *) &buf[8*i+0]);
		data[i].im = (gdouble) *((gfloat *) &buf[8*i+4]);
		data[i].abs = sqrt(data[i].re*data[i].re + data[i].im*data[i].im);
	}

	return data;
}

/* Take a snapshot of the current VNA display */
static void vna_take_snapshot ()
{
	int sockfd, i, points=0;
	char enterbuf[30];
	float f_points;
	gdouble start=0.0, stop=0.0, *frq;
	ComplexDouble *data;
	DataVector *dvec;
	FILE *outfh;
#ifndef NO_ZLIB
	gzFile *gzoutfh;
#endif
	struct timeval tv;
	struct tm* ptm;
	char time_string[22];
	NetworkGraphUpdateData *graphupdate;

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	sockfd = glob->netwin->sockfd;

	vna_update_netstat ("Setting up network analyzer...");
	
	/* Local lock out */
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" LLO", VNA_ETIMEOUT);
	/* Set data format */
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FORM5;'", VNA_ETIMEOUT);
	/* Get start frequency */
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'STAR; OUTPACTI;'", VNA_ETIMEOUT);
	vna_enter (sockfd, enterbuf, 30, VNA_GBIP_INT, VNA_ETIMEOUT); 
	sscanf (enterbuf, "%lf", &start);
	/* Get stop frequency */
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'STOP; OUTPACTI;'", VNA_ETIMEOUT);
	vna_enter (sockfd, enterbuf, 30, VNA_GBIP_INT, VNA_ETIMEOUT); 
	sscanf (enterbuf, "%lf", &stop);
	/* Get number of points */
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'POIN; OUTPACTI;'", VNA_ETIMEOUT);
	vna_enter (sockfd, enterbuf, 30, VNA_GBIP_INT, VNA_ETIMEOUT); 
	sscanf (enterbuf, "%f", &f_points);
	points = (int) f_points;

	if ((start == 0.0) || (stop == 0.0) || (points == 0))
	{
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);
		vna_thread_exit ("Could not get frequency range information.");
	}
	if (points == 1)
	{
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);
		vna_thread_exit ("Single points cannot be measured.");
	}

	/* Get data */
	vna_update_netstat ("Reading datapoints from network analyzer...");
	data = vna_recv_data (sockfd, points);
	g_return_if_fail (data);

	/* Clear network analyzer display by sending an "entry off" */
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'ENTO;'", VNA_ETIMEOUT);

	/* Go back to local mode */
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);

	/* Create frequency list */
	frq = g_new (gdouble, points);
	for (i=0; i<points; i++)
		frq[i] = start + (gdouble)i/(gdouble)(points-1) * (stop-start);

	gettimeofday (&tv, NULL);
	ptm = localtime (&tv.tv_sec);
	strftime (time_string, sizeof (time_string), "%Y-%m-%d %H:%M:%S", ptm);

	if (!glob->netwin->compress)
	{
		if (!(outfh = fopen (glob->netwin->fullname[0], "w")))
			vna_thread_exit ("Could not open output file for writing.");
		fprintf (outfh, "# Date of measurement: %s\r\n", time_string);
		fprintf (outfh, "# Measurement type   : Snapshot of network analyzer display\r\n");
		fprintf (outfh, "# Frequency range    : %.3f - %.3f GHz (%.3f kHz resolution)\r\n",
				start/1e9, stop/1e9, (frq[1]-frq[0])/1e3);
		if (glob->netwin->comment)
			fprintf (outfh, "# Comment            : %s\r\n", glob->netwin->comment);
		fprintf (outfh, DATAHDR);
		for (i=0; i<points; i++)
			fprintf (outfh, DATAFRMT, frq[i], data[i].re, data[i].im);
		fclose (outfh);
	}
	else
	{
#ifndef NO_ZLIB
		if (!(gzoutfh = gzopen (glob->netwin->fullname[0], "w")))
			vna_thread_exit ("Could not open output file for writing.");
		gzprintf (gzoutfh, "# Date of measurement: %s\r\n", time_string);
		gzprintf (gzoutfh, "# Measurement type   : Snapshot of network analyzer display\r\n");
		gzprintf (gzoutfh, "# Frequency range    : %.3f - %.3f GHz (%.3f kHz resolution)\r\n",
				start/1e9, stop/1e9, (frq[1]-frq[0])/1e3);
		if (glob->netwin->comment)
			gzprintf (gzoutfh, "# Comment            : %s\r\n", glob->netwin->comment);
		gzprintf (gzoutfh, DATAHDR);
		for (i=0; i<points; i++)
			gzprintf (gzoutfh, DATAFRMT, frq[i], data[i].re, data[i].im);
		gzclose (gzoutfh);
#endif
	}

	dvec = g_new (DataVector, 1);
	dvec->x = frq;
	dvec->y = data;
	dvec->len = points;
	dvec->file = g_strdup (glob->netwin->fullname[0]);
	dvec->index = 0;

	graphupdate = g_new (NetworkGraphUpdateData, 1);
	graphupdate->dvec = dvec;
	graphupdate->pos = 0;

	g_timeout_add (1, (GSourceFunc) vna_add_data_to_graph, graphupdate);
}

/* Calculate the ms to wait for a window to be measured */
static glong vna_sweep_cal_sleep ()
{
	glong delta;
	
	if (glob->netwin->swpmode == 1)
		delta = 380 * glob->netwin->avg + 700;
	else
	{
		switch (glob->netwin->avg)
		{
			case 1: case 2: case 4: case 8: case 16:
				delta = 40000;
			case 32: case 64:
				delta = 45000;
			case 128:
				delta = 65000;
			case 256:
				delta = 75000;
			default:
				/* Should not be reached */
				delta = 50000;
		}

		/* Prevent numerous timeouts */
		delta += 2000;
	}

	if (strlen (glob->netwin->param) == 1)
		delta *= (glob->netwin->swpmode == 1) ? 5 : 1.5;

	if (!strcmp (glob->netwin->param, "TRL") == 1)
		delta *= (glob->netwin->swpmode == 1) ? 7 : 2.0;

	return delta;
}

/* Write the data file header for sweep measurements
 * pos: the position in the netwin->fullname array
 * sparam: the cleartext name of the measured S-parameter
 * netwin: the rest of the information */
static gboolean vna_write_header (gint pos, gchar *sparam, NetworkWin *netwin)
{
	int h, m, s;
	struct timeval tv;
	struct tm* ptm;
	char time_string[22];
	FILE *outfh;

	gettimeofday (&tv, NULL);
	ptm = localtime (&tv.tv_sec);
	strftime (time_string, sizeof (time_string), "%Y-%m-%d %H:%M:%S", ptm);
	sec_to_hhmmss (netwin->estim_t, &h, &m, &s);

	if (!glob->netwin->compress)
	{
		if (!(outfh = fopen (netwin->fullname[pos], "w")))
			return FALSE;

		fprintf (outfh, "# Date of measurement: %s (estimated duration %02d:%02d:%02d)\r\n", 
				time_string, h, m, s);
		fprintf (outfh, "# Measurement type   : Frequency sweep\r\n");
		fprintf (outfh, "# Frequency range    : %.3f - %.3f GHz (%.3f kHz resolution)\r\n",
				netwin->start/1e9, netwin->stop/1e9, netwin->resol/1e3);
		fprintf (outfh, "# Stimulus settings  : %s, %dx averaging, %s mode\r\n",
				sparam, netwin->avg, netwin->swpmode == 1 ? "ramp" : "step");
		if (netwin->comment)
			fprintf (outfh, "# Comment            : %s\r\n", netwin->comment);
		fprintf (outfh, DATAHDR);
		fclose (outfh);
	}
	else
	{
#ifndef NO_ZLIB
		if (!(netwin->gzoutfh[pos] = gzopen (netwin->fullname[pos], "w")))
			return FALSE;

		gzprintf (netwin->gzoutfh[pos], "# Date of measurement: %s (estimated duration %02d:%02d:%02d)\r\n", 
				time_string, h, m, s);
		gzprintf (netwin->gzoutfh[pos], "# Measurement type   : Frequency sweep\r\n");
		gzprintf (netwin->gzoutfh[pos], "# Frequency range    : %.3f - %.3f GHz (%.3f kHz resolution)\r\n",
				netwin->start/1e9, netwin->stop/1e9, netwin->resol/1e3);
		gzprintf (netwin->gzoutfh[pos], "# Stimulus settings  : %s, %dx averaging, %s mode\r\n",
				sparam, netwin->avg, netwin->swpmode == 1 ? "ramp" : "step");
		if (netwin->comment)
			gzprintf (netwin->gzoutfh[pos], "# Comment            : %s\r\n", netwin->comment);
		gzprintf (netwin->gzoutfh[pos], DATAHDR);
#endif
	}

	return TRUE;
}

/* Measure a whole frequency range by dividing it into windows */
static void vna_sweep_frequency_range ()
{
	NetworkWin *netwin;
	int sockfd, i = 0, j, winleft, windone, h, m, s, Si;
	char cmdstr[81];
	GTimeVal starttime, curtime, difftime;
	struct timeval tv;
	struct tm* ptm;
	char time_string[22];
	gdouble fstart, fstop;
	ComplexDouble *data = NULL;
	DataVector *dvec;
	gchar *sparam[] = {"S11", "S12", "S21", "S22", 
		           "a1 (drive: port 1, lock: a1, numer: a2, denom: a1, conv: S)", 
			   "a2 (drive: port 2, lock: a2, numer: a2, denom: a1, conv: S)"};
	NetworkGraphUpdateData *graphupdate;
	FILE *outfh;

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	netwin = glob->netwin;
	sockfd = netwin->sockfd;

	/* Bring the VNA into the right state of mind */
	vna_update_netstat ("Setting up network analyzer...");

	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'PRES;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_ms_sleep (5000);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'POIN801;'", VNA_ETIMEOUT|VNA_ESYNTAXE);

	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" LLO", VNA_ETIMEOUT);
	if ((strlen (netwin->param) == 1) || !(strcmp (netwin->param, "TRL")))
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'FOUPSPLI;WAIT;'");
	else
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA '%s;'", netwin->param);
	vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_ms_sleep (2000);

	if (netwin->swpmode == 1)
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'RAMP;'");
	else
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'STEP;'");
	vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);

	g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'AVERON %d;'", netwin->avg);
	vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
	
	/* Write datafile header */
	gettimeofday (&tv, NULL);
	ptm = localtime (&tv.tv_sec);
	strftime (time_string, sizeof (time_string), "%Y-%m-%d %H:%M:%S", ptm);
	sec_to_hhmmss (netwin->estim_t, &h, &m, &s);

	if ((strlen (netwin->param) == 1) || !(strcmp (netwin->param, "TRL")))
	{
		/* Full S-matrix measurement */
		for (Si=0; Si<4; Si++)
		{
			if (!vna_write_header (Si, sparam[Si], netwin))
			{
				vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);
				vna_thread_exit ("Could not open output file for writing.");
			}
		}

		if (!strcmp (netwin->param, "TRL"))
		{
			/* TRL measurement */
			if (!vna_write_header (4, sparam[4], netwin))
			{
				vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);
				vna_thread_exit ("Could not open output file for writing.");
			}
			if (!vna_write_header (5, sparam[5], netwin))
			{
				vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);
				vna_thread_exit ("Could not open output file for writing.");
			}
		}
	}
	else
	{
		/* Single S-matrix measurement */
		if (!vna_write_header (0, netwin->param, netwin))
		{
			vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);
			vna_thread_exit ("Could not open output file for writing.");
		}
	}

	/* Measure those frequency windows */
	g_get_current_time (&starttime);
	windone = 0;
	winleft = (int) ceil((netwin->stop - netwin->start)/(801.0*netwin->resol));
	for (fstart=netwin->start; fstart<=netwin->stop; fstart+=netwin->resol*801.0)
	{
		fstop = fstart + netwin->resol * 801.0;
		vna_update_netstat ("Measuring %6.3f - %6.3f GHz...", fstart/1e9, fstop/1e9);

		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'STAR %.1lf HZ;STOP %.1lf HZ;'", 
				fstart, fstop);
		vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'AUTO;NUMG %d;'", 
				netwin->swpmode==1 ? netwin->avg+1 : 1);
		vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);

		/* Wait for this part of the measurement to finish */
		vna_send_cmd (sockfd, "DCL", VNA_ETIMEOUT|VNA_ESYNTAXE);
		vna_spoll_wait (sockfd, 16);

		Si = 0;
		while (netwin->fullname[Si] && (Si<6))
		{
			/* Get data */
			if (netwin->fullname[1] && (Si < 4))
			{
				/* Full S-matrix measurement -> set correct S-parameter */
				g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA '%s;'", sparam[Si]);
				vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
			}
			else
			{
				if (Si == 4)
				{
					/* Prepare TRL measurement */
					vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SPLI;WAIT;'", VNA_ETIMEOUT);

					vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CHAN1;USER3;'", VNA_ETIMEOUT);
					vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA "
						"'DRIVPORT1;LOCKA1;NUMEA2;DENOA1;CONVS;REDD;'", VNA_ETIMEOUT);
					vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CHAN2;USER4;'", VNA_ETIMEOUT);
					vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA "
						"'DRIVPORT2;LOCKA2;NUMEA2;DENOA1;CONVS;REDD;'", VNA_ETIMEOUT);
					vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CHAN1;'", VNA_ETIMEOUT);

					/* and wait */
					g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'NUMG %d;'", 
							netwin->swpmode==1 ? netwin->avg+1 : 1);
					vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
					vna_send_cmd (sockfd, "DCL", VNA_ETIMEOUT|VNA_ESYNTAXE);
					vna_spoll_wait (sockfd, 16);
				}
				if (Si == 5)
					vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CHAN2;'", VNA_ETIMEOUT);
			}
			vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'AUTO;'", VNA_ETIMEOUT);
			data = vna_recv_data (sockfd, 801);
			g_return_if_fail (data);

			if (Si == 5)
				/* TRL measurement done -> back to four parameter split */
				vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FOUPSPLI;WAIT;'", VNA_ETIMEOUT);

			/* Write new data to file */
			if (!netwin->compress)
			{
				if (!(outfh = fopen (netwin->fullname[Si], "a")))
				{
					vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);
					vna_thread_exit ("Could not open output file for writing.");
				}
				for (i=0; (i<801) && (fstart+(gdouble)i*netwin->resol <= netwin->stop); i++)
					fprintf (outfh, DATAFRMT,
							fstart+(gdouble)i*netwin->resol, data[i].re, data[i].im);
				fclose (outfh);
			}
			else
			{
#ifndef NO_ZLIB
				/* There ain't no append for gzopen... */
				for (i=0; (i<801) && (fstart+(gdouble)i*netwin->resol <= netwin->stop); i++)
					gzprintf (netwin->gzoutfh[Si], DATAFRMT,
							fstart+(gdouble)i*netwin->resol, data[i].re, data[i].im);
#endif
			}
			/* Now: i == number of datapoints written */

			/* Display measured data */
			if (netwin->start == fstart)
			{
				/* First window, initialize graph */
				dvec = new_datavector ( (guint) ((netwin->stop - netwin->start)/netwin->resol) + 1 );
				dvec->file = g_strdup (netwin->fullname[Si]);
				for (j=0; j<i; j++)
				{
					dvec->x[j] = netwin->start + (gdouble)j * netwin->resol;
					dvec->y[j] = data[j];
				}
				g_free (data);
				for (j=i; j<dvec->len; j++)
				{
					/* Initialize the rest of the vector */
					dvec->x[j] = netwin->start + (gdouble)j * netwin->resol;
					dvec->y[j].re = dvec->y[j].im = dvec->y[j].abs = 0.0;
				}
			}
			else
			{
				dvec       = g_new0 (DataVector, i);
				dvec->len  = i;
				dvec->x    = g_new (gdouble, 1);
				dvec->x[0] = fstart;
				dvec->y    = data; /* data may be longer than dvec but this doesn't matter */
			}

			/* Update main graph, vna_add_data_to_graph frees data */
			graphupdate       = g_new (NetworkGraphUpdateData, 1);
			graphupdate->dvec = dvec;
			graphupdate->pos  = Si;

			g_timeout_add (1, (GSourceFunc) vna_add_data_to_graph, graphupdate);

			/* Next S-parameter */
			Si++;
		}

		if (winleft)
		{
			/* Recalcualte ETA (only if we aren't finished yet) */
			g_get_current_time (&curtime);
			difftime.tv_sec  = curtime.tv_sec  - starttime.tv_sec;
			difftime.tv_usec = curtime.tv_usec - starttime.tv_usec;
			windone++;	/* Frequency windoes already measured */
			winleft--;	/* Those left to be measured */

			netwin->estim_t = curtime.tv_sec - netwin->start_t
				+ (glong)( (float)difftime.tv_sec  * ((float)winleft/(float)windone))
				+ (glong)(((float)difftime.tv_usec * ((float)winleft/(float)windone))/1e6);
		}

		/* Has anyone canceled the measurement? */
		if (! (glob->flag & FLAG_VNA_MEAS) )
		{
			if (sockfd)
			{
				if (netwin->type == 1)
					vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'RAMP;CONT;'", 0);
				vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", 0);
			}
			vna_thread_exit (NULL);
		}

		/* Finished with this window, start next one */
	}

#ifndef NO_ZLIB
	if (netwin->compress)
	{
		for (i=0; i<6; i++)
			if (netwin->gzoutfh[i])
			{
				gzclose(netwin->gzoutfh[i]);
				netwin->gzoutfh[i] = NULL;
			}
	}
#endif


	/* Go back to continuous and local mode */
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'RAMP;CONT;ENTO;'", 0);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);
}

/* The main routine of the measurement thread */
static void vna_start ()
{
	NetworkWin *netwin;
	GTimeVal curtime;
	gint i;

	g_return_if_fail (glob->netwin);

	if (glob->flag & FLAG_VNA_CAL)
	{
		/* An online calibration is running */
		g_timeout_add (1, (GSourceFunc) dialog_message, 
			"Only one connection to an Ieee488Proxy can be open at a time.");
		g_thread_exit (NULL);
	}

	netwin = glob->netwin;
	netwin->sockfd = -1;

	for (i=0; i<6; i++)
	{
		netwin->index[i] = 0;
		netwin->ydata[i] = NULL;
#ifndef NO_ZLIB
		glob->netwin->gzoutfh[i] = NULL;
#endif
	}

	glob->flag |= FLAG_VNA_MEAS;

	/* Connect to Ieee488Proxy */
	netwin->sockfd = (gint) vna_connect (netwin->host);
	if (netwin->sockfd < 0)
		vna_thread_exit ("Could not connect to proxy.");

	/* Give the Ieee488 card GPIB 21 */
	if (vna_send_cmd (netwin->sockfd, "* PROXYCMD: initialize 21", VNA_ETIMEOUT))
		vna_thread_exit ("Could not initialize Ieee488 Card.");

	/* Set up time estimates */
	g_get_current_time (&curtime);
	netwin->start_t = curtime.tv_sec;
	if (netwin->type == 1)
	{
		/* add time estimate for frequency sweep */
		netwin->estim_t = 8;
		netwin->estim_t += (glong) ( (((float)vna_sweep_cal_sleep()/1000) + 0.56)
				* ceil((netwin->stop - netwin->start)/netwin->resol/801.0));
		g_timeout_add (500, (GSourceFunc) vna_show_time_estimates, NULL);

		/* Start the measurement */
		vna_sweep_frequency_range ();

		/* Make sure, that ETA is correct now. ;-) */
		g_get_current_time (&curtime);
		netwin->estim_t = curtime.tv_sec - netwin->start_t;
		vna_thread_exit ("Measurement completed.");
	}
	else
	{
		vna_take_snapshot ();
		vna_thread_exit (NULL);
	}

}
