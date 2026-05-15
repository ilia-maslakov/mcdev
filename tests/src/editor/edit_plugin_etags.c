/*
   src/editor - tests for etags editor plugin action dispatch

   Copyright (C) 2026
   Free Software Foundation, Inc.
*/

#define TEST_SUITE_NAME "/src/editor"

#include "tests/mctest.h"

#include "lib/editor-plugin.h"
#include "lib/keybind.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor-plugins/etags/etags.h"

/* --------------------------------------------------------------------------------------------- */

/* @CapturedValue */
static gboolean edit_get_match_keyword_cmd_called;
/* @CapturedValue */
static WEdit *edit_get_match_keyword_cmd__edit;
/* @CapturedValue */
static int refresh_called;

/* --------------------------------------------------------------------------------------------- */
/* @Mock */
void
edit_get_match_keyword_cmd (WEdit *edit)
{
    edit_get_match_keyword_cmd_called = TRUE;
    edit_get_match_keyword_cmd__edit = edit;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_host_refresh (mc_editor_host_t *host)
{
    (void) host;
    refresh_called++;
}

/* --------------------------------------------------------------------------------------------- */
/* @Before */
static void
setup (void)
{
    edit_get_match_keyword_cmd_called = FALSE;
    edit_get_match_keyword_cmd__edit = NULL;
    refresh_called = 0;
}

/* --------------------------------------------------------------------------------------------- */
/* @Test */
START_TEST (test_etags_plugin_handle_action)
{
    const mc_editor_plugin_t *plugin;
    mc_editor_host_t host;
    void *plugin_data;
    WEdit *fake_edit;
    mc_ep_result_t ret;

    edit_register_builtin_plugins ();

    plugin = mc_editor_plugin_find_by_name ("etags");
    mctest_assert_not_null (plugin);
    mctest_assert_not_null (plugin->open);
    mctest_assert_not_null (plugin->close);
    mctest_assert_not_null (plugin->handle_action);

    host.redraw = test_host_refresh;
    host.message = NULL;
    host.host_data = NULL;

    plugin_data = plugin->open (&host, NULL);
    mctest_assert_not_null (plugin_data);

    fake_edit = (WEdit *) &host;
    ret = plugin->handle_action (plugin_data, CK_Find, fake_edit);
    ck_assert_int_eq (ret, MC_EPR_OK);
    mctest_assert_true (edit_get_match_keyword_cmd_called);
    mctest_assert_ptr_eq (edit_get_match_keyword_cmd__edit, fake_edit);
    ck_assert_int_eq (refresh_called, 1);

    edit_get_match_keyword_cmd_called = FALSE;
    edit_get_match_keyword_cmd__edit = NULL;
    ret = plugin->handle_action (plugin_data, CK_Save, fake_edit);
    ck_assert_int_eq (ret, MC_EPR_NOT_SUPPORTED);
    mctest_assert_false (edit_get_match_keyword_cmd_called);
    mctest_assert_null (edit_get_match_keyword_cmd__edit);
    ck_assert_int_eq (refresh_called, 1);

    ret = plugin->handle_action (plugin_data, CK_Find, NULL);
    ck_assert_int_eq (ret, MC_EPR_NOT_SUPPORTED);

    plugin->close (plugin_data);
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

    tcase_add_checked_fixture (tc_core, setup, NULL);
    tcase_add_test (tc_core, test_etags_plugin_handle_action);
    suite_add_tcase (s, tc_core);

    sr = srunner_create (s);
    srunner_run_all (sr, CK_NORMAL);
    number_failed = srunner_ntests_failed (sr);
    srunner_free (sr);
    return (number_failed == 0) ? 0 : 1;
}
