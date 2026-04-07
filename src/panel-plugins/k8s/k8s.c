/*
   Kubernetes resources panel plugin.

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
#include <sys/stat.h>
#include <time.h>

#include "lib/global.h"
#include "lib/widget.h"
#include "lib/tty/key.h"

#include "src/panel-plugins/k8s/k8s-internal.h"

/*** forward declarations (file scope functions) *************************************************/

static void *k8s_open (mc_panel_host_t *host, const char *open_path);
static void k8s_close (void *plugin_data);
static mc_pp_result_t k8s_get_items (void *plugin_data, void *list_ptr);
static mc_pp_result_t k8s_chdir (void *plugin_data, const char *path);
static mc_pp_result_t k8s_enter (void *plugin_data, const char *name, const struct stat *st);
static mc_pp_result_t k8s_view (void *plugin_data, const char *fname, const struct stat *st,
                                gboolean plain_view);
static mc_pp_result_t k8s_get_help_info (void *plugin_data, const char **filename,
                                         const char **node);
static mc_pp_result_t k8s_delete_items (void *plugin_data, const char **names, int count);
static const char *k8s_get_title (void *plugin_data);
static mc_pp_result_t k8s_handle_key (void *plugin_data, int key);
static mc_pp_result_t k8s_create_item (void *plugin_data);
static const mc_panel_column_t *k8s_get_columns (void *plugin_data, size_t *count);
static const char *k8s_get_column_value (void *plugin_data, const char *fname,
                                         const char *column_id);
static const char *k8s_get_footer (void *plugin_data);
static const char *k8s_get_focus_name (void *plugin_data);
static const char *k8s_get_default_format (void *plugin_data);

/*** file scope variables ************************************************************************/

const char k8s_version_file[] = "version.txt";
const char k8s_cluster_info_file[] = "cluster-info.txt";
const char k8s_logs_entry[] = "logs";
const char k8s_exec_entry[] = "exec";
const char k8s_describe_entry[] = "describe";
const char k8s_yaml_entry[] = "yaml";

static const mc_panel_plugin_t k8s_plugin = {
    .api_version = MC_PANEL_PLUGIN_API_VERSION,
    .name = "k8s",
    .display_name = "Kubernetes",
    .proto = "k8s",
    .prefix = "k8s:",
    .flags = MC_PPF_NAVIGATE | MC_PPF_DELETE | MC_PPF_CUSTOM_TITLE | MC_PPF_CREATE,

    .open = k8s_open,
    .close = k8s_close,
    .get_items = k8s_get_items,

    .chdir = k8s_chdir,
    .enter = k8s_enter,
    .view = k8s_view,
    .get_help_info = k8s_get_help_info,
    .get_local_copy = NULL,
    .put_file = NULL,
    .save_file = NULL,
    .delete_items = k8s_delete_items,
    .get_title = k8s_get_title,
    .handle_key = k8s_handle_key,
    .create_item = k8s_create_item,

    .get_columns = k8s_get_columns,
    .get_column_value = k8s_get_column_value,
    .get_footer = k8s_get_footer,
    .get_focus_name = k8s_get_focus_name,
    .get_default_format = k8s_get_default_format,
};

/*** file scope functions ************************************************************************/

void
k8s_item_free (gpointer p)
{
    k8s_item_t *item = (k8s_item_t *) p;

    if (item == NULL)
        return;
    g_free (item->name);
    g_free (item->status);
    g_free (item->restarts);
    g_free (item->age);
    g_free (item->node_name);
    g_free (item->ready);
    g_free (item->svc_type);
    g_free (item->cluster_ip);
    g_free (item->ports);
    g_free (item->roles);
    g_free (item->version);
    g_free (item);
}

/* --------------------------------------------------------------------------------------------- */

void
k8s_clear_items (k8s_data_t *data)
{
    if (data->items != NULL)
    {
        g_ptr_array_free (data->items, TRUE);
        data->items = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

const k8s_item_t *
k8s_find_item (const k8s_data_t *data, const char *name)
{
    guint i;

    if (data->items == NULL || name == NULL)
        return NULL;

    for (i = 0; i < data->items->len; i++)
    {
        const k8s_item_t *item = (const k8s_item_t *) g_ptr_array_index (data->items, i);

        if (strcmp (item->name, name) == 0)
            return item;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
k8s_set_pending_focus (k8s_data_t *data, const char *name)
{
    g_free (data->pending_focus);
    data->pending_focus = (name != NULL) ? g_strdup (name) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
k8s_get_path (k8s_data_t *data)
{
    g_free (data->title_buf);

    switch (data->view)
    {
    case K8S_VIEW_FAVORITES:
        data->title_buf = g_strdup ("/");
        break;
    case K8S_VIEW_NAMESPACES:
        data->title_buf =
            g_strdup_printf ("/%s", data->context != NULL ? data->context : "");
        break;
    case K8S_VIEW_RESOURCE_TYPES:
        data->title_buf = g_strdup_printf ("/%s/%s",
                                           data->context != NULL ? data->context : "",
                                           data->namespace != NULL ? data->namespace : "");
        break;
    case K8S_VIEW_PODS:
        data->title_buf = g_strdup_printf ("/%s/%s/pods",
                                           data->context != NULL ? data->context : "",
                                           data->namespace != NULL ? data->namespace : "");
        break;
    case K8S_VIEW_POD_DETAILS:
        data->title_buf =
            g_strdup_printf ("/%s/%s/pods/%s", data->context != NULL ? data->context : "",
                             data->namespace != NULL ? data->namespace : "",
                             data->selected_pod != NULL ? data->selected_pod : "");
        break;
    case K8S_VIEW_DEPLOYMENTS:
        data->title_buf =
            g_strdup_printf ("/%s/%s/deployments", data->context != NULL ? data->context : "",
                             data->namespace != NULL ? data->namespace : "");
        break;
    case K8S_VIEW_SERVICES:
        data->title_buf =
            g_strdup_printf ("/%s/%s/services", data->context != NULL ? data->context : "",
                             data->namespace != NULL ? data->namespace : "");
        break;
    case K8S_VIEW_NODES:
        data->title_buf =
            g_strdup_printf ("/%s/nodes", data->context != NULL ? data->context : "");
        break;
    default:
        data->title_buf = g_strdup ("/");
        break;
    }

    return data->title_buf;
}

/* --------------------------------------------------------------------------------------------- */

static void
k8s_set_view (k8s_data_t *data, k8s_view_t new_view)
{
    data->view = new_view;
    k8s_clear_items (data);

    if (data->host != NULL && data->host->add_history != NULL)
    {
        const char *path = k8s_get_path (data);
        if (path != NULL && k8s_plugin.proto != NULL)
        {
            char *plugin_path = g_strdup_printf ("%s:%s", k8s_plugin.proto, path);
            data->host->add_history (data->host, plugin_path);
            g_free (plugin_path);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
k8s_reload_namespaces (k8s_data_t *data, char **err_text)
{
    char *quoted_ctx;
    char *cmd;
    char *output;
    char **lines;
    int i;
    k8s_item_t *nodes_item;

    quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");
    cmd = g_strdup_printf ("%s get namespaces --context %s"
                           " -o custom-columns=NAME:.metadata.name --no-headers",
                           data->kubectl_full, quoted_ctx);
    g_free (quoted_ctx);

    if (!k8s_run_cmd (cmd, &output, err_text))
    {
        g_free (cmd);
        return FALSE;
    }
    g_free (cmd);

    data->items = g_ptr_array_new_with_free_func (k8s_item_free);

    if (output != NULL && output[0] != '\0')
    {
        lines = g_strsplit (output, "\n", -1);
        for (i = 0; lines[i] != NULL; i++)
        {
            k8s_item_t *item;

            g_strstrip (lines[i]);
            if (lines[i][0] == '\0')
                continue;

            item = g_new0 (k8s_item_t, 1);
            item->kind = K8S_ITEM_NAMESPACE;
            item->is_dir = TRUE;
            item->name = g_strdup (lines[i]);
            g_ptr_array_add (data->items, item);
        }
        g_strfreev (lines);
    }
    g_free (output);

    /* Add "nodes" cluster-wide dir */
    nodes_item = g_new0 (k8s_item_t, 1);
    nodes_item->kind = K8S_ITEM_RESOURCE_TYPE_DIR;
    nodes_item->is_dir = TRUE;
    nodes_item->name = g_strdup ("nodes");
    g_ptr_array_add (data->items, nodes_item);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
k8s_reload_resource_types (k8s_data_t *data)
{
    static const char *types[] = { "pods", "deployments", "services" };
    int i;

    data->items = g_ptr_array_new_with_free_func (k8s_item_free);

    for (i = 0; i < 3; i++)
    {
        k8s_item_t *item;

        item = g_new0 (k8s_item_t, 1);
        item->kind = K8S_ITEM_RESOURCE_TYPE_DIR;
        item->is_dir = TRUE;
        item->name = g_strdup (types[i]);
        g_ptr_array_add (data->items, item);
    }

    {
        k8s_item_t *item;

        item = g_new0 (k8s_item_t, 1);
        item->kind = K8S_ITEM_INFO_FILE;
        item->is_dir = FALSE;
        item->name = g_strdup (k8s_version_file);
        g_ptr_array_add (data->items, item);
    }
    {
        k8s_item_t *item;

        item = g_new0 (k8s_item_t, 1);
        item->kind = K8S_ITEM_INFO_FILE;
        item->is_dir = FALSE;
        item->name = g_strdup (k8s_cluster_info_file);
        g_ptr_array_add (data->items, item);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
k8s_reload_items (k8s_data_t *data)
{
    char *err_text = NULL;
    gboolean ok = TRUE;

    k8s_clear_items (data);

    switch (data->view)
    {
    case K8S_VIEW_FAVORITES:
        return TRUE; /* built in get_items */

    case K8S_VIEW_NAMESPACES:
        ok = k8s_reload_namespaces (data, &err_text);
        break;

    case K8S_VIEW_RESOURCE_TYPES:
        k8s_reload_resource_types (data);
        return TRUE;

    case K8S_VIEW_PODS:
        ok = k8s_pods_reload (data, &err_text);
        break;

    case K8S_VIEW_POD_DETAILS:
        k8s_pods_reload_details (data);
        return TRUE;

    case K8S_VIEW_DEPLOYMENTS:
        ok = k8s_deployments_reload (data, &err_text);
        break;

    case K8S_VIEW_SERVICES:
        ok = k8s_services_reload (data, &err_text);
        break;

    case K8S_VIEW_NODES:
        ok = k8s_nodes_reload (data, &err_text);
        break;

    default:
        data->items = g_ptr_array_new_with_free_func (k8s_item_free);
        return TRUE;
    }

    if (!ok)
    {
        if (err_text != NULL && err_text[0] != '\0')
            message (D_ERROR, MSG_ERROR, "%s", err_text);
        g_free (err_text);
        data->items = g_ptr_array_new_with_free_func (k8s_item_free);
        return FALSE;
    }

    g_free (err_text);

    if (data->items == NULL)
        data->items = g_ptr_array_new_with_free_func (k8s_item_free);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/* Plugin callbacks */
/* --------------------------------------------------------------------------------------------- */

static void *
k8s_open (mc_panel_host_t *host, const char *open_path)
{
    k8s_data_t *data;

    (void) open_path; /* TODO: path restoration */

    data = g_new0 (k8s_data_t, 1);
    data->host = host;
    data->view = K8S_VIEW_FAVORITES;
    data->items = NULL;
    data->title_buf = NULL;
    data->help_filename = g_build_filename (MC_PLUGIN_DIR, "k8s_panel.hlp", (char *) NULL);
    data->cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, k8s_cache_entry_free);

    data->kubectl_cmd = k8s_load_kubectl_cmd ();
    data->kubeconfig = k8s_load_kubeconfig ();
    data->kubectl_full = k8s_build_kubectl (data);
    data->key_refresh =
        k8s_load_hotkey (K8S_PANEL_KEY_REFRESH, K8S_PANEL_KEY_REFRESH_DEFAULT, XCTRL ('r'));
    data->key_fav_add =
        k8s_load_hotkey (K8S_PANEL_KEY_FAV_ADD, K8S_PANEL_KEY_FAV_ADD_DEFAULT, XCTRL ('b'));
    data->key_ns_switch =
        k8s_load_hotkey (K8S_PANEL_KEY_NS_SWITCH, K8S_PANEL_KEY_NS_SWITCH_DEFAULT, XCTRL ('n'));

    data->fav_contexts = k8s_favorites_load ();

    if (data->fav_contexts->len == 0)
    {
        /* No favorites -- show current context's namespaces directly */
            data->context = k8s_get_current_context (data->kubectl_full);
        if (data->context != NULL)
        {
            data->namespace = g_strdup ("default");
            data->view = K8S_VIEW_NAMESPACES;
        }
    }

    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void
k8s_close (void *plugin_data)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    k8s_clear_items (data);
    g_free (data->context);
    g_free (data->namespace);
    g_free (data->selected_pod);
    g_free (data->pending_focus);
    g_free (data->title_buf);
    g_free (data->help_filename);
    g_free (data->kubectl_cmd);
    g_free (data->kubeconfig);
    g_free (data->kubectl_full);
    if (data->fav_contexts != NULL)
        g_ptr_array_free (data->fav_contexts, TRUE);
    if (data->cache != NULL)
        g_hash_table_destroy (data->cache);
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
k8s_get_items (void *plugin_data, void *list_ptr)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;
    guint i;

    if (data->view == K8S_VIEW_FAVORITES)
    {
        if (data->fav_contexts != NULL)
            for (i = 0; i < data->fav_contexts->len; i++)
            {
                const char *ctx = (const char *) data->fav_contexts->pdata[i];
                mc_pp_add_entry (list_ptr, ctx, S_IFDIR | 0755, 0, time (NULL));
            }
        return MC_PPR_OK;
    }

    if (data->items == NULL)
        k8s_reload_items (data);

    if (data->items != NULL)
        for (i = 0; i < data->items->len; i++)
        {
            const k8s_item_t *item =
                (const k8s_item_t *) g_ptr_array_index (data->items, i);
            mode_t mode = item->is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);

            mc_pp_add_entry (list_ptr, item->name, mode, 0, time (NULL));
        }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
k8s_chdir (void *plugin_data, const char *path)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    if (strcmp (path, "..") == 0)
    {
        switch (data->view)
        {
        case K8S_VIEW_FAVORITES:
            return MC_PPR_CLOSE;

        case K8S_VIEW_NAMESPACES:
            if (data->fav_contexts != NULL && data->fav_contexts->len > 0)
            {
                k8s_set_pending_focus (data, data->context);
                k8s_set_view (data, K8S_VIEW_FAVORITES);
                return MC_PPR_OK;
            }
            return MC_PPR_CLOSE;

        case K8S_VIEW_RESOURCE_TYPES:
            k8s_set_pending_focus (data, data->namespace);
            k8s_set_view (data, K8S_VIEW_NAMESPACES);
            k8s_reload_items (data);
            return MC_PPR_OK;

        case K8S_VIEW_PODS:
            k8s_set_pending_focus (data, "pods");
            k8s_set_view (data, K8S_VIEW_RESOURCE_TYPES);
            k8s_reload_items (data);
            return MC_PPR_OK;

        case K8S_VIEW_DEPLOYMENTS:
            k8s_set_pending_focus (data, "deployments");
            k8s_set_view (data, K8S_VIEW_RESOURCE_TYPES);
            k8s_reload_items (data);
            return MC_PPR_OK;

        case K8S_VIEW_SERVICES:
            k8s_set_pending_focus (data, "services");
            k8s_set_view (data, K8S_VIEW_RESOURCE_TYPES);
            k8s_reload_items (data);
            return MC_PPR_OK;

        case K8S_VIEW_POD_DETAILS:
            k8s_set_pending_focus (data, data->selected_pod);
            k8s_set_view (data, K8S_VIEW_PODS);
            k8s_reload_items (data);
            return MC_PPR_OK;

        case K8S_VIEW_NODES:
            k8s_set_pending_focus (data, "nodes");
            k8s_set_view (data, K8S_VIEW_NAMESPACES);
            k8s_reload_items (data);
            return MC_PPR_OK;

        default:
            k8s_set_view (data, K8S_VIEW_FAVORITES);
            return MC_PPR_OK;
        }
    }

    if (data->view == K8S_VIEW_FAVORITES)
    {
        g_free (data->context);
        data->context = g_strdup (path);
        g_free (data->namespace);
        data->namespace = g_strdup ("default");
        k8s_set_view (data, K8S_VIEW_NAMESPACES);
        k8s_reload_items (data);
        return MC_PPR_OK;
    }

    if (data->view == K8S_VIEW_NAMESPACES)
    {
        const k8s_item_t *item = k8s_find_item (data, path);

        if (item == NULL)
            return MC_PPR_FAILED;

        if (item->kind == K8S_ITEM_RESOURCE_TYPE_DIR)
        {
            k8s_set_view (data, K8S_VIEW_NODES);
            k8s_reload_items (data);
            return MC_PPR_OK;
        }

        g_free (data->namespace);
        data->namespace = g_strdup (path);
        k8s_set_view (data, K8S_VIEW_RESOURCE_TYPES);
        k8s_reload_items (data);
        return MC_PPR_OK;
    }

    if (data->view == K8S_VIEW_RESOURCE_TYPES)
    {
        if (strcmp (path, "pods") == 0)
            k8s_set_view (data, K8S_VIEW_PODS);
        else if (strcmp (path, "deployments") == 0)
            k8s_set_view (data, K8S_VIEW_DEPLOYMENTS);
        else if (strcmp (path, "services") == 0)
            k8s_set_view (data, K8S_VIEW_SERVICES);
        else
            return MC_PPR_FAILED;

        k8s_reload_items (data);
        return MC_PPR_OK;
    }

    if (data->view == K8S_VIEW_PODS)
    {
        if (k8s_find_item (data, path) == NULL)
            return MC_PPR_FAILED;

        g_free (data->selected_pod);
        data->selected_pod = g_strdup (path);
        k8s_set_view (data, K8S_VIEW_POD_DETAILS);
        k8s_reload_items (data);
        return MC_PPR_OK;
    }

    return MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
k8s_enter (void *plugin_data, const char *name, const struct stat *st)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    (void) st;

    if (data->view == K8S_VIEW_FAVORITES || data->view == K8S_VIEW_NAMESPACES
        || data->view == K8S_VIEW_RESOURCE_TYPES || data->view == K8S_VIEW_PODS)
        return k8s_chdir (plugin_data, name);

    if (data->view == K8S_VIEW_POD_DETAILS)
    {
        if (k8s_find_item (data, name) == NULL)
            return MC_PPR_FAILED;

        if (strcmp (name, k8s_exec_entry) == 0)
            return k8s_pods_exec (data);

        return MC_PPR_NOT_SUPPORTED;
    }

    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
k8s_view (void *plugin_data, const char *fname, const struct stat *st, gboolean plain_view)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    (void) st;
    (void) plain_view;

    if (fname == NULL)
        return MC_PPR_NOT_SUPPORTED;

    if (data->view == K8S_VIEW_POD_DETAILS)
        return k8s_pods_view (data, fname);

    if (data->view == K8S_VIEW_PODS)
    {
        mc_pp_result_t result;
        char *prev_pod;

        prev_pod = data->selected_pod;
        data->selected_pod = g_strdup (fname);
        result = k8s_pods_view (data, k8s_logs_entry);
        g_free (data->selected_pod);
        data->selected_pod = prev_pod;
        return result;
    }

    if (data->view == K8S_VIEW_DEPLOYMENTS)
        return k8s_deployments_view (data, fname);

    if (data->view == K8S_VIEW_SERVICES)
        return k8s_services_view (data, fname);

    if (data->view == K8S_VIEW_RESOURCE_TYPES)
    {
        char *quoted_ctx;
        char *cmd = NULL;

        quoted_ctx = g_shell_quote (data->context != NULL ? data->context : "");

        if (strcmp (fname, k8s_version_file) == 0)
            cmd = g_strdup_printf ("%s version --context %s", data->kubectl_full, quoted_ctx);
        else if (strcmp (fname, k8s_cluster_info_file) == 0)
            cmd = g_strdup_printf ("%s cluster-info --context %s",
                                   data->kubectl_full, quoted_ctx);

        g_free (quoted_ctx);

        if (cmd == NULL)
            return MC_PPR_NOT_SUPPORTED;

        k8s_ui_view_command (cmd);
        g_free (cmd);
        return MC_PPR_OK;
    }

    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
k8s_get_help_info (void *plugin_data, const char **filename, const char **node)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    if (filename != NULL)
        *filename = data->help_filename;
    if (node != NULL)
        *node = "Kubernetes Plugin";
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
k8s_delete_items (void *plugin_data, const char **names, int count)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    if (data->view == K8S_VIEW_FAVORITES)
    {
        int i;

        for (i = 0; i < count; i++)
        {
            guint j;

            for (j = 0; j < data->fav_contexts->len; j++)
                if (strcmp ((const char *) data->fav_contexts->pdata[j], names[i]) == 0)
                {
                    g_ptr_array_remove_index (data->fav_contexts, j);
                    break;
                }
        }
        k8s_favorites_save (data->fav_contexts);
        return MC_PPR_OK;
    }

    if (data->view == K8S_VIEW_PODS)
        return k8s_pods_delete (data, names, count);

    if (data->view == K8S_VIEW_DEPLOYMENTS)
        return k8s_deployments_delete (data, names, count);

    if (data->view == K8S_VIEW_SERVICES)
        return k8s_services_delete (data, names, count);

    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
k8s_get_title (void *plugin_data)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;
    return k8s_get_path (data);
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
k8s_handle_key (void *plugin_data, int key)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    if (data->key_refresh != 0 && key == data->key_refresh)
    {
        k8s_clear_items (data);
        return MC_PPR_OK;
    }

    if (data->key_fav_add != 0 && key == data->key_fav_add)
    {
        const char *ctx = data->context;
        char *detected = NULL;
        guint i;
        gboolean found = FALSE;

        if (ctx == NULL)
        {
            detected = k8s_get_current_context (data->kubectl_full);
            ctx = detected;
        }

        if (ctx == NULL)
        {
            g_free (detected);
            return MC_PPR_FAILED;
        }

        for (i = 0; i < data->fav_contexts->len; i++)
            if (strcmp ((const char *) data->fav_contexts->pdata[i], ctx) == 0)
            {
                found = TRUE;
                break;
            }

        if (found)
        {
            if (data->host != NULL && data->host->message != NULL)
                data->host->message (data->host, D_NORMAL, "Kubernetes",
                                     "Context is already in favorites.");
        }
        else
        {
            g_ptr_array_add (data->fav_contexts, g_strdup (ctx));
            k8s_favorites_save (data->fav_contexts);
            if (data->host != NULL && data->host->set_hint != NULL)
                data->host->set_hint (data->host, "Context added to favorites.");
        }
        g_free (detected);
        return MC_PPR_OK;
    }

    if (data->view == K8S_VIEW_FAVORITES)
        return MC_PPR_FAILED;

    if (data->key_ns_switch != 0 && key == data->key_ns_switch)
    {
        (void) k8s_ui_show_ns_switch_dialog (data);
        return MC_PPR_OK;
    }

    return MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
k8s_create_item (void *plugin_data)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;
    char *ctx = NULL;
    guint i;

    if (data->view != K8S_VIEW_FAVORITES)
        return MC_PPR_NOT_SUPPORTED;

    if (!k8s_ui_show_add_context_dialog (&ctx))
        return MC_PPR_FAILED;

    for (i = 0; i < data->fav_contexts->len; i++)
        if (strcmp ((const char *) data->fav_contexts->pdata[i], ctx) == 0)
        {
            g_free (ctx);
            if (data->host != NULL && data->host->message != NULL)
                data->host->message (data->host, D_NORMAL, "Kubernetes",
                                     "Context is already in favorites.");
            return MC_PPR_FAILED;
        }

    g_ptr_array_add (data->fav_contexts, ctx);
    k8s_favorites_save (data->fav_contexts);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static const mc_panel_column_t *
k8s_get_columns (void *plugin_data, size_t *count)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    if (data->view == K8S_VIEW_PODS)
        return k8s_pods_get_columns (count);
    if (data->view == K8S_VIEW_DEPLOYMENTS)
        return k8s_deployments_get_columns (count);
    if (data->view == K8S_VIEW_SERVICES)
        return k8s_services_get_columns (count);
    if (data->view == K8S_VIEW_NODES)
        return k8s_nodes_get_columns (count);

    if (count != NULL)
        *count = 0;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
k8s_get_column_value (void *plugin_data, const char *fname, const char *column_id)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    if (fname == NULL || column_id == NULL)
        return NULL;

    if (data->view == K8S_VIEW_PODS)
        return k8s_pods_get_column_value (data, fname, column_id);
    if (data->view == K8S_VIEW_DEPLOYMENTS)
        return k8s_deployments_get_column_value (data, fname, column_id);
    if (data->view == K8S_VIEW_SERVICES)
        return k8s_services_get_column_value (data, fname, column_id);
    if (data->view == K8S_VIEW_NODES)
        return k8s_nodes_get_column_value (data, fname, column_id);

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
k8s_get_footer (void *plugin_data)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    if (data->view == K8S_VIEW_FAVORITES)
        return NULL;

    g_free (data->title_buf);

    if (data->context != NULL && data->namespace != NULL)
        data->title_buf = g_strdup_printf ("%s / %s", data->context, data->namespace);
    else if (data->context != NULL)
        data->title_buf = g_strdup (data->context);
    else
        data->title_buf = NULL;

    return data->title_buf;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
k8s_get_focus_name (void *plugin_data)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;
    return data->pending_focus;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
k8s_get_default_format (void *plugin_data)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    if (data->view == K8S_VIEW_PODS)
        return k8s_pods_get_default_format ();
    if (data->view == K8S_VIEW_DEPLOYMENTS)
        return k8s_deployments_get_default_format ();
    if (data->view == K8S_VIEW_SERVICES)
        return k8s_services_get_default_format ();
    if (data->view == K8S_VIEW_NODES)
        return k8s_nodes_get_default_format ();

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *mc_panel_plugin_register (void);

const mc_panel_plugin_t *
mc_panel_plugin_register (void)
{
    return &k8s_plugin;
}

/* --------------------------------------------------------------------------------------------- */
