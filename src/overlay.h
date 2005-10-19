#ifndef _OVERLAY_H_
#define _OVERLAY_H_

void overlay_show_window ();
void overlay_remove_all ();
gboolean overlay_add_data (DataVector *overlaydata);
gboolean overlay_file (gchar *filename);
GSList* overlay_get_filenames ();
gboolean overlay_get_color (GdkColor *color, gboolean selected, guint uid, GtkTreeIter *iter);

#endif /* _OVERLAY_H_ */
