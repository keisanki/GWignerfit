#ifndef _NETWORK_H_
#define _NETWORK_H_

enum {
	VNA_CAPA_MINFRQ, VNA_CAPA_MAXFRQ, 
	VNA_CAPA_MAXPOINTS, VNA_CAPA_VARPOINTS,
	VNA_CAPA_MINBW, VNA_CAPA_MAXBW
};

void network_open_win ();

void vna_thread_exit (gchar *format, ...);

void vna_update_netstat (gchar *format, ...);

void vna_ms_sleep (glong ms);

#endif
