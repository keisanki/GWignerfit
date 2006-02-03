#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glade/glade.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>

#include "compl_mrqmin.h"
#include "spectral_numeric.h"

#include "gtkspectvis.h"
#include "structs.h"
#include "helpers.h"
#include "export.h"

extern GlobalData *glob;
extern GladeXML *gladexml;

#define SPECTRAL_GRAPH_POINTS 2048

#define SPECTRAL_WEYL        1
#define SPECTRAL_FLUC        2
#define SPECTRAL_NND         3
#define SPECTRAL_INT_NND     4
#define SPECTRAL_S2          5
#define SPECTRAL_D3          6
#define SPECTRAL_LENGTH      7
#define SPECTRAL_WIDTHS_HIST 8

#define COLOR_POISSON	color.red   = 26845; \
			color.green = 53690; \
			color.blue  = 65535; 
#define COLOR_GUE	color.red   = 32896; \
			color.green = 62194; \
			color.blue  = 28784; 
#define COLOR_GOE	color.red   = 65535; \
			color.green = 50115; \
			color.blue  = 22102; 

/* Removes all graphs from the graph widget and frees the memory */
static gboolean spectral_remove_graphs ()
{
	GtkSpectVisData *data;
	GtkSpectVis *graph;
	gboolean removed = FALSE;
	gint i;
	
	if ((!glob->spectral) || (!glob->spectral->xmlspect))
		return FALSE;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_val_if_fail (graph, FALSE);

	for (i=1; i<5; i++)
	{
		data = gtk_spect_vis_get_data_by_uid (graph, i);

		if (data)
		{
			g_free (data->X);
			g_free (data->Y);

			gtk_spect_vis_data_remove (graph, i);

			removed = TRUE;
		}
	}

	return removed;
}

/* Close the window */
void on_spectral_close_activate (GtkMenuItem *menuitem, gpointer user_data)
{
	if ((!glob->spectral) || (!glob->spectral->xmlspect))
		return;

	spectral_remove_graphs ();
	
	gtk_widget_destroy (
			glade_xml_get_widget (glob->spectral->xmlspect, "spectral_win")
			);

	g_free (glob->spectral->xmlspect);
	glob->spectral->xmlspect = NULL;
}

/* Evaluate weyl function at position x */
static gdouble spectral_weyl_val (gdouble x)
{
	SpectralWin *spectral;

	g_return_val_if_fail (glob->spectral, 0);
	spectral = glob->spectral;
	
	return   spectral->area/1e4*M_PI/C0/C0 * x*x
	       - spectral->perim/1e2/2/C0 * x
	       + spectral->offset;
}

/* Estimate the parameters of the weyl formula */
static void spectral_fit_weyl (gdouble *res, guint numres, guint offset)
{
	gdouble **A, **rhs;
	GtkWidget *entry;
	gchar text[20];
	gint k, l, i;

	g_return_if_fail (res);
	g_return_if_fail (numres);

	A   = dmatrix (1, 3, 1, 3);	/* The lhs matrix */
	rhs = dmatrix (1, 3, 1, 1);	/* The rhs matrix of the set of equations */

	/* Populate A */
	for (k=1; k<=3; k++)
		for (l=1; l<=3; l++)
		{
			A[k][l] = 0;
			
			for (i=0; i<numres; i++)
				A[k][l] += pow (res[i], k-1) * pow(res[i], l-1);
		}

	/* Populate rhs */
	for (k=1; k<=3; k++)
	{
		rhs[k][1] = 0;

		for (i=0; i<numres; i++)
			rhs[k][1] += pow (res[i], k-1) * (gdouble) (i + 1 + offset);
	}

	/* Solve the set of equations, rhs[1..3][1] holds the solution */
	gaussj (A, 3, rhs, 1, 0);

	/* Extract the solutions */
	glob->spectral->offset = rhs[1][1];
	glob->spectral->perim  = -2.0 * C0 * rhs[2][1] * 1e2;
	glob->spectral->area   = C0 * C0 / M_PI * rhs[3][1] * 1e4;

	free_dmatrix (A,   1, 3, 1, 3);
	free_dmatrix (rhs, 1, 3, 1, 1);

	/* Update the GUI */
	g_return_if_fail (glob->spectral);
	g_return_if_fail (glob->spectral->xmlspect);
	
	entry = glade_xml_get_widget (glob->spectral->xmlspect, "spectral_area_entry");
	snprintf (text, 20, "%f", glob->spectral->area);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
	
	entry = glade_xml_get_widget (glob->spectral->xmlspect, "spectral_perimeter_entry");
	snprintf (text, 20, "%f", glob->spectral->perim);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
	
	entry = glade_xml_get_widget (glob->spectral->xmlspect, "spectral_offset_entry");
	snprintf (text, 20, "%f", glob->spectral->offset);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
}
/* Prepares a frequency sorted GPtrArray with all resonances for the spectral
 * statistcs. */
static GPtrArray* spectral_get_resonances ()
{
	GtkTreeModel *model = GTK_TREE_MODEL (glob->store);
	SpectralWin *spectral;
	GtkTreeIter iter;
	gboolean toadd;
	Resonance *res;
	GPtrArray *resarray = NULL;
	guint id;

	if (glob->numres == 0)
		return NULL;

	g_return_val_if_fail (glob->spectral, NULL);
	spectral = glob->spectral;

	resarray = g_ptr_array_new ();

	if (gtk_tree_model_get_iter_first (model, &iter))
		do
		{
			gtk_tree_model_get (model, &iter, 0, &id, -1);
			res = g_ptr_array_index(glob->param, id-1);

			switch (spectral->selection)
			{
				case 's': /* Selected resonances */
					gtk_tree_model_get (model, &iter, 5, &toadd, -1);
					break;
				case 'w': /* Windowed resonances */
					if ((res->frq > glob->gparam->max) || (res->frq < glob->gparam->min))
						toadd = FALSE;
					else
						toadd = TRUE;
					break;
				default: /* All resonances */
					toadd = TRUE;
			}

			if (toadd)
				g_ptr_array_add (resarray, res);
		}
		while (gtk_tree_model_iter_next (model, &iter));

	/* sort the array */
	g_ptr_array_sort (resarray, param_compare);

	return resarray;
}

/* Prepares an array with all resonance frequencies for the spectral statistics
 * and unfolds it if requested. */
static gdouble* spectral_get_frequencies (guint *numres, gboolean unfold)
{
	SpectralWin *spectral;
	GPtrArray *resarray;
	gdouble *res;
	guint i;

	g_return_val_if_fail (glob->spectral, NULL);
	spectral = glob->spectral;

	resarray = spectral_get_resonances ();
	if (!resarray)
		return NULL;
	if (!resarray->len)
	{
		g_ptr_array_free (resarray, TRUE);
		*numres = 0;
		return NULL;
	}

	*numres = resarray->len;
	res = g_new (gdouble, *numres);

	for (i=0; i<resarray->len; i++)
		res[i] = ((Resonance *) g_ptr_array_index (resarray, i))->frq;
	
	if (unfold)
	{
		/* unfold spectrum */
		if ((spectral->area == 0) && (spectral->perim == 0) && (spectral->offset == 0))
			spectral_fit_weyl (res, *numres, glob->spectral->first_res - 1);

		for (i=0; i<*numres; i++)
			res[i] = spectral_weyl_val (res[i]);
	}

	g_ptr_array_free (resarray, TRUE);

	return res;
}

/* Prepares an array with all resonance width for the spectral statistics. */
static gdouble* spectral_get_width (guint *numres)
{
	SpectralWin *spectral;
	GPtrArray *resarray;
	gdouble *res;
	guint i;

	g_return_val_if_fail (glob->spectral, NULL);
	spectral = glob->spectral;

	resarray = spectral_get_resonances ();
	if (!resarray)
		return NULL;
	if (!resarray->len)
	{
		g_ptr_array_free (resarray, TRUE);
		*numres = 0;
		return NULL;
	}

	*numres = resarray->len;
	res = g_new (gdouble, *numres);

	for (i=0; i<resarray->len; i++)
		res[i] = ((Resonance *) g_ptr_array_index (resarray, i))->width;
	
	g_ptr_array_free (resarray, TRUE);

	return res;
}

static void spectral_too_few_resonances ()
{
	dialog_message ("Spectral statistics:\nYour selection does not contain enough resonances.");
}

/* Draws the staricase function with a weyl fit */
static void spectral_staircase ()
{
	GtkSpectVis *graph;
	SpectralWin *spectral;
	gdouble *resonances, *resX, *weylX;
	ComplexDouble *resniveau, *weylY;
	GdkColor color;
	guint numres=0, i;

	g_return_if_fail (glob->spectral);
	spectral = glob->spectral;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_if_fail (graph);

	spectral_remove_graphs ();

	resonances = spectral_get_frequencies (&numres, FALSE);
	if (numres < 2)
	{
		spectral_too_few_resonances ();
		return;
	}

	if ((spectral->area == 0) && (spectral->perim == 0) && (spectral->offset == 0))
		spectral_fit_weyl (resonances, numres, glob->spectral->first_res - 1);

	resniveau = g_new (ComplexDouble, numres+1);
	for (i=0; i<numres; i++)
		resniveau[i+1].abs = i + spectral->first_res;

	/* Prepend the resonance data with a point for the zero niveau */
	resniveau[0].abs = resniveau[1].abs - 1;
	resX = g_new (gdouble, numres+1);
	resX[0] = resonances[0];
	for (i=0; i<numres; i++)
		resX[i+1] = resonances[i];
	g_free (resonances);

	/* Add graph */
	color.red   = 65535;
	color.green = 0;
	color.blue  = 0;
	i = gtk_spect_vis_data_add (graph, resX, resniveau, numres+1, color, 'l');
	gtk_spect_vis_request_id (graph, i, 1);
	gtk_spect_vis_set_graphtype (graph, 1, 'h');

	/* Prepare Weyl prediction */
	weylX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
	weylY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
	for (i=0; i<SPECTRAL_GRAPH_POINTS; i++)
	{
		weylX[i] = (resX[numres]+1e9) / SPECTRAL_GRAPH_POINTS * (gdouble) i;
		weylY[i].abs = spectral_weyl_val (weylX[i]);
	}

	color.red   = 0;
	color.green = 0;
	color.blue  = 65535;
	i = gtk_spect_vis_data_add (graph, weylX, weylY, SPECTRAL_GRAPH_POINTS, color, 'f');
	gtk_spect_vis_request_id (graph, i, 2);

	gtk_spect_vis_set_axisscale (graph, 1e9, 0);
}

/* Draws N^{fluc} */
static void spectral_nfluc ()
{
	GtkSpectVis *graph;
	SpectralWin *spectral;
	gdouble *resonances;
	ComplexDouble *flucY;
	GdkColor color;
	guint numres=0, i;

	g_return_if_fail (glob->spectral);
	spectral = glob->spectral;

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_if_fail (graph);

	spectral_remove_graphs ();

	resonances = spectral_get_frequencies (&numres, FALSE);
	if (numres < 2)
	{
		g_free (resonances);
		spectral_too_few_resonances ();
		return;
	}

	if ((spectral->area == 0) && (spectral->perim == 0) && (spectral->offset == 0))
		spectral_fit_weyl (resonances, numres, glob->spectral->first_res - 1);

	flucY = g_new (ComplexDouble, numres);
	for (i=0; i<numres; i++)
		flucY[i].abs = (i + spectral->first_res) - spectral_weyl_val (resonances[i]);

	color.red   = 65535;
	color.green = 0;
	color.blue  = 0;

	i = gtk_spect_vis_data_add (graph, resonances, flucY, numres, color, 'f');
	gtk_spect_vis_request_id (graph, i, 1);

	gtk_spect_vis_set_axisscale (graph, 1e9, 0);
}

/* Draws the NND */
static void spectral_nnd ()
{
	GtkSpectVis *graph;
	SpectralWin *spectral;
	ComplexDouble *bins, *theoryY;
	gdouble *resonances, *binX, *theoryX;
	gdouble delta, maxdelta;
	GdkColor color;
	guint i, binnr, numres=0;

	g_return_if_fail (glob->spectral);
	spectral = glob->spectral;

	g_return_if_fail (glob->spectral->bins);

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_if_fail (graph);

	resonances = spectral_get_frequencies (&numres, TRUE);

	spectral_remove_graphs ();
	if (numres < 3)
	{
		g_free (resonances);
		spectral_too_few_resonances ();
		return;
	}

	bins = g_new0 (ComplexDouble, glob->spectral->bins+2);
	binX = g_new0 (gdouble, glob->spectral->bins+2);

	/* find largest spacing */
	maxdelta = 0;
	for (i=0; i<numres-1; i++)
	{
		delta = resonances[i+1]-resonances[i];
		if (delta > maxdelta)
			maxdelta = delta;
	}

	/* file spacings into the bins */
	for (i=0; i<numres-1; i++)
	{
		delta = resonances[i+1]-resonances[i];
		binnr = (delta < maxdelta) ? (guint) (delta/maxdelta * (glob->spectral->bins)) : glob->spectral->bins-1;

		if (binnr+1 < 0)
			binnr = -1;
		else if (binnr+1 > glob->spectral->bins+1)
			binnr = glob->spectral->bins;
		
		if (glob->spectral->normalize)
			bins[binnr+1].abs += ((gdouble) glob->spectral->bins) / (maxdelta * (gdouble) (numres-1));
		else
			bins[binnr+1].abs += 1.0;
	}

	g_free (resonances);

	/* Left border of each bin */
	for (i=0; i<glob->spectral->bins+1; i++)
		binX[i+1] = (gdouble) i * maxdelta / (gdouble) glob->spectral->bins;

	color.red   = 65535;
	color.green = 0;
	color.blue  = 0;

	i = gtk_spect_vis_data_add (graph, binX, bins, glob->spectral->bins+2, color, 'l');
	gtk_spect_vis_request_id (graph, i, 1);
	gtk_spect_vis_set_graphtype (graph, 1, 'h');

	if (glob->spectral->theo_predict & 1)
	{
		/* Prepare Poisson spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
		for (i=0; i<SPECTRAL_GRAPH_POINTS; i++)
		{
			theoryX[i] = (maxdelta + 0.5) / (gdouble) SPECTRAL_GRAPH_POINTS * (gdouble) i;
			theoryY[i].abs = exp(- theoryX[i]);

			if (!glob->spectral->normalize)
				theoryY[i].abs *= maxdelta / 
					((gdouble) glob->spectral->bins) * ((gdouble) (numres-1));
		}
		COLOR_POISSON;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS, color, 'f');
		gtk_spect_vis_request_id (graph, i, 2);
	}

	if (glob->spectral->theo_predict & 2)
	{
		/* Prepare GOE spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
		for (i=0; i<SPECTRAL_GRAPH_POINTS; i++)
		{
			theoryX[i] = (maxdelta + 0.5) / (gdouble) SPECTRAL_GRAPH_POINTS * (gdouble) i;
			theoryY[i].abs = M_PI_2 * theoryX[i] * exp(-M_PI_4 * theoryX[i]*theoryX[i]);
			/* M_PI_2 = M_PI/2; M_PI_4 = M_PI/4 */

			if (!glob->spectral->normalize)
				theoryY[i].abs *= maxdelta / 
					((gdouble) glob->spectral->bins) * ((gdouble) (numres-1));
		}
		COLOR_GOE;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS, color, 'f');
		gtk_spect_vis_request_id (graph, i, 3);
	}
	
	if (glob->spectral->theo_predict & 4)
	{
		/* Prepare GUE spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
		for (i=0; i<SPECTRAL_GRAPH_POINTS; i++)
		{
			theoryX[i] = (maxdelta + 0.5) / (gdouble) SPECTRAL_GRAPH_POINTS * (gdouble) i;
			theoryY[i].abs = 32.0/M_PI/M_PI * theoryX[i]*theoryX[i] * exp(-4.0/M_PI * theoryX[i]*theoryX[i]);

			if (!glob->spectral->normalize)
				theoryY[i].abs *= maxdelta / 
					((gdouble) glob->spectral->bins) * ((gdouble) (numres-1));
		}
		COLOR_GUE;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS, color, 'f');
		gtk_spect_vis_request_id (graph, i, 4);
	}

	gtk_spect_vis_set_axisscale (graph, 1, 0);
}

/* Draws the integrated NND */
static void spectral_integrated_nnd ()
{
	GtkSpectVis *graph;
	SpectralWin *spectral;
	ComplexDouble *intY, *theoryY;
	gdouble *resonances, *theoryX, *spaceings;
	GdkColor color;
	guint i, numres;

	g_return_if_fail (glob->spectral);
	spectral = glob->spectral;

	g_return_if_fail (glob->spectral->bins);

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_if_fail (graph);

	resonances = spectral_get_frequencies (&numres, TRUE);

	spectral_remove_graphs ();
	if (numres < 3)
	{
		g_free (resonances);
		spectral_too_few_resonances ();
		return;
	}

	/* Calculate all NN spaceings */
	spaceings = g_new (gdouble, numres);
	for (i=0; i<numres-1; i++)
		spaceings[i+1] = resonances[i+1] - resonances[i];
	spaceings[0] = 0.0;
	g_free (resonances);

	bubbleSort (spaceings, numres);

	intY = g_new (ComplexDouble, numres);
	for (i=0; i<numres; i++)
		if (glob->spectral->normalize)
			intY[i].abs = (gdouble) i / (numres-1);
		else
			intY[i].abs = (gdouble) i;

	color.red   = 65535;
	color.green = 0;
	color.blue  = 0;
	i = gtk_spect_vis_data_add (graph, spaceings, intY, numres, color, 'l');
	gtk_spect_vis_request_id (graph, i, 1);
	gtk_spect_vis_set_graphtype (graph, 1, 'h');

	if (glob->spectral->theo_predict & 1)
	{
		/* Prepare Poisson spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
		for (i=0; i<SPECTRAL_GRAPH_POINTS; i++)
		{
			theoryX[i] = (spaceings[numres-1] + 0.5) / (gdouble) SPECTRAL_GRAPH_POINTS * (gdouble) i;
			theoryY[i].abs = 1 - exp(- theoryX[i]);

			if (!glob->spectral->normalize)
				theoryY[i].abs *= (gdouble) (numres-1);
		}
		COLOR_POISSON;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS, color, 'f');
		gtk_spect_vis_request_id (graph, i, 2);
	}

	if (glob->spectral->theo_predict & 2)
	{
		/* Prepare GOE spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
		for (i=0; i<SPECTRAL_GRAPH_POINTS; i++)
		{
			theoryX[i] = (spaceings[numres-1] + 0.5) / (gdouble) SPECTRAL_GRAPH_POINTS * (gdouble) i;
			theoryY[i].abs = 1-exp(-theoryX[i]*theoryX[i]*M_PI_4);

			if (!glob->spectral->normalize)
				theoryY[i].abs *= (gdouble) (numres-1);
		}
		COLOR_GOE;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS, color, 'f');
		gtk_spect_vis_request_id (graph, i, 3);
	}

	if (glob->spectral->theo_predict & 4)
	{
		/* Prepare GUE spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
		for (i=0; i<SPECTRAL_GRAPH_POINTS; i++)
		{
			theoryX[i] = (spaceings[numres-1] + 0.5) / (gdouble) SPECTRAL_GRAPH_POINTS * (gdouble) i;
			theoryY[i].abs = -theoryX[i]/M_PI_4 * exp(-theoryX[i]*theoryX[i]/M_PI_4) + erf(theoryX[i]*M_2_SQRTPI);

			if (!glob->spectral->normalize)
				theoryY[i].abs *= (gdouble) (numres-1);
		}
		COLOR_GUE;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS, color, 'f');
		gtk_spect_vis_request_id (graph, i, 4);
	}

	gtk_spect_vis_set_axisscale (graph, 1, 0);
}

/* Calculate Sigma^2 GOE prediction */
gfloat spectral_cal_s2_goe (gfloat L)
{
	gfloat ci, si, si2;
	
	cisi (      M_PI * L, &ci, &si2);
	cisi (2.0 * M_PI * L, &ci, &si );
	
	return	2.0/M_PI/M_PI * (logf (2.0*M_PI*L) + 0.57721566 + 1.0 - cosf (2.0*M_PI*L) - ci) 
		+ 2.0*L*(1.0-2.0/M_PI*si) + (si2*si2)/M_PI/M_PI - si2/M_PI;
}

/* Calculate Sigma^2 GUE prediction */
gfloat spectral_cal_s2_gue (gfloat L)
{
	gfloat ci, si;
	
	cisi (2.0 * M_PI * L, &ci, &si);
	
	return	1.0/M_PI/M_PI * (logf (2.0*M_PI*L) + 0.57721566 - ci 
		+ 2.0 * pow (sinf (M_PI*L), 2)) - 2.0*L/M_PI * si + L;
}

/* Draws the Sigma 2 distribution */
static void spectral_sigma2 ()
{
	GtkSpectVis *graph;
	SpectralWin *spectral;
	ComplexDouble *dataY, *theoryY;
	gdouble *resonances, *dataX, *theoryX;
	GdkColor color;
	guint pos, i, j, oldj, numres, counter, adapt = 0;
	gdouble sum, l, lstepwidth, lmax, eps0, eps0stepwidth;

	g_return_if_fail (glob->spectral);
	spectral = glob->spectral;

	g_return_if_fail (glob->spectral->bins);

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_if_fail (graph);

	resonances = spectral_get_frequencies (&numres, TRUE);

	spectral_remove_graphs ();
	if (numres < 3)
	{
		g_free (resonances);
		spectral_too_few_resonances ();
		return;
	}

	/* Parameters for the graph, eps = frq of unfolded spectrum */
	lmax = 20.0;		/* Maximal length value */
	lstepwidth = 0.05/5;	/* Calculate lmax/lstepwidth points */
	eps0stepwidth = 0.05/5;	/* Average over intervals with this stepsize */

	dataX = g_new (gdouble, (lmax - lstepwidth)/lstepwidth + 2);
	dataY = g_new (ComplexDouble, (lmax - lstepwidth)/lstepwidth + 2);
	dataX[0]     = 0.0;
	dataY[0].abs = 0.0;
	pos = 1;
	/* sigma2 calculation originated from Thomas Friedrich */
	for (l=lstepwidth; (l <= lmax) && (l < resonances[numres-1]-resonances[0]); l+=lstepwidth)
	{
		/* Be more precise at small lengths */
		if ((adapt == 0) && (l > 2))
		{
			lstepwidth *= 5.0;
			eps0stepwidth *= 5.0;
			adapt = 1;
		}

		i    = 0; 
		sum  = 0;
		oldj = 0;
		for (eps0=resonances[0]+l/2; eps0<=resonances[numres-1]-l/2; eps0+=eps0stepwidth)
		{
			i++;

			/* getnumber */
			j = oldj;
			counter = 0;

			while ((eps0-l/2) > resonances[j])
				j++;

			/* This way I don't need to search from 0 in the next run */
			oldj = j;

			while ((eps0+l/2) > resonances[j])
			{
				j++;
				counter++;
			}
			/* getnumber end */
	      
			sum += ((gdouble) counter - l) * ((gdouble) counter - l);
		}
		
		sum = sum / (gdouble) i;
		
		dataX[pos]     = l;
		dataY[pos].abs = sum;

		pos++;
	}
	/* End sigma2 calculation */

	g_free (resonances);

	color.red   = 65535;
	color.green = 0;
	color.blue  = 0;
	i = gtk_spect_vis_data_add (graph, dataX, dataY, pos, color, 'l');
	gtk_spect_vis_request_id (graph, i, 1);

	if (glob->spectral->theo_predict & 1)
	{
		/* Prepare Poisson spectrum */

		/* Find data maximum */
		sum = 0.0;
		for (i=0; i<pos; i++)
			if (dataY[i].abs > sum)
				sum = dataY[i].abs;
		
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
		for (i=0; i<SPECTRAL_GRAPH_POINTS; i++)
		{
			theoryX[i] = (sum*1.1)/SPECTRAL_GRAPH_POINTS * (gdouble) i;
			theoryY[i].abs = theoryX[i];
		}
		COLOR_POISSON;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS, color, 'f');
		gtk_spect_vis_request_id (graph, i, 2);
	}

	if (glob->spectral->theo_predict & 2)
	{
		/* Prepare GOE spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
		theoryX[0]     = 0;
		theoryY[0].abs = 0;
		for (i=1; i<SPECTRAL_GRAPH_POINTS; i++)
		{
			/* Be more precise at the beginning -> L * pow (fraction, 2) */
			theoryX[i] = dataX[pos-1] * pow ( ((gdouble)i) / (SPECTRAL_GRAPH_POINTS-1), 2);
			theoryY[i].abs = (gdouble) spectral_cal_s2_goe ((gfloat) theoryX[i]);
		}
		COLOR_GOE;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS, color, 'f');
		gtk_spect_vis_request_id (graph, i, 3);
	}

	if (glob->spectral->theo_predict & 4)
	{
		/* Prepare GUE spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS);
		theoryX[0]     = 0;
		theoryY[0].abs = 0;
		for (i=1; i<SPECTRAL_GRAPH_POINTS; i++)
		{
			theoryX[i] = dataX[pos-1] * pow ( ((gdouble)i) / (SPECTRAL_GRAPH_POINTS-1), 2);
			theoryY[i].abs = (gdouble) spectral_cal_s2_gue ((gfloat) theoryX[i]);
		}
		COLOR_GUE;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS, color, 'f');
		gtk_spect_vis_request_id (graph, i, 4);
	}

	gtk_spect_vis_set_axisscale (graph, 1, 0);
}

/* Calculate Delta^3 GOE prediction
 * Needs to be integrated from 0 to L */
gfloat spectral_cal_d3_goe (gfloat L, gfloat x)
{
	return (2/L/L/L/L * (L*L*L - 2*L*L*x + x*x*x) * spectral_cal_s2_goe(x));
}

/* Calculate Delta^3 GUE prediction
 * Needs to be integrated from 0 to L */
gfloat spectral_cal_d3_gue (gfloat L, gfloat x)
{
	return (2/L/L/L/L * (L*L*L - 2*L*L*x + x*x*x) * spectral_cal_s2_gue(x));
}

/* Draws the Delta 3 distribution */
static void spectral_delta3 ()
{
	GtkSpectVis *graph;
	SpectralWin *spectral;
	ComplexDouble *dataY, *theoryY;
	gdouble *resonances, *dataX, *theoryX;
	GdkColor color;
	gint pos, i, j, adapt = 0;
	guint numres;
	gdouble l, lstepwidth, lmax, alpha, alphastepwidth;
	gint first_in_interval, num_in_interval;
	gdouble delta3, xtilde, x1, x2, x3;

	g_return_if_fail (glob->spectral);
	spectral = glob->spectral;

	g_return_if_fail (glob->spectral->bins);

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_if_fail (graph);

	resonances = spectral_get_frequencies (&numres, TRUE);

	spectral_remove_graphs ();
	if (numres < 3)
	{
		g_free (resonances);
		spectral_too_few_resonances ();
		return;
	}

	/* Parameters for the graph, eps = frq of unfolded spectrum */
	lmax = 100.0;		/* Maximal length value */
	lstepwidth = 0.5/5;	/* Calculate lmax/lstepwidth points */
	alphastepwidth = 0.2/5;	/* Average over intervals with this stepsize */

	dataX = g_new (gdouble, (lmax - lstepwidth)/lstepwidth + 2);
	dataY = g_new (ComplexDouble, (lmax - lstepwidth)/lstepwidth + 2);
	dataX[0]     = 0.0;
	dataY[0].abs = 0.0;
	pos = 1;
	/* delta3 calculation */
	for (l=lstepwidth; (l <= lmax) && (l < resonances[numres-1]-resonances[0]); l+=lstepwidth)
	{
		/* Be more precise at small lengths */
		if ((adapt == 0) && (l > 5))
		{
			lstepwidth *= 5.0;
			alphastepwidth *= 5.0;
			adapt = 1;
		}
		if ((adapt == 1) && (l > 50))
		{
			lstepwidth *= 2.0;
			alphastepwidth *= 2.0;
			adapt = 2;
		}
		//alphastepwidth = l*0.2;
		
		i = 0; 
		delta3 = 0.0;
		first_in_interval = 0;
		for (alpha=resonances[0]; alpha<=resonances[numres-1]-l; alpha+=alphastepwidth)
		{
			i++;

			/* get first value and number of values in interval */
			j = first_in_interval;

			while (resonances[j] < alpha)
				j++;

			first_in_interval = j;

			num_in_interval = 0;
			while ((j < numres) && (resonances[j] < (alpha+l)))
			{
				j++;
				num_in_interval++;
			}
			/* getnumber end */
	      
			x1 = x2 = x3 = 0.0;
			for (j=0; j<num_in_interval; j++)
			{
				xtilde = resonances[first_in_interval+j] - (alpha+l/2.0);
				x1 += xtilde;
				x2 += xtilde * xtilde;
				x3 += ((gdouble) (num_in_interval - 2*(j+1) + 1)) * xtilde;
			}

			delta3 += (gdouble) (num_in_interval*num_in_interval) / 16.0;
			delta3 -= x1*x1/l/l;
			delta3 += 1.5*((gdouble) num_in_interval)/l/l * x2;
			delta3 -= 3.0/pow(l,4) * x2 * x2;
			delta3 += x3/l;
		}
		
		dataX[pos]     = l;
		dataY[pos].abs = delta3 / (gdouble) i;

		pos++;
	}
	/* End delta3 calculation */

	g_free (resonances);

	color.red   = 65535;
	color.green = 0;
	color.blue  = 0;
	i = gtk_spect_vis_data_add (graph, dataX, dataY, pos, color, 'l');
	gtk_spect_vis_request_id (graph, i, 1);

	if (glob->spectral->theo_predict & 1)
	{
		/* Prepare Poisson spectrum */

		/* Find data maximum */
		l = 0.0;
		for (i=0; i<pos; i++)
			if (dataY[i].abs > l)
				l = dataY[i].abs;
		
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS/2);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS/2);
		theoryX[0] = theoryY[0].abs = 0.0;
		for (i=1; i<SPECTRAL_GRAPH_POINTS/2; i++)
		{
			theoryX[i] = (l*1.1*15)/(SPECTRAL_GRAPH_POINTS/2) * (gdouble) i;
			theoryY[i].abs = theoryX[i] / 15.0;
		}
		COLOR_POISSON;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS/2, color, 'f');
		gtk_spect_vis_request_id (graph, i, 2);
	}

	if (glob->spectral->theo_predict & 2)
	{
		/* Prepare GOE spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS/4);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS/4);
		theoryX[0]     = 0;
		theoryY[0].abs = 0;
		for (i=1; i<SPECTRAL_GRAPH_POINTS/4; i++)
		{
			/* Be more precise at the beginning -> L * pow (fraction, 2) */
			theoryX[i] = dataX[pos-1] * pow ( ((gdouble)i) / (SPECTRAL_GRAPH_POINTS/4-1), 2);
			/* Do not start integral at 0.0, as sigma2 has a singularity there */
			theoryY[i].abs = qsimp (spectral_cal_d3_goe, (gfloat) theoryX[i], 0.00001, (gfloat) theoryX[i]);
		}
		COLOR_GOE;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS/4, color, 'f');
		gtk_spect_vis_request_id (graph, i, 3);
	}

	if (glob->spectral->theo_predict & 4)
	{
		/* Prepare GUE spectrum */
		theoryY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS/4);
		theoryX = g_new (gdouble, SPECTRAL_GRAPH_POINTS/4);
		theoryX[0]     = 0;
		theoryY[0].abs = 0;
		for (i=1; i<SPECTRAL_GRAPH_POINTS/4; i++)
		{
			theoryX[i] = dataX[pos-1] * pow ( ((gdouble)i) / (SPECTRAL_GRAPH_POINTS/4-1), 2);
			theoryY[i].abs = qsimp (spectral_cal_d3_gue, (gfloat) theoryX[i], 1e-20, (gfloat) theoryX[i]);
		}
		COLOR_GUE;
		i = gtk_spect_vis_data_add (graph, theoryX, theoryY, SPECTRAL_GRAPH_POINTS/4, color, 'f');
		gtk_spect_vis_request_id (graph, i, 4);
	}

	gtk_spect_vis_set_axisscale (graph, 1, 0);
}

/* Calculates and draws the length spectrum */
static void spectral_length ()
{
	GtkSpectVis *graph;
	SpectralWin *spectral;
	ComplexDouble *dataY;
	gdouble *resonances, *dataX;
	gdouble pre, A, B1, B2, c1, c2, s1, s2;
	GdkColor color;
	guint i, j, numres;

	g_return_if_fail (glob->spectral);
	spectral = glob->spectral;

	g_return_if_fail (glob->spectral->bins);

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_if_fail (graph);

	resonances = spectral_get_frequencies (&numres, FALSE);

	spectral_remove_graphs ();
	if (numres < 3)
	{
		g_free (resonances);
		spectral_too_few_resonances ();
		return;
	}

	dataX = g_new (gdouble,       SPECTRAL_GRAPH_POINTS*4);
	dataY = g_new (ComplexDouble, SPECTRAL_GRAPH_POINTS*4);
	for (i=0; i<SPECTRAL_GRAPH_POINTS*4; i++)
	{
		dataX[i] = 0.01 + 4.99 / (gdouble) (SPECTRAL_GRAPH_POINTS*4) * (gdouble) (i);

		dataY[i].re = 0;
		dataY[i].im = 0;

		/* Fourier trafo of stick spectrum */
		for (j=0; j<numres; j++)
		{
			dataY[i].re += cos (2*M_PI/C0 * resonances[j] * dataX[i]);
			dataY[i].im += sin (2*M_PI/C0 * resonances[j] * dataX[i]);
		}

		/* Substract fourier trafo of Weyl contribution */
		pre = C0/(2*M_PI*M_PI*dataX[i]*dataX[i]);
		A   = C0 * spectral->area/1e4*M_PI/C0/C0;
		B1  = (- spectral->perim/1e2/2/C0 + 2*A/C0*resonances[0])*M_PI*dataX[i];
		B2  = (- spectral->perim/1e2/2/C0 + 2*A/C0*resonances[numres-1])*M_PI*dataX[i];
		c1  = cos (2*resonances[0]*M_PI*dataX[i]/C0);
		c2  = cos (2*resonances[numres-1]*M_PI*dataX[i]/C0);
		s1  = sin (2*resonances[0]*M_PI*dataX[i]/C0);
		s2  = sin (2*resonances[numres-1]*M_PI*dataX[i]/C0);

		dataY[i].re -= pre * (A*(c2-c1) + B2*s2 - B1*s1);
		dataY[i].im -= pre * (A*(s2-s1) - B2*c2 + B1*c1);

		dataY[i].abs = sqrt (dataY[i].re * dataY[i].re + dataY[i].im * dataY[i].im);
	}

	g_free (resonances);

	color.red   = 65535;
	color.green = 0;
	color.blue  = 0;
	i = gtk_spect_vis_data_add (graph, dataX, dataY, SPECTRAL_GRAPH_POINTS*4, color, 'l');
	gtk_spect_vis_request_id (graph, i, 1);
	gtk_spect_vis_set_axisscale (graph, 1, 0);
}

/* Draws the NND */
static void spectral_widths_hist ()
{
	GtkSpectVis *graph;
	SpectralWin *spectral;
	ComplexDouble *bins;
	gdouble *widths, *binX;
	gdouble maxwidth;
	GdkColor color;
	guint i, binnr, numres=0;

	g_return_if_fail (glob->spectral);
	spectral = glob->spectral;

	g_return_if_fail (glob->spectral->bins);

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_if_fail (graph);

	widths = spectral_get_width (&numres);

	spectral_remove_graphs ();
	if (numres < 3)
	{
		g_free (widths);
		spectral_too_few_resonances ();
		return;
	}

	bins = g_new0 (ComplexDouble, glob->spectral->bins+2);
	binX = g_new0 (gdouble, glob->spectral->bins+2);

	/* find largest width */
	maxwidth = 0;
	for (i=0; i<numres; i++)
		if (widths[i] > maxwidth)
			maxwidth = widths[i];

	/* file spacings into the bins */
	for (i=0; i<numres; i++)
	{
		binnr = (widths[i] < maxwidth) ? (guint) (widths[i]/maxwidth * (glob->spectral->bins)) : glob->spectral->bins-1;

		if (binnr+1 < 0)
			binnr = -1;
		else if (binnr+1 > glob->spectral->bins+1)
			binnr = glob->spectral->bins;
		
		if (glob->spectral->normalize)
			bins[binnr+1].abs += 1/(gdouble) numres;
		else
			bins[binnr+1].abs += 1.0;
	}

	g_free (widths);

	/* Left border of each bin */
	for (i=0; i<glob->spectral->bins+1; i++)
		binX[i+1] = (gdouble) i * maxwidth / (gdouble) glob->spectral->bins;

	color.red   = 65535;
	color.green = 0;
	color.blue  = 0;

	i = gtk_spect_vis_data_add (graph, binX, bins, glob->spectral->bins+2, color, 'l');
	gtk_spect_vis_request_id (graph, i, 1);
	gtk_spect_vis_set_graphtype (graph, 1, 'h');
	gtk_spect_vis_set_axisscale (graph, 1e6, 0);
}

/* Opens a new spectral window if necessary */
void spectral_open_win ()
{
	GtkSpectVis *graph;
	GladeXML *xmlspect;
	GtkWidget *entry;
	GdkColor color;
	gchar text[20];
	
	if (glob->spectral == NULL)
	{
		/* Need to create the data structure */
		glob->spectral = g_new0 (SpectralWin, 1);
		glob->spectral->view = SPECTRAL_WEYL;
		glob->spectral->first_res = 1;
		glob->spectral->selection = 'a';
		glob->spectral->bins = 10;
		glob->spectral->theo_predict = 7;
		glob->spectral->normalize = TRUE;
	}

	if (glob->spectral->xmlspect == NULL)
	{
		/* Need to open a new window */
		glob->spectral->xmlspect = glade_xml_new (GLADEFILE, "spectral_win", NULL);
		glade_xml_signal_autoconnect (glob->spectral->xmlspect);

		xmlspect = glob->spectral->xmlspect;

		gtk_spin_button_set_value (
				GTK_SPIN_BUTTON (glade_xml_get_widget (xmlspect, "spectral_bins_spin")), 
				glob->spectral->bins);

		gtk_widget_hide_all (glade_xml_get_widget (xmlspect, "spectral_theo_box"));
		graph = GTK_SPECTVIS (glade_xml_get_widget (xmlspect, "spectral_spectvis"));

		/* Set GUI selection mode */
		switch (glob->spectral->selection)
		{
			case 'a':
				gtk_check_menu_item_set_active (
					GTK_CHECK_MENU_ITEM (glade_xml_get_widget (xmlspect, "spectral_all_res")),
					TRUE);
				break;
			case 'w':
				gtk_check_menu_item_set_active (
					GTK_CHECK_MENU_ITEM (glade_xml_get_widget (xmlspect, "spectral_windowed_res")),
					TRUE);
				break;
			case 's':
				gtk_check_menu_item_set_active (
					GTK_CHECK_MENU_ITEM (glade_xml_get_widget (xmlspect, "spectral_checked_res")),
					TRUE);
				break;

		}
		
		/* Set colors of buttons */
		COLOR_POISSON;
		gtk_widget_modify_base (
				glade_xml_get_widget (glob->spectral->xmlspect, "spectral_poisson_button"), 
				GTK_STATE_NORMAL, &color);
		gtk_widget_modify_base (
				glade_xml_get_widget (glob->spectral->xmlspect, "spectral_poisson_button"), 
				GTK_STATE_PRELIGHT, &color);
		COLOR_GOE;
		gtk_widget_modify_base (
				glade_xml_get_widget (glob->spectral->xmlspect, "spectral_goe_button"), 
				GTK_STATE_NORMAL, &color);
		gtk_widget_modify_base (
				glade_xml_get_widget (glob->spectral->xmlspect, "spectral_goe_button"), 
				GTK_STATE_PRELIGHT, &color);
		COLOR_GUE;
		gtk_widget_modify_base (
				glade_xml_get_widget (glob->spectral->xmlspect, "spectral_gue_button"), 
				GTK_STATE_NORMAL, &color);
		gtk_widget_modify_base (
				glade_xml_get_widget (glob->spectral->xmlspect, "spectral_gue_button"), 
				GTK_STATE_PRELIGHT, &color);

		gtk_spect_vis_set_axisscale (graph, 1e9, 1);
		
		/* Display current Weyl coefficients */
		entry = glade_xml_get_widget (glob->spectral->xmlspect, "spectral_area_entry");
		snprintf (text, 20, "%f", glob->spectral->area);
		gtk_entry_set_text (GTK_ENTRY (entry), text);
		
		entry = glade_xml_get_widget (glob->spectral->xmlspect, "spectral_perimeter_entry");
		snprintf (text, 20, "%f", glob->spectral->perim);
		gtk_entry_set_text (GTK_ENTRY (entry), text);
		
		entry = glade_xml_get_widget (glob->spectral->xmlspect, "spectral_offset_entry");
		snprintf (text, 20, "%f", glob->spectral->offset);
		gtk_entry_set_text (GTK_ENTRY (entry), text);

		/* Set normalize checkbox */
		gtk_toggle_button_set_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_norm_button")),
			glob->spectral->normalize);

		/* Let's start with the most basic graph */
		spectral_staircase ();
		gtk_spect_vis_zoom_x_all (graph);
		gtk_spect_vis_zoom_y_all (graph);
		gtk_spect_vis_redraw (graph);
	}
}

/* This should be called whenever the resonances list changes in any way */
void spectral_resonances_changed ()
{
	guint numres;
	
	if (glob->numres < 3)
	{
		on_spectral_close_activate (NULL, NULL);
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "spectral_stat"), FALSE);
	}
	else
	{
		gtk_widget_set_sensitive (glade_xml_get_widget (gladexml, "spectral_stat"), TRUE);

		if ((!glob->spectral) || (!glob->spectral->xmlspect))
			return;

		/* Are there really at least two resonances for evaluation? */
		g_free (spectral_get_frequencies (&numres, FALSE));
		if (numres < 3)
		{
			spectral_too_few_resonances ();
			return;
		}

		switch (glob->spectral->view)
		{
			case SPECTRAL_WEYL:
				spectral_staircase ();
				break;
			case SPECTRAL_FLUC:
				spectral_nfluc ();
				break;
			case SPECTRAL_NND:
				spectral_nnd ();
				break;
			case SPECTRAL_INT_NND:
				spectral_integrated_nnd ();
				break;
			case SPECTRAL_S2:
				spectral_sigma2 ();
				break;
			case SPECTRAL_D3:
				spectral_delta3 ();
				break;
			case SPECTRAL_LENGTH:
				spectral_length ();
				break;
			case SPECTRAL_WIDTHS_HIST:
				spectral_widths_hist ();
				break;
		}

		gtk_spect_vis_redraw (GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis")));
	}
}

/* The user changed the viewport */
void spectral_handle_viewport_changed (GtkSpectVis *spectvis, gchar *zoomtype)
{
	gtk_spect_vis_redraw (spectvis);
}

/* Mark the selected point with its coordinates */
gint spectral_handle_value_selected (GtkSpectVis *spectvis, gdouble *xval, gdouble *yval)
{
	g_return_val_if_fail (glob->spectral, 0);

	gtk_spect_vis_mark_point (spectvis, *xval, *yval);

	return 0;
}

/* The user entered a new perimeter value */
gboolean spectral_on_area_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;

	g_return_val_if_fail (glob->spectral, FALSE);
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter)) return FALSE;
	
	if (sscanf (gtk_entry_get_text (GTK_ENTRY (entry)), "%lf", &value) != 1) return FALSE;

	glob->spectral->area = value;
	spectral_resonances_changed ();
	
	return FALSE;
}

/* The user entered a new offset value */
gboolean spectral_on_perim_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;

	g_return_val_if_fail (glob->spectral, FALSE);
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter)) return FALSE;
	
	if (sscanf (gtk_entry_get_text (GTK_ENTRY (entry)), "%lf", &value) != 1) return FALSE;

	glob->spectral->perim = value;
	spectral_resonances_changed ();
	
	return FALSE;
}

/* The user entered a new area value */
gboolean spectral_on_offset_changed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gdouble value;

	g_return_val_if_fail (glob->spectral, FALSE);
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter)) return FALSE;
	
	if (sscanf (gtk_entry_get_text (GTK_ENTRY (entry)), "%lf", &value) != 1) return FALSE;

	glob->spectral->offset = value;
	spectral_resonances_changed ();
	
	return FALSE;
}

/* View graph in normal or logarithmic scale */
gboolean on_spectral_scale_change (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkCheckMenuItem *item;
	GtkSpectVis *graph;

	if ((!glob->spectral) || (!glob->spectral->xmlspect))
		return FALSE;

	item = GTK_CHECK_MENU_ITEM (
			glade_xml_get_widget (glob->spectral->xmlspect, "spectral_normal_scale")
			);
	
	graph = GTK_SPECTVIS (
			glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis")
			);

	if (gtk_check_menu_item_get_active (item))
	{
		gtk_spect_vis_set_displaytype (graph, 'a');
		gtk_spect_vis_set_axisscale (graph, 0, 1);
	}
	else
	{
		gtk_spect_vis_set_displaytype (graph, 'l');
		/* gtk_spect_vis takes the square -> rescale y axis */
		gtk_spect_vis_set_axisscale (graph, 0, 2);
	}
	
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

/* Change graph type to be displayed */
gboolean on_spectral_view_change (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkCheckMenuItem *item;
	GtkSpectVis *graph;

	if ((!glob->spectral) || (!glob->spectral->xmlspect))
		return FALSE;

	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem)))
		/* This is the signal from the menuitem being deactivated */
		return FALSE;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_staircase"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->view = SPECTRAL_WEYL;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_fluctuations"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->view = SPECTRAL_FLUC;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_nnd"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->view = SPECTRAL_NND;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_int_nnd"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->view = SPECTRAL_INT_NND;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_s2"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->view = SPECTRAL_S2;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_d3"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->view = SPECTRAL_D3;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_length"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->view = SPECTRAL_LENGTH;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_widths_hist"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->view = SPECTRAL_WIDTHS_HIST;

	gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->spectral->xmlspect, "hbox19"), 
			TRUE);
	gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->spectral->xmlspect, "spectral_poisson_button"), 
			TRUE);
	gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->spectral->xmlspect, "spectral_goe_button"), 
			TRUE);
	gtk_widget_set_sensitive (
			glade_xml_get_widget (glob->spectral->xmlspect, "spectral_gue_button"), 
			TRUE);
			
	switch (glob->spectral->view)
	{
		case SPECTRAL_WEYL:
			gtk_widget_hide_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_theo_box"));
			gtk_widget_show_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_weyl_box"));
			spectral_staircase ();
			break;
		case SPECTRAL_FLUC:
			gtk_widget_hide_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_theo_box"));
			gtk_widget_show_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_weyl_box"));
			spectral_nfluc ();
			break;
		case SPECTRAL_NND:
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "label49"), TRUE);
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_bins_spin"), TRUE);
			gtk_widget_hide_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_weyl_box"));
			gtk_widget_show_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_theo_box"));
			spectral_nnd ();
			break;
		case SPECTRAL_INT_NND:
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "label49"), FALSE);
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_bins_spin"), FALSE);
			gtk_widget_hide_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_weyl_box"));
			gtk_widget_show_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_theo_box"));
			spectral_integrated_nnd ();
			break;
		case SPECTRAL_S2:
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "hbox19"), FALSE);
			gtk_widget_hide_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_weyl_box"));
			gtk_widget_show_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_theo_box"));
			spectral_sigma2 ();
			break;
		case SPECTRAL_D3:
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "hbox19"), FALSE);
			gtk_widget_hide_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_weyl_box"));
			gtk_widget_show_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_theo_box"));
			spectral_delta3 ();
			break;
		case SPECTRAL_LENGTH:
			gtk_widget_hide_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_weyl_box"));
			gtk_widget_hide_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_theo_box"));
			spectral_length ();
			break;
		case SPECTRAL_WIDTHS_HIST:
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "label49"), TRUE);
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_bins_spin"), TRUE);
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "label50"), FALSE);
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_poisson_button"), FALSE);
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_goe_button"), FALSE);
			gtk_widget_set_sensitive (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_gue_button"), FALSE);
			gtk_widget_hide_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_weyl_box"));
			gtk_widget_show_all (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_theo_box"));
			spectral_widths_hist ();
			break;
	}

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	gtk_spect_vis_zoom_x_all (graph);
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

/* Change resonance selection mode */
gboolean on_spectral_res_sel_change (GtkMenuItem *menuitem, gpointer user_data)
{
	GtkCheckMenuItem *item;

	if ((!glob->spectral) || (!glob->spectral->xmlspect))
		return FALSE;

	if (!gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (menuitem)))
		/* This is the signal from the menuitem being deactivated */
		return FALSE;

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_all_res"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->selection = 'a';

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_windowed_res"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->selection = 'w';

	item = GTK_CHECK_MENU_ITEM (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_checked_res"));
	if (gtk_check_menu_item_get_active (item))
		glob->spectral->selection = 's';

	spectral_resonances_changed ();

	return TRUE;
}

/* Do a Weyl fit if the GUI button is pressed */
gboolean on_spectral_fit_button_clicked (GtkWidget *button)
{
	GtkSpectVis *graph;
	gdouble *resonances;
	guint numres = 0;

	g_return_val_if_fail (glob->spectral, FALSE);

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	g_return_val_if_fail (graph, FALSE);

	resonances = spectral_get_frequencies (&numres, FALSE);
	g_return_val_if_fail (resonances, FALSE);

	spectral_fit_weyl (resonances, numres, glob->spectral->first_res - 1);
	g_free (resonances);

	spectral_resonances_changed ();

	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);

	return TRUE;
}

/* Adjust the number offset for the first resonance */
void on_spectral_1st_res_changed (GtkSpinButton *spinbutton, gpointer user_data)
{
	glob->spectral->first_res = gtk_spin_button_get_value_as_int (spinbutton);

	spectral_resonances_changed ();
}

/* The user entered a new value directly */
gboolean on_spectral_1st_res_keypressed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	gint value;

	g_return_val_if_fail (glob->spectral, FALSE);
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter)) return FALSE;
	
	if (sscanf (gtk_entry_get_text (GTK_ENTRY (entry)), "%i", &value) != 1) return FALSE;
	if (value < 1) return FALSE;

	glob->spectral->first_res = value;
	spectral_resonances_changed ();
	
	return FALSE;
}

/* Adjust the number of bins */
void on_spectral_bins_changed (GtkSpinButton *spinbutton, gpointer user_data)
{
	GtkSpectVis *graph;
	guint oldbins = glob->spectral->bins;
	
	glob->spectral->bins = gtk_spin_button_get_value_as_int (spinbutton);

	if (glob->spectral->bins != oldbins)
	{
		/* Do not call spectral_resonances_changed(), as I want
		 * to rezoom y (for spectral_nnd) if not normalized. */
		if (glob->spectral->view == SPECTRAL_NND)
			spectral_nnd ();
		else
			spectral_widths_hist ();

		graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
		if (!glob->spectral->normalize || (glob->spectral->view == SPECTRAL_WIDTHS_HIST))
			gtk_spect_vis_zoom_y_all (graph);
		gtk_spect_vis_redraw (graph);
	}
}

/* The user entered a new value directly */
gboolean on_spectral_bins_keypressed (GtkWidget *entry, GdkEventKey *event, gpointer user_data)
{
	GtkSpectVis *graph;
	gint value;

	g_return_val_if_fail (glob->spectral, FALSE);
	
	if ((event->keyval != GDK_Return) && (event->keyval != GDK_KP_Enter)) return FALSE;
	
	if (sscanf (gtk_entry_get_text (GTK_ENTRY (entry)), "%i", &value) != 1) return FALSE;
	if (value < 3) return FALSE;

	glob->spectral->bins = value;

	spectral_nnd ();
	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	if (!glob->spectral->normalize)
		gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);
	
	return FALSE;
}

/* Change the normalization of the NND and iNND spectra */
void on_spectral_norm_button_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	GtkSpectVis *graph;
	
	g_return_if_fail (glob->spectral);
	
	if (gtk_toggle_button_get_active (togglebutton))
		glob->spectral->normalize = TRUE;
	else
		glob->spectral->normalize = FALSE;

	if (glob->spectral->view == SPECTRAL_NND)
		spectral_nnd ();
	else if (glob->spectral->view == SPECTRAL_INT_NND)
		spectral_integrated_nnd ();
	else
		spectral_widths_hist ();

	graph = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));
	gtk_spect_vis_zoom_y_all (graph);
	gtk_spect_vis_redraw (graph);
}

/* Change the theory graph predictions to be displayed */
void on_spectral_predict_button_toggled (GtkToggleButton *togglebutton, gpointer user_data)
{
	GtkToggleButton *check;
	
	glob->spectral->theo_predict = 0;

	check = GTK_TOGGLE_BUTTON (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_poisson_button"));
	if (gtk_toggle_button_get_active (check))
		glob->spectral->theo_predict += 1;

	check = GTK_TOGGLE_BUTTON (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_goe_button"));
	if (gtk_toggle_button_get_active (check))
		glob->spectral->theo_predict += 2;

	check = GTK_TOGGLE_BUTTON (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_gue_button"));
	if (gtk_toggle_button_get_active (check))
		glob->spectral->theo_predict += 4;

	spectral_resonances_changed ();
}

/* Export data of current spectral statistic */
gboolean on_spectral_export_data (GtkMenuItem *menuitem, gpointer user_data)
{
	gchar *filename, *path = NULL;
	GtkSpectVisData *spectdata;
	gdouble *resonances;
	guint i, numres, len;
	FILE *file;

	g_return_val_if_fail (glob->spectral, FALSE);
	g_return_val_if_fail (glob->spectral->xmlspect, FALSE);

	if (glob->path)
		path = g_strdup_printf ("%s%c", glob->path, G_DIR_SEPARATOR);

	filename = get_filename ("Select datafile for export...", path, 2);
	g_free (path);

	if (!filename)
		return FALSE;

	spectdata = gtk_spect_vis_get_data_by_uid (
			GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis")), 
			1);

	g_return_val_if_fail (spectdata, FALSE);

	file = fopen (filename, "w");
	if (!file)
	{
		dialog_message ("Error: Could not open file %s\n", filename);
		g_free (filename);
		return FALSE;
	}

	resonances = spectral_get_frequencies (&numres, FALSE);

	len = spectdata->len;
	if (spectdata->type == 'h')
		i = 1;
	else
		i = 0;

	fprintf (file, "# Spectral statistics calculated by GWignerFit\r\n");
	if ((glob->data) && (glob->data->file))
		fprintf (file, "# spectrum data file: %s\r\n", glob->data->file);
	fprintf (file, "# Statistical data consisted of %i resonances from %f to %f GHz\r\n",
			numres, resonances[0]/1e9, resonances[numres-1]/1e9);
	fprintf (file, "# First resonance has been given level number %i\r\n#\r\n# Type: ", 
			glob->spectral->first_res);
	
	switch (glob->spectral->view)
	{
		case SPECTRAL_WEYL:
			fprintf (file, "staircase function\r\n");
			fprintf (file, "# Weyl coefficients: A=%g cm�, C=%g cm, const=%g\r\n#\r\n",
					glob->spectral->area, glob->spectral->perim, glob->spectral->offset);
			fprintf (file, "# f [Hz]\t\tnumber\r\n");
			break;
		case SPECTRAL_FLUC:
			fprintf (file, "fluctuating part of the staircase function\r\n");
			fprintf (file, "# Weyl coefficients: A=%g cm�, C=%g cm, const=%g\r\n#\r\n",
					glob->spectral->area, glob->spectral->perim, glob->spectral->offset);
			fprintf (file, "# f [Hz]\t\tN fluc\r\n");
			break;
		case SPECTRAL_NND:
			fprintf (file, "nearest neighbour spacing distribution (NND)");
			if (!glob->spectral->normalize)
				fprintf (file, ", not normalized");
			fprintf (file, "\r\n# Weyl coefficients: A=%g cm�, C=%g cm, const=%g\r\n",
					glob->spectral->area, glob->spectral->perim, glob->spectral->offset);
			fprintf (file, "# Number of bins: %i\r\n", glob->spectral->bins);
			fprintf (file, "# 's' is the left boundary of each bin\r\n#\r\n");
			if (glob->spectral->normalize)
				fprintf (file, "# s\t\tP(s)\r\n");
			else
				fprintf (file, "# s\t\tN(s)\r\n");
			len--;
			break;
		case SPECTRAL_INT_NND:
			fprintf (file, "integrated nearest neighbour spacing distribution");
			if (!glob->spectral->normalize)
				fprintf (file, ", not normalized");
			fprintf (file, "\r\n# Weyl coefficients: A=%g cm�, C=%g cm, const=%g\r\n#\r\n",
					glob->spectral->area, glob->spectral->perim, glob->spectral->offset);
			if (glob->spectral->normalize)
				fprintf (file, "# s\t\tint P(s)\r\n");
			else
				fprintf (file, "# s\t\tint N(s)\r\n");
			break;
		case SPECTRAL_S2:
			fprintf (file, "Sigma^2\r\n");
			fprintf (file, "# Weyl coefficients: A=%g cm�, C=%g cm, const=%g\r\n#\r\n",
					glob->spectral->area, glob->spectral->perim, glob->spectral->offset);
			fprintf (file, "# L\t\tSigma^2\r\n");
			break;
		case SPECTRAL_D3:
			fprintf (file, "Dyson-Metha statistics (Delta^3)\r\n");
			fprintf (file, "# Weyl coefficients: A=%g cm�, C=%g cm, const=%g\r\n#\r\n",
					glob->spectral->area, glob->spectral->perim, glob->spectral->offset);
			fprintf (file, "# L\t\tDelta^3\r\n");
			break;
		case SPECTRAL_LENGTH:
			fprintf (file, "length spectrum\r\n#\r\n");
			fprintf (file, "# x [m]\t\tAmplitude\r\n");
			break;
		case SPECTRAL_WIDTHS_HIST:
			fprintf (file, "widths histogram");
			if (!glob->spectral->normalize)
				fprintf (file, ", not normalized");
			fprintf (file, "\r\n# Number of bins: %i\r\n", glob->spectral->bins);
			fprintf (file, "# 'w' is the left boundary of each bin\r\n#\r\n");
			if (glob->spectral->normalize)
				fprintf (file, "# w [Hz]\t\tP(s)\r\n");
			else
				fprintf (file, "# w [Hz]\t\tN(s)\r\n");
			len--;
			break;
	}

	for (; i<len; i++)
		fprintf (file, "%f\t%g\r\n", spectdata->X[i], spectdata->Y[i].abs);

	fclose (file);
	g_free (resonances);
	return TRUE;
}
/* Export postscript of current graph */
gboolean on_spectral_export_ps (GtkMenuItem *menuitem, gpointer user_data)
{
	const gchar *filename, *title, *footer;
	const gchar *filen, *path;
	GArray *uids, *legend, *lt;
	GtkSpectVis *spectvis;
	GladeXML *xmldialog;
	GtkWidget *dialog;
	GtkToggleButton *toggle;
	gboolean showlegend = FALSE;
	gint linetype, uid;
	gchar *str, *basename, *xlabel = NULL, *ylabel = NULL, *ylabel2, pos;
	FILE *file;

	if ((!glob->spectral) || (!glob->spectral->xmlspect))
		return FALSE;

	spectvis = GTK_SPECTVIS (glade_xml_get_widget (glob->spectral->xmlspect, "spectral_spectvis"));

	/* Load the widgets */
	xmldialog = glade_xml_new (GLADEFILE, "export_ps_dialog", NULL);
	gtk_widget_hide_all (glade_xml_get_widget (xmldialog, "ps_overlay_check"));
	dialog = glade_xml_get_widget (xmldialog, "export_ps_dialog");

	/* Connect the select filename button */
	g_signal_connect (
			G_OBJECT (glade_xml_get_widget (xmldialog, "button10")), 
			"clicked",
			(GCallback) export_select_filename,
			xmldialog);

	/* Put some default text into the GtkEntry fields */
	if (glob->section)
	{
		basename = g_path_get_basename (glob->resonancefile);
		str = g_strdup_printf ("file: %s (%s)", basename, glob->section);
		g_free (basename);

		gtk_entry_set_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_footer_entry")),
				str);
	}
	else if ((glob->data) && (glob->data->file))
	{
		basename = g_path_get_basename (glob->data->file);
		str = g_strdup_printf ("file: %s", basename);
		g_free (basename);

		gtk_entry_set_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_footer_entry")),
				str);
	}

	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
	{
		path = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_path_entry")));
		filen = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_filename_entry")));
		if (path)
			filename = g_build_filename (path, filen, NULL);
		else
			filename = g_strdup (filen);

		title = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_title_entry")));
		footer = gtk_entry_get_text (GTK_ENTRY (
				glade_xml_get_widget (xmldialog, "ps_footer_entry")));

		/* Does the file already exist? */
		file = fopen (filename, "r");
		if (file)
		{
			basename = g_path_get_basename (filename);

			if (dialog_question ("File '%s' already exists, overwrite?", basename)
					!= GTK_RESPONSE_YES)
			{
				/* File exists, do not overwrite it. */
				fclose (file);
				g_free (basename);
				gtk_widget_destroy (dialog);
				statusbar_message ("Postscript export canceled by user");

				return FALSE;
			}

			fclose (file);
			g_free (basename);

			if (unlink (filename))
			{
				/* Something went wrong during file deletion */
				gtk_widget_destroy (dialog);
				dialog_message ("Cannot delete file %s.", filename);
				statusbar_message ("Postscript export canceled");
			}
		}
		else if (filename[0] != '|')
		{
			/* Is the file writeable? */

			/* Do this check only, if the first char isn't a pipe.
			 * If it is, the user wants to pass the ps to another
			 * application such as lpr.
			 */

			file = fopen (filename, "w");
			if (!file)
			{
				/* Cannot write to file -> abort */
				gtk_widget_destroy (dialog);
				dialog_message ("Cannot write to file %s.", filename);
				statusbar_message ("Postscript export canceled");

				return FALSE;
			}
			fclose (file);
		}

		uids = g_array_new (FALSE, FALSE, sizeof (guint));
		legend = g_array_new (FALSE, FALSE, sizeof (gchar*));
		lt = g_array_new (FALSE, FALSE, sizeof (gint));

		/* Should a legend be displayed? */
		toggle = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_legend_check"));
		if (gtk_toggle_button_get_active (toggle))
			showlegend = TRUE;

		/* And where? */
		if (gtk_toggle_button_get_active (
			GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_top_button"))))
			pos = 't';
		else
			pos = 'b';
		
		/* Always include the data graph */;
		uid = 1;
		g_array_append_val (uids, uid);
		if (showlegend)
			str = g_strdup_printf ("spectral data");
		else
			str = "";
		g_array_append_val (legend, str);

		linetype = 1;
		g_array_append_val (lt, linetype);

		toggle = GTK_TOGGLE_BUTTON (glade_xml_get_widget (xmldialog, "ps_theory_check"));
		if (gtk_toggle_button_get_active (toggle))
		{
			/* Include the theory graph */

			/* Poisson */
			uid = 2;
			g_array_append_val (uids, uid);
			if ((showlegend) && (glob->spectral->view == SPECTRAL_WEYL))
				str = g_strdup_printf ("Weyl fit");
			else if (showlegend)
				str = g_strdup_printf ("Poisson");
			else
				str = "";
			g_array_append_val (legend, str);
			linetype = 3;
			g_array_append_val (lt, linetype);

			/* GOE */
			uid = 3;
			g_array_append_val (uids, uid);
			if (showlegend)
				str = g_strdup_printf ("GOE");
			else
				str = "";
			g_array_append_val (legend, str);
			linetype = 4;
			g_array_append_val (lt, linetype);

			/* Poisson */
			uid = 4;
			g_array_append_val (uids, uid);
			if (showlegend)
				str = g_strdup_printf ("GUE");
			else
				str = "";
			g_array_append_val (legend, str);
			linetype = 2;
			g_array_append_val (lt, linetype);
		}

		/* Choose a xlabel */
		switch (glob->spectral->view)
		{
			case SPECTRAL_WEYL:
				ylabel = g_strdup_printf ("N(f)");
			case SPECTRAL_FLUC:
				xlabel = g_strdup_printf ("frequency (GHz)");
				if (!ylabel)
					ylabel = g_strdup_printf ("N^{fluc}(f)");
				break;
			case SPECTRAL_NND:
				if (glob->spectral->normalize)
					ylabel = g_strdup_printf ("P(s)");
				else
					ylabel = g_strdup_printf ("N(s)");
			case SPECTRAL_INT_NND:
				xlabel = g_strdup_printf ("spacing");
				if (!ylabel)
				{
					if (glob->spectral->normalize)
						ylabel = g_strdup_printf ("{/Symbol=18 \362}P(s)");
					else
						ylabel = g_strdup_printf ("{/Symbol=18 \362}N(s)");
				}
				break;
			case SPECTRAL_S2:
				ylabel = g_strdup_printf ("{/Symbol S}(l)");
			case SPECTRAL_D3:
				xlabel = g_strdup_printf ("length");
				if (!ylabel)
					ylabel = g_strdup_printf ("{/Symbol D}(l)");
				break;
			case SPECTRAL_LENGTH:
				xlabel = g_strdup_printf ("length (m)");
				ylabel = g_strdup_printf ("intensity");
				break;
		}

		if (gtk_check_menu_item_get_active (
			GTK_CHECK_MENU_ITEM (
				glade_xml_get_widget (glob->spectral->xmlspect, "spectral_log_scale")
			)))
		{
			ylabel2 = g_strdup_printf ("%s (dB)", ylabel);
			g_free (ylabel);
			ylabel = ylabel2;
		}

		/* Export! */
		if (!gtk_spect_vis_export_ps (spectvis, uids, filename, title, 
					 xlabel, ylabel, footer, 
					 legend, pos, lt))
		{
			dialog_message ("Error: Could not create graph. Is gnuplot installed on your system?");
			if (filename[0] != '|')
				unlink (filename);
		}

		/* Tidy up */
		g_array_free (uids, TRUE);
		g_array_free (legend, TRUE);
		g_array_free (lt, TRUE);
		g_free (xlabel);
		g_free (ylabel);
	}

	gtk_widget_destroy (dialog);
	statusbar_message ("Postscript export successful");

	return TRUE;
}
