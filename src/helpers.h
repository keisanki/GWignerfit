#include "structs.h"
#include <glib.h>

#ifndef _HELPERS_H_
#define _HELPERS_H_

void statusbar_message (gchar *format, ...);

gboolean dialog_message (gchar *format, ...);

gint dialog_question (gchar *format, ...);

GPtrArray *get_unique_frqs ();
	
DataVector *new_datavector (guint len);

void free_datavector (DataVector *vec);

gboolean file_is_writeable (gchar *filename);

gchar *get_filename (const gchar *title, const gchar *defaultname, gchar check);

gboolean select_section_dialog (gchar *filename, gchar *default_section, gchar **secname);

gboolean update_fit_window (FitWindowParam *fitwinparam);

void detach_and_free_shm (int shmid);

gboolean check_and_take_parameters (gdouble *p);

void set_up_undo ();

void undo_changes (gchar undo_redo);

void disable_undo ();

void on_load_spectrum ();

void on_delete_spectrum ();

void set_unsaved_changes ();

void unset_unsaved_changes ();

void create_backup ();

void delete_backup ();

gint param_compare (gconstpointer a_in, gconstpointer b_in);

gchar *get_defaultname (gchar *suffix);

void set_busy_cursor (gboolean busy);

void bubbleSort (gdouble *array, int length);

#endif
