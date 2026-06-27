dockerdir = $(panel_plugins_dir)/docker
docker_LTLIBRARIES = mc-panel-docker.la
docker_DATA = docker_panel.hlp dlog.sh
EXTRA_DIST = dlog.sh

mc_panel_docker_la_SOURCES = \
	docker.c \
	docker-ui.c \
	docker-logs.c \
	containers.c \
	container-files.c \
	images.c \
	volumes.c \
	networks.c \
	docker-cp-stream.c \
	docker-tar.c
mc_panel_docker_la_CPPFLAGS = $(GLIB_CFLAGS) -I$(top_srcdir) \
	-DMC_PLUGIN_DIR=\""$(panel_plugins_dir)/docker"\"
mc_panel_docker_la_LDFLAGS = -module -avoid-version
mc_panel_docker_la_LIBADD = $(GLIB_LIBS)
