#include <glade/glade.h>
#include "structs.h"

#ifndef _VNA_N5230A_H_
#define _VNA_N5230A_H_

#define VNA_N5230A_PORT 5025	/* Port of Ieee488Proxy */
#define VNA_N5230A_CONN_TOUT  5	/* Timeout (in sec) for connect */
#define VNA_N5230A_RECV_TOUT 30	/* Timeout (in sec) for recv */

int vna_n5230a_receiveall_full (int s, char *buf, int len, int failok);
int vna_n5230a_receiveall (int s, char *buf, int len);
int vna_n5230a_sendall (int s, char *buf, int len);
int vna_n5230a_send_cmd (int fd, char *format, ...);

int vna_n5230a_connect (const gchar *host);
ComplexDouble *vna_n5230a_recv_data (int points);
void vna_n5230a_gtl ();
void vna_n5230a_llo ();
glong vna_n5230a_sweep_cal_sleep ();
gdouble vna_n5230a_get_start_frq ();
gdouble vna_n5230a_get_stop_frq ();
gint vna_n5230a_get_points ();
void vna_n5230a_sweep_prepare ();
void vna_n5230a_set_startstop (gdouble start, gdouble stop);
void vna_n5230a_trace_scale_auto (gchar *sparam);
void vna_n5230a_trace_fourparam ();
void vna_n5230a_set_numg (gint numg);
void vna_n5230a_wait ();
void vna_n5230a_select_s (gchar *sparam);
void vna_n5230a_select_trl (gint Si);
gchar* vna_n5230a_calibrate (gdouble fstart, gdouble fstop, gdouble resol, gint num);
gchar* vna_n5230a_cal_recall (gdouble fstart, gdouble fstop, gdouble resol, gint num);
gdouble vna_n5230a_round_bwid (gdouble bwid_in);
gdouble vna_n5230a_get_capa (gint type);

#endif
