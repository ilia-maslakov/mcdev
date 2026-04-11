/*
   Docker container domain logic.

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
#include <unistd.h>

#include "src/panel-plugins/docker/docker-internal.h"

static const mc_panel_column_t docker_columns[] = {
    { "status", "Status", 12, FALSE, J_LEFT_FIT, TRUE },
    { "image", "Image", 20, FALSE, J_LEFT_FIT, TRUE },
    { "ports", "Ports", 12, FALSE, J_LEFT_FIT, TRUE },
};

static gboolean docker_project_match (const char *selected_project,
                                      const char *project_from_docker);
static gboolean docker_load_containers_output (docker_data_t *data, char **output, char **err_text);
static GPtrArray *parse_projects_from_containers (const char *output, const char *focused_project);
static GPtrArray *parse_container_items_from_project (const char *output, const char *project_name,
                                                      const char *focused_container_id);
static char *docker_format_ports (const char *raw_ports);
static gboolean docker_parse_port_range (const char *text, int *start, int *end);
static void docker_append_formatted_port (GString *out, const char *host_port,
                                          const char *container_port);
static char *docker_format_port_mappings (const char *ports);
static char *docker_capture_stats_block (docker_data_t *data, const char *container_id);
static const char *docker_normalize_zero_value (const char *value, const char *replacement);
static gboolean docker_view_container_summary (docker_data_t *data, const docker_item_t *container);

gboolean
docker_containers_is_ungrouped_project (const char *project)
{
    return (project == NULL || project[0] == '\0'
            || strcmp (project, docker_ungrouped_project) == 0);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
docker_project_match (const char *selected_project, const char *project_from_docker)
{
    if (docker_containers_is_ungrouped_project (selected_project))
        return docker_containers_is_ungrouped_project (project_from_docker);

    return (project_from_docker != NULL && strcmp (selected_project, project_from_docker) == 0);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
docker_load_containers_output (docker_data_t *data, char **output, char **err_text)
{
    return docker_conn_run (
        data->active_conn,
        "ps -a --format '{{.ID}}\\t{{.Names}}\\t{{.Status}}\\t{{.Image}}\\t{{.Ports}}\\t"
        "{{.Label \"com.docker.compose.project\"}}'",
        output, err_text);
}

/* --------------------------------------------------------------------------------------------- */

static GPtrArray *
parse_projects_from_containers (const char *output, const char *focused_project)
{
    GPtrArray *items;
    GHashTable *seen;
    char **lines;
    int i;

    items = g_ptr_array_new_with_free_func (docker_item_free);
    seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    if (output == NULL)
        goto done;

    lines = g_strsplit (output, "\n", -1);

    for (i = 0; lines[i] != NULL; i++)
    {
        char **parts;
        int part_count = 0;
        const char *project;
        char *project_key;

        if (lines[i][0] == '\0')
            continue;

        parts = g_strsplit (lines[i], "\t", -1);
        while (parts[part_count] != NULL)
            part_count++;

        if (part_count < 6)
        {
            g_strfreev (parts);
            continue;
        }

        project = parts[5];
        project_key = docker_containers_is_ungrouped_project (project)
            ? g_strdup (docker_ungrouped_project)
            : g_strdup (project);

        if (!g_hash_table_contains (seen, project_key))
        {
            docker_item_t *item = g_new0 (docker_item_t, 1);

            item->id = g_strdup (project_key);
            item->name = g_strdup (project_key);
            item->is_dir = TRUE;

            if (focused_project != NULL && strcmp (item->id, focused_project) == 0)
                g_ptr_array_insert (items, 0, item);
            else
                g_ptr_array_add (items, item);
            g_hash_table_add (seen, project_key);
        }
        else
            g_free (project_key);

        g_strfreev (parts);
    }

    g_strfreev (lines);

done:
    if (!g_hash_table_contains (seen, docker_ungrouped_project))
    {
        docker_item_t *item = g_new0 (docker_item_t, 1);

        item->id = g_strdup (docker_ungrouped_project);
        item->name = g_strdup (docker_ungrouped_project);
        item->is_dir = TRUE;
        g_ptr_array_add (items, item);
    }

    g_hash_table_destroy (seen);
    return items;
}

/* --------------------------------------------------------------------------------------------- */

static GPtrArray *
parse_container_items_from_project (const char *output, const char *project_name,
                                    const char *focused_container_id)
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
        char **parts;
        int part_count = 0;
        const char *project;

        if (lines[i][0] == '\0')
            continue;

        parts = g_strsplit (lines[i], "\t", -1);
        while (parts[part_count] != NULL)
            part_count++;

        if (part_count < 6)
        {
            g_strfreev (parts);
            continue;
        }

        project = parts[5];
        if (!docker_project_match (project_name, project))
        {
            g_strfreev (parts);
            continue;
        }

        {
            docker_item_t *item = g_new0 (docker_item_t, 1);

            item->id = g_strdup (parts[0]);
            item->name = g_strdup (parts[1]);
            item->is_dir = TRUE;
            item->status = g_strdup (parts[2]);
            item->image = g_strdup (parts[3]);
            item->ports = docker_format_ports (parts[4]);

            if (focused_container_id != NULL && strcmp (item->id, focused_container_id) == 0)
                g_ptr_array_insert (items, 0, item);
            else
                g_ptr_array_add (items, item);
        }

        g_strfreev (parts);
    }

    g_strfreev (lines);
    return items;
}

/* --------------------------------------------------------------------------------------------- */

static char *
docker_format_ports (const char *raw_ports)
{
    char **parts;
    int i;
    GString *out;

    if (raw_ports == NULL || *raw_ports == '\0')
        return g_strdup ("");

    parts = g_strsplit (raw_ports, ",", -1);
    out = g_string_new (NULL);

    for (i = 0; parts[i] != NULL; i++)
    {
        char *part;
        char *arrow;
        char *lhs;
        char *rhs;
        char *host_port;
        char *container_port;

        part = g_strstrip (parts[i]);
        if (*part == '\0')
            continue;

        arrow = strstr (part, "->");
        if (arrow == NULL)
            continue;

        lhs = g_strndup (part, (gsize) (arrow - part));
        rhs = g_strdup (arrow + 2);
        g_strstrip (lhs);
        g_strstrip (rhs);

        host_port = strrchr (lhs, ':');
        if (host_port != NULL)
            host_port++;
        else
            host_port = lhs;

        container_port = rhs;
        while (*container_port == '[' || *container_port == ']')
            container_port++;
        {
            char *slash = strchr (container_port, '/');

            if (slash != NULL)
                *slash = '\0';
        }
        {
            char *colon = strchr (container_port, ':');

            if (colon != NULL)
                container_port = colon + 1;
        }

        if (*host_port != '\0' && *container_port != '\0')
            docker_append_formatted_port (out, host_port, container_port);

        g_free (lhs);
        g_free (rhs);
    }

    g_strfreev (parts);

    if (out->len == 0)
    {
        g_string_free (out, TRUE);
        return g_strdup (raw_ports);
    }

    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
docker_parse_port_range (const char *text, int *start, int *end)
{
    char *dash;
    char *endptr;

    if (text == NULL || *text == '\0')
        return FALSE;

    dash = strchr (text, '-');
    if (dash == NULL)
    {
        long value;

        value = strtol (text, &endptr, 10);
        if (*text == '\0' || *endptr != '\0')
            return FALSE;
        *start = (int) value;
        *end = (int) value;
        return TRUE;
    }

    {
        char *start_text = g_strndup (text, (gsize) (dash - text));
        long start_value;
        long end_value;

        start_value = strtol (start_text, &endptr, 10);
        g_free (start_text);
        if (*endptr != '\0')
            return FALSE;

        end_value = strtol (dash + 1, &endptr, 10);
        if (*(dash + 1) == '\0' || *endptr != '\0' || start_value > end_value)
            return FALSE;

        *start = (int) start_value;
        *end = (int) end_value;
        return TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_append_formatted_port (GString *out, const char *host_port, const char *container_port)
{
    int host_start, host_end;
    int container_start, container_end;

    if (*host_port == '\0' || *container_port == '\0')
        return;

    if (docker_parse_port_range (host_port, &host_start, &host_end)
        && docker_parse_port_range (container_port, &container_start, &container_end)
        && (host_end - host_start) == (container_end - container_start))
    {
        int i;
        int count = host_end - host_start + 1;

        for (i = 0; i < count; i++)
        {
            if (out->len > 0)
                g_string_append_c (out, ',');
            g_string_append_printf (out, "%d:%d", host_start + i, container_start + i);
        }
        return;
    }

    if (out->len > 0)
        g_string_append_c (out, ',');
    g_string_append_printf (out, "%s:%s", host_port, container_port);
}

/* --------------------------------------------------------------------------------------------- */

static char *
docker_format_port_mappings (const char *ports)
{
    GString *buf;
    const char *p;

    if (ports == NULL || *ports == '\0')
        return NULL;

    buf = g_string_new (NULL);
    for (p = ports; *p != '\0'; p++)
    {
        if (*p == ',')
        {
            g_string_append (buf, ", ");
            while (p[1] == ' ' || p[1] == '\t')
                p++;
            continue;
        }

        if (*p == '\n' || *p == '\r')
            continue;

        g_string_append_c (buf, *p);
    }

    return g_string_free (buf, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static char *
docker_capture_stats_block (docker_data_t *data, const char *container_id)
{
    char *quoted_id;
    char *docker_args;
    char *value;

    quoted_id = g_shell_quote (container_id);
    docker_args = g_strdup_printf (
        "stats --no-stream --format "
        "'CPU: {{.CPUPerc}}\\nMemory usage: {{.MemUsage}}\\nNetwork I/O: {{.NetIO}}\\n"
        "Block I/O: {{.BlockIO}}\\nPIDs: {{.PIDs}}' %s 2>/dev/null",
        quoted_id);
    value = docker_conn_capture (data->active_conn, docker_args);

    g_free (docker_args);
    g_free (quoted_id);

    return value;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
docker_normalize_zero_value (const char *value, const char *replacement)
{
    if (value == NULL || *value == '\0')
        return replacement;

    return strcmp (value, "0") == 0 ? replacement : value;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
docker_view_container_summary (docker_data_t *data, const docker_item_t *container)
{
    char *name = NULL;
    char *id = NULL;
    char *image = NULL;
    char *status = NULL;
    char *started = NULL;
    char *restart_count = NULL;
    char *command = NULL;
    char *entrypoint = NULL;
    char *ports = NULL;
    char *ip = NULL;
    char *mounts = NULL;
    char *memory_limit = NULL;
    char *cpu_shares = NULL;
    char *stats = NULL;
    char *formatted_ports = NULL;
    docker_container_summary_t summary;
    gboolean ok = FALSE;

    if (container == NULL || container->id == NULL)
        return FALSE;

    name = docker_conn_capture_inspect (data->active_conn, container->id, "{{.Name}}");
    id = docker_conn_capture_inspect (data->active_conn, container->id, "{{.Id}}");
    image = docker_conn_capture_inspect (data->active_conn, container->id, "{{.Config.Image}}");
    status = g_strdup (container->status);
    started =
        docker_conn_capture_inspect (data->active_conn, container->id, "{{.State.StartedAt}}");
    restart_count =
        docker_conn_capture_inspect (data->active_conn, container->id, "{{.RestartCount}}");
    command = docker_conn_capture_inspect (data->active_conn, container->id,
                                           "{{.Path}} {{range .Args}}{{.}} {{end}}");
    entrypoint =
        docker_conn_capture_inspect (data->active_conn, container->id,
                                     "{{if .Config.Entrypoint}}{{range .Config.Entrypoint}}{{.}} "
                                     "{{end}}{{else}}-{{end}}");
    ports = g_strdup (container->ports);
    formatted_ports = docker_format_port_mappings (ports);
    ip = docker_conn_capture_inspect (data->active_conn, container->id,
                                      "{{range .NetworkSettings.Networks}}{{.IPAddress}} {{end}}");
    mounts = docker_conn_capture_inspect (
        data->active_conn, container->id,
        "{{range .Mounts}}{{.Destination}} -> {{.Source}}{{println}}{{end}}");
    memory_limit =
        docker_conn_capture_inspect (data->active_conn, container->id, "{{.HostConfig.Memory}}");
    cpu_shares =
        docker_conn_capture_inspect (data->active_conn, container->id, "{{.HostConfig.CpuShares}}");
    stats = docker_capture_stats_block (data, container->id);

    if (name != NULL && name[0] == '/')
        memmove (name, name + 1, strlen (name));

    summary.name = name;
    summary.id = id;
    summary.image = image;
    summary.status = status;
    summary.started = started;
    summary.restart_count = restart_count;
    summary.command = command;
    summary.entrypoint = entrypoint;
    summary.ports = formatted_ports;
    summary.ip = ip;
    summary.mounts = mounts;
    summary.memory_limit = docker_normalize_zero_value (memory_limit, "unlimited");
    summary.cpu_shares = docker_normalize_zero_value (cpu_shares, "default");
    summary.stats = stats;

    ok = docker_ui_view_container_summary (&summary);

    g_free (name);
    g_free (id);
    g_free (image);
    g_free (status);
    g_free (started);
    g_free (restart_count);
    g_free (command);
    g_free (entrypoint);
    g_free (ports);
    g_free (formatted_ports);
    g_free (ip);
    g_free (mounts);
    g_free (memory_limit);
    g_free (cpu_shares);
    g_free (stats);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_reload_projects (docker_data_t *data, char **err_text)
{
    char *output = NULL;
    gboolean ok;

    ok = docker_load_containers_output (data, &output, err_text);
    if (!ok)
        return FALSE;

    data->items = parse_projects_from_containers (output, data->current_project);
    g_free (output);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_reload_items (docker_data_t *data, char **err_text)
{
    char *output = NULL;
    gboolean ok;

    ok = docker_load_containers_output (data, &output, err_text);
    if (!ok)
        return FALSE;

    data->items = parse_container_items_from_project (output, data->current_project,
                                                      data->current_container_id);
    g_free (output);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_resolve_current (docker_data_t *data, char **err_text)
{
    char *output = NULL;
    gboolean ok;
    GPtrArray *items;
    guint i;
    gboolean found = FALSE;

    if (data == NULL || data->current_project == NULL || data->current_container_name == NULL)
        return FALSE;

    if (data->current_container_id != NULL)
        return TRUE;

    ok = docker_load_containers_output (data, &output, err_text);
    if (!ok)
        return FALSE;

    items = parse_container_items_from_project (output, data->current_project, NULL);

    for (i = 0; i < items->len; i++)
    {
        const docker_item_t *item = (const docker_item_t *) g_ptr_array_index (items, i);

        if (item->name != NULL && strcmp (item->name, data->current_container_name) == 0)
        {
            g_free (data->current_container_id);
            data->current_container_id = g_strdup (item->id);
            found = TRUE;
            break;
        }
    }

    g_ptr_array_free (items, TRUE);
    g_free (output);

    if (!found && err_text != NULL && (*err_text == NULL || (*err_text)[0] == '\0'))
        *err_text = g_strdup_printf ("Container not found: %s", data->current_container_name);

    return found;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_reload_details (docker_data_t *data)
{
    docker_item_t *item;
    gboolean is_ssh = (data->active_conn != NULL && data->active_conn->type == DOCKER_CONN_SSH);

    data->items = g_ptr_array_new_with_free_func (docker_item_free);

    item = g_new0 (docker_item_t, 1);
    item->id = g_strdup (docker_exec_entry);
    item->name = g_strdup (docker_exec_entry);
    item->is_dir = TRUE;
    g_ptr_array_add (data->items, item);

    item = g_new0 (docker_item_t, 1);
    item->id = g_strdup (docker_files_entry);
    item->name = g_strdup (docker_files_entry);
    item->is_dir = TRUE;
    g_ptr_array_add (data->items, item);

    item = g_new0 (docker_item_t, 1);
    item->id = g_strdup (docker_logs_entry);
    item->name = g_strdup (docker_logs_entry);
    g_ptr_array_add (data->items, item);

    /* mounts/ is local-only: host paths are not reachable for SSH profiles */
    if (!is_ssh)
    {
        item = g_new0 (docker_item_t, 1);
        item->id = g_strdup (docker_mounts_entry);
        item->name = g_strdup (docker_mounts_entry);
        item->is_dir = TRUE;
        g_ptr_array_add (data->items, item);
    }

    item = g_new0 (docker_item_t, 1);
    item->id = g_strdup (docker_inspect_file);
    item->name = g_strdup (docker_inspect_file);
    g_ptr_array_add (data->items, item);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_containers_enter (docker_data_t *data, const char *name)
{
    if (strcmp (name, docker_exec_entry) == 0 && data->current_container_id != NULL)
    {
        char *quoted = g_shell_quote (data->current_container_id);
        char *docker_args = g_strdup_printf ("exec -it %s sh", quoted);
        char *cmd = docker_conn_build_shell_cmd (data->active_conn, docker_args);

        data->host->run_command (data->host, cmd, 0);
        g_free (docker_args);
        g_free (cmd);
        g_free (quoted);
        return MC_PPR_OK;
    }

    if (strcmp (name, docker_files_entry) == 0 && data->current_container_id != NULL)
    {
        g_free (data->files_cwd);
        data->files_cwd = g_strdup ("/");
        set_view (data, DOCKER_VIEW_CONTAINER_FILES);
        return MC_PPR_OK;
    }

    if (strcmp (name, docker_logs_entry) == 0)
        return MC_PPR_OK;

    if (strcmp (name, docker_mounts_entry) == 0 && data->current_container_id != NULL)
    {
        set_view (data, DOCKER_VIEW_CONTAINER_MOUNTS);
        return MC_PPR_OK;
    }

    if (strcmp (name, docker_inspect_file) == 0)
        return MC_PPR_NOT_SUPPORTED;

    return MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_view_summary (docker_data_t *data, const char *fname)
{
    const docker_item_t *container = find_item_by_name (data, fname);

    return docker_view_container_summary (data, container);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_view_logs (docker_data_t *data, const char *fname)
{
    if (strcmp (fname, docker_logs_entry) == 0 && data->current_container_id != NULL)
    {
        char *quoted = g_shell_quote (data->current_container_id);
        char *docker_args = g_strdup_printf ("logs --tail 1000 %s 2>&1", quoted);
        char *cmd = docker_conn_build_pipe_cmd (data->active_conn, docker_args);

        (void) docker_ui_viewer_command (cmd);
        g_free (docker_args);
        g_free (cmd);
        g_free (quoted);
        return TRUE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_containers_get_local_copy (docker_data_t *data, const char *fname, char **local_path)
{
    const char *cmd = NULL;
    char *cmd_dynamic = NULL;
    char *output = NULL;
    char *err_text = NULL;
    gboolean ok;

    if (data->view == DOCKER_VIEW_CONTAINER_DETAILS)
    {
        char *quoted_id;

        if (data->current_container_id == NULL || strcmp (fname, docker_inspect_file) != 0)
            return MC_PPR_FAILED;

        quoted_id = g_shell_quote (data->current_container_id);
        cmd_dynamic = g_strdup_printf ("inspect %s", quoted_id);
        g_free (quoted_id);
        cmd = cmd_dynamic;
    }
    else if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS)
    {
        const docker_item_t *container = find_item_by_name (data, fname);
        char *quoted_id;

        if (container == NULL)
            return MC_PPR_FAILED;

        quoted_id = g_shell_quote (container->id);
        cmd_dynamic = g_strdup_printf ("inspect %s", quoted_id);
        g_free (quoted_id);
        cmd = cmd_dynamic;
    }
    else
        return MC_PPR_FAILED;

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

mc_pp_result_t
docker_containers_delete_items (docker_data_t *data, const char **names, int count)
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
        cmd = g_strdup_printf ("rm %s", quoted);

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
docker_containers_get_columns (size_t *count)
{
    if (count != NULL)
        *count = G_N_ELEMENTS (docker_columns);
    return docker_columns;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_containers_get_column_value (docker_data_t *data, const char *fname, const char *column_id)
{
    const docker_item_t *item;

    if (fname == NULL || column_id == NULL)
        return NULL;

    item = find_item_by_name (data, fname);
    if (item == NULL)
        return NULL;

    if (strcmp (column_id, "status") == 0)
        return item->status;
    if (strcmp (column_id, "image") == 0)
        return item->image;
    if (strcmp (column_id, "ports") == 0)
        return item->ports;

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_containers_get_default_format (void)
{
    return "name:20 | status:12 | image | ports:12";
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_containers_create_item (docker_data_t *data)
{
    char *image = NULL;
    char *name = NULL;
    char *command = NULL;
    gboolean detach = TRUE;
    char *q_image = NULL;
    char *q_name = NULL;
    char *q_cmd = NULL;
    char *q_project = NULL;
    char *cmd = NULL;
    char *output = NULL;
    char *err_text = NULL;
    mc_pp_result_t result = MC_PPR_FAILED;

    if (!docker_ui_show_create_container_dialog (&image, &name, &command, &detach))
        goto done;

    if (image == NULL || image[0] == '\0')
        goto done;

    q_image = g_shell_quote (image);

    if (name != NULL && name[0] != '\0')
        q_name = g_shell_quote (name);

    if (command != NULL && command[0] != '\0')
        q_cmd = g_shell_quote (command);

    if (data->view == DOCKER_VIEW_CONTAINERS_ITEMS && data->current_project != NULL
        && !docker_containers_is_ungrouped_project (data->current_project))
        q_project = g_shell_quote (data->current_project);

    cmd = g_strdup ("run ");

    if (detach)
    {
        char *tmp = g_strconcat (cmd, "-d ", (char *) NULL);

        g_free (cmd);
        cmd = tmp;
    }

    if (q_name != NULL)
    {
        char *tmp = g_strconcat (cmd, "--name ", q_name, " ", (char *) NULL);

        g_free (cmd);
        cmd = tmp;
    }

    if (q_project != NULL)
    {
        char *tmp =
            g_strconcat (cmd, "--label com.docker.compose.project=", q_project, " ", (char *) NULL);

        g_free (cmd);
        cmd = tmp;
    }

    {
        char *tmp = g_strconcat (cmd, q_image, (char *) NULL);

        g_free (cmd);
        cmd = tmp;
    }

    if (q_cmd != NULL)
    {
        char *tmp = g_strconcat (cmd, " sh -c ", q_cmd, (char *) NULL);

        g_free (cmd);
        cmd = tmp;
    }

    if (!docker_conn_run (data->active_conn, cmd, &output, &err_text))
    {
        if (err_text != NULL && err_text[0] != '\0')
            message (D_ERROR, MSG_ERROR, "%s", err_text);
        goto done;
    }

    result = MC_PPR_OK;

done:
    g_free (image);
    g_free (name);
    g_free (command);
    g_free (q_image);
    g_free (q_name);
    g_free (q_cmd);
    g_free (q_project);
    g_free (cmd);
    g_free (output);
    g_free (err_text);

    return result;
}
