/*
   src/editor-plugins/ctags - tests for fuzzy symbol matching

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

#define TEST_SUITE_NAME "/src/editor-plugins/ctags"

#include "tests/mctest.h"

#include "src/editor-plugins/ctags/ctags-parser.h"
#include "src/editor-plugins/ctags/ctags-fuzzy.h"

/* --------------------------------------------------------------------------------------------- */
/* @Test
 * @Description: subsequence matching is order-preserving and case-insensitive. */
START_TEST (test_fuzzy_match_basic)
{
    /* Each query character must appear in name in the original order. */
    ck_assert (ctags_fuzzy_match ("Width", "wid"));
    ck_assert (ctags_fuzzy_match ("fillWidth", "fw"));
    ck_assert (ctags_fuzzy_match ("WorkingId", "wid"));

    /* Order matters: "dw" cannot match "Width". */
    ck_assert (!ctags_fuzzy_match ("Width", "dw"));

    /* No match. */
    ck_assert (!ctags_fuzzy_match ("Width", "zz"));

    /* Empty query: every name matches. */
    ck_assert (ctags_fuzzy_match ("Width", ""));

    /* NULL or empty inputs survive without crashing. */
    ck_assert (!ctags_fuzzy_match (NULL, "x"));
    ck_assert (!ctags_fuzzy_match ("Width", NULL));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test
 * @Description: exact substring beats split-across-name; word-boundary beats mid-word. */
START_TEST (test_fuzzy_score_ordering)
{
    int s_exact, s_split, s_boundary, s_midword;

    s_exact = ctags_fuzzy_score ("getWidth", "width");
    s_split = ctags_fuzzy_score ("WorkingId", "wid");
    s_boundary = ctags_fuzzy_score ("get_value", "gv");
    s_midword = ctags_fuzzy_score ("ungrateful", "gv");

    /* A contiguous, word-boundary-aligned match should rank no worse than
       a split one. */
    ck_assert_int_gt (s_exact, s_split);
    ck_assert_int_gt (s_boundary, s_midword);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test
 * @Description: search returns entries ordered by score desc; empty input -> NULL or empty. */
START_TEST (test_fuzzy_search_orders_results)
{
    GPtrArray *entries;
    GPtrArray *result;

    entries = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);
    {
        ctags_entry_t *e;

        e = g_new0 (ctags_entry_t, 1);
        e->name = g_strdup ("Width");
        g_ptr_array_add (entries, e);
        e = g_new0 (ctags_entry_t, 1);
        e->name = g_strdup ("WidgetId");
        g_ptr_array_add (entries, e);
        e = g_new0 (ctags_entry_t, 1);
        e->name = g_strdup ("unrelated");
        g_ptr_array_add (entries, e);
    }

    result = ctags_fuzzy_search (entries, "wid");
    mctest_assert_not_null (result);
    ck_assert_int_eq (result->len, 2);

    /* Higher score first. */
    {
        const ctags_entry_t *first = (const ctags_entry_t *) g_ptr_array_index (result, 0);
        const ctags_entry_t *second = (const ctags_entry_t *) g_ptr_array_index (result, 1);

        ck_assert_int_ge (ctags_fuzzy_score (first->name, "wid"),
                          ctags_fuzzy_score (second->name, "wid"));
    }

    g_ptr_array_free (result, FALSE);
    g_ptr_array_free (entries, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    int number_failed;
    Suite *s;
    TCase *tc_core;
    SRunner *sr;

    s = suite_create (TEST_SUITE_NAME);
    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_fuzzy_match_basic);
    tcase_add_test (tc_core, test_fuzzy_score_ordering);
    tcase_add_test (tc_core, test_fuzzy_search_orders_results);
    suite_add_tcase (s, tc_core);

    sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? 0 : 1;
}
