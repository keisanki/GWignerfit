#ifndef _PREFERENCES_H_
#define _PREFERENCES_H_

#include "structs.h"

void prefs_set_default ();

void prefs_change_win ();

void prefs_load (Preferences *prefs);

void prefs_save (Preferences *prefs);

#endif
