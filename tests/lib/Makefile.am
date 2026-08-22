PACKAGE_STRING = "/lib"

SUBDIRS = . mcconfig search strutil vfs widget

AM_CPPFLAGS = $(GLIB_CFLAGS) -I$(top_srcdir) @CHECK_CFLAGS@

AM_CPPFLAGS += -DTEST_RUNTIME_PLUGIN_DIR=\"$(abs_builddir)/.libs\"
AM_CPPFLAGS += -DTEST_LUA_RUNTIME_DIR=\"$(abs_top_builddir)/src/lua/.libs\"

LIBS = @CHECK_LIBS@ \
	$(top_builddir)/lib/libmc.la

if ENABLE_MCLIB
LIBS += $(GLIB_LIBS) \
	@E2P_LIBS@
endif

EXTRA_DIST = utilunix__my_system-common.c

TESTS = \
	key \
	library_independ \
	mc_build_filename \
	name_quote \
	serialize \
	runtime_events \
	terminal \
	tty \
	utilunix__mc_pstream_get_string \
	utilunix__my_system_fork_fail \
	utilunix__my_system_fork_child_shell \
	utilunix__my_system_fork_child \
	x_basename

TESTS += mc_realpath

if HAVE_RUNTIME_MODULES
TESTS += extension_runtime
noinst_LTLIBRARIES = runtime-plugin-fixture.la

runtime_plugin_fixture_la_SOURCES = \
	runtime_plugin_fixture.c
runtime_plugin_fixture_la_LDFLAGS = -module -avoid-version -rpath $(abs_builddir)/.libs
runtime_plugin_fixture_la_LIBADD = $(GLIB_LIBS)
endif

if HAVE_RUNTIME_MODULES
if ENABLE_LUA_PLUGIN
TESTS += lua_runtime
endif
endif

check_PROGRAMS = $(TESTS)

key_SOURCES = \
	key.c

library_independ_SOURCES = \
	library_independ.c

mc_build_filename_SOURCES = \
	mc_build_filename.c

mc_realpath_SOURCES = \
	mc_realpath.c

name_quote_SOURCES = \
	name_quote.c

serialize_SOURCES = \
	serialize.c

runtime_events_SOURCES = \
	runtime_events.c

extension_runtime_SOURCES = \
	extension_runtime.c
extension_runtime_DEPENDENCIES = runtime-plugin-fixture.la
extension_runtime_LDADD = $(GMODULE_LIBS)

lua_runtime_SOURCES = \
	lua_runtime.c
lua_runtime_DEPENDENCIES = $(top_builddir)/src/lua/mc-lua.la
lua_runtime_LDADD = $(GMODULE_LIBS)

terminal_SOURCES = \
	terminal.c

tty_SOURCES = \
	tty.c

utilunix__mc_pstream_get_string_SOURCES = \
	utilunix__mc_pstream_get_string.c

utilunix__my_system_fork_fail_SOURCES = \
	utilunix__my_system-fork_fail.c

utilunix__my_system_fork_child_shell_SOURCES = \
	utilunix__my_system-fork_child_shell.c

utilunix__my_system_fork_child_SOURCES = \
	utilunix__my_system-fork_child.c

x_basename_SOURCES = \
	x_basename.c
