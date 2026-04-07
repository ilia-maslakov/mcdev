k8sdir = $(panel_plugins_dir)/k8s
k8s_LTLIBRARIES = mc-panel-k8s.la
k8s_DATA = k8s_panel.hlp

mc_panel_k8s_la_SOURCES = \
	k8s.c \
	k8s-cmd.c \
	k8s-ui.c \
	pods.c \
	workloads.c \
	services.c \
	nodes.c
mc_panel_k8s_la_CPPFLAGS = $(GLIB_CFLAGS) -I$(top_srcdir) \
	-DMC_PLUGIN_DIR=\""$(panel_plugins_dir)/k8s"\"
mc_panel_k8s_la_LDFLAGS = -module -avoid-version
mc_panel_k8s_la_LIBADD = $(GLIB_LIBS)
