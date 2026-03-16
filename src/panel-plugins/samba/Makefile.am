sambadir = $(panel_plugins_dir)/samba
samba_LTLIBRARIES = mc-panel-samba.la

mc_panel_samba_la_SOURCES = samba.c
mc_panel_samba_la_CPPFLAGS = $(GLIB_CFLAGS) $(SMBCLIENT_CFLAGS) -I$(top_srcdir)
mc_panel_samba_la_LDFLAGS = -module -avoid-version
mc_panel_samba_la_LIBADD = $(GLIB_LIBS) $(SMBCLIENT_LIBS)
