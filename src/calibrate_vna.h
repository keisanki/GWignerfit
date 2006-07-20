#ifndef _CALIBRATE_VNA_H_
#define _CALIBRATE_VNA_H_

DataVector* cal_vna_calibrate (DataVector *in, DataVector *opn, DataVector *shrt, DataVector *load, gchar *host);

void cal_vna_exit (gchar *format, ...);

gboolean cal_vna_set_netstat (gpointer data);

#endif