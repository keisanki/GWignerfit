#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include "structs.h"
#include "helpers.h"
#include "callbacks.h"
#include "resonancelist.h"
#include "gtkspectvis.h"
#include "visualize.h"

GlobalData *glob;
GladeXML *gladexml;

/* Initialize glob->prefs with default values */
void prefs_set_default ()
{
	if (!glob->prefs)
		glob->prefs = g_new (Preferences, 1);

	glob->prefs->iterations = 50;
	glob->prefs->widthunit = 6;
	glob->prefs->confirm_append = TRUE;
	glob->prefs->confirm_resdel = TRUE;
	glob->prefs->save_overlays = TRUE;
	glob->prefs->datapoint_marks = TRUE;
	glob->prefs->sortparam = FALSE;
	glob->prefs->fit_converge_detect = TRUE;
	glob->prefs->relative_paths = FALSE;
	glob->prefs->angles_in_deg = FALSE;
	glob->prefs->res_export = -1;
	glob->prefs->priority = 5;
	glob->prefs->cal_tauO = 20.837e-12;
	glob->prefs->cal_tauS = 22.548e-12;
	glob->prefs->cal_C0 = 29.72e-15;
	glob->prefs->cal_C1 = 165.78e-27;
	glob->prefs->cal_C2 = -3.54e-36;
	glob->prefs->cal_C3 = 0.07e-45;
	glob->prefs->vnamodel = 2;
	glob->prefs->vnahost = NULL;
}

/* Save the preferences in ~/.gwignerfitrc */
void prefs_save (Preferences *prefs)
{
	FILE *fh;
	gchar *filename;

	g_return_if_fail (prefs);

	filename = g_build_filename (g_get_home_dir (), ".gwignerfitrc", NULL);

	if (g_file_test (filename, G_FILE_TEST_IS_DIR))
	{
		g_free (filename);
		return;
	}
	if (!(fh = fopen (filename, "w")))
		return;
	g_free (filename);

	fprintf (fh, "# GWignerFit configuration file\n");
	fprintf (fh, "iterations = %d\n", prefs->iterations);
	fprintf (fh, "widthunit = %d\n", prefs->widthunit);
	fprintf (fh, "confirm_append = %d\n", prefs->confirm_append);
	fprintf (fh, "confirm_resdel = %d\n", prefs->confirm_resdel);
	fprintf (fh, "save_overlays = %d\n", prefs->save_overlays);
	fprintf (fh, "datapoint_marks = %d\n", prefs->datapoint_marks);
	fprintf (fh, "sortparam = %d\n", prefs->sortparam);
	fprintf (fh, "fit_converge_detect = %d\n", prefs->fit_converge_detect);
	fprintf (fh, "relative_paths = %d\n", prefs->relative_paths);
	fprintf (fh, "angles_in_deg = %d\n", prefs->angles_in_deg);
	fprintf (fh, "res_export = %d\n", prefs->res_export);
	fprintf (fh, "priority = %d\n", prefs->priority);
	fprintf (fh, "cal_tauO = %e\n", prefs->cal_tauO);
	fprintf (fh, "cal_tauS = %e\n", prefs->cal_tauS);
	fprintf (fh, "cal_C0 = %e\n", prefs->cal_C0);
	fprintf (fh, "cal_C1 = %e\n", prefs->cal_C1);
	fprintf (fh, "cal_C2 = %e\n", prefs->cal_C2);
	fprintf (fh, "cal_C3 = %e\n", prefs->cal_C3);
	fprintf (fh, "vna_model = %d\n", prefs->vnamodel);
	if (prefs->vnahost)
		fprintf (fh, "vna_host = %s\n", prefs->vnahost);

	fclose (fh);
}

/* Load the preferences from ~/.gwignerfitrc */
void prefs_load (Preferences *prefs)
{
	FILE *fh;
	gchar *filename;
	gchar line[100], cmd[100], str[100];
	gint val;
	gdouble val2;

	g_return_if_fail (prefs);

	filename = g_build_filename (g_get_home_dir (), ".gwignerfitrc", NULL);

	if (g_file_test (filename, G_FILE_TEST_IS_DIR))
	{
		g_free (filename);
		return;
	}
	if (!(fh = fopen (filename, "r")))
		return;
	g_free (filename);

	while (!feof (fh)) {
		if (!(fgets (line, 99, fh)))
			continue;
		if ((line[0] == '#') || (line[0] == '\r') || (line[0] == '\n'))
			continue;

		if (g_str_has_prefix (line, "cal_"))
		{
			/* Calibration constants use double values */
			
			if (sscanf (line, "%s = %lf", cmd, &val2) != 2)
				continue;

			if (!g_ascii_strncasecmp (cmd, "cal_tauO", 9))
				prefs->cal_tauO = val2;
			if (!g_ascii_strncasecmp (cmd, "cal_tauS", 9))
				prefs->cal_tauS = val2;
			if (!g_ascii_strncasecmp (cmd, "cal_C0", 9))
				prefs->cal_C0 = val2;
			if (!g_ascii_strncasecmp (cmd, "cal_C1", 9))
				prefs->cal_C1 = val2;
			if (!g_ascii_strncasecmp (cmd, "cal_C2", 9))
				prefs->cal_C2 = val2;
			if (!g_ascii_strncasecmp (cmd, "cal_C3", 9))
				prefs->cal_C3 = val2;

			continue;
		}

		if (g_str_has_prefix (line, "vna_host"))
		{
			/* VNA host is a string */
			
			if (sscanf (line, "%s = %s", cmd, str) != 2)
				continue;

			prefs->vnahost = g_strdup (str);

			continue;
		}

		if (sscanf (line, "%s = %d", cmd, &val) != 2)
			continue;

		if (!g_ascii_strncasecmp (cmd, "iterations", 11))
			prefs->iterations = val;
		if (!g_ascii_strncasecmp (cmd, "widthunit", 10))
			prefs->widthunit = val;
		if (!g_ascii_strncasecmp (cmd, "confirm_append", 15))
			prefs->confirm_append = val;
		if (!g_ascii_strncasecmp (cmd, "confirm_resdel", 15))
			prefs->confirm_resdel = val;
		if (!g_ascii_strncasecmp (cmd, "save_overlays", 14))
			prefs->save_overlays = val;
		if (!g_ascii_strncasecmp (cmd, "datapoint_marks", 16))
			prefs->datapoint_marks = val;
		if (!g_ascii_strncasecmp (cmd, "sortparam", 10))
			prefs->sortparam = val;
		if (!g_ascii_strncasecmp (cmd, "fit_converge_detect", 20))
			prefs->fit_converge_detect = val;
		if (!g_ascii_strncasecmp (cmd, "relative_paths", 15))
			prefs->relative_paths = val;
		if (!g_ascii_strncasecmp (cmd, "angles_in_deg", 14))
			prefs->angles_in_deg = val;
		if (!g_ascii_strncasecmp (cmd, "res_export", 11))
			prefs->res_export = val;
		if (!g_ascii_strncasecmp (cmd, "priority", 9))
			prefs->priority = val;
		if (!g_ascii_strncasecmp (cmd, "vna_model", 9))
			prefs->vnamodel = val;
	}
	fclose (fh);
}

/* Open the GUI preferences dialog and update glob->prefs */
void prefs_change_win ()
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	GladeXML *xmldialog;
	GtkWidget *dialog;
	guint pos;
	gint result, newpriority;

	xmldialog = glade_xml_new (GLADEFILE, "prefs_dialog", NULL);
	
	dialog = glade_xml_get_widget (xmldialog, "prefs_dialog");
	g_signal_connect (
		G_OBJECT (glade_xml_get_widget (xmldialog, "iterations_spinner")), 
		"activate", 
		G_CALLBACK (ok_dialog), 
		dialog);

	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (xmldialog, "iterations_spinner")), 
		glob->prefs->iterations);

	gtk_spin_button_set_value (
		GTK_SPIN_BUTTON (glade_xml_get_widget (xmldialog, "priority_spinner")), 
		glob->prefs->priority);

	if (glob->prefs->widthunit == 6)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "mhz_radio")), 
			TRUE);
	if (glob->prefs->widthunit == 3)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "khz_radio")), 
			TRUE);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "fit_convergence_check")), 
		glob->prefs->fit_converge_detect);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "confirm_append_check")), 
		glob->prefs->confirm_append);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "confirm_resdel_check")), 
		glob->prefs->confirm_resdel);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "save_overlays_check")), 
		glob->prefs->save_overlays);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "datapoint_marks_check")), 
		glob->prefs->datapoint_marks);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "prefs_sortparam_check")), 
		glob->prefs->sortparam);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "prefs_relative_check")), 
		glob->prefs->relative_paths);

	if (glob->prefs->angles_in_deg)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "deg_radio")), 
			TRUE);
	else
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "rad_radio")), 
			TRUE);

	if (glob->prefs->vnamodel == 1)
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "prefs_8510c_radio")), 
			TRUE);
	else
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "prefs_n5230a_radio")), 
			TRUE);

	if (glob->prefs->vnahost)
		gtk_entry_set_text (
			GTK_ENTRY (glade_xml_get_widget (xmldialog, "prefs_vnahost_entry")),
			glob->prefs->vnahost);

	result = gtk_dialog_run (GTK_DIALOG (dialog));

	if (result == GTK_RESPONSE_OK)
	{
		glob->prefs->iterations = gtk_spin_button_get_value (
			GTK_SPIN_BUTTON (glade_xml_get_widget (xmldialog, "iterations_spinner")));

		newpriority = gtk_spin_button_get_value (
			GTK_SPIN_BUTTON (glade_xml_get_widget (xmldialog, "priority_spinner")));
		if (newpriority < glob->prefs->priority)
			dialog_message ("You have lowered the nice priority value from %d to %d. "
					"This change will only take effect after GWignerFit has been restartet.",
					glob->prefs->priority, newpriority);
		glob->prefs->priority = newpriority;
		adjustpriority ();

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "mhz_radio"))))
		{
			glob->prefs->widthunit = 6;
		}
		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "khz_radio"))))
		{
			glob->prefs->widthunit = 3;
		}
		reslist_update_widthunit ();

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "deg_radio"))))
				glob->prefs->angles_in_deg = TRUE;
		else
			glob->prefs->angles_in_deg = FALSE;

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "fit_convergence_check"))))
			glob->prefs->fit_converge_detect = TRUE;
		else
			glob->prefs->fit_converge_detect = FALSE;

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "confirm_append_check"))))
			glob->prefs->confirm_append = TRUE;
		else
			glob->prefs->confirm_append = FALSE;

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "confirm_resdel_check"))))
			glob->prefs->confirm_resdel = TRUE;
		else
			glob->prefs->confirm_resdel = FALSE;

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "save_overlays_check"))))
			glob->prefs->save_overlays = TRUE;
		else
			glob->prefs->save_overlays = FALSE;

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "prefs_relative_check"))))
			glob->prefs->relative_paths = TRUE;
		else
			glob->prefs->relative_paths = FALSE;

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "datapoint_marks_check"))))
		{
			if (!glob->prefs->datapoint_marks)
			{
				/* Enable marks in current graphs */
				if (glob->data)
					gtk_spect_vis_set_graphtype (GTK_SPECTVIS (graph), glob->data->index, 'm');
				if (glob->overlayspectra)
				{
					pos = 0;
					while (pos < glob->overlayspectra->len)
					{
						gtk_spect_vis_set_graphtype (GTK_SPECTVIS (graph),
							((DataVector *) g_ptr_array_index (glob->overlayspectra, pos))->index,
							'm');
						pos++;
					}
				}
				gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
			}
			glob->prefs->datapoint_marks = TRUE;
		}
		else
		{
			if (glob->prefs->datapoint_marks)
			{
				/* Disable marks in current graphs */
				if (glob->data)
					gtk_spect_vis_set_graphtype (GTK_SPECTVIS (graph), glob->data->index, 'l');
				if (glob->overlayspectra)
				{
					pos = 0;
					while (pos < glob->overlayspectra->len)
					{
						gtk_spect_vis_set_graphtype (GTK_SPECTVIS (graph),
							((DataVector *) g_ptr_array_index (glob->overlayspectra, pos))->index,
							'l');
						pos++;
					}
				}
				gtk_spect_vis_redraw (GTK_SPECTVIS (graph));
			}
			glob->prefs->datapoint_marks = FALSE;
		}

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "prefs_sortparam_check"))))
		{
			if (!glob->prefs->sortparam)
			{
				glob->prefs->sortparam = TRUE;

				/* I'm lazy and use the undo mechanism to get
				 * the order right. */
				disable_undo ();
				set_up_undo ();
				undo_changes (' ');
				disable_undo ();
			}
		}
		else
			glob->prefs->sortparam = FALSE;

		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "prefs_8510c_radio"))))
		{
			glob->prefs->vnamodel = 1;
		}
		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "prefs_n5230a_radio"))))
		{
			glob->prefs->vnamodel = 2;
		}

		glob->prefs->vnahost = g_strdup (gtk_entry_get_text (
			GTK_ENTRY (glade_xml_get_widget (xmldialog, "prefs_vnahost_entry"))));
		if (strlen (glob->prefs->vnahost) == 0)
		{
			g_free (glob->prefs->vnahost);
			glob->prefs->vnahost = NULL;
		}
		
		prefs_save (glob->prefs);
		statusbar_message ("Preferences changed");
	}
	
	gtk_widget_destroy (dialog);
}
