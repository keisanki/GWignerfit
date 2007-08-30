ifndef GLADEFILE
GLADEFILE = ../gwignerfit.glade
endif
ifndef ICONPATH
ICONPATH = ../pixmap
endif

SOURCES = gwignerfit.c callbacks.c processdata.c visualize.c helpers.c resonancelist.c compl_mrqmin.c compl_mrqcof.c nrutil.c numeric.c gtkspectvis.c overlay.c dnd.c export.c fourier.c gnuplot_i.c spectral.c spectral_numeric.c vna_proxy.c vna_n5230a.c network.c preferences.c calibrate.c calibrate_offline.c fcomp.c merge.c loadsave.c merge_util.c correl.c calibrate_vna.c
OBJS    = ${SOURCES:.c=.o}
	
CFLAGS  = `pkg-config gtk+-2.0 --cflags` `pkg-config libglade-2.0 --cflags` `pkg-config gthread-2.0 --cflags` -DGLADEFILE=\"${GLADEFILE}\" -DICONPATH=\"${ICONPATH}\" ${EXTRACFLAGS}
LDADD   = `pkg-config gtk+-2.0 --libs` `pkg-config libglade-2.0 --libs` `pkg-config gthread-2.0 --libs`
DEBUG	= -g -Wall -export-dynamic

CC      = gcc

PACKAGE = gwignerfit

all : ${OBJS} ${PACKAGE}

${PACKAGE}: ${OBJS}
	${CC} -o ${PACKAGE} ${OBJS} ${LDADD} ${DEBUG}

clean:
	rm -f *.o
	rm -f gwignerfit

tags: $(SOURCES)
	ctags $(SOURCES)

.c.o:
	${CC} ${CFLAGS} ${DEBUG} -c $<
