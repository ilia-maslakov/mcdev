/*
   src/filemanager - tests for panel_plugin_close() format restoration

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

#include <string.h>

#include "lib/panel-plugin.h"
#include "src/filemanager/panel.h"

/* --------------------------------------------------------------------------------------------- */

static gboolean set_panel_formats_called;
static gboolean panel_reload_called;
static gboolean panel_do_cd_called;
static gboolean panel_history_add_called;
static char *last_cd_path;
static char *last_history_path;
static const mc_panel_plugin_t *find_by_prefix_result;
static char *last_find_prefix;
static int call_order_formats;
static int call_order_reload;
static int call_counter;

/* @Mock */
static int
mock_set_panel_formats (WPanel *p)
{
    (void) p;
    set_panel_formats_called = TRUE;
    call_order_formats = ++call_counter;
    return 0;
}

/* @Mock */
static void
mock_panel_reload (WPanel *panel)
{
    (void) panel;
    panel_reload_called = TRUE;
    call_order_reload = ++call_counter;
}

/* @Mock */
static gboolean
mock_panel_do_cd (WPanel *panel, const vfs_path_t *new_dir_vpath, enum cd_enum cd_type)
{
    (void) panel;
    (void) cd_type;

    panel_do_cd_called = TRUE;
    g_free (last_cd_path);
    last_cd_path = g_strdup (vfs_path_as_str (new_dir_vpath));

    return TRUE;
}

/* @Mock */
static void
mock_panel_directory_history_add_path (WPanel *panel, const char *path)
{
    (void) panel;

    panel_history_add_called = TRUE;
    g_free (last_history_path);
    last_history_path = g_strdup (path);
}

/* @Mock */
static const mc_panel_plugin_t *
mock_mc_panel_plugin_find_by_prefix (const char *prefix)
{
    g_free (last_find_prefix);
    last_find_prefix = g_strdup (prefix);
    return find_by_prefix_result;
}

/* Redirect calls inside panel_plugin_ui.c to local mocks via preprocessor.
   This is a compile-time seam: panel_plugin_close() calls
   mock_set_panel_formats / mock_panel_reload instead of the real ones. */
#define set_panel_formats                mock_set_panel_formats
#define panel_reload                     mock_panel_reload
#define panel_do_cd                      mock_panel_do_cd
#define panel_directory_history_add_path mock_panel_directory_history_add_path
#define mc_panel_plugin_find_by_prefix   mock_mc_panel_plugin_find_by_prefix

#include "src/filemanager/panel_plugin_ui.c"

#undef set_panel_formats
#undef panel_reload
#undef panel_do_cd
#undef panel_directory_history_add_path
#undef mc_panel_plugin_find_by_prefix

/* --------------------------------------------------------------------------------------------- */

static gboolean mock_plugin_close_called;
static const char *mock_plugin_title;

static void
mock_plugin_close (void *data)
{
    (void) data;
    mock_plugin_close_called = TRUE;
}

static const char *
mock_plugin_get_title (void *data)
{
    (void) data;
    return mock_plugin_title;
}

static const mc_panel_plugin_t mock_plugin = {
    .name = "test",
    .close = mock_plugin_close,
    .prefix = "mock:",
    .get_title = mock_plugin_get_title,
};

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    set_panel_formats_called = FALSE;
    panel_reload_called = FALSE;
    panel_do_cd_called = FALSE;
    panel_history_add_called = FALSE;
    mock_plugin_close_called = FALSE;
    mock_plugin_title = NULL;
    g_clear_pointer (&last_find_prefix, g_free);
    find_by_prefix_result = NULL;
    call_counter = 0;
    call_order_formats = 0;
    call_order_reload = 0;
    g_clear_pointer (&last_cd_path, g_free);
    g_clear_pointer (&last_history_path, g_free);
    g_clear_pointer (&last_find_prefix, g_free);
}

/* @After */
static void
teardown (void)
{
    g_clear_pointer (&last_cd_path, g_free);
    g_clear_pointer (&last_history_path, g_free);
}

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_plugin_close_restores_formats_before_reload)
{
    // given
    WPanel panel;
    mc_panel_host_t *host;
    int dummy_data = 42;

    memset (&panel, 0, sizeof (panel));
    panel.is_plugin_panel = TRUE;
    panel.plugin = &mock_plugin;
    panel.plugin_data = &dummy_data;

    host = g_new0 (mc_panel_host_t, 1);
    host->focus_after = NULL;
    panel.plugin_host = host;

    // when
    panel_plugin_close (&panel);

    // then
    ck_assert_msg (set_panel_formats_called, "set_panel_formats was not called");
    ck_assert_msg (panel_reload_called, "panel_reload was not called");
    ck_assert_msg (mock_plugin_close_called, "plugin close callback was not called");
    ck_assert_int_lt (call_order_formats, call_order_reload);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_plugin_close_clears_state)
{
    // given
    WPanel panel;
    mc_panel_host_t *host;
    int dummy_data = 42;

    memset (&panel, 0, sizeof (panel));
    panel.is_plugin_panel = TRUE;
    panel.is_panelized = TRUE;
    panel.plugin = &mock_plugin;
    panel.plugin_data = &dummy_data;

    host = g_new0 (mc_panel_host_t, 1);
    host->focus_after = NULL;
    panel.plugin_host = host;

    // when
    panel_plugin_close (&panel);

    // then
    ck_assert_msg (!panel.is_plugin_panel, "is_plugin_panel was not cleared");
    ck_assert_msg (!panel.is_panelized, "is_panelized was not cleared");
    mctest_assert_null (panel.plugin);
    mctest_assert_null (panel.plugin_data);
    mctest_assert_null (panel.plugin_host);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_plugin_close_null_panel)
{
    // when
    panel_plugin_close (NULL);

    // then
    ck_assert_msg (!set_panel_formats_called, "set_panel_formats was called");
    ck_assert_msg (!panel_reload_called, "panel_reload was called");
    ck_assert_msg (!mock_plugin_close_called, "plugin close callback was called");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_plugin_close_not_plugin_panel)
{
    // given
    WPanel panel;

    memset (&panel, 0, sizeof (panel));
    panel.is_plugin_panel = FALSE;

    // when
    panel_plugin_close (&panel);

    // then
    ck_assert_msg (!set_panel_formats_called, "set_panel_formats was called");
    ck_assert_msg (!panel_reload_called, "panel_reload was called");
    ck_assert_msg (!mock_plugin_close_called, "plugin close callback was called");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_plugin_close_clears_plugin_host_with_focus_after)
{
    // given
    WPanel panel;
    mc_panel_host_t *host;
    int dummy_data = 42;

    memset (&panel, 0, sizeof (panel));
    panel.is_plugin_panel = TRUE;
    panel.plugin = &mock_plugin;
    panel.plugin_data = &dummy_data;

    host = g_new0 (mc_panel_host_t, 1);
    host->focus_after = g_strdup ("some_file.txt");
    panel.plugin_host = host;

    // when
    panel_plugin_close (&panel);

    // then
    mctest_assert_null (panel.plugin_host);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_plugin_find_by_path_uses_prefix_registry)
{
    const mc_panel_plugin_t *plugin;

    find_by_prefix_result = &mock_plugin;

    plugin = panel_plugin_find_by_path ("mock:/containers/demo");

    ck_assert_ptr_eq ((const void *) plugin, (const void *) &mock_plugin);
    ck_assert_str_eq (last_find_prefix, "mock:");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_plugin_find_by_path_ignores_unknown_prefix)
{
    const mc_panel_plugin_t *plugin;

    plugin = panel_plugin_find_by_path ("unknown:/path");

    ck_assert_ptr_null (plugin);
    ck_assert_str_eq (last_find_prefix, "unknown:");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, setup, teardown);

    tcase_add_test (tc_core, test_plugin_close_restores_formats_before_reload);
    tcase_add_test (tc_core, test_plugin_close_clears_state);
    tcase_add_test (tc_core, test_plugin_close_null_panel);
    tcase_add_test (tc_core, test_plugin_close_not_plugin_panel);
    tcase_add_test (tc_core, test_plugin_close_clears_plugin_host_with_focus_after);
    tcase_add_test (tc_core, test_plugin_find_by_path_uses_prefix_registry);
    tcase_add_test (tc_core, test_plugin_find_by_path_ignores_unknown_prefix);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
