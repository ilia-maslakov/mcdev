sqlitedir = $(panel_plugins_dir)/sqlite
sqlite_LTLIBRARIES = mc-panel-sqlite.la
dist_sqlite_DATA = sqlite_panel.hlp

mc_panel_sqlite_la_SOURCES = \
	sqlite.c \
	sqlite_json.c \
	sqlite_json.h
mc_panel_sqlite_la_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	-I$(top_srcdir) \
	-DMC_PLUGIN_DIR=\"$(panel_plugins_dir)/sqlite\"
mc_panel_sqlite_la_LDFLAGS = -module -avoid-version
mc_panel_sqlite_la_LIBADD = \
	$(GLIB_LIBS) \
	$(SQLITE3_LIBS)
