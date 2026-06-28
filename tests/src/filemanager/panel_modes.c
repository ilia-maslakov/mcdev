/*
   src/filemanager - panel view modes

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

#define TEST_SUITE_NAME "/src/filemanager"

#include "tests/mctest.h"

#include "src/filemanager/panel_modes.h"

/* --------------------------------------------------------------------------------------------- */

static panel_mode_t
make_mode (const char *types, const char *widths, const char *status_types,
           const char *status_widths)
{
    panel_mode_t m;

    m.id = 1;
    m.name = (char *) "test";
    m.types = (char *) types;
    m.widths = (char *) widths;
    m.status_types = (char *) status_types;
    m.status_widths = (char *) status_widths;
    return m;
}

/* --------------------------------------------------------------------------------------------- */

/* panel_mode_to_format() builds the mc listing-format string from a mode. */
START_TEST (test_to_format_single_column)
{
    panel_mode_t m = make_mode ("name", "0", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half name");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_multiple_columns)
{
    panel_mode_t m = make_mode ("type name,size,mtime", "0,0,0", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half type name | size | mtime");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_width_only_when_nonzero)
{
    panel_mode_t m = make_mode ("name,size", "0,8", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half name | size:8");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_empty_falls_back_to_name)
{
    panel_mode_t m = make_mode ("", "", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half name");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_status_line)
{
    panel_mode_t m = make_mode ("name", "0", "name,size", "0,6");
    char *s;

    s = panel_mode_to_format (&m, TRUE);
    ck_assert_str_eq (s, "half name | size:6");
    g_free (s);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* panel_mode_validate() accepts known field ids and non-negative widths. */
START_TEST (test_validate_accepts_known_fields)
{
    char *err = NULL;

    ck_assert (panel_mode_validate ("name,size", "0,0", &err));
    ck_assert (err == NULL);
}
END_TEST

START_TEST (test_validate_accepts_multifield_column)
{
    char *err = NULL;

    ck_assert (panel_mode_validate ("type name", "0", &err));
    ck_assert (err == NULL);
}
END_TEST

START_TEST (test_validate_rejects_unknown_field)
{
    char *err = NULL;

    ck_assert (!panel_mode_validate ("nosuchfield", "0", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

START_TEST (test_validate_rejects_non_numeric_width)
{
    char *err = NULL;

    ck_assert (!panel_mode_validate ("name", "abc", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

START_TEST (test_validate_rejects_negative_width)
{
    char *err = NULL;

    ck_assert (!panel_mode_validate ("name", "-1", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

START_TEST (test_validate_rejects_empty_types)
{
    char *err = NULL;

    ck_assert (!panel_mode_validate ("", "", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    // Add new tests here: ***************
    tcase_add_test (tc_core, test_to_format_single_column);
    tcase_add_test (tc_core, test_to_format_multiple_columns);
    tcase_add_test (tc_core, test_to_format_width_only_when_nonzero);
    tcase_add_test (tc_core, test_to_format_empty_falls_back_to_name);
    tcase_add_test (tc_core, test_to_format_status_line);
    tcase_add_test (tc_core, test_validate_accepts_known_fields);
    tcase_add_test (tc_core, test_validate_accepts_multifield_column);
    tcase_add_test (tc_core, test_validate_rejects_unknown_field);
    tcase_add_test (tc_core, test_validate_rejects_non_numeric_width);
    tcase_add_test (tc_core, test_validate_rejects_negative_width);
    tcase_add_test (tc_core, test_validate_rejects_empty_types);
    // ***********************************

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
