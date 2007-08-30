#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include <gtk/gtk.h>
#include <glade/glade.h>

#include "calibrate.h"
#include "structs.h"
#include "helpers.h"
#include "network.h"
#include "vna_proxy.h"

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
	DataVector **fullin;
	DataVector **fullout;
	gchar *host;
	gint type;
} CalVnaThreadInfo;

/* Forward declaration */
static void cal_vna_start (gpointer data);

/************************ The main thread **********************************/

/* Start the (simple) online calibration thread */
DataVector* cal_vna_calibrate (DataVector *in, DataVector *a, DataVector *b, DataVector *c, gchar *host, gint type)
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
	threadinfo->type = type;

	threadinfo->fullin  = NULL;
	threadinfo->fullout = NULL;

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

/* Start the full 2-port or TRL online calibration thread */
gboolean cal_vna_full_calibrate (DataVector **indata, DataVector **outdata, gchar *host, gint type)
{
	CalVnaThreadInfo *threadinfo;

	glob->flag |= FLAG_VNA_CAL;

	threadinfo = g_new0 (CalVnaThreadInfo, 1);
	threadinfo->fullin   = indata;
	threadinfo->fullout  = outdata;
	threadinfo->host     = host;
	threadinfo->type     = type;

	glob->calwin->cal_GThread = 
		g_thread_create ((GThreadFunc) cal_vna_start, threadinfo, TRUE, NULL);

	/* Wait for measurement thread to finish. Ugly but works. */
	while (glob->flag & FLAG_VNA_CAL)
	{
		while (gtk_events_pending ()) gtk_main_iteration ();
		usleep (1e5);
	}

	g_free (threadinfo);

	return TRUE;
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

/* Sets the ProgressBar to fraction  */
static void cal_vna_update_progress (gfloat fraction)
{
	if (fraction >= 0.0)
	{
		gtk_progress_bar_set_fraction (
			GTK_PROGRESS_BAR (glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress")),
			fraction);
		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress_frame" ), 
			TRUE);
	}
	else
	{
		/* fraction < 0 -> disable ProgressBar */
		gtk_progress_bar_set_fraction (
			GTK_PROGRESS_BAR (glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress")),
			0.0);
		gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->calwin->xmlcal, "cal_progress_frame" ), 
			FALSE);
	}
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

	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'OUTPSTAT;'", VNA_ETIMEOUT);
	vna_proxy_enter (sockfd, statbyte, 10, VNA_GBIP_INT, VNA_ETIMEOUT);
	//printf ("cal_vna_command: statbyte = %s\n", statbyte);
	while (strncmp (statbyte, "000,000", 10) )
	{
		usleep (1e5);
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'OUTPSTAT;'", VNA_ETIMEOUT);
		vna_proxy_enter (sockfd, statbyte, 10, VNA_GBIP_INT, VNA_ETIMEOUT);
	}

	g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA '%s'", cmd);
	vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'WAIT;ENTO;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
}

/* Push spectrum data to VNA via tarray command */
static void cal_vna_transmit_data_block (ComplexDouble *data, gint len, gint mem, gint sockfd)
{
	gchar cmdstr[81], header[] = {0x23, 0x41, 0x19, 0x08}; // last two bytes reversed for FORM2
	gchar buf[8*801], reply[VNA_STAT_LEN+1];
	gfloat val;
	gint i, status = -1;

	g_return_if_fail (data);
	g_return_if_fail (len < 802);
	g_return_if_fail (sockfd > 0);
	g_return_if_fail ((mem > 0) && (mem < 5));

	/* Prepare data input */
	vna_proxy_send_cmd (sockfd, "DCL", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FORM2;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'INPURAW%d;' END", mem);
	vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP, VNA_ETIMEOUT|VNA_ESYNTAXE);

	/* Transmit FORM2 header for 801 datapoints */
	/* 0x23 0x41 is '#A', the standard VNA data header, and 0x08 0x19 is hex for 6408 */
	vna_proxy_send_cmd (sockfd, "* PROXYCMD: tarray 4 0", VNA_ETIMEOUT);
	vna_proxy_sendall (sockfd, header, 4);
	/* Get status message */
	vna_proxy_receiveall (sockfd, reply, VNA_STAT_LEN);
	if (sscanf (reply, "* PROXYMSG: Status %d", &status) != 1)
		cal_vna_exit ("Could not parse proxy reply: %s", reply);

	/* Turn the order around for FORM2 output */
	for (i=0; i<len; i++)
	{
		val = (gfloat)data[i].re;
		buf[8*i+0] = ((gchar *)&val)[3];
		buf[8*i+1] = ((gchar *)&val)[2];
		buf[8*i+2] = ((gchar *)&val)[1];
		buf[8*i+3] = ((gchar *)&val)[0];

		val = (gfloat)data[i].im;
		buf[8*i+4] = ((gchar *)&val)[3];
		buf[8*i+5] = ((gchar *)&val)[2];
		buf[8*i+6] = ((gchar *)&val)[1];
		buf[8*i+7] = ((gchar *)&val)[0];
	}

	g_snprintf (cmdstr, 80, "* PROXYCMD: tarray %d 1", 8*len);
	vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT);

	vna_proxy_sendall (sockfd, buf, 8*len);
	usleep (1e6);
	/* Get status message */
	vna_proxy_receiveall (sockfd, cmdstr, VNA_STAT_LEN);
	if (sscanf (reply, "* PROXYMSG: Status %d", &status) != 1)
		cal_vna_exit ("Could not parse proxy reply: %s", reply);
}

/* Push 'len' points from 'data' into memory 'type' of vna, or just transmit
 * the data if type == NULL. */
static void cal_vna_push_data (ComplexDouble *data, gint len, gchar *type, gint mem, gint sockfd)
{
	ComplexDouble *finaldata;
	gchar cmdstr[81];

	g_return_if_fail (len < 802);
	g_return_if_fail ((mem > 0) && (mem < 5));

	/* Select calibration data type */
	if (type)
	{
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA '%s;' END", type);
		vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
		vna_proxy_spoll_wait (sockfd, 4);
	}

	/* Transmit data */
	if (len == 801)
		cal_vna_transmit_data_block (data, len, mem, sockfd);
	else
	{
		/* If there are less than 801 datapoints fill the rest with zeros */
		finaldata = g_new0 (ComplexDouble, 801);
		memcpy (finaldata, data, len * sizeof(ComplexDouble));
		cal_vna_transmit_data_block (finaldata, 801, mem, sockfd);
		g_free (finaldata);
	}
	
	/* Finish transmission */
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" EOI 10", VNA_ETIMEOUT|VNA_ESYNTAXE);
	
	if (type)
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);

	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'WAIT;' END", VNA_ETIMEOUT|VNA_ESYNTAXE);
}

/* Do a reflection calibration */
static void cal_vna_reflection (CalVnaThreadInfo *threadinfo, gint sockfd)
{
	guint datapos, points_in_win, i;
	ComplexDouble *data;
	gchar cmdstr[81];
	int winleft, windone;

	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'S11;'", VNA_ETIMEOUT);
	usleep (2e6);
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'DELC;CALS1;WAIT;'", VNA_ETIMEOUT);
	usleep (2e6);

	/* Set up time estimates */
	g_get_current_time (&glob->calwin->start_t);
	windone = 0;
	winleft = (int) ceil (threadinfo->a->len/801.0);
	glob->calwin->estim_t = (glong) (winleft * 33);
	g_timeout_add (500, (GSourceFunc) cal_show_time_estimates, NULL);

	datapos = 0;
	while (datapos < threadinfo->a->len)
	{
		points_in_win = threadinfo->in->len - datapos;
		if (points_in_win > 801)
			points_in_win = 801;

		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz...",
				threadinfo->a->x[datapos] / 1e9, 
				(threadinfo->a->x[datapos]+(threadinfo->a->x[1]-threadinfo->a->x[0])*(points_in_win-1))/1e9));

		cal_vna_update_progress (
				(threadinfo->a->x[datapos]-threadinfo->a->x[0])/(threadinfo->a->x[threadinfo->a->len-1]-threadinfo->a->x[0])
			);

		/* Set frequency window and prepare for data aquisition */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;'", VNA_ETIMEOUT);
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'CONT;STAR %.1lfHZ;STOP %.1lfHZ;'", 
				threadinfo->a->x[datapos],
				threadinfo->a->x[datapos]+(threadinfo->a->x[1]-threadinfo->a->x[0])*800);
		vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_command (sockfd, "CLES;TRIG;");
		cal_vna_command (sockfd, "CORROFF;WAIT;");
		cal_vna_command (sockfd, "CAL2;");
		cal_vna_command (sockfd, "CALIS111;");

		/* Transmit calibration data */
		cal_vna_push_data (threadinfo->a->y + datapos, points_in_win, "CLASS11A", 1, sockfd); /* open  */
		cal_vna_push_data (threadinfo->b->y + datapos, points_in_win, "CLASS11B", 1, sockfd); /* short */
		cal_vna_push_data (threadinfo->c->y + datapos, points_in_win, "CLASS11C;STANA", 1, sockfd); /* load  */

		/* Create and recall calset */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'DONE;SAV1;CALS1;'", VNA_ETIMEOUT);
		usleep (5e6);
		vna_proxy_spoll_wait (sockfd, 4);
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FRER;WAIT;'", VNA_ETIMEOUT);
		usleep (2e6);

		/* Transmit measurement data to be calibrated */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'HOLD;'", VNA_ETIMEOUT);
		cal_vna_push_data (threadinfo->in->y + datapos, points_in_win, NULL, 1, sockfd);

		/* Give the vna some time and do useless stuff */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'AUTO;WAIT;' END", VNA_ETIMEOUT);
		usleep (1e6);

		/* Read calibrated data */
		data = vna_proxy_recv_data (801);
		g_return_if_fail (data);
		for (i=0; i<points_in_win; i++)
		{
			threadinfo->out->x[datapos + i] = threadinfo->a->x[datapos + i];
			threadinfo->out->y[datapos + i] = data[i];
		}
		g_free (data);

		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;FRER;CONT;CORROFF;'", VNA_ETIMEOUT);
		usleep (2e6);

		/* Update ETA */
		cal_update_time_estimates (&windone, &winleft);

		datapos += 801;
	}
}

/* Do a full 2-port calibration */
static void cal_vna_full (CalVnaThreadInfo *threadinfo, gint sockfd)
{
	guint datapos, points_in_win, i, j;
	ComplexDouble *data;
	gchar cmdstr[81];
	gfloat startfrq, stopfrq;
	int winleft, windone;
	FILE *fh;

	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'DELC;CALS1;WAIT;ENTO;'", VNA_ETIMEOUT);
	usleep (2e6);

	/* Set up time estimates */
	g_get_current_time (&glob->calwin->start_t);
	windone = 0;
	winleft = (int) ceil (threadinfo->fullin[0]->len/801.0);
	glob->calwin->estim_t = (glong) (winleft * 97);
	g_timeout_add (500, (GSourceFunc) cal_show_time_estimates, NULL);

	datapos = 0;
	while (datapos < threadinfo->fullin[0]->len)
	{
		points_in_win = threadinfo->fullin[0]->len - datapos;
		if (points_in_win > 801)
			points_in_win = 801;

		startfrq = threadinfo->fullin[0]->x[datapos]/1e9;
		stopfrq  = (threadinfo->fullin[0]->x[datapos]+(threadinfo->fullin[0]->x[1]-threadinfo->fullin[0]->x[0])*(points_in_win-1))/1e9;
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (init)...", startfrq, stopfrq));

		/* Set frequency window and prepare for data aquisition */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;'", VNA_ETIMEOUT);
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'CONT;STAR %.1lfHZ;STOP %.1lfHZ;'", 
				threadinfo->fullin[0]->x[datapos],
				threadinfo->fullin[0]->x[datapos]+(threadinfo->fullin[0]->x[1]-threadinfo->fullin[0]->x[0])*800);
		vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_command (sockfd, "CLES;TRIG;");
		cal_vna_command (sockfd, "CORROFF;WAIT;");
		cal_vna_command (sockfd, "CAL2;");
		cal_vna_command (sockfd, "CALIFUL2;");

		/* Transmit calibration data */
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (reflection)...", startfrq, stopfrq));
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'REFL;'", VNA_ETIMEOUT);
		cal_vna_push_data (threadinfo->fullin[ 4]->y + datapos, points_in_win, "CLASS11A", 1, sockfd); /* open  */
		cal_vna_push_data (threadinfo->fullin[ 5]->y + datapos, points_in_win, "CLASS11B", 1, sockfd); /* short */
		cal_vna_push_data (threadinfo->fullin[ 6]->y + datapos, points_in_win, "CLASS11C;STANA", 1, sockfd); /* load */
		cal_vna_push_data (threadinfo->fullin[ 7]->y + datapos, points_in_win, "CLASS22A", 1, sockfd); /* open  */
		cal_vna_push_data (threadinfo->fullin[ 8]->y + datapos, points_in_win, "CLASS22B", 1, sockfd); /* short */
		cal_vna_push_data (threadinfo->fullin[ 9]->y + datapos, points_in_win, "CLASS22C;STANA", 1, sockfd); /* load */
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (refl. save)...", startfrq, stopfrq));
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'REFD;'", VNA_ETIMEOUT);
		usleep(21e6);
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (thru)...", startfrq, stopfrq));
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'TRAN;'", VNA_ETIMEOUT);
		cal_vna_push_data (threadinfo->fullin[10]->y + datapos, points_in_win, "FWDM", 1, sockfd); /* s11thru */
		cal_vna_push_data (threadinfo->fullin[11]->y + datapos, points_in_win, "REVT", 1, sockfd); /* s12thru */
		cal_vna_push_data (threadinfo->fullin[12]->y + datapos, points_in_win, "FWDT", 1, sockfd); /* s21thru */
		cal_vna_push_data (threadinfo->fullin[13]->y + datapos, points_in_win, "REVM", 1, sockfd); /* s22thru */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'TRAD;'", VNA_ETIMEOUT);
		usleep(1e6);
		cal_vna_command (sockfd, "ISOL;OMII;ISOD;"); /* omit isolation */
		usleep(1e6);

		/* Create and recall calset */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'DONE;SAV2;CALS1;'", VNA_ETIMEOUT);
		usleep (5e6);
		vna_proxy_spoll_wait (sockfd, 4);
		cal_vna_command (sockfd, "CORROFF;");
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (data input)...", startfrq, stopfrq));
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FOUPSPLI;FRER;WAIT;'", VNA_ETIMEOUT);
		usleep (2e6);

		/* Transmit measurement data to be calibrated */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'HOLD;'", VNA_ETIMEOUT);
		cal_vna_command (sockfd, "S11;");
		cal_vna_push_data (threadinfo->fullin[0]->y + datapos, points_in_win, NULL, 1, sockfd);
		cal_vna_command (sockfd, "S12;");
		cal_vna_push_data (threadinfo->fullin[1]->y + datapos, points_in_win, NULL, 3, sockfd);
		cal_vna_command (sockfd, "S21;");
		cal_vna_push_data (threadinfo->fullin[2]->y + datapos, points_in_win, NULL, 2, sockfd);
		cal_vna_command (sockfd, "S22;");
		cal_vna_push_data (threadinfo->fullin[3]->y + datapos, points_in_win, NULL, 4, sockfd);

		/* Turn on calibration */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CORRON;CALS1;WAIT;' END", VNA_ETIMEOUT);
		usleep (4e6);

		/* Read calibrated data */
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (data output)...", startfrq, stopfrq));
		for (j=0; j<4; j++)
		{
			switch (j)
			{
				case 0:
					cal_vna_command (sockfd, "S11;");
					break;
				case 1:
					cal_vna_command (sockfd, "S12;");
					break;
				case 2:
					cal_vna_command (sockfd, "S21;");
					break;
				case 3:
					cal_vna_command (sockfd, "S22;");
					break;
			}

			data = vna_proxy_recv_data (801);
			g_return_if_fail (data);
			for (i=0; i<points_in_win; i++)
			{
				threadinfo->fullout[j]->x[datapos + i] = threadinfo->fullin[0]->x[datapos + i];
				threadinfo->fullout[j]->y[datapos + i] = data[i];
			}
			g_free (data);

			/* Append new data to output files */
			fh = fopen (threadinfo->fullout[j]->file, "a");
			if (!fh)
				cal_vna_exit ("Could not open file '%s'.", threadinfo->fullout[j]->file);
			g_return_if_fail (fh);
			for (i=0; i<points_in_win; i++)
				fprintf (fh, DATAFRMT, 
						threadinfo->fullout[j]->x[datapos + i], 
						threadinfo->fullout[j]->y[datapos + i].re, 
						threadinfo->fullout[j]->y[datapos + i].im);
			fclose (fh);
		}

		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;FRER;CONT;CORROFF;'", VNA_ETIMEOUT);
		usleep (2e6);

		/* Update ETA */
		cal_update_time_estimates (&windone, &winleft);

		datapos += 801;
	}
}

/* Do a TRL calibration */
static void cal_vna_trl (CalVnaThreadInfo *threadinfo, gint sockfd)
{
	guint datapos, points_in_win, i, j;
	ComplexDouble *data;
	gchar cmdstr[81];
	gfloat startfrq, stopfrq;
	int winleft, windone;
	FILE *fh;

	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'DELC;CALS1;WAIT;ENTO;'", VNA_ETIMEOUT);
	usleep (2e6);

	/* Set up time estimates */
	g_get_current_time (&glob->calwin->start_t);
	windone = 0;
	winleft = (int) ceil (threadinfo->fullin[0]->len/801.0);
	glob->calwin->estim_t = (glong) (winleft * 65);
	g_timeout_add (500, (GSourceFunc) cal_show_time_estimates, NULL);

	datapos = 0;
	while (datapos < threadinfo->fullin[0]->len)
	{
		points_in_win = threadinfo->fullin[0]->len - datapos;
		if (points_in_win > 801)
			points_in_win = 801;

		startfrq = threadinfo->fullin[0]->x[datapos]/1e9;
		stopfrq  = (threadinfo->fullin[0]->x[datapos]+(threadinfo->fullin[0]->x[1]-threadinfo->fullin[0]->x[0])*(points_in_win-1))/1e9;
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (init)...", startfrq, stopfrq));

		/* Set frequency window and prepare for data aquisition */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;'", VNA_ETIMEOUT);
		g_snprintf (cmdstr, 80, "MTA LISTEN "VNA_GBIP" DATA 'CONT;STAR %.1lfHZ;STOP %.1lfHZ;'", 
				threadinfo->fullin[0]->x[datapos],
				threadinfo->fullin[0]->x[datapos]+(threadinfo->fullin[0]->x[1]-threadinfo->fullin[0]->x[0])*800);
		vna_proxy_send_cmd (sockfd, cmdstr, VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_command (sockfd, "CLES;TRIG;");
		cal_vna_command (sockfd, "CORROFF;WAIT;");
		cal_vna_command (sockfd, "CAL2;");
		cal_vna_command (sockfd, "CALITRL2;");

		/* Transmit calibration data */
		/* Thru (zero length) standard */
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (thru)...", startfrq, stopfrq));
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'TRLT;'", VNA_ETIMEOUT);
		usleep(1e6);
		cal_vna_push_data (threadinfo->fullin[ 4]->y + datapos, points_in_win, NULL, 1, sockfd); /* S11  */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[ 6]->y + datapos, points_in_win, NULL, 1, sockfd); /* S21 */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[ 5]->y + datapos, points_in_win, NULL, 1, sockfd); /* S12 */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[ 7]->y + datapos, points_in_win, NULL, 1, sockfd); /* S22  */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[ 8]->y + datapos, points_in_win, NULL, 1, sockfd); /* User3 */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[ 9]->y + datapos, points_in_win, NULL, 1, sockfd); /* User3 */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);

		/* S11 & S22 reflection */
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (refl.)...", startfrq, stopfrq));
		cal_vna_push_data (threadinfo->fullin[10]->y + datapos, points_in_win, "TRLR1", 1, sockfd);
		cal_vna_push_data (threadinfo->fullin[11]->y + datapos, points_in_win, "TRLR2", 1, sockfd);

		/* Line standard */
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (line)...", startfrq, stopfrq));
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'TRLL;'", VNA_ETIMEOUT);
		usleep(1e6);
		cal_vna_push_data (threadinfo->fullin[12]->y + datapos, points_in_win, NULL, 1, sockfd); /* S11  */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[14]->y + datapos, points_in_win, NULL, 1, sockfd); /* S21 */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[13]->y + datapos, points_in_win, NULL, 1, sockfd); /* S12 */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[15]->y + datapos, points_in_win, NULL, 1, sockfd); /* S22  */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[16]->y + datapos, points_in_win, NULL, 1, sockfd); /* User3 */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		cal_vna_push_data (threadinfo->fullin[17]->y + datapos, points_in_win, NULL, 1, sockfd); /* User3 */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'SIMS;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
		usleep(1e6);

		/* Omit isolation */
		cal_vna_command (sockfd, "ISOL;OMII;ISOD;");

		/* Create and recall calset */
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (save)...", startfrq, stopfrq));
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'DONE;SAVT;CALS1;'", VNA_ETIMEOUT);
		usleep (10e6);
		vna_proxy_spoll_wait (sockfd, 4);
		cal_vna_command (sockfd, "CORROFF;");
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (data input)...", startfrq, stopfrq));
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'FOUPSPLI;FRER;WAIT;'", VNA_ETIMEOUT);
		usleep (2e6);

		/* Transmit measurement data to be calibrated */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'HOLD;'", VNA_ETIMEOUT);
		cal_vna_command (sockfd, "S11;");
		cal_vna_push_data (threadinfo->fullin[0]->y + datapos, points_in_win, NULL, 1, sockfd);
		cal_vna_command (sockfd, "S12;");
		cal_vna_push_data (threadinfo->fullin[1]->y + datapos, points_in_win, NULL, 3, sockfd);
		cal_vna_command (sockfd, "S21;");
		cal_vna_push_data (threadinfo->fullin[2]->y + datapos, points_in_win, NULL, 2, sockfd);
		cal_vna_command (sockfd, "S22;");
		cal_vna_push_data (threadinfo->fullin[3]->y + datapos, points_in_win, NULL, 4, sockfd);

		/* Turn on calibration */
		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CORRON;CALS1;WAIT;' END", VNA_ETIMEOUT);
		usleep (4e6);

		/* Read calibrated data */
		cal_vna_set_netstat (g_strdup_printf ("Calibrating %.3f - %.3f GHz (data output)...", startfrq, stopfrq));
		for (j=0; j<4; j++)
		{
			switch (j)
			{
				case 0:
					cal_vna_command (sockfd, "S11;");
					break;
				case 1:
					cal_vna_command (sockfd, "S12;");
					break;
				case 2:
					cal_vna_command (sockfd, "S21;");
					break;
				case 3:
					cal_vna_command (sockfd, "S22;");
					break;
			}

			data = vna_proxy_recv_data (801);
			g_return_if_fail (data);
			for (i=0; i<points_in_win; i++)
			{
				threadinfo->fullout[j]->x[datapos + i] = threadinfo->fullin[0]->x[datapos + i];
				threadinfo->fullout[j]->y[datapos + i] = data[i];
			}
			g_free (data);

			/* Append new data to output files */
			fh = fopen (threadinfo->fullout[j]->file, "a");
			if (!fh)
				cal_vna_exit ("Could not open file '%s'.", threadinfo->fullout[j]->file);
			g_return_if_fail (fh);
			for (i=0; i<points_in_win; i++)
				fprintf (fh, DATAFRMT, 
						threadinfo->fullout[j]->x[datapos + i], 
						threadinfo->fullout[j]->y[datapos + i].re, 
						threadinfo->fullout[j]->y[datapos + i].im);
			fclose (fh);
		}

		vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CLES;FRER;CONT;CORROFF;'", VNA_ETIMEOUT);
		usleep (2e6);

		/* Update ETA */
		cal_update_time_estimates (&windone, &winleft);

		datapos += 801;
	}
}

/* Start an online calibration */
static void cal_vna_start (gpointer data)
{
	CalVnaThreadInfo *threadinfo;
	gint sockfd;

	threadinfo = (CalVnaThreadInfo *) data;
	
	/* Open connection */
	sockfd = (gint) vna_proxy_connect (threadinfo->host);
	if (sockfd < 0)
		cal_vna_exit ("Could not connect to Ieee488Proxy host %s", threadinfo->host);

	glob->calwin->sockfd = sockfd;

	/* Give the Ieee488 card GPIB 21 */
	if (vna_proxy_send_cmd (sockfd, "* PROXYCMD: initialize 21", VNA_ETIMEOUT))
		cal_vna_exit ("Could not initialize Ieee488 Card.");

	/* Reset network (to 801 points) and do local lock out */
	cal_vna_set_netstat (g_strdup ("Setting up network analyzer..."));
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'PRES;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	usleep (8e6);
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" LLO", VNA_ETIMEOUT);
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'CORROFF;CONT;LOGM;'", VNA_ETIMEOUT|VNA_ESYNTAXE);
	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" DATA 'POIN801;AVEROFF;RAMP;WAIT;'", VNA_ETIMEOUT|VNA_ESYNTAXE);

	switch (threadinfo->type)
	{
		case 0: /* reflection */
			cal_vna_reflection (threadinfo, sockfd);
			threadinfo->out->file = (gchar *) 1; /* Mark a successful calibration run */
			break;
		case 1: /* transmission */
			dialog_message ("Transmission calibration is not supported yet.");
			break;
		case 2: /* full 2-port */
			cal_vna_full (threadinfo, sockfd);
			break;
		case 3: /* TRL */
			cal_vna_trl (threadinfo, sockfd);
			break;
	}

	vna_proxy_send_cmd (sockfd, "MTA LISTEN "VNA_GBIP" GTL", VNA_ETIMEOUT);

	close (sockfd);
	glob->flag &= ~FLAG_VNA_CAL;
}
