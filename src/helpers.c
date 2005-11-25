#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>

#include "structs.h"
#include "callbacks.h"
#include "numeric.h"
#include "resonancelist.h"
#include "visualize.h"
#include "processdata.h"
#include "fourier.h"
#include "spectral.h"
#include "fcomp.h"

/* Global variables */
extern GladeXML *gladexml;
extern GlobalData *glob;

/* Forward declarations */
void delete_backup ();
void unset_unsaved_changes ();

/* Remove the latest message from the statusbar. */
static gint statusbar_message_remove (gpointer data)
{
	GtkWidget *statusbar = glade_xml_get_widget(gladexml, "statusbar");
	gtk_statusbar_pop (GTK_STATUSBAR (statusbar), GPOINTER_TO_INT (data));

	return FALSE;
}

/* Add a new message to the statusbar which will be removed after 5 sec. */
void statusbar_message (gchar *format, ...)
{
	va_list ap;
	gchar *message;
	gint context_id;
	GtkWidget *statusbar = glade_xml_get_widget(gladexml, "statusbar");

	va_start(ap, format);
	message = g_strdup_vprintf (format, ap);
	va_end(ap);

	context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR (statusbar), message);
	gtk_statusbar_push(GTK_STATUSBAR (statusbar), context_id, message);

	g_free (message);

	g_timeout_add(5*1000, statusbar_message_remove, GINT_TO_POINTER (context_id));
}

/* Display a window with a given message. The window has an OK button. */
gboolean dialog_message (gchar *format, ...)
{
	va_list ap;
	GladeXML *xmldialog;
	GtkWidget *dialog;
	gchar *message;
	gint result;

	xmldialog = glade_xml_new (GLADEFILE, "message_dialog", NULL);
	visualize_restore_cursor ();

	va_start(ap, format);
	message = g_strdup_vprintf (format, ap);
	va_end(ap);

	gtk_label_set_text (GTK_LABEL (glade_xml_get_widget (xmldialog, "message_label")), message);

	g_free (message);

	dialog = glade_xml_get_widget (xmldialog, "message_dialog");
	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	return FALSE;
}

#if 0
/* Display a window with a given message. The window has an OK button. */
gboolean dialog_message (gchar *format, ...)
{
	va_list ap;
	GtkWidget *dialog;
	gint result;

	visualize_restore_cursor ();

	va_start (ap, format);
	dialog = gtk_message_dialog_new (
			GTK_WINDOW (glade_xml_get_widget (gladexml, "mainwindow")),
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_INFO,
			GTK_BUTTONS_OK,
			format,
			ap);
	va_end (ap);

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	return FALSE;
}
#endif

/* Display a window with a given question. The window has a YES and a NO button. 
 * Return values: GTK_RESPONSE_YES or GTK_RESPONSE_NO
 */
gint dialog_question (gchar *format, ...)
{
	va_list ap;
	GladeXML *xmldialog;
	GtkWidget *dialog;
	gchar *message;
	gint result;

	xmldialog = glade_xml_new (GLADEFILE, "question_dialog", NULL);
	visualize_restore_cursor ();

	dialog = glade_xml_get_widget (xmldialog, "question_dialog");

	va_start(ap, format);
	message = g_strdup_vprintf (format, ap);
	va_end(ap);

	gtk_label_set_text (GTK_LABEL (glade_xml_get_widget (xmldialog, "question_label")), message);

	g_free (message);

	result = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);

	return result;
}

/* Return a list of DataVector* with unique frequency datasets,
 * free result with g_ptr_array_free (*get_unique_frqs, FALSE) */
GPtrArray *get_unique_frqs ()
{
	GPtrArray *sets;
	DataVector *testvec;
	gboolean is_unique;
	guint i, j;

	sets = g_ptr_array_new ();

	if (glob->data)
	{
		g_ptr_array_add (sets, glob->data);

		if ((glob->theory) && (glob->theory->x != glob->data->x))
			g_ptr_array_add (sets, glob->theory);
	}
	else if (glob->theory)
		g_ptr_array_add (sets, glob->theory);

	if (glob->overlayspectra)
		for (i=0; i<glob->overlayspectra->len; i++)
		{
			is_unique = TRUE;
			
			testvec = (DataVector *) g_ptr_array_index (glob->overlayspectra, i);
			for (j=0; j<sets->len; j++)
				if (((DataVector *)g_ptr_array_index (sets, j))->x == testvec->x)
					is_unique = FALSE;

			if (is_unique)
				g_ptr_array_add (sets, testvec);
		}

	return sets;
}

/* Alloc memory for a new DataVector of the given length */
DataVector *new_datavector (guint len)
{
	DataVector *vec;
	
	g_return_val_if_fail (len, NULL);

	vec = g_new (DataVector, 1);

	vec->x = g_new (gdouble, len);
	vec->y = g_new (ComplexDouble, len);
	vec->len = len;
	vec->file = NULL;
	vec->index = 0;

	return vec;
}

/* Free a DaraVector */
void free_datavector (DataVector *vec)
{
	guint i;
	DataVector *testvec;
	gboolean in_use = FALSE;

	if (vec == NULL) return;

	if (glob->overlaystore)
		for (i=0; i<glob->overlayspectra->len; i++)
		{
			testvec = (DataVector *) g_ptr_array_index (glob->overlayspectra, i);
			if ((vec->x == testvec->x) && (vec != testvec))
				in_use = TRUE;
		}

	if ((glob->data) && (vec->x == glob->data->x) && (vec != glob->data))
		in_use = TRUE;

	if ((glob->theory) && (vec->x == glob->theory->x) && (vec != glob->theory))
		in_use = TRUE;

	/* Free x only if it is not used by another dataset */
	if (!in_use)
		g_free (vec->x);
	
	g_free (vec->y);
	g_free (vec->file);

	g_free (vec);
	vec = NULL;
}

/* Can the file be (over)written? */
gboolean file_is_writeable (gchar *filename)
{
	FILE *file;
	gchar *basename;

	basename = g_path_get_basename (filename);

	/* Is the file a directory */
	if (g_file_test (filename, G_FILE_TEST_IS_DIR))
	{
		dialog_message ("Cannot write to '%s', this is a directory.", basename);
		g_free (basename);
		return FALSE;
	}
	
	/* Does the file already exist? */
	file = fopen (filename, "r");
	if (file)
	{
		if (dialog_question ("File '%s' already exists, overwrite?", basename)
				!= GTK_RESPONSE_YES)
		{
			/* File exists, do not overwrite it. */
			fclose (file);
			g_free (basename);
			return FALSE;
		}
		fclose (file);

		if (unlink (filename))
		{
			/* Something went wrong during file deletion */
			dialog_message ("Cannot delete file '%s'.", basename);
			g_free (basename);
			return FALSE;
		}
	}
	else
	{
		/* Is the file writeable? */
		file = fopen (filename, "w");
		if (!file)
		{
			/* Cannot write to file -> abort */
			dialog_message ("Cannot open file '%s' for writing.", basename);
			g_free (basename);
			return FALSE;
		}
		fclose (file);

		if (unlink (filename))
		{
			/* Something went wrong during file deletion */
			dialog_message ("Cannot delete file '%s'.", basename);
			g_free (basename);
			return FALSE;
		}
	}

	g_free (basename);
	return TRUE;
}

/* Check file helper for get_filename() */
static gboolean get_filename_helper (char *filename, gchar check)
{
	gchar *basename;
	FILE *fileh;

	/* check: 
	 * 1 bit set: file should be readable
	 * 2 bit set: file should be writeable
	 */
	
	/* Has the user selected a directory? He should not have done this. */
	if ((check) && (g_file_test (filename, G_FILE_TEST_IS_DIR)))
	{
		basename = g_path_get_basename (filename);
		dialog_message ("Cannot use '%s' as a filename, this is a directory.", basename);
		g_free (basename);

		return FALSE;
	}
	
	if (check)
	{
		/* Check if the file already exists */
		fileh = fopen (filename, "r");
		if ((check & 2) && (fileh))
		{
			fclose (fileh);
			basename = g_path_get_basename (filename);

			if (dialog_question ("File '%s' already exists, overwrite?", basename)
					== GTK_RESPONSE_YES)
			{
				/* File exists, but overwrite it anyway */
				g_free (basename);

				return TRUE;
			}

			/* File exists and do not overwrite it */
			g_free (basename);
			return FALSE;
		}
		else if (fileh)
			fclose (fileh);

		if ((check & 1) && (!fileh))
		{
			/* Could not open file for reading */
			basename = g_path_get_basename (filename);
			dialog_message ("Cannot open '%s' for reading.", basename);
			g_free (basename);

			return FALSE;
		}
	}

	return TRUE;
}

/* Asks the user for a filename */
gchar *get_filename (const gchar *title, const gchar *defaultname, gchar check)
{
	GtkWidget *filew;
	gint result;
	gchar *filename = NULL;
	gboolean selection_done = FALSE;

	/* check: 
	 * 1 bit set: file should be readable
	 * 2 bit set: file should be writeable
	 */

	/* Create the selector with the proper title */
	if (title)
		filew = gtk_file_selection_new (title);
	else
		filew = gtk_file_selection_new ("Select filename");

	/* Set the default path or name if fiven */
	if (defaultname)
		gtk_file_selection_set_filename (GTK_FILE_SELECTION (filew), defaultname);

	/* Keep the dialog running untill the user has made a good choice(TM) */
	while (!selection_done)
	{
		result = gtk_dialog_run (GTK_DIALOG (filew));

		if (result == GTK_RESPONSE_OK)
		{
			filename = g_strdup (gtk_file_selection_get_filename (GTK_FILE_SELECTION (filew)));

			if (!(selection_done = get_filename_helper (filename, check)))
			{
				g_free (filename);
				filename = NULL;
			}
		}
		else
			selection_done = TRUE;
	}
	if (filename)
	{
		g_free (glob->path);
		glob->path = g_path_get_dirname (filename);
	}

	gtk_widget_destroy (filew);
	return filename;
}

/* Called by select_section_dialog() */
gboolean select_section_ok (gchar *section)
{
	gchar *title, *basename;

	g_return_val_if_fail (section, FALSE);

	glob->section = section;
	
	if (read_resonancefile (glob->resonancefile, glob->section) >= 0)
	{
		visualize_update_res_bar (0);
		show_global_parameters (glob->gparam);

		basename = g_path_get_basename (glob->resonancefile);
		title = g_strdup_printf ("%s:%s - GWignerFit", basename, glob->section);
		gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")), title);
		g_free (basename);
		g_free (title);

		visualize_update_min_max (0);
		visualize_theory_graph ();
		spectral_resonances_changed ();
		unset_unsaved_changes ();
	}
	else
	{
		/* No resonances were read but the old parameters have been
		 * deleted by read_resonancefile, get us safely out of here.
		 */
		unset_unsaved_changes ();
		on_new_activate (NULL, NULL);
	}

	return TRUE;
}

/* Takes a full filename and tries to find the sections contained in the
 * selected file. On success a window with the section_dialog is displayed.
 * If secname == NULL, read the section, otherwise just set secname to the name
 * of the section.
 * Returns FALSE on success.
 */
gboolean select_section_dialog (gchar *selected_filename, gchar *default_section, gchar **secname) 
{
	GtkWidget *combo;
	GladeXML *xml;
	gchar line[256], *filename, *section;
	FILE *datafile;
	GList *sections = NULL;
	gint pos = 0, found_a_section = 0, def_pos = -1;
	gint result;

	/* Has the user selected a directory?
	 * He should not have done this. */
	if (g_file_test (selected_filename, G_FILE_TEST_IS_DIR))
	{
		dialog_message ("Cannot read file, this is a directory.");
		return TRUE;
	}
	
	datafile = fopen(selected_filename, "r");
	if (datafile == NULL) {
		dialog_message ("Error: Could not open file %s.", selected_filename);
		return TRUE;
	}

	if (!secname)
	{
		delete_backup ();
		glob->resonancefile = selected_filename;
	}

	line[255] = '\0';
	while (!feof(datafile)) {
		fgets(line, 255, datafile);
		if ((strlen(line)) && (line[strlen(line)-1] == '\n'))
			line[strlen(line)-1] = '\0'; /* strip final \n */
		if ((strlen(line)) && (line[strlen(line)-1] == '\r'))
			line[strlen(line)-1] = '\0'; /* strip final \r */
		pos++;

		if (strlen(line) > 254) {
			fprintf(stderr, "Error: Line %i in '%s' too long!\n", pos, selected_filename);
			g_list_foreach (sections, (GFunc) g_free, NULL);
			g_list_free (sections);
			return TRUE;
		}

		if ((*line == '$') || (*line == '=')) {
			/* Found a new section */
			sections = g_list_append (sections, g_strdup (line+1));
			found_a_section = 1;

			if ((default_section) &&
			    (!strncmp(line+1, default_section, 100)))
				def_pos = g_list_length (sections);
		}
	}

	if (!found_a_section)
	{
		filename = g_path_get_basename (selected_filename);
		dialog_message ("Error: Found no sections in '%s'", filename);
		g_free (filename);
		return TRUE;
	}

	xml = glade_xml_new (GLADEFILE, "section_dialog", NULL);
	
	/* Add sections to combo box */
	combo = glade_xml_get_widget (xml, "sections_combo");
	gtk_combo_set_popdown_strings (GTK_COMBO (combo), sections);
	g_list_foreach (sections, (GFunc) g_free, NULL);
	g_list_free (sections);

	/* Select current section if found */
	if (def_pos > 0)
		gtk_entry_set_text (
			GTK_ENTRY (glade_xml_get_widget(xml, "section_combo_entry")), 
			default_section);

	result = gtk_dialog_run (GTK_DIALOG (glade_xml_get_widget (xml, "section_dialog")));

	if (result == GTK_RESPONSE_OK)
	{
		section = g_strdup (gtk_entry_get_text (
					GTK_ENTRY (glade_xml_get_widget (
							xml, "section_combo_entry"))));
		
		gtk_widget_destroy (glade_xml_get_widget (xml, "section_dialog"));

		if (!secname)
		{
			select_section_ok (section);
			/* Do _not_ free section as it is now in glob->section */
		}
		else
		{
			*secname = section;
			/* Do _not_ free section as it is a "return value" */
		}
	}
	else
	{
		gtk_widget_destroy (glade_xml_get_widget (xml, "section_dialog"));
		return TRUE;
	}

	return FALSE;
}

/* Update the FitWindow with the given fitwinparam information. */
gboolean update_fit_window (FitWindowParam *fitwinparam)
{
	GtkLabel *label;
	gchar *text;
	gfloat percentage;

	g_return_val_if_fail (fitwinparam != NULL, FALSE);

	/* Has the timeout come after the fit is already finished? */
	g_mutex_lock (glob->threads->flaglock);
	if (!(glob->flag & FLAG_FIT_RUN))
	{
		g_mutex_unlock (glob->threads->flaglock);
		return FALSE;
	}
	g_mutex_unlock (glob->threads->flaglock);

	/* Nobody else should change fitwinparam now */
	g_mutex_lock (glob->threads->fitwinlock);

	if (glade_xml_get_widget (fitwinparam->xmlfit, "fit_progress") == NULL)
	{
		g_mutex_unlock (glob->threads->fitwinlock);
		return FALSE;
	}

	if ((fitwinparam->min > 0) || (fitwinparam->max > 0))
	{
		label = GTK_LABEL (glade_xml_get_widget (fitwinparam->xmlfit, "frequency_range_label"));
		text = g_strdup_printf ("%f - %f GHz", fitwinparam->min / 1e9, fitwinparam->max / 1e9);
		gtk_label_set_text(label, text);
		g_free(text);
	}
	
	if (fitwinparam->numpoints > 0)
	{
		label = GTK_LABEL (glade_xml_get_widget (fitwinparam->xmlfit, "num_datapoints_label"));
		text = g_strdup_printf ("%i", fitwinparam->numpoints);
		gtk_label_set_text(label, text);
		g_free(text);
	}
	
	if (fitwinparam->freeparam > 0)
	{
		label = GTK_LABEL (glade_xml_get_widget (fitwinparam->xmlfit, "free_parameters_label"));
		text = g_strdup_printf ("%i", fitwinparam->freeparam);
		gtk_label_set_text(label, text);
		g_free(text);
	}
	
	label = GTK_LABEL (glade_xml_get_widget (fitwinparam->xmlfit, "rms_label"));
	text = g_strdup_printf ("%.10e", fitwinparam->chi);
	gtk_label_set_text(label, text);
	g_free(text);
	
	if (fitwinparam->text)
	{
		label = GTK_LABEL (glade_xml_get_widget (fitwinparam->xmlfit, "last_iteration_label"));
		gtk_label_set_text(label, fitwinparam->text);
	}
	
	percentage = (float) fitwinparam->iter / fitwinparam->maxiter;
	gtk_progress_bar_set_fraction (
			GTK_PROGRESS_BAR (glade_xml_get_widget (fitwinparam->xmlfit, "fit_progressbar")),
			percentage <= 1.0 ? percentage : 1.0
			);

	text = g_strdup_printf ("iteration %i of %i", fitwinparam->iter, fitwinparam->maxiter);
	gtk_progress_bar_set_text (
			GTK_PROGRESS_BAR (glade_xml_get_widget (fitwinparam->xmlfit, "fit_progressbar")),
			text
			);
	g_free(text);

	g_mutex_unlock (glob->threads->fitwinlock);

	return FALSE;
}

/* Helper function to sort Resonance structs */
gint param_compare (gconstpointer a_in, gconstpointer b_in)
{
	/* See http://bugzilla.gnome.org/show_bug.cgi?id=113697 for this construct */
	const Resonance *a = *((Resonance**)a_in);
	const Resonance *b = *((Resonance**)b_in);
	
	if (a->frq < b->frq)
		return -1;
	else if (a->frq > b->frq)
		return +1;
	else
		return 0;
}

/* Make a backup of all relevant parameters into glob->oldparam. */
void set_up_undo ()
{
	glob->oldparam = (void *) g_malloc (
			sizeof (gint) +				/* number of resonances */
			sizeof (gint) +				/* number of FourierComponents */
			sizeof (double) * (TOTALNUMPARAM+1)	/* glob->param & glob->gparam */
		);

	if (!glob->oldparam) return;

	/* Copy numres */
	*((gint *) glob->oldparam + 0) = glob->numres;
	*((gint *) glob->oldparam + 1) = glob->fcomp->numfcomp;

	/* Copy param & gparam */
	create_param_array (glob->param, glob->fcomp->data, glob->gparam, 
			glob->numres, glob->fcomp->numfcomp,
			(double *) (glob->oldparam+2*sizeof(gint)));

	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "undo_last_fit"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "redo_changes"),  FALSE);
}

/* Discard all undo information. */
void disable_undo ()
{
	g_free (glob->oldparam);
	glob->oldparam = NULL;

	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "undo_last_fit"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "redo_changes"),  FALSE);
}

/* Revert to the old parameter set. */
void undo_changes (gchar undo_redo)
{
	gint numres, numfcomp, i;
	Resonance *res;
	FourierComponent *fcomp;
	gdouble *param;
	void *oldparamset;

	if (!glob->oldparam)
	{
		dialog_message ("No undo/redo information available");
		return;
	}

	visualize_stop_background_calc ();

	/* Save the parameterset we want to restore so that the current
	 * parameters can be stored in glob->oldparam for redo/undo. */
	oldparamset = glob->oldparam;
	set_up_undo ();
	
	param = (gdouble *)(oldparamset+2*sizeof(gint));
	
	/* Restore numres */
	numres   = *((gint *) oldparamset + 0);
	numfcomp = *((gint *) oldparamset + 1);

	/* Has the number of resonances changed? */
	if (numres != glob->numres)
	{
		/* Yes, prepare a new resonance list */
		clear_resonancelist ();
		g_ptr_array_foreach (glob->param, (GFunc) g_free, NULL);
		g_ptr_array_free (glob->param, TRUE);
		glob->param = g_ptr_array_new ();

		/* and populate it */
		for (i=0; i<numres; i++)
		{
			res = g_new (Resonance, 1);

			res->amp   = param[4*i+1];
			res->phase = param[4*i+2];
			res->frq   = param[4*i+3];
			res->width = param[4*i+4];

			add_resonance_to_list (res);
		}

		glob->numres = numres;
	}
	else
	{
		/* No, just overwrite the old parameters */
		create_param_structs (glob->param, NULL, NULL, param, numres, numfcomp);
		update_resonance_list (glob->param);
	}

	/* Set global parameters */
	glob->gparam->phase = param[4*numres+3*numfcomp+1];
	glob->gparam->scale = param[4*numres+3*numfcomp+2];
	glob->gparam->tau   = param[4*numres+3*numfcomp+3];

	/* Has the number of FourierComponents changed? */
	if (numfcomp != glob->fcomp->numfcomp)
	{
		/* Yes */

		/* rebuild list */
		g_ptr_array_foreach (glob->fcomp->data, (GFunc) g_free, NULL);
		g_ptr_array_free (glob->fcomp->data, TRUE);
		glob->fcomp->data = g_ptr_array_new ();

		/* and populate it */
		for (i=0; i<numfcomp; i++)
		{
			fcomp = g_new (FourierComponent, 1);

			fcomp->amp = param[4*numres+3*i+1];
			fcomp->tau = param[4*numres+3*i+2];
			fcomp->phi = param[4*numres+3*i+3];

			fcomp_add_component (fcomp, 0);
		}
		
		glob->fcomp->numfcomp = numfcomp;
	}
	else
	{
		/* No */
		create_param_structs (NULL, glob->fcomp->data, NULL, param, numres, numfcomp);
	}
	
	g_free (oldparamset);

	show_global_parameters (glob->gparam);

	visualize_update_res_bar (FALSE);
	visualize_theory_graph ();
	fcomp_update_list ();
	spectral_resonances_changed ();

	/* Invalidate any error estimates. */
	g_free (glob->stddev);
	glob->stddev = NULL;

	if (undo_redo == 'u')
	{
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "undo_last_fit"), FALSE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "redo_changes") , TRUE);
		statusbar_message ("Reverted to parameterset before last fit or last redo.");
	}
	else if (undo_redo == 'r')
	{
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "undo_last_fit"), TRUE);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "redo_changes") , FALSE);
		statusbar_message ("Reverted to parameterset before last undo.");
	}
}

/* Set a flag that there are unsaved changes */
void set_unsaved_changes ()
{
	const gchar *oldtitle;
	gchar *newtitle;
	
	if ((glob->flag & FLAG_CHANGES) == 0)
	{
		g_mutex_lock (glob->threads->flaglock);
		glob->flag |= FLAG_CHANGES;
		g_mutex_unlock (glob->threads->flaglock);

		oldtitle = gtk_window_get_title (GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")));
		newtitle = g_strdup_printf ("*%s", oldtitle);
		gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")), newtitle);
		g_free (newtitle);
	}

	/* All changes that call cause this function to be called will
	 * have some effect on the spectrum and its fit. Invalidate
	 * any error estimates. */
	g_free (glob->stddev);
	glob->stddev = NULL;
}

/* All changes are saved, update the flag and title */
void unset_unsaved_changes ()
{
	const gchar *oldtitle;
	gchar *newtitle;
	
	g_mutex_lock (glob->threads->flaglock);
	if ((glob->flag & FLAG_CHANGES))
	{
		glob->flag &= ~FLAG_CHANGES;

		oldtitle = gtk_window_get_title (GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")));
		if (oldtitle[0] == '*')
		{
			newtitle = g_strdup_printf ("%s", oldtitle);
			gtk_window_set_title (GTK_WINDOW (glade_xml_get_widget(gladexml, "mainwindow")), newtitle+1);
			g_free (newtitle);
		}
	}
	g_mutex_unlock (glob->threads->flaglock);
}

/* Checks the parameters in *p and asks the user what to do with
 * suspicious values. Updates the graphs and reslist afterwards, too.
 */
gboolean check_and_take_parameters (gdouble *p)
{
	GladeXML *xmlinspect;
	GdkColor color;
	GtkWidget *widget;
	Resonance *res;
	gdouble *p_new;
	gint i;
	gchar *text;
	gboolean all_taken = TRUE;

	p_new = g_new (gdouble, TOTALNUMPARAM+1);

	for (i=0; i<glob->numres; i++)
	{
		/* get the old parameterset for this resonance */
		res = g_ptr_array_index (glob->param, i);

		if (((p[4*i+3] > glob->gparam->min) || (p[4*i+3] == res->frq)) &&
		    ((p[4*i+3] < glob->gparam->max) || (p[4*i+3] == res->frq)) &&
		    ((p[4*i+4] > 1e3) || (p[4*i+4] == res->width)) &&
		    ((p[4*i+4] < 1e8) || (p[4*i+4] == res->width)) &&
		    ((p[4*i+1] > 0  ) || (p[4*i+1] == res->amp  )) &&
		    ((p[4*i+1] < 1e9) || (p[4*i+1] == res->amp  )))
		{
			/* take resonance */
			p_new[4*i+1] = p[4*i+1];
			p_new[4*i+2] = p[4*i+2];
			p_new[4*i+3] = p[4*i+3];
			p_new[4*i+4] = p[4*i+4];
		}
		else
		{
			/* ask the user */
			xmlinspect = glade_xml_new (GLADEFILE, "inspect_resonance_dialog", NULL);

			gdk_color_parse ("red", &color);

			widget = glade_xml_get_widget (xmlinspect, "inspect_text_label");
			text = g_strdup_printf ("Resonance #%d has improbable parameters.\nPlease check and decide what to do.", i+1);
			gtk_label_set_text (GTK_LABEL (widget), text);
			g_free (text);

			widget = glade_xml_get_widget (xmlinspect, "check_frq_label");
			text = g_strdup_printf ("%e", p[4*i+3] / 1e9);
			if ((p[4*i+3] <= glob->gparam->min) || (p[4*i+3] >= glob->gparam->max))
				gtk_widget_modify_fg (widget, GTK_STATE_NORMAL, &color);
			gtk_label_set_text (GTK_LABEL (widget), text);
			g_free (text);

			widget = glade_xml_get_widget (xmlinspect, "check_width_label");
			text = g_strdup_printf ("%e", p[4*i+4] / 1e6);
			if ((p[4*i+4] <= 1e3) || (p[4*i+4] >= 1e8))
				gtk_widget_modify_fg (widget, GTK_STATE_NORMAL, &color);
			gtk_label_set_text (GTK_LABEL (widget), text);
			g_free (text);

			widget = glade_xml_get_widget (xmlinspect, "check_amp_label");
			text = g_strdup_printf ("%e", p[4*i+1]);
			if ((p[4*i+1] <= 0) || (p[4*i+1] > 1e9))
				gtk_widget_modify_fg (widget, GTK_STATE_NORMAL, &color);
			gtk_label_set_text (GTK_LABEL (widget), text);
			g_free (text);

			widget = glade_xml_get_widget (xmlinspect, "check_phase_label");
			text = g_strdup_printf ("%f", NormalisePhase(p[4*i+2]) / M_PI*180);
			gtk_label_set_text (GTK_LABEL (widget), text);
			g_free (text);
			
			/* Mark the resonance for easier identification*/
			if (res->width < 50e6)
				visualize_zoom_to_frequency_range (res->frq-200e6, res->frq+200e6);
			else
				visualize_zoom_to_frequency_range (res->frq-800e6, res->frq+800e6);
			select_res_by_id (i+1);
			
			gtk_dialog_run (GTK_DIALOG (glade_xml_get_widget (xmlinspect, "inspect_resonance_dialog")));

			if (gtk_toggle_button_get_active (
					GTK_TOGGLE_BUTTON (
						glade_xml_get_widget (xmlinspect, "old_param_radio")
					)))
			{
				/* keep old parameter set */
				p_new[4*i+1] = res->amp;
				p_new[4*i+2] = res->phase;
				p_new[4*i+3] = res->frq;
				p_new[4*i+4] = res->width;

				all_taken = FALSE;
			}
			if (gtk_toggle_button_get_active (
					GTK_TOGGLE_BUTTON (
						glade_xml_get_widget (xmlinspect, "use_parameters_radio")
					)))
			{
				/* use those parameters anyway */
				p_new[4*i+1] = p[4*i+1];
				p_new[4*i+2] = p[4*i+2];
				p_new[4*i+3] = p[4*i+3];
				p_new[4*i+4] = p[4*i+4];
			}
			if (gtk_toggle_button_get_active (
					GTK_TOGGLE_BUTTON (
						glade_xml_get_widget (xmlinspect, "skip_all_radio")
					)))
			{
				/* cancel entire process */
				gtk_widget_destroy (glade_xml_get_widget(xmlinspect, "inspect_resonance_dialog"));

				g_free (p);
				p = NULL;
				g_free (p_new);

				return FALSE;
			}

			gtk_widget_destroy (glade_xml_get_widget(xmlinspect, "inspect_resonance_dialog"));
		}
	} /* end of for loop over resonances */

	/* Copy all parameters (without error estimates) */
	for (i=0; i<NUM_GLOB_PARAM+3*glob->fcomp->numfcomp; i++)
		p_new[4*glob->numres+1+i] = p[4*glob->numres+1+i];

	disable_undo ();
	set_up_undo ();

	create_param_structs (glob->param, glob->fcomp->data, glob->gparam, 
			p_new, glob->numres, glob->fcomp->numfcomp);

	/* Need to do this before copying the error data */
	set_unsaved_changes ();

	/* Handle the parameter error data */
	g_free (glob->stddev);
	if ((all_taken) && (p[TOTALNUMPARAM + 1] != -1))
	{
		glob->stddev = g_new (gdouble, TOTALNUMPARAM+1);
		for (i=1; i<=TOTALNUMPARAM; i++)
			glob->stddev[i] = p[TOTALNUMPARAM + i];
	}
	else
		glob->stddev = NULL;

	/* Tidy up */
	g_free (p_new);
	g_free (p);
	p = NULL;

	statusbar_message ("Fit procedure completed");

	visualize_update_res_bar (0);

	update_resonance_list (glob->param);
	show_global_parameters (glob->gparam);
	fcomp_update_list ();
	visualize_theory_graph ();

	return FALSE;
}

/* Gets called each time a new main spectrum is loaded */
void on_load_spectrum ()
{
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "overlay_spectra"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "zoom_selection"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "export"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "graph_as_postscript"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "measure_distance"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "integrate_spectrum"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "fourier_transform"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "fourier_transform_window"), TRUE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "fourier_components"), TRUE);

	on_fft_close_activate (NULL, NULL);
}

/* Gets called each time the main spectrum is deleted */
void on_delete_spectrum ()
{
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "overlay_spectra"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "zoom_selection"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "export"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "graph_as_postscript"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "measure_distance"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "integrate_spectrum"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "fourier_transform"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "fourier_transform_window"), FALSE);
	gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "fourier_components"), FALSE);

	on_fft_close_activate (NULL, NULL);
}

/* Create a backup file with the name of the resonancefile plus '~' suffix */
void create_backup ()
{
	gchar *bakname;

	if (glob->resonancefile == NULL)
		/* No filename given yet */
		return;

	bakname = g_strdup_printf ("%s~", glob->resonancefile);

	save_file (bakname, glob->section, 0);

	g_free (bakname);
}

/* Delete the backup file created above */
void delete_backup ()
{
	gchar *bakname;

	if (glob->resonancefile == NULL)
		/* No filename given yet */
		return;

	bakname = g_strdup_printf ("%s~", glob->resonancefile);

	unlink (bakname);
	g_free (bakname);
}

/* Build a default filename with the given suffix.
 * Returns a directory if suffix == NULL. */
gchar *get_defaultname (gchar *suffix)
{
	gchar *path = NULL;
	gchar *file = NULL, *file2 = NULL;
	gchar *dot, *fullname;

	/* Get a path */
	if (glob->path)
		path = g_strdup (glob->path);
	else if (glob->resonancefile)
		path = g_path_get_dirname (glob->resonancefile);
	else if ((glob->data) && (glob->data->file))
		path = g_path_get_dirname (glob->data->file);
	else
		path = g_get_current_dir ();

	if (!suffix)
	{
		/* No suffix -> just the path is enough */
		fullname = g_strconcat (path, G_DIR_SEPARATOR_S, NULL);
		g_free (path);
		return fullname;
	}

	/* Get a filename */
	if (glob->resonancefile)
		file = g_path_get_basename (glob->resonancefile);
	else if ((glob->data) && (glob->data->file))
		file = g_path_get_basename (glob->data->file);

	if (file)
	{
		/* Remove old suffixes from file */
		if (g_str_has_suffix (file, ".gz"))
			file[strlen(file)-3] = '\0';
		if ((dot = g_strrstr (file, ".")))
			*dot = '\0';
		
		/* Append suffix to file */
		file2 = g_strconcat (file, suffix, NULL);
		g_free (file);
		file = file2;
	}
	else
		file = g_strdup (G_DIR_SEPARATOR_S);

	fullname = g_build_filename (path, file, NULL);
	g_free (path);
	g_free (file);

	return fullname;
}

/* Changes the mouse cursor into a busy cursor if busy == TRUE */
void set_busy_cursor (gboolean busy)
{
	GdkCursor *cursor;
	GtkWidget *widget;

	if (busy)
		cursor = gdk_cursor_new (GDK_WATCH);
	else
		cursor = NULL;

	widget = glade_xml_get_widget (gladexml, "mainwindow");
	gdk_window_set_cursor (widget->window, cursor);
	
	while (gtk_events_pending ()) gtk_main_iteration ();
}

/* BubbleSort, taken from http://wikisource.org/wiki/Bubble_sort#C */
void bubbleSort (gdouble *array, int length)
{
	gint i, j;
	gdouble temp;
	
	for (i = length - 1; i > 0; i--)
		for (j = 0; j < i; j++)
			if (array[j] > array[j+1])
			{
				temp = array[j];
				array[j] = array[j+1];
				array[j+1] = temp;
			}
}
