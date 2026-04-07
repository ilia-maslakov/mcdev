/*
   Kubernetes panel plugin -- node domain logic.

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

#include <string.h>

#include "lib/global.h"
#include "lib/panel-plugin.h"

#include "src/panel-plugins/k8s/k8s-internal.h"

/* --------------------------------------------------------------------------------------------- */
/*** file scope variables ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static const mc_panel_column_t k8s_node_columns[] = {
    { "status",  "Status",  10, FALSE, J_LEFT_FIT, TRUE },
    { "roles",   "Roles",   15, FALSE, J_LEFT_FIT, TRUE },
    { "age",     "Age",      8, FALSE, J_LEFT_FIT, TRUE },
    { "version", "Version", 14, FALSE, J_LEFT_FIT, TRUE },
};

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
k8s_nodes_reload (k8s_data_t *data, char **err_text)
{
    char *quoted_ctx;
    char *cmd;
    char *output;
    char **lines;
    int i;

    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");
    cmd = g_strdup_printf (
        "%s get nodes --context %s"
        " -o custom-columns="
        "NAME:.metadata.name,"
        "STATUS:.status.conditions[-1].type,"
        "ROLES:.metadata.labels.kubernetes\\.io/role,"
        "AGE:.metadata.creationTimestamp,"
        "VERSION:.status.nodeInfo.kubeletVersion"
        " --no-headers",
        data->kubectl_full, quoted_ctx);
    g_free (quoted_ctx);

    if (!k8s_run_cmd (cmd, &output, err_text))
    {
        g_free (cmd);
        return FALSE;
    }
    g_free (cmd);

    data->items = g_ptr_array_new_with_free_func (k8s_item_free);

    if (output == NULL || output[0] == '\0')
    {
        g_free (output);
        return TRUE;
    }

    lines = g_strsplit (output, "\n", -1);
    g_free (output);

    for (i = 0; lines[i] != NULL; i++)
    {
        char **cols;
        k8s_item_t *item;

        g_strstrip (lines[i]);
        if (lines[i][0] == '\0')
            continue;

        cols = g_strsplit_set (lines[i], " \t", -1);

        item = g_new0 (k8s_item_t, 1);
        item->kind = K8S_ITEM_NODE;
        item->is_dir = FALSE;
        item->name = g_strdup (cols[0] != NULL ? cols[0] : "");
        item->status = g_strdup (cols[1] != NULL ? cols[1] : "");
        item->roles = g_strdup (cols[2] != NULL ? cols[2] : "");
        item->age = g_strdup (cols[3] != NULL ? cols[3] : "");
        item->version = g_strdup (cols[4] != NULL ? cols[4] : "");

        g_ptr_array_add (data->items, item);
        g_strfreev (cols);
    }
    g_strfreev (lines);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

const mc_panel_column_t *
k8s_nodes_get_columns (size_t *count)
{
    if (count != NULL)
        *count = G_N_ELEMENTS (k8s_node_columns);
    return k8s_node_columns;
}

/* --------------------------------------------------------------------------------------------- */

const char *
k8s_nodes_get_column_value (k8s_data_t *data, const char *name, const char *col)
{
    const k8s_item_t *item;

    item = k8s_find_item (data, name);
    if (item == NULL)
        return NULL;

    if (strcmp (col, "status") == 0)
        return item->status;
    if (strcmp (col, "roles") == 0)
        return item->roles;
    if (strcmp (col, "age") == 0)
        return item->age;
    if (strcmp (col, "version") == 0)
        return item->version;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
k8s_nodes_get_default_format (void)
{
    return "name | status:10 | roles:15 | age:8 | version:14";
}
