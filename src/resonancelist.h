#include "structs.h"

#ifndef _RESONANCELIST_H_
#define _RESONANCELIST_H_

enum {
	ID_COL, FRQ_COL, WID_COL, AMP_COL, PHAS_COL, N_COLUMNS
};

void clear_resonancelist ();
	
void set_up_resonancelist ();

void add_resonance_to_list (Resonance *res);

void show_global_parameters (GlobalParam *gparam);

void update_resonance_list (GPtrArray *param);

gint* get_selected_resonance_ids (gboolean failok);

gint get_selected_resonance_iters (GtkTreeIter **iters);

gint get_resonance_id_by_cursur ();

GtkTreeIter *get_res_iter_by_id (gint wanted_id);

gboolean select_res_by_id (gint id);

void remove_selected_resonance ();

void uncheck_res_out_of_frq_win (gdouble min, gdouble max);

void what_to_fit (gint *ia);

void reslist_update_widthunit ();

void resonance_check_all (gboolean type);

void resonance_toggle_row ();

gboolean import_resonance_list (gchar *filename);

#endif
