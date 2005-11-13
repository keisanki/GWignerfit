#ifndef _FCOMP_H_
#define _FCOMP_H_

#include "structs.h"

void fcomp_open_win ();

void fcomp_add_component (FourierComponent *fcomp, gint id);

void fcomp_update_list ();

void fcomp_update_graph ();

void fcomp_what_to_fit (gint *ia);

void fcomp_purge ();

#endif

