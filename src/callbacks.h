#ifndef _CALLBACKS_H_
#define _CALLBACKS_H_

#include <gtk/gtk.h>

gboolean quit_application (GtkWidget *widget, GdkEvent *event, gpointer data);

void destroy (GtkWidget	*widget, gpointer data);

gboolean on_close_dialog (GtkWidget *button);

void on_new_activate (GtkMenuItem *menuitem, gpointer user_data);

void import_data (GtkMenuItem *menuitem, gpointer user_data);

void on_open1_activate (GtkMenuItem *menuitem, gpointer user_data);

gboolean on_reflection_activate (GtkMenuItem *menuitem, gpointer user_data);

gboolean on_transmission_activate (GtkMenuItem *menuitem, gpointer user_data);

void sigusr1_handler (int num);

void sigchld_handler (int num);

void ok_dialog (GtkWidget *entry, gpointer dialog);

void on_save_as_activate (GtkMenuItem *menuitem, gpointer write_callback);

#endif
