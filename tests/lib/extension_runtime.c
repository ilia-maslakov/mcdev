/*
   lib - tests for the runtime extension ABI and loader

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

#include <gmodule.h>

#include "lib/event.h"
#include "lib/extension-runtime.h"
#include "lib/runtime-events.h"

/*** global variables ****************************************************************************/

static GError *error = NULL;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

typedef guint (*runtime_fixture_count_fn_t) (void);

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
    mc_runtime_plugins_set_directory_for_tests (TEST_RUNTIME_PLUGIN_DIR);
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    mc_runtime_plugins_shutdown ();
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
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_runtime_plugin_loads_and_receives_events)
{
    GModule *fixture_module;
    char *fixture_path;
    runtime_fixture_count_fn_t init_count;
    runtime_fixture_count_fn_t startup_count;
    runtime_fixture_count_fn_t shutdown_count;
    mc_runtime_event_snapshot_t *snapshot;

    fixture_path =
        g_build_filename (TEST_RUNTIME_PLUGIN_DIR, "runtime-plugin-fixture.so", (char *) NULL);
    fixture_module = g_module_open (fixture_path, 0);
    ck_assert_msg (fixture_module != NULL, "Failed to open runtime fixture: %s", g_module_error ());
    g_free (fixture_path);

    mctest_assert_true (g_module_symbol (fixture_module, "mc_runtime_plugin_fixture_init_count",
                                         (gpointer *) &init_count));
    mctest_assert_true (g_module_symbol (fixture_module, "mc_runtime_plugin_fixture_startup_count",
                                         (gpointer *) &startup_count));
    mctest_assert_true (g_module_symbol (fixture_module, "mc_runtime_plugin_fixture_shutdown_count",
                                         (gpointer *) &shutdown_count));

    mctest_assert_true (mc_runtime_plugins_load (&error));
    mctest_assert_true (mc_runtime_plugins_are_loaded ());
    ck_assert_int_eq ((int) mc_runtime_plugins_count (), 1);
    ck_assert_int_eq ((int) init_count (), 1);

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);
    ck_assert_int_eq ((int) startup_count (), 1);

    mc_runtime_plugins_shutdown ();
    mctest_assert_false (mc_runtime_plugins_are_loaded ());
    ck_assert_int_eq ((int) mc_runtime_plugins_count (), 0);
    ck_assert_int_eq ((int) shutdown_count (), 1);

    g_module_close (fixture_module);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_runtime_plugin_loads_and_receives_events);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
