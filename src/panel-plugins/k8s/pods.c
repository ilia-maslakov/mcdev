/*
   Kubernetes panel plugin -- pod domain logic.

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

static const mc_panel_column_t k8s_pod_columns[] = {
    { "status",   "Status",   10, FALSE, J_LEFT_FIT, TRUE },
    { "restarts", "Restarts",  5, FALSE, J_RIGHT,    TRUE },
    { "age",      "Age",       8, FALSE, J_LEFT_FIT, TRUE },
    { "node",     "Node",     20, FALSE, J_LEFT_FIT, TRUE },
};

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
k8s_pods_reload (k8s_data_t *data, char **err_text)
{
    char *quoted_ns;
    char *quoted_ctx;
    char *cmd;
    char *output;
    char **lines;
    int i;

    quoted_ns = g_shell_quote (data->namespace != NULL ? data->namespace : "default");
    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");
    cmd = g_strdup_printf (
        "%s get pods -n %s --context %s"
        " -o custom-columns="
        "NAME:.metadata.name,"
        "STATUS:.status.phase,"
        "RESTARTS:.status.containerStatuses[0].restartCount,"
        "AGE:.metadata.creationTimestamp,"
        "NODE:.spec.nodeName"
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
        item->kind = K8S_ITEM_POD;
        item->is_dir = TRUE;
        item->name = g_strdup (cols[0] != NULL ? cols[0] : "");
        item->status = g_strdup (cols[1] != NULL ? cols[1] : "");
        item->restarts = g_strdup (cols[2] != NULL ? cols[2] : "");
        item->age = g_strdup (cols[3] != NULL ? cols[3] : "");
        item->node_name = g_strdup (cols[4] != NULL ? cols[4] : "");

        g_ptr_array_add (data->items, item);
        g_strfreev (cols);
    }
    g_strfreev (lines);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
k8s_pods_reload_details (k8s_data_t *data)
{
    static const char *detail_names[] = { NULL, NULL, NULL, NULL };
    int i;

    detail_names[0] = k8s_logs_entry;
    detail_names[1] = k8s_exec_entry;
    detail_names[2] = k8s_describe_entry;
    detail_names[3] = k8s_yaml_entry;

    data->items = g_ptr_array_new_with_free_func (k8s_item_free);

    for (i = 0; i < 4; i++)
    {
        k8s_item_t *item;

        item = g_new0 (k8s_item_t, 1);
        item->kind = K8S_ITEM_POD_DETAIL_DIR;
        item->is_dir = FALSE;
        item->name = g_strdup (detail_names[i]);
        g_ptr_array_add (data->items, item);
    }
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
k8s_pods_exec (k8s_data_t *data)
{
    char *quoted_pod;
    char *quoted_ns;
    char *quoted_ctx;
    char *cmd;

    if (data->selected_pod == NULL)
        return MC_PPR_FAILED;

    quoted_pod = g_shell_quote (data->selected_pod);
    quoted_ns = g_shell_quote (data->namespace != NULL ? data->namespace : "default");
    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");
    cmd = g_strdup_printf ("%s exec -it %s -n %s --context %s -- sh",
                           data->kubectl_full, quoted_pod, quoted_ns, quoted_ctx);
    g_free (quoted_pod);
    g_free (quoted_ns);
    g_free (quoted_ctx);

    if (data->host != NULL && data->host->run_command != NULL)
        data->host->run_command (data->host, cmd, 0);

    g_free (cmd);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
k8s_pods_view (k8s_data_t *data, const char *fname)
{
    char *quoted_pod;
    char *quoted_ns;
    char *quoted_ctx;
    char *cmd = NULL;

    if (data->selected_pod == NULL || fname == NULL)
        return MC_PPR_NOT_SUPPORTED;

    quoted_pod = g_shell_quote (data->selected_pod);
    quoted_ns = g_shell_quote (data->namespace != NULL ? data->namespace : "default");
    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");

    if (strcmp (fname, k8s_logs_entry) == 0)
        cmd = g_strdup_printf ("%s logs %s -n %s --context %s --tail=1000",
                               data->kubectl_full, quoted_pod, quoted_ns, quoted_ctx);
    else if (strcmp (fname, k8s_describe_entry) == 0)
        cmd = g_strdup_printf ("%s describe pod %s -n %s --context %s",
                               data->kubectl_full, quoted_pod, quoted_ns, quoted_ctx);
    else if (strcmp (fname, k8s_yaml_entry) == 0)
        cmd = g_strdup_printf ("%s get pod %s -n %s --context %s -o yaml",
                               data->kubectl_full, quoted_pod, quoted_ns, quoted_ctx);

    g_free (quoted_pod);
    g_free (quoted_ns);
    g_free (quoted_ctx);

    if (cmd == NULL)
        return MC_PPR_NOT_SUPPORTED;

    k8s_ui_view_command (cmd);
    g_free (cmd);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
k8s_pods_delete (k8s_data_t *data, const char **names, int count)
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
        cmd = g_strdup_printf ("%s delete pod %s -n %s --context %s",
                               data->kubectl_full, quoted_name, quoted_ns, quoted_ctx);
        g_free (quoted_name);

        if (!k8s_run_cmd (cmd, NULL, &err_text)
            && data->host != NULL && data->host->message != NULL)
        {
            char *msg;

            msg = g_strdup_printf ("Failed to delete pod %s: %s", names[i],
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
k8s_pods_get_columns (size_t *count)
{
    if (count != NULL)
        *count = G_N_ELEMENTS (k8s_pod_columns);
    return k8s_pod_columns;
}

/* --------------------------------------------------------------------------------------------- */

const char *
k8s_pods_get_column_value (k8s_data_t *data, const char *name, const char *col)
{
    const k8s_item_t *item;

    item = k8s_find_item (data, name);
    if (item == NULL)
        return NULL;

    if (strcmp (col, "status") == 0)
        return item->status;
    if (strcmp (col, "restarts") == 0)
        return item->restarts;
    if (strcmp (col, "age") == 0)
        return item->age;
    if (strcmp (col, "node") == 0)
        return item->node_name;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
k8s_pods_get_default_format (void)
{
    return "name | status:10 | restarts:5 | age:8 | node:20";
}
