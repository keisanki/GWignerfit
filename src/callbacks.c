#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "callbacks.h"
#include "processdata.h"
#include "visualize.h"
#include "helpers.h"
#include "resonancelist.h"
#include "numeric.h"
#include "structs.h"
#include "overlay.h"
#include "export.h"
#include "fourier.h"
#include "spectral.h"
#include "network.h"
#include "preferences.h"
#include "calibrate.h"
#include "fcomp.h"
#include "merge.h"
#include "loadsave.h"
#include "correl.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

gboolean quit_application (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	/* Check for running measurement */
	if ((glob->flag & FLAG_VNA_MEAS) && (glob->netwin))
	{
		if (dialog_question ("This will abort your current measurement, quit anyway?") 
	     			!= GTK_RESPONSE_YES)
			return TRUE;
	
		glob->flag &= ~FLAG_VNA_MEAS;

		/* Wait for the thread to finish */
		g_thread_join (glob->netwin->vna_GThread);
	}

	/* Check for unsaved changes */
	if ((glob->flag & FLAG_CHANGES))
	{
		if (dialog_question ("Do you really want to quit?")
			== GTK_RESPONSE_YES)
		{
			delete_backup ();
			gtk_main_quit ();
			return TRUE;
		}
		else
			return TRUE;
	}

	delete_backup ();
	gtk_main_quit ();
	return TRUE;
}

void ok_dialog (GtkWidget *entry, gpointer dialog)
{
	g_return_if_fail (GTK_IS_DIALOG (dialog));
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
}

void on_new_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	gint i;

	if ((glob->flag & FLAG_CHANGES))
	{
		if (dialog_question ("Do you want to loose your changes?")
					== GTK_RESPONSE_NO)
			return;
	}

	visualize_stop_background_calc ();

	g_ptr_array_foreach (glob->param, (GFunc) g_free, NULL);
	g_ptr_array_free (glob->param, TRUE);
	glob->param = g_ptr_array_new();

	for (i=0; i<3; i++)
		if (glob->bars[i] != 0)
		{
			gtk_spect_vis_remove_bar (GTK_SPECTVIS (graph), glob->bars[i]);
			glob->bars[i] = 0;
		}

	on_spectral_close_activate (NULL, NULL);
	clear_resonancelist ();
	overlay_remove_all ();
	fcomp_purge ();
	merge_purge ();
	on_delete_spectrum ();
	visualize_newgraph ();
	unset_unsaved_changes ();
	delete_backup ();
	disable_undo ();

	gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")), "GWignerFit");

	g_free (glob->gparam);
	g_free (glob->resonancefile);
	g_free (glob->section);
	g_free (glob->stddev);
	g_free (glob->comment);

	g_free (glob->spectral);
	glob->spectral = NULL;

	free_datavector (glob->data);
	glob->data = NULL;
	free_datavector (glob->theory);
	glob->theory = NULL;

	glob->noise = -1;
	glob->numres = 0;
	glob->resonancefile = NULL;
	glob->section = NULL;
	glob->stddev = NULL;
	glob->comment = NULL;
	glob->gparam = g_new0 (GlobalParam, 1);
	glob->fitwindow.fit_GThread = NULL;
	if (glob->flag & FLAG_VNA_MEAS)
		glob->flag = FLAG_VNA_MEAS;
	else
		glob->flag = 0;
}

void import_data (GtkMenuItem *menuitem, gpointer user_data)
{
	gchar *filename, *path = NULL;

	path = get_defaultname (NULL);
	filename = get_filename ("Select datafile", path, 1);
	g_free (path);

	if (filename)
		read_datafile (filename, FALSE);

	g_free (filename);
}

gboolean show_about_window (void)
{
	glade_xml_signal_autoconnect (glade_xml_new(GLADEFILE, "aboutdialog", NULL));

	//gtk_label_set_text (
	//	GTK_LABEL (glade_xml_get_widget (gladexml, "label36")),
	//	__DATE__);

	return TRUE;
}

gboolean on_close_dialog (GtkWidget *button)
{
	gtk_widget_destroy (gtk_widget_get_toplevel(button));

	return TRUE;
}

gboolean view_absolute_part (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	
	gtk_spect_vis_set_displaytype (GTK_SPECTVIS (graph), 'a');
	gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (graph));
	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));

	return TRUE;
}

gboolean view_real_part (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	
	gtk_spect_vis_set_displaytype (GTK_SPECTVIS (graph), 'r');
	gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (graph));
	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));

	return TRUE;
}

gboolean view_imaginary_part (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	
	gtk_spect_vis_set_displaytype (GTK_SPECTVIS (graph), 'i');
	gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (graph));
	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));

	return TRUE;
}

gboolean view_phase_part (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	
	gtk_spect_vis_set_displaytype (GTK_SPECTVIS (graph), 'p');
	gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (graph));
	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));

	return TRUE;
}

gboolean view_log_power (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkWidget *graph = glade_xml_get_widget (gladexml, "graph");
	
	gtk_spect_vis_set_displaytype (GTK_SPECTVIS (graph), 'l');
	gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (graph));
	gtk_spect_vis_redraw (GTK_SPECTVIS (graph));

	return TRUE;
}

gboolean on_view_difference_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if (!glob->data)
	{
		dialog_message ("Please import some data first");
		
		g_signal_handlers_block_by_func (
				menuitem,
				on_view_difference_activate,
				user_data);
		gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menuitem), FALSE);
		g_signal_handlers_unblock_by_func (
				menuitem,
				on_view_difference_activate,
				user_data);
		return FALSE;
	}
	
	visualize_stop_background_calc ();
	if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem)))
	{
		glob->viewdifference = TRUE;
		visualize_difference_graph ();
	}
	else
	{
		glob->viewdifference = FALSE;
		visualize_remove_difference_graph ();
		visualize_draw_data ();
		visualize_theory_graph ();
	}

	return TRUE;
}

gboolean on_reflection_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	glob->IsReflection = 1;

	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scaleentry"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scale_check"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "label7"), TRUE);

	visualize_theory_graph ();

	return FALSE;
}

gboolean on_transmission_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	glob->IsReflection = 0;

	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scaleentry"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "scale_check"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "label7"), FALSE);

	gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (glade_xml_get_widget (gladexml, "scale_check")), 
		FALSE
	);

	visualize_draw_data ();
	visualize_theory_graph ();

	return FALSE;
}

void on_open1_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	gchar *path = NULL;
	gchar *filename = NULL;

	if ((glob->flag & FLAG_CHANGES))
	{
		if (dialog_question ("Do you want to loose your changes?")
					== GTK_RESPONSE_NO)
			return;
	}

	path = get_defaultname (NULL);
	filename = get_filename ("Select resonancefile", path, 1);
	g_free (path);

	if (!filename)
		return;

	load_gwf_resonance_file (filename);
	g_free (filename);
}

void on_open_section_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if ((glob->flag & FLAG_CHANGES))
	{
		if (dialog_question ("Do you want to loose your changes?")
					== GTK_RESPONSE_NO)
			return;
	}

	if (!glob->resonancefile)
	{
		dialog_message ("You need to open a gwf file first.");
		return;
	}

	//select_section_dialog (glob->resonancefile, glob->section, NULL);
}

gboolean on_phaseentry_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;
	gchar text[20];
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter)) return FALSE;
	
	if (sscanf(gtk_entry_get_text(GTK_ENTRY (entry)), "%lf", &value) != 1) return FALSE;

	glob->gparam->phase = value / 180*M_PI;
	visualize_theory_graph ();
	set_unsaved_changes ();

	snprintf (text, 20, "%f", value);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
	statusbar_message ("Global phase changed to %lf degree.", value);
	
	return FALSE;
}

gboolean on_scaleentry_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;
	gchar text[20];
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter)) return FALSE;
	
	if (sscanf(gtk_entry_get_text(GTK_ENTRY (entry)), "%lf", &value) != 1) return FALSE;

	glob->gparam->scale = value;
	visualize_theory_graph ();
	set_unsaved_changes ();

	snprintf (text, 20, "%f", value);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
	statusbar_message ("Global scale changed to %lf.", value);
	
	return FALSE;
}

gboolean on_tauentry_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;
	gchar text[20];
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter)) return FALSE;
	
	if (sscanf(gtk_entry_get_text(GTK_ENTRY (entry)), "%lf", &value) != 1) return FALSE;

	glob->gparam->tau = value * 1e-9;
	visualize_theory_graph ();
	set_unsaved_changes ();

	snprintf (text, 20, "%f", value);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
	statusbar_message ("Global parameter tau changed to %lf ns.", value);
	
	return FALSE;
}

gboolean on_minfrqentry_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;
	gchar text[20];
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter))
		return FALSE;
	
	if (sscanf(gtk_entry_get_text(GTK_ENTRY (entry)), "%lf", &value) != 1)
		return FALSE;

	if (value > glob->gparam->max/1e9)
	{
		dialog_message ("The minimal frequency must be below the maximal frequency.");
		snprintf (text, 20, "%f", glob->gparam->min/1e9);
		gtk_entry_set_text (GTK_ENTRY (entry), text);
		return FALSE;
	}

	glob->gparam->min = value * 1e9;

	if (glob->fft || glob->fcomp->theo)
	{
		/* Update theory, as the FFT or fcomp graphs may
		 * depend on the frequency window. */
		visualize_update_min_max (0);
		visualize_theory_graph ();
	}
	else
		/* Update with redraw */
		visualize_update_min_max (1);

	set_unsaved_changes ();

	snprintf (text, 20, "%f", value);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
	statusbar_message ("Minimal frequency changed to %lf GHz.", value);
	
	return FALSE;
}

gboolean on_maxfrqentry_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;
	gchar text[20];
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter))
		return FALSE;
	
	if (sscanf(gtk_entry_get_text(GTK_ENTRY (entry)), "%lf", &value) != 1)
		return FALSE;

	if (value < glob->gparam->min/1e9)
	{
		dialog_message ("The maximal frequency must be above the minimal frequency.");
		snprintf (text, 20, "%f", glob->gparam->max/1e9);
		gtk_entry_set_text (GTK_ENTRY (entry), text);
		return FALSE;
	}

	glob->gparam->max = value * 1e9;

	if (glob->fft || glob->fcomp->theo)
	{
		/* Update theory, as the FFT or fcomp graphs may
		 * depend on the frequency window. */
		visualize_update_min_max (0);
		visualize_theory_graph ();
	}
	else
		/* Update with redraw */
		visualize_update_min_max (1);

	set_unsaved_changes ();

	snprintf (text, 20, "%f", value);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
	statusbar_message ("Maximal frequency changed to %lf GHz.", value);
	
	return FALSE;
}

gboolean on_restreeview_button_press_event (GtkWidget *widget, GdkEvent *event)
{
	GtkMenu *menu;
	GdkEventButton *event_button;
	GladeXML *xmlmenu;

	g_return_val_if_fail (event != NULL, FALSE);

	event_button = (GdkEventButton *) event;

	if (event->type == GDK_BUTTON_PRESS)
	{
		if (event_button->button == 3)
		{
			xmlmenu = glade_xml_new (GLADEFILE, "resonance_popup", NULL);
			glade_xml_signal_autoconnect (xmlmenu);

			menu = GTK_MENU (glade_xml_get_widget (xmlmenu, "resonance_popup"));

			if (!glob->data)
			{
				gtk_widget_set_sensitive (
						glade_xml_get_widget (xmlmenu, "add_resonance"),
						FALSE);
				gtk_widget_set_sensitive (
						glade_xml_get_widget (xmlmenu, "add_resonance_by_click"),
						FALSE);
			}
			if (glob->numres == 0)
			{
				gtk_widget_set_sensitive (
						glade_xml_get_widget (xmlmenu, "remove_resonance"),
						FALSE);
				gtk_widget_set_sensitive (
						glade_xml_get_widget (xmlmenu, "clear_resonancelist"),
						FALSE);
				gtk_widget_set_sensitive (
						glade_xml_get_widget (xmlmenu, "check_all"),
						FALSE);
				gtk_widget_set_sensitive (
						glade_xml_get_widget (xmlmenu, "uncheck_all"),
						FALSE);
			}
			if ((glob->numres == 0) || (!glob->data))
			{
				gtk_widget_set_sensitive (
						glade_xml_get_widget (xmlmenu, "fit_this_resonance"),
						FALSE);
			}
			
			gtk_menu_popup (menu, NULL, NULL, NULL, NULL,
					event_button->button, event_button->time);
			return TRUE;
		}
	}

	if (event->type == GDK_2BUTTON_PRESS)
	{
		if (event_button->button == 1)
		{
			/* Check or uncheck whole row */
			resonance_toggle_row ();
		}
	}

	return FALSE;
}

gboolean on_restreeview_key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	g_return_val_if_fail (event != NULL, FALSE);

	if (event->keyval == GDK_Delete)
		remove_selected_resonance ();
	
	return FALSE;
}

void on_add_resonance_by_click_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	g_mutex_lock (glob->threads->flaglock);
	glob->flag |= FLAG_RES_CLICK;
	glob->flag &= ~FLAG_RES_FIND;
	glob->flag &= ~FLAG_FRQ_MIN;
	glob->flag &= ~FLAG_FRQ_MAX;
	glob->flag &= ~FLAG_FRQ_MEAS;
	glob->flag &= ~FLAG_FRQ_INT;
	glob->flag &= ~FLAG_MRK_NOISE;
	g_mutex_unlock (glob->threads->flaglock);
	statusbar_message ("Click on resonance now");
}

void on_add_resonance_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	Resonance *res;

	res = g_new (Resonance, 1);

	res->frq   = 0;
	res->width = 0;
	res->amp   = 0;
	res->phase = 0;
	
	add_resonance_to_list (res);
	set_unsaved_changes ();
}

void on_remove_resonance_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	remove_selected_resonance ();
}

void on_save_as_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	gchar *defaultname, *filename, *newsection, *title, *basename;

	newsection = ls_input_section (glob->section);
	if (!newsection)
		return;

	g_free (glob->section);
	glob->section = newsection;

	if (glob->resonancefile)
		defaultname = g_strdup (glob->resonancefile);
	else
		defaultname = get_defaultname (".gwf");

	filename = get_filename ("Select session file", defaultname, 0);
	g_free (defaultname);

	if (!filename)
		return;

	if (ls_save_file (filename, newsection, '=', save_write_section))
	{
		/* Delete a backup file if one's there. */
		delete_backup ();

		g_free (glob->resonancefile);
		glob->resonancefile = g_strdup (filename);

		basename = g_filename_display_basename (glob->resonancefile);
		title = g_strdup_printf ("%s:%s - GWignerFit", basename, glob->section);
		gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget (gladexml, "mainwindow")), title);
		g_free (title);
		g_free (basename);

		statusbar_message ("Save operation successful");
		unset_unsaved_changes ();
	}
	else
		statusbar_message ("Something went wrong during the save operation");
}

void on_save_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if ((!glob->section) || (!glob->resonancefile)) 
		on_save_as_activate (NULL, NULL);
	else 
		if (ls_save_file_exec (glob->resonancefile, glob->section, '=', 2, save_write_section))
		{
			statusbar_message ("Save operation successful");
			unset_unsaved_changes ();
			delete_backup ();
		}
}

gboolean cancel_fit_in_progress (GtkMenuItem *menuitem, gpointer user_data)
{
	/* clear the fit flag and set the cancel flag, the fit 
	 * process will then know that it has to quit. */
	g_mutex_lock (glob->threads->flaglock);
	glob->flag &= ~FLAG_FIT_RUN;
	glob->flag |= FLAG_FIT_CANCEL;
	g_mutex_unlock (glob->threads->flaglock);

	statusbar_message ("Fit canceled by user");

	return TRUE;
}

void take_current_values (GtkMenuItem *menuitem, gpointer user_data)
{
	/* clear the fit flag, the fit process will then
	 * know that it has to quit. */
	g_mutex_lock (glob->threads->flaglock);
	glob->flag &= ~FLAG_FIT_RUN;
	g_mutex_unlock (glob->threads->flaglock);
}

void on_fit_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	gint *ia;

	/* ia will be freed by start_fit() */
	ia = g_new0 (gint, TOTALNUMPARAM+1);

	/* A backup can never hurt */
	create_backup ();

	what_to_fit (ia);

	visualize_restore_cursor ();
	
	fit (ia);
}

void on_fit_this_resonance_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	gint *ia, i, *id, j, idpos;

	/* ia will be freed by start_fit() */
	ia = g_new0 (gint, TOTALNUMPARAM+1);

	id = get_selected_resonance_ids (FALSE);

	if (id[0] > 0)
	{
		what_to_fit (ia);

		/* A backup can never hurt */
		create_backup ();
		
		/* only the selected resonances should be fitted */
		for (i=0; i<glob->numres; i++)
		{
			idpos = 0;
			while (id[idpos])
			{
				if (i == id[idpos]-1) 
					break;
				idpos++;
			}

			if (i != id[idpos]-1)
				ia[4*i+1] = ia[4*i+2] = ia[4*i+3] = ia[4*i+4] = 0;
		}

		for (j=1; j<=NUM_GLOB_PARAM+3*glob->fcomp->numfcomp; j++)
			ia[4*glob->numres+j] = 0;

		/* ia[0] will hold the number of free parameters */
		ia[0] = 0;
		for (i=1; i<TOTALNUMPARAM+1; i++) 
			if (ia[i]) 
				ia[0]++;
		
		visualize_restore_cursor ();
		
		fit (ia);
	}

	g_free (id);
}

void on_clear_resonancelist_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if (dialog_question ("Do you really want to delete all resonance data?")
			== GTK_RESPONSE_YES)
	{
		clear_resonancelist ();
		spectral_resonances_changed ();
		g_ptr_array_foreach (glob->param, (GFunc) g_free, NULL);
		g_ptr_array_free (glob->param, TRUE);
		glob->param = g_ptr_array_new();

		show_global_parameters (glob->gparam);
		
		visualize_update_res_bar (0);
		visualize_theory_graph ();
		set_unsaved_changes ();

		statusbar_message ("Cleared all resonances in the list");
	}
	else
		statusbar_message ("Clear resonance list canceled by user");
}

void on_find_isolated_resonances_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	g_mutex_lock (glob->threads->flaglock);
	glob->flag |= FLAG_RES_FIND;
	glob->flag &= ~FLAG_RES_CLICK;
	glob->flag &= ~FLAG_FRQ_MIN;
	glob->flag &= ~FLAG_FRQ_MAX;
	glob->flag &= ~FLAG_FRQ_MEAS;
	glob->flag &= ~FLAG_FRQ_INT;
	glob->flag &= ~FLAG_MRK_NOISE;
	g_mutex_unlock (glob->threads->flaglock);
	statusbar_message ("Select threshold amplitude on graph");
}

void on_estimate_global_parameters_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if (!glob->data)
	{
		dialog_message ("Please import some data first");
		return;
	}
	
	glob->gparam->scale = 0;
	glob->gparam->phase = 0;
	glob->gparam->tau   = 0;
	
	calculate_global_paramters (glob->data, glob->gparam);
	show_global_parameters (glob->gparam);

	visualize_theory_graph ();
	set_unsaved_changes ();
	
	statusbar_message ("Global resonance parameters recalculated");
}

void on_set_frequency_win_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	g_mutex_lock (glob->threads->flaglock);
	glob->flag |= FLAG_FRQ_MIN;
	glob->flag &= ~FLAG_RES_FIND;
	glob->flag &= ~FLAG_RES_CLICK;
	glob->flag &= ~FLAG_FRQ_MAX;
	glob->flag &= ~FLAG_FRQ_MEAS;
	glob->flag &= ~FLAG_FRQ_INT;
	glob->flag &= ~FLAG_MRK_NOISE;
	g_mutex_unlock (glob->threads->flaglock);
	statusbar_message ("Mark minimal frequency on the graph");
}

void on_undo_last_fit_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	undo_changes ('u');
}

void on_redo_changes_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	undo_changes ('r');
}

void on_restreeview_select (GtkTreeView *treeview, gpointer user_data)
{
	visualize_update_res_bar (1);
}

void on_mainwindow_focus_out_event (GtkWidget *widget, GdkEventFocus *event)
{
	visualize_restore_cursor ();
}

void on_overlay_spectra_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	overlay_show_window ();
}

void on_zoom_selection_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	visualize_zoom_to_frequency_range (glob->gparam->min, glob->gparam->max);
}

void on_preferences_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	prefs_change_win ();
}

void on_export_resonance_data_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	export_resonance_data ();
}

void on_export_theory_graph_data_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	gchar *filename, *defaultname;

	defaultname = get_defaultname ("_theo.dat");
	filename = get_filename ("Select filename for data export", defaultname, 2);
	g_free (defaultname);

	if (filename)
	{
		if (export_theory_graph_data (filename))
			statusbar_message ("Data of theoretical graph exported");
		else
			statusbar_message ("Error: Could not export theoretical graph data");
	}
	else
		statusbar_message ("Export aborted");
}

void on_export_ps_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	export_graph_ps ();
}

void on_measure_distance_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if (!glob->data) 
	{
		dialog_message ("Please import some spectrum data first.");
		return;
	}

	g_mutex_lock (glob->threads->flaglock);
	glob->flag |= FLAG_FRQ_MEAS;
	glob->flag &= ~FLAG_RES_FIND;
	glob->flag &= ~FLAG_RES_CLICK;
	glob->flag &= ~FLAG_FRQ_MIN;
	glob->flag &= ~FLAG_FRQ_MAX;
	glob->flag &= ~FLAG_FRQ_INT;
	glob->flag &= ~FLAG_MRK_NOISE;
	g_mutex_unlock (glob->threads->flaglock);

	glob->measure = 0;
	statusbar_message ("Mark first frequency on the graph");
}

void on_integrate_spectrum_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if (!glob->data) 
	{
		dialog_message ("Please import some spectrum data first.");
		return;
	}

	g_mutex_lock (glob->threads->flaglock);
	glob->flag |= FLAG_FRQ_INT;
	glob->flag &= ~FLAG_RES_FIND;
	glob->flag &= ~FLAG_RES_CLICK;
	glob->flag &= ~FLAG_FRQ_MIN;
	glob->flag &= ~FLAG_FRQ_MAX;
	glob->flag &= ~FLAG_FRQ_MEAS;
	glob->flag &= ~FLAG_MRK_NOISE;
	g_mutex_unlock (glob->threads->flaglock);

	glob->measure = 0;
	statusbar_message ("Mark first frequency on the graph");
}

void on_estimate_data_noise_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	gint i;

	if (glob->stddev)
	{
		for (i=1; i<=TOTALNUMPARAM; i++)
			printf ("%e\n", glob->stddev[i]);
	}
	
	if (!glob->data) 
	{
		dialog_message ("Please import some spectrum data first.");
		return;
	}

	g_mutex_lock (glob->threads->flaglock);
	glob->flag |= FLAG_MRK_NOISE;
	glob->flag &= ~FLAG_RES_FIND;
	glob->flag &= ~FLAG_RES_CLICK;
	glob->flag &= ~FLAG_FRQ_MIN;
	glob->flag &= ~FLAG_FRQ_MAX;
	glob->flag &= ~FLAG_FRQ_INT;
	glob->flag &= ~FLAG_FRQ_MEAS;
	g_mutex_unlock (glob->threads->flaglock);

	statusbar_message ("Mark first frequency on the graph");
}

void on_fourier_transform_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	fourier_open_win (FALSE);
}

void on_fourier_transform_window_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	fourier_open_win (TRUE);
}

void on_check_all_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	resonance_check_all (TRUE);
}

void on_uncheck_all_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	resonance_check_all (FALSE);
}

void on_import_resonance_list_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	gchar *path = NULL;
	gchar *filename = NULL;

	path = get_defaultname (NULL);
	filename = get_filename ("Select file with resonance list", path, 1);
	g_free (path);

	if (filename)
		import_resonance_list (filename);
}

void on_spectral_stat_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if (!glob->numres)
	{
		dialog_message ("Please define some resonances first.");
		return;
	}

	spectral_open_win ();
}

void on_import_measure_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	network_open_win ();
}

void on_calibrate_spectrum_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	cal_open_win ();
}

void on_fourier_components_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	fcomp_open_win ();
}

void on_manual_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	dialog_message ("Please refer to the provided user manual, which can be found at\n"
			"/usr/local/share/doc/gwignerfit/gwignerfit-manual.html");
}

void on_merge_resonancelists_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	merge_open_win ();
}

void on_correlation_function_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if (!glob->data) 
	{
		dialog_message ("Please import some spectrum data first.");
		return;
	}

	correl_open_win ();
}

void on_comment_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkTextView *textview;
	GtkTextBuffer *buffer;
	GtkTextIter textiter;

	if (glob->commentxml)
		return;
	
	glob->commentxml = glade_xml_new (GLADEFILE, "comment_dialog", NULL);
	glade_xml_signal_autoconnect (glob->commentxml);

	if (glob->comment)
	{
		textview = GTK_TEXT_VIEW (glade_xml_get_widget (glob->commentxml, "comment_text"));
		buffer   = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
		gtk_text_buffer_get_end_iter (buffer, &textiter);
		gtk_text_buffer_insert (buffer, &textiter, glob->comment, -1);
	}
}

gboolean on_comment_done (GtkWidget *widget, gpointer data)
{
//	GtkWidget *toplevel, *vbox, *sw;
	GtkTextView *textview;
	GtkTextBuffer *buffer;
	GtkTextIter start, stop;
	gchar *newcomment;

	if (!glob->commentxml)
		return FALSE;

#if 0
	/* Walk through dialog to recover textview widget */
	toplevel = gtk_widget_get_toplevel (widget);
	vbox = GTK_DIALOG (toplevel)->vbox;
	sw = ((GtkBoxChild *) (GTK_BOX (vbox)->children->next->next->data))->widget;
	textview = GTK_TEXT_VIEW (gtk_bin_get_child (GTK_BIN (sw)));
#endif
	textview = GTK_TEXT_VIEW (glade_xml_get_widget (glob->commentxml, "comment_text"));
	g_return_val_if_fail (GTK_IS_TEXT_VIEW (textview), FALSE);

	if (data)
	{
		/* Close window without saving comment */
		gtk_widget_destroy (glade_xml_get_widget (glob->commentxml, "comment_dialog"));
		g_free (glob->commentxml);
		glob->commentxml = NULL;
		return TRUE;
	}

	buffer = gtk_text_view_get_buffer (textview);
	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter   (buffer, &stop);

	newcomment = gtk_text_buffer_get_text (buffer, &start, &stop, FALSE);
	if ((!glob->comment && strlen (newcomment)) ||             /* both comments are empty */
	    (glob->comment && strcmp (glob->comment, newcomment))) /* comment does not exist or comment changed */
	{
		/* The comment has changed */
		g_free (glob->comment);
		glob->comment = newcomment;
		set_unsaved_changes ();
	}
	else
		g_free (newcomment);
	
	gtk_widget_destroy (glade_xml_get_widget (glob->commentxml, "comment_dialog"));
	g_free (glob->commentxml);
	glob->commentxml = NULL;
	return TRUE;
}
