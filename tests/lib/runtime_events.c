/*
   lib - tests for runtime extension events

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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define TEST_SUITE_NAME "/lib"

#include "tests/mctest.h"

#include "lib/event.h"
#include "lib/runtime-events.h"

/*** global variables ****************************************************************************/

static GError *error = NULL;
static int runtime_context;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

typedef struct
{
    GString *calls;
    const char *text;
} append_data_t;

typedef struct
{
    GString *calls;
    mc_runtime_subscription_t subscription_to_remove;
    gboolean removed;
} unsubscribe_during_dispatch_data_t;

typedef struct
{
    GString *calls;
    guint calls_count;
} reentrant_dispatch_data_t;

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    error = NULL;

    ck_assert_msg (mc_event_init (&error), "Failed to initialize event transport: %s",
                   error != NULL ? error->message : "unknown error");
    ck_assert_msg (mc_runtime_events_init (&error), "Failed to initialize runtime events: %s",
                   error != NULL ? error->message : "unknown error");
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    mc_runtime_events_deinit ();
    g_clear_error (&error);
    ck_assert_msg (mc_event_deinit (&error), "Failed to deinitialize event transport: %s",
                   error != NULL ? error->message : "unknown error");
    g_clear_error (&error);
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_snapshot_t *
startup_snapshot_new (void)
{
    mc_runtime_event_snapshot_t *snapshot;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_STARTUP);
    snapshot->data.startup.run_mode = g_strdup ("full");
    snapshot->data.startup.config_dir = g_strdup ("/tmp/config");
    snapshot->data.startup.data_dir = g_strdup ("/tmp/data");

    return snapshot;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_snapshot_t *
selection_snapshot_new (guint selected_count)
{
    mc_runtime_event_snapshot_t *snapshot;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED);
    snapshot->data.panel_selection_changed.panel.kind = MC_RUNTIME_HANDLE_PANEL;
    snapshot->data.panel_selection_changed.panel.id = 7;
    snapshot->data.panel_selection_changed.panel.generation = 3;
    snapshot->data.panel_selection_changed.selected_count = selected_count;
    snapshot->data.panel_selection_changed.selected_truncated = FALSE;

    return snapshot;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_snapshot_t *
editor_key_snapshot_new (void)
{
    mc_runtime_event_snapshot_t *snapshot;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor.kind = MC_RUNTIME_HANDLE_EDITOR;
    snapshot->data.editor_key.editor.id = 5;
    snapshot->data.editor_key.editor.generation = 2;
    snapshot->data.editor_key.key.name = g_strdup ("Ctrl-S");
    snapshot->data.editor_key.key.code = 19;

    return snapshot;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
append_callback (gpointer context, const mc_runtime_event_snapshot_t *snapshot, gpointer user_data)
{
    append_data_t *data = (append_data_t *) user_data;

    (void) context;
    (void) snapshot;

    g_string_append (data->calls, data->text);
    return MC_RUNTIME_EVENT_PASS;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
unsubscribe_during_dispatch_callback (gpointer context, const mc_runtime_event_snapshot_t *snapshot,
                                      gpointer user_data)
{
    unsubscribe_during_dispatch_data_t *data = (unsubscribe_during_dispatch_data_t *) user_data;

    (void) snapshot;

    g_string_append (data->calls, "a");
    if (!data->removed)
    {
        mctest_assert_true (mc_runtime_events_unsubscribe (context, data->subscription_to_remove));
        data->removed = TRUE;
    }
    return MC_RUNTIME_EVENT_PASS;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
error_callback (gpointer context, const mc_runtime_event_snapshot_t *snapshot, gpointer user_data)
{
    guint *calls = (guint *) user_data;

    (void) context;
    (void) snapshot;

    (*calls)++;
    return MC_RUNTIME_EVENT_ERROR;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
reentrant_callback (gpointer context, const mc_runtime_event_snapshot_t *snapshot,
                    gpointer user_data)
{
    reentrant_dispatch_data_t *data = (reentrant_dispatch_data_t *) user_data;

    (void) context;
    (void) snapshot;

    if (data->calls_count++ == 0)
    {
        mc_runtime_event_snapshot_t *next_snapshot;

        g_string_append (data->calls, "a");
        next_snapshot = startup_snapshot_new ();
        mctest_assert_true (mc_runtime_event_publish (next_snapshot, &error));
        mc_runtime_event_snapshot_free (next_snapshot);
        g_string_append (data->calls, "b");
    }
    else
        g_string_append (data->calls, "c");

    return MC_RUNTIME_EVENT_PASS;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
selection_callback (gpointer context, const mc_runtime_event_snapshot_t *snapshot,
                    gpointer user_data)
{
    reentrant_dispatch_data_t *data = (reentrant_dispatch_data_t *) user_data;

    (void) context;

    g_string_append_printf (data->calls, "%u",
                            snapshot->data.panel_selection_changed.selected_count);

    if (data->calls_count++ == 0)
    {
        guint selected_count;

        for (selected_count = 2; selected_count <= 4; selected_count++)
        {
            mc_runtime_event_snapshot_t *next_snapshot = selection_snapshot_new (selected_count);

            mctest_assert_true (mc_runtime_event_publish (next_snapshot, &error));
            mc_runtime_event_snapshot_free (next_snapshot);
        }
    }

    return MC_RUNTIME_EVENT_PASS;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
consume_callback (gpointer context, const mc_runtime_event_snapshot_t *snapshot, gpointer user_data)
{
    append_data_t *data = (append_data_t *) user_data;

    (void) context;
    (void) snapshot;

    g_string_append (data->calls, data->text);
    return MC_RUNTIME_EVENT_CONSUME;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_names)
{
    ck_assert_int_eq (mc_runtime_event_id_from_name (MCEVENT_RUNTIME_STARTUP),
                      MC_RUNTIME_EVENT_STARTUP);
    ck_assert_str_eq (mc_runtime_event_name (MC_RUNTIME_EVENT_EDITOR_KEY),
                      MCEVENT_RUNTIME_EDITOR_KEY);
    mctest_assert_true (mc_runtime_event_name_is_valid (MCEVENT_RUNTIME_VIEWER_OPEN));
    mctest_assert_false (mc_runtime_event_name_is_valid ("Startup"));
    ck_assert_int_eq (mc_runtime_event_id_from_name ("Panel.chdir"), MC_RUNTIME_EVENT_INVALID);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_snapshot_copy)
{
    mc_runtime_event_snapshot_t *snapshot;
    mc_runtime_event_snapshot_t *copy;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_PANEL_CHDIR);
    snapshot->data.panel_chdir.panel.kind = MC_RUNTIME_HANDLE_PANEL;
    snapshot->data.panel_chdir.panel.id = 12;
    snapshot->data.panel_chdir.panel.generation = 4;
    snapshot->data.panel_chdir.old_path = g_strdup ("/old");
    snapshot->data.panel_chdir.new_path = g_strdup ("/new");
    snapshot->data.panel_chdir.cause = g_strdup ("user");

    copy = mc_runtime_event_snapshot_copy (snapshot);

    mctest_assert_not_null (copy);
    mctest_assert_str_eq (copy->data.panel_chdir.old_path, "/old");
    mctest_assert_ptr_ne (copy->data.panel_chdir.old_path, snapshot->data.panel_chdir.old_path);

    snapshot->data.panel_chdir.old_path[1] = 'X';
    mctest_assert_str_eq (copy->data.panel_chdir.old_path, "/old");

    mc_runtime_event_snapshot_free (copy);
    mc_runtime_event_snapshot_free (snapshot);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_snapshot_validation)
{
    mc_runtime_event_snapshot_t *snapshot;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_STARTUP);

    mctest_assert_false (mc_runtime_event_snapshot_validate (snapshot, &error));
    mctest_assert_not_null (error);
    g_clear_error (&error);

    snapshot->data.startup.run_mode = g_strdup ("full");
    snapshot->data.startup.config_dir = g_strdup ("/tmp/config");
    snapshot->data.startup.data_dir = g_strdup ("/tmp/data");
    mctest_assert_true (mc_runtime_event_snapshot_validate (snapshot, &error));

    mc_runtime_event_snapshot_free (snapshot);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_handles_are_opaque_and_invalidated)
{
    int first_object;
    int second_object;
    mc_runtime_handle_t first;
    mc_runtime_handle_t same_first;
    mc_runtime_handle_t replacement;

    first = mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_PANEL, &first_object);
    same_first = mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_PANEL, &first_object);

    mctest_assert_true (mc_runtime_handle_is_valid (&first));
    ck_assert_uint_eq (first.id, same_first.id);
    ck_assert_uint_eq (first.generation, same_first.generation);
    ck_assert_ptr_eq (mc_runtime_handle_resolve (&first, MC_RUNTIME_HANDLE_PANEL), &first_object);
    ck_assert_ptr_null (mc_runtime_handle_resolve (&first, MC_RUNTIME_HANDLE_EDITOR));

    mc_runtime_handle_invalidate_object (MC_RUNTIME_HANDLE_PANEL, &first_object);
    ck_assert_ptr_null (mc_runtime_handle_resolve (&first, MC_RUNTIME_HANDLE_PANEL));

    replacement = mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_PANEL, &second_object);
    mctest_assert_true (mc_runtime_handle_is_valid (&replacement));
    ck_assert_uint_ne (first.id, replacement.id);
    ck_assert_uint_ne (first.generation, replacement.generation);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_priority_order)
{
    GString *calls;
    append_data_t first = { NULL, "a" };
    append_data_t second = { NULL, "b" };
    append_data_t third = { NULL, "c" };
    append_data_t fourth = { NULL, "d" };
    mc_runtime_event_snapshot_t *snapshot;

    calls = g_string_new (NULL);
    first.calls = calls;
    second.calls = calls;
    third.calls = calls;
    fourth.calls = calls;

    mctest_assert_true ((mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_STARTUP, 0,
                                                      append_callback, &first, NULL, &error)
                         != 0));
    mctest_assert_true ((mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_STARTUP,
                                                      20, append_callback, &second, NULL, &error)
                         != 0));
    mctest_assert_true ((mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_STARTUP,
                                                      20, append_callback, &third, NULL, &error)
                         != 0));
    mctest_assert_true ((mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_STARTUP,
                                                      -20, append_callback, &fourth, NULL, &error)
                         != 0));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_str_eq (calls->str, "bcad");

    mc_runtime_event_snapshot_free (snapshot);
    g_string_free (calls, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_unsubscribe_during_dispatch)
{
    GString *calls;
    append_data_t second_data = { NULL, "b" };
    unsubscribe_during_dispatch_data_t data;
    mc_runtime_subscription_t first_subscription;
    mc_runtime_event_snapshot_t *snapshot;

    calls = g_string_new (NULL);
    second_data.calls = calls;
    data.calls = calls;
    data.removed = FALSE;
    data.subscription_to_remove = mc_runtime_events_subscribe (
        &runtime_context, MC_RUNTIME_EVENT_STARTUP, 0, append_callback, &second_data, NULL, &error);
    mctest_assert_true ((data.subscription_to_remove != 0));

    first_subscription =
        mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_STARTUP, 10,
                                     unsubscribe_during_dispatch_callback, &data, NULL, &error);
    mctest_assert_true ((first_subscription != 0));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_str_eq (calls->str, "a");

    g_string_truncate (calls, 0);
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_str_eq (calls->str, "a");

    mc_runtime_event_snapshot_free (snapshot);
    g_string_free (calls, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_disables_callback_after_three_errors)
{
    guint calls = 0;
    mc_runtime_subscription_t subscription;
    mc_runtime_event_snapshot_t *snapshot;
    guint i;

    subscription = mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_STARTUP, 0,
                                                error_callback, &calls, NULL, &error);
    mctest_assert_true ((subscription != 0));

    snapshot = startup_snapshot_new ();
    for (i = 0; i < 4; i++)
        mctest_assert_true (mc_runtime_event_publish (snapshot, &error));

    ck_assert_int_eq ((int) calls, 3);
    mctest_assert_false (mc_runtime_events_unsubscribe (&runtime_context, subscription));

    mc_runtime_event_snapshot_free (snapshot);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_defers_reentrant_publish)
{
    reentrant_dispatch_data_t data;
    mc_runtime_event_snapshot_t *snapshot;

    data.calls = g_string_new (NULL);
    data.calls_count = 0;

    mctest_assert_true ((mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_STARTUP, 0,
                                                      reentrant_callback, &data, NULL, &error)
                         != 0));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_str_eq (data.calls->str, "abc");

    mc_runtime_event_snapshot_free (snapshot);
    g_string_free (data.calls, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_coalesces_panel_selection)
{
    reentrant_dispatch_data_t data;
    mc_runtime_event_snapshot_t *snapshot;

    data.calls = g_string_new (NULL);
    data.calls_count = 0;

    mctest_assert_true (
        (mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED, 0,
                                      selection_callback, &data, NULL, &error)
         != 0));

    snapshot = selection_snapshot_new (1);
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_str_eq (data.calls->str, "14");

    mc_runtime_event_snapshot_free (snapshot);
    g_string_free (data.calls, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_event_editor_key_can_be_consumed)
{
    GString *calls;
    append_data_t consume_data = { NULL, "consume" };
    append_data_t pass_data = { NULL, "pass" };
    mc_runtime_event_snapshot_t *snapshot;

    calls = g_string_new (NULL);
    consume_data.calls = calls;
    pass_data.calls = calls;

    mctest_assert_true (
        (mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_EDITOR_KEY, 10,
                                      consume_callback, &consume_data, NULL, &error)
         != 0));
    mctest_assert_true ((mc_runtime_events_subscribe (&runtime_context, MC_RUNTIME_EVENT_EDITOR_KEY,
                                                      0, append_callback, &pass_data, NULL, &error)
                         != 0));

    snapshot = editor_key_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_true (snapshot->consumed);
    mctest_assert_str_eq (calls->str, "consume");

    mc_runtime_event_snapshot_free (snapshot);
    g_string_free (calls, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_runtime_event_names);
    tcase_add_test (tc_core, test_runtime_event_snapshot_copy);
    tcase_add_test (tc_core, test_runtime_event_snapshot_validation);
    tcase_add_test (tc_core, test_runtime_event_handles_are_opaque_and_invalidated);
    tcase_add_test (tc_core, test_runtime_event_priority_order);
    tcase_add_test (tc_core, test_runtime_event_unsubscribe_during_dispatch);
    tcase_add_test (tc_core, test_runtime_event_disables_callback_after_three_errors);
    tcase_add_test (tc_core, test_runtime_event_defers_reentrant_publish);
    tcase_add_test (tc_core, test_runtime_event_coalesces_panel_selection);
    tcase_add_test (tc_core, test_runtime_event_editor_key_can_be_consumed);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
