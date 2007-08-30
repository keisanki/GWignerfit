#ifndef _NETWORK_H_
#define _NETWORK_H_

void network_open_win ();

void vna_thread_exit (gchar *format, ...);

void vna_update_netstat (gchar *format, ...);

void vna_ms_sleep (glong ms);

#endif
