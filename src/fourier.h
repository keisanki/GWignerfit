#ifndef _FOURIER_H_
#define _FOURIER_H_

#include <gtk/gtk.h>

void fourier_open_win ();

void fourier_update_main_graphs ();

void fourier_update_overlay_graphs (gint uid, gboolean redraw);

void fourier_difference_changed (gboolean activate, gint olddataindex, gint oldtheoindex);

void fourier_set_color (gint uid, GdkColor color);

void on_fft_close_activate (GtkMenuItem *menuitem, gpointer user_data);

DataVector* fourier_gen_dataset (DataVector *source, gdouble startfrq, gdouble endfrq);

DataVector* fourier_inverse_transform (DataVector *d, gdouble fmin, gdouble fmax);

#endif
