XMLBASE = gwignerfit-manual
XMLFILE = $(XMLBASE).xml
CUSTOMDSL = utils/gwf-custom.dsl
XMLDCL = utils/xml.dcl

STYLESHEET = /usr/share/sgml/docbook/stylesheet/xsl/nwalsh/xhtml/docbook.xsl
#STYLESHEET = /usr/share/sgml/docbook/xsl-stylesheets-1.68.1-1/xhtml/docbook.xsl

${XMLBASE}.html: ${XMLFILE}
	#xsltproc -o $(XMLBASE).html --stringparam html.stylesheet gwf-style.css --stringparam section.autolabel 1 $(STYLESHEET) $(XMLFILE)
	jw -f docbook -b html -d $(CUSTOMDSL)\#html --nochunks $(XMLFILE)
	./utils/texmath2png.pl equation-list.sgml
	rm equation-list.sgml

${XMLBASE}.tex: ${XMLFILE}
	@#-jw -f docbook -b tex -d $(CUSTOMDSL) -V tex.math.in.alt="latex" $(XMLFILE)
	jade -t tex -d $(CUSTOMDSL) $(XMLDCL) $(XMLFILE)
	./utils/unescape_math.pl $(XMLBASE).tex

#Create html output:
html: ${XMLBASE}.html

#Create custom ps:
ps: ${XMLBASE}.tex
	-jadetex $(XMLBASE).tex
	-jadetex $(XMLBASE).tex
	-jadetex $(XMLBASE).tex
	dvips $(XMLBASE).dvi
	rm $(XMLBASE).aux $(XMLBASE).dvi $(XMLBASE).log $(XMLBASE).out $(XMLBASE).tex

#Create custom pdf:
pdf: ${XMLBASE}.tex
	perl -p -i -e 's/\.eps/.png/g' $(XMLBASE).tex
	-pdfjadetex $(XMLBASE).tex
	-pdfjadetex $(XMLBASE).tex
	-pdfjadetex $(XMLBASE).tex
	rm $(XMLBASE).aux $(XMLBASE).log $(XMLBASE).out $(XMLBASE).tex

#Create normal ps:
#ps:
#	-jw -f docbook -b tex -V tex.math.in.alt="latex" $(XMLFILE)
#	-./unescape_math.pl $(XMLBASE).tex
#	-jadetex $(XMLBASE).tex
#	-jadetex $(XMLBASE).tex
#	-jadetex $(XMLBASE).tex
#	dvips $(XMLBASE).dvi
#	rm $(XMLBASE).aux $(XMLBASE).dvi $(XMLBASE).log $(XMLBASE).out $(XMLBASE).tex

#"Real" latex code:
latex:
	xsltproc -o $(XMLBASE).tex ./utils/db2latex-style.xsl $(XMLFILE)

clean:
	rm -f $(XMLBASE).aux $(XMLBASE).dvi $(XMLBASE).log $(XMLBASE).out $(XMLBASE).tex equation-list.sgml

