PACKAGE_STRING = "/src/panel-plugins/docker"

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	@CHECK_CFLAGS@

LIBS = @CHECK_LIBS@ \
	$(top_builddir)/lib/libmc.la

if ENABLE_MCLIB
LIBS += $(GLIB_LIBS)
endif

TESTS = \
	docker_tar_test \
	docker_cp_stream_test \
	docker_domains_test

check_PROGRAMS = $(TESTS)

docker_tar_test_SOURCES = \
	docker_tar_test.c \
	$(top_srcdir)/src/panel-plugins/docker/docker-tar.c

docker_cp_stream_test_SOURCES = \
	docker_cp_stream_test.c \
	$(top_srcdir)/src/panel-plugins/docker/docker-cp-stream.c

docker_domains_test_SOURCES = \
	docker_domains_test.c

AM_TESTS_ENVIRONMENT = CK_FORK=no; export CK_FORK;
