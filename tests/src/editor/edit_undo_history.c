/*
   src/editor - tests for Undo History dialog: group collection, labels,
   UTF-8 character counts, preview truncation and redo tracking.

   Copyright (C) 2026
   Free Software Foundation, Inc.
*/

#define TEST_SUITE_NAME "/src/editor"

#include "tests/mctest.h"

#include "lib/charsets.h"
#include "src/selcodepage.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editcmd_private.h"

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
/* Check that a label for group n contains the expected substring. */
static void
assert_label_contains (gboolean is_redo, guint n, const char *expected)
{
    char label[256];
    gboolean ok;

    ok = edit_undo_history_get_label (test_edit, is_redo, n, label, sizeof (label));
    ck_assert_msg (ok, "no history entry at is_redo=%d n=%u", (int) is_redo, n);
    ck_assert_msg (strstr (label, expected) != NULL,
                   "label[%u] = \"%s\", expected substring \"%s\"", n, label, expected);
}

static void
assert_label_not_contains (gboolean is_redo, guint n, const char *unexpected)
{
    char label[256];
    gboolean ok;

    ok = edit_undo_history_get_label (test_edit, is_redo, n, label, sizeof (label));
    ck_assert_msg (ok, "no history entry at is_redo=%d n=%u", (int) is_redo, n);
    ck_assert_msg (strstr (label, unexpected) == NULL,
                   "label[%u] = \"%s\", must NOT contain \"%s\"", n, label, unexpected);
}

/* --------------------------------------------------------------------------------------------- */
/* Three printable ASCII chars without spaces coalesce into a single "insert 3" group. */

START_TEST (test_ascii_coalesce)
{
    test_insert_char ('a');
    test_insert_char ('b');
    test_insert_char ('c');

    assert_label_contains (FALSE, 0, "insert 3");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* Space breaks a coalesce run: "abc" + ' ' + "de" produces three groups:
 * group 0 = "de" (insert 2), group 1 = " " (insert 1), group 2 = "abc" (insert 3). */

START_TEST (test_space_breaks_coalesce)
{
    test_insert_char ('a');
    test_insert_char ('b');
    test_insert_char ('c');
    test_insert_char (' ');
    test_insert_char ('d');
    test_insert_char ('e');

    assert_label_contains (FALSE, 0, "insert 2"); /* de coalesced */
    assert_label_contains (FALSE, 1, "insert 1"); /* space is its own group */
    assert_label_contains (FALSE, 2, "insert 3"); /* abc coalesced */
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* Inserting a char and then undoing it moves the entry to the redo stack. */

START_TEST (test_redo_entry_after_undo)
{
    test_insert_char ('x');
    edit_execute_key_command (test_edit, CK_Undo, -1);

    assert_label_contains (TRUE, 0, "[+]");
    assert_label_contains (TRUE, 0, "insert 1");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* Deleting a char records a delete group. */

START_TEST (test_delete_entry)
{
    test_insert_char ('z');
    edit_cursor_move (test_edit, -1);
    edit_push_key_press (test_edit);
    edit_delete (test_edit, FALSE);

    assert_label_contains (FALSE, 0, "delete 1");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* Long ASCII insert (>30 chars) truncates preview with '~'. */

START_TEST (test_preview_truncation_ascii)
{
    int i;

    for (i = 0; i < 35; i++)
        test_insert_char ('a' + (i % 26));

    assert_label_contains (FALSE, 0, "~");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* UTF-8: label shows character count, not byte count.
 * "мир" = 3 Cyrillic chars = 6 bytes.  With utf8=TRUE the label must say "insert 3". */

START_TEST (test_utf8_char_count)
{
    test_edit->utf8 = TRUE;

    /* м = U+043C: 0xD0 0xBC */
    test_insert_char (0xD0);
    test_insert_char (0xBC);
    /* и = U+0438: 0xD0 0xB8 */
    test_insert_char (0xD0);
    test_insert_char (0xB8);
    /* р = U+0440: 0xD1 0x80 */
    test_insert_char (0xD1);
    test_insert_char (0x80);

    assert_label_contains (FALSE, 0, "insert 3");
    assert_label_not_contains (FALSE, 0, "insert 6");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* UTF-8: long Cyrillic insert (>15 chars) truncates preview with '~'. */

START_TEST (test_utf8_preview_truncation)
{
    int i;

    test_edit->utf8 = TRUE;

    /* Insert 20 copies of "й" (U+0439: 0xD0 0xB9) = 20 chars, 40 bytes.
     * Preview buffer is 30 bytes so ~15 chars fit before truncation. */
    for (i = 0; i < 20; i++)
    {
        test_insert_char (0xD0);
        test_insert_char (0xB9);
    }

    assert_label_contains (FALSE, 0, "insert 20");
    assert_label_contains (FALSE, 0, "~");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_fast_ascii_cursor_move_undo)
{
    const off_t size = 4099;
    off_t i;

    for (i = 0; i < size; i++)
        edit_buffer_insert (&test_edit->buffer, 'a');

    edit_push_key_press (test_edit);
    edit_cursor_move (test_edit, -size);
    ck_assert_int_eq (test_edit->buffer.curs1, 0);
    ck_assert_uint_lt (test_edit->undo_stack_pointer, 8);

    edit_execute_key_command (test_edit, CK_Undo, -1);
    ck_assert_int_eq (test_edit->buffer.curs1, size);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");
    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_ascii_coalesce);
    tcase_add_test (tc_core, test_fast_ascii_cursor_move_undo);
    tcase_add_test (tc_core, test_space_breaks_coalesce);
    tcase_add_test (tc_core, test_redo_entry_after_undo);
    tcase_add_test (tc_core, test_delete_entry);
    tcase_add_test (tc_core, test_preview_truncation_ascii);
    tcase_add_test (tc_core, test_utf8_char_count);
    tcase_add_test (tc_core, test_utf8_preview_truncation);

    return mctest_run_all (tc_core);
}
