#ifndef _EXPORT_H_
#define _EXPORT_H_

void export_resonance_data ();
gboolean export_theory_graph_data (char *filename);
gboolean export_select_filename (GtkButton *button, gpointer user_data);
gboolean export_graph_ps ();

gboolean postscript_export_dialog (
		const gchar *default_filename,
		const gchar *default_title,
		const gchar *default_footer,
		const gboolean include_theory,
		const gboolean include_overlay,
		gchar **selected_filename,
		gchar **selected_title,
		gchar **selected_footer,
		gboolean *selected_theory,
		gboolean *selected_overlay,
		gchar *selected_legend
		);

#endif /* _EXPORT_H_ */
