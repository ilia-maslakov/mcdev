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
#include <stdlib.h>
#include <time.h>

#include "lib/global.h"
#include "lib/tty/key.h"  // ALT()
#include "lib/panel-plugin.h"

#include "src/panel-plugins/k8s/k8s-internal.h"
#include "src/panel-plugins/k8s/k8s-logs.h"

/* --------------------------------------------------------------------------------------------- */
/*** file scope variables ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static const mc_panel_column_t k8s_pod_columns[] = {
    { "ready", "Ready", 5, FALSE, J_RIGHT, TRUE },
    { "status", "Status", 6, FALSE, J_LEFT_FIT, TRUE },
    { "restage", "Rest/Age", 8, FALSE, J_RIGHT, TRUE },
    { "ip", "IP", 8, FALSE, J_RIGHT, TRUE },
    { "node", "Node", 4, FALSE, J_LEFT_FIT, TRUE },
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope helpers **************************************************************************/

/* Convert "true,false,true" -> "2/3". "<none>" -> "0/0". */
static char *
k8s_pods_format_ready (const char *s)
{
    int total = 0, ready = 0;
    const char *p;
    const char *tok;

    if (s == NULL || s[0] == '\0' || strcmp (s, "<none>") == 0)
        return g_strdup ("0/0");
    tok = s;
    for (p = s;; p++)
    {
        if (*p == ',' || *p == '\0')
        {
            total++;
            if ((p - tok) == 4 && strncmp (tok, "true", 4) == 0)
                ready++;
            if (*p == '\0')
                break;
            tok = p + 1;
        }
    }
    return g_strdup_printf ("%d/%d", ready, total);
}

/* Convert ISO timestamp to relative ("5d", "3h", "27m", "12s").
   "<none>" or unparseable -> "". */
static char *
k8s_pods_format_age (const char *iso)
{
    struct tm tm;
    char *end;
    time_t then, now;
    long diff;

    if (iso == NULL || iso[0] == '\0' || strcmp (iso, "<none>") == 0)
        return g_strdup ("");
    memset (&tm, 0, sizeof (tm));
    end = strptime (iso, "%Y-%m-%dT%H:%M:%SZ", &tm);
    if (end == NULL)
        return g_strdup (iso);
    then = timegm (&tm);
    now = time (NULL);
    diff = (long) (now - then);
    if (diff < 0)
        diff = 0;
    if (diff < 60)
        return g_strdup_printf ("%lds", diff);
    if (diff < 3600)
        return g_strdup_printf ("%ldm", diff / 60);
    if (diff < 86400)
        return g_strdup_printf ("%ldh", diff / 3600);
    return g_strdup_printf ("%ldd", diff / 86400);
}

/* Whitespace-aware tokenizer; treats runs of spaces/tabs as one delim. */
static char *
k8s_next_field (char *line, char **saveptr)
{
    return strtok_r (line, " \t", saveptr);
}

/* Node name -> suffix after the last '-' ("cl1b...dae5-upyc" -> "upyc"). */
static char *
k8s_pods_format_node (const char *full)
{
    const char *dash;

    if (full == NULL || full[0] == '\0' || strcmp (full, "<none>") == 0)
        return g_strdup ("");
    dash = strrchr (full, '-');
    return g_strdup (dash != NULL ? dash + 1 : full);
}

/* IP -> "~" prefix + last two octets ("10.10.178.115" -> "~178.115"). */
static char *
k8s_pods_format_ip (const char *full)
{
    const char *p, *second_last_dot = NULL, *last_dot = NULL;

    if (full == NULL || full[0] == '\0' || strcmp (full, "<none>") == 0)
        return g_strdup ("");
    for (p = full; *p; p++)
        if (*p == '.')
        {
            second_last_dot = last_dot;
            last_dot = p;
        }
    return g_strdup_printf ("~%s", second_last_dot != NULL ? second_last_dot + 1 : full);
}

/* In all-namespaces mode actions use the namespace captured with the pod. */
static const char *
k8s_pods_action_ns (const k8s_data_t *data)
{
    if (data->selected_namespace != NULL && data->selected_namespace[0] != '\0')
        return data->selected_namespace;
    return data->namespace != NULL ? data->namespace : "default";
}

static const char *
k8s_pods_name_ns (k8s_data_t *data, const char *name)
{
    const k8s_item_t *item = k8s_find_item (data, name);

    if (item != NULL && item->namespace != NULL && item->namespace[0] != '\0')
        return item->namespace;
    return data->namespace != NULL ? data->namespace : "default";
}

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
    const char *cols_spec = "NAME:.metadata.name,"
                            "READY:.status.containerStatuses[*].ready,"
                            "STATUS:.status.phase,"
                            "RESTARTS:.status.containerStatuses[0].restartCount,"
                            "AGE:.metadata.creationTimestamp,"
                            "IP:.status.podIP,"
                            "NODE:.spec.nodeName,"
                            "IMAGE:.spec.containers[0].image,"
                            "NAMESPACE:.metadata.namespace";

    quoted_ns = g_shell_quote (data->namespace != NULL ? data->namespace : "default");
    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");
    if (data->all_namespaces)
        cmd = g_strdup_printf ("%s get pods --all-namespaces --context %s -o custom-columns=%s"
                               " --no-headers",
                               data->kubectl_full, quoted_ctx, cols_spec);
    else
        cmd = g_strdup_printf ("%s get pods -n %s --context %s -o custom-columns=%s"
                               " --no-headers",
                               data->kubectl_full, quoted_ns, quoted_ctx, cols_spec);
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
        k8s_item_t *item;
        char *saveptr = NULL;
        char *tok;

        if (lines[i][0] == '\0')
            continue;

        item = g_new0 (k8s_item_t, 1);
        item->kind = K8S_ITEM_POD;
        item->is_dir = TRUE;

        tok = k8s_next_field (lines[i], &saveptr);
        item->name = g_strdup (tok != NULL ? tok : "");
        tok = k8s_next_field (NULL, &saveptr);
        item->ready = k8s_pods_format_ready (tok);
        tok = k8s_next_field (NULL, &saveptr);
        item->status = g_strdup (tok != NULL ? tok : "");
        tok = k8s_next_field (NULL, &saveptr);
        item->restarts = g_strdup (tok != NULL && strcmp (tok, "<none>") != 0 ? tok : "0");
        tok = k8s_next_field (NULL, &saveptr);
        item->age = k8s_pods_format_age (tok);
        item->restage = g_strdup_printf ("%s/%s", item->restarts, item->age);
        tok = k8s_next_field (NULL, &saveptr);
        item->ip = k8s_pods_format_ip (tok);
        tok = k8s_next_field (NULL, &saveptr);
        item->node_name = k8s_pods_format_node (tok);
        tok = k8s_next_field (NULL, &saveptr);
        item->image = g_strdup (tok != NULL && strcmp (tok, "<none>") != 0 ? tok : "");
        tok = k8s_next_field (NULL, &saveptr);
        item->namespace = g_strdup (tok != NULL && strcmp (tok, "<none>") != 0 ? tok : "");

        g_ptr_array_add (data->items, item);
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
    quoted_ns = g_shell_quote (k8s_pods_action_ns (data));
    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");
    cmd = g_strdup_printf ("%s exec -it %s -n %s --context %s -- sh", data->kubectl_full,
                           quoted_pod, quoted_ns, quoted_ctx);
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

    /* "logs" goes through the source-controller path so the user can re-fetch
       with different --since / --tail / container / pipe via M-r in mcview. */
    if (strcmp (fname, k8s_logs_entry) == 0)
    {
        k8s_logs_identity_t id = { 0 };

        id.kubectl = data->kubectl_cmd;
        id.kubeconfig = data->kubeconfig;
        id.context = data->context;
        id.namespace = (char *) k8s_pods_action_ns (data);
        id.pod = data->selected_pod;
        id.help_file = data->help_filename;
        id.options_key =
            k8s_load_hotkey (K8S_PANEL_KEY_LOGS_OPTS, K8S_PANEL_KEY_LOGS_OPTS_DEFAULT, ALT ('s'));
        id.initial_since = (char *) "5m";
        id.initial_tail = 0;
        id.initial_follow = FALSE;
        id.initial_previous = FALSE;
        return k8s_logs_open (&id) ? MC_PPR_OK : MC_PPR_FAILED;
    }

    quoted_pod = g_shell_quote (data->selected_pod);
    quoted_ns = g_shell_quote (k8s_pods_action_ns (data));
    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");

    if (strcmp (fname, k8s_describe_entry) == 0)
        cmd = g_strdup_printf ("%s describe pod %s -n %s --context %s", data->kubectl_full,
                               quoted_pod, quoted_ns, quoted_ctx);
    else if (strcmp (fname, k8s_yaml_entry) == 0)
        cmd = g_strdup_printf ("%s get pod %s -n %s --context %s -o yaml", data->kubectl_full,
                               quoted_pod, quoted_ns, quoted_ctx);

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
    char *quoted_ctx;
    int i;

    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");

    for (i = 0; i < count; i++)
    {
        char *quoted_name;
        char *quoted_ns;
        char *cmd;
        char *err_text = NULL;

        quoted_name = g_shell_quote (names[i]);
        quoted_ns = g_shell_quote (k8s_pods_name_ns (data, names[i]));
        cmd = g_strdup_printf ("%s delete pod %s -n %s --context %s", data->kubectl_full,
                               quoted_name, quoted_ns, quoted_ctx);
        g_free (quoted_name);
        g_free (quoted_ns);

        if (!k8s_run_cmd (cmd, NULL, &err_text) && data->host != NULL
            && data->host->message != NULL)
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

    if (strcmp (col, "ready") == 0)
        return item->ready;
    if (strcmp (col, "status") == 0)
        return item->status;
    if (strcmp (col, "restage") == 0)
        return item->restage;
    if (strcmp (col, "ip") == 0)
        return item->ip;
    if (strcmp (col, "node") == 0)
        return item->node_name;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
k8s_pods_get_default_format (void)
{
    return "type name | ready:5 | status:6 | restage:8 | ip:8 | node:4";
}
