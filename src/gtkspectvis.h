#ifndef __GTK_SPECT_VIS_H__
#define __GTK_SPECT_VIS_H__

#include <gdk/gdk.h>
#include <glib.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkdrawingarea.h>
#include "structs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GTK_SPECTVIS_TYPE          (gtk_spect_vis_get_type ())
#define GTK_SPECTVIS(obj)          GTK_CHECK_CAST (obj, gtk_spect_vis_get_type (), GtkSpectVis)
#define GTK_SPECTVIS_CLASS(klass)  GTK_CHECK_CLASS_CAST (klass, gtk_spect_vis_get_type (), GtkSpectVisClass)
#define GTK_IS_SPECTVIS(obj)       GTK_CHECK_TYPE (obj, gtk_spect_vis_get_type ())

typedef struct _GtkSpectVis         GtkSpectVis;
typedef struct _GtkSpectVisData     GtkSpectVisData;
typedef struct _GtkSpectVisClass    GtkSpectVisClass;
typedef struct _GtkSpectVisViewport GtkSpectVisViewport;
typedef struct _GtkSpectVisBar      GtkSpectVisBar;
typedef struct _GtkSpectVisPolygon  GtkSpectVisPolygon;

struct _GtkSpectVis
{
	GtkDrawingArea widget;

	/* The gtk_drawing_area */
	GtkWidget *draw;

	/* The backing pixmap of the graph */
	GdkPixmap *pixmap;

	/* The last position of the cursor */
	gint lastxpos;
	gint lastypos;

	/* The Graphics Context for drawing of the crosshair */
	GdkGC *cursorgc;

	/* Scale the axes in the graph by those factors */
	gdouble xAxisScale;
	gdouble yAxisScale;

	/* The double linked list with the graph data */
	GList *data;

	/* Display real part, imaginary part, absolute value, phase or log power*/
	gchar displaytype;

	/* The part of the graph to be displayed */
	GtkSpectVisViewport *view;

	/* A list of vertical bars to be drawn */
	GList *bars;

	/* A list of polygons to be drawn */
	GList *poly;
};

struct _GtkSpectVisData {
	/* A UID for this dataset */
	guint uid;
	
	/* The pointer to the y-values dataset */
	ComplexDouble *Y;

	/* The pointer to the x-values dataset */
	gdouble *X;

	/* The number of elements in *X and *Y */
	guint len;

	/* The x boundaries as positions in the X array */
	guint xmin_arraypos;
	guint xmax_arraypos;

	/* The color of this graph */
	GdkColor color;

	/* The graph type, e.g. lines or histogramm */
	gchar type;
};

struct _GtkSpectVisViewport {
	/* The lower left corner in data units */
	gdouble xmin;
	gdouble ymin;

	/* The upper right corner in data units */
	gdouble xmax;
	gdouble ymax;

	/* The size of the graphic box in pixel */
	guint graphboxwidth;
	guint graphboxheight;

	/* The position of the lower left corner in pixel */
	guint graphboxxoff;
	guint graphboxyoff;
};

struct _GtkSpectVisBar
{
	/* A UID for this dataset */
	guint uid;

	/* The position of the bar in units */
	gdouble pos;

	/* The FWHM of the bar */
	gdouble width;

	/* The color of the bar */
	GdkColor col;
};

struct _GtkSpectVisPolygon
{
	/* A UID for this dataset */
	guint uid;

	/* The datapoints for this polygon */
	gdouble *X;
	gdouble *Y;

	/* The number of elements in *points */
	guint len;

	/* The color of the polygon */
	GdkColor color;
};

struct _GtkSpectVisClass
{
	GtkDrawingAreaClass parent_class;

	void (* gtk_spect_vis)		(GtkSpectVis 		*spectvis);

	void (* viewport_changed)	(GtkSpectVis		*spectvis,
					 gchar			*type);
	void (* value_selected)		(GtkSpectVis		*spectvis,
					 gdouble		*x,
					 gdouble		*y);
};


GtkWidget*	gtk_spect_vis_new	(void);
GtkType		gtk_spect_vis_get_type	(void);

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
						 guint uid);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GTK_SPECT_VIS_H__ */
