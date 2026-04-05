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
#include "src/viewer/mcviewer.h"

/*** forward declarations (file scope functions) *************************************************/

static void *docker_open (mc_panel_host_t *host, const char *open_path);
static void docker_close (void *plugin_data);
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

    .get_columns = docker_get_columns,
    .get_column_value = docker_get_column_value,
    .get_default_format = docker_get_default_format,
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

void
clear_files_cache (docker_data_t *data)
{
    if (data->files_cache != NULL)
        g_hash_table_remove_all (data->files_cache);
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

char *
docker_capture_output (const char *cmd)
{
    char *output = NULL;
    char *err_text = NULL;

    if (!run_cmd (cmd, &output, &err_text))
    {
        g_free (output);
        output = NULL;
    }

    g_free (err_text);
    return strip_trailing_newlines (output);
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_capture_inspect_field (const char *container_id, const char *format)
{
    char *quoted_id;
    char *quoted_format;
    char *cmd;
    char *value;

    quoted_id = g_shell_quote (container_id);
    quoted_format = g_shell_quote (format);
    cmd = g_strdup_printf ("docker inspect --format %s %s", quoted_format, quoted_id);
    value = docker_capture_output (cmd);

    g_free (cmd);
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
        if (!docker_containers_reload_projects (data, &err_text))
            goto cmd_failed;
        break;

    case DOCKER_VIEW_CONTAINERS_ITEMS:
        if (!docker_containers_reload_items (data, &err_text))
            goto cmd_failed;
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
    g_free (data->title_buf);

    switch (data->view)
    {
    case DOCKER_VIEW_ROOT:
        data->title_buf = g_strdup ("/");
        break;

    case DOCKER_VIEW_CONTAINERS_PROJECTS:
        data->title_buf = g_strdup ("/containers");
        break;

    case DOCKER_VIEW_CONTAINERS_ITEMS:
        data->title_buf = g_strdup_printf (
            "/containers/%s",
            data->current_project != NULL ? data->current_project : docker_ungrouped_project);
        break;

    case DOCKER_VIEW_CONTAINER_DETAILS:
        data->title_buf = g_strdup_printf (
            "/containers/%s/%s",
            data->current_project != NULL ? data->current_project : docker_ungrouped_project,
            data->current_container_name != NULL ? data->current_container_name : "container");
        break;

    case DOCKER_VIEW_CONTAINER_FILES:
        if (data->files_cwd == NULL || strcmp (data->files_cwd, "/") == 0)
            data->title_buf = g_strdup_printf (
                "/containers/%s/%s/files",
                data->current_project != NULL ? data->current_project : docker_ungrouped_project,
                data->current_container_name != NULL ? data->current_container_name : "container");
        else
            data->title_buf = g_strdup_printf (
                "/containers/%s/%s/files%s",
                data->current_project != NULL ? data->current_project : docker_ungrouped_project,
                data->current_container_name != NULL ? data->current_container_name : "container",
                data->files_cwd);
        break;

    case DOCKER_VIEW_CONTAINER_MOUNTS:
        data->title_buf = g_strdup_printf (
            "/containers/%s/%s/mounts",
            data->current_project != NULL ? data->current_project : docker_ungrouped_project,
            data->current_container_name != NULL ? data->current_container_name : "container");
        break;

    case DOCKER_VIEW_IMAGES:
        data->title_buf = g_strdup ("/images");
        break;

    case DOCKER_VIEW_VOLUMES:
        data->title_buf = g_strdup ("/volumes");
        break;

    case DOCKER_VIEW_NETWORKS:
        data->title_buf = g_strdup ("/networks");
        break;

    default:
        data->title_buf = g_strdup ("/");
        break;
    }

    return data->title_buf;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
docker_apply_open_path (docker_data_t *data, const char *open_path)
{
    char *normalized_path;
    g_debug ("docker_apply_open_path input='%s'", open_path ? open_path : "(null)");
    const char *path;
    const char *rest;
    const char *slash;

    normalized_path = docker_normalize_open_path (open_path);
    if (normalized_path == NULL)
        return TRUE;

    path = normalized_path;

    if (strcmp (path, "/containers") == 0)
    {
        data->view = DOCKER_VIEW_CONTAINERS_PROJECTS;
        g_free (normalized_path);
        return TRUE;
    }

    if (strcmp (path, "/images") == 0)
    {
        data->view = DOCKER_VIEW_IMAGES;
        g_free (normalized_path);
        return TRUE;
    }

    if (strcmp (path, "/volumes") == 0)
    {
        data->view = DOCKER_VIEW_VOLUMES;
        g_free (normalized_path);
        return TRUE;
    }

    if (strcmp (path, "/networks") == 0)
    {
        data->view = DOCKER_VIEW_NETWORKS;
        g_free (normalized_path);
        return TRUE;
    }

    if (strcmp (path, "/") == 0)
    {
        data->view = DOCKER_VIEW_ROOT;
        g_free (normalized_path);
        return TRUE;
    }

    if (!g_str_has_prefix (path, "/containers/"))
    {
        g_debug ("docker_apply_open_path: unsupported normalized path='%s'", path);
        g_free (normalized_path);
        return FALSE;
    }

    rest = path + strlen ("/containers/");
    if (*rest == '\0')
    {
        data->view = DOCKER_VIEW_CONTAINERS_PROJECTS;
        g_free (normalized_path);
        return TRUE;
    }

    slash = strchr (rest, '/');
    if (slash == NULL)
    {
        data->current_project = g_strdup (rest);
        data->view = DOCKER_VIEW_CONTAINERS_ITEMS;
        g_free (normalized_path);
        return TRUE;
    }

    if (slash == rest)
    {
        g_debug ("docker_apply_open_path: empty project segment path='%s'", path);
        g_free (normalized_path);
        return FALSE;
    }

    data->current_project = g_strndup (rest, (gsize) (slash - rest));
    rest = slash + 1;
    if (*rest == '\0')
    {
        data->view = DOCKER_VIEW_CONTAINERS_ITEMS;
        g_free (normalized_path);
        return TRUE;
    }

    slash = strchr (rest, '/');
    if (slash == NULL)
    {
        data->current_container_name = g_strdup (rest);
        data->view = DOCKER_VIEW_CONTAINER_DETAILS;
        g_free (normalized_path);
        return TRUE;
    }

    if (slash == rest)
    {
        g_debug ("docker_apply_open_path: empty container segment path='%s'", path);
        g_free (normalized_path);
        return FALSE;
    }

    data->current_container_name = g_strndup (rest, (gsize) (slash - rest));
    rest = slash + 1;

    if (strcmp (rest, docker_mounts_entry) == 0)
    {
        data->view = DOCKER_VIEW_CONTAINER_MOUNTS;
        g_free (normalized_path);
        return TRUE;
    }

    if (strcmp (rest, docker_files_entry) == 0)
    {
        data->view = DOCKER_VIEW_CONTAINER_FILES;
        data->files_cwd = g_strdup ("/");
        g_free (normalized_path);
        return TRUE;
    }

    if (g_str_has_prefix (rest, "files/"))
    {
        data->view = DOCKER_VIEW_CONTAINER_FILES;
        data->files_cwd = g_strdup_printf ("/%s", rest + strlen ("files/"));
        g_free (normalized_path);
        return TRUE;
    }

    g_debug ("docker_apply_open_path: unsupported container subpath path='%s' rest='%s'", path,
             rest);
    g_free (normalized_path);
    return FALSE;
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
    data->view = DOCKER_VIEW_ROOT;
    data->items = NULL;
    data->root_focus = NULL;
    data->current_project = NULL;
    data->current_container_id = NULL;
    data->current_container_name = NULL;
    data->files_cache =
        g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);
    data->title_buf = NULL;

    if (!docker_apply_open_path (data, open_path))
    {
        const char *rest_dbg = NULL;

        if (open_path != NULL && docker_plugin.prefix != NULL
            && g_str_has_prefix (open_path, docker_plugin.prefix))
        {
            rest_dbg = open_path + strlen (docker_plugin.prefix);
        }

        g_debug ("docker_open: reopen failed open_path='%s', prefix='%s', rest='%s'",
                 open_path != NULL ? open_path : "(null)",
                 docker_plugin.prefix != NULL ? docker_plugin.prefix : "(null)",
                 rest_dbg != NULL ? rest_dbg : "(null)");

        docker_close (data);
        if (open_path != NULL && docker_plugin.prefix != NULL
            && g_str_has_prefix (open_path, docker_plugin.prefix))
        {
            const char *rest = open_path + strlen (docker_plugin.prefix);

            while (*rest == '/')
                rest++;
            if (*rest == '\0')
                return data;
        }

        if (host != NULL && host->message != NULL)
            host->message (host, D_ERROR, "Docker", "Cannot reopen Docker panel path.");
        return NULL;
    }

    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_close (void *plugin_data)
{
    docker_data_t *data = (docker_data_t *) plugin_data;

    clear_items (data);
    g_free (data->root_focus);
    g_free (data->current_project);
    g_free (data->current_container_id);
    g_free (data->current_container_name);
    g_free (data->files_cwd);
    if (data->files_cache != NULL)
        g_hash_table_destroy (data->files_cache);
    g_free (data->title_buf);
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
docker_get_items (void *plugin_data, void *list_ptr)
{
    docker_data_t *data = (docker_data_t *) plugin_data;
    guint idx;

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
        case DOCKER_VIEW_ROOT:
            return MC_PPR_CLOSE; /* close plugin */

        case DOCKER_VIEW_CONTAINERS_PROJECTS:
            g_free (data->root_focus);
            data->root_focus = g_strdup ("containers");
            set_view (data, DOCKER_VIEW_ROOT);
            return MC_PPR_OK;

        case DOCKER_VIEW_IMAGES:
            g_free (data->root_focus);
            data->root_focus = g_strdup ("images");
            set_view (data, DOCKER_VIEW_ROOT);
            return MC_PPR_OK;

        case DOCKER_VIEW_VOLUMES:
            g_free (data->root_focus);
            data->root_focus = g_strdup ("volumes");
            set_view (data, DOCKER_VIEW_ROOT);
            return MC_PPR_OK;

        case DOCKER_VIEW_NETWORKS:
            g_free (data->root_focus);
            data->root_focus = g_strdup ("networks");
            set_view (data, DOCKER_VIEW_ROOT);
            return MC_PPR_OK;

        case DOCKER_VIEW_CONTAINERS_ITEMS:
            set_view (data, DOCKER_VIEW_CONTAINERS_PROJECTS);
            reload_items (data);
            return MC_PPR_OK;

        case DOCKER_VIEW_CONTAINER_DETAILS:
            set_view (data, DOCKER_VIEW_CONTAINERS_ITEMS);
            reload_items (data);
            return MC_PPR_OK;

        case DOCKER_VIEW_CONTAINER_FILES:
        {
            /* go up one level inside container, or back to details if at root */
            char *parent;

            parent = mc_pp_path_up (data->files_cwd);
            if (parent == NULL)
            {
                /* already at / -- go back to container details */
                g_free (data->files_cwd);
                data->files_cwd = NULL;
                set_view (data, DOCKER_VIEW_CONTAINER_DETAILS);
                reload_items (data);
                return MC_PPR_OK;
            }

            g_free (data->files_cwd);
            data->files_cwd = parent;
            set_view (data, DOCKER_VIEW_CONTAINER_FILES);
            reload_items (data);
            return MC_PPR_OK;
        }

        case DOCKER_VIEW_CONTAINER_MOUNTS:
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
        mc_pp_result_t result = docker_container_files_chdir (data, path);

        if (result == MC_PPR_OK)
            reload_items (data);
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
    if (data->view == DOCKER_VIEW_ROOT)
    {
        docker_view_t next = view_from_root_path (name);

        if (next != DOCKER_VIEW_ROOT)
        {
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
        mc_pp_result_t result = docker_containers_enter (data, name);

        if (result == MC_PPR_OK
            && (strcmp (name, docker_files_entry) == 0 || strcmp (name, docker_mounts_entry) == 0))
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
        mc_pp_result_t result = docker_container_files_enter_mounts (data, name);

        if (result == MC_PPR_OK)
            reload_items (data);
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
            cmd = "docker info";
        else if (strcmp (fname, docker_version_file) == 0)
            cmd = "docker version";
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
        cmd_dynamic = g_strdup_printf ("docker inspect %s", quoted_id);
        g_free (quoted_id);
        cmd = cmd_dynamic;
    }

    ok = run_cmd (cmd, &output, &err_text);
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

    if (data->view == DOCKER_VIEW_ROOT || data->view == DOCKER_VIEW_CONTAINERS_PROJECTS
        || data->view == DOCKER_VIEW_CONTAINER_DETAILS)
        return MC_PPR_NOT_SUPPORTED;

    if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
    {
        mc_pp_result_t result = docker_containers_delete_items (data, names, count);

        if (result == MC_PPR_OK)
            reload_items (data);
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
    return docker_get_path (data);
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

    if (key != CK_Refresh && key != CK_Reread)
        return MC_PPR_NOT_SUPPORTED;

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
    if (data->view != DOCKER_VIEW_CONTAINERS_ITEMS && data->view != DOCKER_VIEW_CONTAINERS_PROJECTS)
        return MC_PPR_NOT_SUPPORTED;
    {
        mc_pp_result_t result = docker_containers_create_item (data);

        if (result == MC_PPR_OK)
            reload_items (data);
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
