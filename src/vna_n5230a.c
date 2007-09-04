#include <string.h>
#include <math.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "vna_n5230a.h"
#include "network.h"

#define DV(x) x			/* For debuggin set DV(x) x */

extern GlobalData *glob;	/* Global variables */

/* Receive from socket s len bytes into buf. 
 * You have to make sure, that buf can hold len bytes! 
 * Breaks with timeout error message if failok==0. */
int vna_n5230a_receiveall_full (int s, char *buf, int len, int failok)
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
		/* Wait VNA_N5230A_RECV_TOUT seconds for data */
		for (i=0; i<VNA_N5230A_RECV_TOUT; i++)
		{
			tv.tv_sec  = 1;
			tv.tv_usec = 0;
			select (s+1, &fdset, NULL, NULL, &tv);
			
			if (FD_ISSET (s, &fdset))
				break;

			if (! (glob->flag & (FLAG_VNA_MEAS | FLAG_VNA_CAL)) )
			{
				if (glob->netwin && glob->netwin->sockfd)
					vna_n5230a_gtl ();
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
					vna_n5230a_gtl ();

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
				DV(printf ("*** Ignored timeout in vna_n5230a_receiveall_full().\n");)
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

	return n==-1?-1:0; /* return -1 on failure, 0 on success */
}

/* Convenience wrapper for vna_n5230a_receiveall_full() _with_ timeout */
int vna_n5230a_receiveall (int s, char *buf, int len)
{
	return vna_n5230a_receiveall_full (s, buf, len, 0);
}

int vna_n5230a_get_reply (int s, char *buf, int maxlen)
{
	int n = 0, flag = 0;

	while ((n < maxlen) && (flag == 0))
	{
		vna_n5230a_receiveall (s, buf + n, 1);
		if (buf[n] == '\n')
			flag = 1;
		else
			n++;
	}
	if (n < maxlen)
		buf[n] = '\0';

	DV(if (n < 128)
		printf ("received (%d bytes): %.*s\n", n, n, buf);
	else
		printf ("received (%d bytes): [data not shown]\n", n);)

	return n;
}

/* Send a whole buffer to the proxy */
int vna_n5230a_sendall (int s, char *buf, int len)
{
	int total = 0;       /* how many bytes we've sent */
	int bytesleft = len; /* how many we have left to send */
	int n = 0;

	DV(if (len < 128)
		printf("transmit (%d bytes): %.*s", len, len, buf);
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
int vna_n5230a_send_cmd (int fd, char *msg)
{
	gchar *sndmsg;

	g_return_val_if_fail (msg, -1);

	sndmsg = g_strdup_printf ("%s\n", msg);
	vna_n5230a_sendall (fd, sndmsg, strlen (sndmsg));
	g_free (sndmsg);

	return 0;
}

/* Connect to host and return the file descriptor */
int vna_n5230a_connect (const gchar *host)
{
	int sockfd, errsv, res, valopt;
	struct hostent *he;
	struct sockaddr_in proxy_addr;
	struct in_addr addr;
	long arg;
	fd_set myset;
	struct timeval tv;
	socklen_t lon;

	g_return_val_if_fail (glob->netwin, -1);
	g_return_val_if_fail (host, -1);

	vna_update_netstat ("Connecting to PNA N-5230A...");
	
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
	proxy_addr.sin_port = htons (VNA_N5230A_PORT);
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
				tv.tv_sec  = VNA_N5230A_CONN_TOUT; 
				tv.tv_usec = 0; 
				FD_ZERO (&myset); 
				FD_SET (sockfd, &myset); 
				if (select (sockfd+1, NULL, &myset, NULL, &tv) > 0) { 
					 lon = sizeof (int); 
					 getsockopt (sockfd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon); 
					 if ((valopt) && (valopt == ECONNREFUSED))
						vna_thread_exit ("Connection to proy host refused. "
								"Make sure that socket port is enabled on VNA.");
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

	glob->netwin->sockfd = (gint) sockfd;
	vna_update_netstat ("Connection established.");

	return sockfd;
}

/* Retrieve the measurement points */
ComplexDouble *vna_n5230a_recv_data (int points)
{
	ComplexDouble *data;
	char buf[16001*8], tmp;
	int sockfd, i, numdigits;

	g_return_val_if_fail (glob->netwin || glob->calwin, NULL);
	g_return_val_if_fail (glob->netwin->sockfd > 0, NULL);
	sockfd = glob->netwin->sockfd;

	vna_n5230a_send_cmd (sockfd, "FORM:DATA REAL,32");
	vna_n5230a_send_cmd (sockfd, "CALC:DATA? SDATA");

	/* Read the header */
	vna_n5230a_receiveall (sockfd, buf, 1);
	vna_n5230a_receiveall (sockfd, buf, 1);
	numdigits = buf[0]-48;
	vna_n5230a_receiveall (sockfd, buf, numdigits);
	buf[numdigits] = '\0';
	sscanf (buf, "%d", &numdigits);

	g_return_val_if_fail (numdigits >= points*8, NULL);

	/* Read the data (e.g. 16001*8 bytes + \n = 128008+1 bytes) */
	vna_n5230a_receiveall (sockfd, buf, numdigits+1);

	data = g_new (ComplexDouble, points);
	for (i=0; i<2*points; i++)
	{
		tmp        = buf[4*i+0];
		buf[4*i+0] = buf[4*i+3];
		buf[4*i+3] = tmp;
		tmp        = buf[4*i+1];
		buf[4*i+1] = buf[4*i+2];
		buf[4*i+2] = tmp;
	}
	for (i=0; i<points; i++)
	{
		data[i].re = (gdouble) *((gfloat *) &buf[8*i+0]);
		data[i].im = (gdouble) *((gfloat *) &buf[8*i+4]);
		data[i].abs = sqrt(data[i].re*data[i].re + data[i].im*data[i].im);
	}

	return data;
}

/* Get the network back into a reasonable mode and do not forget the GTL */
void vna_n5230a_gtl ()
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	if (glob->netwin->type == 1)
		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:SWE:MODE CONT");

	//TODO: How to do a GTL
}

/* Send Local Lock Out to VNA */
void vna_n5230a_llo ()
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	//TODO: How to do a LLO
}

/* Calculate the ms to wait for a window to be measured for an HP 8510C */
glong vna_n5230a_sweep_cal_sleep ()
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
gdouble vna_n5230a_get_start_frq ()
{
	char enterbuf[30];
	gdouble start;

	g_return_val_if_fail (glob->netwin, 0.0);
	g_return_val_if_fail (glob->netwin->sockfd, 0.0);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:FREQ:STAR?");
	vna_n5230a_get_reply (glob->netwin->sockfd, enterbuf, 30);
	sscanf (enterbuf, "%lf", &start);

	return start;
}

/* Retrieve the current stop frequency from the VNA */
gdouble vna_n5230a_get_stop_frq ()
{
	char enterbuf[30];
	gdouble stop;

	g_return_val_if_fail (glob->netwin, 0.0);
	g_return_val_if_fail (glob->netwin->sockfd, 0.0);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:FREQ:STOP?");
	vna_n5230a_get_reply (glob->netwin->sockfd, enterbuf, 30);
	sscanf (enterbuf, "%lf", &stop);

	return stop;
}

/* Retrieve the current number of points from the VNA */
gint vna_n5230a_get_points ()
{
	char enterbuf[30];
	float f_points;

	g_return_val_if_fail (glob->netwin, 0);
	g_return_val_if_fail (glob->netwin->sockfd, 0);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:SWE:POIN?");
	vna_n5230a_get_reply (glob->netwin->sockfd, enterbuf, 30);
	sscanf (enterbuf, "%f", &f_points);

	return (gint) f_points;
}

/* Prepare a measurement in sweep mode */
void vna_n5230a_sweep_prepare ()
{
	NetworkWin *netwin;
	char cmdstr[81];
	int sockfd;

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd);
	netwin = glob->netwin;
	sockfd = netwin->sockfd;

	vna_n5230a_llo (sockfd);
	vna_n5230a_send_cmd (sockfd, "SYST:FPR");

	if (netwin->numparam > 1)
	{
		/* Prepare display for full S-matrix measurement */
		vna_n5230a_send_cmd (sockfd, "DISP:ARR STAC");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF:EXT 'gwf_S11','S11'");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF:EXT 'gwf_S12','S12'");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF:EXT 'gwf_S21','S21'");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF:EXT 'gwf_S22','S22'");
		vna_n5230a_send_cmd (sockfd, "SENS:SWE:POIN 16001");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND1:TRAC1:FEED 'gwf_S12'");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND1:TRAC2:FEED 'gwf_S21'");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND2:TRAC1:FEED 'gwf_S11'");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND2:TRAC2:FEED 'gwf_S22'");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND1:TRAC1:Y:SCALE:AUTO");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND1:TRAC2:Y:SCALE:AUTO");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND2:TRAC1:Y:SCALE:AUTO");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND2:TRAC2:Y:SCALE:AUTO");
	}
	else
	{
		/* Select correct S parameter for single element measurement */
		vna_n5230a_send_cmd (sockfd, "DISP:WIND ON");
		g_snprintf (cmdstr, 80, "CALC:PAR:DEF:EXT 'gwf_%s','%s'", netwin->param, netwin->param);
		vna_n5230a_send_cmd (sockfd, cmdstr);
		vna_n5230a_send_cmd (sockfd, "SENS:SWE:POIN 16001");
		g_snprintf (cmdstr, 80, "DISP:WIND:TRAC:FEED 'gwf_%s'", netwin->param);
		vna_n5230a_send_cmd (sockfd, cmdstr);
		vna_n5230a_send_cmd (sockfd, "DISP:WIND:TRAC:Y:SCALE:AUTO");
		g_snprintf (cmdstr, 80, "CALC:PAR:SEL 'gwf_%s'", netwin->param);
		vna_n5230a_send_cmd (sockfd, cmdstr);
	}

	if (netwin->bandwidth > 0)
	{
		g_snprintf (cmdstr, 80, "SENS:BWID %f", netwin->bandwidth);
		vna_n5230a_send_cmd (sockfd, cmdstr);
		vna_n5230a_wait ();
	}

	if (netwin->dwell > 0)
		g_snprintf (cmdstr, 80, "SENS:SWE:DWEL %f", netwin->dwell);
	else
		g_snprintf (cmdstr, 80, "SENS:SWE:DWEL 0");
	vna_n5230a_send_cmd (sockfd, cmdstr);
	vna_n5230a_wait ();

	if (netwin->swpmode == 1)
		g_snprintf (cmdstr, 80, "SENS:SWE:GEN ANAL");
	else
		g_snprintf (cmdstr, 80, "SENS:SWE:GEN STEP");
	vna_n5230a_send_cmd (sockfd, cmdstr);
	vna_n5230a_wait ();

	g_snprintf (cmdstr, 80, "SENS:AVER:COUN %d", netwin->avg);
	vna_n5230a_send_cmd (sockfd, cmdstr);
	vna_n5230a_send_cmd (sockfd, "SENS:AVER ON");
}

/* Set the VNA start and stop frequency (in Hz) */
void vna_n5230a_set_startstop (gdouble start, gdouble stop)
{
	char cmdstr[81];

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);
	g_return_if_fail (stop > start);

	g_snprintf (cmdstr, 80, "SENS:FREQ:STAR %.1lf", start);
	vna_n5230a_send_cmd (glob->netwin->sockfd, cmdstr);
	g_snprintf (cmdstr, 80, "SENS:FREQ:STOP %.1lf", stop);
	vna_n5230a_send_cmd (glob->netwin->sockfd, cmdstr);
}

/* Scale the trace on the VNA automatically */
void vna_n5230a_trace_scale_auto (gchar *sparam)
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	if ( (sparam) && (glob->netwin->numparam > 1) )
	{
		if (!strncmp (sparam, "S12", 3))
			vna_n5230a_send_cmd (glob->netwin->sockfd, "DISP:WIND1:TRAC1:Y:SCALE:AUTO");
		if (!strncmp (sparam, "S21", 3))
			vna_n5230a_send_cmd (glob->netwin->sockfd, "DISP:WIND1:TRAC2:Y:SCALE:AUTO");
		if (!strncmp (sparam, "S11", 3))
			vna_n5230a_send_cmd (glob->netwin->sockfd, "DISP:WIND2:TRAC1:Y:SCALE:AUTO");
		if (!strncmp (sparam, "S22", 3))
			vna_n5230a_send_cmd (glob->netwin->sockfd, "DISP:WIND2:TRAC2:Y:SCALE:AUTO");
	}
	else
		vna_n5230a_send_cmd (glob->netwin->sockfd, "DISP:WIND:TRAC:Y:SCALE:AUTO");
}

/* Display four separate traces on the VNA */
void vna_n5230a_trace_fourparam ()
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "DISP:ARR STAC");
	vna_n5230a_wait ();
}

/* Set the number of groups */
void vna_n5230a_set_numg (gint numg)
{
	char cmdstr[81];

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);
	g_return_if_fail (numg > 0);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:AVER:CLE");
	g_snprintf (cmdstr, 80, "SENS:SWE:GRO:COUN %d", numg);
	vna_n5230a_send_cmd (glob->netwin->sockfd, cmdstr);
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:SWE:MODE GRO");
}

/* Wait for the VNA to finish the current operation */
void vna_n5230a_wait ()
{
	char buf[8];
	int counter = 0;

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "*OPC?");

	while ((buf[0] != '+') && (counter < 1200))
	{
		buf[0] = '\0';
		vna_n5230a_receiveall_full (glob->netwin->sockfd, buf, 3, 1);
		usleep (5e5);
		counter++;
	}

	if (counter == 1200)
		vna_thread_exit ("Measurement window did not finish within 5 minutes, something must have gone wrong.");
}

/* Select/activate given S-parameter */
void vna_n5230a_select_s (gchar *sparam)
{
	char cmdstr[81];

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);
	g_return_if_fail (strlen (sparam) == 3);

	g_snprintf (cmdstr, 80, "CALC:PAR:SEL 'gwf_%s'", sparam);
	vna_n5230a_send_cmd (glob->netwin->sockfd, cmdstr);
}

/* Prepare VNA for special TRL measurements */
void vna_n5230a_select_trl (gint Si)
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

		/* and wait */
		vna_n5230a_wait ();
	}
	if (Si == 5)
	{
	}
}

/* Return some VNA capabilities */
gdouble vna_n5230a_get_capa (gint type)
{
	gdouble ret = -1;

	switch (type)
	{
		case 1: /* Minimal frequency */
			ret =  0.010;
			break;
		case 2: /* Maximal frequency */
			ret = 50.000;
			break;
		case 3: /* Number of points */
			ret = 16001.0;
			break;
		case 4: /* Minimal IF bandwidth */
			ret = 1.0;
			break;
		case 5: /* Maximal IF bandwidth */
			ret = 250000.0;
			break;
	}

	return ret;
}