#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "structs.h"
#include "helpers.h"
#include "network.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

/* Data that needs to be passed to the calibration thread */
typedef struct
{
	DataVector *in;
	DataVector *a;
	DataVector *b;
	DataVector *c;
	DataVector *out;
	gchar *host;
} CalVnaThreadInfo;

/* Forward declaration */
static void cal_vna_start (gpointer data);

/************************ The main thread **********************************/

/* Start the online calibration thread */
DataVector* cal_vna_calibrate (DataVector *in, DataVector *a, DataVector *b, DataVector *c, gchar *host)
{
	DataVector *outdata;
	CalVnaThreadInfo *threadinfo;

	glob->flag |= FLAG_VNA_CAL;

	outdata = new_datavector (in->len);

	threadinfo = g_new (CalVnaThreadInfo, 1);
	threadinfo->in   = in;
	threadinfo->a    = a;
	threadinfo->b    = b;
	threadinfo->c    = c;
	threadinfo->out  = outdata;
	threadinfo->host = host;

	glob->calwin->cal_GThread = 
		g_thread_create ((GThreadFunc) cal_vna_start, threadinfo, TRUE, NULL);

	/* Wait for measurement thread to finish. Ugly but works. */
	while (glob->flag & FLAG_VNA_CAL)
	{
		while (gtk_events_pending ()) gtk_main_iteration ();
		usleep (1e5);
	}

	g_free (threadinfo);

	if (!outdata->file)
	{
		free_datavector (outdata);
		return NULL;
	}

	return outdata;
}

/************************ Calibrate thread callbacks ***********************/

/* Show a message that has been created in the calibration thread */
static gboolean vna_cal_measurement_finished (gpointer data)
{
	if (data)
	{
		dialog_message ((gchar *) data);
		g_free (data);
	}

	return FALSE;
}

/* Set text string in progress bar */
gboolean cal_vna_set_netstat (gpointer data)
{
	if ((!glob->calwin) || (!glob->calwin->xmlcal))
		return FALSE;

	gtk_progress_bar_set_text (
		GTK_PROGRESS_BAR (glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress")),
		(gchar *) data);
	g_free (data);
	
	return FALSE;
}

/************************ The calibrate thread *****************************/

/* Exit calibration as something has gone wrong */
void cal_vna_exit (gchar *format, ...)
{
	va_list ap;
	char *message = NULL;

	if (format)
	{
		va_start (ap, format);
		message = g_strdup_vprintf (format, ap);
		va_end (ap);
	}

printf ("Errormessage: %s\n", message);
	g_timeout_add (1, (GSourceFunc) vna_cal_measurement_finished, message);

	if (glob->calwin && (glob->calwin->sockfd > 0))
		close (glob->calwin->sockfd);
	glob->flag &= ~FLAG_VNA_CAL;
	g_thread_exit (NULL);
}

/* Send a command to the network in an ordered way */
void cal_vna_command (int sockfd, gchar *cmd)
{
	gchar statbyte[10], cmdstr[81];

	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'OUTPSTAT;'", VNA_ETIMEOUT);
	vna_enter (sockfd, statbyte, 10, VNA_GBIP_INT, VNA_ETIMEOUT);
printf ("cal_vna_command: statbyte = %s\n", statbyte);
	while (strncmp (statbyte, "000,000", 10) )
	{
		usleep (10000);
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'OUTPSTAT;'", VNA_ETIMEOUT);
		vna_enter (sockfd, statbyte, 10, VNA_GBIP_INT, VNA_ETIMEOUT);
	}

	g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA '%s'", cmd);
	vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'WAIT;ENTO;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
}

/* Push data to VNA via tarray command */
static void cal_vna_transmit_data_block (ComplexDouble *data, gint len, gint sockfd)
{
	gchar buf[8*801];
	gchar cmdstr[81];
	gchar tmp;
	gint i;

	g_return_if_fail (data);
	g_return_if_fail (len < 802);
	g_return_if_fail (sockfd > 0);

	for (i=0; i<len; i++)
	{
		*((gfloat *)(buf + (8*i+0))) = (gfloat) data[i].re;
		*((gfloat *)(buf + (8*i+4))) = (gfloat) data[i].im;
	}

	g_snprintf (cmdstr, 80, "* PROXYCMD: tarray %d 1", 8*len);
	vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT);

	/* Turn the order around for FROM2 output */
	for (i=0; i<2*len; i++)
	{
		tmp = buf[4*i+0];
		buf[4*i+0] = buf[4*i+3];
		buf[4*i+3] = tmp;
		tmp = buf[4*i+1];
		buf[4*i+1] = buf[4*i+2];
		buf[4*i+2] = tmp;
	}

	vna_sendall (sockfd, buf, 8*len);
}

/* Push 'len' points from 'data' into memory 'type' of vna, or just transmit
 * the data if type == NULL. */
static void cal_vna_push_data (ComplexDouble *data, gint len, gchar *type, gint sockfd)
{
	gchar cmdstr[81], header[] = {0x23, 0x41, 0x19, 0x08}; // last two bytes reversed for FORM2
	ComplexDouble *finaldata;

	g_return_if_fail (len < 802);

	/* Select calibration data type */
	usleep (1e6);
	if (type)
	{
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'CLASS%s;'", type);
		vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
		usleep (1e6);
		vna_spoll_wait (sockfd);
	}

	/* Prepare data input */
	vna_send_cmd (sockfd, "DCL", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FORM2;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'INPURAW1;' END", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP, VNA_ETIMEOUT|VNA_ESYNTAXE);

	/* Transmit FORM2 header for 801 datapoints */
	/* 0x23 0x41 is '#A', the standard VNA data header, and 0x08 0x19 is hex for 6408 */
	vna_send_cmd (sockfd, "* PROXYCMD: tarray 4 0", VNA_ETIMEOUT);
	vna_sendall (sockfd, header, 4);

	/* Transmit data */
printf ("transmit data (len = %d)\n", len);
	if (len == 801)
		cal_vna_transmit_data_block (data, len, sockfd);
	else
	{
		/* If there are less than 801 datapoints fill the rest with zeros */
		finaldata = g_new0 (ComplexDouble, 801);
		memcpy (finaldata, data, len * sizeof(ComplexDouble));
		cal_vna_transmit_data_block (finaldata, 801, sockfd);
		g_free (finaldata);
	}
	
	/* Finish transmission */
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" EOI 10", VNA_ETIMEOUT|VNA_ESYNTAXE);
	
	usleep (1e6);
	if (type)
		//cal_vna_command (sockfd, "SIMS;");
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);

	usleep (1e6);
//	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'WAIT;' END", VNA_ETIMEOUT|VNA_ESYNTAXE);
}

/* Do a reflection calibration */
static void cal_vna_reflection (CalVnaThreadInfo *threadinfo, gint sockfd)
{
	guint datapos, points_in_win, i;
	ComplexDouble *data;
	gchar cmdstr[81];

	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'S11;'", VNA_ETIMEOUT);
	usleep (2e6);
//	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'DELC;CALS1;WAIT;'", VNA_ETIMEOUT);
//	usleep (2e6);

	datapos = 0;
	while (datapos < threadinfo->a->len)
	{
printf ("Now starting at point %d\n", datapos);
		points_in_win = threadinfo->in->len - datapos;
		if (points_in_win > 801)
			points_in_win = 801;

		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz...",
				threadinfo->a->x[datapos] / 1e9, 
				(threadinfo->a->x[datapos]+(threadinfo->a->x[1]-threadinfo->a->x[0])*(points_in_win-1))/1e9));

		/* Set frequency window and prepare for data aquisition */
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;'", VNA_ETIMEOUT);
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'CONT;STAR %.1lfHZ;STOP %.1lfHZ;'", 
				threadinfo->a->x[datapos],
				threadinfo->a->x[datapos]+(threadinfo->a->x[1]-threadinfo->a->x[0])*800);
		vna_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_command (sockfd, "CLES;TRIG;");
//		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;TRIG;'", VNA_ETIMEOUT);
		cal_vna_command (sockfd, "CORROFF;WAIT;");
		cal_vna_command (sockfd, "CAL1;");
//		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CAL1;'", VNA_ETIMEOUT);
		cal_vna_command (sockfd, "CALIS111;");
//		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CALIS111;'", VNA_ETIMEOUT);
		usleep (3e6);

		/* Transmit calibration data */
		cal_vna_push_data (threadinfo->a->y + datapos, points_in_win, "11A", sockfd); /* open  */
		cal_vna_push_data (threadinfo->b->y + datapos, points_in_win, "11B", sockfd); /* short */
		cal_vna_push_data (threadinfo->c->y + datapos, points_in_win, "11C;STANA", sockfd); /* load  */
		usleep (2e6);

		/* Create and recall calset */
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'DONE;SAV1;CALS1;'", VNA_ETIMEOUT);
		usleep (10e6);
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FRER;WAIT;'", VNA_ETIMEOUT);
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CORRON;CALS1;WAIT;' END", VNA_ETIMEOUT);
		usleep (2e6);

		/* Transmit measurement data to be calibrated */
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'HOLD;'", VNA_ETIMEOUT);
		cal_vna_push_data (threadinfo->in->y + datapos, points_in_win, NULL, sockfd);
		usleep (1e6);

		/* Give the vna some time and do useless stuff */
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'AUTO;'", VNA_ETIMEOUT);
		usleep (1e6);
		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'MARK1;MARKMAXI;WAIT;' END", VNA_ETIMEOUT);
		usleep (1e6);

		/* Read calibrated data */
		data = vna_recv_data (sockfd, 801);
		for (i=0; i<points_in_win; i++)
			threadinfo->out->y[datapos + i] = data[i];
		g_free (data);

		vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;FRER;CONT;CORROFF;'", VNA_ETIMEOUT);
		usleep (2e6);

		datapos += 801;
	}
}

/* Start an online calibration */
static void cal_vna_start (gpointer data)
{
	CalVnaThreadInfo *threadinfo;
	gint sockfd;

	threadinfo = (CalVnaThreadInfo *) data;
	g_return_if_fail (threadinfo->in->len > 3);
	
	/* Open connection */
	sockfd = (gint) vna_connect (threadinfo->host);
	if (sockfd < 0)
	{
		glob->flag &= ~FLAG_VNA_CAL;
		cal_vna_exit ("Could not connect to Ieee488Proxy host %s", threadinfo->host);
		return;
	}
	glob->calwin->sockfd = sockfd;

	/* Give the Ieee488 card GPIB 21 */
	if (vna_send_cmd (sockfd, "* PROXYCMD: initialize 21", VNA_ETIMEOUT))
		cal_vna_exit ("Could not initialize Ieee488 Card.");

	/* Reset network (to 801 points) and do local lock out */
	cal_vna_set_netstat (g_strdup ("Setting up network analyzer..."));
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'PRES;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	usleep (8e6);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" LLO", VNA_ETIMEOUT);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CORROFF;CONT;LOGM;RAMP;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'POIN801;AVEROFF;WAIT;'", VNA_ETIMEOUT|VNA_ESYNTAXE);

	if (!threadinfo->c)
		/* Transmission calibration */
		dialog_message ("Transmission calibration is not supported yet.");
	else
		/* Reflection calibration */
		cal_vna_reflection (threadinfo, sockfd);

	vna_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);

	close (sockfd);
	threadinfo->out->file = (gchar *) 1; /* Mark a successful calibration run */
	glob->flag &= ~FLAG_VNA_CAL;
}
