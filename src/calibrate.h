#ifndef _CALIBRATE_H_
#define _CALIBRATE_H_

void cal_open_win ();

void cal_update_progress (gfloat fraction);

gboolean cal_show_time_estimates ();

void cal_update_time_estimates (int *windone, int *winleft);

#endif
