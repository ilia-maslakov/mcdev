/*
   src/viewer - unit tests for filter activate/deactivate logic

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

#define TEST_SUITE_NAME "/src/viewer/filter"

#include "tests/mctest.h"

#include "lib/global.h"
#include "lib/search.h"
#include "lib/strutil.h"

#include "src/viewer/internal.h"

/*** mocks ***********************************************************************************/

void
mcview_compute_areas (WView *view)
{
    (void) view;
}

void
mcview_update_bytes_per_line (WView *view)
{
    (void) view;
}

void
mcview_display (WView *view)
{
    (void) view;
}

void
mcview_set_codeset (WView *view)
{
    (void) view;
}

void
mcview_show_error (WView *view, const char *format, const char *filename)
{
    (void) view;
    (void) format;
    (void) filename;
}

void
load_file_position (const vfs_path_t *filename_vpath, long *line, long *column, off_t *offset,
                    GArray **bookmarks)
{
    (void) filename_vpath;
    (void) line;
    (void) column;
    (void) offset;
    (void) bookmarks;
}

void
message (int flags, const char *title, const char *text, ...)
{
    (void) flags;
    (void) title;
    (void) text;
}

/*** fixtures ********************************************************************************/

static WView test_view;

/* @Before */
static void
setup (void)
{
    str_init_strings (NULL);
    memset (&test_view, 0, sizeof (test_view));
    /* Reset filter options to known defaults for each test. */
    mcview_filter_options.type = MC_SEARCH_T_NORMAL;
    mcview_filter_options.case_sens = FALSE;
    mcview_filter_options.whole_words = FALSE;
    mcview_filter_options.all_codepages = FALSE;
}

/* @After */
static void
teardown (void)
{
    if (test_view.filter_active)
        mcview_filter_deactivate (&test_view);
    g_free (test_view.filter_pattern);
    test_view.filter_pattern = NULL;
    str_uninit_strings ();
}

/*** helpers *********************************************************************************/

static mcview_filter_options_t
make_opts (mc_search_type_t type, gboolean case_sens, gboolean whole_words, gboolean all_codepages)
{
    mcview_filter_options_t o;
    o.type = type;
    o.case_sens = case_sens;
    o.whole_words = whole_words;
    o.all_codepages = all_codepages;
    return o;
}

/*** tests ***********************************************************************************/

/* @Test: valid normal pattern activates filter */
START_TEST (test_activate_valid_normal)
{
    mcview_filter_options_t opts = make_opts (MC_SEARCH_T_NORMAL, FALSE, FALSE, FALSE);
    gchar *err = NULL;

    ck_assert (mcview_filter_activate (&test_view, "hello", &opts, &err));
    ck_assert (err == NULL);
    ck_assert (test_view.filter_active);
    ck_assert_str_eq (test_view.filter_pattern, "hello");
    ck_assert (test_view.filter_engine != NULL);
}
END_TEST

/* @Test: valid regex pattern activates filter with correct search type */
START_TEST (test_activate_valid_regex)
{
    mcview_filter_options_t opts = make_opts (MC_SEARCH_T_REGEX, FALSE, FALSE, FALSE);
    gchar *err = NULL;

    ck_assert (mcview_filter_activate (&test_view, "err.*or", &opts, &err));
    ck_assert (err == NULL);
    ck_assert (test_view.filter_active);
    ck_assert_int_eq ((int) test_view.filter_engine->search_type, (int) MC_SEARCH_T_REGEX);
}
END_TEST

/* @Test: invalid regex returns FALSE, sets err_msg, leaves view unchanged */
START_TEST (test_activate_invalid_regex_preserves_state)
{
    mcview_filter_options_t opts = make_opts (MC_SEARCH_T_REGEX, FALSE, FALSE, FALSE);
    gchar *err = NULL;

    /* Activate a valid filter first. */
    ck_assert (mcview_filter_activate (&test_view, "good", &opts, NULL));
    ck_assert (test_view.filter_active);

    /* Attempt to replace with an invalid regex. */
    ck_assert (!mcview_filter_activate (&test_view, "[unclosed", &opts, &err));
    ck_assert (err != NULL);
    g_free (err);

    /* Old filter must still be running. */
    ck_assert (test_view.filter_active);
    ck_assert_str_eq (test_view.filter_pattern, "good");
}
END_TEST

/* @Test: invalid regex with NULL err_msg does not crash */
START_TEST (test_activate_invalid_regex_null_errmsg)
{
    mcview_filter_options_t opts = make_opts (MC_SEARCH_T_REGEX, FALSE, FALSE, FALSE);

    ck_assert (!mcview_filter_activate (&test_view, "[unclosed", &opts, NULL));
    ck_assert (!test_view.filter_active);
}
END_TEST

/* @Test: case_sens option is propagated to the engine */
START_TEST (test_activate_case_sens_propagated)
{
    mcview_filter_options_t opts;
    gchar *err = NULL;

    opts = make_opts (MC_SEARCH_T_NORMAL, TRUE, FALSE, FALSE);
    ck_assert (mcview_filter_activate (&test_view, "Foo", &opts, &err));
    ck_assert (err == NULL);
    ck_assert (test_view.filter_engine->is_case_sensitive);

    mcview_filter_deactivate (&test_view);

    opts = make_opts (MC_SEARCH_T_NORMAL, FALSE, FALSE, FALSE);
    ck_assert (mcview_filter_activate (&test_view, "Foo", &opts, &err));
    ck_assert (err == NULL);
    ck_assert (!test_view.filter_engine->is_case_sensitive);
}
END_TEST

/* @Test: whole_words option is propagated to the engine */
START_TEST (test_activate_whole_words_propagated)
{
    mcview_filter_options_t opts = make_opts (MC_SEARCH_T_NORMAL, FALSE, TRUE, FALSE);
    gchar *err = NULL;

    ck_assert (mcview_filter_activate (&test_view, "word", &opts, &err));
    ck_assert (err == NULL);
    ck_assert (test_view.filter_engine->whole_words);
}
END_TEST

/* @Test: deactivate clears all filter state */
START_TEST (test_deactivate_clears_state)
{
    mcview_filter_options_t opts = make_opts (MC_SEARCH_T_NORMAL, FALSE, FALSE, FALSE);

    ck_assert (mcview_filter_activate (&test_view, "hello", &opts, NULL));
    ck_assert (test_view.filter_active);

    mcview_filter_deactivate (&test_view);

    ck_assert (!test_view.filter_active);
    ck_assert (test_view.filter_engine == NULL);
    ck_assert (test_view.filter_pattern == NULL);
    ck_assert (test_view.filter_offsets == NULL);
}
END_TEST

/* @Test: second activate replaces first after deactivating old state */
START_TEST (test_activate_replaces_previous)
{
    mcview_filter_options_t opts = make_opts (MC_SEARCH_T_NORMAL, FALSE, FALSE, FALSE);

    ck_assert (mcview_filter_activate (&test_view, "first", &opts, NULL));
    ck_assert_str_eq (test_view.filter_pattern, "first");

    ck_assert (mcview_filter_activate (&test_view, "second", &opts, NULL));
    ck_assert_str_eq (test_view.filter_pattern, "second");
    ck_assert (test_view.filter_active);
}
END_TEST

/*** main ************************************************************************************/

int
main (void)
{
    TCase *tc;

    tc = tcase_create ("filter");
    tcase_set_timeout (tc, 10);

    tcase_add_checked_fixture (tc, setup, teardown);

    tcase_add_test (tc, test_activate_valid_normal);
    tcase_add_test (tc, test_activate_valid_regex);
    tcase_add_test (tc, test_activate_invalid_regex_preserves_state);
    tcase_add_test (tc, test_activate_invalid_regex_null_errmsg);
    tcase_add_test (tc, test_activate_case_sens_propagated);
    tcase_add_test (tc, test_activate_whole_words_propagated);
    tcase_add_test (tc, test_deactivate_clears_state);
    tcase_add_test (tc, test_activate_replaces_previous);

    return mctest_run_all (tc);
}

/* --------------------------------------------------------------------------------------------- */
