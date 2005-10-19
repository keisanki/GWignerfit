#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glade/glade.h>
#include <unistd.h>

#include "structs.h"
#include "helpers.h"
#include "gtkspectvis.h"
#include "preferences.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

enum {
	ID_COL = 0, 
	FRQ_COL, WID_COL, AMP_COL, PHAS_COL,
	FIT_FRQ_COL, FIT_WID_COL, FIT_AMP_COL, FIT_PHAS_COL,
	N_COLUMNS
};

typedef struct {
	GtkWidget *textview;
	GtkToggleButton *idbut, *frqbut, *widbut, *ampbut, *phasbut, *qualbut, *stddevbut;
} ExportTextviewData;

static void export_text_to_view (GtkWidget *textview, gchar *format, ...)
{
	GtkTextBuffer *buffer;
	GtkTextIter textiter;
	va_list ap;
	gchar *text;

	va_start (ap, format);
	text = g_strdup_vprintf (format, ap);
	va_end (ap);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));

	gtk_text_buffer_get_end_iter (buffer, &textiter);
	gtk_text_buffer_insert (buffer, &textiter, text, -1);
	g_free (text);
}

static void export_fill_textview (GtkWidget *textview, gboolean addid, gboolean addfrq, gboolean addwid, gboolean addamp, gboolean addphas, gboolean addqual, gboolean show_stddev)
{
	GtkTextBuffer *buffer;
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	GtkTreePath *path;
	GtkTreeIter iter;
	GtkTextIter textiter, start;
	gdouble frq, wid, amp, phas, qval, qerr;
	gboolean added = FALSE;
	gint id;

	g_return_if_fail (textview != NULL);

	/* Get the TextBuffer */
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));

	/* Delete any old content */
	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter   (buffer, &textiter);
	gtk_text_buffer_delete (buffer, &start, &textiter);

	/* Create a header line */
	if (addid)
	{
		export_text_to_view (textview, "ID");
		added = TRUE;
	}
	if (addfrq)
	{
		if (added)
			export_text_to_view (textview, "\t");
		if (show_stddev && glob->stddev)
			export_text_to_view (textview, "frq [GHz]\t  delta frq");
		else
			export_text_to_view (textview, "frq [GHz]");
		added = TRUE;
	}
	if (addwid)
	{
		if (added)
			export_text_to_view (textview, "\t");
		if (show_stddev && glob->stddev)
			export_text_to_view (textview, "width [%s]\tdelta width",
					glob->prefs->widthunit == 6 ? "MHz" : "kHz");
		else
			export_text_to_view (textview, "width [%s]",
					glob->prefs->widthunit == 6 ? "MHz" : "kHz");
		added = TRUE;
	}
	if (addamp)
	{
		if (added)
			export_text_to_view (textview, "\t");
		if (show_stddev && glob->stddev)
			export_text_to_view (textview, "  amp [Hz]\tdelta amp");
		else
			export_text_to_view (textview, "  amp [Hz]");
		added = TRUE;
	}
	if (addphas)
	{
		if (added)
			export_text_to_view (textview, "\t");
		if (show_stddev && glob->stddev)
			export_text_to_view (textview, "phase [deg]\tdelta phase");
		else
			export_text_to_view (textview, "phase [deg]");
		added = TRUE;
	}
	if (addqual)
	{
		qval = frq*1e9/wid/pow(10, glob->prefs->widthunit);
		if (added)
			export_text_to_view (textview, "\t");
		if (show_stddev && glob->stddev)
			export_text_to_view (textview, " quality\tdelta quality");
		else
			export_text_to_view (textview, " quality");
		added = TRUE;
	}
	if (added)
	{
		export_text_to_view (textview, "\r\n");
		gtk_text_buffer_get_start_iter (buffer, &textiter);
		gtk_text_buffer_insert (buffer, &textiter, "# ", -1);
	}

	/* Go through the resonancelist */
	path = gtk_tree_path_new_first ();
	while (gtk_tree_model_get_iter(model, &iter, path))
	{
		gtk_tree_model_get (model, &iter,
				ID_COL, &id,
				FRQ_COL, &frq,
				WID_COL, &wid,
				AMP_COL, &amp,
				PHAS_COL, &phas,
				-1);

		added = FALSE;
		if (addid)
		{
			export_text_to_view (textview, "% 4d", id);
			added = TRUE;
		}
		if (addfrq)
		{
			if (added)
				export_text_to_view (textview, "\t");
			if (show_stddev && glob->stddev)
				if (glob->stddev[4*(id-1)+3] > 0)
					export_text_to_view (textview, "% 12.9f\t % 11.9f", 
						frq, glob->stddev[4*(id-1)+3]/1e9);
				else
					export_text_to_view (textview, "% 12.9f\t %s", 
						frq, "     ---    ");
			else
				export_text_to_view (textview, "% 12.9f", frq);
			added = TRUE;
		}
		if (addwid)
		{
			if (added)
				export_text_to_view (textview, "\t");
			if (show_stddev && glob->stddev)
				if (glob->stddev[4*(id-1)+4] > 0)
					export_text_to_view (textview, "% 10.6f\t % 9.6f",
						wid, glob->stddev[4*(id-1)+4]/pow(10, glob->prefs->widthunit));
				else
					export_text_to_view (textview, "% 10.6f\t %s",
						wid, "   ---   ");
			else
				export_text_to_view (textview, "% 10.6f", wid);
			added = TRUE;
		}
		if (addamp)
		{
			if (added)
				export_text_to_view (textview, "\t");
			if (show_stddev && glob->stddev)
				if (glob->stddev[4*(id-1)+1] > 0)
					export_text_to_view (textview, "% 11.2f\t % 7.2f",
						amp, glob->stddev[4*(id-1)+1]);
				else
					export_text_to_view (textview, "% 11.2f\t %s",
						amp, "   --- ");
			else
				export_text_to_view (textview, "% 11.2f", amp);
			added = TRUE;
		}
		if (addphas)
		{
			if (added)
				export_text_to_view (textview, "\t");
			if (show_stddev && glob->stddev)
				if (glob->stddev[4*(id-1)+2] > 0)
					export_text_to_view (textview, "% 8.3f\t % 8.3f",
						phas, glob->stddev[4*(id-1)+2]/M_PI*180.0);
				else
					export_text_to_view (textview, "% 8.3f\t %s",
						phas, "   ---  ");
			else
				export_text_to_view (textview, "% 8.3f", phas);
			added = TRUE;
		}
		if (addqual)
		{
			qval = frq*1e9/wid/pow(10, glob->prefs->widthunit);
			if (added)
				export_text_to_view (textview, "\t");
			if (show_stddev && glob->stddev)
				if ((glob->stddev[4*(id-1)+3] > 0) &&
				    (glob->stddev[4*(id-1)+4] > 0))
				{
					qerr = sqrt ( pow(glob->stddev[4*(id-1)+3]/wid/pow(10, glob->prefs->widthunit), 2) +
						      pow(glob->stddev[4*(id-1)+4]*qval/wid/pow(10, glob->prefs->widthunit), 2)
						    );
					export_text_to_view (textview, "% 8.1f\t % 5.1f",
						qval, qerr);
				}
				else
					export_text_to_view (textview, "% 8.1f\t %s",
						qval, " --- ");
			else
				export_text_to_view (textview, "% 8.1f", qval);
			added = TRUE;
		}
		
		if (added)
			export_text_to_view (textview, "\r\n");

		gtk_tree_path_next (path);
	}
}

static void export_change_exported_data (GtkToggleButton *togglebutton, gpointer data)
{
	gboolean addid, addfrq, addwid, addamp, addphas, addqual, show_stddev;
	ExportTextviewData *passdata = (ExportTextviewData *) data;

	addid = addfrq = addwid = addamp = addphas = addqual = show_stddev = FALSE;

	if (gtk_toggle_button_get_active (passdata->idbut))
		addid = TRUE;
	if (gtk_toggle_button_get_active (passdata->frqbut))
		addfrq = TRUE;
	if (gtk_toggle_button_get_active (passdata->widbut))
		addwid = TRUE;
	if (gtk_toggle_button_get_active (passdata->ampbut))
		addamp = TRUE;
	if (gtk_toggle_button_get_active (passdata->phasbut))
		addphas = TRUE;
	if (gtk_toggle_button_get_active (passdata->qualbut))
		addqual = TRUE;
	if (gtk_toggle_button_get_active (passdata->stddevbut))
		show_stddev = TRUE;

	export_fill_textview (passdata->textview, addid, addfrq, addwid, addamp, addphas, addqual, show_stddev);
}

gboolean export_savebutton_clicked (GtkButton *button, gpointer user_data)
{
	GtkTextView *view = (GtkTextView *) user_data;
	GtkTextBuffer *buffer;
	GtkTextIter start, stop;
	gchar *text, *filename, *defaultname;
	FILE *fh;

	defaultname = get_defaultname (".res");
	filename = get_filename ("Select filename for parameter export", defaultname, 2);
	g_free (defaultname);

	if (!filename) 
		return TRUE;
	
	/* Get the TextBuffer */
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));

	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter   (buffer, &stop);

	text = gtk_text_buffer_get_text (buffer,
			&start, &stop,
			FALSE);

	fh = fopen (filename, "w");
	if (fh)
	{
		fprintf (fh, "%s", text);
		fclose (fh);
		
		statusbar_message ("Resonance data export successful");
	}
	else
		dialog_message ("Error: Could not create file '%s'.", filename);

	g_free (text);

	return TRUE;
}

void export_resonance_data ()
{
	GladeXML *xmldialog;
	GtkWidget *dialog, *textview, *sw;
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	PangoFontDescription *font_desc;
	ExportTextviewData *passdata;
	GtkTextIter textiter, start;
	GtkTextBuffer *buffer;

	g_return_if_fail (model != NULL);

	/* Load the widgets */
	xmldialog = glade_xml_new (GLADEFILE, "export_dialog", NULL);
	dialog = glade_xml_get_widget (xmldialog, "export_dialog");
	sw = glade_xml_get_widget (xmldialog, "export_scrolledwin");

	/* Add a GtkTextView to the ScrolledWindow */
	textview = gtk_text_view_new ();
	gtk_widget_show (textview);
	gtk_container_add (GTK_CONTAINER (sw), textview);
	gtk_text_view_set_editable (GTK_TEXT_VIEW (textview), FALSE);

	/* Set the correct unit for the export_width_check label */
	if (glob->prefs->widthunit == 6)
		gtk_button_set_label (
				GTK_BUTTON (glade_xml_get_widget (xmldialog, "export_width_check")),
				"Include width data [MHz]");
	if (glob->prefs->widthunit == 3)
		gtk_button_set_label (
				GTK_BUTTON (glade_xml_get_widget (xmldialog, "export_width_check")),
				"Include width data [kHz]");

	/* Make the export error check insensitive if there is no appropriate data. */
	if (glob->stddev == NULL)
		gtk_widget_set_sensitive (
				glade_xml_get_widget (xmldialog, "export_stddev_check"), FALSE);

	/* Set up the signals for the toggle buttons */
	passdata = g_new (ExportTextviewData, 1);
	passdata->textview = textview;
	passdata->idbut  = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "export_id_check"));
	passdata->frqbut = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "export_frq_check"));
	passdata->widbut = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "export_width_check"));
	passdata->ampbut = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "export_amp_check"));
	passdata->phasbut= GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "export_phase_check"));
	passdata->qualbut= GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "export_qual_check"));
	passdata->stddevbut = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "export_stddev_check"));

	/* Set state of checkboxes */
	if (glob->prefs->res_export != -1)
	{
		gtk_toggle_button_set_active (passdata->idbut    , glob->prefs->res_export & (1 << 0));
		gtk_toggle_button_set_active (passdata->frqbut   , glob->prefs->res_export & (1 << 1));
		gtk_toggle_button_set_active (passdata->widbut   , glob->prefs->res_export & (1 << 2));
		gtk_toggle_button_set_active (passdata->ampbut   , glob->prefs->res_export & (1 << 3));
		gtk_toggle_button_set_active (passdata->phasbut  , glob->prefs->res_export & (1 << 4));
		gtk_toggle_button_set_active (passdata->qualbut  , glob->prefs->res_export & (1 << 5));
		gtk_toggle_button_set_active (passdata->stddevbut, glob->prefs->res_export & (1 << 6));
	}
	
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "export_id_check")), 
			"toggled",
			(GCallback) export_change_exported_data, (gpointer) passdata);
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "export_frq_check")), 
			"toggled",
			(GCallback) export_change_exported_data, (gpointer) passdata);
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "export_width_check")), 
			"toggled",
			(GCallback) export_change_exported_data, (gpointer) passdata);
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "export_amp_check")), 
			"toggled",
			(GCallback) export_change_exported_data, (gpointer) passdata);
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "export_phase_check")), 
			"toggled",
			(GCallback) export_change_exported_data, (gpointer) passdata);
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "export_qual_check")), 
			"toggled",
			(GCallback) export_change_exported_data, (gpointer) passdata);
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "export_stddev_check")), 
			"toggled",
			(GCallback) export_change_exported_data, (gpointer) passdata);
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "savebutton")), 
			"clicked",
			(GCallback) export_savebutton_clicked, (gpointer) textview);

	/* Set the font */
	font_desc = pango_font_description_from_string ("Monospace 12");
	gtk_widget_modify_font (textview, font_desc);
	pango_font_description_free (font_desc);

	//export_fill_textview (textview, FALSE, TRUE, TRUE, TRUE, TRUE, FALSE, FALSE);
	export_change_exported_data (NULL, (gpointer) passdata);
	
	gtk_dialog_run (GTK_DIALOG (dialog));

	/* I don't know why, but if the dialog gets destroyed while some text
	 * is selected, the application segfaults. As I don't know how to
	 * unselect text, I just remove everything.
	 */
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
	gtk_text_buffer_get_start_iter (buffer, &start);
	gtk_text_buffer_get_end_iter   (buffer, &textiter);
	gtk_text_buffer_delete (buffer, &start, &textiter);

	/* Remember state of checkboxes */
	glob->prefs->res_export = 0;
	if (gtk_toggle_button_get_active (passdata->idbut    ))
		glob->prefs->res_export += (1 << 0);
	if (gtk_toggle_button_get_active (passdata->frqbut   ))
		glob->prefs->res_export += (1 << 1);
	if (gtk_toggle_button_get_active (passdata->widbut   ))
		glob->prefs->res_export += (1 << 2);
	if (gtk_toggle_button_get_active (passdata->ampbut   ))
		glob->prefs->res_export += (1 << 3);
	if (gtk_toggle_button_get_active (passdata->phasbut  ))
		glob->prefs->res_export += (1 << 4);
	if (gtk_toggle_button_get_active (passdata->qualbut  ))
		glob->prefs->res_export += (1 << 5);
	if (gtk_toggle_button_get_active (passdata->stddevbut))
		glob->prefs->res_export += (1 << 6);
	prefs_save (glob->prefs);
	
	gtk_widget_destroy (dialog);
}

gboolean export_theory_graph_data (char *filename)
{
	FILE *datafile;
	guint i;

	g_return_val_if_fail (glob->theory->len > 0, FALSE);

	/* Wait for a possible background calculation to finish */
	g_mutex_lock (glob->threads->theorylock);

	datafile = fopen (filename, "w");
	if (!datafile)
	{
		dialog_message ("Error: Could not open file '%s' for writing.", filename);
		g_mutex_unlock (glob->threads->theorylock);

		return FALSE;
	}

	for (i=0; i<glob->theory->len; i++)
	{
		fprintf (datafile, "%11.1f\t%10.8f\t%10.8f\r\n",
				glob->theory->x[i], 
				glob->theory->y[i].re,
				glob->theory->y[i].im);
	}

	fclose (datafile);
	g_mutex_unlock (glob->threads->theorylock);

	return TRUE;
}

gboolean export_select_filename (GtkButton *button, gpointer user_data)
{
	gchar *filename, *defaultname;
	gchar *file, *path;
	GladeXML *xmldialog = (GladeXML *) user_data;
	GtkEntry *pathentry, *fileentry;

	defaultname = get_defaultname (".ps");
	filename = get_filename ("Select filename for postscript", defaultname, 0);
	g_free (defaultname);

	if (filename)
	{
		pathentry = GTK_ENTRY (glade_xml_get_widget (xmldialog, "ps_path_entry"));
		fileentry = GTK_ENTRY (glade_xml_get_widget (xmldialog, "ps_filename_entry"));

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

	return TRUE;
}

gboolean export_graph_ps ()
{
	const gchar *filename, *title, *footer;
	const gchar *filen, *path;
	GArray *uids, *legend, *lt;
	GtkSpectVis *spectvis;
	GladeXML *xmldialog;
	GtkWidget *dialog;
	GtkToggleButton *toggle;
	gboolean showlegend = FALSE;
	GtkTreeIter iter;
	GtkTreeModel *model;
	guint overlayid;
	gint linetype;
	gchar *str, *basename, pos;
	FILE *file;

	spectvis = GTK_SPECTVIS (glade_xml_get_widget (gladexml, "graph"));
	g_return_val_if_fail (spectvis, FALSE);

	/* Load the widgets */
	xmldialog = glade_xml_new (GLADEFILE, "export_ps_dialog", NULL);
	dialog = glade_xml_get_widget (xmldialog, "export_ps_dialog");

	/* Connect the select filename button */
	g_signal_connect(
			G_OBJECT (glade_xml_get_widget (xmldialog, "button10")), 
			"clicked",
			(GCallback) export_select_filename,
			xmldialog);

	/* Put some default text into the GtkEntry fields */
	if (glob->section)
	{
		basename = g_path_get_basename (glob->resonancefile);
		str = g_strdup_printf ("file: %s (%s)", basename, glob->section);
		g_free (basename);

		gtk_entry_set_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_footer_entry")),
				str);
	}
	else if ((glob->data) && (glob->data->file))
	{
		basename = g_path_get_basename (glob->data->file);
		str = g_strdup_printf ("file: %s", basename);
		g_free (basename);

		gtk_entry_set_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_footer_entry")),
				str);
	}

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
	{
		path = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_path_entry")));
		filen = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_filename_entry")));
		if (path)
			filename = g_build_filename (path, filen, NULL);
		else
			filename = g_strdup (filen);

		title = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_title_entry")));
		footer = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_footer_entry")));

		/* Does the file already exist? */
		file = fopen (filename, "r");
		if (file)
		{
			basename = g_path_get_basename (filename);

			if (dialog_question ("File '%s' already exists, overwrite?", basename)
					!= GTK_RESPONSE_YES)
			{
				/* File exists, do not overwrite it. */
				fclose (file);
				g_free (basename);
				//g_free ((gchar *) filename);
				gtk_widget_destroy (dialog);
				statusbar_message ("Postscript export canceled by user");

				return FALSE;
			}

			fclose (file);
			g_free (basename);

			if (unlink (filename))
			{
				/* Something went wrong during file deletion */
				gtk_widget_destroy (dialog);
				dialog_message ("Error: Cannot delete file %s.", filename);
				statusbar_message ("Postscript export canceled");
			}
		}
		else if (filename[0] != '|')
		{
			/* Is the file writeable? */

			/* Do this check only, if the first char isn't a pipe.
			 * If it is, the user wants to pass the ps to another
			 * application such as lpr.
			 */

			file = fopen (filename, "w");
			if (!file)
			{
				/* Cannot write to file -> abort */
				gtk_widget_destroy (dialog);
				dialog_message ("Error: Cannot write to file %s.", filename);
				statusbar_message ("Postscript export canceled");
				//g_free ((gchar *) filename);

				return FALSE;
			}
			fclose (file);
		}

		uids = g_array_new (FALSE, FALSE, sizeof (guint));
		legend = g_array_new (FALSE, FALSE, sizeof (gchar*));
		lt = g_array_new (FALSE, FALSE, sizeof (gint));

		/* Should a legend be displayed? */
		toggle = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_legend_check"));
		if (gtk_toggle_button_get_active (toggle))
			showlegend = TRUE;

		/* And where? */
		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_top_button"))))
			pos = 't';
		else
			pos = 'b';
		
		toggle = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_overlay_check"));
		if ((gtk_toggle_button_get_active (toggle)) && (glob->overlaystore))
		{
			/* Include the overlay graphs */
			model = GTK_TREE_MODEL (glob->overlaystore);

			if (gtk_tree_model_get_iter_first (model, &iter))
			{
				gtk_tree_model_get (model, &iter, 0, &overlayid, -1);
				g_array_append_val (uids, overlayid);

				/* Set the linetype */
				/* g_array_append_val needs a variable as second parameter */
				linetype = 9;
				g_array_append_val (lt, linetype);

				if (showlegend)
					str = g_strdup_printf ("overlay");
				else
					str = "";
				g_array_append_val (legend, str);
				str = "";

				while (gtk_tree_model_iter_next (model, &iter))
				{
					gtk_tree_model_get (model, &iter, 0, &overlayid, -1);
					g_array_append_val (uids, overlayid);
					g_array_append_val (legend, str);
					g_array_append_val (lt, linetype);
				}
			}
		}
		
		/* Always include the data graph */;
		g_array_append_val (uids, glob->data->index);
		if (showlegend)
			str = g_strdup_printf ("data");
		else
			str = "";
		g_array_append_val (legend, str);

		linetype = 1;
		g_array_append_val (lt, linetype);

		toggle = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_theory_check"));
		if (gtk_toggle_button_get_active (toggle))
		{
			/* Include the theory graph */
			g_array_append_val (uids, glob->theory->index);
			if (showlegend)
				str = g_strdup_printf ("theory");
			else
				str = "";
			g_array_append_val (legend, str);

			linetype = 3;
			g_array_append_val (lt, linetype);
		}

		/* Export! */
		gtk_spect_vis_export_ps (spectvis, uids, filename, title, 
					 "frequency (GHz)", NULL, footer, 
					 legend, pos, lt);

		/* Tidy up */
		g_array_free (uids, TRUE);
		g_array_free (legend, TRUE);
		g_array_free (lt, TRUE);
		//g_free ((gchar *) filename);
	}

	gtk_widget_destroy (dialog);
	statusbar_message ("Postscript export successful");

	return TRUE;
}
