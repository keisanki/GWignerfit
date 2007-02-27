#include <math.h>
#include <stdio.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkmain.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkdrawingarea.h>

#include "gtkspectvis.h"
#include "structs.h"
#include "gnuplot_i.h"

/* Definitions */

#define COORD_BORDER_DIST	5
#define MIN_DATAPOINTS_VIEW	4	/* actually it is one more */
#define MIN_IMPULSES_VIEW	1	/* actually it is one more */
#define ADD_MARK_DISTANCE	20

#define g_marshal_value_peek_pointer(v)		g_value_get_pointer (v)
#define gtk_spect_vis_marshal_VOID__POINTER	g_cclosure_marshal_VOID__POINTER
#define gtk_spect_vis_marshal_VOID__VOID	g_cclosure_marshal_VOID__VOID

/* Signals */
enum
{
	VIEWPORT_CHANGED,
	VALUE_SELECTED,
	LAST_SIGNAL
};

/* Forward declarations */

/* public functions */
void 		gtk_spect_vis_redraw		(GtkSpectVis *spectvis);
gint		gtk_spect_vis_data_add		(GtkSpectVis *spectvis,
						 gdouble *X,
						 ComplexDouble *Y,
						 guint len,
						 GdkColor color,
						 gchar pos);
gboolean	gtk_spect_vis_data_update	(GtkSpectVis *spectvis, 
						 guint uid, 
						 gdouble *X,
						 ComplexDouble *Y,
						 guint len);
gboolean	gtk_spect_vis_request_id	(GtkSpectVis *spectvis,
						 guint oldid,
						 guint newid);
gboolean	gtk_spect_vis_data_remove	(GtkSpectVis *spectvis,
						 guint uid);
gboolean	gtk_spect_vis_set_data_color	(GtkSpectVis *spectvis,
						 guint uid,
						 GdkColor color);
GtkSpectVisData* gtk_spect_vis_get_data_by_uid	(GtkSpectVis *spectvis,
						 guint uid);
gboolean	gtk_spect_vis_zoom_x_all	(GtkSpectVis *spectvis);
gboolean	gtk_spect_vis_zoom_y_all	(GtkSpectVis *spectvis);
gboolean	gtk_spect_vis_zoom_x		(GtkSpectVis *spectvis,
						 gdouble center,
						 gdouble factor);
gboolean	gtk_spect_vis_zoom_x_to		(GtkSpectVis *spectvis,
						 gdouble min,
						 gdouble max);
gboolean	gtk_spect_vis_zoom_y		(GtkSpectVis *spectvis,
						 gdouble center,
						 gdouble factor);
void		gtk_spect_vis_set_axisscale	(GtkSpectVis *spectvis,
						 gdouble xscale,
						 gdouble yscale);
void		gtk_spect_vis_get_axisscale	(GtkSpectVis *spectvis,
						 gdouble *xscale,
						 gdouble *yscale);
void		gtk_spect_vis_set_displaytype	(GtkSpectVis *spectvis,
						 gchar type);
gboolean	gtk_spect_vis_set_graphtype	(GtkSpectVis *spectvis,
						 guint uid, gchar type);
guint		gtk_spect_vis_add_bar		(GtkSpectVis *spectvis,
						 gdouble pos,
						 gdouble width,
						 GdkColor color);
gboolean	gtk_spect_vis_remove_bar	(GtkSpectVis *spectvis,
						 guint uid);
void		gtk_spect_vis_set_visible_cursor(GtkSpectVis *spectvis);
gboolean	gtk_spect_vis_export_ps		(GtkSpectVis *spectvis,
						 GArray *uids,
						 const gchar *filename,
						 const gchar *title,
			 			 const gchar *xlabel,
						 const gchar *ylabel,
						 const gchar *footer,
						 GArray *legend,
						 const gchar legendpos,
						 GArray *lt);
void		gtk_spect_vis_mark_point	(GtkSpectVis *spectvis,
						 gdouble xval,
						 gdouble yval);
gint		gtk_spect_vis_polygon_add	(GtkSpectVis *spectvis,
						 gdouble *X,
						 gdouble *Y,
						 guint len,
						 GdkColor color,
						 gchar pos);
gboolean	gtk_spect_vis_polygon_remove	(GtkSpectVis *spectvis,
						 guint uid,
						 gboolean free_data);

/* Handling the GtkWidget */
static void	gtk_spect_vis_class_init	(GtkSpectVisClass *class);
static void	gtk_spect_vis_init		(GtkSpectVis *spectvis);
static void	gtk_spect_vis_destroy		(GtkObject *object);
static void	gtk_spect_vis_size_request	(GtkWidget *widget, 
						 GtkRequisition *requisition);
static gboolean	gtk_spect_vis_expose		(GtkWidget *widget, 
						 GdkEventExpose *event);
static gboolean	gtk_spect_vis_configure		(GtkWidget *widget, 
						 GdkEventConfigure *event);

/* Private functions for lowlevel stuff */
static guint	gtk_spect_vis_data_gen_uid	(GList *list);
static void	gtk_spect_vis_draw_coordinates	(GtkWidget *widget,
						 gboolean drawit);
static void	gtk_spect_vis_gen_axis		(gdouble min,
						 gdouble max,
						 guint steps,
						 gdouble *stepsize,
						 gdouble *firsttic,
						 gchar *format);
static void	gtk_spect_vis_draw_graphs	(GtkWidget *widget);
static gint	gtk_spect_vis_bars_compare	(gconstpointer a,
						 gconstpointer b);
static void	gtk_spect_vis_draw_bars		(GtkSpectVis *spectvis);

static void	gtk_spect_vis_pixel_to_units	(GtkSpectVis *spectvis,
						 gdouble xpix,
						 gdouble ypix,
						 gdouble *xunit,
						 gdouble *yunit);

static void	gtk_spect_vis_units_to_pixel	(GtkSpectVis *spectvis,
						 gdouble xunit,
						 gdouble yunit,
						 gint *xpix,
						 gint *ypix);

static void	gtk_spect_vis_draw_graphs_fast	(GtkSpectVis *spectvis,
						 GdkGC *gc,
						 GtkSpectVisData *data,
						 guint start,
						 guint stop,
						 gdouble xscale,
						 gdouble yscale);

static void	gtk_spect_vis_draw_impulses	(GtkSpectVis *spectvis,
						 GdkGC *gc,
						 GtkSpectVisData *data,
						 guint start,
						 guint stop,
						 gdouble xscale,
						 gdouble yscale);

static void	gtk_spect_vis_draw_polygons	(GtkSpectVis *spectvis,
						 GdkGC *gc,
						 gdouble xscale,
						 gdouble yscale);

static gdouble	gtk_spect_vis_cal_db		(ComplexDouble value);

static gboolean	gtk_spect_vis_button_press	(GtkWidget *widget,
						 GdkEventButton *event);
static gboolean	gtk_spect_vis_button_scroll	(GtkWidget *widget,
						 GdkEventScroll *event);
static void	gtk_spect_vis_viewport_changed	(GtkSpectVis *spectvis,
						 gchar *type);
static gint	gtk_spect_vis_motion_notify	(GtkWidget *widget,
						 GdkEventMotion *event);
static gboolean	gtk_spect_vis_leave_notify	(GtkWidget *widget,
						 GdkEventCrossing *event);

static void	gtk_spect_vis_set_invisible_cursor		(GdkWindow *window);

void		gtk_spect_vis_marshal_VOID__POINTER_POINTER 	(GClosure * closure,
								 GValue * return_value,
								 guint n_param_values,
								 const GValue * param_values,
								 gpointer invocation_hint,
								 gpointer marshal_data);

/* Local data */

static GtkWidgetClass *parent_class = NULL;
static guint spect_vis_signals[LAST_SIGNAL] = {0};

GType 
gtk_spect_vis_get_type ()
{
	static GType spect_vis_type = 0;

	if (!spect_vis_type)
	{
		static const GTypeInfo spect_vis_info =
		{
			sizeof (GtkSpectVisClass),
			NULL,
			NULL,
			(GClassInitFunc) gtk_spect_vis_class_init,
			NULL,
			NULL,
			sizeof (GtkSpectVis),
			0,
			(GInstanceInitFunc) gtk_spect_vis_init,
		};

		spect_vis_type = g_type_register_static (GTK_TYPE_DRAWING_AREA, "GtkSpectVis", &spect_vis_info, 0);
	}

	return spect_vis_type;
}

static void 
gtk_spect_vis_class_init (GtkSpectVisClass *class)
{
	GObjectClass   *gobject_class = G_OBJECT_CLASS (class);
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = (GtkObjectClass*) class;
	widget_class = (GtkWidgetClass*) class;

	parent_class = gtk_type_class (gtk_widget_get_type ());

	object_class->destroy = gtk_spect_vis_destroy;

	widget_class->expose_event = gtk_spect_vis_expose;
	widget_class->size_request = gtk_spect_vis_size_request;
	widget_class->configure_event = gtk_spect_vis_configure;
	widget_class->button_press_event = gtk_spect_vis_button_press;
	widget_class->scroll_event = gtk_spect_vis_button_scroll;
	widget_class->motion_notify_event = gtk_spect_vis_motion_notify;
	widget_class->leave_notify_event = gtk_spect_vis_leave_notify;

	class->viewport_changed = gtk_spect_vis_viewport_changed;
	
	spect_vis_signals[VIEWPORT_CHANGED] =
		g_signal_new ("viewport_changed",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			G_STRUCT_OFFSET (GtkSpectVisClass, viewport_changed),
			NULL, NULL,
			gtk_spect_vis_marshal_VOID__POINTER,
			G_TYPE_NONE, 1,
			G_TYPE_POINTER);

	spect_vis_signals[VALUE_SELECTED] =
		g_signal_new ("value_selected",
			G_TYPE_FROM_CLASS (gobject_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			G_STRUCT_OFFSET (GtkSpectVisClass, value_selected),
			NULL, NULL,
			gtk_spect_vis_marshal_VOID__POINTER_POINTER,
			G_TYPE_NONE, 2,
			G_TYPE_POINTER,
			G_TYPE_POINTER);
}

static void 
gtk_spect_vis_init (GtkSpectVis *spectvis)
{
	GtkWidget *widget = NULL;
	GdkPixmap *pixmap = NULL;

	widget = GTK_WIDGET (spectvis);
	gtk_widget_set_events (widget, GDK_EXPOSURE_MASK
		| GDK_LEAVE_NOTIFY_MASK
		| GDK_BUTTON_PRESS_MASK
		| GDK_BUTTON_RELEASE_MASK
		| GDK_POINTER_MOTION_MASK
		| GDK_POINTER_MOTION_HINT_MASK);
	
	gtk_widget_show (widget);
	
	spectvis->draw = widget;
	spectvis->pixmap = pixmap;

	spectvis->lastxpos = -1;
	spectvis->lastypos = -1;
	spectvis->cursorgc = NULL;
	spectvis->data = NULL;
	spectvis->view = NULL;
	spectvis->bars = NULL;
	spectvis->poly = NULL;
	spectvis->displaytype = 'a';
	spectvis->xAxisScale = 1.0;
	spectvis->yAxisScale = 1.0;

	//g_print ("inited\n");
}

GtkWidget* 
gtk_spect_vis_new ()
{
	GtkSpectVis *spectvis;

	spectvis = g_object_new (gtk_spect_vis_get_type (), NULL);

	return GTK_WIDGET (spectvis);
}

static void 
gtk_spect_vis_destroy (GtkObject *object)
{
	GtkSpectVis *spectvis;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GTK_IS_SPECTVIS (object));

	spectvis = GTK_SPECTVIS (object);

	if (spectvis->data)
	{
		g_list_foreach (spectvis->data, (GFunc) g_free, NULL);
		g_list_free (spectvis->data);
		spectvis->data = NULL;
	}
	
	if (spectvis->bars)
	{
		g_list_foreach (spectvis->bars, (GFunc) g_free, NULL);
		g_list_free (spectvis->bars);
		spectvis->bars = NULL;
	}
	
	if (spectvis->poly)
	{
		g_list_foreach (spectvis->poly, (GFunc) g_free, NULL);
		g_list_free (spectvis->poly);
		spectvis->poly = NULL;
	}
	
	g_free (spectvis->view);
	spectvis->view = NULL;

	if (spectvis->pixmap)
		g_object_unref (spectvis->pixmap);
	spectvis->pixmap = NULL;

	if (spectvis->cursorgc)
		g_object_unref (spectvis->cursorgc);
	spectvis->cursorgc = NULL;

	if (spectvis->draw)
		gtk_widget_destroy (spectvis->draw);
	spectvis->draw = NULL;

	if (GTK_OBJECT_CLASS (parent_class)->destroy)
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);

	//g_print("gtkspectvis destroyed %p\n", spectvis);
}

static void 
gtk_spect_vis_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
	requisition->width  = 300;
	requisition->height = 100;
}

static gboolean 
gtk_spect_vis_expose (GtkWidget *widget, GdkEventExpose *event)
{
	GtkSpectVis *spectvis;

	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SPECTVIS (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	spectvis = GTK_SPECTVIS (widget);

	if (event->count > 0)
		return FALSE;

	gdk_draw_drawable (widget->window,
			widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
			spectvis->pixmap,
			event->area.x, event->area.y,
			event->area.x, event->area.y,
			event->area.width, event->area.height);

	if (spectvis->lastxpos != -1)
	{
		gdk_draw_line (widget->window, spectvis->cursorgc, 
				0, spectvis->lastypos, 
				widget->allocation.width, spectvis->lastypos);
		gdk_draw_line (widget->window, spectvis->cursorgc, 
				spectvis->lastxpos, 0, 
				spectvis->lastxpos, widget->allocation.height);
	}

	//g_print("expose\n");

	return FALSE;
}

static gboolean 
gtk_spect_vis_configure (GtkWidget *widget, GdkEventConfigure *event)
{
	GtkSpectVis *spectvis;
	
	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (GTK_IS_SPECTVIS (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);

	spectvis = GTK_SPECTVIS (widget);

	if (spectvis->pixmap)
		g_object_unref (spectvis->pixmap);

	spectvis->pixmap = gdk_pixmap_new (widget->window,
			widget->allocation.width,
			widget->allocation.height,
			-1);

	gdk_draw_rectangle (spectvis->pixmap,
			widget->style->white_gc,
			TRUE,
			0, 0,
			widget->allocation.width,
			widget->allocation.height);

	if (!spectvis->cursorgc)
	{
		spectvis->cursorgc = gdk_gc_new (widget->window);
		gdk_gc_set_function (spectvis->cursorgc, GDK_INVERT);
	}

	gtk_spect_vis_redraw (GTK_SPECTVIS (widget));

	spectvis->lastxpos = spectvis->lastypos = -1;

	//g_print("configured\n");

	return TRUE;
}

static guint 
gtk_spect_vis_data_gen_uid (GList *list)
{
	guint uid;
	
	if (list == NULL) return 1;
	
	uid = ((GtkSpectVisData *) (list->data))->uid;

	while ((list = g_list_next (list)))
	{
		if (((GtkSpectVisData *) (list->data))->uid > uid)
			uid = ((GtkSpectVisData *) (list->data))->uid;
	}

	return ++uid;
}

gint 
gtk_spect_vis_data_add (GtkSpectVis *spectvis, gdouble *X, ComplexDouble *Y, guint len, GdkColor color, gchar pos)
{
	GList *datalist;
	GtkSpectVisData *data;
	guint i;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), -1);
	g_return_val_if_fail (len, -1);
	g_return_val_if_fail (X, -1);
	g_return_val_if_fail (Y, -1);
	g_return_val_if_fail ((pos == 'l') || (pos == 'f'), -1);

	data = g_new (GtkSpectVisData, 1);
	data->X = X;
	data->Y = Y;
	data->len = len;
	data->type = 'l';
	data->color = color;
	data->uid = gtk_spect_vis_data_gen_uid (spectvis->data);

	if (spectvis->view == NULL)
	{
		spectvis->view = g_new0 (GtkSpectVisViewport, 1);
		spectvis->view->xmin = X[0];
		spectvis->view->xmax = X[len-1];
	}
	
	i = 0;
	while ((i < len-1) && (data->X[i] < spectvis->view->xmin))
		i++;
	data->xmin_arraypos = i;
	
	i = data->len - 1;
	while ((i > 0    ) && (data->X[i] > spectvis->view->xmax))
		i--;
	data->xmax_arraypos = i;

	if (pos == 'l')
		/* append as last element */
		datalist = g_list_append (spectvis->data, data);
	else
		/* prepend as first element */
		datalist = g_list_prepend (spectvis->data, data);

	g_return_val_if_fail (datalist, -1);
	spectvis->data = datalist;

	return data->uid;
}

gboolean
gtk_spect_vis_data_update (GtkSpectVis *spectvis, guint uid, gdouble *X, ComplexDouble *Y, guint len)
{
	GList *datalist;
	GtkSpectVisData *data;
	guint i;

	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (uid != 0, FALSE);
	g_return_val_if_fail (len, -1);
	g_return_val_if_fail (X, -1);
	g_return_val_if_fail (Y, -1);

	datalist = spectvis->data;
	while ((datalist != NULL) && (((GtkSpectVisData *) (datalist->data))->uid != uid))
		datalist = g_list_next (datalist);

	if (datalist != NULL)
	{
		data = (GtkSpectVisData *) (datalist->data);
		data->X = X;
		data->Y = Y;
		data->len = len;

		i = len-1;
		while ((data->X[i] > spectvis->view->xmax) && (i > 0))
			i--;
		data->xmax_arraypos = i;

		i = 0;
		while ((data->X[i] < spectvis->view->xmin) && (i < len-1))
			i++;
		data->xmin_arraypos = i;
	}
	else
		return FALSE;

	return TRUE;
}

gboolean
gtk_spect_vis_request_id (GtkSpectVis *spectvis, guint oldid, guint newid)
{
	GList *datalist;
	GtkSpectVisData *data = NULL;
	gboolean found = FALSE;

	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (oldid != 0, FALSE);
	g_return_val_if_fail (newid != 0, FALSE);
	g_return_val_if_fail (spectvis->data, FALSE);

	if (oldid == newid)
		return TRUE;
	
	datalist = spectvis->data;
	do
	{
		if (((GtkSpectVisData *) (datalist->data))->uid == newid)
			found = TRUE;

		if (((GtkSpectVisData *) (datalist->data))->uid == oldid)
			data = (GtkSpectVisData *) datalist->data;
	}
	while ((datalist = g_list_next (datalist)));

	if ((found) || (!data))
		return FALSE;
	
	data->uid = newid;

	return TRUE;
}

gboolean
gtk_spect_vis_data_remove (GtkSpectVis *spectvis, guint uid)
{
	GList *datalist = spectvis->data;

	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (uid != 0, FALSE);

	while ((datalist != NULL) && (((GtkSpectVisData *) (datalist->data))->uid != uid))
		datalist = g_list_next (datalist);

	if (datalist != NULL)
	{
		g_free (datalist->data);
		spectvis->data = g_list_delete_link (spectvis->data, datalist);
	}
	else
		return FALSE;

	if (g_list_length (spectvis->data) == 0)
	{
		g_list_free (spectvis->data);
		spectvis->data = NULL;
	}

	return TRUE;
}

gboolean
gtk_spect_vis_set_data_color (GtkSpectVis *spectvis, guint uid, GdkColor color)
{
	GList *datalist;

	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (uid != 0, FALSE);

	datalist = spectvis->data;

	while ((datalist != NULL) && (((GtkSpectVisData *) (datalist->data))->uid != uid))
		datalist = g_list_next (datalist);

	if (datalist != NULL)
		((GtkSpectVisData *) (datalist->data))->color = color;
	else
		return FALSE;

	return TRUE;
}

GtkSpectVisData*
gtk_spect_vis_get_data_by_uid (GtkSpectVis *spectvis, guint uid)
{
	GList *datalist = spectvis->data;

	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), NULL);
	g_return_val_if_fail (uid != 0, NULL);

	while ((datalist != NULL) && (((GtkSpectVisData *) (datalist->data))->uid != uid))
		datalist = g_list_next (datalist);

	if (datalist != NULL)
		return (GtkSpectVisData *) (datalist->data);
	else
		return NULL;
}

gboolean
gtk_spect_vis_zoom_x_to (GtkSpectVis *spectvis, gdouble min, gdouble max)
{
	GtkSpectVisViewport *view = spectvis->view;
	GList *datalist, *searchlist;
	GtkSpectVisData *data;
	gboolean rangetest;
	guint i, pos, searchi, limit, *newbounds;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (spectvis->data, FALSE);

	if (view == NULL)
	{
		spectvis->view = g_new0 (GtkSpectVisViewport, 1);
		view = spectvis->view;
	}

	i = 0;
	datalist  = spectvis->data;
	newbounds = g_new (guint, 2*g_list_length(datalist));
	rangetest = FALSE;
	while (datalist != NULL)
	{
		data = (GtkSpectVisData *) (datalist->data);
		
		/* Has a previous dataset the same X-value pointer? */
		searchlist = g_list_previous (datalist);
		searchi  = i - 2;
		while ((searchlist) && (((GtkSpectVisData *) (searchlist->data))->X != data->X))
		{
			searchlist = g_list_previous (searchlist);
			searchi -= 2;
		}
		
		if (searchlist)
		{
			/* Found -> take known limits */
			newbounds[i  ] = newbounds[searchi  ];
			newbounds[i+1] = newbounds[searchi+1];
		}
		else
		{
			/* Not found -> search for limits */
			pos = 0;
			while ((data->X[pos] < min) && (pos < data->len-1))
				pos++;
			newbounds[i] = pos;
			
			pos = data->len - 1;
			while ((data->X[pos] > max) && (pos > 0))
				pos--;
			newbounds[i+1] = pos;

			if (data->type != 'i')
				limit = MIN_DATAPOINTS_VIEW;
			else
				limit = MIN_IMPULSES_VIEW;

			if ((newbounds[i+1]+1 != newbounds[i]) &&
			    (newbounds[i+1] - newbounds[i] >= limit))
				/* The first condition is FALSE if the graph has _no_ datapoint
				 * inside the new view and is needed as the newbounds is uint. */
				rangetest = TRUE;
		}

		i += 2;
		datalist = g_list_next (datalist);
	}

	if (!rangetest) 
	{
		/* No graph has more than the required number of points in the new viewport */
		g_free (newbounds);
		return FALSE;
	}

	i = 0;
	datalist = spectvis->data;
	while (datalist != NULL)
	{
		data = (GtkSpectVisData *) (datalist->data);

		data->xmin_arraypos = newbounds[i++];
		data->xmax_arraypos = newbounds[i++];

		datalist = g_list_next (datalist);
	}

	view->xmin = min;
	view->xmax = max;

	g_free (newbounds);
	return TRUE;
}

gboolean 
gtk_spect_vis_zoom_x_all (GtkSpectVis *spectvis)
{
	GtkSpectVisViewport *view = spectvis->view;
	GList *datalist = spectvis->data;
	GtkSpectVisData *data;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (spectvis->data, FALSE);

	if (view == NULL)
	{
		spectvis->view = g_new0 (GtkSpectVisViewport, 1);
		view = spectvis->view;
	}

	view->xmin =  1e30;
	view->xmax = -1e30;	
	
	while (datalist != NULL)
	{
		data = (GtkSpectVisData *) (datalist->data);

		if (data->X[0] < view->xmin)
			view->xmin = data->X[0];
		
		if (data->X[data->len-1] > view->xmax)
			view->xmax = data->X[data->len-1];

		data->xmin_arraypos = 0;
		data->xmax_arraypos = data->len - 1;

		datalist = g_list_next (datalist);
	}

	return TRUE;
}

gboolean 
gtk_spect_vis_zoom_y_all (GtkSpectVis *spectvis)
{
	int i;
	gdouble tmpval, tmp1, tmp2;
	ComplexDouble *ydata, val;
	GtkSpectVisViewport *view = spectvis->view;
	GtkSpectVisData *data;
	GList *datalist;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (spectvis->data, FALSE);

	view->ymin = 1e20;
	view->ymax = -1e20;

	datalist = spectvis->data;
	
	do {
		data = (GtkSpectVisData *) (datalist->data);
		ydata = data->Y;

		if ((data->X[data->xmax_arraypos] < view->xmin) ||
		    (data->X[data->xmin_arraypos] > view->xmax))
			continue;

		/* Special calculation for impulse display */
		if (data->type == 'i')
		{
			if (spectvis->displaytype == 'l')
			{
				for (i=data->xmin_arraypos; i<=data->xmax_arraypos; i++)
				{
					val.abs = ydata[i].re;
					tmp1 = gtk_spect_vis_cal_db (val);
					val.abs = ydata[i].im;
					tmp2 = gtk_spect_vis_cal_db (val);

					if (tmp1 < view->ymin) view->ymin = tmp1;
					if (tmp1 > view->ymax) view->ymax = tmp1;
					if (tmp2 < view->ymin) view->ymin = tmp2;
					if (tmp2 > view->ymax) view->ymax = tmp2;
				}
			}
			else
			{
				for (i=data->xmin_arraypos; i<=data->xmax_arraypos; i++)
				{
					if (ydata[i].re < view->ymin)
						view->ymin = ydata[i].re;
					if (ydata[i].im > view->ymax)
						view->ymax = ydata[i].im;
				}
			}
			continue;
		}

		switch (spectvis->displaytype)
		{
			case 'r':
				for (i=data->xmin_arraypos; i<=data->xmax_arraypos; i++)
				{
					if (ydata[i].re < view->ymin)
						view->ymin = ydata[i].re;
					if (ydata[i].re > view->ymax)
						view->ymax = ydata[i].re;
				}
				break;
			case 'i':
				for (i=data->xmin_arraypos; i<=data->xmax_arraypos; i++)
				{
					if (ydata[i].im < view->ymin)
						view->ymin = ydata[i].im;
					if (ydata[i].im > view->ymax)
						view->ymax = ydata[i].im;
				}
				break;
			case 'p':
				for (i=data->xmin_arraypos; i<=data->xmax_arraypos; i++)
				{
					tmpval = atan2 (ydata[i].im, ydata[i].re);
					if (tmpval < view->ymin)
						view->ymin = tmpval;
					if (tmpval > view->ymax)
						view->ymax = tmpval;
				}
				break;
			case 'l':
				for (i=data->xmin_arraypos; i<=data->xmax_arraypos; i++)
				{
					tmpval = gtk_spect_vis_cal_db (ydata[i]);
					if (tmpval < view->ymin)
						view->ymin = tmpval;
					if (tmpval > view->ymax)
						view->ymax = tmpval;
				}
				break;
			default:
				for (i=data->xmin_arraypos; i<=data->xmax_arraypos; i++)
				{
					if (ydata[i].abs < view->ymin)
						view->ymin = ydata[i].abs;
					if (ydata[i].abs > view->ymax)
						view->ymax = ydata[i].abs;
				}
		}
	} while ((datalist = g_list_next (datalist)));

	view->ymax = view->ymax + 0.03 * (view->ymax - view->ymin);
	view->ymin = view->ymin - 0.03 * (view->ymax - view->ymin);

	return TRUE;
}

static void 
gtk_spect_vis_draw_coordinates (GtkWidget *widget, gboolean drawit)
{
	GtkSpectVis *spectvis;
	PangoLayout *layout;
	GdkGC *gc;
	gdouble stepsize, ticpos, pixelscale, maxval;
	gchar format[20], valstring[20];
	guint steps, ticpixel, xleft, ybottom, xwidth, yheight;
	gint xsize, ysize, flag, xextraspace, yextraspace;

	g_return_if_fail (GTK_IS_SPECTVIS (widget));
	spectvis = GTK_SPECTVIS (widget);

	g_return_if_fail (spectvis->view->xmax - spectvis->view->xmin > 0);
	g_return_if_fail (spectvis->view->ymax - spectvis->view->ymin > 0);

	if (spectvis->view == NULL) return;

	/* Calculate y axis parameters */
	yheight = widget->allocation.height-10-2*COORD_BORDER_DIST;
	steps = yheight / 80;
	if (steps < 2) steps = 2;
	gtk_spect_vis_gen_axis (
			spectvis->view->ymin / spectvis->yAxisScale, 
			spectvis->view->ymax / spectvis->yAxisScale,
			steps,
			&stepsize,
			&ticpos,
			format);

	stepsize *= spectvis->yAxisScale;
	ticpos   *= spectvis->yAxisScale;

	gc = widget->style->black_gc;

	/* Create a test string for extraspace calculation */
	maxval = ticpos;
	while (maxval+stepsize < spectvis->view->ymax)
		maxval += stepsize;
	if (abs(maxval)*(maxval<0?10:1) < abs(ticpos)*(ticpos<0?10:1))
		maxval = ticpos;
	g_snprintf (valstring, 19, format, maxval / spectvis->yAxisScale);
	layout = gtk_widget_create_pango_layout (widget, valstring);
	pango_layout_get_pixel_size (layout, &xsize, &ysize);
	xextraspace = xsize + 0;
	yextraspace = ysize + 0;

	/* Calculate sizes */
	xwidth  = widget->allocation.width -xextraspace-2*COORD_BORDER_DIST;
	yheight = widget->allocation.height-yextraspace-2*COORD_BORDER_DIST;
	xleft   = COORD_BORDER_DIST + xextraspace;
	ybottom = COORD_BORDER_DIST + yheight;

	/* And remember them for later usage */
	spectvis->view->graphboxwidth  = xwidth;
	spectvis->view->graphboxheight = yheight;
	spectvis->view->graphboxxoff   = xleft;
	spectvis->view->graphboxyoff   = ybottom;

	/* Calculation of the view parameters is enough */
	if (drawit == FALSE)
		return;
	
	/* Draw the big rectangle */
	gdk_draw_rectangle (spectvis->pixmap, gc, FALSE, 
			xextraspace+COORD_BORDER_DIST, 
			COORD_BORDER_DIST,
			xwidth, yheight);
	
	/* Draw main y axis tics */
	pixelscale = yheight / (spectvis->view->ymax - spectvis->view->ymin);
	while (ticpos < spectvis->view->ymax)
	{
		g_snprintf (valstring, 19, format, ticpos / spectvis->yAxisScale);

		/* Allow for rounding errors around zero */
		if ((fabs(ticpos/spectvis->yAxisScale) < 1e-10) && (stepsize/spectvis->yAxisScale > 1e-6))
			g_snprintf (valstring, 19, format, 0.0);

		layout = gtk_widget_create_pango_layout (widget, valstring);
		pango_layout_get_pixel_size (layout, &xsize, &ysize);

		ticpixel = (guint) (ybottom - pixelscale*(ticpos-spectvis->view->ymin));
		ticpos += stepsize;

		if ( ((gint) ticpixel < (gint) (ybottom-yheight)) || (ticpixel > ybottom) )
			continue;

		gdk_draw_layout (spectvis->pixmap, gc,
				(gint) (xleft-xsize-1),
				(gint) (ticpixel - ysize/2),
				layout);

		gdk_draw_line (spectvis->pixmap, gc,
				xleft, ticpixel,
				xleft+8, ticpixel);

		gdk_draw_line (spectvis->pixmap, gc,
				xleft+xwidth, ticpixel,
				xleft+xwidth-8, ticpixel);
	}

	/* Draw minor y axis tics */
	flag = 0;
	while (ticpos > spectvis->view->ymin)
	{
		if (ticpos < spectvis->view->ymax)
		{
			ticpixel = (guint) (ybottom - pixelscale*(ticpos-spectvis->view->ymin));
			gdk_draw_line (spectvis->pixmap, gc,
					xleft, ticpixel,
					xleft+6-2*(flag%2), ticpixel);

			gdk_draw_line (spectvis->pixmap, gc,
					xleft+xwidth, ticpixel,
					xleft+xwidth-6+2*(flag%2), ticpixel);
		}

		ticpos -= stepsize/4;
		flag++;
	}

	/* Draw main x axis tics */
	steps = xwidth / 150;
	if (steps < 2) steps = 2;
	gtk_spect_vis_gen_axis (
			spectvis->view->xmin / spectvis->xAxisScale, 
			spectvis->view->xmax / spectvis->xAxisScale,
			steps,
			&stepsize,
			&ticpos,
			format);

	stepsize *= spectvis->xAxisScale;
	ticpos   *= spectvis->xAxisScale;

	pixelscale = xwidth / (spectvis->view->xmax - spectvis->view->xmin);
	while (ticpos < spectvis->view->xmax)
	{
		g_snprintf (valstring, 19, format, ticpos / spectvis->xAxisScale);

		/* Allow for rounding errors around zero */
		if ((fabs(ticpos/spectvis->xAxisScale) < 1e-10) && (stepsize/spectvis->xAxisScale > 1e-6))
			g_snprintf (valstring, 19, format, 0.0);
		
		layout = gtk_widget_create_pango_layout (widget, valstring);
		pango_layout_get_pixel_size (layout, &xsize, &ysize);

		ticpixel = (guint) (xleft + pixelscale*(ticpos-spectvis->view->xmin));
		ticpos += stepsize;

		if ( ((gint) ticpixel < (gint) xleft) || (ticpixel > xleft+xwidth) )
			continue;

		gdk_draw_layout (spectvis->pixmap, gc,
				(gint) (ticpixel-xsize/2.0+1),
				ybottom,
				layout);

		gdk_draw_line (spectvis->pixmap, gc,
				ticpixel, ybottom,
				ticpixel, ybottom-8);

		gdk_draw_line (spectvis->pixmap, gc,
				ticpixel, ybottom-yheight,
				ticpixel, ybottom-yheight+8);
	}

	/* Draw minor x axis tics */
	flag = 0;
	while (ticpos > spectvis->view->xmin)
	{
		if (ticpos < spectvis->view->xmax)
		{
			ticpixel = (guint) (xleft + pixelscale*(ticpos-spectvis->view->xmin));
			gdk_draw_line (spectvis->pixmap, gc,
					ticpixel, ybottom,
					ticpixel, ybottom-6+2*(flag%2));

			gdk_draw_line (spectvis->pixmap, gc,
					ticpixel, ybottom-yheight,
					ticpixel, ybottom-yheight+6-2*(flag%2));
		}

		ticpos -= stepsize/4;
		flag++;
	}
}

static void 
gtk_spect_vis_gen_axis (gdouble min, gdouble max, guint steps, gdouble *stepsize, gdouble *firsttic, gchar *format)
{
	double diff, exponent, powexpo, tmp;

	g_return_if_fail (steps > 0);
	diff = (max - min) / steps;
	g_return_if_fail (diff > 0);

	/* The order of the changes between steps */
	exponent = floor (log10(diff));
	powexpo = pow (10, exponent);

	/* Set the correct stepsize */
	if (diff/powexpo > 7.5) *stepsize = 10 * powexpo;
	else if (diff/powexpo > 3.5) *stepsize = 5 * powexpo;
	else if (diff/powexpo > 1.5) *stepsize = 2 * powexpo;
	else *stepsize = 1 * powexpo;

	/* Find the first tic above min */
	*firsttic = ceil (min / powexpo) * powexpo;
	/* tmp = the value of the last significant digit */
	tmp = ceil (min / powexpo) - (floor (min / powexpo / 10) * 10);
	if (*stepsize / powexpo == 2)
	{
		if      (tmp > 8.5) *firsttic = *firsttic + (10 - tmp) * powexpo;
		else if (tmp > 6.5) *firsttic = *firsttic + ( 8 - tmp) * powexpo;
		else if (tmp > 4.5) *firsttic = *firsttic + ( 6 - tmp) * powexpo;
		else if (tmp > 2.5) *firsttic = *firsttic + ( 4 - tmp) * powexpo;
		else if (tmp > 0.5) *firsttic = *firsttic + ( 2 - tmp) * powexpo;
		else *firsttic = *firsttic + (0 - tmp) * powexpo;
	}
	else if  (*stepsize / powexpo == 1)
	{
		/* *firsttic is already correct, do nothing */
	}
	else if  (*stepsize / powexpo == 10)
	{
		*firsttic = *firsttic + (0 - tmp) * powexpo;
	}
	else
	{
		if      (tmp > 5.5) *firsttic = *firsttic + (10 - tmp) * powexpo;
		else if (tmp > 0.5) *firsttic = *firsttic + ( 5 - tmp) * powexpo;
		else *firsttic = *firsttic + (0 - tmp) * powexpo;
	}

	if (*firsttic < min)
		*firsttic += *stepsize;

	/* How to output the numbers */
	if (exponent + (*stepsize == 10*powexpo ? 1:0) < 0)
		g_snprintf (format, 20, "%%.%df", 
				(gint) -exponent - (*stepsize == 10*powexpo ? 1:0) );
	else
		g_snprintf (format, 20, "%%g");
}

void
gtk_spect_vis_set_axisscale (GtkSpectVis *spectvis, gdouble xscale, gdouble yscale)
{
	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));
	
	if (xscale != 0)
		spectvis->xAxisScale = xscale;

	if (yscale != 0)
		spectvis->yAxisScale = yscale;
}

void
gtk_spect_vis_set_displaytype (GtkSpectVis *spectvis, gchar type)
{
	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));
	g_return_if_fail ( (type == 'a') || (type == 'r') || (type == 'i') 
			|| (type == 'p') || (type == 'l'));

	spectvis->displaytype = type;
}

gboolean
gtk_spect_vis_set_graphtype (GtkSpectVis *spectvis, guint uid, gchar type)
{
	GList *datalist;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (uid, FALSE);
	g_return_val_if_fail ( (type == 'l') || (type == 'm') 
			    || (type == 'h') || (type == 'i'), FALSE);

	/* Possible types:
	 * l: lines
	 * m: lines with datapoint markers
	 * h: histogram
	 * i: impulses (from Y.re till Y.im)
	 */

	datalist = spectvis->data;

	while ((datalist != NULL) && (((GtkSpectVisData *) (datalist->data))->uid != uid))
		datalist = g_list_next (datalist);

	if (datalist != NULL)
		((GtkSpectVisData *) (datalist->data))->type = type;
	else
		return FALSE;

	return TRUE;
}

static void
gtk_spect_vis_pixel_to_units (GtkSpectVis *spectvis, gdouble xpix, gdouble ypix, gdouble *xunit, gdouble *yunit)
{
	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));
	g_return_if_fail (spectvis->view != NULL);

	*xunit = spectvis->view->xmin 
		+ (spectvis->view->xmax - spectvis->view->xmin) / (gdouble) spectvis->view->graphboxwidth 
		* (xpix - (gdouble) spectvis->view->graphboxxoff);
	*yunit = spectvis->view->ymin 
		+ (spectvis->view->ymax - spectvis->view->ymin) / (gdouble) spectvis->view->graphboxheight 
		* ((gdouble) spectvis->view->graphboxyoff - ypix);
	
	if (spectvis->displaytype == 'l')
		*yunit = pow(10, *yunit / 20);
}

static void
gtk_spect_vis_units_to_pixel (GtkSpectVis *spectvis, gdouble xunit, gdouble yunit, gint *xpix, gint *ypix)
{
	GtkSpectVisViewport *view;
	ComplexDouble val;
	
	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));
	g_return_if_fail (spectvis->view != NULL);

	view = spectvis->view;

	*xpix = view->graphboxxoff 
		+ (gdouble) view->graphboxwidth / (view->xmax - view->xmin) * (xunit - view->xmin);
	*ypix = view->graphboxyoff
		- (gdouble) view->graphboxheight / (view->ymax - view->ymin) * (yunit - view->ymin);

	if (spectvis->displaytype == 'l')
	{
		val.abs = yunit;
		*ypix = view->graphboxyoff
			- (gdouble) view->graphboxheight / (view->ymax - view->ymin) 
			* (gtk_spect_vis_cal_db (val) - view->ymin);
	}
}

void
gtk_spect_vis_get_axisscale (GtkSpectVis *spectvis, gdouble *xscale, gdouble *yscale)
{
	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));
	
	if (xscale) *xscale = spectvis->xAxisScale;
	if (yscale) *yscale = spectvis->yAxisScale;
}

void 
gtk_spect_vis_redraw (GtkSpectVis *spectvis)
{
	GtkWidget *widget;
	
	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));

	widget = GTK_WIDGET (spectvis);

	/* Clear the whole graph */
	gdk_draw_rectangle (spectvis->pixmap,
			widget->style->white_gc,
			TRUE,
			0, 0,
			widget->allocation.width,
			widget->allocation.height);

	if ((spectvis->view) &&
	    (spectvis->data) &&
	    (spectvis->view->xmax - spectvis->view->xmin > 0) &&
	    (spectvis->view->ymax - spectvis->view->ymin > 0))
	{
		/* Redraw each part */
		gtk_spect_vis_draw_coordinates (widget, FALSE);
		gtk_spect_vis_draw_bars (spectvis);
		gtk_spect_vis_draw_coordinates (widget, TRUE);
		gtk_spect_vis_draw_graphs (widget);
	}

	/* And copy the pixmap on the screen */
	gdk_draw_drawable (widget->window,
			widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
			spectvis->pixmap,
			0, 0,
			0, 0, 
			widget->allocation.width, widget->allocation.height);

	if (spectvis->lastxpos != -1)
	{
		gdk_draw_line (widget->window, spectvis->cursorgc, 0, 
				spectvis->lastypos, widget->allocation.width, spectvis->lastypos);
		gdk_draw_line (widget->window, spectvis->cursorgc, 
				spectvis->lastxpos, 0, spectvis->lastxpos, widget->allocation.height);
	}
}

static void 
gtk_spect_vis_draw_graphs (GtkWidget *widget)
{
	GtkSpectVis *spectvis;
	guint i, start, stop;
	gdouble phase, xscale, yscale;
	ComplexDouble *ydata;
	GtkSpectVisViewport *view;
	GtkSpectVisData *data;
	GdkPoint *polygon, *poly2;
	GList *datalist;
	GdkGC *gc;
	GdkRectangle *rect;

	g_return_if_fail (GTK_IS_SPECTVIS (widget));
	spectvis = GTK_SPECTVIS (widget);

	g_return_if_fail (spectvis->data);
	g_return_if_fail (spectvis->view != NULL);

	view = spectvis->view;
	datalist = spectvis->data;

	xscale = view->graphboxwidth  / (view->xmax - view->xmin);
	yscale = view->graphboxheight / (view->ymax - view->ymin);

	gc = gdk_gc_new (spectvis->pixmap);

	/* Create a mask so that the graph is only withing the graphbox */
	rect = g_new (GdkRectangle, 1);
	rect->x = view->graphboxxoff + 1;
	rect->y = view->graphboxyoff - view->graphboxheight;
	rect->width = view->graphboxwidth - 1;
	rect->height = view->graphboxheight;
	gdk_gc_set_clip_rectangle (gc, rect);

	while (datalist)
	{
		data = (GtkSpectVisData *) (datalist->data);
		ydata = data->Y;

		start = data->xmin_arraypos == 0 ? 0 : (data->xmin_arraypos - 1);
		stop  = data->xmax_arraypos >= data->len - 1 ? data->xmax_arraypos : (data->xmax_arraypos + 1);
		if (stop - start > 2 * view->graphboxwidth)
		{
			gtk_spect_vis_draw_graphs_fast (spectvis, gc, data, start, stop, xscale, yscale);
			datalist = g_list_next (datalist);
			continue;
		}

		if (data->type == 'i')
		{
			gtk_spect_vis_draw_impulses (spectvis, gc, data, start, stop, xscale, yscale);
			datalist = g_list_next (datalist);
			continue;
		}

		polygon = g_new (GdkPoint, data->xmax_arraypos - data->xmin_arraypos+3);
		for (i=start; i<=stop; i++)
			polygon[i-start].x = (data->X[i] - view->xmin) * xscale + view->graphboxxoff;

		gdk_gc_set_rgb_fg_color (gc, &(data->color));

		switch (spectvis->displaytype)
		{
			case 'r':
				for (i=start; i<=stop; i++)
					polygon[i-start].y = view->graphboxyoff 
						- (ydata[i].re - view->ymin) * yscale;
				break;
			case 'i':
				for (i=start; i<=stop; i++)
					polygon[i-start].y = view->graphboxyoff 
						- (ydata[i].im - view->ymin) * yscale;
				break;
			case 'p':
				for (i=start; i<=stop; i++) {
					phase = atan2 (ydata[i].im, ydata[i].re);
					polygon[i-start].y = view->graphboxyoff 
						- (phase - view->ymin) * yscale;
				}
				break;
			case 'l':
				for (i=start; i<=stop; i++)
					polygon[i-start].y = view->graphboxyoff 
						- (gtk_spect_vis_cal_db (ydata[i]) 
								- view->ymin) * yscale;
				break;
			default:
				for (i=start; i<=stop; i++)
					polygon[i-start].y = view->graphboxyoff 
						- (ydata[i].abs - view->ymin) * yscale;
		}

		/* Prevent variable over- and underruns */
		for (i=0; i<=stop-start; i++)
		{
			if (polygon[i].y > 10000)
				polygon[i].y = 10000;
			else 
			if (polygon[i].y < -10000)
				polygon[i].y = -10000;
		}

		if (data->type == 'h')
		{
			/* Draw a histogramm */
			poly2 = g_new (GdkPoint, 2*(data->xmax_arraypos - data->xmin_arraypos+3));
			for (i=0; i<stop-start; i++)
			{
				poly2[2*i  ].x = polygon[i  ].x;
				poly2[2*i  ].y = polygon[i  ].y;
				poly2[2*i+1].x = polygon[i+1].x;
				poly2[2*i+1].y = polygon[i  ].y;
			}
			poly2[2*(stop-start)].x = polygon[stop-start].x;
			poly2[2*(stop-start)].y = polygon[stop-start].y;

			gdk_draw_lines (spectvis->pixmap,
					gc,
					poly2,
					2*(stop-start)+1);

			g_free (poly2);
		}
		else
		{
			/* Connect all points with straight lines */
			gdk_draw_lines (spectvis->pixmap,
					gc,
					polygon,
					stop-start+1);

			if ((data->type == 'm') && 
			    (polygon[1].x - polygon[0].x > ADD_MARK_DISTANCE))
				/* Add marks if the datapoints are far enough apart */
				for (i=0; i<=stop-start; i++)
					gdk_draw_line (spectvis->pixmap, gc,
						polygon[i].x, polygon[i].y-3,
						polygon[i].x, polygon[i].y+3);
		}

		g_free (polygon);

		datalist = g_list_next (datalist);
	}
	
	/* Include any polygons */
	gtk_spect_vis_draw_polygons (spectvis, gc, xscale, yscale);

	g_object_unref (gc);
	g_free (rect);
}

static void
gtk_spect_vis_draw_graphs_fast (GtkSpectVis *spectvis, GdkGC *gc, GtkSpectVisData *data, 
                                guint start, guint stop, gdouble xscale, gdouble yscale)
{
	gdouble phase;
	ComplexDouble *ydata;
	GtkSpectVisViewport *view;
	guint i, j;
	gint *min, *max, *x;
	gint startpoint, value;

	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));
	view = spectvis->view;

	x   = g_new (gint, view->graphboxwidth + 1);
	min = g_new (gint, view->graphboxwidth + 1);
	max = g_new (gint, view->graphboxwidth + 1);

	startpoint = (int) ((data->X[start] - view->xmin) * xscale);
	g_return_if_fail (startpoint >= 0);

	for (i=0; i<=view->graphboxwidth; i++)
	{
		x  [i] = i + view->graphboxxoff;
		min[i] = G_MAXINT;
		max[i] = G_MININT;
	}

	ydata = data->Y;

	/* ydata should always be != NULL but who knows... */
	if (ydata == NULL) {
		g_free (x);
		g_free (min);
		g_free (max);
		g_return_if_fail (ydata != NULL);
	}

	gdk_gc_set_rgb_fg_color (gc, &(data->color));
	j = startpoint;

	for (i=0; i<=view->graphboxwidth; i++)
	{
		min[i] = G_MAXINT;
		max[i] = G_MININT;
	}

	switch (spectvis->displaytype)
	{
		case 'r':
			for (i=start; i<=stop; i++)
			{
				if ((int) ((data->X[i] - view->xmin) * xscale) > j) j++;

				value = (int) (view->graphboxyoff - (ydata[i].re - view->ymin) * yscale);
				if (value < min[j]) min[j] = value;
				if (value > max[j]) max[j] = value;

				if (min[j] == G_MAXINT) break;
			}
			break;
		case 'i':
			for (i=start; i<=stop; i++)
			{
				if ((int) ((data->X[i] - view->xmin) * xscale) > j) j++;

				value = (int) (view->graphboxyoff - (ydata[i].im - view->ymin) * yscale);
				if (value < min[j]) min[j] = value;
				if (value > max[j]) max[j] = value;

				if (min[j] == G_MAXINT) break;
			}
			break;
		case 'p':
			for (i=start; i<=stop; i++)
			{
				if ((int) ((data->X[i] - view->xmin) * xscale) > j) j++;

				phase = atan2 (ydata[i].im, ydata[i].re);
				value = (int) (view->graphboxyoff - (phase - view->ymin) * yscale);
				if (value < min[j]) min[j] = value;
				if (value > max[j]) max[j] = value;

				if (min[j] == G_MAXINT) break;
			}
			break;
		case 'l':
			for (i=start; i<=stop; i++)
			{
				if ((int) ((data->X[i] - view->xmin) * xscale) > j) j++;

				value = (int) (view->graphboxyoff - 
						(gtk_spect_vis_cal_db (ydata[i]) 
						 - view->ymin) * yscale);
				if (value < min[j]) min[j] = value;
				if (value > max[j]) max[j] = value;

				if (min[j] == G_MAXINT) break;
			}
			break;
		default:
			for (i=start; i<=stop; i++)
			{
				/* Values belong to a new pixel */
				if ((int) ((data->X[i] - view->xmin) * xscale) > j) j++;

				/* Determine maximum and minimum for this pixel */
				value = (int) (view->graphboxyoff - (ydata[i].abs - view->ymin) * yscale);
				if (value < min[j]) min[j] = value;
				if (value > max[j]) max[j] = value;

				/* End of values, let's get out of here */
				if (min[j] == G_MAXINT) break;
			}
	}

	/* "j" should now be the last position plus one in the arrays where
	 * line information is stored. If it is _not_ bigger or equal than the
	 * startpoint something must have gone grossly wrong. */
	g_return_if_fail ((j >= startpoint) && (j > 0));
	
	/* Prevent variable over- and underruns */
	for (i=startpoint; i < j; i++)
	{
		if (min[i] < 0) 
			min[i] = max[i] = 0;
		if (max[i] > 10000)
			min[i] = max[i] = 10000;
	}

	/* The first vertical line */
	gdk_draw_line (spectvis->pixmap,
		       gc,
		       x[startpoint], min[startpoint],
		       x[startpoint], max[startpoint]);

	/* The normal vertical lines plus connections between them */
	for (i=startpoint+1; i < j-1; i++)
	{
		gdk_draw_line (spectvis->pixmap,
			       gc,
			       x[i], min[i],
			       x[i], max[i]);
		gdk_draw_line (spectvis->pixmap,
			       gc,
			       x[i], max[i],
			       x[i+1], min[i+1]);
	}

	/* The final vertical line */
	gdk_draw_line (spectvis->pixmap,
		       gc,
		       x[j-1], min[j-1],
		       x[j-1], max[j-1]);

	g_free (x);
	g_free (min);
	g_free (max);
}

static void
gtk_spect_vis_draw_impulses (GtkSpectVis *spectvis, GdkGC *gc, GtkSpectVisData *data, 
                             guint start, guint stop, gdouble xscale, gdouble yscale)
{
	ComplexDouble *ydata, val;
	GtkSpectVisViewport *view;
	guint i, x;
	gint ymin, ymax, tmp;

	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));
	view = spectvis->view;

	ydata = data->Y;
	g_return_if_fail (ydata);

	gdk_gc_set_rgb_fg_color (gc, &(data->color));

	for (i=start; i<=stop; i++)
	{
		x = (data->X[i] - view->xmin) * xscale + view->graphboxxoff;
		if (spectvis->displaytype == 'l')
		{
			val.abs = ydata[i].re;
			ymin = view->graphboxyoff - (gtk_spect_vis_cal_db(val) - view->ymin) * yscale;
			val.abs = ydata[i].im;
			ymax = view->graphboxyoff - (gtk_spect_vis_cal_db(val) - view->ymin) * yscale;
		}
		else
		{
			ymin = view->graphboxyoff - (ydata[i].re - view->ymin) * yscale;
			ymax = view->graphboxyoff - (ydata[i].im - view->ymin) * yscale;
		}

		if (ymin > ymax)
		{
			tmp  = ymin;
			ymin = ymax;
			ymax = tmp;
		}

		if (ymin > 10000) ymin = 10000;
		if (ymin <     0) ymin =     0;
		if (ymax > 10000) ymax = 10000;
		if (ymax <     0) ymax =     0;
		
		gdk_draw_line (spectvis->pixmap, gc,
				x, ymin,
				x, ymax);
	}
}

static void
gtk_spect_vis_draw_polygons (GtkSpectVis *spectvis, GdkGC *gc, gdouble xscale, gdouble yscale)
{
	GtkSpectVisViewport *view;
	GtkSpectVisPolygon *data;
	GList *polylist;
	GdkPoint *polygon;
	guint i;

	if (!spectvis->poly)
		return;

	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));

	view     = spectvis->view;
	polylist = spectvis->poly;

	while (polylist)
	{
		data = (GtkSpectVisPolygon *) (polylist->data);

		polygon = g_new (GdkPoint, data->len);
		
		for (i=0; i<data->len; i++)
		{
			polygon[i].x = view->graphboxxoff + (data->X[i] - view->xmin) * xscale;
			polygon[i].y = view->graphboxyoff - (data->Y[i] - view->ymin) * yscale;

			if (polygon[i].x >  10000) polygon[i].x =  10000;
			if (polygon[i].x < -10000) polygon[i].x = -10000;
			if (polygon[i].y >  10000) polygon[i].y =  10000;
			if (polygon[i].y < -10000) polygon[i].y = -10000;
		}

		gdk_gc_set_rgb_fg_color (gc, &(data->color));
		gdk_draw_lines (spectvis->pixmap, gc, polygon, data->len);

		g_free (polygon);

		polylist = g_list_next (polylist);
	}
}

static gdouble
gtk_spect_vis_cal_db (ComplexDouble value)
{
	if (value.abs < 0)
		value.abs *= -1.0;

	if (value.abs > 0)
	{
		/* 20/ln(10) = 8.68588963807 */
		return 8.68588963807 * log (value.abs);
	}
	else
		return -100.0;
}

guint
gtk_spect_vis_add_bar (GtkSpectVis *spectvis, gdouble pos, gdouble width, GdkColor color)
{
	GList *barlist;
	GtkSpectVisBar *bar;

	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), -1);

	barlist = spectvis->bars;

	bar = g_new (GtkSpectVisBar, 1);
	bar->pos  = pos;
	bar->width = width;
	bar->col  = color;
	bar->uid  = gtk_spect_vis_data_gen_uid (barlist);

	barlist = g_list_insert_sorted (barlist, bar, gtk_spect_vis_bars_compare);

	g_return_val_if_fail (barlist, -1);
	spectvis->bars = barlist;

	return bar->uid;
}

gboolean
gtk_spect_vis_remove_bar (GtkSpectVis *spectvis, guint uid)
{
	GList *bar;

	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (uid != 0, FALSE);

	bar = spectvis->bars;

	while ((bar != NULL) && (((GtkSpectVisBar *) (bar->data))->uid != uid))
		bar = g_list_next (bar);

	if (bar != NULL)
	{
		g_free (bar->data);
		spectvis->bars = g_list_delete_link (spectvis->bars, bar);
	}
	else
		return FALSE;

	return TRUE;
}

static gint
gtk_spect_vis_bars_compare (gconstpointer a, gconstpointer b)
{
	const GtkSpectVisBar *b1 = a;
	const GtkSpectVisBar *b2 = b;

	return b2->width - b1->width;
}

static void
gtk_spect_vis_draw_bars (GtkSpectVis *spectvis)
{
	GList *barpointer;
	GtkSpectVisBar *bar;
	gint xpix, xpixplus, xpixminus, ypix;
	gdouble pos, fade, diff, x1, x2, y;
	GdkColor color;
	GdkGC *gc;
	
	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));

	gc = gdk_gc_new(spectvis->pixmap);
	
	barpointer = spectvis->bars;

	while (barpointer != NULL)
	{
		bar = barpointer->data;
		
		if (bar->width == 0)
		{
			gdk_gc_set_rgb_fg_color (gc, &(bar->col));
			gtk_spect_vis_units_to_pixel (spectvis, bar->pos, 0, &xpix, &ypix);

			if ((xpix > spectvis->view->graphboxxoff) &&
			    (xpix < spectvis->view->graphboxxoff + spectvis->view->graphboxwidth))
				gdk_draw_line (spectvis->pixmap,
					       gc,
					       xpix, spectvis->view->graphboxyoff,
					       xpix, spectvis->view->graphboxyoff - spectvis->view->graphboxheight);
		}
		else
		{
			gtk_spect_vis_pixel_to_units (spectvis, 0, 0, &x1, &y);
			gtk_spect_vis_pixel_to_units (spectvis, 1, 0, &x2, &y);
			diff = x2 - x1;

			gtk_spect_vis_units_to_pixel (spectvis, bar->pos, 0, &xpixplus, &ypix);
			xpixminus = xpixplus;

			for (pos=0; pos<=1.5*bar->width; pos+=diff)
			{
				fade = 1 / ( pow(2*pos/bar->width,2) + 1 );
				color.red   = 65535 - (65535 - bar->col.red  ) * fade;
				color.green = 65535 - (65535 - bar->col.green) * fade;
				color.blue  = 65535 - (65535 - bar->col.blue ) * fade;
				gdk_gc_set_rgb_fg_color (gc, &color);

				if ((xpixplus > spectvis->view->graphboxxoff) &&
				    (xpixplus < spectvis->view->graphboxxoff + spectvis->view->graphboxwidth))
					gdk_draw_line (spectvis->pixmap,
						       gc,
						       xpixplus, spectvis->view->graphboxyoff,
						       xpixplus, spectvis->view->graphboxyoff - spectvis->view->graphboxheight);

				if ((xpixminus > spectvis->view->graphboxxoff) &&
				    (xpixminus < spectvis->view->graphboxxoff + spectvis->view->graphboxwidth))
					gdk_draw_line (spectvis->pixmap,
						       gc,
						       xpixminus, spectvis->view->graphboxyoff,
						       xpixminus, spectvis->view->graphboxyoff - spectvis->view->graphboxheight);
				
				xpixplus ++;
				xpixminus--;
			}

			/* Draw an extra line a bit darker at bar->pos */
			color.red   = bar->col.red   * 0.8;
			color.green = bar->col.green * 0.8;
			color.blue  = bar->col.blue  * 0.8;
			gdk_gc_set_rgb_fg_color (gc, &color);
			gtk_spect_vis_units_to_pixel (spectvis, bar->pos, 0, &xpix, &ypix);

			if ((xpix > spectvis->view->graphboxxoff) &&
			    (xpix < spectvis->view->graphboxxoff + spectvis->view->graphboxwidth))
				gdk_draw_line (spectvis->pixmap,
					       gc,
					       xpix, spectvis->view->graphboxyoff,
					       xpix, spectvis->view->graphboxyoff - spectvis->view->graphboxheight);
		}

		barpointer = g_list_next (barpointer);
	}
}

gboolean
gtk_spect_vis_zoom_x (GtkSpectVis *spectvis, gdouble center, gdouble factor)
{
	gdouble newmin, newmax;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (spectvis->data, FALSE);

	newmin = center - (center - spectvis->view->xmin) * factor;
	newmax = center + (spectvis->view->xmax - center) * factor;

	return gtk_spect_vis_zoom_x_to (spectvis, newmin, newmax);
}

gboolean
gtk_spect_vis_zoom_y (GtkSpectVis *spectvis, gdouble center, gdouble factor)
{
	GtkSpectVisViewport *view;
	ComplexDouble val;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);

	view = spectvis->view;

	if (spectvis->displaytype == 'l')
	{
		val.abs = center;
		center = gtk_spect_vis_cal_db (val);
	}

	view->ymin = center - (center - view->ymin) * factor;
	view->ymax = center + (view->ymax - center) * factor;

	return TRUE;
}

static gboolean 
gtk_spect_vis_button_press (GtkWidget *widget, GdkEventButton *event)
{
	gdouble xval, yval, xmin, xmax;
	GtkSpectVisViewport *view;
	GtkSpectVis *spectvis;
	GList *datalist;

	g_return_val_if_fail (GTK_IS_SPECTVIS (widget), FALSE);
	g_return_val_if_fail (event != NULL, FALSE);
	spectvis = GTK_SPECTVIS (widget);

	g_return_val_if_fail (spectvis->draw != NULL, FALSE);

	if (spectvis->data == NULL) return FALSE;

	view = spectvis->view;
	datalist = spectvis->data;
	gtk_spect_vis_pixel_to_units (spectvis, event->x, event->y, &xval, &yval);
	
	if ((event->button == 2) && !(event->state & GDK_CONTROL_MASK))
	{
		xmin = xval - (view->xmax - view->xmin)/2;
		xmax = xval + (view->xmax - view->xmin)/2;

		gtk_spect_vis_zoom_x_to (spectvis, xmin, xmax);

		gtk_spect_vis_zoom_y_all (spectvis);
		gtk_spect_vis_redraw (spectvis);

		g_signal_emit (spectvis, spect_vis_signals[VIEWPORT_CHANGED], 0, "s");
	}
	
	if ((event->button == 2) && (event->state & GDK_CONTROL_MASK))
	{
		gtk_spect_vis_zoom_x_all (spectvis);
		gtk_spect_vis_zoom_y_all (spectvis);

		g_signal_emit (spectvis, spect_vis_signals[VIEWPORT_CHANGED], 0, "a");
	}

	if (event->button == 1)
	{
		g_signal_emit (spectvis, spect_vis_signals[VALUE_SELECTED], 0, &xval, &yval);
	}

	/* Poor man's zoom: for those without a scrollwheel */
	
	if ((event->button == 3) && (event->state & GDK_SHIFT_MASK))
	{
		gtk_spect_vis_zoom_x (GTK_SPECTVIS (widget), xval, 0.80);
		gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (widget));
		g_signal_emit (spectvis, spect_vis_signals[VIEWPORT_CHANGED], 0, "i");
	}
	else if ((event->button == 3) && (event->state & GDK_CONTROL_MASK))
	{
		gtk_spect_vis_zoom_x (GTK_SPECTVIS (widget), xval, 1.20);
		gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (widget));
		g_signal_emit (spectvis, spect_vis_signals[VIEWPORT_CHANGED], 0, "o");
	}
	
	return TRUE;
}

static gboolean
gtk_spect_vis_button_scroll (GtkWidget *widget, GdkEventScroll *event)
{
	GtkSpectVis *spectvis;
	gdouble xval, yval;

	g_return_val_if_fail (GTK_IS_SPECTVIS (widget), FALSE);

	spectvis = GTK_SPECTVIS (widget);
	if (spectvis->data == NULL) return FALSE;
	
	gtk_spect_vis_pixel_to_units (spectvis, event->x, event->y, &xval, &yval);

	if (event->direction == GDK_SCROLL_UP)
	{
		if ((event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
		{
			gtk_spect_vis_zoom_y (GTK_SPECTVIS (widget), yval, 0.80);
			g_signal_emit (spectvis, spect_vis_signals[VIEWPORT_CHANGED], 0, "I");
		}
		else
		{
			gtk_spect_vis_zoom_x (GTK_SPECTVIS (widget), xval, 0.80);
			if (!(event->state & (GDK_SHIFT_MASK | GDK_LOCK_MASK)))
				/* Rezoom y only, if neither shift nor shift lock is prossed. */
				gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (widget));
			g_signal_emit (spectvis, spect_vis_signals[VIEWPORT_CHANGED], 0, "i");
		}
	}
	else if (event->direction == GDK_SCROLL_DOWN)
	{
		if ((event->state & GDK_CONTROL_MASK) == GDK_CONTROL_MASK)
		{
			gtk_spect_vis_zoom_y (GTK_SPECTVIS (widget), yval, 1.20);
			g_signal_emit (spectvis, spect_vis_signals[VIEWPORT_CHANGED], 0, "O");
		}
		else
		{
			gtk_spect_vis_zoom_x (GTK_SPECTVIS (widget), xval, 1.20);
			if (!(event->state & (GDK_SHIFT_MASK | GDK_LOCK_MASK)))
				/* Rezoom y only, if neither shift nor shift lock is prossed. */
				gtk_spect_vis_zoom_y_all (GTK_SPECTVIS (widget));
			g_signal_emit (spectvis, spect_vis_signals[VIEWPORT_CHANGED], 0, "o");
		}
	}
	else
		return FALSE;

	return TRUE;
}

static void
gtk_spect_vis_viewport_changed (GtkSpectVis *spectvis, gchar *type)
{
	//gtk_spect_vis_redraw (spectvis);
}

static gint
gtk_spect_vis_motion_notify (GtkWidget *widget, GdkEventMotion *event)
{
	GtkSpectVis *spectvis;
	GdkModifierType mask;
	gint x, y;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (widget), FALSE);

	spectvis = GTK_SPECTVIS (widget);

	if (spectvis->lastxpos == -1)
	{
		gtk_spect_vis_set_invisible_cursor (widget->window);
	}
	else
	{
		if (spectvis->lastypos != event->y)
			gdk_draw_line (widget->window, spectvis->cursorgc, 
					0, spectvis->lastypos, widget->allocation.width, spectvis->lastypos);
		if (spectvis->lastxpos != event->x)
			gdk_draw_line (widget->window, spectvis->cursorgc, 
					spectvis->lastxpos, 0, spectvis->lastxpos, widget->allocation.height);
	}

	if (spectvis->lastypos != event->y)
		gdk_draw_line (widget->window, spectvis->cursorgc, 
				0, event->y, widget->allocation.width, event->y);
	if (spectvis->lastxpos != event->x)
		gdk_draw_line (widget->window, spectvis->cursorgc, 
				event->x, 0, event->x, widget->allocation.height);

	spectvis->lastxpos = event->x;
	spectvis->lastypos = event->y;

	gdk_window_get_pointer (widget->window, &x, &y, &mask);

	return TRUE;
}

static gboolean
gtk_spect_vis_leave_notify (GtkWidget *widget, GdkEventCrossing *event)
{
	GtkSpectVis *spectvis;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (widget), FALSE);

	spectvis = GTK_SPECTVIS (widget);

	gtk_spect_vis_set_visible_cursor (spectvis);

	return TRUE;
}

void
gtk_spect_vis_set_visible_cursor (GtkSpectVis *spectvis)
{
	//GdkCursor *cursor;
	GtkWidget *widget;

	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));
	widget = GTK_WIDGET (spectvis);

	if (spectvis->lastxpos != -1)
	{
		gdk_draw_line (widget->window, spectvis->cursorgc, 
				0, spectvis->lastypos, widget->allocation.width, spectvis->lastypos);
		gdk_draw_line (widget->window, spectvis->cursorgc, 
				spectvis->lastxpos, 0, spectvis->lastxpos, widget->allocation.height);
		spectvis->lastxpos = spectvis->lastypos = -1;
	}

	//cursor = gdk_cursor_new (GDK_LEFT_PTR);
	//gdk_window_set_cursor (gtk_widget_get_root_window (GTK_WIDGET (spectvis)), cursor);
	gdk_window_set_cursor (widget->window, NULL);
	//gdk_cursor_destroy (cursor);
}

/* from gtkentry.c */
static void
gtk_spect_vis_set_invisible_cursor (GdkWindow *window)
{
	GdkBitmap *empty_bitmap;
	GdkCursor *cursor;
	GdkColor useless;
	char invisible_cursor_bits[] = { 0x0 };	

	useless.red = useless.green = useless.blue = 0;
	useless.pixel = 0;

	empty_bitmap = gdk_bitmap_create_from_data (window, invisible_cursor_bits, 1, 1);

	cursor = gdk_cursor_new_from_pixmap (empty_bitmap,
			empty_bitmap,
			&useless,
			&useless, 0, 0);

	gdk_window_set_cursor (window, cursor);

	gdk_cursor_unref (cursor);

	g_object_unref (empty_bitmap);
}

gboolean
gtk_spect_vis_export_ps (GtkSpectVis *spectvis, GArray *uids, 
			 const gchar *filename, const gchar *title, 
			 const gchar *xlabel, const gchar *ylabel,
			 const gchar *footer, GArray *legend, 
			 const gchar legendpos, GArray *lt)
{
	gnuplot_ctrl *g;
	gdouble *x, *y, from, to;
	guint start, stop, len;
	guint arraypos, i;
	gint linetype;
	ComplexDouble tmp;
	GtkSpectVisViewport *view;
	GtkSpectVisData *data;
	gchar *legendstr;

	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (spectvis->data, FALSE);
	g_return_val_if_fail (uids, FALSE);
	g_return_val_if_fail (uids->len > 0, FALSE);
	g_return_val_if_fail (filename, FALSE);
	g_return_val_if_fail ((legendpos == 't') || (legendpos == 'b') || (legendpos == 0), FALSE);

	/* Initialize the gnuplot stuff. */
	g = gnuplot_init ();
	g_return_val_if_fail (g, FALSE);

	/* Set some axis labels and other defaults */
	gnuplot_setstyle (g, "lines");

	view = spectvis->view;
	gnuplot_cmd(g, "set xrange [%e:%e]", view->xmin/spectvis->xAxisScale, view->xmax/spectvis->xAxisScale);
	gnuplot_cmd(g, "set yrange [%e:%e]", view->ymin/spectvis->yAxisScale, view->ymax/spectvis->yAxisScale);

	if (xlabel)
		gnuplot_set_xlabel (g, (gchar *) xlabel);

	if (ylabel == NULL)
		/* Provide some default ylabels */
		switch (spectvis->displaytype)
		{
			case 'r':
				gnuplot_set_ylabel (g, "real part of amplitude (a.u.)");
				break;
			case 'i':
				gnuplot_set_ylabel (g, "imaginary part of amplitude (a.u.)");
				break;
			case 'p':
				gnuplot_set_ylabel (g, "phase value of amplitude (a.u.)");
				break;
			case 'l':
				gnuplot_set_ylabel (g, "squared amplitude (dB)");
				break;
			default:
				gnuplot_set_ylabel (g, "absolute amplitude (a.u.)");
		}
	else
		gnuplot_set_ylabel (g, (gchar *) ylabel);

	/* Set the title if given. */
	if (title)
		gnuplot_cmd (g, "set title \"%s\"", title);

	/* Set the position of the legend. */
	if (legendpos == 'b')
		gnuplot_cmd (g, "set key bottom right");
	else if (legendpos == 0)
		gnuplot_cmd (g, "unset key");

	/* Add a graph for each id in uids. */
	for (arraypos=0; arraypos<uids->len; arraypos++)
	{
		data = gtk_spect_vis_get_data_by_uid (spectvis, g_array_index (uids, guint, arraypos));
		if (data == NULL)
			break;

		/* No need to plot all values, only those visible are needed. */
		start = data->xmin_arraypos == 0 ? 0 : (data->xmin_arraypos - 1);
		stop  = data->xmax_arraypos >= data->len - 1 ? data->xmax_arraypos : (data->xmax_arraypos + 1);

		if (data->type == 'i')
		{
			/* Draw impulses */
			linetype  = g_array_index (lt, gint, arraypos);
			tmp.re    = 0.0;
			tmp.im    = 0.0;
			for (i=start; i<=stop; i++)
			{
				if (spectvis->displaytype == 'l')
				{
					tmp.abs = data->Y[i].re;
					from = gtk_spect_vis_cal_db (tmp) / spectvis->yAxisScale;
					tmp.abs = data->Y[i].im;
					to = gtk_spect_vis_cal_db (tmp) / spectvis->yAxisScale;
				}
				else
				{
					from = data->Y[i].re / spectvis->yAxisScale;
					to   = data->Y[i].im / spectvis->yAxisScale;
				}
				
				/* Clip data range to selected range */
				from = from < view->ymin/spectvis->yAxisScale ? view->ymin/spectvis->yAxisScale : from;
				from = from > view->ymax/spectvis->yAxisScale ? view->ymax/spectvis->yAxisScale : from;
				to   = to   < view->ymin/spectvis->yAxisScale ? view->ymin/spectvis->yAxisScale : to  ;
				to   = to   > view->ymax/spectvis->yAxisScale ? view->ymax/spectvis->yAxisScale : to  ;

				gnuplot_cmd (g, "set arrow from %f,%f to %f,%f nohead lt %d",
						data->X[i] / spectvis->xAxisScale,
						from,
						data->X[i] / spectvis->xAxisScale,
						to,
						linetype
					    );
			}

			/* A graph cannot be made out of arrows alone -> Redraw the first
			 * stick by a "real" graph. */
			x = g_new (gdouble, 2);
			y = g_new (gdouble, 2);

			x[0] = data->X[i] / spectvis->xAxisScale;
			x[0] = data->Y[i].re / spectvis->yAxisScale;
			x[1] = data->X[i] / spectvis->xAxisScale;
			x[1] = data->Y[i].im / spectvis->yAxisScale;

			legendstr = g_array_index (legend, gchar*, arraypos);
			gnuplot_plot_xy (g, x, y, 2, legendstr, linetype);

			g_free (x);
			g_free (y);

			break;
		}

		x = g_new (gdouble, stop-start+1);
		y = g_new (gdouble, stop-start+1);

		for (i=start; i<=stop; i++)
			x[i-start] = data->X[i] / spectvis->xAxisScale;

		for (i=start; i<=stop; i++)
		{
			switch (spectvis->displaytype)
			{
				case 'r':
					y[i-start] = data->Y[i].re / spectvis->yAxisScale;
					break;
				case 'i':
					y[i-start] = data->Y[i].im / spectvis->yAxisScale;
					break;
				case 'p':
					y[i-start] = atan2 (data->Y[i].im, data->Y[i].re) / spectvis->yAxisScale;
					break;
				case 'l':
					y[i-start] = gtk_spect_vis_cal_db (data->Y[i]) / spectvis->yAxisScale;
					break;
				default:
					y[i-start] = data->Y[i].abs / spectvis->yAxisScale;
			}
		}
		len = stop-start+1;

		if (data->type == 'h')
		{
			/* Draw a histogramm */
			gnuplot_setstyle (g, "steps");
#if 0
			x2 = g_new (gdouble, 2*(stop-start)+1);
			y2 = g_new (gdouble, 2*(stop-start)+1);
			for (i=0; i<stop-start; i++)
			{
				x2[2*i  ] = x[i  ];
				y2[2*i  ] = y[i  ];
				x2[2*i+1] = x[i+1];
				y2[2*i+1] = y[i  ];
			}
			x2[2*(stop-start)] = x[stop-start];
			y2[2*(stop-start)] = y[stop-start];

			g_free (x);
			g_free (y);

			x = x2;
			y = y2;
			len = 2*(stop-start)+1;
#endif
		}

		linetype  = g_array_index (lt, gint, arraypos);
		legendstr = g_array_index (legend, gchar*, arraypos);
		gnuplot_plot_xy (g, x, y, len, legendstr, linetype);

		if (data->type == 'h')
			/* Set plot style back to lines */
			gnuplot_setstyle (g, "lines");

		g_free (x);
		g_free (y);
	}

	if (footer)
		gnuplot_cmd (g, "set label 1 \"%s\" at screen 0.99,0.015 right font \"Helvetica,9\"", footer);

	gnuplot_cmd (g, "set terminal postscript color solid");
	gnuplot_cmd (g, "set output \"%s\"", filename);
	gnuplot_cmd (g, "replot");

	/* Tidy up */
	gnuplot_close (g);

	return TRUE;
}

void
gtk_spect_vis_mark_point (GtkSpectVis *spectvis, gdouble xval, gdouble yval)
{
	GtkWidget *widget;
	PangoLayout *layout;
	GdkGC *gc;
	ComplexDouble dummy;
	gint xpos, ypos, xsize, ysize, xplus, yplus;
	gchar *str;
	gdouble exponent;
	
	g_return_if_fail (GTK_IS_SPECTVIS (spectvis));
	widget = GTK_WIDGET (spectvis);

	/* Center position of the cross */
	gtk_spect_vis_units_to_pixel (spectvis, xval, yval, &xpos, &ypos);

	/* Convert yval in case of logarithmic scale */
	if (spectvis->displaytype == 'l')
	{
		dummy.abs = yval;
		yval = gtk_spect_vis_cal_db (dummy);
	}
	
	yval /= spectvis->yAxisScale;
	xval /= spectvis->xAxisScale;

	/* Try to find a sensible precision for the coordinate labels */

	exponent = pow (10, floor (log10 (
				(spectvis->view->xmax - spectvis->view->xmin) 
				/ spectvis->view->graphboxwidth / spectvis->xAxisScale
				)
			));
	xval = floor (xval/exponent) * exponent;

	exponent = pow (10, floor (log10 (
				(spectvis->view->ymax - spectvis->view->ymin) 
				/ spectvis->view->graphboxheight / spectvis->yAxisScale
				)
			));
	yval = floor (yval/exponent) * exponent;

	str = g_strdup_printf ("(%g, %g)", xval, yval);

	gc = widget->style->black_gc;
	layout = gtk_widget_create_pango_layout (widget, str);
	pango_layout_get_pixel_size (layout, &xsize, &ysize);
	g_free (str);

	/* Hide the cursor cross lines */
	gtk_spect_vis_set_visible_cursor (spectvis);

	/* Draw a cross on the screen... */
	gdk_draw_line (widget->window, gc,
			xpos-3, ypos,
			xpos+3, ypos);

	gdk_draw_line (widget->window, gc,
			xpos, ypos-3,
			xpos, ypos+3);

	/* ...and the same for the backing pixmap */
	gdk_draw_line (spectvis->pixmap, gc,
			xpos-3, ypos,
			xpos+3, ypos);

	gdk_draw_line (spectvis->pixmap, gc,
			xpos, ypos-3,
			xpos, ypos+3);

	if (xpos+3+xsize > widget->allocation.width-COORD_BORDER_DIST-5)
		/* Too close to right border */
		xplus = -3 - xsize;
	else
		xplus = +3;

	if (ypos-1-ysize < COORD_BORDER_DIST+5)
		/* Too close to upper border */
		yplus = +1;
	else
		yplus = -1 - ysize;

	/* Draw the coordinate text on the screen and the pixmap */
	gdk_draw_layout (widget->window, gc,
			xpos + xplus, ypos + yplus,
			layout);
	gdk_draw_layout (spectvis->pixmap, gc,
			xpos + xplus, ypos + yplus,
			layout);
}

gint
gtk_spect_vis_polygon_add (GtkSpectVis *spectvis, gdouble *X, gdouble *Y, guint len, GdkColor color, gchar pos)
{
	GtkSpectVisPolygon *poly;
	GList *polylist;
	
	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), -1);
	g_return_val_if_fail (X, -1);
	g_return_val_if_fail (Y, -1);
	g_return_val_if_fail (len, -1);
	g_return_val_if_fail ((pos == 'l') || (pos == 'f'), -1);

	poly = g_new (GtkSpectVisPolygon, 1);
	poly->X = X;
	poly->Y = Y;
	poly->len = len;
	poly->color = color;
	poly->uid = gtk_spect_vis_data_gen_uid (spectvis->poly);

	if (spectvis->view == NULL)
	{
		spectvis->view = g_new0 (GtkSpectVisViewport, 1);
		spectvis->view->xmin = 0;
		spectvis->view->xmax = 0;
	}

	if (pos == 'l')
		/* append as last element */
		polylist = g_list_append (spectvis->poly, poly);
	else
		/* prepend as first element */
		polylist = g_list_prepend (spectvis->poly, poly);

	g_return_val_if_fail (polylist, -1);
	spectvis->poly = polylist;

	return poly->uid;
}

gboolean
gtk_spect_vis_polygon_remove (GtkSpectVis *spectvis, guint uid, gboolean free_data)
{
	GList *polylist = spectvis->poly;

	g_return_val_if_fail (GTK_IS_SPECTVIS (spectvis), FALSE);
	g_return_val_if_fail (uid != 0, FALSE);

	while ((polylist != NULL) && (((GtkSpectVisPolygon *) (polylist->data))->uid != uid))
		polylist = g_list_next (polylist);

	if (polylist != NULL)
	{
		if (free_data)
		{
			g_free (((GtkSpectVisPolygon*) polylist->data)->X);
			g_free (((GtkSpectVisPolygon*) polylist->data)->Y);
		}
		g_free (polylist->data);
		spectvis->poly = g_list_delete_link (spectvis->poly, polylist);
	}
	else
		return FALSE;

	if (g_list_length (spectvis->poly) == 0)
	{
		g_list_free (spectvis->poly);
		spectvis->poly = NULL;
	}

	return TRUE;
}

/********************* The marshal stuff ************************/

void
gtk_spect_vis_marshal_VOID__POINTER_POINTER (GClosure * closure,
					     GValue * return_value,
					     guint n_param_values,
					     const GValue * param_values,
					     gpointer invocation_hint,
					     gpointer marshal_data)
{
   typedef void (*GMarshalFunc_VOID__POINTER_POINTER) (gpointer data1,
						       gpointer arg_1,
						       gpointer arg_2,
						       gpointer data2);
   register GMarshalFunc_VOID__POINTER_POINTER callback;
   register GCClosure *cc = (GCClosure *) closure;
   register gpointer data1, data2;

   g_return_if_fail (n_param_values == 3);

   if (G_CCLOSURE_SWAP_DATA (closure))
   {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
   }
   else
   {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
   }
   callback =
      (GMarshalFunc_VOID__POINTER_POINTER) (marshal_data ? marshal_data : cc->
					    callback);

   callback (data1,
	     g_marshal_value_peek_pointer (param_values + 1),
	     g_marshal_value_peek_pointer (param_values + 2), data2);
}
