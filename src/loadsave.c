#include <stdio.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <string.h>

#include "helpers.h"

/* Global variables */
extern GladeXML *gladexml;
extern GlobalData *glob;

/* Forward declarations */

/* Strips any newline chars from a (NULL terminated!) string */
static void ls_strip_newline (gchar *line)
{
	if ((strlen (line)) && (line[strlen (line)-1] == '\n'))
		line[strlen (line)-1] = '\0'; /* strip final \n */
	if ((strlen (line)) && (line[strlen (line)-1] == '\r'))
		line[strlen (line)-1] = '\0'; /* strip final \r */
}

/* Returns a list of sections found in filename. The section marker has to
 * match one of those given in marker (NULL terminated!). */
GList* ls_get_sections (gchar *filename, gchar *marker)
{
	FILE *datafile;
	GList *sections = NULL;
	gchar line[256];
	gint pos = 0, i;
	
	g_return_val_if_fail (filename && marker, NULL);
	
	datafile = fopen (filename, "r");
	if (datafile == NULL)
	{
		dialog_message ("Error: Could not open file %s.", filename);
		return NULL;
	}

	line[255] = '\0';
	while (!feof (datafile)) {
		fgets (line, 255, datafile);
		ls_strip_newline (line);
		pos++;

		if (strlen (line) < 2)
			continue;

		if (strlen (line) > 254) {
			fprintf (stderr, "Error: Line %i in '%s' too long!\n", pos, filename);
			g_list_foreach (sections, (GFunc) g_free, NULL);
			g_list_free (sections);
			return NULL;
		}

		for (i=0; i<strlen (marker); i++)
		{
			if (line[0] == marker[i])
			{
				/* Found a new section */
				sections = g_list_append (sections, g_strdup (line+1));
				break;
			}
		}
	}

	fclose (datafile);

	return sections;
}

/* Lets the user select between sections. If default_section != NULL highlight
 * it as the initial value. Return the (newly allocated) section. */
gchar* ls_select_section (GList *sections, gchar *default_section)
{
	GtkWidget *combo;
	GladeXML *xml;
	gchar *section = NULL;
	gint result;

	g_return_val_if_fail (sections, NULL);

	xml = glade_xml_new (GLADEFILE, "section_dialog", NULL);
	
	/* Add sections to combo box */
	combo = glade_xml_get_widget (xml, "sections_combo");
	gtk_combo_set_popdown_strings (GTK_COMBO (combo), sections);

	/* Select current section if found */
	if (default_section)
	{
		while (sections)
		{
			if (!strcmp ((gchar *) sections->data, default_section))
				break;
			sections = g_list_next (sections);
		}
		
		if (sections && (!strcmp ((gchar *) sections->data, default_section)))
			gtk_entry_set_text (
				GTK_ENTRY (glade_xml_get_widget(xml, "section_combo_entry")), 
				default_section);
	}

	/* Run dialog */
	result = gtk_dialog_run (GTK_DIALOG (glade_xml_get_widget (xml, "section_dialog")));

	if (result == GTK_RESPONSE_OK)
	{
		section = g_strdup (gtk_entry_get_text (
					GTK_ENTRY (glade_xml_get_widget (
							xml, "section_combo_entry"))));
		
		gtk_widget_destroy (glade_xml_get_widget (xml, "section_dialog"));
//		g_free (xml);
	}
	else
	{
		gtk_widget_destroy (glade_xml_get_widget (xml, "section_dialog"));
//		g_free (xml);
		return NULL;
	}

	return section;
}

/* Asks the user for a new section name (displaying defsec as default) 
 * and returns it. */
gchar* ls_input_section (gchar *defsec)
{
	GladeXML *xmldialog;
	gint result;
	gchar *newsection = NULL;

	xmldialog = glade_xml_new (GLADEFILE, "save_section_dialog", NULL);

	if (defsec)
	{
		gtk_entry_set_text (GTK_ENTRY (glade_xml_get_widget (xmldialog, "save_section_entry")),
				defsec);
	}

	result = gtk_dialog_run (GTK_DIALOG (glade_xml_get_widget (xmldialog, "save_section_dialog")));

	if (result == GTK_RESPONSE_OK)
	{
		newsection = g_strdup (gtk_entry_get_text (
					GTK_ENTRY (glade_xml_get_widget (xmldialog, "save_section_entry"))
					));
		gtk_widget_destroy (glade_xml_get_widget(xmldialog, "save_section_dialog"));

		if (strlen (newsection) == 0)
		{
			dialog_message ("You need to enter a non empty section name.");
			return FALSE;
		}

		if (g_strrstr (newsection, ":"))
		{
			dialog_message ("You must not use colons in a section name.");
			g_free (newsection);
			return FALSE;
		}
	}
	else
		gtk_widget_destroy (glade_xml_get_widget(xmldialog, "save_section_dialog"));

	return newsection;
}

/* Executes write_callback at the correct position in filename so that it can
 * write its information into the file. Returns TRUE on success. */
gboolean ls_save_file_exec (gchar *filename, gchar *section, gchar marker, gint exists,
		    void (*write_callback) (FILE *datafile, gchar *filename, gchar *section, gchar *newline))
{
	GString *old_file_before = NULL, *old_file_after = NULL;
	gchar line[256], *newline;
	FILE *datafile;	

	g_return_val_if_fail (filename, FALSE);
	g_return_val_if_fail (section, FALSE);

	newline = g_alloca (3 * sizeof(gchar));
	newline[0] = '\0';

	if (exists == 2)
	{
		/* File and section already exist */
		datafile = fopen (filename, "r");
		if (!datafile)
			return FALSE;

		old_file_before = g_string_new ("");

		/* Copy everything before the relevant section into old_file_before */
		while (!feof (datafile))
		{
			fgets (line, 255, datafile);

			if (strlen (line) > 254)
			{
				dialog_message ("Error: Line is too long!\n");
				fclose (datafile);
				g_string_free (old_file_before, TRUE);
				return FALSE;
			}

			/* Deduce newline chars */
			if ((strlen (line) > 2) && (newline[0] == '\0'))
			{
				if (line[strlen (line)-2] == '\r')
					newline = "\r\n\0";
				else
					newline = "\n\0\0";
			}

			ls_strip_newline (line);

			if ((strlen (line) > 1) && (line[0] == marker) && (strncmp (line+1, section, 254) == 0))
			{
				/* Found the selected section */
				break;
			}

			g_string_append (old_file_before, line);
			g_string_append (old_file_before, newline);
		}
		
		if (feof (datafile))
		{
			/* We shouldn't be at the end of the file yet */
			fclose (datafile);
			g_string_free (old_file_before, TRUE);
			return FALSE;
		}

		old_file_after  = g_string_new ("");
		
		/* Skip the section to be saved */
		while (!feof (datafile))
		{
			fgets (line, 255, datafile);

			if (strlen(line) > 254)
			{
				dialog_message ("Error: Line is too long!\n");
				fclose (datafile);
				g_string_free (old_file_before, TRUE);
				return FALSE;
			}

			/* Deduce newline chars */
			if ((strlen (line) > 3) && (newline[0] == '\0'))
			{
				if (line[strlen (line)-2] == '\r')
					newline = "\r\n\0";
				else
					newline = "\n\0\0";
			}

			if ((*line == '$') || (*line == '=') || (*line == '>')) 
			{
				/* Found the next section */
				g_string_append (old_file_after, line);
				break;
			}
		}

		/* Copy everything after the relevant section into old_file_after */
		while (!feof (datafile))
		{
			fgets (line, 255, datafile);

			if (strlen (line) > 254)
			{
				dialog_message ("Error: Line is too long!\n");
				fclose (datafile);
				g_string_free (old_file_before, TRUE);
				g_string_free (old_file_after, TRUE);
				return FALSE;
			}

			g_string_append (old_file_after, line);
		}

		fclose (datafile);
		datafile = fopen (filename, "w");

		fprintf (datafile, "%s", old_file_before->str);
		g_string_free (old_file_before, TRUE);
	}
	else if (exists == 1)
	{
		/* The file exists, but the section does not */
		datafile = fopen (filename, "a");
		if (!datafile)
			return FALSE;
		newline = "\n\0\0";
	}
	else
	{
		/* Nothing exists */
		datafile = fopen (filename, "w");
		if (!datafile)
			return FALSE;
		newline = "\n\0\0";

		fprintf (datafile, "# This is a GWignerFit session file, version 1%s", newline);
	}

	/* Write content into section */
	(* write_callback) (datafile, filename, section, newline);
	
	/* A newline before the next section */
	fprintf (datafile, "%s", newline);

	if (exists == 2)
	{
		/* Write the rest of the old file */
		fprintf (datafile, "%s", old_file_after->str);
		g_string_free (old_file_after, TRUE);
	}

	fclose (datafile);

	return TRUE;
}

/* Calls write_callback to save data under the given section in selected_filename.
 * The section will be prepended by the marker character.
 * Asks the user about overwrites. Returns TRUE on successful write. */
gboolean ls_save_file (gchar *selected_filename, gchar *section, gchar marker,
		void (*write_callback) (FILE *datafile, gchar *filename, gchar *section, gchar *newline)) 
{
	gchar line[256];
	FILE *datafile;
	gint pos = 0, exists = 0;

	datafile = fopen (selected_filename, "r");
	if (datafile) 
	{
		while (!feof (datafile))
		{
			fgets (line, 255, datafile);
			ls_strip_newline (line);
			pos++;

			if (strlen (line) < 2)
				continue;

			if (strlen (line) > 254)
			{
				fprintf (stderr, "Error: Line %i in '%s' too long!\n", pos, selected_filename);
				return FALSE;
			}

			if ((line[0] == marker) && (strncmp(line+1, section, 254) == 0))
			{
				/* Found the selected section -> overwrite */
				exists = 2;
				break;
			}

			if ((*line == '$') || (*line == '=') || (*line == '>')) 
			{
				/* Found a new section */
				pos++;
			}
		}
		fclose (datafile);

		if (!exists)
			/* File exists, but section does not -> append */
			exists = 1;
	}

	if (exists == 2)
	{
		if (dialog_question ("Section '%s' already exists in '%s', overwrite?", 
				section, g_path_get_basename (selected_filename)) 
				!= GTK_RESPONSE_YES)
			return FALSE;
	}
	else if ((exists == 1) && (glob->prefs->confirm_append))
	{
		if (dialog_question ("File '%s' exists, append section '%s'?", 
				g_path_get_basename (selected_filename), section)
				!= GTK_RESPONSE_YES)
			return FALSE;
	}

	return ls_save_file_exec (selected_filename, section, marker, exists, write_callback);
}

/* Reads the given section from file marked by marker a GPtrArray, returning it */
GPtrArray* ls_read_section (gchar *filename, gchar *section, gchar marker)
{
	FILE *datafile;
	gchar *line;
	GPtrArray *lines;
	gint pos = 0, flag = 0;
	
	g_return_val_if_fail (filename, NULL);
	g_return_val_if_fail (section, NULL);

	datafile = fopen (filename, "r");

	if (!datafile) {
		dialog_message("Error: Could not open file %s.", filename);
		return NULL;
	}

	lines = g_ptr_array_new ();
	line  = g_new0 (gchar, 256);

	while (!feof (datafile)) {
		fgets (line, 255, datafile);
		ls_strip_newline (line);
		pos++;

		if (strlen (line) > 254) {
			dialog_message ("Error: Line %i in '%s' too long!\n", pos, filename);
			g_ptr_array_foreach (lines, (GFunc) g_free, NULL);
			g_ptr_array_free (lines, TRUE);
			g_free (line);
			return NULL;
		}

		/* Stop parsing after the current section */
		if ((flag) && ((line[0] == '$') || (line[0] == '=') || (line[0] == '>')))
			break;

		if (flag)
		{
			/* Copy section content */
			g_ptr_array_add (lines, line);
			line = g_new0 (gchar, 256);
		}

		if ((strlen (line) > 1) && (line[0] == marker) && (strncmp (line+1, section, 254) == 0))
		{
			/* Start of requested section */
			flag = 1;
		}
	}

	if ((feof (datafile)) && (flag == 0)) {
		dialog_message ("Error: No section '%s' found in '%s'.\n", section, filename);
		g_ptr_array_free (lines, TRUE);
		g_free (line);
		return NULL;
	}

	fclose (datafile);
	g_free (line);

	return lines;
}
