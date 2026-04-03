dockerdir = $(panel_plugins_dir)/docker
docker_LTLIBRARIES = mc-panel-docker.la

mc_panel_docker_la_SOURCES = \
	docker.c \
	docker-ui.c \
	containers.c \
	container-files.c \
	images.c \
	volumes.c \
	networks.c \
	docker-cp-stream.c \
	docker-tar.c
mc_panel_docker_la_CPPFLAGS = $(GLIB_CFLAGS) -I$(top_srcdir)
mc_panel_docker_la_LDFLAGS = -module -avoid-version
mc_panel_docker_la_LIBADD = $(GLIB_LIBS)
