/*
   tests for mc_pp_rename_with_ext() from lib/panel-plugin.c

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

#define TEST_SUITE_NAME "/src/panel-plugins/pp_common"

#include "tests/mctest.h"

#include <string.h>
#include <unistd.h>

#include "lib/panel-plugin.h"

/* --------------------------------------------------------------------------------------------- */

/* Helper: create a temp file and return its path (caller must g_free + unlink). */
static char *
make_temp (void)
{
    GError *error = NULL;
    char *path = NULL;
    int fd;

    fd = g_file_open_tmp ("mc-test-XXXXXX", &path, &error);
    if (fd == -1)
    {
        if (error != NULL)
            g_error_free (error);
        return NULL;
    }
    close (fd);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

/* Plain filename with extension: temp path gets the extension appended. */
START_TEST (test_rename_with_ext_plain)
{
    char *path;
    char *orig;

    path = make_temp ();
    ck_assert_msg (path != NULL, "make_temp failed");
    orig = g_strdup (path);

    mc_pp_rename_with_ext (&path, "Collector.class");

    ck_assert_msg (g_str_has_suffix (path, ".class"), "expected path ending with .class, got: %s",
                   path);
    ck_assert_msg (!g_file_test (orig, G_FILE_TEST_EXISTS),
                   "original temp path should no longer exist: %s", orig);
    ck_assert_msg (g_file_test (path, G_FILE_TEST_EXISTS), "renamed path should exist: %s", path);

    unlink (path);
    g_free (path);
    g_free (orig);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Full entry path (as found inside an archive): only basename extension is taken. */
START_TEST (test_rename_with_ext_full_path)
{
    char *path;

    path = make_temp ();
    ck_assert_msg (path != NULL, "make_temp failed");

    mc_pp_rename_with_ext (&path, "io/prometheus/jmx/Collector.class");

    ck_assert_msg (g_str_has_suffix (path, ".class"), "expected path ending with .class, got: %s",
                   path);

    unlink (path);
    g_free (path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Directory component containing a dot must not be treated as extension. */
START_TEST (test_rename_with_ext_dir_with_dot)
{
    char *path;
    char *orig;

    path = make_temp ();
    ck_assert_msg (path != NULL, "make_temp failed");
    orig = g_strdup (path);

    mc_pp_rename_with_ext (&path, "dir.with.dot/Makefile");

    /* basename "Makefile" has no extension -> path must be unchanged */
    ck_assert_msg (strcmp (path, orig) == 0,
                   "path should be unchanged for basename without extension, got: %s", path);
    ck_assert_msg (g_file_test (path, G_FILE_TEST_EXISTS),
                   "original temp file should still exist: %s", path);

    unlink (path);
    g_free (path);
    g_free (orig);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Filename without any extension: path is left unchanged. */
START_TEST (test_rename_with_ext_no_ext)
{
    char *path;
    char *orig;

    path = make_temp ();
    ck_assert_msg (path != NULL, "make_temp failed");
    orig = g_strdup (path);

    mc_pp_rename_with_ext (&path, "Makefile");

    ck_assert_msg (strcmp (path, orig) == 0,
                   "path should be unchanged for file without extension, got: %s", path);

    unlink (path);
    g_free (path);
    g_free (orig);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* NULL fname: no crash, path unchanged. */
START_TEST (test_rename_with_ext_null_fname)
{
    char *path;
    char *orig;

    path = make_temp ();
    ck_assert_msg (path != NULL, "make_temp failed");
    orig = g_strdup (path);

    mc_pp_rename_with_ext (&path, NULL);

    ck_assert_msg (strcmp (path, orig) == 0, "path should be unchanged when fname is NULL");

    unlink (path);
    g_free (path);
    g_free (orig);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* NULL local_path pointer: no crash. */
START_TEST (test_rename_with_ext_null_local_path)
{
    mc_pp_rename_with_ext (NULL, "file.txt");
    /* if we reach here without crashing the test passes */
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* NULL *local_path (pointer to NULL): no crash. */
START_TEST (test_rename_with_ext_null_deref)
{
    char *path = NULL;

    mc_pp_rename_with_ext (&path, "file.txt");
    /* if we reach here without crashing the test passes */
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_rename_with_ext_plain);
    tcase_add_test (tc_core, test_rename_with_ext_full_path);
    tcase_add_test (tc_core, test_rename_with_ext_dir_with_dot);
    tcase_add_test (tc_core, test_rename_with_ext_no_ext);
    tcase_add_test (tc_core, test_rename_with_ext_null_fname);
    tcase_add_test (tc_core, test_rename_with_ext_null_local_path);
    tcase_add_test (tc_core, test_rename_with_ext_null_deref);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
