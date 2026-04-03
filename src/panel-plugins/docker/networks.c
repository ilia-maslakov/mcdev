/*
   Docker network domain logic.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026.

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

#include "src/panel-plugins/docker/docker-internal.h"

static GPtrArray *
parse_network_items (const char *output)
{
    GPtrArray *items;
    char **lines;
    int i;

    items = g_ptr_array_new_with_free_func (docker_item_free);
    if (output == NULL)
        return items;

    lines = g_strsplit (output, "\n", -1);

    for (i = 0; lines[i] != NULL; i++)
    {
        docker_item_t *item;
        char **parts;

        if (lines[i][0] == '\0')
            continue;

        parts = g_strsplit (lines[i], "\t", 4);
        if (parts[0] == NULL || parts[1] == NULL)
        {
            g_strfreev (parts);
            continue;
        }

        item = g_new0 (docker_item_t, 1);
        item->id = g_strdup (parts[0]);
        if (parts[2] != NULL && parts[3] != NULL)
            item->name = g_strdup_printf ("%s (%s/%s)", parts[1], parts[2], parts[3]);
        else
            item->name = g_strdup (parts[1]);

        g_ptr_array_add (items, item);
        g_strfreev (parts);
    }

    g_strfreev (lines);
    return items;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_networks_reload (docker_data_t *data, char **err_text)
{
    char *output = NULL;
    gboolean ok;

    ok = run_cmd ("docker network ls --format '{{.ID}}\\t{{.Name}}\\t{{.Driver}}\\t{{.Scope}}'",
                  &output, err_text);
    if (!ok)
    {
        g_free (output);
        return FALSE;
    }

    data->items = parse_network_items (output);
    g_free (output);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_networks_delete_items (docker_data_t *data, const char **names, int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        const docker_item_t *item;
        char *quoted;
        char *cmd;
        char *output = NULL;
        char *err_text = NULL;

        item = find_item_by_name (data, names[i]);
        if (item == NULL)
            continue;

        quoted = g_shell_quote (item->id);
        cmd = g_strdup_printf ("docker network rm %s", quoted);

        if (!run_cmd (cmd, &output, &err_text) && err_text != NULL && err_text[0] != '\0')
            message (D_ERROR, MSG_ERROR, "%s", err_text);

        g_free (output);
        g_free (err_text);
        g_free (cmd);
        g_free (quoted);
    }

    return MC_PPR_OK;
}
