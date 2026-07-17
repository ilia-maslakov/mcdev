/*
   src/editor - tests for line-local syntax highlighting

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.
 */

#define TEST_SUITE_NAME "/src/editor"

#include "tests/mctest.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include "lib/global.h"
#include "lib/skin.h"
#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"

static WEdit *test_edit;

/* --------------------------------------------------------------------------------------------- */

static void
setup (void)
{
    test_edit = g_new0 (WEdit, 1);
    test_edit->syntax_line_local = TRUE;
    test_edit->syntax_line_local_number_max = 16;
    test_edit->syntax_line_local_number_color = 1;
    test_edit->syntax_line_local_single_quote_color = 2;
    test_edit->syntax_line_local_double_quote_color = 2;
    test_edit->syntax_line_local_symbols = g_strdup ("{}[]()/;,.");
    test_edit->syntax_line_local_symbols_color = 3;
    test_edit->buffer.b1 = g_ptr_array_new_with_free_func (g_free);
    test_edit->buffer.b2 = g_ptr_array_new_with_free_func (g_free);
    test_edit->buffer.one_byte_per_column = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
teardown (void)
{
    g_ptr_array_free (test_edit->buffer.b1, TRUE);
    g_ptr_array_free (test_edit->buffer.b2, TRUE);
    if (test_edit->line_layout_caches != NULL)
        g_ptr_array_free (test_edit->line_layout_caches, TRUE);
    g_free (test_edit->undo_stack);
    g_free (test_edit->syntax_line_local_symbols);
    g_free (test_edit);
}

/* --------------------------------------------------------------------------------------------- */

static void
load_text (const char *text)
{
    for (; *text != '\0'; text++)
        edit_buffer_insert (&test_edit->buffer, *text);
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_line_local_syntax)
{
    static const char text[] = "12 12345678901234567 \"34\" '56' {}[]()/;,.\n\"broken\n78";
    edit_line_local_syntax_state_t state;
    int colors[sizeof (text) - 1];
    const char *p;
    off_t i;

    load_text (text);
    edit_line_local_syntax_reset (&state, 0);

    for (i = 0; i < test_edit->buffer.size; i++)
        colors[i] = edit_get_line_local_syntax_color (test_edit, &state, i);

    ck_assert_int_eq (colors[0], 1);
    ck_assert_int_eq (colors[1], 1);

    p = strstr (text, "12345678901234567");
    for (i = p - text; i < p - text + 17; i++)
        ck_assert_int_eq (colors[i], EDITOR_NORMAL_COLOR);

    p = strstr (text, "\"34\"");
    for (i = p - text; i < p - text + 4; i++)
        ck_assert_int_eq (colors[i], 2);

    edit_line_local_syntax_reset (&state, p - text + 1);
    ck_assert_int_eq (edit_get_line_local_syntax_color (test_edit, &state, p - text + 1), 1);

    p = strstr (text, "'56'");
    for (i = p - text; i < p - text + 4; i++)
        ck_assert_int_eq (colors[i], 2);

    p = strstr (text, "{}[]()/;,.");
    for (i = p - text; i < p - text + 10; i++)
        ck_assert_int_eq (colors[i], 3);

    p = strrchr (text, '\n');
    ck_assert_int_eq (colors[p - text + 1], 1);
    ck_assert_int_eq (colors[p - text + 2], 1);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_fast_ascii_layout)
{
    load_text ("1234567890");
    test_edit->buffer.curs1 = 7;
    test_edit->syntax_line_local = FALSE;

    ck_assert (edit_has_fast_ascii_layout (test_edit));
    ck_assert_int_eq (edit_buffer_get_bol (&test_edit->buffer, test_edit->buffer.curs1), 0);
    ck_assert_int_eq (edit_buffer_get_eol (&test_edit->buffer, test_edit->buffer.curs1),
                      test_edit->buffer.size);
    ck_assert_int_eq (edit_get_col (test_edit), 7);
    ck_assert_int_eq (edit_move_forward3 (test_edit, 0, 5, 0), 5);
    ck_assert_int_eq (edit_move_forward3 (test_edit, 2, 0, 7), 5);
    ck_assert_int_eq (edit_move_forward3 (test_edit, 8, 5, 0), test_edit->buffer.size);

    test_edit->buffer.curs1 = test_edit->buffer.size;
    edit_buffer_insert (&test_edit->buffer, '\t');
    ck_assert (!edit_has_fast_ascii_layout (test_edit));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_single_line_utf8_layout)
{
    static const char text[] = "A\t\xe2\x82\xac\xe7\x95\x8c"
                               "B";
    gboolean old_utf8_display;
    long expected_column;
    long column;
    off_t offset;

    load_text (text);
    test_edit->utf8 = TRUE;
    old_utf8_display = mc_global.utf8_display;
    mc_global.utf8_display = TRUE;
    test_edit->undo_stack_size = START_STACK_SIZE;
    test_edit->undo_stack_size_mask = START_STACK_SIZE - 1;
    test_edit->undo_stack = g_malloc0 ((START_STACK_SIZE + 10) * sizeof (long));

    expected_column = (long) edit_move_forward3 (test_edit, 0, 0, test_edit->buffer.size);
    edit_update_curs_col (test_edit);
    ck_assert_int_eq (test_edit->curs_col, expected_column);

    for (column = 0; column <= expected_column; column++)
    {
        long actual_column;
        off_t expected_offset;

        offset = edit_get_line_offset (test_edit, 0, column, &actual_column);
        expected_offset = edit_move_forward3 (test_edit, 0, column, 0);
        ck_assert_int_eq (offset, expected_offset);
        ck_assert_int_eq (actual_column, edit_move_forward3 (test_edit, 0, 0, expected_offset));
    }

    edit_cursor_move (test_edit, -1);
    edit_update_curs_col (test_edit);
    ck_assert_int_eq (test_edit->curs_col,
                      edit_move_forward3 (test_edit, 0, 0, test_edit->buffer.curs1));

    mc_global.utf8_display = old_utf8_display;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_single_line_utf8_layout_checkpoints)
{
    static const char text[] = "A\t\xe2\x82\xac\xe7\x95\x8c"
                               "B";
    gboolean old_utf8_display;
    long expected_column;
    long actual_column;
    long target_column;
    off_t expected_offset;
    off_t offset;
    int i;

    for (i = 0; i < 20000; i++)
        load_text (text);

    test_edit->utf8 = TRUE;
    old_utf8_display = mc_global.utf8_display;
    mc_global.utf8_display = TRUE;

    expected_column = (long) edit_move_forward3 (test_edit, 0, 0, test_edit->buffer.size);
    edit_update_curs_col (test_edit);
    target_column = expected_column - 97;
    expected_offset = edit_move_forward3 (test_edit, 0, target_column, 0);
    offset = edit_get_line_offset (test_edit, 0, target_column, &actual_column);

    ck_assert_int_eq (offset, expected_offset);
    ck_assert_int_eq (actual_column, edit_move_forward3 (test_edit, 0, 0, expected_offset));

    mc_global.utf8_display = old_utf8_display;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_multiline_utf8_layout_cache)
{
    static const char text[] = "A\t\xe2\x82\xac\xe7\x95\x8c"
                               "B";
    gboolean old_utf8_display;
    const off_t line_size = sizeof (text) - 1;
    off_t bol[3];
    off_t expected_offset;
    off_t offset;
    long actual_column;
    long expected_column;
    long target_column;
    int i;

    for (i = 0; i < 3; i++)
    {
        bol[i] = test_edit->buffer.size;
        for (int j = 0; j < 12000; j++)
            load_text (text);
        if (i != 2)
            edit_buffer_insert (&test_edit->buffer, '\n');
    }
    test_edit->buffer.lines = 2;
    test_edit->utf8 = TRUE;
    old_utf8_display = mc_global.utf8_display;
    mc_global.utf8_display = TRUE;

    test_edit->buffer.curs_line = 2;
    test_edit->curs_bol = bol[2];
    test_edit->curs_bol_valid = TRUE;
    edit_update_curs_col (test_edit);
    expected_column = (long) edit_move_forward3 (test_edit, bol[2], 0, test_edit->buffer.size);
    ck_assert_int_eq (test_edit->curs_col, expected_column);

    edit_buffer_move_cursor_fast (&test_edit->buffer,
                                  bol[1] + 12000 * line_size - test_edit->buffer.curs1);
    test_edit->buffer.curs_line = 1;
    test_edit->curs_bol = bol[1];
    edit_update_curs_col (test_edit);
    ck_assert_int_eq (test_edit->line_layout_caches->len, 2);

    edit_buffer_move_cursor_fast (&test_edit->buffer,
                                  test_edit->buffer.size - test_edit->buffer.curs1);
    test_edit->buffer.curs_line = 2;
    test_edit->curs_bol = bol[2];
    edit_update_curs_col (test_edit);

    target_column = expected_column - 97;
    expected_offset = edit_move_forward3 (test_edit, bol[2], target_column, 0);
    offset = edit_get_line_offset (test_edit, bol[2], target_column, &actual_column);
    ck_assert_int_eq (offset, expected_offset);
    ck_assert_int_eq (actual_column, edit_move_forward3 (test_edit, bol[2], 0, expected_offset));

    mc_global.utf8_display = old_utf8_display;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_current_eol_cache)
{
    off_t bol_long;
    int i;

    // three lines; the middle one is long
    load_text ("aa\n");
    bol_long = test_edit->buffer.size;
    for (i = 0; i < 5000; i++)
        load_text ("x");
    load_text ("\nbb");
    test_edit->buffer.lines = 2;

    // put the cursor inside the long line
    edit_buffer_move_cursor_fast (&test_edit->buffer, (bol_long + 10) - test_edit->buffer.curs1);
    test_edit->curs_bol = bol_long;
    test_edit->curs_bol_valid = TRUE;
    test_edit->curs_eol_valid = FALSE;

    // cached eol and the "line below" helper must match the ground truth
    ck_assert_int_eq (edit_get_current_eol (test_edit),
                      edit_buffer_get_current_eol (&test_edit->buffer));
    ck_assert_int_eq (edit_line_next_offset (test_edit, bol_long),
                      edit_buffer_get_forward_offset (&test_edit->buffer, bol_long, 1, 0));
    // a non-cursor line gets its own cached end offset
    ck_assert_int_eq (edit_line_next_offset (test_edit, 0),
                      edit_buffer_get_forward_offset (&test_edit->buffer, 0, 1, 0));

    // grow the current line through the editor API; it must invalidate this line's EOL
    test_edit->undo_stack_size = START_STACK_SIZE;
    test_edit->undo_stack_size_mask = START_STACK_SIZE - 1;
    test_edit->undo_stack = g_malloc0 ((START_STACK_SIZE + 10) * sizeof (long));
    edit_insert (test_edit, 'y');
    ck_assert_int_eq (edit_get_current_eol (test_edit),
                      edit_buffer_get_current_eol (&test_edit->buffer));
    ck_assert_int_eq (edit_line_next_offset (test_edit, bol_long),
                      edit_buffer_get_forward_offset (&test_edit->buffer, bol_long, 1, 0));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_perf_long_utf8_line)
{
    gboolean old_utf8_display = mc_global.utf8_display;
    off_t curs;
    long col;
    int i;
    clock_t t0, t1;
    double ms;

    test_edit->syntax_line_local = FALSE;
    test_edit->utf8 = TRUE;
    mc_global.utf8_display = TRUE;

    for (i = 0; i < 100000; i++)
        load_text ("\xe2\x82\xac");  // euro sign, 3 bytes, 1 column
    test_edit->curs_bol = 0;
    test_edit->curs_bol_valid = TRUE;

    edit_update_curs_col (test_edit);  // warm the cache (this one pays O(col) once)

    // Simulate typing more euro signs at the end of the long line, one byte at a time, with a
    // redraw-style column/offset query after every byte - exactly what the editor does per key.
    t0 = clock ();
    for (i = 0; i < 2000; i++)
    {
        static const unsigned char euro[] = { 0xe2, 0x82, 0xac };
        size_t j;

        for (j = 0; j < sizeof (euro); j++)
        {
            edit_buffer_insert (&test_edit->buffer, euro[j]);
            edit_update_curs_col (test_edit);
            (void) edit_get_line_offset (test_edit, 0, MAX (0, test_edit->curs_col - 80), &col);
        }
    }
    t1 = clock ();
    curs = test_edit->buffer.curs1;
    ms = (double) (t1 - t0) * 1000.0 / CLOCKS_PER_SEC;
    printf ("[perf] typing 2000 utf8 chars at the end of a long line (per-byte redraw): %.1f ms\n",
            ms);
    // final column must be exactly one per euro sign
    ck_assert_int_eq (test_edit->curs_col, (long) edit_move_forward3 (test_edit, 0, 0, curs));
    ck_assert (ms < 1000.0);  // stale-cache corruption makes this O(col) per key -> seconds

    mc_global.utf8_display = old_utf8_display;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_incremental_utf8_insert)
{
    // U+20AC EURO SIGN, encoded as E2 82 AC, is one display column but three bytes.
    // A multibyte character is typed one byte at a time, so the layout cache must not keep a
    // stale anchor in the middle of the half-typed character.  Reproduces the cursor overshoot.
    static const unsigned char euro[] = { 0xE2, 0x82, 0xAC };
    gboolean old_utf8_display = mc_global.utf8_display;
    size_t i;

    test_edit->syntax_line_local = FALSE;
    test_edit->utf8 = TRUE;
    mc_global.utf8_display = TRUE;

    load_text ("abc");
    test_edit->curs_bol = 0;
    test_edit->curs_bol_valid = TRUE;

    for (i = 0; i < sizeof (euro); i++)
    {
        edit_buffer_insert (&test_edit->buffer, euro[i]);
        edit_update_curs_col (test_edit);
        ck_assert_int_eq (test_edit->curs_col,
                          (long) edit_move_forward3 (test_edit, 0, 0, test_edit->buffer.curs1));
    }

    // "abc" plus one column for the euro sign
    ck_assert_int_eq (test_edit->curs_col, 4);

    mc_global.utf8_display = old_utf8_display;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_fast_buffer_cursor_move)
{
    const off_t size = 200003;
    off_t i;

    for (i = 0; i < size; i++)
        edit_buffer_insert (&test_edit->buffer, 'A' + (i % 26));

    edit_buffer_move_cursor_fast (&test_edit->buffer, -size);
    ck_assert_int_eq (test_edit->buffer.curs1, 0);
    ck_assert_int_eq (test_edit->buffer.curs2, size);

    for (i = 0; i < size; i++)
        ck_assert_int_eq (edit_buffer_get_byte (&test_edit->buffer, i), 'A' + (i % 26));

    edit_buffer_move_cursor_fast (&test_edit->buffer, size);
    ck_assert_int_eq (test_edit->buffer.curs1, size);
    ck_assert_int_eq (test_edit->buffer.curs2, 0);

    for (i = 0; i < size; i++)
        ck_assert_int_eq (edit_buffer_get_byte (&test_edit->buffer, i), 'A' + (i % 26));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");
    tcase_set_timeout (tc_core,
                       30);  // the long-line tests build large buffers; avoid wall-clock flakiness
    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_line_local_syntax);
    tcase_add_test (tc_core, test_fast_ascii_layout);
    tcase_add_test (tc_core, test_single_line_utf8_layout);
    tcase_add_test (tc_core, test_single_line_utf8_layout_checkpoints);
    tcase_add_test (tc_core, test_multiline_utf8_layout_cache);
    tcase_add_test (tc_core, test_current_eol_cache);
    tcase_add_test (tc_core, test_perf_long_utf8_line);
    tcase_add_test (tc_core, test_incremental_utf8_insert);
    tcase_add_test (tc_core, test_fast_buffer_cursor_move);

    return mctest_run_all (tc_core);
}
