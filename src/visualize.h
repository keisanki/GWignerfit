#include "gtkspectvis.h"

#ifndef _VISUALIZE_H_
#define _VISUALIZE_H_

GtkWidget *NewGtkSpectvis (gchar *widget_name, gchar *string1, gchar *string2, gint int1, gint int2);

void visualize_newgraph ();
void visualize_draw_data ();
void visualize_theory_graph (gchar *type);
void visualize_difference_graph ();
gint visualize_handle_signal_marked (GtkSpectVis *spectvis, gdouble *xval, gdouble *yval);
void visualize_update_res_bar (gboolean redraw);
void visualize_update_min_max (gboolean redraw);
void visualize_restore_cursor ();
void visualize_remove_difference_graph ();
void visualize_handle_viewport_changed (GtkSpectVis *spectvis, gchar *zoomtype);
void visualize_background_calc (GtkSpectVisData *data);
gboolean visualize_stop_background_calc ();
void visualize_zoom_to_frequency_range (gdouble min, gdouble max);
void visualize_calculate_difference (ComplexDouble *val, ComplexDouble *diff, guint pos);

#endif
