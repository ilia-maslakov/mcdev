/*
   Midnight Commander - mcterm cwd synchronization.

   Tracks the shell's working directory via OSC 7 and syncs it with
   the file-manager panel when switching between panel and terminal mode.

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

#include <config.h>
#include <string.h>

#include "lib/global.h"

#include "mcterm.h"
#include "mcterm_cwd.h"

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

/* Parse "7;file://[host]/path" from an OSC 7 raw string.
 * Returns an owned g_strdup'd path, or NULL if the string is not valid OSC 7. */
static char *
osc7_uri_to_path (const char *osc7_raw)
{
    const char *path;

    if (osc7_raw == NULL || strncmp (osc7_raw, "7;file://", 9) != 0)
        return NULL;

    path = osc7_raw + 9; /* skip "7;file://" */

    /* Skip optional hostname up to the path component. */
    if (*path != '/')
    {
        path = strchr (path, '/');
        if (path == NULL)
            return NULL;
    }

    return (*path != '\0') ? g_strdup (path) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Return the shell's current cwd from its most recent OSC 7 notification,
 * or NULL if not yet received.  Caller must g_free() the result. */
static char *
mcterm_cwd_from_osc7 (WMcTerm *t)
{
    return osc7_uri_to_path (mcterm_osc7_raw (t));
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

char *
mcterm_cwd_on_exit (WMcTerm *t, const char *panel_cwd)
{
    char *path;

    path = mcterm_cwd_from_osc7 (t);
    if (path == NULL)
        return NULL;

    /* No navigation needed when shell is already at the panel dir. */
    if (panel_cwd != NULL && strcmp (path, panel_cwd) == 0)
    {
        g_free (path);
        return NULL;
    }

    return path; /* caller must g_free() */
}

/* --------------------------------------------------------------------------------------------- */
