PACKAGE_STRING = "/src/panel-plugins/sqlite"

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	$(SQLITE3_CFLAGS) \
	-I$(top_srcdir) \
	-DMC_PLUGIN_DIR=\"/tmp\" \
	@CHECK_CFLAGS@

LIBS = @CHECK_LIBS@ \
	$(top_builddir)/lib/libmc.la \
	$(SQLITE3_LIBS)

if ENABLE_MCLIB
LIBS += $(GLIB_LIBS)
endif

TESTS = \
	sqlite_plugin_test

check_PROGRAMS = $(TESTS)

sqlite_plugin_test_SOURCES = \
	sqlite_plugin_test.c \
	$(top_srcdir)/src/panel-plugins/sqlite/sqlite.c \
	$(top_srcdir)/src/panel-plugins/sqlite/sqlite_json.c
