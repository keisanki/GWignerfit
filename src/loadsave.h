#ifndef _LOADSAVE_H_
#define _LOADSAVE_H_

GList* ls_get_sections (gchar *filename, gchar *marker);

gchar* ls_select_section (GList *sections, gchar *default_section);

gchar* ls_input_section (gchar *defsec);

gboolean ls_save_file_exec (gchar *filename, gchar *section, gchar marker, gint exists,
		    void (*write_callback) (FILE *datafile, gchar *filename, gchar *section, gchar *newline));
	
gboolean ls_save_file (gchar *selected_filename, gchar *section, gchar marker,
		void (*write_callback) (FILE *datafile, gchar *filename, gchar *section, gchar *newline));

GPtrArray* ls_read_section (gchar *filename, gchar *section, gchar marker);

#endif

