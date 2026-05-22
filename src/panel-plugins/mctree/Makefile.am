mctreedir = $(panel_plugins_dir)/mctree
mctree_LTLIBRARIES = mc-panel-mctree.la

mc_panel_mctree_la_SOURCES = mctree.c
mc_panel_mctree_la_CPPFLAGS = $(GLIB_CFLAGS) -I$(top_srcdir)
mc_panel_mctree_la_LDFLAGS = -module -avoid-version
mc_panel_mctree_la_LIBADD = \
	$(top_builddir)/src/mctree/libmctree.la \
	$(GLIB_LIBS)
