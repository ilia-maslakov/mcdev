/*
   src/filemanager - placement of the filename scroll (truncation) markers

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

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

#define TEST_SUITE_NAME "/src/filemanager"

#include "tests/mctest.h"

#include "src/filemanager/panel.h"

/* --------------------------------------------------------------------------------------------- */

/* The bug: the scroll markers were placed from list_format, so a name-last
   user format ("Long") drew the left marker at column 0, over the owner field.
   panel_scroll_marker_pos() must anchor them to the name column instead. */

/* name not first, blank cell before it (e.g. "... space name") -> the left
   marker moves onto that blank cell. */
START_TEST (test_marker_anchored_to_name)
{
    int offset = 0;
    int width = 40;

    // name at col 19, len 11
    panel_scroll_marker_pos (1, 19, TRUE, 12, &offset, &width);
    ck_assert_int_eq (offset, 19);
    ck_assert_int_eq (width, 11);
}
END_TEST

/* name preceded by data (e.g. the type char in "type name") -> the left
   marker stays on the frame, the right one still hugs the name end. */
START_TEST (test_marker_on_frame_when_data_before_name)
{
    int offset = 0;
    int width = 40;

    // name at col 1, len 11: right marker at offset + width + 1 = 13
    panel_scroll_marker_pos (1, 1, FALSE, 12, &offset, &width);
    ck_assert_int_eq (offset, 0);
    ck_assert_int_eq (width, 12);
}
END_TEST

/* name first (name_col == 0) -> markers bracket the name from the frame. */
START_TEST (test_marker_at_start_when_name_first)
{
    int offset = 0;
    int width = 40;

    // name len 11: right marker at offset + width + 1 = 12, one past the name
    panel_scroll_marker_pos (1, 0, FALSE, 12, &offset, &width);
    ck_assert_int_eq (offset, 0);
    ck_assert_int_eq (width, 11);
}
END_TEST

/* multi-column listing -> no adjustment. */
START_TEST (test_marker_multicolumn_unchanged)
{
    int offset = 5;
    int width = 20;

    panel_scroll_marker_pos (2, 10, TRUE, 8, &offset, &width);
    ck_assert_int_eq (offset, 5);
    ck_assert_int_eq (width, 20);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    // Add new tests here: ***************
    tcase_add_test (tc_core, test_marker_anchored_to_name);
    tcase_add_test (tc_core, test_marker_on_frame_when_data_before_name);
    tcase_add_test (tc_core, test_marker_at_start_when_name_first);
    tcase_add_test (tc_core, test_marker_multicolumn_unchanged);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
