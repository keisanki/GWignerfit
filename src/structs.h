#include <glib.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#ifndef NO_ZLIB
#include <zlib.h>
#endif

#ifndef _STRUCTS_H_
#define _STRUCTS_H_

#define FLAG_RES_CLICK  (1 << 0)
#define FLAG_RES_FIND   (1 << 1)
#define FLAG_FRQ_MIN    (1 << 2)
#define FLAG_FRQ_MAX    (1 << 3)
#define FLAG_CAL_RUN	(1 << 4)
#define FLAG_FIT_RUN	(1 << 5)
#define FLAG_FIT_CANCEL	(1 << 6)
#define FLAG_FRQ_MEAS   (1 << 7)
#define FLAG_FRQ_INT    (1 << 8)
#define FLAG_MRK_NOISE  (1 << 9)
#define FLAG_CHANGES    (1 << 10)
#define FLAG_VNA_MEAS   (1 << 11)
#define FLAG_VNA_CAL    (1 << 12)

#define FIT_EXIT_ERROR      0
#define FIT_EXIT_NRERR      1
#define FIT_EXIT_COMPLETE   2
#define FIT_EXIT_CANCELED   3
#define FIT_EXIT_EARLYSTOP  4
#define FIT_EXIT_CONVERGED  5

#define C0 299792458.0		/* Vacuum speed of light */

#define NUM_GLOB_PARAM 3	/* Number of global fit parameters */

#define DATAHDR  "%c\r\n%c f [Hz]\t Re(S)\t\t Im(S)\r\n"	/* Output data header */
#define DATAFRMT "%13.1f\t% 11.8f\t% 11.8f\r\n"			/* Format for output data (dat or s1p) */

#define DATAHS2P "!\r\n! f [Hz]\t Re(S11)\t Im(S11)\t Re(S21)\t Im(S21)\t Re(S12)\t Im(S12)\t Re(S22)\t Im(S22)\r\n"	/* Output data header */
#define DATAFS2P "%13.1f\t% 11.8f\t% 11.8f\t% 11.8f\t% 11.8f\t% 11.8f\t% 11.8f\t% 11.8f\t% 11.8f\r\n"			/* Format for output data (s2p) */

#ifndef GLADEFILE
#define GLADEFILE "../gwignerfit.glade"
#endif

/* This is short for the total number of parameters */
#define TOTALNUMPARAM	4*glob->numres+NUM_GLOB_PARAM+3*glob->fcomp->numfcomp

typedef struct
{
	gdouble re;		/* Real part */
	gdouble im;		/* Imaginary part */
	gdouble abs;		/* Absolute value */
} ComplexDouble;

typedef struct 
{
	gdouble *x;		/* x values */
	ComplexDouble *y;	/* complex y values */
	guint len;		/* length of datasets 0 ... (len-1) */
	gchar *file;		/* filename of dataset */
	gint index;		/* GtkSpectVis index */
} DataVector;

typedef struct
{
	gdouble frq;		/* Resonance frequency (in Hz) */
	gdouble width;		/* Resonance width (in Hz) */
	gdouble amp;		/* Resonance amplitude (in Hz) */
	gdouble phase;		/* Resonance phase (in rad) */
} Resonance;

typedef struct
{
	gdouble amp;		/* Amplitude of fourier factor */
	gdouble tau;		/* Time value of fourier factor (in sec) */
	gdouble phi;		/* Additional phase shift (in rad) */
} FourierComponent;

typedef struct
{
	gdouble min;		/* Frequency window minimal frequency (in Hz) */
	gdouble max;		/* Frequency window maximal frequency (in Hz) */
	gdouble phase;		/* Global phase (in rad) */
	gdouble scale;		/* Global scale factor */
	gdouble tau;		/* Global time delau (in sec) */
} GlobalParam;

typedef struct
{
	/* For the background calculation */
	GThreadPool *pool;		/* ThreadPool for background calculation */
	GMutex *theorylock;		/* Lock the theory graph against changes */
	GMutex *flaglock;		/* Lock glob->flag against changes */
	GAsyncQueue *aqueue1;		/* Pass messages to the calculation thread */
	GAsyncQueue *aqueue2;		/* Pass messages to the parent thread */
	/* For the fitting stuff */
	GMutex *fitwinlock;		/* Lock the FitWindowParam struct */
} ThreadStuff;

typedef struct
{
	gint iterations;		/* Number of iterations for a whole fit */
	gint widthunit;			/* The unit for the width column */
	gboolean confirm_append;	/* Confirm appending of sections? */
	gboolean confirm_resdel;	/* Confirm the deletion of resonances? */
	gboolean save_overlays;		/* Include overlayed graphs in gwf files? */
	gboolean datapoint_marks;	/* Mark the datapoints in the graph? */
	gboolean sortparam;		/* Sort glob->param array by frequencies? */
	gboolean fit_converge_detect;	/* Stop fit automatically if converged? */
	gboolean relative_paths;	/* Save relative paths in gwf files? */
	gboolean angles_in_deg;		/* True if angles should be given in degrees */
	gint res_export;		/* Bitmask of parameters for resonance export */
	gint priority;			/* The nice level GWignerFit should run with */
	gdouble cal_tauO;		/* Open calibration standard: signal delay */
	gdouble cal_tauS;		/* Short calibration standard: signal delay */
	gdouble cal_C0;			/* Open calibration signal: capacity C0 value */
	gdouble cal_C1;			/* Open calibration signal: capacity C1 value */
	gdouble cal_C2;			/* Open calibration signal: capacity C2 value */
	gdouble cal_C3;			/* Open calibration signal: capacity C3 value */
	gint vnamodel;			/* VNA backend: 1=proxy, 2=n5230a */
	gchar *vnahost;			/* VNA resp. Ieee488Proxy hostname */
} Preferences;

typedef struct
{
	GladeXML *xmlfft;		/* Glade XML structure for fft_window */
	GPtrArray *data;		/* The fourier transformed data sets */
	DataVector *theory;		/* The fourier transformed theory data set */
	gint windowing;			/* Which windowing method to use */
	gdouble fmin;			/* Original start frequency of FFT in Hz */
	gdouble fmax;			/* Original stop frequency of FFT in Hz */
} FFTWindow;

typedef struct
{
	GladeXML *xmlspect;		/* Glade XML structure for spectral_window */
	gdouble area;			/* Area term in weyl formula in cm² */
	gdouble perim;			/* Perimeter term in weyl formula cm */
	gdouble offset;			/* Offset term in weyl formula */
	gchar view;			/* Type of graph to be displayed */
	guint first_res;		/* Level number of first resonance */
	gchar selection;		/* What resonances are to be taken ('a'll, 'w'indow, 's'elected) */
	guint bins;			/* Number of bins for NND */
	gchar theo_predict;		/* Theory graph predictions to be displayed */
	gboolean normalize;		/* Normalize the NND and iNND spectra */
} SpectralWin;

typedef struct
{
	int (*connect) (const gchar*);	/* Connect to the VNA/proxy to given host */
	ComplexDouble* (*recv_data)(int); /* Get data of current trace, points given */
	void (*gtl) ();			/* Go to local method */
	void (*llo) ();			/* Local lock out method */
	glong (*sweep_cal_sleep) ();	/* Estimated time for one sweep in ms */
	gdouble (*get_start_frq) ();	/* Return start frequency of current window in Hz */
	gdouble (*get_stop_frq) ();	/* Return stop frequency of current window in Hz */
	gint (*get_points) ();		/* Get number of currently selected points */
	void (*sweep_prepare) ();	/* Prepare a sweep (reset, display, traces, etc.) */
	void (*set_startstop) (gdouble, gdouble); /* Change frequency window to given limits in Hz */
	void (*trace_scale_auto) ();	/* Autoscale all traces */
	void (*trace_fourparam) ();	/* Display full 2x2 S-matrix on display */
	void (*set_numg) (gint);	/* Set up number of groups */
	void (*wait) ();		/* Wait for VNA operation to finish */
	void (*select_s) (gchar*);	/* Select given S-parameter for first trace */
	void (*select_trl) (gint);	/* Set up measurement of special TRL parameters */
	gdouble (*get_capa) (gint);	/* Return VNA capabilities: 1=f_min, 2=f_max, 3=number_of_points */
} VnaBackend;

typedef struct
{
	GladeXML *xmlnet;		/* Glade XML structure for network_window */
	gint vnamodel;			/* VNA backend: 1=proxy, 2=n5230a */
	gchar *host;			/* VNA resp. Ieee488Proxy hostname */
	VnaBackend *vna_func;		/* Callbacks to VNA communication layer */
	gchar *path;			/* Path for output file */
	gchar *file;			/* Filename for output file */
	gchar *fullname[6];		/* The full name of the output file(s) */
	gchar *comment;			/* An optional comment for the datafile header */
	gint format;			/* File format: 1=dat; 2=snp */
	gboolean compress;		/* Compress the output file? */
	gchar type;			/* 1=sweep mode; 2=snapshot mode */
	gdouble start;			/* Start frequency for sweep in Hz */
	gdouble stop;			/* Stop frequency for sweep in Hz */
	gdouble resol;			/* Resolution of sweep in Hz */
	gint points;			/* Number of points in one measurement window */
	gchar param[6];			/* S-Parameter (Sxy, S, TRL) to measure in sweep mode */
	gint numparam;			/* Total number of parameters to measure */
	gint avg;			/* Averaging factor for sweep */
	gchar swpmode;			/* 1=ramp mode; 2=step mode */
	gdouble bandwidth;		/* Measurement IF bandwidth */
	gdouble dwell;			/* Measurement dwell time */
	GThread *vna_GThread;		/* The handle of the measurement process */
	glong start_t;			/* The time at which the measurement started in sec */
	glong estim_t;			/* Estimated time for measurement in sec */
	gint sockfd;			/* Socket for network communication */
	ComplexDouble *ydata[6];	/* The measured data */
	guint index[6];			/* The graph index of the measured data */
#ifndef NO_ZLIB
	gzFile *gzoutfh[6];		/* Filehandle for the compressed data file */
#endif
} NetworkWin;

typedef struct
{
	GladeXML *xmlcal;		/* Glade XML structure for cal_dialog */
	gchar *in_file;			/* Full filename for input data file */
	gchar *out_file;		/* Full filename for calibrated output data */
	gchar *open_file;		/* Full filename for open standard data */
	gchar *short_file;		/* Full filename for short standard data */
	gchar *load_file;		/* Full filename for load standard data */
	gchar *thru_file;		/* Full filename for thru standard data */
	gchar *isol_file;		/* Full filename for isolation standard data */
	gchar *full_filenames[40];	/* Full filenames for full 2-port and TRL calibration */
	gint calib_type;		/* 0: reflection, 1: transmission, 2: full 2-port, 3: TRL */
	gboolean offline;		/* True for offline calibration method */
	gchar *proxyhost;		/* Ieee488Proxy hostname */
	gint sockfd;			/* Socket for network communication */
	GThread *cal_GThread;		/* The handle of the calibration process */
	GTimeVal start_t;		/* The time at which the calibration started in sec */
	glong estim_t;			/* Estimated time for calibration in sec */
} CalWin;

typedef struct
{
	GladeXML *xmlfcomp;		/* Glade XML structure for fcomp_win */
	GtkListStore *store;		/* ListStore for fcomp data */
	GPtrArray *data;		/* Array with additions FourierComponents */
	gint numfcomp;			/* Number of elements in *fcomp */
	DataVector *quotient;		/* Quotient fft graph data */
	DataVector *theo;		/* Theory fft graph data */
} FourierCompWin;

typedef struct
{
	GladeXML *xmlmerge;		/* Glade XML struct for merge_win */
	GtkListStore *store;		/* ListStore for merge data files */
	GPtrArray *nodelist;		/* Array with GPtrArrays of *MergeNodes */
	GPtrArray *datafilename;	/* Array with associated datafilenames */
	GPtrArray *graphuid;		/* Array with graph uids */
	GPtrArray *origlen;		/* Original length of nodelists */
	GPtrArray *links;		/* Links between the "same" resonances */
	GPtrArray *spectra;		/* Array with measured spectra for nodelists */
	void *nearnode;			/* The nearest MergeNode the cursor points to */
	guint flag;			/* A flag to identify click events */
	gint selx, sely;		/* Coordinates of selection marker */
	gchar *savefile;		/* Filename to store link information */
	gchar *section;			/* Section in savefile */
} MergeWin;

typedef struct
{
	gdouble min;			/* Start frequency of fit */
	gdouble max;			/* Stop frequency of fit */
	gint numpoints;			/* Number of points in frequency window */
	gint freeparam;			/* Number of free parameters */
	gint iter;			/* Number of current fit iteration */
	gint maxiter;			/* Maximal number of iterations */
	gdouble chi;			/* Last Chi^2 value */

	gdouble *paramarray;		/* Current parameterset as an array */
	gdouble *stddev;		/* Stddev of parameters after fit */
	gchar text[30];			/* Text message for status line */
	GladeXML *xmlfit;		/* GladeXML struct of progess window */
	GThread *fit_GThread;		/* the handle of the fitting process */
} FitWindowParam;

typedef struct
{
	gint num_cpu;			/* The number of CPUs in the system */
	GThreadPool* pool;		/* Pool of threads for computations */
} SMPdata;

typedef struct
{
	GladeXML *xmlcorrel;		/* GladeXML struct of correlation function window */
	gdouble min;			/* Start frequency in Hz */
	gdouble max;			/* Stop frequency in Hz */
	DataVector *data;		/* Graph data */
} CorrelWin;

typedef struct
{
	DataVector *data;		/* The complex measured data plus frequencies */
	DataVector *theory;		/* The complex theory data */
	gdouble noise;			/* Stddev of data due to thermal noise */
	gint numres;			/* Total number of resonances in theory */
	Preferences *prefs;		/* Global preferences */
	gint IsReflection;		/* Is 1 if spectrum is a reflection one */
	gboolean viewdifference;	/* TRUE if view difference is active */
	gboolean viewtheory;		/* TRUE if theory graph is to be displayed */
	guint flag;			/* Contains general flags, see FLAG_ */
	GPtrArray *param;		/* The current resonances parameterset */
	void *oldparam;			/* The old parameter set for undo */
	GtkListStore *store;		/* The store holding the resonance data */
	GlobalParam *gparam;		/* Stores global spectrum information */
	gdouble *stddev;		/* Stddev of parameters after fit */
	gchar *resonancefile;		/* The absolute position of the savefile */
	gchar *section;			/* The name of the savefile section */
	gchar *path;			/* The path of the last file operation */
	guint *bars;			/* uids of min, max and width bar */
	ThreadStuff *threads;		/* For the background calculations */
	GtkTreeView *overlaytreeview;	/* Treeview for overlay window */
	GtkListStore *overlaystore;	/* The model for the overlay treeview */
	GPtrArray *overlayspectra;	/* DataVector* for the overlay data*/
	gdouble measure;		/* Holds the first frequency during measuring */
	FFTWindow *fft;			/* Structure for the fourier transform window */
	FitWindowParam fitwindow;	/* Holds FitWindowParam* during a fit */
	SpectralWin *spectral;		/* Structure for the spectral window */
	NetworkWin *netwin;		/* Structure for the network measurement window */
	CalWin *calwin;			/* Structure for the calibration dialog window */
	FourierCompWin *fcomp;		/* Structure for the fourier components window */
	MergeWin *merge;		/* Structure for the merge resonance lists window */
	GladeXML *commentxml;		/* GladeXML struct for the comment dialog */
	gchar *comment;			/* For arbitrary comments of the gwf section */
	SMPdata *smp;			/* Structure for SMP parallel computation */
	CorrelWin *correl;		/* Structure for the correlation function window */
} GlobalData;

#endif
