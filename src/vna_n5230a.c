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

#define DV(x)  			/* For debuggin set DV(x) x */

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

	while (total < len)
	{
		/* Wait VNA_N5230A_RECV_TOUT seconds for data */
		for (i=0; i<VNA_N5230A_RECV_TOUT; i++)
		{
			tv.tv_sec  = 1;
			tv.tv_usec = 0;
			FD_ZERO (&fdset);
			FD_SET (s, &fdset);
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

	DV(if (len < 128)
		printf ("received (%d bytes): %.*s\n", len, len, buf);
	else
		printf ("received (%d bytes): [data not shown]\n", len);)

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
int vna_n5230a_send_cmd (int fd, char *format, ...)
{
	va_list ap;
	gchar *msg, *sndmsg;

	g_return_val_if_fail (format, -1);

	va_start (ap, format);
	msg = g_strdup_vprintf (format, ap);
	va_end (ap);

	sndmsg = g_strdup_printf ("%s\n", msg);
	g_free (msg);
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
	char buf[16001*8];
	int sockfd, i, numdigits;

	g_return_val_if_fail (glob->netwin || glob->calwin, NULL);
	g_return_val_if_fail (glob->netwin->sockfd > 0, NULL);
	sockfd = glob->netwin->sockfd;

	vna_n5230a_send_cmd (sockfd, "FORM:BORD SWAP");
	vna_n5230a_send_cmd (sockfd, "FORM:DATA REAL,32");

	if ((glob->netwin->type == 2) || (glob->netwin->calmode != 3))
		/* Read measurement data */
		vna_n5230a_send_cmd (sockfd, "CALC:DATA? SDATA");
	else
	{
		/* Read formatted data for calibration verification */
		vna_n5230a_send_cmd (sockfd, "CALC:FORM SMIT");
		vna_n5230a_send_cmd (sockfd, "CALC:DATA? FDATA");
	}

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
	for (i=0; i<points; i++)
	{
		data[i].re = (gdouble) *((gfloat *) &buf[8*i+0]);
		data[i].im = (gdouble) *((gfloat *) &buf[8*i+4]);
		data[i].abs = sqrt(data[i].re*data[i].re + data[i].im*data[i].im);
	}

	if ((glob->netwin->type == 1) && (glob->netwin->calmode == 3))
		/* Go back to log scale after reading cal verify data */
		vna_n5230a_send_cmd (sockfd, "CALC:FORM MLOG");

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
	gdouble delta;
	
	if (glob->netwin->swpmode == 1)
	{
		/* Ramp mode */
		delta = 10.0 * pow (glob->netwin->bandwidth/1e3, -0.7);
		delta *= (gdouble) glob->netwin->avg;
		delta += (gdouble) glob->netwin->points * glob->netwin->dwell;
		delta *= 1000.0;
		delta *= 0.9;
	}
	else
	{
		/* Step mode */
		delta = 29.5 * (pow (glob->netwin->bandwidth/1e3, -1.4) + 0.17 * pow (glob->netwin->bandwidth/1e3, -0.13));
		delta *= (gdouble) glob->netwin->avg;
		delta += (gdouble) glob->netwin->points * glob->netwin->dwell;
		delta *= 1000.0;
		delta *= 0.85;
	}

	switch (glob->netwin->calmode)
	{
		case 0:
			/* No calibration */
			if (glob->netwin->numparam == 4)
				delta *= (glob->netwin->swpmode == 1) ? 2.7 : 2.0;
			break;
		case 1:
			/* Calibration enabled */
			delta *= glob->netwin->numparam == 1 ? 1.85 : 1.26;
			if (glob->netwin->swpmode == 2)
				delta *= 1.13;
			break;
		case 2:
			/* Acquire calibration */
			delta *= 21.6;
			break;
		case 3:
			/* Verify calibration */
			delta += 70000;
			break;
	}

	/*
	if (glob->netwin->numparam == 6)
		delta *= (glob->netwin->swpmode == 1) ? 7 : 2.0;
	*/

	return (glong) delta;
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

/* Delete old calsets that would be in the way */
void vna_n5230a_del_calsets ()
{
	NetworkWin *netwin;
	int sockfd, i, num, maxwin;
	gchar guid[39], curname[80], comparename[80];
	GPtrArray *guidarray;

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd);
	netwin = glob->netwin;
	sockfd = netwin->sockfd;

	guidarray = g_ptr_array_new ();

	/* Build array of all know GUIDs */
	vna_n5230a_send_cmd (sockfd, "SENS:CORR:CSET:CAT?");
	vna_n5230a_get_reply (sockfd, guid, 1);
	guid[38] = '\0';
	vna_n5230a_get_reply (sockfd, guid, 38);
	while (guid[0] == '{')
	{
		g_ptr_array_add (guidarray, g_strdup (guid));
		vna_n5230a_get_reply (sockfd, guid, 1);
		vna_n5230a_get_reply (sockfd, guid, 38);
	}

	/* Delete calsets if they are to be overwritten */
	maxwin = ceil((netwin->stop - netwin->start)/netwin->resol/(gdouble)netwin->points);
	for (i=0; i<guidarray->len; i++)
	{
		vna_n5230a_send_cmd (sockfd, "SENS:CORR OFF");
		vna_n5230a_send_cmd (sockfd, "SENS:CORR:CSET:ACT '%s',1", g_ptr_array_index (guidarray, i));
		vna_n5230a_send_cmd (sockfd, "SENS:CORR:CSET:NAME?");
		vna_n5230a_get_reply (sockfd, curname, 80);
		
		for (num=1; num<=maxwin; num++)
		{
			sprintf (comparename, "\"gwfcal_%.3f-%.3f_%.0f_%03d\"", 
					netwin->start/1e9, netwin->stop/1e9, netwin->resol/1e3, num);
			if (!strncmp (curname, comparename, 80))
			{
				vna_n5230a_send_cmd (sockfd, "SENS:CORR:CSET:DEL '%s'", g_ptr_array_index (guidarray, i));
				break;
			}
		}
		g_free (g_ptr_array_index (guidarray, i));
	}
	vna_n5230a_send_cmd (sockfd, "SENS:CORR OFF");

	g_ptr_array_free (guidarray, TRUE);
}

/* Prepare a measurement in sweep mode */
void vna_n5230a_sweep_prepare ()
{
	NetworkWin *netwin;
	int sockfd;

	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd);
	netwin = glob->netwin;
	sockfd = netwin->sockfd;

	vna_n5230a_llo (sockfd);
	vna_n5230a_send_cmd (sockfd, "SYST:FPR");

	if (netwin->calmode == 2)
	{
		vna_n5230a_send_cmd (sockfd, "DISP:WIND ON");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF 'gwf_S12',S12");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND1:TRAC1:FEED 'gwf_S12'");
		vna_n5230a_send_cmd (sockfd, "DISP:WIND1:TRAC1:Y:SCALE:AUTO");
		vna_n5230a_del_calsets ();
		vna_n5230a_send_cmd (sockfd, "SYST:FPR");
	}

	if (netwin->numparam > 1)
	{
		/* Prepare display for full S-matrix measurement */
		vna_n5230a_send_cmd (sockfd, "DISP:ARR STAC");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF 'gwf_S11',S11");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF 'gwf_S12',S12");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF 'gwf_S21',S21");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF 'gwf_S22',S22");
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
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:DEF 'gwf_%s',%s", netwin->param, netwin->param);
		vna_n5230a_send_cmd (sockfd, "DISP:WIND:TRAC:FEED 'gwf_%s'", netwin->param);
		vna_n5230a_send_cmd (sockfd, "DISP:WIND:TRAC:Y:SCALE:AUTO");
		vna_n5230a_send_cmd (sockfd, "CALC:PAR:SEL 'gwf_%s'", netwin->param);
	}

	/* Prevent a possibly long initial update cycle */
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:SWE:POIN 11");

	vna_n5230a_send_cmd (sockfd, "SENS:BWID:TRAC OFF");
	if (netwin->bandwidth > 0)
	{
		vna_n5230a_send_cmd (sockfd, "SENS:BWID %f", netwin->bandwidth);
		vna_n5230a_wait ();
	}

	if (netwin->swpmode == 1)
		vna_n5230a_send_cmd (sockfd, "SENS:SWE:GEN ANAL");
	else
		vna_n5230a_send_cmd (sockfd, "SENS:SWE:GEN STEP");
	vna_n5230a_wait ();

	vna_n5230a_send_cmd (sockfd, "SENS:AVER:COUN %d", netwin->avg);
	vna_n5230a_send_cmd (sockfd, "SENS:AVER ON");

	vna_n5230a_send_cmd (sockfd, "SENS:SWE:POIN 16001");
	vna_n5230a_wait ();

	vna_n5230a_send_cmd (sockfd, "SOUR:POW1 0");

	if (netwin->dwell > 0)
		vna_n5230a_send_cmd (sockfd, "SENS:SWE:DWEL %f", netwin->dwell);
	else
		vna_n5230a_send_cmd (sockfd, "SENS:SWE:DWEL 0");
	vna_n5230a_wait ();
}

/* Set the VNA start and stop frequency (in Hz) */
void vna_n5230a_set_startstop (gdouble start, gdouble stop)
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);
	g_return_if_fail (stop > start);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:FREQ:STAR %.1lf", start);
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:FREQ:STOP %.1lf", stop);
	vna_n5230a_wait ();
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
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);

	numg--;
	g_return_if_fail (numg > 0);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:AVER:CLE");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:SWE:GRO:COUN %d", numg);
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

	buf[0] = '\0';
	while ((buf[0] != '+') && (counter < 1200))
	{
		buf[0] = '\0';
		vna_n5230a_receiveall_full (glob->netwin->sockfd, buf, 3, 1);
		counter++;
	}

	if (counter == 1200)
		vna_thread_exit ("Measurement window did not finish within 5 minutes, something must have gone wrong.");
}

/* Reads the last error message from the system error stack */
gchar* vna_n5230a_get_err ()
{
	char reply[255];

	g_return_val_if_fail (glob->netwin, NULL);
	g_return_val_if_fail (glob->netwin->sockfd > 0, NULL);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "SYST:ERR?");
	vna_n5230a_get_reply (glob->netwin->sockfd, reply, 255);

	return g_strdup (reply);
}

/* Select/activate given S-parameter */
void vna_n5230a_select_s (gchar *sparam)
{
	g_return_if_fail (glob->netwin);
	g_return_if_fail (glob->netwin->sockfd > 0);
	g_return_if_fail (strlen (sparam) == 3);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_%s'", sparam);
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
		g_return_if_fail (Si != 4 );

		/* and wait */
		vna_n5230a_wait ();
	}
	if (Si == 5)
	{
		/* Not supported */
		g_return_if_fail (Si != 5 );
	}
}

/* Select the first available parameter */
gboolean vna_n5230a_sel_first_par ()
{
	gchar reply[256];
	gint i=0;

	g_return_val_if_fail (glob->netwin, FALSE);
	g_return_val_if_fail (glob->netwin->sockfd > 0, FALSE);

	vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:CAT?");
	vna_n5230a_get_reply (glob->netwin->sockfd, reply, 255);

	if (reply[0] == '\0')
		return FALSE;

	while ((i<255) && (reply[i]!=',') && (reply[i]!='\0'))
		i++;

	if (reply[i] != ',')
		return FALSE;

	reply[0] = '\'';
	reply[i] = '\0';

	vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL %s'", reply);

	return TRUE;
}

/* Calibrate the currently selected frequency window */
gchar* vna_n5230a_calibrate (gdouble fstart, gdouble fstop, gdouble resol, gint num)
{
	gchar *err;

	vna_n5230a_send_cmd (glob->netwin->sockfd, "*CLS");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:PREF:CSET:SAVU 1");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:COLL:METH SPARSOLT");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:PREF:ECAL:ORI ON");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:COLL:ACQ ECAL1,CHAR0");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CSET:NAME 'gwfcal_%.3f-%.3f_%.0f_%03d'", fstart/1e9, fstop/1e9, resol/1e3, num);
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CSET:DESC '%06.3f - %06.3f GHz'",
			(fstart + 16001.0*((gdouble)num-1.0) * resol)/1e9, (fstart + (16001.0*(gdouble)num-1.0) * resol)/1e9);
	vna_n5230a_wait ();

	err = vna_n5230a_get_err ();
	if ((err) && (err[1] != '0'))
		return err;

	g_free (err);
	return NULL;
}

/* Calibrate the currently selected frequency window */
gchar* vna_n5230a_cal_recall (gdouble fstart, gdouble fstop, gdouble resol, gint num)
{
	gchar *err;

	if (num < 0)
	{
		/* Disable calibration */
		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR OFF");
		vna_n5230a_wait ();
		return NULL;
	}

	if ((glob->netwin->calmode == 3) && (glob->netwin->numparam > 1))
	{
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_S12'");
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC NORM");
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_S21'");
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC NORM");
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_S11'");
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC NORM");
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_S22'");
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC NORM");
	}
	if ((glob->netwin->calmode == 3) && (glob->netwin->numparam == 1))
	{
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_%s'", glob->netwin->param);
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC NORM");
	}

	g_return_val_if_fail (fstart > 0.0   , NULL);
	g_return_val_if_fail (fstop  > fstart, NULL);
	g_return_val_if_fail (resol  > 0.0   , NULL);

	/* Make sure the new start and stop frequencies are actually set */
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:SWE:POIN 11");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:SWE:GRO:COUN 1");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:SWE:MODE GRO");
	vna_n5230a_wait ();
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:SWE:POIN 16001");
	vna_n5230a_wait ();

	vna_n5230a_send_cmd (glob->netwin->sockfd, "*CLS");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:INT ON");
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CSET:ACT 'gwfcal_%.3f-%.3f_%.0f_%03d',0", fstart/1e9, fstop/1e9, resol/1e3, num);
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR ON");
	vna_n5230a_wait ();

	err = vna_n5230a_get_err ();
	if ((err) && (err[1] != '0'))
		return err;

	g_free (err);
	return NULL;
}

/* Verify the current calibration using the ECAL confidence check */
void vna_n5230a_cal_verify ()
{
	g_return_if_fail (glob->netwin->sockfd);

	vna_n5230a_set_numg (2);
	vna_n5230a_wait ();
	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:AVER OFF");
	vna_n5230a_wait ();

	if (glob->netwin->numparam > 1)
	{
		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH:PAR 'gwf_S12'");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH ECAL1");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_S12'");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC DIV");
		vna_n5230a_wait ();

		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH:PAR 'gwf_S21'");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH ECAL1");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_S21'");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC DIV");
		vna_n5230a_wait ();

		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH:PAR 'gwf_S11'");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH ECAL1");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_S11'");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC DIV");
		vna_n5230a_wait ();

		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH:PAR 'gwf_S22'");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH ECAL1");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_S22'");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC DIV");
		vna_n5230a_wait ();
	}
	else
	{
		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH:PAR 'gwf_%s'", glob->netwin->param);
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH ECAL1");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:PAR:SEL 'gwf_%s'", glob->netwin->param);
		vna_n5230a_wait ();
		vna_n5230a_set_numg (2);	/* Don't ask... */
		vna_n5230a_send_cmd (glob->netwin->sockfd, "DISP:WIND:TRAC:Y:SCALE:AUTO");
		vna_n5230a_wait ();
		vna_n5230a_send_cmd (glob->netwin->sockfd, "CALC:MATH:FUNC DIV");
		vna_n5230a_wait ();
	}

	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:AVER ON");

	vna_n5230a_wait ();
	vna_n5230a_set_numg (glob->netwin->avg+1);
	vna_n5230a_wait ();

	vna_n5230a_send_cmd (glob->netwin->sockfd, "SENS:CORR:CCH:DONE");
}

/* Round IF bandwidth value to nearest possible value */
gdouble vna_n5230a_round_bwid (gdouble bwid_in)
{
	int i = 0;
	gdouble bwid_out;
	gdouble bwids[] = {0.001, 0.002, 0.003, 0.005, 0.007, 0.010, 0.015, 0.020, 
		           0.030, 0.050, 0.070, 0.100, 0.150, 0.200, 0.300, 0.500, 
			   0.700, 1.000, 1.500, 2.000, 3.000, 5.000, 7.000, 10.00, 
			   15.00, 20.00, 30.00, 50.00, 70.00, 100.0, 150.0, 200.0, 
			   250.0, -1};
	
	bwid_in /= 1e3;
	while ((bwids[i] > 0) && (bwids[i] < bwid_in))
		i++;

	if (bwids[i] <= 0)
		bwid_out = bwids[i-1];
	else
	{
		if (i > 0)
		{
			if (fabs (bwids[i-1]-bwid_in) <
			    fabs (bwids[i  ]-bwid_in))
				bwid_out = bwids[i-1];
			else
				bwid_out = bwids[i];

		}
		else
			bwid_out = bwids[0];
	}

	return bwid_out * 1e3;
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
