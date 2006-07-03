ifndef PREFIX
PREFIX    = /usr/local
endif
ifndef INSTALLPREFIX
INSTALLPREFIX = $(PREFIX)
endif
GLADEFILE = $(PREFIX)/share/gwignerfit/gwignerfit.glade
ICONPATH  = $(PREFIX)/share/gwignerfit

KNOPPIXSRC = /localhome/schaefer/knx/source/KNOPPIX

SUBDIRS = src
DOCDIR  = doc
INSTALL = install
STRIP   = strip

ARCH = pentium4
SPEEDOPTIMIZE = "-march=$(ARCH)"

all:
	@ for i in $(SUBDIRS); do\
		$(MAKE) GLADEFILE=$(GLADEFILE) ICONPATH=$(ICONPATH) EXTRACFLAGS="-O3 -fomit-frame-pointer $(SPEEDOPTIMIZE)" -C $$i $@;\
	done

386:
	@ for i in $(SUBDIRS); do\
		$(MAKE) GLADEFILE=$(GLADEFILE) ICONPATH=$(ICONPATH) EXTRACFLAGS="-O3 -fomit-frame-pointer" -C $$i;\
	done

clean:
	@ for i in $(SUBDIRS); do $(MAKE) -C $$i $@; done

htmldoc:
	$(MAKE) -C $(DOCDIR) html

install: all htmldoc
	test -d $(INSTALLPREFIX)/bin || mkdir -p $(INSTALLPREFIX)/bin
	test -d $(INSTALLPREFIX)/share/gwignerfit || mkdir -p $(INSTALLPREFIX)/share/gwignerfit
	test -d $(INSTALLPREFIX)/share/applications || mkdir -p $(INSTALLPREFIX)/share/applications
	test -d $(INSTALLPREFIX)/share/doc/gwignerfit || mkdir -p $(INSTALLPREFIX)/share/doc/gwignerfit
	test -d $(INSTALLPREFIX)/share/doc/gwignerfit/figures || mkdir -p $(INSTALLPREFIX)/share/doc/gwignerfit/figures
	$(INSTALL) -m 755 src/gwignerfit $(INSTALLPREFIX)/bin/gwignerfit
	$(STRIP) $(INSTALLPREFIX)/bin/gwignerfit
	sed -e "s|###VERSION###|`date --iso-8601`|" gwignerfit.glade > $(INSTALLPREFIX)/share/gwignerfit/gwignerfit.glade
	chmod 644 $(INSTALLPREFIX)/share/gwignerfit/gwignerfit.glade
	$(INSTALL) -m 644 pixmap/gwignerfit-48x48.png $(INSTALLPREFIX)/share/gwignerfit/gwignerfit-48x48.png
	$(INSTALL) -m 644 pixmap/gwignerfit-16x16.png $(INSTALLPREFIX)/share/gwignerfit/gwignerfit-16x16.png
	sed -e 's|###PREFIX###|$(PREFIX)|' gwignerfit.desktop > $(INSTALLPREFIX)/share/applications/gwignerfit.desktop
	cp -a $(DOCDIR)/figures/*.png $(INSTALLPREFIX)/share/doc/gwignerfit/figures
	install -m 644 $(DOCDIR)/gwignerfit-manual.html $(INSTALLPREFIX)/share/doc/gwignerfit/gwignerfit-manual.html
	install -m 644 $(DOCDIR)/gwf-style.css $(INSTALLPREFIX)/share/doc/gwignerfit/gwf-style.css

uninstall:
	-rm -f $(INSTALLPREFIX)/bin/gwignerfit
	-rm -f $(INSTALLPREFIX)/share/gwignerfit/gwignerfit.glade
	-rm -f $(INSTALLPREFIX)/share/gwignerfit/gwignerfit-48x48.png
	-rm -f $(INSTALLPREFIX)/share/gwignerfit/gwignerfit-16x16.png
	-rmdir $(INSTALLPREFIX)/share/gwignerfit
	-rm -f $(INSTALLPREFIX)/share/applications/gwignerfit.desktop
	-rm -rf $(INSTALLPREFIX)/share/doc/gwignerfit

knoppix:
	$(MAKE) clean
	$(MAKE) PREFIX=/usr 386
	$(MAKE) PREFIX=/usr INSTALLPREFIX=$(KNOPPIXSRC)/usr install
	$(MAKE) clean
	$(MAKE) -C doc html pdf
	$(INSTALL) -m 644 doc/gwignerfit-manual.html $(KNOPPIXSRC)/../../master/index.html
	$(INSTALL) -m 644 doc/gwf-style.css $(KNOPPIXSRC)/../../master/gwf-style.css
	$(INSTALL) -m 644 doc/gwignerfit-manual.pdf $(KNOPPIXSRC)/usr/share/doc/gwignerfit/gwignerfit-manual.pdf
	cp -a doc/figures/*.png $(KNOPPIXSRC)/../../master/figures
	chown -R root.root $(KNOPPIXSRC)/../../master/figures
	chmod -R a+r $(KNOPPIXSRC)/../../master/figures
	-rm -rf $(KNOPPIXSRC)/usr/src/GWignerfit
	cp -a ../GWignerfit $(KNOPPIXSRC)/usr/src
	for I in `find $(KNOPPIXSRC)/usr/src/GWignerfit -name .svn -type d`; do  rm -rf "$$I"; done
	-rm -f $(KNOPPIXSRC)/usr/src/GWignerfit/*.bak
	chown -R root.root $(KNOPPIXSRC)/usr/src/GWignerfit
	chmod -R a+r $(KNOPPIXSRC)/usr/src/GWignerfit
