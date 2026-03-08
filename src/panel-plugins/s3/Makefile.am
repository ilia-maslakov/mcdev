panelplugindir = $(panel_plugins_dir)
panelplugin_LTLIBRARIES = mc-panel-s3.la

mc_panel_s3_la_SOURCES = s3.c
mc_panel_s3_la_CPPFLAGS = $(GLIB_CFLAGS) $(LIBCURL_S3_CFLAGS) -I$(top_srcdir)
mc_panel_s3_la_LDFLAGS = -module -avoid-version
mc_panel_s3_la_LIBADD = $(GLIB_LIBS) $(LIBCURL_S3_LIBS)
