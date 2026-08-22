/*
   Event callbacks initialization

   Copyright (C) 2011-2025
   Free Software Foundation, Inc.

   Written by:
   Slava Zanko <slavazanko@gmail.com>, 2011.

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

#include <config.h>

#include "lib/global.h"

#include "lib/event.h"
#include "lib/mcconfig.h"
#include "lib/runtime-events.h"

#ifdef ENABLE_BACKGROUND
#include "background.h"  // (background_parent_call), background_parent_call_string()
#endif
#include "clipboard.h"  // clipboard events
#include "execute.h"    // execute_suspend()
#include "help.h"       // help_interactive_display()
#include "runtime-host.h"

#include "events_init.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

static gboolean runtime_startup_published = FALSE;
static gboolean runtime_shutdown_published = FALSE;

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
events_init (GError **mcerror)
{
    static const event_init_t standard_events[] = {
        { MCEVENT_GROUP_CORE, "clipboard_file_to_ext_clip", clipboard_file_to_ext_clip, NULL },
        { MCEVENT_GROUP_CORE, "clipboard_file_from_ext_clip", clipboard_file_from_ext_clip, NULL },
        { MCEVENT_GROUP_CORE, "clipboard_text_to_file", clipboard_text_to_file, NULL },
        { MCEVENT_GROUP_CORE, "clipboard_text_from_file", clipboard_text_from_file, NULL },

        { MCEVENT_GROUP_CORE, "help", help_interactive_display, NULL },
        { MCEVENT_GROUP_CORE, "suspend", execute_suspend, NULL },

#ifdef ENABLE_BACKGROUND
        { MCEVENT_GROUP_CORE, "background_parent_call", background_parent_call, NULL },
        { MCEVENT_GROUP_CORE, "background_parent_call_string", background_parent_call_string,
          NULL },
#endif

        { NULL, NULL, NULL, NULL },
    };

    if (!mc_event_init (mcerror))
        return FALSE;

    runtime_startup_published = FALSE;
    runtime_shutdown_published = FALSE;

    if (!mc_runtime_events_init (mcerror))
    {
        (void) mc_event_deinit (NULL);
        return FALSE;
    }

    if (!mc_event_mass_add (standard_events, mcerror))
    {
        mc_runtime_events_deinit ();
        (void) mc_event_deinit (NULL);
        return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
events_deinit (GError **mcerror)
{
    runtime_host_clear_errors ();
    mc_runtime_events_deinit ();
    runtime_startup_published = FALSE;
    runtime_shutdown_published = FALSE;
    return mc_event_deinit (mcerror);
}

/* --------------------------------------------------------------------------------------------- */

static const char *
runtime_run_mode_name (void)
{
    switch (mc_global.mc_run_mode)
    {
    case MC_RUN_FULL:
        return "full";
    case MC_RUN_EDITOR:
        return "editor";
    case MC_RUN_VIEWER:
        return "viewer";
    case MC_RUN_DIFFVIEWER:
        return "diffviewer";
    default:
        return "unknown";
    }
}

/* --------------------------------------------------------------------------------------------- */

void
events_publish_runtime_startup (void)
{
    mc_runtime_event_snapshot_t *snapshot;

    if (runtime_startup_published || !mc_runtime_events_is_initialized ())
        return;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_STARTUP);
    if (snapshot == NULL)
        return;

    snapshot->data.startup.run_mode = g_strdup (runtime_run_mode_name ());
    snapshot->data.startup.config_dir =
        g_strdup (mc_config_get_home_dir () != NULL ? mc_config_get_home_dir () : "");
    snapshot->data.startup.data_dir =
        g_strdup (mc_config_get_data_path () != NULL ? mc_config_get_data_path () : "");

    if (mc_runtime_event_publish (snapshot, NULL))
    {
        runtime_startup_published = TRUE;
        runtime_host_flush_errors ();
    }
    mc_runtime_event_snapshot_free (snapshot);
}

/* --------------------------------------------------------------------------------------------- */

void
events_publish_runtime_shutdown (const char *reason)
{
    mc_runtime_event_snapshot_t *snapshot;

    if (!runtime_startup_published || runtime_shutdown_published)
        return;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_SHUTDOWN);
    if (snapshot == NULL)
        return;

    snapshot->data.shutdown.reason = g_strdup (g_strcmp0 (reason, "quit") == 0 ? "quit" : "normal");
    if (mc_runtime_event_publish (snapshot, NULL))
        runtime_shutdown_published = TRUE;
    mc_runtime_event_snapshot_free (snapshot);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
events_runtime_is_started (void)
{
    return runtime_startup_published && !runtime_shutdown_published;
}

/* --------------------------------------------------------------------------------------------- */
