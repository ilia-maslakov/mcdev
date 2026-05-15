/*
   src/editor-plugins/ctags - tests for the ctags file parser

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

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "src/editor-plugins/ctags/ctags-parser.h"

/* --------------------------------------------------------------------------------------------- */

static char *tmp_path = NULL;

static void
write_tags (const char *body)
{
    FILE *f;

    int fd;

    tmp_path = g_strdup ("/tmp/mc-ctags-parser-tests.XXXXXX");
    fd = mkstemp (tmp_path);
    ck_assert (fd != -1);
    f = fdopen (fd, "w");
    mctest_assert_not_null (f);
    fputs (body, f);
    fclose (f);
}

static void
my_teardown (void)
{
    if (tmp_path != NULL)
    {
        unlink (tmp_path);
        g_free (tmp_path);
        tmp_path = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

static const ctags_entry_t *
find_by_name (const GPtrArray *out, const char *name)
{
    guint i;

    for (i = 0; i < out->len; i++)
    {
        const ctags_entry_t *e = (const ctags_entry_t *) g_ptr_array_index (out, i);
        if (g_strcmp0 (e->name, name) == 0)
            return e;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
/* @Test
 * @Description: parser reads name, file, excmd pattern, kind and line-from-line field. */
START_TEST (test_parse_basic_fields)
{
    GPtrArray *out;
    const ctags_entry_t *e;
    gsize n;

    /* Two real-world-style entries:
       - `foo` from a header, with /^pattern$/;" excmd, kind f and an explicit line:
       - `Bar` from a class header, line via /^pattern/ format. */
    write_tags ("!_TAG_FILE_FORMAT\t2\t/extended format/\n"
                "!_TAG_FILE_SORTED\t1\t/0=unsorted, 1=sorted, 2=foldcase/\n"
                "foo\tsrc/a.c\t/^int foo(int x)$/;\"\tf\tline:42\n"
                "Bar\tsrc/a.h\t/^class Bar$/;\"\tc\n");

    out = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);
    n = ctags_parse_file (tmp_path, out);

    ck_assert_int_eq (n, 2);
    ck_assert_int_eq (out->len, 2);

    e = find_by_name (out, "foo");
    mctest_assert_not_null (e);
    mctest_assert_str_eq (e->file, "src/a.c");
    ck_assert_int_eq (e->kind, 'f');
    ck_assert_int_eq (e->line, 42);

    e = find_by_name (out, "Bar");
    mctest_assert_not_null (e);
    mctest_assert_str_eq (e->file, "src/a.h");
    ck_assert_int_eq (e->kind, 'c');

    g_ptr_array_free (out, TRUE);
    my_teardown ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test
 * @Description: comment / metadata lines starting with '!' are not entries. */
START_TEST (test_parse_skips_metadata)
{
    GPtrArray *out;
    gsize n;

    write_tags ("!_TAG_PROGRAM_NAME\tUniversal Ctags\t//\n"
                "!_TAG_FILE_ENCODING\tUTF-8\t//\n"
                "real_one\tx.c\t/^int real_one(void)$/;\"\tf\n");

    out = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);
    n = ctags_parse_file (tmp_path, out);

    ck_assert_int_eq (n, 1);
    mctest_assert_str_eq (((ctags_entry_t *) g_ptr_array_index (out, 0))->name, "real_one");

    g_ptr_array_free (out, TRUE);
    my_teardown ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test
 * @Description: scope field captures both kind name and the scope value. */
START_TEST (test_parse_scope_fields)
{
    GPtrArray *out;
    const ctags_entry_t *e;

    write_tags ("method\tx.cpp\t/^void Foo::method()$/;\"\tf\tclass:Foo\n");

    out = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);
    ctags_parse_file (tmp_path, out);

    e = find_by_name (out, "method");
    mctest_assert_not_null (e);
    mctest_assert_str_eq (e->scope, "Foo");
    mctest_assert_str_eq (e->scope_kind, "class");

    g_ptr_array_free (out, TRUE);
    my_teardown ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test
 * @Description: parser tolerates an empty file and returns zero entries. */
START_TEST (test_parse_empty_file)
{
    GPtrArray *out;

    write_tags ("");

    out = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);
    ck_assert_int_eq (ctags_parse_file (tmp_path, out), 0);
    ck_assert_int_eq (out->len, 0);

    g_ptr_array_free (out, TRUE);
    my_teardown ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* @Test
 * @Description: missing file path returns zero entries (no crash). */
START_TEST (test_parse_missing_file)
{
    GPtrArray *out;

    out = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);
    ck_assert_int_eq (ctags_parse_file ("/no/such/path/tags", out), 0);
    ck_assert_int_eq (out->len, 0);

    g_ptr_array_free (out, TRUE);
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

    tcase_add_test (tc_core, test_parse_basic_fields);
    tcase_add_test (tc_core, test_parse_skips_metadata);
    tcase_add_test (tc_core, test_parse_scope_fields);
    tcase_add_test (tc_core, test_parse_empty_file);
    tcase_add_test (tc_core, test_parse_missing_file);
    suite_add_tcase (s, tc_core);

    sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? 0 : 1;
}
