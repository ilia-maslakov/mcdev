PACKAGE_STRING = "/src/viewer"

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/lib/vfs \
	-DTEST_SHARE_DIR=\"$(abs_srcdir)/../fixtures\" \
	@CHECK_CFLAGS@

LIBS = @CHECK_LIBS@ \
	$(top_builddir)/src/libinternal.la \
	$(top_builddir)/lib/libmc.la

if ENABLE_MCLIB
LIBS += $(GLIB_LIBS)
endif

TESTS = \
	mcview__load_zip_magic \
	mcview__stream

check_PROGRAMS = $(TESTS)

mcview__load_zip_magic_SOURCES = \
	mcview__load_zip_magic.c

mcview__stream_SOURCES = \
	mcview__stream.c
