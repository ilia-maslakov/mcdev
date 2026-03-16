systemddir = $(panel_plugins_dir)/systemd
systemd_LTLIBRARIES = mc-panel-systemd.la

mc_panel_systemd_la_SOURCES = systemd.c
mc_panel_systemd_la_CPPFLAGS = $(GLIB_CFLAGS) -I$(top_srcdir)
mc_panel_systemd_la_LDFLAGS = -module -avoid-version
mc_panel_systemd_la_LIBADD = $(GLIB_LIBS)
