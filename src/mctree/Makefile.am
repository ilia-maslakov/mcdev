noinst_LTLIBRARIES = libmctree.la

libmctree_la_SOURCES = \
	mctree-model.c mctree-model.h \
	mctree-view.c mctree-view.h \
	mctree-resolver.c mctree-resolver.h \
	mctree-providers.h \
	mctree-json-provider.c \
	mctree-xml-provider.c \
	mctree-yaml-provider.c

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(GLIB_CFLAGS) \
	$(MCTREE_XML_CFLAGS) \
	$(MCTREE_MAGIC_CFLAGS)

libmctree_la_LIBADD = \
	$(GLIB_LIBS) \
	$(MCTREE_XML_LIBS) \
	$(MCTREE_MAGIC_LIBS)
