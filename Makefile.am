# Makefile.am

SUBDIRS = src doc
ACLOCAL_AMFLAGS = -I admin

dist_doc_DATA=README

EXTRA_DIST=bootstrap makefile.vc makefile.mingw extracfg.h test_linux

dist-hook:
	rm -rf `find $(distdir) -name .git`

appsdir = $(datadir)/applications

