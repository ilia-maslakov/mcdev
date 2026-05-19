/*
   Panelize plugin - activation URL parser.

   Copyright (C) 1995-2026
   Free Software Foundation, Inc.

   Written by:
   Janne Kukonlehto, 1995
   Jakub Jelinek, 1995
   Andrew Borodin <aborodin@vmail.ru> 2011-2023
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

#include "panelize_url.h"

/*** file scope macro definitions ****************************************************************/

#define URL_PREFIX     "panelize:"
#define URL_PREFIX_LEN 9

/*** public functions ****************************************************************************/

gboolean
panelize_url_parse (const char *open_path, panelize_url_t *out_url)
{
    char **parts;
    char **p;

    if (out_url == NULL)
        return FALSE;

    out_url->file = NULL;
    out_url->label = NULL;
    out_url->nul_sep = FALSE;

    if (open_path == NULL || open_path[0] == '\0')
        return FALSE;

    if (strncmp (open_path, URL_PREFIX, URL_PREFIX_LEN) != 0)
        return FALSE;

    parts = g_strsplit (open_path + URL_PREFIX_LEN, ";", -1);
    for (p = parts; *p != NULL; p++)
    {
        char *eq = strchr (*p, '=');
        const char *key;
        const char *value;

        if (eq == NULL)
        {
            /* bare flag */
            if (strcmp (*p, "nul") == 0)
                out_url->nul_sep = TRUE;
            continue;
        }

        *eq = '\0';
        key = *p;
        value = eq + 1;

        if (strcmp (key, "from-file") == 0)
        {
            g_free (out_url->file);
            out_url->file = g_strdup (value);
        }
        else if (strcmp (key, "label") == 0)
        {
            g_free (out_url->label);
            out_url->label = g_strdup (value);
        }
        /* unknown keys silently ignored */
    }
    g_strfreev (parts);

    return (out_url->file != NULL);
}

/* --------------------------------------------------------------------------------------------- */

void
panelize_url_clear (panelize_url_t *url)
{
    if (url == NULL)
        return;
    g_free (url->file);
    g_free (url->label);
    url->file = NULL;
    url->label = NULL;
    url->nul_sep = FALSE;
}

/* --------------------------------------------------------------------------------------------- */
