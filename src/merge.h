#ifndef _MERGE_H_
#define _MERGE_H_

#include "structs.h"

/* Columns in merge_treeview */
enum {
	MERGE_ID_COL = 0,
	MERGE_LIST_COL,
	MERGE_DATAFILE_COL,
	MERGE_N_COLUMNS
};

/* Each resonance is denoted by such a node */
typedef struct
{
	guint id;	/* The id of the resonance list set */
	guint num;	/* The number of the resonance in the list */
	guint guid1;	/* The uid of the first GtkSpectVis graph link */
	guint guid2;	/* The uid of the second GtkSpectVis graph link */
	GList *link;	/* The position in the links group this node belongs to or NULL */
	Resonance *res;	/* The properties of the resonance */
} MergeNode;

void merge_open_win ();

void merge_purge ();

#endif

