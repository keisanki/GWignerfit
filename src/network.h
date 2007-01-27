#ifndef _NETWORK_H_
#define _NETWORK_H_

#define VNA_PORT    8510	/* Port of Ieee488Proxy */
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

void network_open_win ();

int vna_connect (const gchar *host);

int vna_sendall (int s, char *buf, int len);

int vna_send_cmd (int fd, char *msg, int errmask);

void vna_enter (int sockfd, char *buf, int len, int addr, int errmask);

void vna_spoll_wait (int sockfd, int status);

ComplexDouble *vna_recv_data (int sockfd, int points);

int vna_receiveall (int s, char *buf, int len);

#endif
