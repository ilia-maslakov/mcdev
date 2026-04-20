/*
   src/viewer - vterm terminal-mode unit tests

   Regression tests for scroll-region semantics (DECSTBM), VPA, ECH,
   colored erase, cursor clamping, and size-aware reset.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#define TEST_SUITE_NAME "/src/viewer/vterm_terminal"

#include "tests/mctest.h"

#include "src/viewer/ansi.h"
#include "src/viewer/terminal_buffer.h"
#include "src/viewer/vterm.h"

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static void
feed_bytes (mcview_vterm_t *vt, const char *data, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++)
    {
        vterm_event_t ev = mcview_vterm_feed (vt, (unsigned char) data[i]);
        mcview_vterm_apply_event (vt, &ev);
    }
}

/* --------------------------------------------------------------------------------------------- */

#define FEED(vt, s) feed_bytes ((vt), (s), sizeof (s) - 1)

/* --------------------------------------------------------------------------------------------- */

static gunichar
cell_ch (mcview_vterm_t *vt, int row, int col)
{
    const mcview_vterm_cell_t *cell;

    cell = mcview_terminal_buffer_get (mcview_vterm_buf (vt), row, col);
    return (cell != NULL) ? cell->ch : 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
cell_bg (mcview_vterm_t *vt, int row, int col)
{
    const mcview_vterm_cell_t *cell;

    cell = mcview_terminal_buffer_get (mcview_vterm_buf (vt), row, col);
    return (cell != NULL) ? cell->attr.bg : MCVIEW_ANSI_COLOR_DEFAULT;
}

/* --------------------------------------------------------------------------------------------- */

static char *
canvas_to_text (mcview_vterm_t *vt, int rows, int cols)
{
    GString *s;
    int row, col;

    s = g_string_new ("");
    for (row = 0; row < rows; row++)
    {
        for (col = 0; col < cols; col++)
        {
            gunichar ch = cell_ch (vt, row, col);
            g_string_append_c (s, ch ? (char) ch : ' ');
        }
        g_string_append_c (s, '\n');
    }
    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/* Tests *****************************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* LF at scroll_bottom scrolls region up; cursor stays */

START_TEST (test_scroll_region_lf_at_bottom_scrolls_up)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 20);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[2;4r"); /* DECSTBM 1-based 2..4 = 0-based 1..3 */
    FEED (vt, "\033[2;1H");
    FEED (vt, "A");
    FEED (vt, "\033[3;1H");
    FEED (vt, "B");
    FEED (vt, "\033[4;1H");
    FEED (vt, "C");
    FEED (vt, "\033[4;1H");
    FEED (vt, "\n");

    ck_assert_uint_eq (cell_ch (vt, 1, 0), 'B');
    ck_assert_uint_eq (cell_ch (vt, 2, 0), 'C');
    ck_assert_uint_eq (cell_ch (vt, 3, 0), ' ');
    ck_assert_uint_eq (cell_ch (vt, 0, 0), 0);
    ck_assert_int_eq (mcview_vterm_cursor_row (vt), 3);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* LF above scroll_top advances cursor without scrolling */

START_TEST (test_scroll_region_lf_above_region_advances_cursor)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 20);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[2;4r");
    FEED (vt, "\033[2;1H");
    FEED (vt, "X");
    FEED (vt, "\033[1;1H");
    FEED (vt, "\n");

    ck_assert_int_eq (mcview_vterm_cursor_row (vt), 1);
    ck_assert_uint_eq (cell_ch (vt, 1, 0), 'X');

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* VPA (CSI d) changes row, leaves col unchanged */

START_TEST (test_vpa_moves_row_only)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 10, 40);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[7C"); /* forward 7 -> col 7 */
    FEED (vt, "\033[4d"); /* VPA row 4 (1-based) = row 3 */

    ck_assert_int_eq (mcview_vterm_cursor_row (vt), 3);
    ck_assert_int_eq (mcview_vterm_cursor_col (vt), 7);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* ECH (CSI X) erases with current attrs; cursor does not move */

START_TEST (test_ech_erases_with_attrs_cursor_stays)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 20);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[H");
    FEED (vt, "XYZ");
    FEED (vt, "\033[H");
    FEED (vt, "\033[42m"); /* green bg */
    FEED (vt, "\033[2X");  /* erase 2 chars */

    ck_assert_uint_eq (cell_ch (vt, 0, 0), ' ');
    ck_assert_int_eq (cell_bg (vt, 0, 0), 2);
    ck_assert_uint_eq (cell_ch (vt, 0, 1), ' ');
    ck_assert_int_eq (cell_bg (vt, 0, 1), 2);
    ck_assert_uint_eq (cell_ch (vt, 0, 2), 'Z');
    ck_assert_int_eq (mcview_vterm_cursor_col (vt), 0);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* erase_eol fills to term_cols-1 with space and current attrs */

START_TEST (test_erase_eol_fills_full_width_with_attrs)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 10);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[43m"); /* yellow bg */
    FEED (vt, "\033[H");
    FEED (vt, "\033[K");

    ck_assert_uint_eq (cell_ch (vt, 0, 0), ' ');
    ck_assert_int_eq (cell_bg (vt, 0, 0), 3);
    ck_assert_uint_eq (cell_ch (vt, 0, 9), ' ');
    ck_assert_int_eq (cell_bg (vt, 0, 9), 3);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* DECSTBM out-of-range bottom is clamped to term_rows-1 */

START_TEST (test_decstbm_out_of_range_bottom_is_clamped)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 20);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[1;999r");
    FEED (vt, "\033[5;1H");
    FEED (vt, "Q");
    FEED (vt, "\033[5;1H");
    FEED (vt, "\n");

    ck_assert_int_eq (mcview_vterm_cursor_row (vt), 4);
    ck_assert_uint_eq (cell_ch (vt, 3, 0), 'Q');

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* CURSOR_FWD clamps cursor_col to term_cols-1 */

START_TEST (test_cursor_fwd_clamps_to_term_cols)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 10);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[999C");

    ck_assert_int_eq (mcview_vterm_cursor_col (vt), 9);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* CURSOR_ABS col is clamped to term_cols-1 */

START_TEST (test_cursor_abs_col_clamps_to_term_cols)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 10);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[1;999H");

    ck_assert_int_eq (mcview_vterm_cursor_col (vt), 9);
    ck_assert_int_eq (mcview_vterm_cursor_row (vt), 0);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* CURSOR_ABS row is clamped to term_rows-1 */

START_TEST (test_cursor_abs_row_clamps_to_term_rows)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 10);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[999;1H");

    ck_assert_int_eq (mcview_vterm_cursor_row (vt), 4);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* CURSOR_DOWN clamps cursor_row to term_rows-1 */

START_TEST (test_cursor_down_clamps_to_term_rows)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 10);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[999B");

    ck_assert_int_eq (mcview_vterm_cursor_row (vt), 4);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* erase_bol with clamped cursor_col does not expand row past term_cols */

START_TEST (test_erase_bol_does_not_expand_past_term_cols)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 5, 10);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[H");
    FEED (vt, "0123456789");
    FEED (vt, "\033[999C");

    ck_assert_int_eq (mcview_vterm_cursor_col (vt), 9);

    FEED (vt, "\033[1K");

    ck_assert_uint_eq (cell_ch (vt, 0, 0), ' ');
    ck_assert_uint_eq (cell_ch (vt, 0, 9), ' ');
    ck_assert_int_eq (mcview_terminal_buffer_max_row (mcview_vterm_buf (vt)), 0);

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* mcview_vterm_set_size returns TRUE when size changes */

START_TEST (test_set_size_returns_true_on_change)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mctest_assert_true (mcview_vterm_set_size (vt, 44, 187));

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* mcview_vterm_set_size returns FALSE when size is unchanged */

START_TEST (test_set_size_returns_false_on_same_size)
{
    mcview_vterm_t *vt = mcview_vterm_new ();

    mcview_vterm_set_size (vt, 24, 80);
    mctest_assert_false (mcview_vterm_set_size (vt, 24, 80));

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Golden test: cursor moves, erases, scroll region, colored background.
 *
 * Terminal 6x12.  Operations in order:
 *   - write header/footer with CUP
 *   - DECSTBM restricts scroll to rows 1..4
 *   - fill rows 1..4, then LF at scroll_bottom shifts content up
 *   - navigate with CURSOR_UP/DOWN/FWD, overwrite, erase EOL with red bg
 *
 * Expected canvas after all operations:
 *   ============
 *   BBBB
 *   CCCC **[red]
 *   DDDXX  !
 *   (blank, vacated by scroll)
 *   ------------
 */

START_TEST (test_golden_draw_move_erase)
{
    mcview_vterm_t *vt = mcview_vterm_new ();
    char *got;

    mcview_vterm_set_size (vt, 6, 12);
    mcview_vterm_reset (vt);

    FEED (vt, "\033[1;1H");
    FEED (vt, "============");

    FEED (vt, "\033[6;1H");
    FEED (vt, "------------");

    /* scroll region 0-based 1..4 = 1-based 2..5 */
    FEED (vt, "\033[2;5r");

    FEED (vt, "\033[2;1H");
    FEED (vt, "AAAA");
    FEED (vt, "\033[3;1H");
    FEED (vt, "BBBB");
    FEED (vt, "\033[4;1H");
    FEED (vt, "CCCC");
    FEED (vt, "\033[5;1H");
    FEED (vt, "DDDD");

    /* LF at scroll_bottom: region shifts up, row 4 cleared */
    FEED (vt, "\033[5;1H");
    FEED (vt, "\n");

    /* cursor: (4,0) -- up 2 -> row 2, fwd 5 -> col 5 */
    FEED (vt, "\033[2A");
    FEED (vt, "\033[5C");
    FEED (vt, "**");
    /* erase rest of row 2 with red background */
    FEED (vt, "\033[41m");
    FEED (vt, "\033[K");
    FEED (vt, "\033[m");

    /* cursor: (2,7) -- down 1 -> row 3 */
    FEED (vt, "\033[B");
    FEED (vt, "!"); /* col 7 */
    FEED (vt, "\r");
    FEED (vt, "\033[3C"); /* fwd 3 -> col 3 */
    FEED (vt, "XX");      /* overwrite cols 3,4 */

    /* cursor: (3,5) -- up 2 -> row 1, erase EOL */
    FEED (vt, "\033[2A");
    FEED (vt, "\033[K");

    got = canvas_to_text (vt, 6, 12);
    ck_assert_str_eq (got,
                      "============\n"
                      "BBBB        \n"
                      "CCCC **     \n"
                      "DDDXX  !    \n"
                      "            \n"
                      "------------\n");
    g_free (got);

    /* cols 7..11 on row 2 carry red background from the colored erase */
    ck_assert_int_eq (cell_bg (vt, 2, 7), 1);
    ck_assert_int_eq (cell_bg (vt, 2, 11), 1);
    /* cols before the erase have default background */
    ck_assert_int_eq (cell_bg (vt, 2, 6), MCVIEW_ANSI_COLOR_DEFAULT);

    /* header and footer are outside scroll region -- must be untouched */
    ck_assert_int_eq (mcview_vterm_cursor_row (vt), 1);
    ck_assert_uint_eq (cell_ch (vt, 0, 0), '=');
    ck_assert_uint_eq (cell_ch (vt, 5, 0), '-');

    mcview_vterm_free (vt);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_scroll_region_lf_at_bottom_scrolls_up);
    tcase_add_test (tc_core, test_scroll_region_lf_above_region_advances_cursor);
    tcase_add_test (tc_core, test_vpa_moves_row_only);
    tcase_add_test (tc_core, test_ech_erases_with_attrs_cursor_stays);
    tcase_add_test (tc_core, test_erase_eol_fills_full_width_with_attrs);
    tcase_add_test (tc_core, test_decstbm_out_of_range_bottom_is_clamped);
    tcase_add_test (tc_core, test_cursor_fwd_clamps_to_term_cols);
    tcase_add_test (tc_core, test_cursor_abs_col_clamps_to_term_cols);
    tcase_add_test (tc_core, test_cursor_abs_row_clamps_to_term_rows);
    tcase_add_test (tc_core, test_cursor_down_clamps_to_term_rows);
    tcase_add_test (tc_core, test_erase_bol_does_not_expand_past_term_cols);
    tcase_add_test (tc_core, test_set_size_returns_true_on_change);
    tcase_add_test (tc_core, test_set_size_returns_false_on_same_size);
    tcase_add_test (tc_core, test_golden_draw_move_erase);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
