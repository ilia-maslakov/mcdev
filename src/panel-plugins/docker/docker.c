/*
   Docker resources panel plugin.

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
#include <unistd.h>

#include "src/panel-plugins/docker/docker-internal.h"
#include "lib/mcconfig.h"
#include "src/viewer/mcviewer.h"

/*** forward declarations (file scope functions) *************************************************/

static void *docker_open (mc_panel_host_t *host, const char *open_path);
static void docker_close (void *plugin_data);
static mc_pp_result_t docker_get_help_info (void *plugin_data, const char **filename,
                                            const char **node);
static mc_pp_result_t docker_get_items (void *plugin_data, void *list_ptr);
static mc_pp_result_t docker_chdir (void *plugin_data, const char *path);
static mc_pp_result_t docker_enter (void *plugin_data, const char *name, const struct stat *st);
static mc_pp_result_t docker_view (void *plugin_data, const char *fname, const struct stat *st,
                                   gboolean plain_view);
static mc_pp_result_t docker_get_local_copy (void *plugin_data, const char *fname,
                                             char **local_path);
static mc_pp_result_t docker_save_file (void *plugin_data, const char *local_path,
                                        const char *remote_name);
static mc_pp_result_t docker_delete_items (void *plugin_data, const char **names, int count);
static const char *docker_get_title (void *plugin_data);
static mc_pp_result_t docker_handle_key (void *plugin_data, int key);
static mc_pp_result_t docker_create_item (void *plugin_data);
static const mc_panel_column_t *docker_get_columns (void *plugin_data, size_t *count);
static const char *docker_get_column_value (void *plugin_data, const char *fname,
                                            const char *column_id);
static const char *docker_get_default_format (void *plugin_data);
static gboolean docker_apply_open_path (docker_data_t *data, const char *open_path);
static char *docker_normalize_open_path (const char *open_path);
static const char *docker_get_focus_name (void *plugin_data);

/*** file scope variables ************************************************************************/

const char docker_daemon_info_file[] = "daemon-info.txt";
const char docker_version_file[] = "version.txt";
const char docker_inspect_file[] = "inspect.json";
const char docker_exec_entry[] = "exec";
const char docker_files_entry[] = "files";
const char docker_logs_entry[] = "logs";
const char docker_mounts_entry[] = "mounts";
const char docker_ungrouped_project[] = "ungrouped";

static const mc_panel_plugin_t docker_plugin = {
    .api_version = MC_PANEL_PLUGIN_API_VERSION,
    .name = "docker",
    .display_name = "Docker",
    .proto = "docker",
    .prefix = "docker:",
    .flags =
        MC_PPF_NAVIGATE | MC_PPF_GET_FILES | MC_PPF_DELETE | MC_PPF_CUSTOM_TITLE | MC_PPF_CREATE,

    .open = docker_open,
    .close = docker_close,
    .get_items = docker_get_items,

    .chdir = docker_chdir,
    .enter = docker_enter,
    .view = docker_view,
    .get_local_copy = docker_get_local_copy,
    .put_file = NULL,
    .save_file = docker_save_file,
    .delete_items = docker_delete_items,
    .get_title = docker_get_title,
    .handle_key = docker_handle_key,
    .create_item = docker_create_item,
    .get_help_info = docker_get_help_info,

    .get_columns = docker_get_columns,
    .get_column_value = docker_get_column_value,
    .get_default_format = docker_get_default_format,
    .get_focus_name = docker_get_focus_name,
};

/*** file scope functions ************************************************************************/

void
docker_item_free (gpointer p)
{
    docker_item_t *item = (docker_item_t *) p;

    g_free (item->name);
    g_free (item->id);
    g_free (item->link_target);
    g_free (item->status);
    g_free (item->image);
    g_free (item->ports);
    g_free (item->driver);
    g_free (item->scope);
    g_free (item->created);
    g_free (item);
}

/* --------------------------------------------------------------------------------------------- */

docker_item_t *
docker_item_clone (const docker_item_t *item)
{
    docker_item_t *copy;

    copy = g_new0 (docker_item_t, 1);
    copy->name = g_strdup (item->name);
    copy->id = g_strdup (item->id);
    copy->is_dir = item->is_dir;
    copy->is_link = item->is_link;
    copy->size = item->size;
    copy->link_target = g_strdup (item->link_target);
    copy->status = g_strdup (item->status);
    copy->image = g_strdup (item->image);
    copy->ports = g_strdup (item->ports);
    copy->driver = g_strdup (item->driver);
    copy->scope = g_strdup (item->scope);
    copy->created = g_strdup (item->created);

    return copy;
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
docker_items_clone (const GPtrArray *items)
{
    GPtrArray *copy;
    guint i;

    copy = g_ptr_array_new_with_free_func (docker_item_free);

    if (items == NULL)
        return copy;

    for (i = 0; i < items->len; i++)
    {
        const docker_item_t *item =
            (const docker_item_t *) g_ptr_array_index ((GPtrArray *) items, i);

        g_ptr_array_add (copy, docker_item_clone (item));
    }

    return copy;
}

/* --------------------------------------------------------------------------------------------- */
/*** Navigation back-cache *********************************************************************/
/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    docker_view_t view;
    GPtrArray *items;                 /* NULL for PROFILES and ROOT views */
    char *focus_name;                 /* item to focus when returning here */
    docker_connection_t *active_conn; /* non-owned pointer into data->connections */
    char *current_project;
    char *current_container_id;
    char *current_container_name;
    char *files_cwd;
} docker_nav_frame_t;

/* --------------------------------------------------------------------------------------------- */

static void
nav_frame_clear (docker_nav_frame_t *frame)
{
    if (frame->items != NULL)
        g_ptr_array_free (frame->items, TRUE);
    g_free (frame->focus_name);
    g_free (frame->current_project);
    g_free (frame->current_container_id);
    g_free (frame->current_container_name);
    g_free (frame->files_cwd);
}

/* --------------------------------------------------------------------------------------------- */

/* Save the current view state and transfer item ownership to the stack. */
static void
nav_stack_push (docker_data_t *data, const char *focus_name)
{
    docker_nav_frame_t frame;

    g_free (data->pending_focus);
    data->pending_focus = NULL;

    frame.view = data->view;
    frame.items = data->items; /* transfer ownership */
    frame.focus_name = g_strdup (focus_name);
    frame.active_conn = data->active_conn;
    frame.current_project = g_strdup (data->current_project);
    frame.current_container_id = g_strdup (data->current_container_id);
    frame.current_container_name = g_strdup (data->current_container_name);
    frame.files_cwd = g_strdup (data->files_cwd);

    data->items = NULL; /* transferred; prevents double-free in clear_items */

    g_array_append_val (data->nav_stack, frame);
}

/* --------------------------------------------------------------------------------------------- */

/* Restore the most recent saved view state. */
static gboolean
nav_stack_pop (docker_data_t *data)
{
    docker_nav_frame_t *frame;

    if (data->nav_stack->len == 0)
        return FALSE;

    frame = &g_array_index (data->nav_stack, docker_nav_frame_t, data->nav_stack->len - 1);

    /* Restore all saved context */
    data->active_conn = frame->active_conn;

    g_free (data->current_project);
    data->current_project = frame->current_project;
    frame->current_project = NULL;

    g_free (data->current_container_id);
    data->current_container_id = frame->current_container_id;
    frame->current_container_id = NULL;

    g_free (data->current_container_name);
    data->current_container_name = frame->current_container_name;
    frame->current_container_name = NULL;

    g_free (data->files_cwd);
    data->files_cwd = frame->files_cwd;
    frame->files_cwd = NULL;

    /* Save items to the appropriate cache before they are replaced. */
    if (data->view == DOCKER_VIEW_CONTAINERS_PROJECTS && data->items != NULL)
    {
        if (data->projects_cache != NULL)
            g_ptr_array_free (data->projects_cache, TRUE);
        data->projects_cache = data->items;
        data->items = NULL;
    }
    else if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS && data->items != NULL)
    {
        if (data->containers_cache != NULL)
            g_ptr_array_free (data->containers_cache, TRUE);
        data->containers_cache = data->items;
        data->items = NULL;
        g_free (data->containers_cache_proj);
        data->containers_cache_proj = g_strdup (data->current_project);
    }

    /* Restore items (replaces whatever is in data->items) */
    clear_items (data);
    data->items = frame->items;
    frame->items = NULL;

    /* Set cursor target */
    g_free (data->pending_focus);
    data->pending_focus = frame->focus_name;
    frame->focus_name = NULL;

    /* Restore view */
    data->view = frame->view;

    /* Remove frame and add to panel history */
    g_array_remove_index (data->nav_stack, data->nav_stack->len - 1);

    if (data->host != NULL && data->host->add_history != NULL)
    {
        const char *p = docker_get_path (data);

        if (p != NULL)
        {
            char *pp = g_strdup_printf ("%s:%s", docker_plugin.proto, p);

            data->host->add_history (data->host, pp);
            g_free (pp);
        }
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Drop all saved navigation state. */
static void
nav_stack_clear (docker_data_t *data)
{
    guint i;

    for (i = 0; i < data->nav_stack->len; i++)
    {
        docker_nav_frame_t *frame = &g_array_index (data->nav_stack, docker_nav_frame_t, i);

        nav_frame_clear (frame);
    }
    g_array_set_size (data->nav_stack, 0);

    g_free (data->pending_focus);
    data->pending_focus = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
clear_container_list_caches (docker_data_t *data)
{
    if (data->projects_cache != NULL)
    {
        g_ptr_array_free (data->projects_cache, TRUE);
        data->projects_cache = NULL;
    }

    if (data->containers_cache != NULL)
    {
        g_ptr_array_free (data->containers_cache, TRUE);
        data->containers_cache = NULL;
    }

    g_free (data->containers_cache_proj);
    data->containers_cache_proj = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Save the current files view before chdir replaces its items. */
static void
nav_push_files_frame (docker_data_t *data, const char *focus_name, GPtrArray *items,
                      const char *old_cwd)
{
    docker_nav_frame_t frame;

    g_free (data->pending_focus);
    data->pending_focus = NULL;

    frame.view = DOCKER_VIEW_CONTAINER_FILES;
    frame.items = items; /* transfer ownership */
    frame.focus_name = g_strdup (focus_name);
    frame.active_conn = data->active_conn;
    frame.current_project = g_strdup (data->current_project);
    frame.current_container_id = g_strdup (data->current_container_id);
    frame.current_container_name = g_strdup (data->current_container_name);
    frame.files_cwd = g_strdup (old_cwd);

    g_array_append_val (data->nav_stack, frame);
}

/* --------------------------------------------------------------------------------------------- */

static void
files_focus_cache_store (docker_data_t *data, const char *cwd, const char *focus_name)
{
    char *key;

    if (data == NULL || data->files_focus_cache == NULL || data->current_container_id == NULL
        || cwd == NULL || focus_name == NULL || focus_name[0] == '\0'
        || strcmp (focus_name, "..") == 0)
        return;

    key = g_strdup_printf ("%s\t%s", data->current_container_id, cwd);
    g_hash_table_replace (data->files_focus_cache, key, g_strdup (focus_name));
}

/* --------------------------------------------------------------------------------------------- */

static void
files_focus_cache_store_current (docker_data_t *data, const char *fallback_name)
{
    const char *cwd;
    const char *focus_name = fallback_name;

    if (data == NULL || data->view != DOCKER_VIEW_CONTAINER_FILES)
        return;

    cwd = (data->files_cwd != NULL) ? data->files_cwd : "/";

    if ((focus_name == NULL || focus_name[0] == '\0') && data->host != NULL
        && data->host->get_current != NULL)
    {
        const GString *cur = data->host->get_current (data->host);

        if (cur != NULL && cur->len > 0)
            focus_name = cur->str;
    }

    files_focus_cache_store (data, cwd, focus_name);
}

/* --------------------------------------------------------------------------------------------- */

static const char *
docker_get_focus_name (void *plugin_data)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    return data->pending_focus;
}

/* --------------------------------------------------------------------------------------------- */

void
clear_files_cache (docker_data_t *data)
{
    if (data->files_cache != NULL)
        g_hash_table_remove_all (data->files_cache);
    if (data->files_focus_cache != NULL)
        g_hash_table_remove_all (data->files_focus_cache);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_parse_port (const char *port_str, int *port_out)
{
    char *end;
    long port_val;

    if (port_str == NULL || port_str[0] == '\0')
    {
        *port_out = 0;
        return TRUE;
    }
    port_val = strtol (port_str, &end, 10);
    if (*end != '\0' || port_val < 1 || port_val > 65535)
        return FALSE;
    *port_out = (int) port_val;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
clear_items (docker_data_t *data)
{
    if (data->items != NULL)
    {
        g_ptr_array_free (data->items, TRUE);
        data->items = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

const docker_item_t *
find_item_by_name (const docker_data_t *data, const char *name)
{
    guint i;
    const char *lookup_name = name;

    if (data->items == NULL || name == NULL)
        return NULL;

    if (data->view == DOCKER_VIEW_VOLUMES && (name[0] == '*' || name[0] == ' ') && name[1] != '\0')
        lookup_name = name + 1;

    for (i = 0; i < data->items->len; i++)
    {
        const docker_item_t *item = (const docker_item_t *) g_ptr_array_index (data->items, i);

        if (strcmp (item->name, lookup_name) == 0)
            return item;
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

docker_view_t
view_from_root_path (const char *path)
{
    if (strcmp (path, "containers") == 0)
        return DOCKER_VIEW_CONTAINERS_PROJECTS;
    if (strcmp (path, "images") == 0)
        return DOCKER_VIEW_IMAGES;
    if (strcmp (path, "volumes") == 0)
        return DOCKER_VIEW_VOLUMES;
    if (strcmp (path, "networks") == 0)
        return DOCKER_VIEW_NETWORKS;

    return DOCKER_VIEW_ROOT;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
run_cmd (const char *cmd, char **output, char **err_text)
{
    gchar *std_out = NULL;
    gchar *std_err = NULL;
    gint status = 0;
    GError *error = NULL;
    gboolean spawned;
    gboolean exited_ok;

    spawned = g_spawn_command_line_sync (cmd, &std_out, &std_err, &status, &error);
    if (!spawned)
    {
        if (err_text != NULL)
        {
            if (error != NULL && error->message != NULL)
                *err_text = g_strdup (error->message);
            else
                *err_text = g_strdup (_ ("Failed to start docker command"));
        }

        if (output != NULL)
            *output = NULL;

        if (error != NULL)
            g_error_free (error);
        g_free (std_out);
        g_free (std_err);
        return FALSE;
    }

#if GLIB_CHECK_VERSION(2, 70, 0)
    exited_ok = g_spawn_check_wait_status (status, NULL);
#else
    exited_ok = g_spawn_check_exit_status (status, NULL);
#endif

    if (output != NULL)
        *output = std_out;
    else
        g_free (std_out);

    if (err_text != NULL)
        *err_text = std_err;
    else
        g_free (std_err);

    return exited_ok;
}

/* --------------------------------------------------------------------------------------------- */

char *
strip_trailing_newlines (char *text)
{
    size_t len;

    if (text == NULL)
        return NULL;

    len = strlen (text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r'))
        text[--len] = '\0';

    return text;
}

/* --------------------------------------------------------------------------------------------- */
/*** Connection model *************************************************************************/
/* --------------------------------------------------------------------------------------------- */

docker_connection_t *
docker_connection_new_local (void)
{
    docker_connection_t *conn;

    conn = g_new0 (docker_connection_t, 1);
    conn->id = g_strdup ("local");
    conn->label = g_strdup (_ ("Local Docker"));
    conn->type = DOCKER_CONN_LOCAL;
    conn->docker_path = NULL;
    conn->host = NULL;
    conn->user = NULL;
    conn->port = 0;
    conn->key_path = NULL;
    return conn;
}

/* --------------------------------------------------------------------------------------------- */

docker_connection_t *
docker_connection_clone (const docker_connection_t *conn)
{
    docker_connection_t *copy;

    if (conn == NULL)
        return NULL;

    copy = g_new0 (docker_connection_t, 1);
    copy->id = g_strdup (conn->id);
    copy->label = g_strdup (conn->label);
    copy->type = conn->type;
    copy->docker_path = g_strdup (conn->docker_path);
    copy->host = g_strdup (conn->host);
    copy->user = g_strdup (conn->user);
    copy->port = conn->port;
    copy->key_path = g_strdup (conn->key_path);
    return copy;
}

/* --------------------------------------------------------------------------------------------- */

void
docker_connection_free (gpointer p)
{
    docker_connection_t *conn = (docker_connection_t *) p;

    if (conn == NULL)
        return;

    g_free (conn->id);
    g_free (conn->label);
    g_free (conn->docker_path);
    g_free (conn->host);
    g_free (conn->user);
    g_free (conn->key_path);
    g_free (conn);
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_connection_get_docker_path (const docker_connection_t *conn)
{
    if (conn == NULL || conn->docker_path == NULL || conn->docker_path[0] == '\0')
        return "docker";
    return conn->docker_path;
}

/* --------------------------------------------------------------------------------------------- */
/*** Config ***/
/* --------------------------------------------------------------------------------------------- */

char *
docker_connections_get_file_path (void)
{
    return g_build_filename (mc_config_get_path (), "docker-connections.ini", (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
docker_connections_load (const char *path)
{
    GKeyFile *kf;
    GPtrArray *connections;
    gchar **groups;
    gsize n_groups;
    guint i;

    connections = g_ptr_array_new_with_free_func (docker_connection_free);

    kf = g_key_file_new ();
    if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL))
    {
        g_key_file_free (kf);
        return connections;
    }

    groups = g_key_file_get_groups (kf, &n_groups);
    for (i = 0; i < (guint) n_groups; i++)
    {
        docker_connection_t *conn;
        char *type_str;

        conn = g_new0 (docker_connection_t, 1);
        conn->id = g_strdup (groups[i]);

        conn->label = g_key_file_get_string (kf, groups[i], "label", NULL);
        if (conn->label == NULL || conn->label[0] == '\0')
        {
            g_free (conn->label);
            conn->label = g_strdup (groups[i]);
        }

        type_str = g_key_file_get_string (kf, groups[i], "type", NULL);
        conn->type = (type_str != NULL && strcmp (type_str, "ssh") == 0) ? DOCKER_CONN_SSH
                                                                         : DOCKER_CONN_LOCAL;
        g_free (type_str);

        conn->docker_path = g_key_file_get_string (kf, groups[i], "docker_path", NULL);
        if (conn->docker_path != NULL && conn->docker_path[0] == '\0')
        {
            g_free (conn->docker_path);
            conn->docker_path = NULL;
        }

        conn->host = g_key_file_get_string (kf, groups[i], "host", NULL);
        conn->user = g_key_file_get_string (kf, groups[i], "user", NULL);
        conn->port = (int) g_key_file_get_integer (kf, groups[i], "port", NULL);
        conn->key_path = g_key_file_get_string (kf, groups[i], "key_path", NULL);
        if (conn->key_path != NULL && conn->key_path[0] == '\0')
        {
            g_free (conn->key_path);
            conn->key_path = NULL;
        }

        g_ptr_array_add (connections, conn);
    }

    g_strfreev (groups);
    g_key_file_free (kf);
    return connections;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_connections_save (const char *path, GPtrArray *connections)
{
    GKeyFile *kf;
    gchar *data;
    gsize length;
    gboolean ok;
    char *dir;
    guint i;

    kf = g_key_file_new ();

    for (i = 0; i < connections->len; i++)
    {
        const docker_connection_t *conn =
            (const docker_connection_t *) g_ptr_array_index (connections, i);

        g_key_file_set_string (kf, conn->id, "label", conn->label != NULL ? conn->label : conn->id);
        g_key_file_set_string (kf, conn->id, "type",
                               conn->type == DOCKER_CONN_SSH ? "ssh" : "local");
        g_key_file_set_string (kf, conn->id, "docker_path",
                               conn->docker_path != NULL ? conn->docker_path : "");
        if (conn->type == DOCKER_CONN_SSH)
        {
            g_key_file_set_string (kf, conn->id, "host", conn->host != NULL ? conn->host : "");
            g_key_file_set_string (kf, conn->id, "user", conn->user != NULL ? conn->user : "");
            g_key_file_set_integer (kf, conn->id, "port", conn->port);
            g_key_file_set_string (kf, conn->id, "key_path",
                                   conn->key_path != NULL ? conn->key_path : "");
        }
    }

    data = g_key_file_to_data (kf, &length, NULL);
    g_key_file_free (kf);

    if (data == NULL)
        return FALSE;

    dir = g_path_get_dirname (path);
    g_mkdir_with_parents (dir, 0700);
    g_free (dir);

    ok = g_file_set_contents (path, data, (gssize) length, NULL);
    g_free (data);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

docker_connection_t *
docker_connections_find_by_id (GPtrArray *connections, const char *id)
{
    guint i;

    if (connections == NULL || id == NULL)
        return NULL;

    for (i = 0; i < connections->len; i++)
    {
        docker_connection_t *conn = (docker_connection_t *) g_ptr_array_index (connections, i);

        if (conn->id != NULL && strcmp (conn->id, id) == 0)
            return conn;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

docker_connection_t *
docker_connections_find_by_label (GPtrArray *connections, const char *label)
{
    guint i;

    if (connections == NULL || label == NULL)
        return NULL;

    for (i = 0; i < connections->len; i++)
    {
        docker_connection_t *conn = (docker_connection_t *) g_ptr_array_index (connections, i);

        if (conn->label != NULL && strcmp (conn->label, label) == 0)
            return conn;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

docker_connection_t *
docker_connections_find_default_local (GPtrArray *connections)
{
    docker_connection_t *first_local = NULL;
    guint i;

    if (connections == NULL)
        return NULL;

    for (i = 0; i < connections->len; i++)
    {
        docker_connection_t *conn = (docker_connection_t *) g_ptr_array_index (connections, i);

        /* Prefer the reserved "local" id */
        if (conn->id != NULL && strcmp (conn->id, "local") == 0)
            return conn;

        if (conn->type == DOCKER_CONN_LOCAL && first_local == NULL)
            first_local = conn;
    }

    return first_local;
}

/* --------------------------------------------------------------------------------------------- */
/*** Profile-aware command execution **********************************************************/
/* --------------------------------------------------------------------------------------------- */

static char *
docker_conn_build_ssh_prefix (const docker_connection_t *conn, gboolean interactive)
{
    char *ssh_host;
    char *result;

    if (conn->user != NULL && conn->user[0] != '\0')
    {
        char *user_at_host = g_strdup_printf ("%s@%s", conn->user, conn->host);
        ssh_host = g_shell_quote (user_at_host);
        g_free (user_at_host);
    }
    else
        ssh_host = g_shell_quote (conn->host);

    {
        const char *mode =
            interactive ? "-t" : "-o BatchMode=yes -o StrictHostKeyChecking=accept-new";
        char *key_opt = NULL;

        if (conn->key_path != NULL && conn->key_path[0] != '\0')
        {
            char *quoted_key = g_shell_quote (conn->key_path);
            key_opt = g_strdup_printf ("-i %s", quoted_key);
            g_free (quoted_key);
        }

        if (conn->port > 0 && key_opt != NULL)
            result = g_strdup_printf ("ssh %s %s -p %d %s", mode, key_opt, conn->port, ssh_host);
        else if (conn->port > 0)
            result = g_strdup_printf ("ssh %s -p %d %s", mode, conn->port, ssh_host);
        else if (key_opt != NULL)
            result = g_strdup_printf ("ssh %s %s %s", mode, key_opt, ssh_host);
        else
            result = g_strdup_printf ("ssh %s %s", mode, ssh_host);

        g_free (key_opt);
    }

    g_free (ssh_host);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_conn_build_shell_cmd (const docker_connection_t *conn, const char *docker_args)
{
    const char *dp = docker_connection_get_docker_path (conn);

    if (conn == NULL || conn->type == DOCKER_CONN_LOCAL)
        return g_strdup_printf ("%s %s", dp, docker_args);

    {
        char *full_cmd = g_strdup_printf ("%s %s", dp, docker_args);
        char *quoted_cmd = g_shell_quote (full_cmd);
        char *ssh_prefix = docker_conn_build_ssh_prefix (conn, TRUE);
        char *result = g_strdup_printf ("%s %s", ssh_prefix, quoted_cmd);

        g_free (full_cmd);
        g_free (quoted_cmd);
        g_free (ssh_prefix);
        return result;
    }
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_conn_build_pipe_cmd (const docker_connection_t *conn, const char *docker_args)
{
    const char *dp = docker_connection_get_docker_path (conn);

    if (conn == NULL || conn->type == DOCKER_CONN_LOCAL)
        return g_strdup_printf ("%s %s", dp, docker_args);

    {
        char *full_cmd = g_strdup_printf ("%s %s", dp, docker_args);
        char *quoted_cmd = g_shell_quote (full_cmd);
        char *ssh_prefix = docker_conn_build_ssh_prefix (conn, FALSE);
        char *result = g_strdup_printf ("%s %s", ssh_prefix, quoted_cmd);

        g_free (full_cmd);
        g_free (quoted_cmd);
        g_free (ssh_prefix);
        return result;
    }
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_conn_run (const docker_connection_t *conn, const char *docker_args, char **output,
                 char **err_text)
{
    const char *dp = docker_connection_get_docker_path (conn);
    char *cmd;
    gboolean ok;

    if (conn == NULL || conn->type == DOCKER_CONN_LOCAL)
    {
        cmd = g_strdup_printf ("%s %s", dp, docker_args);
        ok = run_cmd (cmd, output, err_text);
        g_free (cmd);
        return ok;
    }

    {
        char *full_cmd = g_strdup_printf ("%s %s", dp, docker_args);
        char *quoted_cmd = g_shell_quote (full_cmd);
        char *ssh_prefix = docker_conn_build_ssh_prefix (conn, FALSE);

        cmd = g_strdup_printf ("%s %s", ssh_prefix, quoted_cmd);
        g_free (full_cmd);
        g_free (quoted_cmd);
        g_free (ssh_prefix);

        ok = run_cmd (cmd, output, err_text);
        g_free (cmd);
        return ok;
    }
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_conn_capture (const docker_connection_t *conn, const char *docker_args)
{
    char *output = NULL;
    char *err_text = NULL;

    if (!docker_conn_run (conn, docker_args, &output, &err_text))
    {
        g_free (output);
        output = NULL;
    }

    g_free (err_text);
    return strip_trailing_newlines (output);
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_conn_capture_inspect (const docker_connection_t *conn, const char *obj_id,
                             const char *format)
{
    char *quoted_id;
    char *quoted_format;
    char *docker_args;
    char *value;

    quoted_id = g_shell_quote (obj_id);
    quoted_format = g_shell_quote (format);
    docker_args = g_strdup_printf ("inspect --format %s %s", quoted_format, quoted_id);
    value = docker_conn_capture (conn, docker_args);

    g_free (docker_args);
    g_free (quoted_format);
    g_free (quoted_id);
    return value;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
reload_items (docker_data_t *data)
{
    char *output = NULL;
    char *err_text = NULL;

    clear_items (data);

    switch (data->view)
    {
    case DOCKER_VIEW_ROOT:
        return TRUE;

    case DOCKER_VIEW_CONTAINERS_PROJECTS:
        if (data->projects_cache != NULL)
        {
            /* Reuse the cached listing. */
            data->items = docker_items_clone (data->projects_cache);
            return TRUE;
        }
        if (!docker_containers_reload_projects (data, &err_text))
            goto cmd_failed;
        if (data->projects_cache != NULL)
            g_ptr_array_free (data->projects_cache, TRUE);
        data->projects_cache = docker_items_clone (data->items);
        break;

    case DOCKER_VIEW_CONTAINERS_ITEMS:
        if (data->containers_cache != NULL && data->current_project != NULL
            && data->containers_cache_proj != NULL
            && strcmp (data->containers_cache_proj, data->current_project) == 0)
        {
            data->items = docker_items_clone (data->containers_cache);
            return TRUE;
        }
        /* Discard a cache for another project. */
        if (data->containers_cache != NULL)
        {
            g_ptr_array_free (data->containers_cache, TRUE);
            data->containers_cache = NULL;
            g_free (data->containers_cache_proj);
            data->containers_cache_proj = NULL;
        }
        if (!docker_containers_reload_items (data, &err_text))
            goto cmd_failed;
        if (data->containers_cache != NULL)
            g_ptr_array_free (data->containers_cache, TRUE);
        data->containers_cache = docker_items_clone (data->items);
        g_free (data->containers_cache_proj);
        data->containers_cache_proj = g_strdup (data->current_project);
        break;

    case DOCKER_VIEW_CONTAINER_DETAILS:
        if (!docker_containers_resolve_current (data, &err_text))
            goto cmd_failed;
        if (!docker_containers_reload_details (data))
            goto cmd_failed;
        break;

    case DOCKER_VIEW_CONTAINER_FILES:
        if (!docker_containers_resolve_current (data, &err_text))
            goto cmd_failed;
        if (!docker_container_files_reload (data, &err_text))
            goto cmd_failed;
        break;

    case DOCKER_VIEW_CONTAINER_MOUNTS:
        if (!docker_containers_resolve_current (data, &err_text))
            goto cmd_failed;
        if (!docker_container_mounts_reload (data, &err_text))
            goto cmd_failed;
        break;

    case DOCKER_VIEW_IMAGES:
        if (!docker_images_reload (data, &err_text))
            goto cmd_failed;
        break;

    case DOCKER_VIEW_VOLUMES:
        if (!docker_volumes_reload (data, &err_text))
            goto cmd_failed;
        break;

    case DOCKER_VIEW_NETWORKS:
        if (!docker_networks_reload (data, &err_text))
            goto cmd_failed;
        break;

    default:
        data->items = g_ptr_array_new_with_free_func (docker_item_free);
        break;
    }

    g_free (output);
    g_free (err_text);

    if (data->items == NULL)
        data->items = g_ptr_array_new_with_free_func (docker_item_free);

    return TRUE;

cmd_failed:
    if (err_text != NULL && err_text[0] != '\0')
        message (D_ERROR, MSG_ERROR, "%s", err_text);

    g_free (output);
    g_free (err_text);

    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
write_temp_content (const char *prefix, const char *content, char **local_path)
{
    GError *error = NULL;
    int fd;

    fd = g_file_open_tmp (prefix, local_path, &error);
    if (fd == -1)
    {
        if (error != NULL)
            g_error_free (error);
        return FALSE;
    }

    if (content == NULL)
        content = "";

    if (write (fd, content, strlen (content)) == -1)
    {
        close (fd);
        unlink (*local_path);
        g_free (*local_path);
        *local_path = NULL;
        return FALSE;
    }

    close (fd);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
set_view (docker_data_t *data, docker_view_t new_view)
{
    data->view = new_view;
    clear_items (data);

    if (data->host != NULL && data->host->add_history != NULL)
    {
        const char *path = docker_get_path (data);
        if (path != NULL && docker_plugin.proto != NULL)
        {
            char *plugin_path = g_strdup_printf ("%s:%s", docker_plugin.proto, path);
            data->host->add_history (data->host, plugin_path);
            g_free (plugin_path);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_get_path (docker_data_t *data)
{
    const char *conn_id;

    g_free (data->title_buf);

    if (data->view == DOCKER_VIEW_PROFILES)
    {
        data->title_buf = g_strdup ("/");
        return data->title_buf;
    }

    conn_id = (data->active_conn != NULL && data->active_conn->id != NULL) ? data->active_conn->id
                                                                           : "local";

    switch (data->view)
    {
    case DOCKER_VIEW_ROOT:
        data->title_buf = g_strdup_printf ("/%s", conn_id);
        break;

    case DOCKER_VIEW_CONTAINERS_PROJECTS:
        data->title_buf = g_strdup_printf ("/%s/containers", conn_id);
        break;

    case DOCKER_VIEW_CONTAINERS_ITEMS:
        data->title_buf = g_strdup_printf (
            "/%s/containers/%s", conn_id,
            data->current_project != NULL ? data->current_project : docker_ungrouped_project);
        break;

    case DOCKER_VIEW_CONTAINER_DETAILS:
        data->title_buf = g_strdup_printf (
            "/%s/containers/%s/%s", conn_id,
            data->current_project != NULL ? data->current_project : docker_ungrouped_project,
            data->current_container_name != NULL ? data->current_container_name : "container");
        break;

    case DOCKER_VIEW_CONTAINER_FILES:
        if (data->files_cwd == NULL || strcmp (data->files_cwd, "/") == 0)
            data->title_buf = g_strdup_printf (
                "/%s/containers/%s/%s/files", conn_id,
                data->current_project != NULL ? data->current_project : docker_ungrouped_project,
                data->current_container_name != NULL ? data->current_container_name : "container");
        else
            data->title_buf = g_strdup_printf (
                "/%s/containers/%s/%s/files%s", conn_id,
                data->current_project != NULL ? data->current_project : docker_ungrouped_project,
                data->current_container_name != NULL ? data->current_container_name : "container",
                data->files_cwd);
        break;

    case DOCKER_VIEW_CONTAINER_MOUNTS:
        data->title_buf = g_strdup_printf (
            "/%s/containers/%s/%s/mounts", conn_id,
            data->current_project != NULL ? data->current_project : docker_ungrouped_project,
            data->current_container_name != NULL ? data->current_container_name : "container");
        break;

    case DOCKER_VIEW_IMAGES:
        data->title_buf = g_strdup_printf ("/%s/images", conn_id);
        break;

    case DOCKER_VIEW_VOLUMES:
        data->title_buf = g_strdup_printf ("/%s/volumes", conn_id);
        break;

    case DOCKER_VIEW_NETWORKS:
        data->title_buf = g_strdup_printf ("/%s/networks", conn_id);
        break;

    default:
        data->title_buf = g_strdup ("/");
        break;
    }

    return data->title_buf;
}

/* --------------------------------------------------------------------------------------------- */

/* Apply a resource path (everything after the profile prefix, starting with '/').
 * Sets view, current_project, current_container_name, files_cwd as appropriate.
 * The path must start with '/' or be empty. */
static gboolean
docker_apply_resource_path (docker_data_t *data, const char *path)
{
    const char *rest;
    const char *slash;

    if (path == NULL || path[0] == '\0' || strcmp (path, "/") == 0)
    {
        data->view = DOCKER_VIEW_ROOT;
        return TRUE;
    }

    if (strcmp (path, "/containers") == 0)
    {
        data->view = DOCKER_VIEW_CONTAINERS_PROJECTS;
        return TRUE;
    }

    if (strcmp (path, "/images") == 0)
    {
        data->view = DOCKER_VIEW_IMAGES;
        return TRUE;
    }

    if (strcmp (path, "/volumes") == 0)
    {
        data->view = DOCKER_VIEW_VOLUMES;
        return TRUE;
    }

    if (strcmp (path, "/networks") == 0)
    {
        data->view = DOCKER_VIEW_NETWORKS;
        return TRUE;
    }

    if (!g_str_has_prefix (path, "/containers/"))
    {
        g_debug ("docker_apply_resource_path: unsupported path='%s'", path);
        return FALSE;
    }

    rest = path + strlen ("/containers/");
    if (*rest == '\0')
    {
        data->view = DOCKER_VIEW_CONTAINERS_PROJECTS;
        return TRUE;
    }

    slash = strchr (rest, '/');
    if (slash == NULL)
    {
        data->current_project = g_strdup (rest);
        data->view = DOCKER_VIEW_CONTAINERS_ITEMS;
        return TRUE;
    }

    if (slash == rest)
    {
        g_debug ("docker_apply_resource_path: empty project segment path='%s'", path);
        return FALSE;
    }

    data->current_project = g_strndup (rest, (gsize) (slash - rest));
    rest = slash + 1;
    if (*rest == '\0')
    {
        data->view = DOCKER_VIEW_CONTAINERS_ITEMS;
        return TRUE;
    }

    slash = strchr (rest, '/');
    if (slash == NULL)
    {
        data->current_container_name = g_strdup (rest);
        data->view = DOCKER_VIEW_CONTAINER_DETAILS;
        return TRUE;
    }

    if (slash == rest)
    {
        g_debug ("docker_apply_resource_path: empty container segment path='%s'", path);
        return FALSE;
    }

    data->current_container_name = g_strndup (rest, (gsize) (slash - rest));
    rest = slash + 1;

    {
        gboolean is_ssh = (data->active_conn != NULL && data->active_conn->type == DOCKER_CONN_SSH);

        if (strcmp (rest, docker_mounts_entry) == 0)
        {
            if (is_ssh)
                return FALSE;
            data->view = DOCKER_VIEW_CONTAINER_MOUNTS;
            return TRUE;
        }

        if (strcmp (rest, docker_files_entry) == 0)
        {
            data->view = DOCKER_VIEW_CONTAINER_FILES;
            data->files_cwd = g_strdup ("/");
            return TRUE;
        }

        if (g_str_has_prefix (rest, "files/"))
        {
            data->view = DOCKER_VIEW_CONTAINER_FILES;
            data->files_cwd = g_strdup_printf ("/%s", rest + strlen ("files/"));
            return TRUE;
        }
    }

    g_debug ("docker_apply_resource_path: unsupported subpath='%s'", path);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
docker_apply_open_path (docker_data_t *data, const char *open_path)
{
    char *normalized;
    const char *path;
    const char *id_end;
    char *profile_id;
    docker_connection_t *conn;
    gboolean ok;

    g_debug ("docker_apply_open_path input='%s'", open_path != NULL ? open_path : "(null)");

    normalized = docker_normalize_open_path (open_path);
    if (normalized == NULL)
    {
        data->view = DOCKER_VIEW_PROFILES;
        return TRUE;
    }

    path = normalized;
    g_debug ("docker_apply_open_path normalized='%s'", path);

    /* "/" -> profile list */
    if (strcmp (path, "/") == 0)
    {
        data->view = DOCKER_VIEW_PROFILES;
        g_free (normalized);
        return TRUE;
    }

    /* Legacy paths: /containers/... /images /volumes /networks
     * resolve through the default local profile. */
    if (strcmp (path, "/containers") == 0 || g_str_has_prefix (path, "/containers/")
        || strcmp (path, "/images") == 0 || strcmp (path, "/volumes") == 0
        || strcmp (path, "/networks") == 0)
    {
        conn = docker_connections_find_default_local (data->connections);
        if (conn == NULL)
        {
            g_debug ("docker_apply_open_path: legacy path but no local profile");
            g_free (normalized);
            return FALSE;
        }
        data->active_conn = conn;
        ok = docker_apply_resource_path (data, path);
        g_free (normalized);
        return ok;
    }

    /* Profile-prefixed path: /<profile_id>[/rest] */
    path++; /* skip leading '/' */
    id_end = strchr (path, '/');
    if (id_end == NULL)
        profile_id = g_strdup (path);
    else
        profile_id = g_strndup (path, (gsize) (id_end - path));

    conn = docker_connections_find_by_id (data->connections, profile_id);
    g_free (profile_id);

    if (conn == NULL)
    {
        g_debug ("docker_apply_open_path: unknown profile in path='%s'", normalized);
        g_free (normalized);
        return FALSE;
    }

    data->active_conn = conn;

    if (id_end == NULL || id_end[0] == '\0' || strcmp (id_end, "/") == 0)
    {
        /* Just /<profile_id> or /<profile_id>/ -> docker root */
        data->view = DOCKER_VIEW_ROOT;
        g_free (normalized);
        return TRUE;
    }

    /* Apply the resource path (/containers/... etc.) */
    ok = docker_apply_resource_path (data, id_end);
    g_free (normalized);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static char *
docker_normalize_open_path (const char *open_path)
{
    const char *path = open_path;
    GString *normalized;
    gboolean prev_was_slash = FALSE;

    if (path == NULL || path[0] == '\0')
        return NULL;

    if (g_str_has_prefix (path, docker_plugin.prefix))
        path += strlen (docker_plugin.prefix);
    else
        return NULL;

    if (path[0] == '\0')
        return g_strdup ("/");

    normalized = g_string_new ("/");

    while (*path == '/')
        path++;

    for (; *path != '\0'; path++)
    {
        if (*path == '/')
        {
            if (prev_was_slash)
                continue;
            prev_was_slash = TRUE;
        }
        else
            prev_was_slash = FALSE;

        g_string_append_c (normalized, *path);
    }

    while (normalized->len > 1 && normalized->str[normalized->len - 1] == '/')
        g_string_truncate (normalized, normalized->len - 1);

    g_debug ("docker_normalize_open_path result='%s'", normalized->str);
    return g_string_free (normalized, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/* Plugin callbacks */
/* --------------------------------------------------------------------------------------------- */

static void *
docker_open (mc_panel_host_t *host, const char *open_path)
{
    docker_data_t *data;

    data = g_new0 (docker_data_t, 1);
    data->host = host;
    data->view = DOCKER_VIEW_PROFILES; /* g_new0 sets to 0 = PROFILES */
    data->items = NULL;
    data->root_focus = NULL;
    data->current_project = NULL;
    data->current_container_id = NULL;
    data->current_container_name = NULL;
    data->files_cache =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
    data->files_focus_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    data->title_buf = NULL;
    data->help_filename = g_build_filename (MC_PLUGIN_DIR, "docker_panel.hlp", (char *) NULL);
    data->pending_focus = NULL;
    data->nav_stack = g_array_new (FALSE, TRUE, sizeof (docker_nav_frame_t));

    /* Load connections before applying the path */
    data->connections_file = docker_connections_get_file_path ();
    data->connections = docker_connections_load (data->connections_file);

    /* Auto-create default local profile if config is empty */
    if (data->connections->len == 0)
    {
        docker_connection_t *local = docker_connection_new_local ();

        g_ptr_array_add (data->connections, local);
        (void) docker_connections_save (data->connections_file, data->connections);
    }

    if (!docker_apply_open_path (data, open_path))
    {
        message (D_ERROR, MSG_ERROR, _ ("Cannot open docker path: %s"),
                 open_path != NULL ? open_path : "");
        data->view = DOCKER_VIEW_PROFILES;
        data->active_conn = NULL;
    }

    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_close (void *plugin_data)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    clear_items (data);
    if (data->connections != NULL)
        g_ptr_array_free (data->connections, TRUE);
    g_free (data->connections_file);
    g_free (data->root_focus);
    g_free (data->current_project);
    g_free (data->current_container_id);
    g_free (data->current_container_name);
    g_free (data->files_cwd);
    if (data->files_cache != NULL)
        g_hash_table_destroy (data->files_cache);
    if (data->files_focus_cache != NULL)
        g_hash_table_destroy (data->files_focus_cache);
    g_free (data->title_buf);
    g_free (data->help_filename);
    nav_stack_clear (data);
    g_array_free (data->nav_stack, TRUE);
    clear_container_list_caches (data);
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_get_help_info (void *plugin_data, const char **filename, const char **node)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    if (filename != NULL)
    {
        if (data != NULL && data->help_filename != NULL
            && g_file_test (data->help_filename, G_FILE_TEST_IS_REGULAR))
            *filename = data->help_filename;
        else
            *filename = NULL;
    }

    if (node != NULL)
        *node = "[Docker Plugin]";

    if (filename != NULL && *filename == NULL)
        return MC_PPR_NOT_SUPPORTED;

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_get_items (void *plugin_data, void *list_ptr)
{
    docker_data_t *data = (docker_data_t *) plugin_data;
    guint idx;

    if (data->view == DOCKER_VIEW_PROFILES)
    {
        /* Show the list of connection profiles */
        if (data->connections != NULL)
        {
            for (idx = 0; idx < data->connections->len; idx++)
            {
                const docker_connection_t *conn =
                    (const docker_connection_t *) g_ptr_array_index (data->connections, idx);
                const char *display_name = (conn->label != NULL) ? conn->label : conn->id;

                mc_pp_add_entry (list_ptr, display_name, S_IFDIR | 0755, 0, time (NULL));
            }
        }
        return MC_PPR_OK;
    }

    if (data->view == DOCKER_VIEW_ROOT)
    {
        const char *sections[] = { "containers", "images", "volumes", "networks" };
        int sec_i;

        for (sec_i = 0; sec_i < 4; sec_i++)
            if (data->root_focus != NULL && strcmp (sections[sec_i], data->root_focus) == 0)
                mc_pp_add_entry (list_ptr, sections[sec_i], S_IFDIR | 0755, 0, time (NULL));

        for (sec_i = 0; sec_i < 4; sec_i++)
            if (data->root_focus == NULL || strcmp (sections[sec_i], data->root_focus) != 0)
                mc_pp_add_entry (list_ptr, sections[sec_i], S_IFDIR | 0755, 0, time (NULL));

        mc_pp_add_entry (list_ptr, docker_daemon_info_file, S_IFREG | 0644, 0, time (NULL));
        mc_pp_add_entry (list_ptr, docker_version_file, S_IFREG | 0644, 0, time (NULL));
        return MC_PPR_OK;
    }

    if (data->items == NULL)
        reload_items (data);

    if (data->items != NULL)
    {
        for (idx = 0; idx < data->items->len; idx++)
        {
            const docker_item_t *item =
                (const docker_item_t *) g_ptr_array_index (data->items, idx);
            mode_t mode;
            const char *entry_name = item->name;
            char *display_name = NULL;

            if (data->view == DOCKER_VIEW_CONTAINER_MOUNTS && item->is_dir)
                mode = S_IFDIR | 0755;
            else if (item->is_link)
                mode = S_IFLNK | 0777;
            else if (item->is_dir)
                mode = S_IFDIR | 0755;
            else if (data->view == DOCKER_VIEW_VOLUMES && item->status != NULL
                     && strcmp (item->status, "in use") == 0)
                mode = S_IFREG | 0755;
            else
                mode = S_IFREG | 0644;

            if (data->view == DOCKER_VIEW_VOLUMES)
            {
                display_name = g_strdup_printf (
                    "%c%s",
                    (item->status != NULL && strcmp (item->status, "in use") == 0) ? '*' : ' ',
                    item->name);
                entry_name = display_name;
            }

            mc_pp_add_entry (list_ptr, entry_name, mode, item->size, time (NULL));
            g_free (display_name);
        }
    }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_chdir (void *plugin_data, const char *path)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    if (strcmp (path, "..") == 0)
    {
        switch (data->view)
        {
        case DOCKER_VIEW_PROFILES:
            return MC_PPR_CLOSE; /* close plugin */

        case DOCKER_VIEW_ROOT:
            /* Try cache hit first */
            if (nav_stack_pop (data))
                return MC_PPR_OK;
            /* Fallback: opened via path with no stack */
            g_free (data->pending_focus);
            data->pending_focus = (data->active_conn != NULL && data->active_conn->label != NULL)
                ? g_strdup (data->active_conn->label)
                : NULL;
            data->active_conn = NULL;
            set_view (data, DOCKER_VIEW_PROFILES);
            return MC_PPR_OK;

        case DOCKER_VIEW_CONTAINERS_PROJECTS:
            if (nav_stack_pop (data))
                return MC_PPR_OK;
            g_free (data->root_focus);
            data->root_focus = g_strdup ("containers");
            set_view (data, DOCKER_VIEW_ROOT);
            return MC_PPR_OK;

        case DOCKER_VIEW_IMAGES:
            if (nav_stack_pop (data))
                return MC_PPR_OK;
            g_free (data->root_focus);
            data->root_focus = g_strdup ("images");
            set_view (data, DOCKER_VIEW_ROOT);
            return MC_PPR_OK;

        case DOCKER_VIEW_VOLUMES:
            if (nav_stack_pop (data))
                return MC_PPR_OK;
            g_free (data->root_focus);
            data->root_focus = g_strdup ("volumes");
            set_view (data, DOCKER_VIEW_ROOT);
            return MC_PPR_OK;

        case DOCKER_VIEW_NETWORKS:
            if (nav_stack_pop (data))
                return MC_PPR_OK;
            g_free (data->root_focus);
            data->root_focus = g_strdup ("networks");
            set_view (data, DOCKER_VIEW_ROOT);
            return MC_PPR_OK;

        case DOCKER_VIEW_CONTAINERS_ITEMS:
            if (nav_stack_pop (data))
                return MC_PPR_OK;
            /* Fallback */
            g_free (data->pending_focus);
            data->pending_focus = g_strdup (data->current_project);
            set_view (data, DOCKER_VIEW_CONTAINERS_PROJECTS);
            reload_items (data);
            return MC_PPR_OK;

        case DOCKER_VIEW_CONTAINER_DETAILS:
            if (nav_stack_pop (data))
                return MC_PPR_OK;
            /* Fallback */
            g_free (data->pending_focus);
            data->pending_focus = g_strdup (data->current_container_name);
            set_view (data, DOCKER_VIEW_CONTAINERS_ITEMS);
            reload_items (data);
            return MC_PPR_OK;

        case DOCKER_VIEW_CONTAINER_FILES:
        {
            files_focus_cache_store_current (data, NULL);

            if (nav_stack_pop (data))
                return MC_PPR_OK;
            /* Fallback: go up one level inside container, or back to details if at root */
            {
                char *parent = mc_pp_path_up (data->files_cwd);

                if (parent == NULL)
                {
                    /* already at / -- go back to container details */
                    g_free (data->pending_focus);
                    data->pending_focus = g_strdup (docker_files_entry);
                    g_free (data->files_cwd);
                    data->files_cwd = NULL;
                    set_view (data, DOCKER_VIEW_CONTAINER_DETAILS);
                    reload_items (data);
                    return MC_PPR_OK;
                }

                g_free (data->pending_focus);
                data->pending_focus =
                    (data->files_cwd != NULL) ? g_path_get_basename (data->files_cwd) : NULL;
                g_free (data->files_cwd);
                data->files_cwd = parent;
                set_view (data, DOCKER_VIEW_CONTAINER_FILES);
                reload_items (data);
                return MC_PPR_OK;
            }
        }

        case DOCKER_VIEW_CONTAINER_MOUNTS:
            if (nav_stack_pop (data))
                return MC_PPR_OK;
            /* Fallback */
            g_free (data->pending_focus);
            data->pending_focus = g_strdup (docker_mounts_entry);
            set_view (data, DOCKER_VIEW_CONTAINER_DETAILS);
            reload_items (data);
            return MC_PPR_OK;

        default:
            set_view (data, DOCKER_VIEW_ROOT);
            return MC_PPR_OK;
        }
    }

    if (data->view == DOCKER_VIEW_ROOT)
    {
        docker_view_t next = view_from_root_path (path);

        if (next == DOCKER_VIEW_ROOT)
            return MC_PPR_FAILED;

        nav_stack_push (data, path);
        g_free (data->root_focus);
        data->root_focus = g_strdup (path);
        set_view (data, next);
        reload_items (data);
        return MC_PPR_OK;
    }

    if (data->view == DOCKER_VIEW_CONTAINERS_PROJECTS)
    {
        const docker_item_t *project = find_item_by_name (data, path);

        if (project == NULL)
            return MC_PPR_FAILED;

        nav_stack_push (data, path);
        g_free (data->current_project);
        data->current_project = g_strdup (project->id);
        g_free (data->current_container_id);
        data->current_container_id = NULL;
        g_free (data->current_container_name);
        data->current_container_name = NULL;
        set_view (data, DOCKER_VIEW_CONTAINERS_ITEMS);
        reload_items (data);
        return MC_PPR_OK;
    }

    if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
    {
        const docker_item_t *container = find_item_by_name (data, path);

        if (container == NULL)
            return MC_PPR_FAILED;

        nav_stack_push (data, path);
        g_free (data->current_container_id);
        data->current_container_id = g_strdup (container->id);
        g_free (data->current_container_name);
        data->current_container_name = g_strdup (container->name);
        set_view (data, DOCKER_VIEW_CONTAINER_DETAILS);
        reload_items (data);
        return MC_PPR_OK;
    }

    if (data->view == DOCKER_VIEW_CONTAINER_FILES)
    {
        const docker_item_t *item = find_item_by_name (data, path);
        GPtrArray *saved_items = NULL;
        char *saved_cwd = NULL;
        mc_pp_result_t result;

        /* docker_container_files_chdir calls find_item_by_name and then set_view (which
         * clears data->items).  Snapshot items and cwd BEFORE the call so we can push
         * a nav frame after a successful chdir. */
        if (item != NULL && item->is_dir)
        {
            files_focus_cache_store_current (data, path);
            saved_items = docker_items_clone (data->items);
            saved_cwd = g_strdup (data->files_cwd);
        }

        result = docker_container_files_chdir (data, path);
        if (result == MC_PPR_OK)
        {
            if (saved_items != NULL)
            {
                nav_push_files_frame (data, path, saved_items, saved_cwd);
                saved_items = NULL; /* ownership transferred */
            }
            reload_items (data);
        }
        if (saved_items != NULL)
            g_ptr_array_free (saved_items, TRUE);
        g_free (saved_cwd);
        return result;
    }

    return MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_enter (void *plugin_data, const char *name, const struct stat *st)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    (void) st;

    if (data->view == DOCKER_VIEW_PROFILES)
    {
        /* Find profile by label (profiles are shown by label) */
        docker_connection_t *conn = docker_connections_find_by_label (data->connections, name);

        if (conn == NULL)
            return MC_PPR_FAILED;

        nav_stack_push (data, name);
        data->active_conn = conn;
        g_free (data->root_focus);
        data->root_focus = NULL;
        clear_container_list_caches (data);
        clear_files_cache (data);
        set_view (data, DOCKER_VIEW_ROOT);
        return MC_PPR_OK;
    }

    if (data->view == DOCKER_VIEW_ROOT)
    {
        docker_view_t next = view_from_root_path (name);

        if (next != DOCKER_VIEW_ROOT)
        {
            nav_stack_push (data, name);
            g_free (data->root_focus);
            data->root_focus = g_strdup (name);
            set_view (data, next);
            reload_items (data);
            return MC_PPR_OK;
        }

        if (strcmp (name, docker_daemon_info_file) == 0 || strcmp (name, docker_version_file) == 0)
            return MC_PPR_NOT_SUPPORTED;

        return MC_PPR_FAILED;
    }

    if (data->view == DOCKER_VIEW_CONTAINERS_PROJECTS || data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
        return docker_chdir (plugin_data, name);

    if (data->view == DOCKER_VIEW_CONTAINER_DETAILS)
    {
        gboolean is_nav =
            (strcmp (name, docker_files_entry) == 0 || strcmp (name, docker_mounts_entry) == 0);
        mc_pp_result_t result;

        if (is_nav)
            nav_stack_push (data, name);

        result = docker_containers_enter (data, name);

        if (result == MC_PPR_OK && is_nav)
            reload_items (data);
        return result;
    }

    if (data->view == DOCKER_VIEW_CONTAINER_MOUNTS)
    {
        mc_pp_result_t result;

        if (strcmp (name, "..") == 0)
            return docker_chdir (plugin_data, name);

        result = docker_container_files_enter_mounts (data, name);
        return result;
    }

    if (data->view == DOCKER_VIEW_CONTAINER_FILES)
    {
        const docker_item_t *item = find_item_by_name (data, name);
        GPtrArray *saved_items = NULL;
        char *saved_cwd = NULL;
        mc_pp_result_t result;

        if (item != NULL && item->is_dir)
        {
            saved_items = docker_items_clone (data->items);
            saved_cwd = g_strdup (data->files_cwd);
        }

        result = docker_container_files_enter_mounts (data, name);
        if (result == MC_PPR_OK)
        {
            if (saved_items != NULL)
            {
                nav_push_files_frame (data, name, saved_items, saved_cwd);
                saved_items = NULL;
            }
            reload_items (data);
        }
        if (saved_items != NULL)
            g_ptr_array_free (saved_items, TRUE);
        g_free (saved_cwd);
        return result;
    }

    if (find_item_by_name (data, name) != NULL)
        return MC_PPR_NOT_SUPPORTED;

    return MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_view (void *plugin_data, const char *fname, const struct stat *st, gboolean plain_view)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    (void) st;
    (void) plain_view;

    if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
    {
        if (docker_containers_view_summary (data, fname))
            return MC_PPR_OK;
    }

    if (data->view == DOCKER_VIEW_VOLUMES)
    {
        if (docker_volumes_view_summary (data, fname))
            return MC_PPR_OK;
    }

    if (data->view == DOCKER_VIEW_CONTAINER_DETAILS && docker_containers_view_logs (data, fname))
        return MC_PPR_OK;

    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_get_local_copy (void *plugin_data, const char *fname, char **local_path)
{
    docker_data_t *data = (docker_data_t *) plugin_data;
    char *output = NULL;
    char *err_text = NULL;
    const char *cmd = NULL;
    char *cmd_dynamic = NULL;
    gboolean ok;

    if (data->view == DOCKER_VIEW_ROOT)
    {
        if (strcmp (fname, docker_daemon_info_file) == 0)
            cmd = "info";
        else if (strcmp (fname, docker_version_file) == 0)
            cmd = "version";
        else
            return MC_PPR_FAILED;
    }
    else if (data->view == DOCKER_VIEW_CONTAINER_FILES)
        return docker_container_files_get_local_copy (data, fname, local_path);
    else if (data->view == DOCKER_VIEW_CONTAINER_MOUNTS)
    {
        const docker_item_t *item = find_item_by_name (data, fname);
        char *contents = NULL;
        gsize length = 0;

        if (item == NULL || item->id == NULL || item->is_dir)
            return MC_PPR_FAILED;

        if (!g_file_get_contents (item->id, &contents, &length, NULL))
            return MC_PPR_FAILED;

        ok = write_temp_content ("mc-pp-docker-mount-XXXXXX", contents, local_path);
        g_free (contents);
        return ok ? MC_PPR_OK : MC_PPR_FAILED;
    }
    else if (data->view == DOCKER_VIEW_CONTAINER_DETAILS
             || data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
        return docker_containers_get_local_copy (data, fname, local_path);
    else
    {
        const docker_item_t *item = find_item_by_name (data, fname);
        char *quoted_id;

        if (item == NULL)
            return MC_PPR_FAILED;

        quoted_id = g_shell_quote (item->id);
        cmd_dynamic = g_strdup_printf ("inspect %s", quoted_id);
        g_free (quoted_id);
        cmd = cmd_dynamic;
    }

    ok = docker_conn_run (data->active_conn, cmd, &output, &err_text);
    g_free (cmd_dynamic);

    if (!ok)
    {
        if (err_text != NULL && err_text[0] != '\0')
            message (D_ERROR, MSG_ERROR, "%s", err_text);
        g_free (output);
        g_free (err_text);
        return MC_PPR_FAILED;
    }

    ok = write_temp_content ("mc-pp-docker-XXXXXX", output, local_path);

    g_free (output);
    g_free (err_text);

    return ok ? MC_PPR_OK : MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_save_file (void *plugin_data, const char *local_path, const char *remote_name)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    if (data->view == DOCKER_VIEW_CONTAINER_MOUNTS)
    {
        const docker_item_t *item = find_item_by_name (data, remote_name);
        char *contents = NULL;
        gsize length = 0;
        gboolean ok;

        if (item == NULL || item->id == NULL || item->is_dir)
            return MC_PPR_FAILED;

        if (!g_file_get_contents (local_path, &contents, &length, NULL))
            return MC_PPR_FAILED;

        ok = g_file_set_contents (item->id, contents, (gssize) length, NULL);
        g_free (contents);

        return ok ? MC_PPR_OK : MC_PPR_FAILED;
    }

    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_delete_items (void *plugin_data, const char **names, int count)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    if (data->view == DOCKER_VIEW_PROFILES)
    {
        int i;

        for (i = 0; i < count; i++)
        {
            docker_connection_t *conn =
                docker_connections_find_by_label (data->connections, names[i]);

            if (conn == NULL)
                continue;

            /* Block deletion of the last local profile */
            if (conn->type == DOCKER_CONN_LOCAL)
            {
                guint j;
                int local_count = 0;

                for (j = 0; j < data->connections->len; j++)
                {
                    const docker_connection_t *c =
                        (const docker_connection_t *) g_ptr_array_index (data->connections, j);
                    if (c->type == DOCKER_CONN_LOCAL)
                        local_count++;
                }
                if (local_count <= 1)
                {
                    message (D_ERROR, MSG_ERROR, "%s",
                             _ ("Cannot delete the last local Docker profile."));
                    continue;
                }
            }

            /* If active, clear it */
            if (data->active_conn == conn)
                data->active_conn = NULL;

            g_ptr_array_remove (data->connections, conn);
            if (!docker_connections_save (data->connections_file, data->connections))
                message (D_ERROR, MSG_ERROR, "%s", _ ("Failed to save connection profiles."));
        }
        return MC_PPR_OK;
    }

    if (data->view == DOCKER_VIEW_ROOT || data->view == DOCKER_VIEW_CONTAINERS_PROJECTS
        || data->view == DOCKER_VIEW_CONTAINER_DETAILS)
        return MC_PPR_NOT_SUPPORTED;

    if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
    {
        mc_pp_result_t result = docker_containers_delete_items (data, names, count);

        if (result == MC_PPR_OK)
        {
            clear_container_list_caches (data);
            reload_items (data);
        }
        return result;
    }
    if (data->view == DOCKER_VIEW_IMAGES)
    {
        mc_pp_result_t result = docker_images_delete_items (data, names, count);

        if (result == MC_PPR_OK)
            reload_items (data);
        return result;
    }
    if (data->view == DOCKER_VIEW_VOLUMES)
    {
        mc_pp_result_t result = docker_volumes_delete_items (data, names, count);

        if (result == MC_PPR_OK)
            reload_items (data);
        return result;
    }
    if (data->view == DOCKER_VIEW_NETWORKS)
    {
        mc_pp_result_t result = docker_networks_delete_items (data, names, count);

        if (result == MC_PPR_OK)
            reload_items (data);
        return result;
    }

    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
docker_get_title (void *plugin_data)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    if (data->view == DOCKER_VIEW_PROFILES || data->active_conn == NULL)
    {
        g_free (data->title_buf);
        data->title_buf = g_strdup ("Docker");
        return data->title_buf;
    }

    /* Use profile label as the title prefix instead of id */
    {
        const char *label =
            (data->active_conn->label != NULL) ? data->active_conn->label : data->active_conn->id;
        const char *id = (data->active_conn->id != NULL) ? data->active_conn->id : "local";
        const char *path = docker_get_path (data); /* sets title_buf with id-based path */

        /* Replace the leading "/<id>" segment with the label */
        if (path != NULL && path[0] == '/' && strncmp (path + 1, id, strlen (id)) == 0)
        {
            const char *rest = path + 1 + strlen (id);

            g_free (data->title_buf);
            if (rest[0] == '\0')
                data->title_buf = g_strdup (label);
            else
                data->title_buf = g_strdup_printf ("%s%s", label, rest);
        }
        /* else title_buf already set by docker_get_path */
    }

    return data->title_buf;
}

/* --------------------------------------------------------------------------------------------- */

static const mc_panel_column_t *
docker_get_columns (void *plugin_data, size_t *count)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
        return docker_containers_get_columns (count);
    if (data->view == DOCKER_VIEW_VOLUMES)
        return docker_volumes_get_columns (count);

    if (count != NULL)
        *count = 0;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
docker_get_column_value (void *plugin_data, const char *fname, const char *column_id)
{
    docker_data_t *data = (docker_data_t *) plugin_data;
    const docker_item_t *item;

    if (data->view != DOCKER_VIEW_CONTAINERS_ITEMS && data->view != DOCKER_VIEW_VOLUMES)
        return NULL;

    if (fname == NULL || column_id == NULL)
        return NULL;

    item = find_item_by_name (data, fname);
    if (item == NULL)
        return NULL;

    if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
        return docker_containers_get_column_value (data, fname, column_id);
    else if (data->view == DOCKER_VIEW_VOLUMES)
        return docker_volumes_get_column_value (data, fname, column_id);

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
docker_get_default_format (void *plugin_data)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
        return docker_containers_get_default_format ();
    if (data->view == DOCKER_VIEW_VOLUMES)
        return docker_volumes_get_default_format ();
    if (data->view == DOCKER_VIEW_CONTAINER_MOUNTS)
        return docker_container_files_get_default_format (data);

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_handle_key (void *plugin_data, int key)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    if (data->view == DOCKER_VIEW_PROFILES && (key == CK_Move || key == CK_MoveSingle))
        return MC_PPR_NOT_SUPPORTED;

    if (data->view == DOCKER_VIEW_PROFILES
        && (key == CK_Edit || key == CK_Copy || key == CK_CopySingle))
    {
        const GString *cur = data->host->get_current (data->host);
        docker_connection_t *conn;

        if (cur == NULL || cur->len == 0)
            return MC_PPR_NOT_SUPPORTED;

        conn = docker_connections_find_by_label (data->connections, cur->str);
        if (conn == NULL)
            return MC_PPR_NOT_SUPPORTED;

        if (key == CK_Edit)
        {
            /* Work on a clone so conn is not touched until all validation passes */
            docker_connection_t *edited = docker_connection_clone (conn);

            if (docker_ui_show_connection_dialog (edited, FALSE, data->help_filename))
            {
                /* Verify the new label is unique (exclude this conn itself) */
                gboolean label_ok = TRUE;
                guint j;

                for (j = 0; j < data->connections->len; j++)
                {
                    const docker_connection_t *other =
                        (const docker_connection_t *) g_ptr_array_index (data->connections, j);

                    if (other != conn && other->label != NULL && edited->label != NULL
                        && strcmp (other->label, edited->label) == 0)
                    {
                        label_ok = FALSE;
                        break;
                    }
                }

                if (label_ok)
                {
                    /* Apply all edited fields to the live connection at once */
                    g_free (conn->label);
                    conn->label = edited->label;
                    edited->label = NULL;

                    conn->type = edited->type;

                    g_free (conn->docker_path);
                    conn->docker_path = edited->docker_path;
                    edited->docker_path = NULL;

                    g_free (conn->host);
                    conn->host = edited->host;
                    edited->host = NULL;

                    g_free (conn->user);
                    conn->user = edited->user;
                    edited->user = NULL;

                    conn->port = edited->port;

                    g_free (conn->key_path);
                    conn->key_path = edited->key_path;
                    edited->key_path = NULL;

                    if (!docker_connections_save (data->connections_file, data->connections))
                        message (D_ERROR, MSG_ERROR, "%s",
                                 _ ("Failed to save connection profiles."));
                }
                else
                    message (D_ERROR, MSG_ERROR, "%s",
                             _ ("A profile with this label already exists."));
            }

            docker_connection_free (edited);
            return MC_PPR_OK;
        }

        /* Clone (CK_Copy / CK_CopySingle) */
        {
            docker_connection_t *clone = docker_connection_clone (conn);
            guint n = data->connections->len + 1;

            /* Generate unique id */
            do
            {
                g_free (clone->id);
                clone->id = g_strdup_printf ("conn-%u", n++);
            }
            while (docker_connections_find_by_id (data->connections, clone->id) != NULL);

            /* Make label unique */
            {
                char *base_label =
                    g_strdup_printf ("%s (copy)", conn->label != NULL ? conn->label : conn->id);
                char *new_label = g_strdup (base_label);
                int suffix = 2;

                while (docker_connections_find_by_label (data->connections, new_label) != NULL)
                {
                    g_free (new_label);
                    new_label = g_strdup_printf ("%s %d", base_label, suffix++);
                }
                g_free (clone->label);
                clone->label = new_label;
                g_free (base_label);
            }

            if (docker_ui_show_connection_dialog (clone, TRUE, data->help_filename))
            {
                /* Verify label uniqueness after user may have renamed the clone */
                gboolean label_ok = TRUE;
                guint j;

                for (j = 0; j < data->connections->len; j++)
                {
                    const docker_connection_t *other =
                        (const docker_connection_t *) g_ptr_array_index (data->connections, j);

                    if (other->label != NULL && clone->label != NULL
                        && strcmp (other->label, clone->label) == 0)
                    {
                        label_ok = FALSE;
                        break;
                    }
                }

                if (label_ok)
                {
                    g_ptr_array_add (data->connections, clone);
                    if (!docker_connections_save (data->connections_file, data->connections))
                        message (D_ERROR, MSG_ERROR, "%s",
                                 _ ("Failed to save connection profiles."));
                }
                else
                {
                    message (D_ERROR, MSG_ERROR, "%s",
                             _ ("A profile with this label already exists."));
                    docker_connection_free (clone);
                }
            }
            else
                docker_connection_free (clone);
        }
        return MC_PPR_OK;
    }

    if (key != CK_Refresh && key != CK_Reread)
        return MC_PPR_NOT_SUPPORTED;

    nav_stack_clear (data);
    clear_container_list_caches (data);
    clear_files_cache (data);

    if (data->view == DOCKER_VIEW_CONTAINER_FILES)
        clear_items (data);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_create_item (void *plugin_data)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    if (data->view == DOCKER_VIEW_PROFILES)
    {
        /* Create a new connection profile */
        docker_connection_t *conn;
        char *new_id;
        guint n;

        conn = docker_connection_new_local ();

        /* Generate a unique id */
        n = data->connections->len + 1;
        do
        {
            g_free (conn->id);
            conn->id = g_strdup_printf ("conn-%u", n++);
        }
        while (docker_connections_find_by_id (data->connections, conn->id) != NULL);

        g_free (conn->label);
        conn->label = g_strdup (_ ("New Connection"));

        new_id = g_strdup (conn->id); /* save before dialog may modify */

        if (!docker_ui_show_connection_dialog (conn, TRUE, data->help_filename))
        {
            g_free (new_id);
            docker_connection_free (conn);
            return MC_PPR_OK; /* cancelled */
        }

        /* Check label uniqueness before adding */
        {
            gboolean label_ok = TRUE;
            guint j;

            for (j = 0; j < data->connections->len; j++)
            {
                const docker_connection_t *other =
                    (const docker_connection_t *) g_ptr_array_index (data->connections, j);

                if (other->label != NULL && conn->label != NULL
                    && strcmp (other->label, conn->label) == 0)
                {
                    label_ok = FALSE;
                    break;
                }
            }

            if (!label_ok)
            {
                message (D_ERROR, MSG_ERROR, "%s", _ ("A profile with this label already exists."));
                docker_connection_free (conn);
                g_free (new_id);
                return MC_PPR_OK;
            }
        }

        g_ptr_array_add (data->connections, conn);
        if (!docker_connections_save (data->connections_file, data->connections))
            message (D_ERROR, MSG_ERROR, "%s", _ ("Failed to save connection profiles."));
        g_free (new_id);
        return MC_PPR_OK;
    }

    if (data->view != DOCKER_VIEW_CONTAINERS_ITEMS && data->view != DOCKER_VIEW_CONTAINERS_PROJECTS)
        return MC_PPR_NOT_SUPPORTED;
    {
        mc_pp_result_t result = docker_containers_create_item (data);

        if (result == MC_PPR_OK)
        {
            clear_container_list_caches (data);
            reload_items (data);
        }
        return result;
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *mc_panel_plugin_register (void);

const mc_panel_plugin_t *
mc_panel_plugin_register (void)
{
    return &docker_plugin;
}

/* --------------------------------------------------------------------------------------------- */
