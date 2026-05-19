panelizedir = $(panel_plugins_dir)/panelize
panelize_LTLIBRARIES = mc-panel-panelize.la

mc_panel_panelize_la_SOURCES = panelize.c panelize_config.c panelize_config.h panelize_dlg.c panelize_dlg.h panelize_url.c panelize_url.h
mc_panel_panelize_la_CPPFLAGS = $(GLIB_CFLAGS) -I$(top_srcdir)
mc_panel_panelize_la_LDFLAGS = -module -avoid-version
mc_panel_panelize_la_LIBADD = $(GLIB_LIBS)
