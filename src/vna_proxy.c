#include <string.h>
#include <math.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "vna_proxy.h"
#include "network.h"

#define DV(x)  			/* For debuggin set DV(x) x */

extern GlobalData *glob;	/* Global variables */

/* Receive from socket s len bytes into buf. 
 * You have to make sure, that buf can hold len bytes! 
 * Breaks with timeout error message if failok==0. */
int vna_proxy_receiveall_full (int s, char *buf, int len, int failok)
{
	int total = 0;       /* how many bytes we've received */
	int bytesleft = len; /* how many we have left to receive */
	int n = 0;           /* number of bytes received by recv */
	struct timeval tv;
	fd_set fdset;
	int i;

	FD_ZERO (&fdset);
	FD_SET (s, &fdset);

	while (total < len)
	{
		/* Wait VNA_RECV_TOUT seconds for data */
		for (i=0; i<VNA_RECV_TOUT; i++)
		{
			tv.tv_sec  = 1;
			tv.tv_usec = 0;
			select (s+1, &fdset, NULL, NULL, &tv);
			
			if (FD_ISSET (s, &fdset))
				break;

			if (! (glob->flag & (FLAG_VNA_MEAS | FLAG_VNA_CAL)) )
			{
				if (glob->netwin && glob->netwin->sockfd)
				{
					if (glob->netwin->type == 1)
						vna_proxy_send_cmd (glob->netwin->sockfd, 
							"MTA LISTEN "VNA_GBIP" DATA 'RAMP;CONT;'", 0);
					vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" GTL", 0);
				}
				vna_thread_exit (NULL);
			}
		}

		if (! FD_ISSET (s, &fdset))
		{
			/* Timeout */
			if (!failok)
			{
				/* Fail with error message and VNA initialization */
				if (glob->netwin && glob->netwin->sockfd)
				{
					if (glob->netwin->type == 1)
						vna_proxy_send_cmd (glob->netwin->sockfd, 
							"MTA LISTEN "VNA_GBIP" DATA 'RAMP;CONT;'", 0);
					vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" GTL", 0);
				}
				DV(if (len < 128)
					printf ("received (%d bytes): %.*s", len, len, buf);
				else
					printf ("received (%d bytes): [data not shown]\n", len);)
				vna_thread_exit ("Connection to proxy host timed out "
						"(received only %i of %i expected bytes).", total, len);
			}
			else
			{
				/* Fail silently */
				DV(printf ("*** Ignored timeout in vna_proxy_receiveall_full().\n");)
				return -1;
			}
		}
		
		/* Receive data */
		n = recv (s, buf+total, bytesleft, 0);
		if (n == -1) 
			vna_thread_exit ("recv: %s", g_strerror (errno));

		total += n;
		bytesleft -= n;
	}

	/*len = total;*/ /* return number actually received here */
	DV(if (len < 128)
		printf ("received (%d bytes): %.*s", len, len, buf);
	else
		printf ("received (%d bytes): [data not shown]\n", len);)

	return n==-1?-1:0; /* return -1 on failure, 0 on success */
}

/* Convenience wrapper for vna_proxy_receiveall_full() _with_ timeout */
int vna_proxy_receiveall (int s, char *buf, int len)
{
	return vna_proxy_receiveall_full (s, buf, len, 0);
}

/* Send a whole buffer to the proxy */
int vna_proxy_sendall (int s, char *buf, int len)
{
	int total = 0;       /* how many bytes we've sent */
	int bytesleft = len; /* how many we have left to send */
	int n = 0;

	DV(if (len < 128)
		printf("transmit (%d bytes): %.*s\n", len, len, buf);
	else
		printf("transmit (%d bytes): [data not shown]\n", len);)

	while (total < len)
	{
		n = send (s, buf+total, bytesleft, 0);
		if (n == -1) 
			vna_thread_exit ("Connection to proxy host lost.");
		total += n;
		bytesleft -= n;
	}

	/*len = total;*/ /* return number actually sent here */

	return n==-1?-1:0; /* return -1 on failure, 0 on success */
}

/* Send (must be \0 terminated!) msg to the proxy and return the status message. */
int vna_proxy_send_cmd (int fd, char *msg, int errmask)
{
	char reply[VNA_STAT_LEN+1];
	int status = -1;
	int recv_tries = 0;
	
	/* strlen (msg)+1: Transmit tailing \0, too. */
	vna_proxy_sendall (fd, msg, strlen (msg)+1);

	/* Get Proxy status reply */
	while ((vna_proxy_receiveall_full (fd, reply, VNA_STAT_LEN, 1) == -1) &&
	       (recv_tries < 5))
		recv_tries++;
	if ((recv_tries == 5) && (errmask > 0))
		vna_thread_exit ("Connection to proxy host timed out (did not receive status reply).");

	reply[VNA_STAT_LEN] = '\0';

	if ((sscanf (reply, "* PROXYMSG: Status %d", &status) != 1) && (errmask > 0))
		vna_thread_exit ("Could not parse proxy reply: %s", reply);
	
	if ((errmask > 0) && (status & errmask))
	{
		if ((status & VNA_ETIMEOUT) && (errmask & VNA_ETIMEOUT))
			vna_thread_exit ("Connection between proxy and network analyzer timed out.");
		if ((status & VNA_ESYNTAXE) && (errmask & VNA_ESYNTAXE))
			vna_thread_exit ("Syntax error in network analyzer command.");

		vna_thread_exit ("Network analyzer sent status: %d", status);
	}

	return status;
}

/* Connect to host and return the file descriptor */
int vna_proxy_connect (const gchar *host)
{
	int sockfd, errsv, res, valopt;
	struct hostent *he;
	struct sockaddr_in proxy_addr;
	struct in_addr addr;
	long arg;
	fd_set myset;
	struct timeval tv;
	socklen_t lon;
	char buf[VNA_GREET_LEN+1];

	g_return_val_if_fail (glob->netwin, -1);
	g_return_val_if_fail (host, -1);

	vna_update_netstat ("Connecting to proxy host...");
	
	/* Convert host into addr */
	if (inet_aton (host, &addr) == 0)
	{
		/* host is _not_ an IP */
		if ((he = gethostbyname (host)) == NULL)
		{
			errsv = errno;
			if (errsv == 0)
				/* Could not resolve hostname */
				vna_thread_exit ("Could not resolve hostname, measurement cancelled.");

			/* Unknown error */
			vna_thread_exit ("gethostbyname: %s", g_strerror (errsv));
		}

		addr = *((struct in_addr *)he->h_addr);
	}

	/* Open a socket */
	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
		vna_thread_exit ("socket: %s", g_strerror (errno));

	/* I know, I return the socket, but this way vna_thread_exit can
	 * close the socked by itself if I quit somewhere in this function. */
	if (glob->netwin && (glob->flag & FLAG_VNA_MEAS))
		glob->netwin->sockfd = sockfd;
	else if (glob->calwin && (glob->flag & FLAG_VNA_CAL))
		glob->calwin->sockfd = sockfd;

	/* Set connections details */
	proxy_addr.sin_family = AF_INET;
	proxy_addr.sin_port = htons (VNA_PROXY_PORT);
	proxy_addr.sin_addr = addr;
	memset (&(proxy_addr.sin_zero), '\0', 8);

	/* Set non-blocking */
	arg = fcntl (sockfd, F_GETFL, NULL); 
	arg |= O_NONBLOCK; 
	fcntl (sockfd, F_SETFL, arg); 

	/* Trying to connect with timeout */
	res = connect (sockfd, (struct sockaddr *)&proxy_addr, sizeof (struct sockaddr)); 

	if (res < 0) 
	{ 
		 if (errno == EINPROGRESS)
		 { 
				tv.tv_sec  = VNA_CONN_TOUT; 
				tv.tv_usec = 0; 
				FD_ZERO (&myset); 
				FD_SET (sockfd, &myset); 
				if (select (sockfd+1, NULL, &myset, NULL, &tv) > 0) { 
					 lon = sizeof (int); 
					 getsockopt (sockfd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon); 
					 if ((valopt) && (valopt == ECONNREFUSED))
						vna_thread_exit ("Connection to proy host refused. "
								"Make sure that Ieee488Proxy is really running.");
					 else if (valopt != 0)
					 	vna_thread_exit( "connect: %s", g_strerror (valopt));
				} 
				else
					 vna_thread_exit ("Timeout while connecting to proxy host.");
		 } 
		 else
				vna_thread_exit ("Error connecting: %s", g_strerror (errno)); 
	} 
	
	/* Set to blocking mode again... */
	arg = fcntl (sockfd, F_GETFL, NULL); 
	arg &= (~O_NONBLOCK); 
	fcntl (sockfd, F_SETFL, arg); 

	/* Get Proxy greetings */
	vna_proxy_receiveall (sockfd, buf, VNA_GREET_LEN);
	if (!strncmp (buf, "* PROXYMSG: OK           \r\n", VNA_GREET_LEN))
	{
		glob->netwin->sockfd = (gint) sockfd;
		vna_update_netstat ("Connection established.");

		/* Give the Ieee488 card GPIB 21 */
		if (vna_proxy_send_cmd (sockfd, "* PROXYCMD: initialize 21", VNA_ETIMEOUT))
			vna_thread_exit ("Could not initialize Ieee488 Card.");

		return glob->netwin->sockfd;
	}
	else if (!strncmp (buf, "* PROXYMSG: Busy         \r\n", VNA_GREET_LEN))
		vna_thread_exit ("Network analyzer is busy, try again later.");
	else if (!strncmp (buf, "* PROXYMSG: Access denied\r\n", VNA_GREET_LEN))
		vna_thread_exit ("Access to Ieee488Proxy is denied. Check proxy settings.");
	else
	{
		/* What the heck? */
		vna_thread_exit ("%s", buf);
	}

	/* We will never reach this point */
	return -1;
}

/* Execute an enter command */
void vna_proxy_enter (int sockfd, char *buf, int len, int addr, int errmask)
{
	char *cmd;
	
	cmd = g_strdup_printf ("* PROXYCMD: enter %i %i", len, addr);
	vna_proxy_send_cmd (sockfd, cmd, errmask);
	g_free (cmd);

	if (vna_proxy_receiveall (sockfd, buf, len))
		vna_thread_exit ("Failed to receive %d bytes for enter().", len);
}

/* Wait for network to report the given status bitmask */
void vna_proxy_spoll_wait (int sockfd, int status)
{
	char buf[31];
	int statbyte = 0, counter = 0;

	while (!(statbyte & status) && (counter < 1200))
	{
		buf[0] = '\0';
		vna_proxy_send_cmd (sockfd, "* PROXYCMD: spoll "VNA_GBIP, VNA_ETIMEOUT);
		if (vna_proxy_receiveall (sockfd, buf, 31))
			vna_thread_exit ("Failed to receive 31 bytes for spoll().");
		if ((sscanf (buf, "* PROXYMSG: spoll result %d", &statbyte) != 1))
			vna_thread_exit ("Could not parse spoll proxy reply: %s", buf);
		usleep (5e5);
		counter++;
	}

	if (counter == 1200)
		vna_thread_exit ("Measurement window did not finish within 5 minutes, something must have gone wrong.");
}

/* Retrieve the measurement points */
ComplexDouble *vna_proxy_recv_data (int points)
{
	ComplexDouble *data;
	char read[VNA_RARR_LEN+1];
	char buf[6408], *cmd;
	int sockfd, nread, i;

	g_return_val_if_fail (glob->netwin || glob->calwin, NULL);
	g_return_val_if_fail (glob->netwin->sockfd > 0, NULL);
	sockfd = glob->netwin->sockfd;

	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FORM5;OUTPDATA;'", VNA_ETIMEOUT);
	vna_proxy_send_cmd (sockfd, "MLA TALK "VNA_GBIP, VNA_ETIMEOUT);

	/* Read the header */
	vna_proxy_send_cmd (sockfd, "* PROXYCMD: rarray 4", VNA_ETIMEOUT | VNA_ENOLISTE);
	vna_proxy_receiveall (sockfd, read, VNA_RARR_LEN);
	read[VNA_RARR_LEN] = '\0';
	sscanf (read, "* PROXYMSG: Bytes read %d", &nread);
	g_return_val_if_fail (nread == 4, NULL);
	vna_proxy_receiveall (sockfd, buf, 4);
	/* Header is 0x23 0x41 0x08 0x19, 0x08 0x19 is 801*8 bytes */

	/* Read the data (e.g. 801*4*2 bytes = 6408 bytes) */
	cmd = g_strdup_printf ("* PROXYCMD: rarray %i", points*8);
	vna_proxy_send_cmd (sockfd, cmd, VNA_ETIMEOUT);
	g_free (cmd);
	vna_proxy_receiveall (sockfd, read, VNA_RARR_LEN);
	read[VNA_RARR_LEN] = '\0';
	sscanf (read, "* PROXYMSG: Bytes read %d", &nread);
	g_return_val_if_fail (nread == points*8, NULL);
	vna_proxy_receiveall (sockfd, buf, points*8);

	data = g_new (ComplexDouble, points);
	for (i=0; i<points; i++)
	{
		data[i].re = (gdouble) *((gfloat *) &buf[8*i+0]);
		data[i].im = (gdouble) *((gfloat *) &buf[8*i+4]);
		data[i].abs = sqrt(data[i].re*data[i].re + data[i].im*data[i].im);
	}

	return data;
}

/* Get the network back into a reasonable mode and do not forget the GTL */
void vna_proxy_gtl ()
{
	if (glob->netwin->sockfd)
	{
		if (glob->netwin->type == 1)
			vna_proxy_send_cmd (glob->netwin->sockfd, 
				"MTA LISTEN "VNA_GBIP" DATA 'RAMP;CONT;'", 0);
		vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" DATA 'ENTO;'", 0);
		vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" GTL", 0);
	}
}

/* Send Local Lock Out to VNA */
void vna_proxy_llo ()
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd);

	vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" LLO", VNA_ETIMEOUT);
}

/* Calculate the ms to wait for a window to be measured for an HP 8510C */
glong vna_proxy_sweep_cal_sleep ()
{
	glong delta;
	
	if (glob->netwin->swpmode == 1)
		delta = 380 * glob->netwin->avg + 700;
	else
	{
		switch (glob->netwin->avg)
		{
			case 1: case 2: case 4: case 8: case 16:
				delta = 40000;
			case 32: case 64:
				delta = 45000;
			case 128:
				delta = 65000;
			case 256:
				delta = 75000;
			default:
				/* Should not be reached */
				delta = 50000;
		}

		/* Prevent numerous timeouts */
		delta += 2000;
	}

	if (glob->netwin->numparam == 4)
		delta *= (glob->netwin->swpmode == 1) ? 5 : 1.5;

	if (glob->netwin->numparam == 6)
		delta *= (glob->netwin->swpmode == 1) ? 7 : 2.0;

	return delta;
}

/* Retrieve the current start frequency from the VNA */
gdouble vna_proxy_get_start_frq ()
{
	char enterbuf[30];
	gdouble start;

	g_return_val_if_fail (glob->netwin, 0.0);
	g_return_val_if_fail (glob->netwin->sockfd, 0.0);

	vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FORM5; STAR; OUTPACTI;'", VNA_ETIMEOUT);
	vna_proxy_enter (glob->netwin->sockfd, enterbuf, 30, VNA_GBIP_INT, VNA_ETIMEOUT); 
	sscanf (enterbuf, "%lf", &start);

	return start;
}

/* Retrieve the current stop frequency from the VNA */
gdouble vna_proxy_get_stop_frq ()
{
	char enterbuf[30];
	gdouble stop;

	g_return_val_if_fail (glob->netwin, 0.0);
	g_return_val_if_fail (glob->netwin->sockfd, 0.0);

	vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FORM5; STOP; OUTPACTI;'", VNA_ETIMEOUT);
	vna_proxy_enter (glob->netwin->sockfd, enterbuf, 30, VNA_GBIP_INT, VNA_ETIMEOUT); 
	sscanf (enterbuf, "%lf", &stop);

	return stop;
}

/* Retrieve the current number of points from the VNA */
gint vna_proxy_get_points ()
{
	char enterbuf[30];
	float f_points;

	g_return_val_if_fail (glob->netwin, 0);
	g_return_val_if_fail (glob->netwin->sockfd, 0);

	vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FORM5; POIN; OUTPACTI;'", VNA_ETIMEOUT);
	vna_proxy_enter (glob->netwin->sockfd, enterbuf, 30, VNA_GBIP_INT, VNA_ETIMEOUT); 
	sscanf (enterbuf, "%f", &f_points);

	return (gint) f_points;
}

/* Prepare a measurement in sweep mode */
void vna_proxy_sweep_prepare ()
{
	NetworkWin *netwin;
	char cmdstr[81];
	int sockfd;

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd);
	netwin = glob->netwin;
	sockfd = netwin->sockfd;

	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'PRES;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_ms_sleep (5000);
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'POIN801;'", VNA_ETIMEOUT|VNA_ESYNTAXE);

	vna_proxy_llo (sockfd);
	if (netwin->numparam > 1)
		/* Prepare display for full S-matrix measurement */
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'FOUPSPLI;WAIT;'");
	else
		/* Select correct S parameter for single element measurement */
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA '%s;'", netwin->param);
	vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_ms_sleep (2000);

	if (netwin->swpmode == 1)
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'RAMP;'");
	else
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'STEP;'");
	vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);

	g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'AVERON %d;'", netwin->avg);
	vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
}

/* Set the VNA start and stop frequency (in Hz) */
void vna_proxy_set_startstop (gdouble start, gdouble stop)
{
	char cmdstr[81];

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);
	g_return_if_fail (stop > start);

	g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'STAR %.1lf HZ;STOP %.1lf HZ;'", 
			start, stop);
	vna_proxy_send_cmd (glob->netwin->sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
}

/* Scale the trace on the VNA automatically */
void vna_proxy_trace_scale_auto (gchar *sparam)
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" DATA 'AUTO;'", VNA_ETIMEOUT);
}

/* Display four separate traces on the VNA */
void vna_proxy_trace_fourparam ()
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	vna_proxy_send_cmd (glob->netwin->sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FOUPSPLI;WAIT;'", VNA_ETIMEOUT);
}

/* Set the number of groups */
void vna_proxy_set_numg (gint numg)
{
	char cmdstr[81];

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);
	g_return_if_fail (numg > 0);

	/* Only one sweep necessary in step mode */
	if (glob->netwin->swpmode == 2)
		numg = 1;

	g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'NUMG %d;'", numg);
	vna_proxy_send_cmd (glob->netwin->sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
}

/* Wait for the VNA to finish the current operation */
void vna_proxy_wait ()
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	vna_proxy_send_cmd (glob->netwin->sockfd, "DCL", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_proxy_spoll_wait (glob->netwin->sockfd, 16);
}

/* Select/activate given S-parameter */
void vna_proxy_select_s (gchar *sparam)
{
	char cmdstr[81];

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);
	g_return_if_fail (strlen (sparam) == 3);

	g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA '%s;'", sparam);
	vna_proxy_send_cmd (glob->netwin->sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
}

/* Select the first available parameter */
gboolean vna_proxy_sel_first_par ()
{
	return TRUE;
}

/* Prepare VNA for special TRL measurements */
void vna_proxy_select_trl (gint Si)
{
	int sockfd;

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);
	sockfd = glob->netwin->sockfd;

	/* Si selects the TRL parameter to measure
	 * Si = 4 : Prepare VNA for TRL measurement and select first parameter
	 * Si = 5 : Select second parameter
	 */

	if (Si == 4)
	{
		/* Prepare TRL measurement */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SPLI;WAIT;'", VNA_ETIMEOUT);

		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CHAN1;USER3;'", VNA_ETIMEOUT);
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA "
			"'DRIVPORT1;LOCKA1;NUMEA2;DENOA1;CONVS;REDD;'", VNA_ETIMEOUT);
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CHAN2;USER4;'", VNA_ETIMEOUT);
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA "
			"'DRIVPORT2;LOCKA2;NUMEA2;DENOA1;CONVS;REDD;'", VNA_ETIMEOUT);
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CHAN1;'", VNA_ETIMEOUT);
		vna_proxy_set_numg (glob->netwin->swpmode==1 ? glob->netwin->avg+1 : 1);

		/* and wait */
		vna_proxy_wait ();
	}
	if (Si == 5)
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CHAN2;'", VNA_ETIMEOUT);
}

/* Round IF bandwidth value to nearest possible value */
gdouble vna_proxy_round_bwid (gdouble bwid_in)
{
	return bwid_in;
}

/* Return some VNA capabilities */
gdouble vna_proxy_get_capa (gint type)
{
	gdouble ret = -1;

	switch (type)
	{
		case 1: /* Minimal frequency */
			ret =  0.045;
			break;
		case 2: /* Maximal frequency */
			ret = 50.000;
			break;
		case 3: /* Number of points */
			ret = 801.0;
			break;
		case 4: /* Minimal IF bandwidth */
			ret = -1;
			break;
		case 5: /* Maximal IF bandwidth */
			ret = -1;
			break;
	}

	return ret;
}
