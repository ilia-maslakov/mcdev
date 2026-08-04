PACKAGE_STRING = "/src/panel-plugins/arcmc"

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	$(LIBARCHIVE_CFLAGS) \
	-I$(top_srcdir) \
	@CHECK_CFLAGS@

LIBS = @CHECK_LIBS@ \
	$(LIBARCHIVE_LIBS) \
	$(top_builddir)/lib/libmc.la

if ENABLE_MCLIB
LIBS += $(GLIB_LIBS)
endif

TESTS = \
	arcmc_reader \
	arcmc_utils

check_PROGRAMS = $(TESTS)

arcmc_utils_SOURCES = \
	arcmc_utils.c

arcmc_reader_SOURCES = \
	arcmc_reader.c \
	$(top_srcdir)/src/panel-plugins/arcmc/arcmc-reader.c
