XMLBASE = gwignerfit-manual
XMLFILE = $(XMLBASE).xml

STYLESHEET = /usr/share/sgml/docbook/stylesheet/xsl/nwalsh/xhtml/docbook.xsl
#STYLESHEET = /usr/share/sgml/docbook/xsl-stylesheets/xhtml/docbook.xsl
#STYLESHEET = /usr/share/sgml/docbook/xsl-stylesheets-1.68.1-1/xhtml/docbook.xsl

#Create html output:
html: ${XMLBASE}.html

${XMLBASE}.html: ${XMLFILE}
	xsltproc -o $(XMLBASE).html --stringparam html.stylesheet gwf-style.css --stringparam section.autolabel 1 $(STYLESHEET) $(XMLFILE)

#Create custom ps:
custps:
	-jw -f docbook -b tex -d gwf-custom.dsl $(XMLFILE)
	-jadetex $(XMLBASE).tex
	-jadetex $(XMLBASE).tex
	-jadetex $(XMLBASE).tex
	dvips $(XMLBASE).dvi
	rm $(XMLBASE).aux $(XMLBASE).dvi $(XMLBASE).log $(XMLBASE).out $(XMLBASE).tex

#Create custom pdf:
custpdf:
	-jw -f docbook -b tex -d gwf-custom.dsl $(XMLFILE)
	perl -p -i -e 's/\.eps/.png/g' $(XMLBASE).tex
	-pdfjadetex $(XMLBASE).tex
	-pdfjadetex $(XMLBASE).tex
	-pdfjadetex $(XMLBASE).tex
	rm $(XMLBASE).aux $(XMLBASE).log $(XMLBASE).out $(XMLBASE).tex

#Create normal ps:
ps:
	-jw -f docbook -b tex $(XMLFILE)
	-jadetex $(XMLBASE).tex
	-jadetex $(XMLBASE).tex
	-jadetex $(XMLBASE).tex
	dvips $(XMLBASE).dvi
	rm $(XMLBASE).aux $(XMLBASE).dvi $(XMLBASE).log $(XMLBASE).out $(XMLBASE).tex

#"Real" latex code:
latex:
	xsltproc -o $(XMLBASE).tex db2latex-style.xsl $(XMLFILE)

clean:
	rm -f $(XMLBASE).aux $(XMLBASE).dvi $(XMLBASE).log $(XMLBASE).out $(XMLBASE).tex

