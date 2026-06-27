/*
   Kubernetes panel plugin -- deployment/workload domain logic.

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

static const mc_panel_column_t k8s_deployment_columns[] = {
    { "ready", "Ready", 8, FALSE, J_RIGHT, TRUE },
    { "age", "Age", 8, FALSE, J_LEFT_FIT, TRUE },
};

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
k8s_deployments_reload (k8s_data_t *data, char **err_text)
{
    char *quoted_ns;
    char *quoted_ctx;
    char *cmd;
    char *output;
    char **lines;
    int i;

    quoted_ns = g_shell_quote (data->namespace != NULL ? data->namespace : "default");
    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");
    if (data->all_namespaces)
        cmd = g_strdup_printf ("%s get deployments --all-namespaces --context %s"
                               " -o custom-columns="
                               "NAME:.metadata.name,"
                               "READY:.status.readyReplicas/.spec.replicas,"
                               "AGE:.metadata.creationTimestamp"
                               " --no-headers",
                               data->kubectl_full, quoted_ctx);
    else
        cmd = g_strdup_printf ("%s get deployments -n %s --context %s"
                               " -o custom-columns="
                               "NAME:.metadata.name,"
                               "READY:.status.readyReplicas/.spec.replicas,"
                               "AGE:.metadata.creationTimestamp"
                               " --no-headers",
                               data->kubectl_full, quoted_ns, quoted_ctx);
    g_free (quoted_ns);
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
        item->kind = K8S_ITEM_DEPLOYMENT;
        item->is_dir = FALSE;
        item->name = g_strdup (cols[0] != NULL ? cols[0] : "");
        item->ready = g_strdup (cols[1] != NULL ? cols[1] : "");
        item->age = g_strdup (cols[2] != NULL ? cols[2] : "");

        g_ptr_array_add (data->items, item);
        g_strfreev (cols);
    }
    g_strfreev (lines);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
k8s_deployments_view (k8s_data_t *data, const char *fname)
{
    char *quoted_name;
    char *quoted_ns;
    char *quoted_ctx;
    char *cmd;

    if (fname == NULL)
        return MC_PPR_NOT_SUPPORTED;

    quoted_name = g_shell_quote (fname);
    quoted_ns = g_shell_quote (data->namespace != NULL ? data->namespace : "default");
    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");
    cmd = g_strdup_printf ("%s describe deployment %s -n %s --context %s", data->kubectl_full,
                           quoted_name, quoted_ns, quoted_ctx);
    g_free (quoted_name);
    g_free (quoted_ns);
    g_free (quoted_ctx);

    k8s_ui_view_command (cmd);
    g_free (cmd);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
k8s_deployments_delete (k8s_data_t *data, const char **names, int count)
{
    char *quoted_ns;
    char *quoted_ctx;
    int i;

    quoted_ns = g_shell_quote (data->namespace != NULL ? data->namespace : "default");
    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");

    for (i = 0; i < count; i++)
    {
        char *quoted_name;
        char *cmd;
        char *err_text = NULL;

        quoted_name = g_shell_quote (names[i]);
        cmd = g_strdup_printf ("%s delete deployment %s -n %s --context %s", data->kubectl_full,
                               quoted_name, quoted_ns, quoted_ctx);
        g_free (quoted_name);

        if (!k8s_run_cmd (cmd, NULL, &err_text) && data->host != NULL
            && data->host->message != NULL)
        {
            char *msg;

            msg = g_strdup_printf ("Failed to delete deployment %s: %s", names[i],
                                   err_text != NULL ? err_text : "");
            data->host->message (data->host, D_ERROR, "Kubernetes", msg);
            g_free (msg);
        }
        g_free (err_text);
        g_free (cmd);
    }

    g_free (quoted_ns);
    g_free (quoted_ctx);
    k8s_clear_items (data);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

const mc_panel_column_t *
k8s_deployments_get_columns (size_t *count)
{
    if (count != NULL)
        *count = G_N_ELEMENTS (k8s_deployment_columns);
    return k8s_deployment_columns;
}

/* --------------------------------------------------------------------------------------------- */

const char *
k8s_deployments_get_column_value (k8s_data_t *data, const char *name, const char *col)
{
    const k8s_item_t *item;

    item = k8s_find_item (data, name);
    if (item == NULL)
        return NULL;

    if (strcmp (col, "ready") == 0)
        return item->ready;
    if (strcmp (col, "age") == 0)
        return item->age;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
k8s_deployments_get_default_format (void)
{
    return "type name | ready:8 | age:8";
}
