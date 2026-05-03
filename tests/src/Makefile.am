PACKAGE_STRING = "/src"

SUBDIRS = . filemanager vfs panel-plugins viewer

if USE_INTERNAL_EDIT
SUBDIRS += editor
endif

AM_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/lib/vfs \
	-DTEST_SHARE_DIR=\"$(abs_srcdir)/fixtures\" \
	@CHECK_CFLAGS@

LIBS = @CHECK_LIBS@ \
	$(top_builddir)/src/libinternal.la \
	$(top_builddir)/lib/libmc.la

if ENABLE_MCLIB
LIBS += $(GLIB_LIBS)
endif

EXTRA_DIST = \
	fixtures/mc.charsets \
	fixtures/hints/mc.hint \
	fixtures/viewertest.txt \
	execute__common.c

TESTS = \
	execute__execute_with_vfs_arg \
	execute__execute_external_editor_or_viewer \
	execute__execute_get_external_cmd_opts_from_config \
	file_history \
	keymap_reload \
	learn_modifiers \
	term_key_merge \
	usermenu__test_condition \
	usermenu__test_expand_format

if ENABLE_MCTERM
TESTS += \
	mcterm_key_encode \
	mcterm_osc7_parse
endif

check_PROGRAMS = $(TESTS)

execute__execute_with_vfs_arg_SOURCES = \
	execute__execute_with_vfs_arg.c


execute__execute_external_editor_or_viewer_SOURCES = \
	execute__execute_external_editor_or_viewer.c


execute__execute_get_external_cmd_opts_from_config_SOURCES = \
	execute__execute_get_external_cmd_opts_from_config.c

file_history_SOURCES = \
	file_history.c

keymap_reload_SOURCES = \
	keymap_reload.c

learn_modifiers_SOURCES = \
	learn_modifiers.c

mcterm_key_encode_SOURCES = \
	mcterm_key_encode.c

mcterm_osc7_parse_SOURCES = \
	mcterm_osc7_parse.c


term_key_merge_SOURCES = \
	term_key_merge.c

usermenu__test_condition_SOURCES = \
	usermenu__test_condition.c

usermenu__test_expand_format_SOURCES = \
	usermenu__test_expand_format.c
