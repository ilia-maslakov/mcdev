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

/* Return TRUE if host (not NUL-terminated, length len) is the local machine. */
static gboolean
osc7_host_is_local (const char *host, size_t len)
{
    const char *local;

    if (len == 0)
        return TRUE; /* empty host == localhost */
    if (len == 9 && memcmp (host, "localhost", 9) == 0)
        return TRUE;
    local = g_get_host_name ();
    return (local != NULL && strlen (local) == len && memcmp (host, local, len) == 0);
}

/* Decode a local path from OSC 7 payload "7;file://[host]/path".
 * Returns NULL for non-local hostnames and for non-file:// URIs.
 * Tolerates unencoded paths (most shells do not percent-encode $PWD). */
char *
mcterm_osc7_uri_to_path (const char *osc7_raw)
{
    const char *uri;
    const char *host_start;
    const char *path;

    if (osc7_raw == NULL || strncmp (osc7_raw, "7;", 2) != 0)
        return NULL;

    uri = osc7_raw + 2;
    if (strncmp (uri, "file://", 7) != 0)
        return NULL;

    host_start = uri + 7;
    if (*host_start == '/')
    {
        path = host_start; /* empty hostname: file:///path */
    }
    else
    {
        path = strchr (host_start, '/');
        if (path == NULL)
            return NULL;
        if (!osc7_host_is_local (host_start, (size_t) (path - host_start)))
            return NULL;
    }

    /* Decode percent-encoded sequences; leave unencoded chars as-is. */
    return g_uri_unescape_string (path, "/");
}

/* --------------------------------------------------------------------------------------------- */

static char *
mcterm_cwd_from_osc7 (WMcTerm *t)
{
    return mcterm_osc7_uri_to_path (mcterm_osc7_raw (t));
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

    if (panel_cwd != NULL && strcmp (path, panel_cwd) == 0)
    {
        g_free (path);
        return NULL;
    }

    return path;
}

/* --------------------------------------------------------------------------------------------- */
