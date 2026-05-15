/*
   src/editor-plugins/ctags - tests for the in-memory tag index

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
#include "src/editor-plugins/ctags/ctags-index.h"

/* --------------------------------------------------------------------------------------------- */

static ctags_entry_t *
mk_entry (const char *name, const char *file, char kind, const char *scope)
{
    ctags_entry_t *e = g_new0 (ctags_entry_t, 1);
    e->name = g_strdup (name);
    e->file = g_strdup (file);
    e->kind = kind;
    e->scope = scope != NULL ? g_strdup (scope) : NULL;
    return e;
}

static GPtrArray *
mk_entries (void)
{
    GPtrArray *entries = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);

    g_ptr_array_add (entries, mk_entry ("foo", "src/a.c", 'f', NULL));
    g_ptr_array_add (entries, mk_entry ("foobar", "src/b.c", 'f', NULL));
    g_ptr_array_add (entries, mk_entry ("Foo", "src/a.h", 'c', NULL));
    g_ptr_array_add (entries, mk_entry ("method", "src/a.cpp", 'f', "Foo"));
    g_ptr_array_add (entries, mk_entry ("other", "lib/a.c", 'f', NULL));
    return entries;
}

/* --------------------------------------------------------------------------------------------- */
/* @Test */
START_TEST (test_find_exact_case_sensitive)
{
    GPtrArray *entries;
    ctags_index_t *idx;
    const GPtrArray *r;

    entries = mk_entries ();
    idx = ctags_index_build (entries, "/proj");

    r = ctags_index_find_exact (idx, "foo");
    mctest_assert_not_null (r);
    ck_assert_int_eq (r->len, 1);

    r = ctags_index_find_exact (idx, "Foo");
    mctest_assert_not_null (r);
    ck_assert_int_eq (r->len, 1);

    r = ctags_index_find_exact (idx, "FOO");
    ck_assert_ptr_eq ((gpointer) r, NULL);

    ctags_index_free (idx);
    g_ptr_array_free (entries, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test */
START_TEST (test_find_exact_case_insensitive)
{
    GPtrArray *entries;
    ctags_index_t *idx;
    const GPtrArray *r;

    entries = mk_entries ();
    idx = ctags_index_build (entries, "/proj");

    /* case-fold lookup must collapse Foo and foo -- both should be present. */
    r = ctags_index_find_exact_ci (idx, "foo");
    mctest_assert_not_null (r);
    ck_assert_int_eq (r->len, 2);

    ctags_index_free (idx);
    g_ptr_array_free (entries, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test */
START_TEST (test_find_prefix)
{
    GPtrArray *entries;
    ctags_index_t *idx;
    GPtrArray *r;

    entries = mk_entries ();
    idx = ctags_index_build (entries, "/proj");

    /* "foo" prefix matches "foo" and "foobar" (case-sensitive). */
    r = ctags_index_find_prefix (idx, "foo", FALSE);
    mctest_assert_not_null (r);
    ck_assert_int_eq (r->len, 2);
    g_ptr_array_free (r, FALSE);

    /* No match. */
    r = ctags_index_find_prefix (idx, "zzz", FALSE);
    ck_assert_ptr_eq ((gpointer) r, NULL);

    ctags_index_free (idx);
    g_ptr_array_free (entries, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test
 * @Description: find_basename matches by basename prefix AND by path tail anchored at
 *               a directory separator -- not by any in-name suffix. */
START_TEST (test_find_basename_anchored)
{
    GPtrArray *entries = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);
    ctags_index_t *idx;
    GPtrArray *r;

    /* Build an index where the path-suffix branch would over-match on bare "h"
       if we did not require a directory separator before the suffix. */
    g_ptr_array_add (entries, mk_entry ("sym1", "src/edit.h", 'f', NULL));
    g_ptr_array_add (entries, mk_entry ("sym2", "src/edit.c", 'f', NULL));
    g_ptr_array_add (entries, mk_entry ("sym3", "src/io_match.h", 'f', NULL));
    g_ptr_array_add (entries, mk_entry ("sym4", "lib/widget.h", 'f', NULL));
    idx = ctags_index_build (entries, "/proj");

    /* "edit.h" anchors at "/edit.h" -- matches src/edit.h only. */
    r = ctags_index_find_basename (idx, "edit.h");
    mctest_assert_not_null (r);
    ck_assert_int_eq (r->len, 1);
    g_ptr_array_free (r, FALSE);

    /* Bare "h" is a one-char tail that does NOT begin at a separator in any of
       the indexed paths.  Must NOT return everything.  May match basename
       prefix; in our data no basename starts with "h". */
    r = ctags_index_find_basename (idx, "h");
    ck_assert_ptr_eq ((gpointer) r, NULL);

    /* "match.h" sits inside "io_match.h" -- but the byte right before the
       tail is 'o' (not a separator) -- must NOT match. */
    r = ctags_index_find_basename (idx, "match.h");
    ck_assert_ptr_eq ((gpointer) r, NULL);

    ctags_index_free (idx);
    g_ptr_array_free (entries, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test */
START_TEST (test_find_scope)
{
    GPtrArray *entries;
    ctags_index_t *idx;
    const GPtrArray *r;

    entries = mk_entries ();
    idx = ctags_index_build (entries, "/proj");

    r = ctags_index_find_scope (idx, "Foo");
    mctest_assert_not_null (r);
    ck_assert_int_eq (r->len, 1);
    mctest_assert_str_eq (((ctags_entry_t *) g_ptr_array_index (r, 0))->name, "method");

    ctags_index_free (idx);
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

    tcase_add_test (tc_core, test_find_exact_case_sensitive);
    tcase_add_test (tc_core, test_find_exact_case_insensitive);
    tcase_add_test (tc_core, test_find_prefix);
    tcase_add_test (tc_core, test_find_basename_anchored);
    tcase_add_test (tc_core, test_find_scope);
    suite_add_tcase (s, tc_core);

    sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? 0 : 1;
}
