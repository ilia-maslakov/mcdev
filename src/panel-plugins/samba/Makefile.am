sambadir = $(panel_plugins_dir)/samba
samba_LTLIBRARIES = mc-panel-samba.la
samba_DATA = samba_panel.hlp

mc_panel_samba_la_SOURCES = samba.c
mc_panel_samba_la_CPPFLAGS = $(GLIB_CFLAGS) $(SMBCLIENT_CFLAGS) -I$(top_srcdir) \
	-DMC_PLUGIN_DIR=\""$(panel_plugins_dir)/samba"\"
mc_panel_samba_la_LDFLAGS = -module -avoid-version
mc_panel_samba_la_LIBADD = $(GLIB_LIBS) $(SMBCLIENT_LIBS)
