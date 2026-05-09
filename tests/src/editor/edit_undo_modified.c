/*
   src/editor - tests for undo modified-state tracking

   Copyright (C) 2026
   Free Software Foundation, Inc.
*/

#define TEST_SUITE_NAME "/src/editor"

#include "tests/mctest.h"

#include "lib/charsets.h"
#include "src/selcodepage.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"

static WGroup owner;
static WEdit *test_edit;

/* --------------------------------------------------------------------------------------------- */

static void
setup (void)
{
    WRect r;

    str_init_strings (NULL);

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
    group_add_widget (&owner, WIDGET (test_edit));

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
    edit_clean (test_edit);
    group_remove_widget (test_edit);
    g_free (test_edit);

    free_codepages_list ();
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

    return mctest_run_all (tc_core);
}
