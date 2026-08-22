/*
   lib - runtime extension loader fixture

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

#include <config.h>

#include <gmodule.h>

#include "lib/extension-runtime.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

typedef struct
{
    guint startup_calls;
} runtime_plugin_fixture_state_t;

/*** file scope variables ************************************************************************/

static const mc_runtime_host_api_v1_t *fixture_host_api = NULL;
static guint fixture_init_calls = 0;
static guint fixture_startup_calls = 0;
static guint fixture_shutdown_calls = 0;

G_MODULE_EXPORT const mc_runtime_plugin_descriptor_v1_t *mc_runtime_plugin_register_v1 (void);
G_MODULE_EXPORT guint mc_runtime_plugin_fixture_init_count (void);
G_MODULE_EXPORT guint mc_runtime_plugin_fixture_startup_count (void);
G_MODULE_EXPORT guint mc_runtime_plugin_fixture_shutdown_count (void);

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
runtime_plugin_fixture_startup (gpointer runtime_context,
                                const mc_runtime_event_snapshot_t *snapshot, gpointer user_data)
{
    runtime_plugin_fixture_state_t *state;

    (void) snapshot;
    (void) user_data;

    state = (runtime_plugin_fixture_state_t *) fixture_host_api->context_get_data (
        (mc_runtime_plugin_context_t *) runtime_context);
    if (state != NULL)
        state->startup_calls++;
    fixture_startup_calls++;

    return MC_RUNTIME_EVENT_PASS;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_plugin_fixture_init (const mc_runtime_host_api_v1_t *host_api,
                             mc_runtime_plugin_context_t *context, GError **mcerror)
{
    runtime_plugin_fixture_state_t *state;

    fixture_host_api = host_api;
    fixture_init_calls++;

    state = g_new0 (runtime_plugin_fixture_state_t, 1);
    host_api->context_set_data (context, state, g_free);

    if (host_api->subscribe (context, MC_RUNTIME_EVENT_STARTUP, 0, runtime_plugin_fixture_startup,
                             NULL, NULL, mcerror)
        == 0)
        return FALSE;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_plugin_fixture_shutdown (mc_runtime_plugin_context_t *context)
{
    (void) context;

    fixture_shutdown_calls++;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

G_MODULE_EXPORT const mc_runtime_plugin_descriptor_v1_t *
mc_runtime_plugin_register_v1 (void)
{
    static const mc_runtime_plugin_descriptor_v1_t descriptor = {
        .abi_version = MC_RUNTIME_PLUGIN_ABI_VERSION,
        .struct_size = sizeof (mc_runtime_plugin_descriptor_v1_t),
        .capability_flags = 0,
        .runtime_name = "test-runtime",
        .required_host_capabilities = MC_RUNTIME_HOST_CAP_EVENTS | MC_RUNTIME_HOST_CAP_CONTEXT_DATA,
        .init = runtime_plugin_fixture_init,
        .shutdown = runtime_plugin_fixture_shutdown,
    };

    return &descriptor;
}

/* --------------------------------------------------------------------------------------------- */

G_MODULE_EXPORT guint
mc_runtime_plugin_fixture_init_count (void)
{
    return fixture_init_calls;
}

/* --------------------------------------------------------------------------------------------- */

G_MODULE_EXPORT guint
mc_runtime_plugin_fixture_startup_count (void)
{
    return fixture_startup_calls;
}

/* --------------------------------------------------------------------------------------------- */

G_MODULE_EXPORT guint
mc_runtime_plugin_fixture_shutdown_count (void)
{
    return fixture_shutdown_calls;
}

/* --------------------------------------------------------------------------------------------- */
