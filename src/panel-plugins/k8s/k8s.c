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
const char k8s_exec_entry[] = "exec (enter to open shell)";
const char k8s_describe_entry[] = "describe";
const char k8s_yaml_entry[] = "yaml";
const char k8s_ns_all_entry[] = "all";

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
    g_free (item->restage);
    g_free (item->node_name);
    g_free (item->ready);
    g_free (item->ip);
    g_free (item->image);
    g_free (item->namespace);
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
    /* item_index keys borrow item->name. */
    data->index_for = NULL;
    if (data->item_index != NULL)
        g_hash_table_remove_all (data->item_index);
}

/* --------------------------------------------------------------------------------------------- */

const k8s_item_t *
k8s_find_item (const k8s_data_t *data, const char *name)
{
    k8s_data_t *d = (k8s_data_t *) data;

    if (data->items == NULL || name == NULL)
        return NULL;

    /* Rebuild after the items array is replaced. */
    if (d->item_index == NULL || d->index_for != data->items)
    {
        guint i;

        if (d->item_index == NULL)
            d->item_index = g_hash_table_new (g_str_hash, g_str_equal);
        else
            g_hash_table_remove_all (d->item_index);

        for (i = 0; i < data->items->len; i++)
        {
            k8s_item_t *item = (k8s_item_t *) g_ptr_array_index (data->items, i);

            if (!g_hash_table_contains (d->item_index, item->name))
                g_hash_table_insert (d->item_index, item->name, item);
        }
        d->index_for = data->items;
    }

    return (const k8s_item_t *) g_hash_table_lookup (d->item_index, name);
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
        data->title_buf = g_strdup_printf ("/%s", data->context != NULL ? data->context : "");
        break;
    case K8S_VIEW_RESOURCE_TYPES:
        data->title_buf = g_strdup_printf ("/%s/%s", data->context != NULL ? data->context : "",
                                           data->namespace != NULL ? data->namespace : "");
        break;
    case K8S_VIEW_PODS:
        data->title_buf =
            g_strdup_printf ("/%s/%s/pods", data->context != NULL ? data->context : "",
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
        data->title_buf = g_strdup_printf ("/%s/nodes", data->context != NULL ? data->context : "");
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

static k8s_item_t *
k8s_make_ns_item (const char *name, k8s_item_kind_t kind)
{
    k8s_item_t *item = g_new0 (k8s_item_t, 1);
    item->kind = kind;
    item->is_dir = TRUE;
    item->name = g_strdup (name);
    return item;
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
    GPtrArray *favs = NULL;
    gboolean use_favs;

    favs = k8s_ns_favs_load (data->context);
    use_favs = favs->len > 0 && !data->ns_show_all;

    data->items = g_ptr_array_new_with_free_func (k8s_item_free);

    if (use_favs)
    {
        /* Favs non-empty: only [all] + favs, nothing else. */
        g_ptr_array_add (data->items, k8s_make_ns_item (k8s_ns_all_entry, K8S_ITEM_NAMESPACE));
        for (i = 0; i < (int) favs->len; i++)
            g_ptr_array_add (data->items,
                             k8s_make_ns_item ((const char *) favs->pdata[i], K8S_ITEM_NAMESPACE));
        g_ptr_array_free (favs, TRUE);
        return TRUE;
    }
    g_ptr_array_free (favs, TRUE);

    /* Favs empty: full kubectl listing, nothing else. */
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

    if (output != NULL && output[0] != '\0')
    {
        lines = g_strsplit (output, "\n", -1);
        for (i = 0; lines[i] != NULL; i++)
        {
            g_strstrip (lines[i]);
            if (lines[i][0] == '\0')
                continue;
            g_ptr_array_add (data->items, k8s_make_ns_item (lines[i], K8S_ITEM_NAMESPACE));
        }
        g_strfreev (lines);
    }
    g_free (output);

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

/* Parse k8s URI segments out of open_path.
 * Accepts:
 *   "k8s:" / "k8s:/"                                 -> favorites
 *   "k8s:/<ctx>"                                     -> namespaces
 *   "k8s:/<ctx>/nodes"                               -> nodes
 *   "k8s:/<ctx>/<ns>"                                -> resource types
 *   "k8s:/<ctx>/<ns>/<type>"                         -> resource list
 *   "k8s:/<ctx>/<ns>/pods/<pod>"                     -> pod details
 * The plugin's prefix is "k8s:"; the leading slash that follows is optional. */
static void
k8s_parse_open_path (k8s_data_t *data, const char *open_path)
{
    const char *p;
    gchar **segs;
    guint n;

    if (open_path == NULL || open_path[0] == '\0')
        return;
    /* Only consume open_path when it really is a k8s URI. Activation from
       Manage Plugins or from a "switch to plugin" hook passes the panel's
       current filesystem cwd (e.g. /home/<user>) -- in that case there is
       no k8s state to restore, do nothing. */
    if (strncmp (open_path, "k8s:", 4) != 0)
        return;
    p = open_path + 4;
    while (*p == '/')
        p++;
    if (*p == '\0')
        return;

    segs = g_strsplit (p, "/", -1);
    n = g_strv_length (segs);

    if (n >= 1 && segs[0][0] != '\0')
    {
        g_free (data->context);
        data->context = g_strdup (segs[0]);
        data->view = K8S_VIEW_NAMESPACES;
    }
    if (n >= 2 && segs[1][0] != '\0')
    {
        if (strcmp (segs[1], "nodes") == 0 && n == 2)
            data->view = K8S_VIEW_NODES;
        else
        {
            g_free (data->namespace);
            data->namespace = g_strdup (segs[1]);
            data->view = K8S_VIEW_RESOURCE_TYPES;
        }
    }
    if (n >= 3 && segs[2][0] != '\0' && data->view == K8S_VIEW_RESOURCE_TYPES)
    {
        if (strcmp (segs[2], "pods") == 0)
            data->view = K8S_VIEW_PODS;
        else if (strcmp (segs[2], "deployments") == 0)
            data->view = K8S_VIEW_DEPLOYMENTS;
        else if (strcmp (segs[2], "services") == 0)
            data->view = K8S_VIEW_SERVICES;
    }
    if (n >= 4 && segs[3][0] != '\0' && data->view == K8S_VIEW_PODS)
    {
        g_free (data->selected_pod);
        data->selected_pod = g_strdup (segs[3]);
        /* URI carries a concrete namespace (segs[2] -> data->namespace). */
        g_free (data->selected_namespace);
        data->selected_namespace = g_strdup (data->namespace);
        data->view = K8S_VIEW_POD_DETAILS;
    }

    g_strfreev (segs);
}

/* --------------------------------------------------------------------------------------------- */

static void *
k8s_open (mc_panel_host_t *host, const char *open_path)
{
    k8s_data_t *data;

    data = g_new0 (k8s_data_t, 1);
    data->host = host;
    data->view = K8S_VIEW_FAVORITES;
    data->items = NULL;
    data->title_buf = NULL;
    {
        /* User-local install (~/.local/lib/mc/panel-plugins/k8s/) takes
           precedence over the compile-time system path so the help file
           can be iterated without sudo. */
        char *user_help = g_build_filename (g_get_home_dir (), ".local", "lib", "mc",
                                            "panel-plugins", "k8s", "k8s_panel.hlp", (char *) NULL);
        if (g_file_test (user_help, G_FILE_TEST_IS_REGULAR))
            data->help_filename = user_help;
        else
        {
            g_free (user_help);
            data->help_filename = g_build_filename (MC_PLUGIN_DIR, "k8s_panel.hlp", (char *) NULL);
        }
    }

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

    /* If the caller supplied a path (history pick, CLI argument, hotlist),
       override whatever was chosen above. */
    k8s_parse_open_path (data, open_path);

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
    g_free (data->selected_namespace);
    g_free (data->pending_focus);
    g_free (data->title_buf);
    g_free (data->help_filename);
    g_free (data->kubectl_cmd);
    g_free (data->kubeconfig);
    g_free (data->kubectl_full);
    if (data->fav_contexts != NULL)
        g_ptr_array_free (data->fav_contexts, TRUE);
    if (data->item_index != NULL)
        g_hash_table_destroy (data->item_index);
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
            const k8s_item_t *item = (const k8s_item_t *) g_ptr_array_index (data->items, i);
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
            if (data->ns_show_all)
            {
                data->ns_show_all = FALSE;
                k8s_set_pending_focus (data, k8s_ns_all_entry);
                k8s_clear_items (data);
                return MC_PPR_OK;
            }
            if (data->fav_contexts != NULL && data->fav_contexts->len > 0)
            {
                k8s_set_pending_focus (data, data->context);
                k8s_set_view (data, K8S_VIEW_FAVORITES);
                return MC_PPR_OK;
            }
            return MC_PPR_CLOSE;

        case K8S_VIEW_RESOURCE_TYPES:
            k8s_set_pending_focus (data, data->namespace);
            data->all_namespaces = FALSE;
            data->ns_show_all = FALSE;
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

        /* Virtual entry "all" -- show the full kubectl namespace list
           (bypass the favs filter) so the user can pick any namespace
           outside their favourites. */
        if (strcmp (path, k8s_ns_all_entry) == 0)
        {
            data->ns_show_all = TRUE;
            k8s_clear_items (data);
            k8s_reload_items (data);
            return MC_PPR_OK;
        }

        g_free (data->namespace);
        data->namespace = g_strdup (path);
        data->all_namespaces = FALSE;
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
        const k8s_item_t *item = k8s_find_item (data, path);

        if (item == NULL)
            return MC_PPR_FAILED;

        g_free (data->selected_pod);
        data->selected_pod = g_strdup (path);
        /* Details view replaces the pod list, so keep the namespace here. */
        g_free (data->selected_namespace);
        data->selected_namespace = g_strdup (item->namespace);
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
        const k8s_item_t *item = k8s_find_item (data, fname);
        char *prev_pod = data->selected_pod;
        char *prev_ns = data->selected_namespace;

        data->selected_pod = g_strdup (fname);
        data->selected_namespace = item != NULL ? g_strdup (item->namespace) : NULL;
        result = k8s_pods_view (data, k8s_logs_entry);
        g_free (data->selected_pod);
        g_free (data->selected_namespace);
        data->selected_pod = prev_pod;
        data->selected_namespace = prev_ns;
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
            cmd = g_strdup_printf ("%s version --context %s 2>&1", data->kubectl_full, quoted_ctx);
        else if (strcmp (fname, k8s_cluster_info_file) == 0)
            cmd = g_strdup_printf ("%s cluster-info --context %s 2>&1", data->kubectl_full,
                                   quoted_ctx);

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

    /* Namespaces view: F8 on a fav entry removes it from per-context favs;
       on a virtual entry ([all], [browse all], nodes) -- no-op; on a real
       namespace pulled from kubectl -- pass through (no kubectl delete). */
    if (data->view == K8S_VIEW_NAMESPACES && data->context != NULL)
    {
        GPtrArray *favs = k8s_ns_favs_load (data->context);
        gboolean changed = FALSE;
        int i;

        for (i = 0; i < count; i++)
        {
            guint j;
            if (names[i] == NULL || names[i][0] == '\0' || strcmp (names[i], k8s_ns_all_entry) == 0)
                continue;
            for (j = 0; j < favs->len; j++)
                if (strcmp ((const char *) favs->pdata[j], names[i]) == 0)
                {
                    g_ptr_array_remove_index (favs, j);
                    changed = TRUE;
                    break;
                }
        }
        if (changed)
        {
            k8s_ns_favs_save (data->context, favs);
            k8s_clear_items (data);
            g_ptr_array_free (favs, TRUE);
            return MC_PPR_OK;
        }
        g_ptr_array_free (favs, TRUE);
        return MC_PPR_NOT_SUPPORTED;
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
        /* Namespaces view: toggle favourite status for marked entries
           (or for the entry under cursor when nothing is marked). */
        if (data->view == K8S_VIEW_NAMESPACES && data->context != NULL && data->host != NULL)
        {
            GPtrArray *targets = g_ptr_array_new_with_free_func (g_free);
            int marked = (data->host->get_marked_count != NULL)
                ? data->host->get_marked_count (data->host)
                : 0;

            if (marked > 0 && data->host->get_next_marked != NULL)
            {
                int cur_idx = 0;
                const GString *m;
                while ((m = data->host->get_next_marked (data->host, &cur_idx)) != NULL)
                {
                    if (m->str != NULL && m->str[0] != '\0' && strcmp (m->str, "..") != 0
                        && strcmp (m->str, k8s_ns_all_entry) != 0)
                        g_ptr_array_add (targets, g_strdup (m->str));
                    cur_idx++; /* host->get_next_marked stops AT a match; caller advances */
                }
            }
            else if (data->host->get_current != NULL)
            {
                const GString *cur = data->host->get_current (data->host);
                if (cur != NULL && cur->str != NULL && cur->str[0] != '\0'
                    && strcmp (cur->str, "..") != 0 && strcmp (cur->str, k8s_ns_all_entry) != 0)
                    g_ptr_array_add (targets, g_strdup (cur->str));
            }

            if (targets->len > 0)
            {
                GPtrArray *favs = k8s_ns_favs_load (data->context);
                guint added = 0, removed = 0;
                guint t;

                for (t = 0; t < targets->len; t++)
                {
                    const char *name = (const char *) targets->pdata[t];
                    gboolean found = FALSE;
                    guint j;

                    for (j = 0; j < favs->len; j++)
                        if (strcmp ((const char *) favs->pdata[j], name) == 0)
                        {
                            found = TRUE;
                            break;
                        }
                    if (found)
                    {
                        g_ptr_array_remove_index (favs, j);
                        removed++;
                    }
                    else
                    {
                        g_ptr_array_add (favs, g_strdup (name));
                        added++;
                    }
                }
                k8s_ns_favs_save (data->context, favs);
                if (data->host->set_hint != NULL)
                {
                    char hint[128];
                    g_snprintf (hint, sizeof (hint),
                                "%u added, %u removed from namespace favorites.", added, removed);
                    data->host->set_hint (data->host, hint);
                }
                k8s_clear_items (data);
                g_ptr_array_free (favs, TRUE);
                g_ptr_array_free (targets, TRUE);
                return MC_PPR_OK;
            }
            g_ptr_array_free (targets, TRUE);
        }

        /* Default: add the active context to favourites. */
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

/* Image tail for the footer: prefer the tag (after ':'); cap at 21 chars,
   prefixing with '~' when truncated. */
static char *
k8s_image_tail (const char *image)
{
    size_t len;
    const size_t max = 30;

    if (image == NULL || image[0] == '\0')
        return NULL;
    len = strlen (image);
    if (len <= max)
        return g_strdup (image);
    return g_strdup_printf ("~%s", image + len - (max - 1));
}

/* --------------------------------------------------------------------------------------------- */

static const char *
k8s_get_footer (void *plugin_data)
{
    k8s_data_t *data = (k8s_data_t *) plugin_data;

    if (data->view == K8S_VIEW_FAVORITES)
        return NULL;

    g_free (data->title_buf);
    data->title_buf = NULL;

    if (data->view == K8S_VIEW_PODS)
    {
        if (data->host != NULL && data->host->get_current != NULL)
        {
            const GString *cur = data->host->get_current (data->host);
            if (cur != NULL && cur->str != NULL)
            {
                const k8s_item_t *item = k8s_find_item (data, cur->str);
                if (item != NULL && item->image != NULL && item->image[0] != '\0')
                    data->title_buf = k8s_image_tail (item->image);
            }
        }
        return data->title_buf;
    }

    if (data->context != NULL && data->namespace != NULL)
        data->title_buf = g_strdup_printf ("%s / %s", data->context, data->namespace);
    else if (data->context != NULL)
        data->title_buf = g_strdup (data->context);
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
