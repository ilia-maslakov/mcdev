PACKAGE_STRING = "/src/editor"

AM_CPPFLAGS = \
	-DTEST_SHARE_DIR=\"$(abs_srcdir)/../fixtures\" \
	$(GLIB_CFLAGS) \
	-I$(top_srcdir) \
	@CHECK_CFLAGS@

LIBS = @CHECK_LIBS@ \
	$(top_builddir)/src/editor/libedit.la \
	$(top_builddir)/src/libinternal.la \
	$(top_builddir)/lib/libmc.la

if ENABLE_MCLIB
LIBS += $(GLIB_LIBS)
endif

EXTRA_DIST = edit_complete_word_cmd_test_data.txt.in

TESTS = \
	edit_complete_word_cmd \
	edit_fold \
	edit_insert_column_of_text \
	edit_line_local_syntax \
	edit_plugin_etags \
	edit_undo_history \
	edit_undo_modified \
	edit_save_sudo \
	edit_replace_cmd \
	nav_probe \
	ctags_parser_tests \
	ctags_index_tests \
	ctags_fuzzy_tests

check_PROGRAMS = $(TESTS)

edit_complete_word_cmd_SOURCES = \
	edit_complete_word_cmd.c

edit_fold_SOURCES = \
	edit_fold.c

edit_insert_column_of_text_SOURCES = \
	edit_insert_column_of_text.c

edit_line_local_syntax_SOURCES = \
	edit_line_local_syntax.c

edit_plugin_etags_SOURCES = \
	edit_plugin_etags.c

edit_undo_history_SOURCES = \
	edit_undo_history.c

edit_undo_modified_SOURCES = \
	edit_undo_modified.c

edit_save_sudo_SOURCES = \
	edit_save_sudo.c

edit_replace_cmd_SOURCES = \
	edit_replace_cmd.c

nav_probe_SOURCES = \
	nav_probe.c

ctags_parser_tests_SOURCES = \
	ctags_parser_tests.c

ctags_index_tests_SOURCES = \
	ctags_index_tests.c

ctags_fuzzy_tests_SOURCES = \
	ctags_fuzzy_tests.c
