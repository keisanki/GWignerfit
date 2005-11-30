#ifndef _MERGE_UTIL_H_
#define _MERGE_UTIL_H_

void merge_zoom_x_all ();

GList* merge_delete_link_node (MergeNode *node, gint type);

gint merge_link_compare (gconstpointer a_in, gconstpointer b_in);

void merge_draw_remove_all ();

gboolean merge_draw_link (GList *link);

void merge_undisplay_node_selection ();

gboolean merge_is_id_in_list (guint id, GList *list);

GArray* merge_gather_reslist ();

void merge_highlight_width (MergeNode *node);

MergeNode* merge_get_nearnode (gint xpos, gint ypos, gint *xpix, gint *ypix);

void merge_link_two_nodes (MergeNode *node1, MergeNode *node2);

gboolean merge_delres (MergeNode *node);

void merge_add_reslist (GPtrArray *reslist, gchar *datafilename, gchar *name);

void merge_statusbar_message (gchar *format, ...);

#endif
