#include <gtk/gtk.h>
#include <glade/glade.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

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
#include "vna_proxy.h"
#include "vna_n5230a.h"

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
	gint index;

	g_return_if_fail (glob->netwin);
	netwin = glob->netwin;

	g_return_if_fail (glob->netwin);
	netwin = glob->netwin;

	g_return_if_fail (netwin->xmlnet);

	if (glob->netwin->vnamodel == 1)
	{
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_8510_radio")),
			TRUE);
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_5230_radio")),
			FALSE);
		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->netwin->xmlnet, "vna_advanced_frame"),
			FALSE);
	}
	else
	{
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_8510_radio")),
			FALSE);
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_5230_radio")),
			TRUE);
	}

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

	if (netwin->format == 1)
	{
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_dat_radio")),
			TRUE);
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_snp_radio")),
			FALSE);
	}
	else
	{
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_dat_radio")),
			FALSE);
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_snp_radio")),
			TRUE);
	}

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
		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->netwin->xmlnet, "vna_advanced_frame"),
			FALSE);
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

	if (netwin->param[0] != '\0')
	{
		if (!strcmp (netwin->param, "S11")) index = 0;
		if (!strcmp (netwin->param, "S12")) index = 1;
		if (!strcmp (netwin->param, "S21")) index = 2;
		if (!strcmp (netwin->param, "S22")) index = 3;
		if (!strcmp (netwin->param, "S"))   index = 4;
		if (!strcmp (netwin->param, "TRL")) index = 5;
		gtk_combo_box_set_active (
			GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_s_combo")),
			index);
	}

	if (netwin->avg > 0)
	{
		index = abs (log (netwin->avg) / log (2.0) + 0.1);
		gtk_combo_box_set_active (
			GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_avg_combo")),
			index);
	}

	if (netwin->swpmode == 1)
	{
		/* ramp mode */
		gtk_combo_box_set_active (
			GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_stim_combo")),
			0);
	}
	else
	{
		/* step mode */
		gtk_combo_box_set_active (
			GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_stim_combo")),
			1);
	}

	if (netwin->bandwidth)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_bw_entry"));
		text = g_strdup_printf ("%g", netwin->bandwidth / 1e3);
		gtk_entry_set_text (entry, text);
		g_free (text);
	}

	if (netwin->dwell >= 0)
	{
		entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_dwell_entry"));
		text = g_strdup_printf ("%g", netwin->dwell * 1e6);
		gtk_entry_set_text (entry, text);
		g_free (text);
	}

	gtk_combo_box_set_active (
		GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_cal_combo")),
		netwin->calmode);
}

/* Connect the VNA accessing backend functions */
void vna_connect_backend (VnaBackend *vna_func)
{
	g_return_if_fail (vna_func);
	g_return_if_fail (glob->netwin);

	if (glob->netwin->vnamodel == 1)
	{
		/* HP 8510C via Ieee488Proxy */
		vna_func->connect = &vna_proxy_connect;
		vna_func->recv_data = &vna_proxy_recv_data;
		vna_func->gtl = &vna_proxy_gtl;
		vna_func->llo = &vna_proxy_llo;
		vna_func->sweep_cal_sleep = &vna_proxy_sweep_cal_sleep;
		vna_func->get_start_frq = &vna_proxy_get_start_frq;
		vna_func->get_stop_frq = &vna_proxy_get_stop_frq;
		vna_func->get_points = &vna_proxy_get_points;
		vna_func->sweep_prepare = &vna_proxy_sweep_prepare;
		vna_func->set_startstop = &vna_proxy_set_startstop;
		vna_func->trace_scale_auto = &vna_proxy_trace_scale_auto;
		vna_func->trace_fourparam = &vna_proxy_trace_fourparam;
		vna_func->set_numg = &vna_proxy_set_numg;
		vna_func->wait = &vna_proxy_wait;
		vna_func->select_s = &vna_proxy_select_s;
		vna_func->sel_first_par = &vna_proxy_sel_first_par;
		vna_func->select_trl = &vna_proxy_select_trl;
		vna_func->calibrate = NULL;
		vna_func->cal_recall = NULL;
		vna_func->round_bwid = &vna_proxy_round_bwid;
		vna_func->get_capa = &vna_proxy_get_capa;
		glob->netwin->points = (gint) vna_proxy_get_capa (3);
	}
	else
	{
		/* Agilent N5230A-L via LAN socket */
		vna_func->connect = &vna_n5230a_connect;
		vna_func->recv_data = &vna_n5230a_recv_data;
		vna_func->gtl = &vna_n5230a_gtl;
		vna_func->llo = &vna_n5230a_llo;
		vna_func->sweep_cal_sleep = &vna_n5230a_sweep_cal_sleep;
		vna_func->get_start_frq = &vna_n5230a_get_start_frq;
		vna_func->get_stop_frq = &vna_n5230a_get_stop_frq;
		vna_func->get_points = &vna_n5230a_get_points;
		vna_func->sweep_prepare = &vna_n5230a_sweep_prepare;
		vna_func->set_startstop = &vna_n5230a_set_startstop;
		vna_func->trace_scale_auto = &vna_n5230a_trace_scale_auto;
		vna_func->trace_fourparam = &vna_n5230a_trace_fourparam;
		vna_func->set_numg = &vna_n5230a_set_numg;
		vna_func->wait = &vna_n5230a_wait;
		vna_func->select_s = &vna_n5230a_select_s;
		vna_func->sel_first_par = &vna_n5230a_sel_first_par;
		vna_func->select_trl = &vna_n5230a_select_trl;
		vna_func->calibrate = &vna_n5230a_calibrate;
		vna_func->cal_recall = &vna_n5230a_cal_recall;
		vna_func->round_bwid = &vna_n5230a_round_bwid;
		vna_func->get_capa = &vna_n5230a_get_capa;
		glob->netwin->points = (gint) vna_n5230a_get_capa (3);
	}
}

/* Update the netwin struct with the GUI entries after doing some sanity checks */
static int network_gui_to_struct ()
{
	NetworkWin *netwin;
	GtkTreeIter iter;
	GtkEntry *entry;
	const gchar *text;
	gdouble val;

	g_return_val_if_fail (glob->netwin, 1);
	netwin = glob->netwin;

	g_return_val_if_fail (netwin->xmlnet, 1);

	/* Connect the VNA accessing backend functions */
	vna_connect_backend (glob->netwin->vna_func);

	/* VNA host */
	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_host_entry"));
	text = gtk_entry_get_text (entry);
	if (!strlen(text))
	{
		dialog_message ("Please enter a host name or host IP.");
		return 1;
	}
	g_free (netwin->host);
	netwin->host = g_strdup (text);

	/* calibration mode */
	netwin->calmode = gtk_combo_box_get_active (
			GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_cal_combo")));

	/* file format */
	if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (netwin->xmlnet, "vna_dat_radio"))))
		netwin->format = 1;
	else
		netwin->format = 2;

	/* datafile path */
	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_path_entry"));
	text = gtk_entry_get_text (entry);
	g_free (netwin->path);
	if (strlen (text))
		netwin->path = g_strdup (text);
	else
		netwin->path = NULL;

	/* datafile name */
	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_file_entry"));
	text = gtk_entry_get_text (entry);
	if (netwin->calmode < 2)
	{
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
		if ((!g_str_has_suffix (text, ".dat")) && (netwin->format == 1))
			if (dialog_question ("You output file does not have the suffix '.dat', proceed anyway?")
					!= GTK_RESPONSE_YES)
				return 1;
		g_free (netwin->file);
		netwin->file = g_strdup (text);
	}

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
	if (val < netwin->vna_func->get_capa (1))
	{
		dialog_message ("Start frequency must not be less than %.3f GHz.", netwin->vna_func->get_capa (1));
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
	if (val > netwin->vna_func->get_capa (2))
	{
		dialog_message ("Stop frequency must be below %.3f GHz.",netwin->vna_func->get_capa (2));
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

	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_s_combo")), &iter);
	gtk_tree_model_get (
			GTK_TREE_MODEL (gtk_combo_box_get_model (GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_s_combo")))),
			&iter, 0, &text, -1);
	snprintf (netwin->param, 4, "%s", text);

	if (strlen (netwin->param) == 1)
		netwin->numparam = 4; /* Full S-matrix -> 4 parameters */
	else if (!(strcmp (netwin->param, "TRL")))
		netwin->numparam = 6; /* TRL -> 6 parameters */
	else
		netwin->numparam = 1; /* Single element -> 1 parameter */

	if ( (netwin->numparam == 6) && (netwin->format == 2) && (netwin->type == 1))
	{
		/* TRL selected and SNP format are not possible */
		dialog_message ("You cannot use the SNP format for TRL type measurements.");
		return 1;
	}

	if ((netwin->type == 1) && (netwin->calmode < 2))
	{
		if ( (netwin->numparam > 1) && (netwin->format == 1) 
		     && (!g_strrstr (netwin->file, "%%")) )
		{
			/* S or TRL selected, DAT format but no wild-card */
			dialog_message ("Output filename does not contain wild-card \"%%%%\".");
			return 1;
		}
		if ( (netwin->numparam == 4) && (netwin->format == 2) 
		     && (!g_str_has_suffix (netwin->file, ".s2p")) )
		{
			/* S selected, SNP format but wrong suffix */
			dialog_message ("The output filename must have '.s2p' as suffix.");
			return 1;
		}
		if ( (netwin->numparam == 1) && (netwin->format == 2) 
		     && (!g_str_has_suffix (netwin->file, ".s1p")) )
		{
			/* S?? selected, SNP format but wrong suffix */
			dialog_message ("The output filename must have '.s1p' as suffix.");
			return 1;
		}
	} 
	else 
		if ((netwin->type == 2) && (netwin->format == 2)
		     && (!g_str_has_suffix (netwin->file, ".s1p")) )
		{
			/* SNP format but wrong suffix in snapshot mode */
			dialog_message ("The output filename must have '.s1p' as suffix.");
			return 1;
		}

	
	netwin->avg = 1 << gtk_combo_box_get_active (
			GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_avg_combo")));

	/* ramp mode -> 1; step mode -> 2 */
	netwin->swpmode = 1 + gtk_combo_box_get_active (
			GTK_COMBO_BOX (glade_xml_get_widget (netwin->xmlnet, "vna_stim_combo")));

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_bw_entry"));
	text = gtk_entry_get_text (entry);
	if (sscanf (text, "%lf", &val) != 1)
	{
		dialog_message ("Could not parse IF bandwidth.");
		return 1;
	}
	val *= 1e3;
	if (netwin->vna_func->get_capa (4) > 0)
	{
		if (val < netwin->vna_func->get_capa (4))
		{
			dialog_message ("IF bandwidth must not be less than %.3f kHz.",
				netwin->vna_func->get_capa (4) / 1e3);
			return 1;
		}
		if (val > netwin->vna_func->get_capa (5))
		{
			dialog_message ("IF bandwidth must not be greater than %.3f kHz.",
				netwin->vna_func->get_capa (5) / 1e3);
			return 1;
		}
	}
	netwin->bandwidth = netwin->vna_func->round_bwid (val);
	if (fabs (netwin->bandwidth - val) > 1)
		dialog_message ("IF bandwidth rounded to nearest possible value "
				"of %.3f kHz.", netwin->bandwidth / 1e3);

	entry = GTK_ENTRY (glade_xml_get_widget (netwin->xmlnet, "vna_dwell_entry"));
	text = gtk_entry_get_text (entry);
	if (sscanf (text, "%lf", &val) != 1)
	{
		dialog_message ("Could not parse dwell time.");
		return 1;
	}
	if (val < 0)
	{
		dialog_message ("Dwell time must not be negative");
		return 1;
	}
	netwin->dwell = val / 1e6;

	return 0;
}

/* Open the main measurement dialog */
void network_open_win ()
{
	GladeXML *xmlnet;

	if (glob->netwin == NULL)
	{
		/* Populate struct with sensible default values */
		glob->netwin = g_new0 (NetworkWin, 1);
		glob->netwin->vnamodel = glob->prefs->vnamodel;
		glob->netwin->fullname[0] = NULL;
		glob->netwin->fullname[1] = NULL;
		glob->netwin->fullname[2] = NULL;
		glob->netwin->fullname[3] = NULL;
		glob->netwin->fullname[4] = NULL;
		glob->netwin->fullname[5] = NULL;
		glob->netwin->comment = NULL;
		glob->netwin->compress = FALSE;
		glob->netwin->format = 1;
		glob->netwin->type = 1;
		glob->netwin->start =  0.045e9;
		glob->netwin->stop = 20e9;
		glob->netwin->resol = 100e3;
		glob->netwin->param[0] = 'S';
		glob->netwin->param[1] = '1';
		glob->netwin->param[2] = '2';
		glob->netwin->param[3] = '\0';
		glob->netwin->numparam = 0;
		glob->netwin->points = 801;
		glob->netwin->avg = 4;
		glob->netwin->swpmode = 1;
		glob->netwin->bandwidth = 10000;
		glob->netwin->dwell = 0;
		glob->netwin->calmode = 0;
		glob->netwin->vna_func = NULL;
		if (glob->prefs->vnahost)
			glob->netwin->host = g_strdup (glob->prefs->vnahost);
	}

	if (glob->netwin->vna_func == NULL)
		glob->netwin->vna_func = g_new0 (VnaBackend, 1);

	if (glob->netwin->xmlnet == NULL)
	{
		/* Create, show and populate measurement window */
		xmlnet = glade_xml_new (GLADEFILE, "vna_win", NULL);
		glob->netwin->xmlnet = xmlnet;
		glade_xml_signal_autoconnect (xmlnet);

#ifdef NO_ZLIB
		gtk_widget_hide_all (glade_xml_get_widget (xmlnet, "vna_compress_check"));
#endif

		network_struct_to_gui ();
	}
}

gboolean network_create_files ()
{
	gchar *filename, *tmpname, *pos;
	gint i, j;

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
	if ((glob->netwin->numparam == 1) || (glob->netwin->format == 2))
	{
		/* (single S-parameter measurement) or (SNP format) */
		if (!file_is_writeable (filename))
		{
			g_free (filename);
			return FALSE;
		}

		glob->netwin->fullname[0] = filename;
	}
	else
	{
		/* Full S-matrix measurement (S or TRL) in DAT format */
		pos = g_strrstr (filename, "%%");
		g_return_val_if_fail (pos, FALSE);

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
					return FALSE;
				}

				glob->netwin->fullname[2*i+j] = g_strdup (filename);
			}

		if (glob->netwin->numparam == 6)
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
					return FALSE;
				}
				glob->netwin->fullname[4+i] = g_strdup (filename);
			}
		}

		pos[0] = '%';
		pos[1] = '%';
	}

	return TRUE;
}

/* The user clicked the start button */
void on_vna_start_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->xmlnet);
	
	/* Update glob->netwin struct */
	if (network_gui_to_struct ())
		return;

	if (glob->netwin->calmode < 2)
	{
		if (!network_create_files ())
			return;
	} else {
		if (dialog_question ("Is the ECal module connected to the PNA and properly warmed up?") 
				!= GTK_RESPONSE_YES)
			return;
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
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->netwin->xmlnet, "vna_advanced_frame"),
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
	gtk_widget_set_sensitive (
		glade_xml_get_widget (glob->netwin->xmlnet, "vna_advanced_frame"),
		gtk_toggle_button_get_active (toggle) && (glob->netwin->vnamodel == 2));
}

/* Change VNA model */
void on_vna_model_change (GtkToggleButton *toggle, gpointer user_data)
{
	if ((!glob->netwin) || (!glob->netwin->xmlnet))
		return;

	if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (glob->netwin->xmlnet, "vna_8510_radio"))))
	{
		glob->netwin->vnamodel = 1;
		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->netwin->xmlnet, "vna_advanced_frame"),
			FALSE);
	}
	else
	{
		glob->netwin->vnamodel = 2;
		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->netwin->xmlnet, "vna_advanced_frame"),
			TRUE && gtk_toggle_button_get_active (
				GTK_TOGGLE_BUTTON (glade_xml_get_widget (glob->netwin->xmlnet, "vna_sweep_radio"))
			));
	}
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
		if ((glob->netwin->type == 1) && (glob->netwin->vnamodel > 1))
			gtk_widget_set_sensitive (
				glade_xml_get_widget (glob->netwin->xmlnet, "vna_advanced_frame"),
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

/* Exit the thread cleanly */
void vna_thread_exit (gchar *format, ...)
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
void vna_update_netstat (gchar *format, ...)
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
void vna_ms_sleep (glong ms)
{
	g_return_if_fail (glob->netwin);

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
			glob->netwin->vna_func->gtl ();
			vna_thread_exit (NULL);
		}
	}
}

/* Take a snapshot of the current VNA display */
static void vna_take_snapshot ()
{
	VnaBackend *vna_func;
	int i;
	gint points=0;
	gdouble start=0.0, stop=0.0, *frq;
	ComplexDouble *data;
	DataVector *dvec;
	FILE *outfh;
#ifndef NO_ZLIB
	gzFile *gzoutfh;
#endif
	struct timeval tv;
	struct tm* ptm;
	char time_string[22], comment_char;
	NetworkGraphUpdateData *graphupdate;

	g_return_if_fail (glob->netwin);
	vna_func = glob->netwin->vna_func;

	vna_update_netstat ("Setting up network analyzer...");
	
	/* Local lock out */
	vna_func->llo ();
	/* Select measurement */
	if (!vna_func->sel_first_par ())
	{
		vna_func->gtl ();
		vna_thread_exit ("Could not select a measurement.");
	}
	/* Get current start and stop frequency */
	start = vna_func->get_start_frq ();
	stop  = vna_func->get_stop_frq ();
	/* Get number of points */
	points = vna_func->get_points ();

	if ((start == 0.0) || (stop == 0.0) || (points == 0))
	{
		vna_func->gtl ();
		vna_thread_exit ("Could not get frequency range information.");
	}
	if (points == 1)
	{
		vna_func->gtl ();
		vna_thread_exit ("Single points cannot be measured.");
	}

	/* Get data */
	vna_update_netstat ("Reading datapoints from network analyzer...");
	data = vna_func->recv_data (points);
	g_return_if_fail (data);

	/* Clear VNA display and go back to local mode */
	vna_func->gtl ();

	/* Create frequency list */
	frq = g_new (gdouble, points);
	for (i=0; i<points; i++)
		frq[i] = start + (gdouble)i/(gdouble)(points-1) * (stop-start);

	gettimeofday (&tv, NULL);
	ptm = localtime (&tv.tv_sec);
	strftime (time_string, sizeof (time_string), "%Y-%m-%d %H:%M:%S", ptm);

	if (glob->netwin->format == 1)
		comment_char = '#';
	else
		comment_char = '!';

	if (!glob->netwin->compress)
	{
		if (!(outfh = fopen (glob->netwin->fullname[0], "w")))
			vna_thread_exit ("Could not open output file for writing.");
		fprintf (outfh, "%c Date of measurement: %s\r\n", comment_char, time_string);
		fprintf (outfh, "%c Measurement type   : Snapshot of network analyzer display\r\n", comment_char);
		fprintf (outfh, "%c Frequency range    : %.3f - %.3f GHz (%.3f kHz resolution)\r\n",
				comment_char, start/1e9, stop/1e9, (frq[1]-frq[0])/1e3);
		if (glob->netwin->comment)
			fprintf (outfh, "%c Comment            : %s\r\n", comment_char, glob->netwin->comment);
		if (glob->netwin->format == 2)
			fprintf (outfh, "# Hz S  RI   R 50\r\n");
		fprintf (outfh, DATAHDR, comment_char, comment_char);
		for (i=0; i<points; i++)
			fprintf (outfh, DATAFRMT, frq[i], data[i].re, data[i].im);
		fclose (outfh);
	}
	else
	{
#ifndef NO_ZLIB
		if (!(gzoutfh = gzopen (glob->netwin->fullname[0], "w")))
			vna_thread_exit ("Could not open output file for writing.");
		gzprintf (gzoutfh, "%c Date of measurement: %s\r\n", comment_char, time_string);
		gzprintf (gzoutfh, "%c Measurement type   : Snapshot of network analyzer display\r\n", comment_char);
		gzprintf (gzoutfh, "%c Frequency range    : %.3f - %.3f GHz (%.3f kHz resolution)\r\n",
				comment_char, start/1e9, stop/1e9, (frq[1]-frq[0])/1e3);
		if (glob->netwin->comment)
			gzprintf (gzoutfh, "%c Comment            : %s\r\n", comment_char, glob->netwin->comment);
		if (glob->netwin->format == 2)
			gzprintf (gzoutfh, "# Hz S  RI   R 50\r\n");
		gzprintf (gzoutfh, DATAHDR, comment_char, comment_char);
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

/* Write the data file header for sweep measurements
 * pos: the position in the netwin->fullname array
 * sparam: the cleartext name of the measured S-parameter
 * netwin: the rest of the information */
static gboolean vna_write_header (gint pos, gchar *sparam, NetworkWin *netwin)
{
	int h, m, s;
	struct timeval tv;
	struct tm* ptm;
	char time_string[22], comment_char;
	FILE *outfh;

	gettimeofday (&tv, NULL);
	ptm = localtime (&tv.tv_sec);
	strftime (time_string, sizeof (time_string), "%Y-%m-%d %H:%M:%S", ptm);
	sec_to_hhmmss (netwin->estim_t, &h, &m, &s);

	if (netwin->format == 1)
		comment_char = '#';
	else
		comment_char = '!';

	if (!netwin->compress)
	{
		if (!(outfh = fopen (netwin->fullname[pos], "w")))
			return FALSE;

		fprintf (outfh, "%c Date of measurement: %s (estimated duration %02d:%02d:%02d)\r\n", 
				comment_char, time_string, h, m, s);
		fprintf (outfh, "%c Measurement type   : Frequency sweep\r\n", comment_char);
		fprintf (outfh, "%c Frequency range    : %.3f - %.3f GHz (%.3f kHz resolution)\r\n",
				comment_char, netwin->start/1e9, netwin->stop/1e9, netwin->resol/1e3);
		fprintf (outfh, "%c Stimulus settings  : %s, %dx averaging, %s mode\r\n",
				comment_char, sparam, netwin->avg, netwin->swpmode == 1 ? "ramp" : "step");
		if (netwin->vnamodel > 1)
			fprintf (outfh, "%c                      bandwidth %.3f kHz, dwell time %g µs\r\n",
		                comment_char, netwin->bandwidth / 1e3, netwin->dwell * 1e6);
		fprintf (outfh, "%c Calibration mode   : ", comment_char);
		if (netwin->calmode == 1)
			fprintf (outfh, "ECal full 2-port SOLT calibration\r\n");
		else
			fprintf (outfh, "none\r\n");
		if (netwin->comment)
			fprintf (outfh, "%c Comment            : %s\r\n", comment_char, netwin->comment);
		if (glob->netwin->format == 2)
			fprintf (outfh, "# Hz S  RI   R 50\r\n");
		if ((glob->netwin->format == 1) || (glob->netwin->numparam == 1))
			fprintf (outfh, DATAHDR, comment_char, comment_char);
		else
			fprintf (outfh, DATAHS2P);
		fclose (outfh);
	}
	else
	{
#ifndef NO_ZLIB
		if (!(netwin->gzoutfh[pos] = gzopen (netwin->fullname[pos], "w")))
			return FALSE;

		gzprintf (netwin->gzoutfh[pos], "%c Date of measurement: %s (estimated duration %02d:%02d:%02d)\r\n", 
				comment_char, time_string, h, m, s);
		gzprintf (netwin->gzoutfh[pos], "%c Measurement type   : Frequency sweep\r\n", comment_char);
		gzprintf (netwin->gzoutfh[pos], "%c Frequency range    : %.3f - %.3f GHz (%.3f kHz resolution)\r\n",
				comment_char, netwin->start/1e9, netwin->stop/1e9, netwin->resol/1e3);
		gzprintf (netwin->gzoutfh[pos], "%c Stimulus settings  : %s, %dx averaging, %s mode\r\n",
				comment_char, sparam, netwin->avg, netwin->swpmode == 1 ? "ramp" : "step");
		if (netwin->vnamodel > 1)
			gzprintf (netwin->gzoutfh[pos], "%c                      bandwidth %f kHz, dwell time %g ms\r\n",
		                comment_char, netwin->bandwidth / 1e3, netwin->dwell * 1e6);
		gzprintf (netwin->gzoutfh[pos], "%c Calibration mode   : ", comment_char);
		if (netwin->calmode == 1)
			gzprintf (netwin->gzoutfh[pos], "ECal full 2-port SOLT calibration\r\n");
		else
			gzprintf (netwin->gzoutfh[pos], "none\r\n");
		if (netwin->comment)
			gzprintf (netwin->gzoutfh[pos], "%c Comment            : %s\r\n", comment_char, netwin->comment);
		if (glob->netwin->format == 2)
			gzprintf (netwin->gzoutfh[pos], "# Hz S  RI   R 50\r\n");
		if ((glob->netwin->format == 1) || (glob->netwin->numparam == 1))
			gzprintf (netwin->gzoutfh[pos], DATAHDR, comment_char, comment_char);
		else
			gzprintf (netwin->gzoutfh[pos], DATAHS2P);
#endif
	}

	return TRUE;
}

static void vna_cal_frequency_range ()
{
	NetworkWin *netwin;
	VnaBackend *vna_func;
	int winleft, windone;
	gint startpointoffset;
	GTimeVal starttime, curtime, difftime;
	gdouble fstart, fstop;
	gchar *err;

	g_return_if_fail (glob->netwin);
	netwin = glob->netwin;
	vna_func = glob->netwin->vna_func;
	g_return_if_fail (vna_func->calibrate);

	/* Bring the VNA into the right state of mind */
	vna_update_netstat ("Setting up network analyzer...");
	vna_func->sweep_prepare ();

	/* Calibrate those frequency windows */
	g_get_current_time (&starttime);
	windone = 0;
	winleft = (int) ceil((netwin->stop - netwin->start)/((gdouble)netwin->points*netwin->resol));
	for (fstart=netwin->start; fstart<=netwin->stop; fstart+=netwin->resol*(gdouble)netwin->points)
	{
		fstop = fstart + netwin->resol * ((gdouble)netwin->points - 1.0);

		if (fstop > 50e9)
		{
			/* Right align measurement window */
			while (fstop > 50e9)
				fstop -= netwin->resol;

			/* startpointoffset: position in data array where new data will start */
			startpointoffset = (gint) ((fstart - (fstop - netwin->resol * ((gdouble)netwin->points - 1.0)))/netwin->resol);

			fstart = fstop - netwin->resol * ((gdouble)netwin->points - 1.0);
		}
		else
			startpointoffset = 0;

		vna_update_netstat ("Calibrating %6.3f - %6.3f GHz...", fstart/1e9, fstop/1e9);

		/* Choose frequency window and run calibration */
		vna_func->set_startstop (fstart, fstop);
		err = vna_func->calibrate (netwin->start, netwin->stop, netwin->resol, windone+1);
		if (err)
		{
			vna_func->gtl ();
			//FIXME: This will leak some memory.
			vna_thread_exit (err);
		}

		if (startpointoffset)
			/* Restore original start frequency */
			fstart += (gdouble) startpointoffset * netwin->resol;

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

		/* Has anyone canceled the calibration? */
		if (! (glob->flag & FLAG_VNA_MEAS) )
		{
			vna_func->gtl ();
			vna_thread_exit (NULL);
		}

		/* Finished with this window, start next one */
	}

	/* Go back to continuous and local mode */
	vna_func->gtl ();
}

/* Measure a whole frequency range by dividing it into windows */
static void vna_sweep_frequency_range ()
{
	NetworkWin *netwin;
	VnaBackend *vna_func;
	int i = 0, j, winleft, windone, h, m, s, Si;
	gint startpointoffset;
	GTimeVal starttime, curtime, difftime;
	struct timeval tv;
	struct tm* ptm;
	char time_string[22];
	gdouble fstart, fstop;
	ComplexDouble *data[6];
	DataVector *dvec;
	gchar *sparam[] = {"S11", "S12", "S21", "S22", 
		           "a1 (drive: port 1, lock: a1, numer: a2, denom: a1, conv: S)", 
			   "a2 (drive: port 2, lock: a2, numer: a2, denom: a1, conv: S)"};
	NetworkGraphUpdateData *graphupdate;
	FILE *outfh;
	gchar *err;

	g_return_if_fail (glob->netwin);
	netwin = glob->netwin;
	vna_func = glob->netwin->vna_func;

	/* Bring the VNA into the right state of mind */
	vna_update_netstat ("Setting up network analyzer...");
	vna_func->sweep_prepare ();
	
	/* Write datafile header */
	gettimeofday (&tv, NULL);
	ptm = localtime (&tv.tv_sec);
	strftime (time_string, sizeof (time_string), "%Y-%m-%d %H:%M:%S", ptm);
	sec_to_hhmmss (netwin->estim_t, &h, &m, &s);

	if ((netwin->numparam > 1) && (netwin->format == 1))
	{
		/* Full S-matrix measurement in DAT format */
		for (Si=0; Si<4; Si++)
		{
			if (!vna_write_header (Si, sparam[Si], netwin))
			{
				vna_func->gtl ();
				vna_thread_exit ("Could not open output file for writing.");
			}
		}

		if (netwin->numparam == 6)
		{
			/* TRL measurement (in DAT format) */
			if (!vna_write_header (4, sparam[4], netwin))
			{
				vna_func->gtl ();
				vna_thread_exit ("Could not open output file for writing.");
			}
			if (!vna_write_header (5, sparam[5], netwin))
			{
				vna_func->gtl ();
				vna_thread_exit ("Could not open output file for writing.");
			}
		}
	}
	else
	{
		/* Single S-matrix measurement or SNP format */
		if (!vna_write_header (0, netwin->param, netwin))
		{
			vna_func->gtl ();
			vna_thread_exit ("Could not open output file for writing.");
		}
	}

	/* Measure those frequency windows */
	g_get_current_time (&starttime);
	windone = 0;
	winleft = (int) ceil((netwin->stop - netwin->start)/((gdouble)netwin->points*netwin->resol));
	for (fstart=netwin->start; fstart<=netwin->stop; fstart+=netwin->resol*(gdouble)netwin->points)
	{
		fstop = fstart + netwin->resol * ((gdouble)netwin->points - 1.0);

		if (fstop > 50e9)
		{
			/* Right align measurement window */
			while (fstop > 50e9)
				fstop -= netwin->resol;

			/* startpointoffset: position in data array where new data will start */
			startpointoffset = (gint) ((fstart - (fstop - netwin->resol * ((gdouble)netwin->points - 1.0)))/netwin->resol);

			fstart = fstop - netwin->resol * ((gdouble)netwin->points - 1.0);
		}
		else
			startpointoffset = 0;

		vna_update_netstat ("Measuring %6.3f - %6.3f GHz...", fstart/1e9, fstop/1e9);

		if (netwin->calmode == 1)
			vna_func->cal_recall (netwin->start, netwin->stop, netwin->resol, -1);

		vna_func->set_startstop (fstart, fstop);
		if (netwin->calmode == 1)
		{
			err = vna_func->cal_recall (netwin->start, netwin->stop, netwin->resol, windone+1);
			if (err)
			{
				vna_func->gtl ();
				//FIXME: This will leak some memory.
				vna_thread_exit (err);
			}
		}
		vna_func->trace_scale_auto (NULL);
		vna_func->set_numg (netwin->avg+1);

		if (startpointoffset)
			/* Restore original start frequency */
			fstart += (gdouble)startpointoffset * netwin->resol;

		/* Wait for this part of the measurement to finish */
		vna_func->wait ();

		Si = 0; /* Si: S-parameter iterator */
		while ( ((netwin->fullname[Si]) && (Si<6)) /* parameters left not in S2P format */
		        || ((netwin->format == 2) && (netwin->numparam == 4) && (Si<4)) ) /* left in S2P format */
		{
			/* Get data */
			if (netwin->numparam > 1)
			{
				/* more then 1 S-parameter -> select correct one */
				if (Si < 4)
					/* Full S-matrix measurement */
					vna_func->select_s (sparam[Si]);
				else
					/* Prepare TRL measurement */
					vna_func->select_trl (Si);
			}

			vna_func->trace_scale_auto (sparam[Si]);

			/* Read data into buffers */
			data[Si] = NULL;
			data[Si] = vna_func->recv_data (netwin->points);
			g_return_if_fail (data[Si]);

			if (startpointoffset)
			{
				/* Shuffle right aligned data back to the left */
				for (i=0; i<netwin->points-startpointoffset; i++)
					data[Si][i]=data[Si][i+startpointoffset];
			}

			if (Si == 5)
				/* TRL measurement done -> back to four parameter split */
				vna_func->trace_fourparam ();

			/* i == number of datapoints to be written */
			for (i=0; (i<netwin->points) && (fstart+(gdouble)i*netwin->resol <= netwin->stop); i++) {}

			/* Display measured data */
			if (netwin->start == fstart)
			{
				/* First window, initialize graph */
				dvec = new_datavector ( (guint) ((netwin->stop - netwin->start)/netwin->resol) + 1 );
				if ((netwin->format == 1) || (netwin->numparam == 1))
					dvec->file = g_strdup (netwin->fullname[Si]);
				else
					dvec->file = g_strdup_printf ("%s:%s", netwin->fullname[0], sparam[Si]);
				for (j=0; j<i; j++)
				{
					dvec->x[j] = netwin->start + (gdouble)j * netwin->resol;
					dvec->y[j] = data[Si][j];
				}
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
				dvec->y    = g_new  (ComplexDouble, netwin->points);
				dvec->y    = memcpy (dvec->y, data[Si], netwin->points*sizeof(ComplexDouble));
				/* data may be longer than dvec but this doesn't matter */
			}

			/* Update main graph, vna_add_data_to_graph frees data */
			graphupdate       = g_new (NetworkGraphUpdateData, 1);
			graphupdate->dvec = dvec;
			graphupdate->pos  = Si;

			g_timeout_add (1, (GSourceFunc) vna_add_data_to_graph, graphupdate);

			/* Next S-parameter */
			Si++;
		}

		/* Write new data to file(s) */
		if ((netwin->format == 1) || (netwin->numparam == 1))
		{
			/* DAT or S1P format */
			for (j=0; j<Si; j++)
			{
				if (!netwin->compress)
				{
					if (!(outfh = fopen (netwin->fullname[j], "a")))
					{
						vna_func->gtl ();
						vna_thread_exit ("Could not open output file for writing.");
					}
					for (i=0; (i<netwin->points) && (fstart+(gdouble)i*netwin->resol <= netwin->stop); i++)
						fprintf (outfh, DATAFRMT,
								fstart+(gdouble)i*netwin->resol, data[j][i].re, data[j][i].im);
					fclose (outfh);
				}
				else
				{
#ifndef NO_ZLIB
					/* There ain't no append for gzopen... */
					for (i=0; (i<netwin->points) && (fstart+(gdouble)i*netwin->resol <= netwin->stop); i++)
						gzprintf (netwin->gzoutfh[j], DATAFRMT,
								fstart+(gdouble)i*netwin->resol, data[j][i].re, data[j][i].im);
#endif
				}
				g_free (data[j]);
				data[j] = NULL;
			}
		}
		else
		{
			/* S2P format */
			if (!netwin->compress)
			{
				if (!(outfh = fopen (netwin->fullname[0], "a")))
				{
					vna_func->gtl ();
					vna_thread_exit ("Could not open output file for writing.");
				}
				for (i=0; (i<netwin->points) && (fstart+(gdouble)i*netwin->resol <= netwin->stop); i++)
					fprintf (outfh, DATAFS2P, fstart+(gdouble)i*netwin->resol, 
							data[0][i].re, data[0][i].im, data[2][i].re, data[2][i].im,
							data[1][i].re, data[1][i].im, data[3][i].re, data[3][i].im);
				fclose (outfh);
			}
			else
			{
#ifndef NO_ZLIB
				/* There ain't no append for gzopen... */
				for (i=0; (i<netwin->points) && (fstart+(gdouble)i*netwin->resol <= netwin->stop); i++)
					gzprintf (netwin->gzoutfh[0], DATAFS2P, fstart+(gdouble)i*netwin->resol, 
							data[0][i].re, data[0][i].im, data[2][i].re, data[2][i].im,
							data[1][i].re, data[1][i].im, data[3][i].re, data[3][i].im);
#endif
			}
			g_free (data[0]); data[0] = NULL;
			g_free (data[1]); data[1] = NULL;
			g_free (data[2]); data[2] = NULL;
			g_free (data[3]); data[3] = NULL;
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
			vna_func->gtl ();
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
				gzclose (netwin->gzoutfh[i]);
				netwin->gzoutfh[i] = NULL;
			}
	}
#endif


	/* Go back to continuous and local mode */
	vna_func->gtl ();
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
	if (glob->netwin->vna_func->connect (netwin->host) < 0)
		vna_thread_exit ("Could not connect to network analyzer.");

	/* Set up time estimates */
	g_get_current_time (&curtime);
	netwin->start_t = curtime.tv_sec;
	if (netwin->type == 1)
	{
		/* add time estimate for frequency sweep */
		netwin->estim_t = 8;
		netwin->estim_t += (glong) ( (((float)glob->netwin->vna_func->sweep_cal_sleep()/1000) + 0.56)
				* ceil((netwin->stop - netwin->start)/netwin->resol/(gdouble)netwin->points));
		g_timeout_add (500, (GSourceFunc) vna_show_time_estimates, NULL);

		if ((glob->netwin->vnamodel == 1) || glob->netwin->calmode < 2)
			/* Start the measurement */
			vna_sweep_frequency_range ();
		else
			/* Start the calibration */
			vna_cal_frequency_range ();

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
