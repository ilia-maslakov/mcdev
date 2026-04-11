/*
   Docker volume domain logic.

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

static const mc_panel_column_t docker_volume_columns[] = {
    { "state", "S", 1, FALSE, J_LEFT_FIT, TRUE },
    { "scope", "Scope", 5, FALSE, J_LEFT_FIT, TRUE },
};

static char *docker_load_container_mounts_output (docker_data_t *data);
static GPtrArray *parse_volume_items (const char *output, const char *mounts_output);

/* --------------------------------------------------------------------------------------------- */

static char *
docker_load_container_mounts_output (docker_data_t *data)
{
    char *ids_output = NULL;
    char *ids_err = NULL;
    char *mounts_output = NULL;
    char *mounts_err = NULL;
    char **lines;
    GString *cmd;
    int i;

    if (!docker_conn_run (data->active_conn, "ps -aq", &ids_output, &ids_err))
    {
        g_free (ids_output);
        g_free (ids_err);
        return NULL;
    }
    g_free (ids_err);

    if (ids_output == NULL || *ids_output == '\0')
    {
        g_free (ids_output);
        return NULL;
    }

    lines = g_strsplit (ids_output, "\n", -1);
    cmd = g_string_new ("inspect --format ");
    g_string_append (cmd, "'{{.Name}} {{range .Mounts}}{{.Type}}:{{.Name}} {{end}}'");

    for (i = 0; lines[i] != NULL; i++)
    {
        char *id = g_strstrip (lines[i]);
        char *quoted_id;

        if (*id == '\0')
            continue;

        quoted_id = g_shell_quote (id);
        g_string_append_c (cmd, ' ');
        g_string_append (cmd, quoted_id);
        g_free (quoted_id);
    }

    (void) docker_conn_run (data->active_conn, cmd->str, &mounts_output, &mounts_err);
    g_free (mounts_err);
    g_string_free (cmd, TRUE);
    g_strfreev (lines);
    g_free (ids_output);

    return mounts_output;
}

/* --------------------------------------------------------------------------------------------- */

static GPtrArray *
parse_volume_items (const char *output, const char *mounts_output)
{
    GPtrArray *items;
    GHashTable *used;
    char **lines;
    int i;

    items = g_ptr_array_new_with_free_func (docker_item_free);
    if (output == NULL)
        return items;

    used = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    if (mounts_output != NULL && *mounts_output != '\0')
    {
        char **mount_lines = g_strsplit (mounts_output, "\n", -1);
        int mi;

        for (mi = 0; mount_lines[mi] != NULL; mi++)
        {
            char **parts;
            int mj;

            if (mount_lines[mi][0] == '\0')
                continue;

            parts = g_strsplit (mount_lines[mi], " ", -1);
            for (mj = 0; parts[mj] != NULL; mj++)
            {
                char *entry = g_strstrip (parts[mj]);

                if (g_str_has_prefix (entry, "volume:") && entry[7] != '\0')
                    g_hash_table_add (used, g_strdup (entry + 7));
            }
            g_strfreev (parts);
        }

        g_strfreev (mount_lines);
    }

    lines = g_strsplit (output, "\n", -1);

    for (i = 0; lines[i] != NULL; i++)
    {
        char **parts;
        docker_item_t *item;

        if (lines[i][0] == '\0')
            continue;

        parts = g_strsplit (lines[i], "\t", 4);
        if (parts[0] == NULL || parts[1] == NULL || parts[2] == NULL || parts[3] == NULL)
        {
            g_strfreev (parts);
            continue;
        }

        item = g_new0 (docker_item_t, 1);
        item->id = g_strdup (parts[0]);
        item->name = g_strdup (parts[0]);
        item->status = g_strdup (g_hash_table_contains (used, parts[0]) ? "in use" : "unused");
        item->driver = g_strdup (parts[1]);
        item->scope = g_strdup (parts[2]);

        g_ptr_array_add (items, item);
        g_strfreev (parts);
    }

    g_strfreev (lines);
    g_hash_table_destroy (used);
    return items;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_volumes_reload (docker_data_t *data, char **err_text)
{
    char *output = NULL;
    char *mounts_output = NULL;
    gboolean ok;

    ok = docker_conn_run (data->active_conn,
                          "volume ls --format '{{.Name}}\\t{{.Driver}}\\t{{.Scope}}\\t{{.Status}}'",
                          &output, err_text);
    if (!ok)
    {
        g_free (output);
        return FALSE;
    }

    mounts_output = docker_load_container_mounts_output (data);
    data->items = parse_volume_items (output, mounts_output);

    g_free (mounts_output);
    g_free (output);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_volumes_view_summary (docker_data_t *data, const char *fname)
{
    const docker_item_t *volume = find_item_by_name (data, fname);
    char *name = NULL;
    char *scope = NULL;
    char *mountpoint = NULL;
    char *created = NULL;
    char *labels = NULL;
    char *options = NULL;
    docker_volume_summary_t summary;
    gboolean ok = FALSE;

    if (volume == NULL || volume->id == NULL)
        return FALSE;

    name = docker_conn_capture_inspect (data->active_conn, volume->id, "{{.Name}}");
    scope = docker_conn_capture_inspect (data->active_conn, volume->id, "{{.Scope}}");
    mountpoint = docker_conn_capture_inspect (data->active_conn, volume->id, "{{.Mountpoint}}");
    created = docker_conn_capture_inspect (data->active_conn, volume->id, "{{.CreatedAt}}");
    labels = docker_conn_capture_inspect (
        data->active_conn, volume->id,
        "{{if .Labels}}{{range $k, $v := .Labels}}{{$k}}={{$v}}{{println}}{{end}}{{else}}-{{end}}");
    options =
        docker_conn_capture_inspect (data->active_conn, volume->id,
                                     "{{if .Options}}{{range $k, $v := "
                                     ".Options}}{{$k}}={{$v}}{{println}}{{end}}{{else}}-{{end}}");

    summary.name = name != NULL ? name : volume->name;
    summary.scope = scope != NULL ? scope : volume->scope;
    summary.mountpoint = mountpoint;
    summary.created = created != NULL ? created : volume->created;
    summary.state = volume->status;
    summary.labels = labels;
    summary.options = options;

    ok = docker_ui_view_volume_summary (&summary);

    g_free (name);
    g_free (scope);
    g_free (mountpoint);
    g_free (created);
    g_free (labels);
    g_free (options);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_volumes_delete_items (docker_data_t *data, const char **names, int count)
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
        cmd = g_strdup_printf ("volume rm %s", quoted);

        if (!docker_conn_run (data->active_conn, cmd, &output, &err_text) && err_text != NULL
            && err_text[0] != '\0')
            message (D_ERROR, MSG_ERROR, "%s", err_text);

        g_free (output);
        g_free (err_text);
        g_free (cmd);
        g_free (quoted);
    }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

const mc_panel_column_t *
docker_volumes_get_columns (size_t *count)
{
    if (count != NULL)
        *count = G_N_ELEMENTS (docker_volume_columns);
    return docker_volume_columns;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_volumes_get_column_value (docker_data_t *data, const char *fname, const char *column_id)
{
    const docker_item_t *item;

    if (fname == NULL || column_id == NULL)
        return NULL;

    item = find_item_by_name (data, fname);
    if (item == NULL)
        return NULL;

    if (strcmp (column_id, "state") == 0)
        return item->status != NULL && strcmp (item->status, "in use") == 0 ? "U" : "N";
    if (strcmp (column_id, "scope") == 0)
        return item->scope;

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_volumes_get_default_format (void)
{
    return "name | state:1 | scope:5";
}
