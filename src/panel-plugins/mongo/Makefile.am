mongodir = $(panel_plugins_dir)/mongo
mongo_LTLIBRARIES = mc-panel-mongo.la
mongo_DATA = mongo_panel.hlp

mc_panel_mongo_la_SOURCES = \
	mongo.c \
	mongo_internal.h \
	mongo_bucket.c \
	mongo_columns.c \
	mongo_conn.c \
	mongo_conn.h \
	mongo_config.c \
	mongo_config.h \
	mongo_connections.c \
	mongo_docops.c \
	mongo_filter.c \
	mongo_filter.h \
	mongo_nav.c \
	mongo_render.c \
	mongo_render.h \
	mongo_session.c \
	mongo_settings.c \
	mongo_settings.h \
	mongo_ui.c \
	mongo_ui.h \
	mongo_wizard.c \
	mongo_wizard.h

mc_panel_mongo_la_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	$(MONGOC_CFLAGS) \
	-I$(top_srcdir) \
	-DMC_PLUGIN_DIR=\""$(panel_plugins_dir)/mongo"\"

mc_panel_mongo_la_LDFLAGS = -module -avoid-version

mc_panel_mongo_la_LIBADD = \
	$(GLIB_LIBS) \
	$(MONGOC_LIBS)
