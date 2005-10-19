#ifndef _EXPORT_H_
#define _EXPORT_H_

void export_resonance_data ();
gboolean export_theory_graph_data (char *filename);
gboolean export_select_filename (GtkButton *button, gpointer user_data);
gboolean export_graph_ps ();

#endif /* _EXPORT_H_ */
