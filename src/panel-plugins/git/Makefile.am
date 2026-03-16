gitdir = $(panel_plugins_dir)/git
git_LTLIBRARIES = mc-panel-git.la
git_DATA = git_panel.hlp

mc_panel_git_la_SOURCES = git.c
mc_panel_git_la_CPPFLAGS = $(GLIB_CFLAGS) -I$(top_srcdir) \
	-DMC_PLUGIN_DIR=\""$(panel_plugins_dir)/git"\"
mc_panel_git_la_LDFLAGS = -module -avoid-version
mc_panel_git_la_LIBADD = $(GLIB_LIBS)
