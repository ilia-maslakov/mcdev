PACKAGE_STRING = "/src/panel-plugins/mongo"

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	$(MONGOC_CFLAGS) \
	-I$(top_srcdir) \
	@CHECK_CFLAGS@

LIBS = @CHECK_LIBS@ \
	$(top_builddir)/lib/libmc.la \
	$(MONGOC_LIBS)

if ENABLE_MCLIB
LIBS += $(GLIB_LIBS)
endif

TESTS = \
	mongo_render_test

check_PROGRAMS = $(TESTS)

mongo_render_test_SOURCES = \
	mongo_render_test.c \
	$(top_srcdir)/src/panel-plugins/mongo/mongo_render.c
