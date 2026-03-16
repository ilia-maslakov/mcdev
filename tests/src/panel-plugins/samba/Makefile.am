PACKAGE_STRING = "/src/panel-plugins/samba"

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
	samba_utils

check_PROGRAMS = $(TESTS)

samba_utils_SOURCES = \
	samba_utils.c
