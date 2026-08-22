/*
   src/editor - tests for undo modified-state tracking

   Copyright (C) 2026
   Free Software Foundation, Inc.
*/

#define TEST_SUITE_NAME "/src/editor"

#include "tests/mctest.h"

#include "lib/charsets.h"
#include "lib/event.h"
#include "lib/runtime-events.h"
#include "src/selcodepage.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/events_init.h"
#include "src/runtime-host.h"

/* A dialog, not a bare group: the editor reaches its owner as one, and a
 * WGroup has no room for the fields it reads there. */
static WDialog owner;
static WEdit *test_edit;

/* --------------------------------------------------------------------------------------------- */

static void
setup (void)
{
    WRect r;
    GError *error = NULL;

    str_init_strings (NULL);
    ck_assert_msg (events_init (&error), "events init failed: %s",
                   error != NULL ? error->message : "unknown error");
    events_publish_runtime_startup ();

    mc_global.sysconfig_dir = (char *) TEST_SHARE_DIR;
    load_codepages_list ();

    edit_options.filesize_threshold = (char *) "64M";
    edit_options.group_undo = FALSE;
    edit_options.persistent_selections = TRUE;
    edit_options.cursor_beyond_eol = FALSE;
    edit_options.fake_half_tabs = FALSE;
    edit_options.backspace_through_tabs = FALSE;

    rect_init (&r, 0, 0, 24, 80);
    test_edit = edit_init (NULL, &r, NULL);
    memset (&owner, 0, sizeof (owner));
    group_add_widget (&owner.group, WIDGET (test_edit));

    mc_global.source_codepage = 0;
    mc_global.display_codepage = 0;
    cp_source = "ASCII";
    cp_display = "ASCII";

    do_set_codepage (0);
    edit_set_codeset (test_edit);
}

/* --------------------------------------------------------------------------------------------- */

static void
teardown (void)
{
    GError *error = NULL;

    edit_clean (test_edit);
    group_remove_widget (test_edit);
    g_free (test_edit);

    free_codepages_list ();
    ck_assert (events_deinit (&error));
    g_clear_error (&error);
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

static void
test_insert_char (int c)
{
    edit_execute_key_command (test_edit, CK_InsertChar, c);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_mark_saved (void)
{
    test_edit->modified = 0;
    test_edit->undo_content_saved = test_edit->undo_content_seq;
    test_edit->undo_content_saved_gen = test_edit->undo_content_gen;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_assert_text (const char *expected)
{
    GString *actual;
    off_t i;

    actual = g_string_new ("");

    for (i = 0; i < test_edit->buffer.size; i++)
    {
        const int chr = edit_buffer_get_byte (&test_edit->buffer, i);

        g_string_append_c (actual, (gchar) chr);
    }

    mctest_assert_str_eq (actual->str, expected);
    g_string_free (actual, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_undo_to_saved_content_clears_modified)
{
    test_insert_char ('a');
    test_mark_saved ();

    test_insert_char ('b');
    ck_assert_int_eq (test_edit->modified, 1);

    edit_execute_key_command (test_edit, CK_Undo, -1);

    test_assert_text ("a");
    ck_assert_int_eq (test_edit->modified, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_history_branch_does_not_clear_modified_at_same_depth)
{
    test_insert_char ('a');
    test_mark_saved ();

    edit_execute_key_command (test_edit, CK_Undo, -1);
    test_insert_char ('b');
    edit_execute_key_command (test_edit, CK_Undo, -1);
    edit_execute_key_command (test_edit, CK_Redo, -1);

    test_assert_text ("b");
    ck_assert_int_eq (test_edit->modified, 1);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_redo_delete_of_blank_character)
{
    test_insert_char (' ');
    edit_cursor_move (test_edit, -1);
    edit_push_key_press (test_edit);
    edit_delete (test_edit, FALSE);

    test_assert_text ("");

    edit_execute_key_command (test_edit, CK_Undo, -1);
    test_assert_text (" ");

    edit_execute_key_command (test_edit, CK_Redo, -1);
    test_assert_text ("");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Cursor move after undo clears the redo stack but must not cause modified to be
 * incorrectly cleared when a new branch is later created and then redone.
 *
 * Sequence: type 'a','b' -> save -> undo 'b' -> cursor left (clears redo) ->
 *           type 'c' -> undo 'c' -> redo 'c'.
 * Buffer is now "ac", not "ab", so modified must remain 1. */
START_TEST (test_cursor_clears_redo_branch_keeps_modified)
{
    test_insert_char ('a');
    test_insert_char ('b');
    test_mark_saved ();

    edit_execute_key_command (test_edit, CK_Undo, -1);
    test_assert_text ("a");

    /* cursor left: clears redo stack without a content op */
    edit_execute_key_command (test_edit, CK_Left, -1);

    test_insert_char ('c');
    edit_execute_key_command (test_edit, CK_Undo, -1);
    edit_execute_key_command (test_edit, CK_Redo, -1);

    test_assert_text ("ca");
    ck_assert_int_eq (test_edit->modified, 1);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Cursor move after undo (while already on saved content) clears the redo stack and
 * bumps gen, but the content still matches the save point.  A subsequent type+undo
 * must land back on saved content and clear modified.
 *
 * Sequence: type 'a' -> save -> type 'b' -> undo 'b' (modified=0) ->
 *           cursor left (clears content redo, bumps gen) ->
 *           type 'c' -> undo 'c'.
 * Buffer is "a" = saved state, so modified must be 0. */
START_TEST (test_cursor_clear_redo_on_saved_content_no_false_modified)
{
    test_insert_char ('a');
    test_mark_saved ();
    test_insert_char ('b');
    edit_execute_key_command (test_edit, CK_Undo, -1);
    ck_assert_int_eq (test_edit->modified, 0);

    edit_execute_key_command (test_edit, CK_Left, -1);

    test_insert_char ('c');
    edit_execute_key_command (test_edit, CK_Undo, -1);

    test_assert_text ("a");
    ck_assert_int_eq (test_edit->modified, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_reload_preserves_runtime_handle_and_advances_revision)
{
    const mc_runtime_handle_t handle =
        mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_EDITOR, test_edit);
    const guint64 revision = test_edit->runtime_revision;

    ck_assert (edit_reload_line (test_edit, NULL));
    ck_assert_ptr_eq (mc_runtime_handle_resolve (&handle, MC_RUNTIME_HANDLE_EDITOR), test_edit);
    ck_assert_uint_gt (test_edit->runtime_revision, revision);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_insert_materializes_virtual_columns)
{
    const mc_runtime_handle_t handle =
        mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_EDITOR, test_edit);
    const char *error = NULL;

    test_edit->over_col = 4;
    ck_assert_msg (runtime_host_editor_insert (&handle, "X", &error), "%s",
                   error != NULL ? error : "insert failed");
    test_assert_text ("    X");
    ck_assert_int_eq (test_edit->over_col, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_edit_is_atomic_and_revision_checked)
{
    const mc_runtime_handle_t handle =
        mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_EDITOR, test_edit);
    mc_runtime_editor_change_t changes[2] = {
        { 1, 3, "BC", 2 },
        { 4, 6, "EF", 2 },
    };
    mc_runtime_editor_edit_t edit_spec;
    mc_runtime_editor_edit_result_t result;
    const char *error = NULL;
    guint64 revision;

    for (const char *text = "abcdef"; *text != '\0'; text++)
        edit_insert (test_edit, *text);
    revision = test_edit->runtime_revision;
    edit_spec = (mc_runtime_editor_edit_t) {
        .revision = revision,
        .changes = changes,
        .changes_count = G_N_ELEMENTS (changes),
    };

    ck_assert_msg (runtime_host_editor_edit (&handle, &edit_spec, &result, &error), "%s",
                   error != NULL ? error : "edit failed");
    test_assert_text ("aBCdEF");
    ck_assert_uint_eq (result.revision, revision + 1);

    edit_execute_key_command (test_edit, CK_Undo, -1);
    test_assert_text ("abcdef");

    edit_spec.revision = revision;
    ck_assert (!runtime_host_editor_edit (&handle, &edit_spec, &result, &error));
    mctest_assert_str_eq (error, "stale_revision");
    test_assert_text ("abcdef");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_column_replace_pads_short_lines)
{
    const mc_runtime_handle_t handle =
        mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_EDITOR, test_edit);
    mc_runtime_editor_edit_result_t result;
    const char *error = NULL;

    for (const char *text = "a\nbb"; *text != '\0'; text++)
        edit_insert (test_edit, *text);
    edit_set_markers (test_edit, 0, test_edit->buffer.size, 3, 4);
    test_edit->column_highlight = 1;

    ck_assert_msg (runtime_host_editor_replace_selection (&handle, "X", 1, &result, &error), "%s",
                   error != NULL ? error : "replace selection failed");
    test_assert_text ("a  X\nbb X");
    ck_assert_int_eq (test_edit->column_highlight, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");
    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_undo_to_saved_content_clears_modified);
    tcase_add_test (tc_core, test_history_branch_does_not_clear_modified_at_same_depth);
    tcase_add_test (tc_core, test_redo_delete_of_blank_character);
    tcase_add_test (tc_core, test_cursor_clears_redo_branch_keeps_modified);
    tcase_add_test (tc_core, test_cursor_clear_redo_on_saved_content_no_false_modified);
    tcase_add_test (tc_core, test_reload_preserves_runtime_handle_and_advances_revision);
    tcase_add_test (tc_core, test_runtime_insert_materializes_virtual_columns);
    tcase_add_test (tc_core, test_runtime_edit_is_atomic_and_revision_checked);
    tcase_add_test (tc_core, test_runtime_column_replace_pads_short_lines);

    return mctest_run_all (tc_core);
}
