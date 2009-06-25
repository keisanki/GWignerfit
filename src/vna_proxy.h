#include <glade/glade.h>
#include "structs.h"

#ifndef _VNA_PROXY_H_
#define _VNA_PROXY_H_

#define VNA_PROXY_PORT 8510	/* Port of Ieee488Proxy */
#define VNA_GBIP     "16"	/* GBIP address of network analyzer as string */
#define VNA_GBIP_INT  16 	/* GBIP address of network analyzer as integer */
#define VNA_GREET_LEN 28	/* Length of greeting message */
#define VNA_STAT_LEN  24	/* Length of status message */
#define VNA_RARR_LEN  33	/* Length of rarray 'Bytes read' reply */
#define VNA_CONN_TOUT  5	/* Timeout (in sec) for connect */
#define VNA_RECV_TOUT  3	/* Timeout (in sec) for recv */
#define VNA_ESUCCESS   0	/* Proxy status: Success */
#define VNA_ESYNTAXE   1	/* Proxy status: Illegal Syntax command */
#define VNA_ENOLISTE   2	/* Proxy status: Receive when PC was not a listener */
#define VNA_EQUOTEDS   4	/* Proxy status: A quoted string in LISTEN or TALK */
#define VNA_ETIMEOUT   8	/* Proxy status: Timeout */
#define VNA_EUNKNOWN  16	/* Proxy status: Unknown command */
#define VNA_ESUCCEOI  32	/* Proxy status: Seccess, transfer ended with EOI */

int vna_proxy_receiveall_full (int s, char *buf, int len, int failok);
int vna_proxy_receiveall (int s, char *buf, int len);
int vna_proxy_sendall (int s, char *buf, int len);
int vna_proxy_send_cmd (int fd, char *msg, int errmask);
void vna_proxy_enter (int sockfd, char *buf, int len, int addr, int errmask);
void vna_proxy_spoll_wait (int sockfd, int status);

int vna_proxy_connect (const gchar *host);
ComplexDouble *vna_proxy_recv_data (int points);
ComplexDouble **vna_proxy_recv_s2p_data (int points);
void vna_proxy_gtl ();
void vna_proxy_llo ();
glong vna_proxy_sweep_cal_sleep ();
gdouble vna_proxy_get_start_frq ();
gdouble vna_proxy_get_stop_frq ();
gint vna_proxy_get_points ();
void vna_proxy_sweep_prepare ();
void vna_proxy_set_startstop (gdouble start, gdouble stop);
void vna_proxy_trace_scale_auto (gchar *sparam);
void vna_proxy_trace_fourparam ();
void vna_proxy_set_numg (gint numg);
void vna_proxy_wait ();
void vna_proxy_select_s (gchar *sparam);
gboolean vna_proxy_sel_first_par ();
void vna_proxy_select_trl (gint Si);
gdouble vna_proxy_round_bwid (gdouble bwid_in);
gdouble vna_proxy_get_capa (gint type);

#endif
