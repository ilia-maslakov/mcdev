/*
   External panelize panel plugin.

   Copyright (C) 1995-2026
   Free Software Foundation, Inc.

   Written by:
   Janne Kukonlehto, 1995
   Jakub Jelinek, 1995
   Andrew Borodin <aborodin@vmail.ru> 2011-2023
   Ilia Maslakov <il.smind@gmail.com>, 2026

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

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/panel-plugin.h"
#include "lib/util.h"
#include "lib/widget.h"

#include "src/filemanager/dir.h"
#include "src/history.h"

#include "panelize_config.h"
#include "panelize_dlg.h"
#include "panelize_url.h"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    mc_panel_host_t *host;
    GPtrArray *paths;  /* gchar* paths, owned */
    char *command;     /* command line that produced the list (for title bar) */
    char *label;       /* preset label, NULL for custom command */
    gboolean absolute; /* TRUE if paths are absolute (panel cwd set to "/") */
} panelize_data_t;

/*** forward declarations (file scope functions) *************************************************/

static void *panelize_open (mc_panel_host_t *host, const char *open_path);
static void panelize_close (void *plugin_data);
static mc_pp_result_t panelize_get_items (void *plugin_data, void *list_ptr);
static mc_pp_result_t panelize_reload (void *plugin_data);
static const char *panelize_get_title (void *plugin_data);
static const char *panelize_get_default_format (void *plugin_data);
static void *panelize_action_dialog (mc_panel_host_t *host, const char *open_path);
static void *panelize_action_empty (mc_panel_host_t *host, const char *open_path);
static mc_pp_result_t panelize_put_file (void *plugin_data, const char *local_path,
                                         const char *dest_name);
static mc_pp_result_t panelize_delete_items (void *plugin_data, const char **names, int count);
static void *panelize_open_file_list (mc_panel_host_t *host, const char *const *paths, size_t count,
                                      const char *label);

/*** file scope variables ************************************************************************/

static const mc_pp_action_t panelize_actions[] = {
    { N_ ("&External panelize"), panelize_action_dialog },
    { N_ ("Paneli&ze (empty)"), panelize_action_empty },
};

static const mc_pp_cmd_menu_entry_t panelize_menu[] = {
    { N_ ("E&xternal panelize"), 0, "C-x !", 0, MC_PP_MENU_COMMAND },
    { N_ ("Paneli&ze"), 1, NULL, 0, MC_PP_MENU_PANEL },
};

static const mc_panel_plugin_t panelize_plugin = {
    .api_version = MC_PANEL_PLUGIN_API_VERSION,
    .name = "panelize",
    .display_name = N_ ("Panelize"),
    .proto = "panelize",
    .prefix = "panelize:",
    .flags = MC_PPF_GET_FILES | MC_PPF_DELETE | MC_PPF_PUT_FILES | MC_PPF_CUSTOM_TITLE
        | MC_PPF_SHOW_IN_DRIVE_MENU | MC_PPF_LOCAL_FILES | MC_PPF_ACCEPTS_FILE_LIST,

    .open = panelize_open,
    .close = panelize_close,
    .get_items = panelize_get_items,

    .put_file = panelize_put_file,
    .delete_items = panelize_delete_items,
    .get_title = panelize_get_title,
    .get_default_format = panelize_get_default_format,
    .reload = panelize_reload,
    .open_file_list = panelize_open_file_list,

    .actions = panelize_actions,
    .action_count = G_N_ELEMENTS (panelize_actions),
    .cmd_menu_entries = panelize_menu,
    .cmd_menu_entry_count = G_N_ELEMENTS (panelize_menu),
};

/*** file scope functions ************************************************************************/

/* Takes ownership of @path. */
static gboolean
panelize_paths_add_owned (GPtrArray *paths, char *path)
{
    guint i;

    for (i = 0; i < paths->len; i++)
        if (strcmp (g_ptr_array_index (paths, i), path) == 0)
        {
            g_free (path);
            return FALSE;
        }

    g_ptr_array_add (paths, path);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Run command, populating data->paths from stdout (newline-separated). */
static void
panelize_run_command (panelize_data_t *data, const char *command)
{
    GError *error = NULL;
    mc_pipe_t *pipe;
    GString *remain_line = NULL;

    pipe = mc_popen (command, TRUE, TRUE, &error);
    if (pipe == NULL)
    {
        if (error != NULL)
            g_error_free (error);
        return;
    }

    while (TRUE)
    {
        GString *line;

        pipe->out.len = MC_PIPE_BUFSIZE;
        pipe->err.len = MC_PIPE_BUFSIZE;
        pipe->err.null_term = TRUE;

        mc_pread (pipe, &error);

        if (error != NULL)
        {
            g_error_free (error);
            error = NULL;
            break;
        }

        if (pipe->out.len == MC_PIPE_STREAM_EOF)
            break;

        if (pipe->out.len <= 0)
            continue;

        while ((line = mc_pstream_get_string (&pipe->out)) != NULL)
        {
            if (line->len > 0 && line->str[line->len - 1] == '\n')
            {
                g_string_truncate (line, line->len - 1);

                if (remain_line != NULL)
                {
                    g_string_append_len (remain_line, line->str, line->len);
                    g_string_free (line, TRUE);
                    line = remain_line;
                    remain_line = NULL;
                }
            }
            else
            {
                if (remain_line == NULL)
                    remain_line = line;
                else
                {
                    g_string_append_len (remain_line, line->str, line->len);
                    g_string_free (line, TRUE);
                }
                continue;
            }

            if (line->len > 0)
            {
                const char *name = line->str;

                if (name[0] == '.' && name[1] == '/' && name[2] != '\0')
                    name += 2;

                panelize_paths_add_owned (data->paths, g_strdup (name));
            }
            g_string_free (line, TRUE);
        }
    }

    /* Accept a final line without a trailing newline. */
    if (remain_line != NULL)
    {
        if (remain_line->len > 0)
        {
            const char *name = remain_line->str;

            if (name[0] == '.' && name[1] == '/' && name[2] != '\0')
                name += 2;

            panelize_paths_add_owned (data->paths, g_strdup (name));
        }
        g_string_free (remain_line, TRUE);
    }

    mc_pclose (pipe, NULL);
}

/* --------------------------------------------------------------------------------------------- */

/* Absolute entries are resolved against "/". */
static void
panelize_set_panel_cwd_if_absolute (panelize_data_t *data)
{
    guint i;

    data->absolute = FALSE;
    for (i = 0; i < data->paths->len; i++)
    {
        const char *path = g_ptr_array_index (data->paths, i);
        if (path != NULL && path[0] == '/')
        {
            data->absolute = TRUE;
            break;
        }
    }

    if (data->absolute && data->host != NULL && data->host->set_cwd != NULL)
        data->host->set_cwd (data->host, "/");
}

/* --------------------------------------------------------------------------------------------- */

static void
panelize_action_add (GPtrArray *presets, const char *current_command)
{
    char *label;

    if (current_command == NULL || current_command[0] == '\0')
        return;

    label = input_dialog (_ ("Add to external panelize"), _ ("Enter command label:"),
                          MC_HISTORY_FM_PANELIZE_LABEL, "", INPUT_COMPLETE_NONE);
    if (label != NULL && label[0] != '\0')
    {
        panelize_config_add (presets, label, current_command);
        panelize_config_save (presets);
    }
    g_free (label);
}

/* --------------------------------------------------------------------------------------------- */

static void
panelize_action_remove (GPtrArray *presets, int index)
{
    if (index < 0 || (guint) index >= presets->len)
        return;
    panelize_config_remove (presets, (guint) index);
    panelize_config_save (presets);
}

/* --------------------------------------------------------------------------------------------- */

static void
panelize_action_edit (GPtrArray *presets, int index, const char *current_command)
{
    panelize_preset_t *p;
    char *new_label;
    char *new_command;

    if (index < 0 || (guint) index >= presets->len)
        return;

    p = g_ptr_array_index (presets, (guint) index);

    new_label = input_dialog (_ ("Edit preset"), _ ("Label:"), MC_HISTORY_FM_PANELIZE_LABEL,
                              p->label, INPUT_COMPLETE_NONE);
    if (new_label == NULL || new_label[0] == '\0')
    {
        g_free (new_label);
        return;
    }

    new_command = input_dialog (
        _ ("Edit preset"), _ ("Command:"), MC_HISTORY_FM_PANELIZE_CMD,
        (current_command != NULL && current_command[0] != '\0') ? current_command : p->command,
        INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_COMMANDS | INPUT_COMPLETE_VARIABLES);
    if (new_command == NULL || new_command[0] == '\0')
    {
        g_free (new_label);
        g_free (new_command);
        return;
    }

    g_free (p->label);
    p->label = new_label;
    g_free (p->command);
    p->command = new_command;
    panelize_config_save (presets);
}

/* --------------------------------------------------------------------------------------------- */

/* Returns a command to run, or FALSE on cancel. */
static gboolean
panelize_dialog_loop (GPtrArray *presets, char **out_command, char **out_label)
{
    int selected = -1;

    for (;;)
    {
        int idx = -1;
        char *cmd = NULL;
        panelize_dlg_result_t r;

        r = panelize_dlg_run (presets, selected, &idx, &cmd);
        selected = idx;

        switch (r)
        {
        case PANELIZE_DLG_CANCEL:
            g_free (cmd);
            return FALSE;

        case PANELIZE_DLG_PANELIZE:
            if (cmd == NULL || cmd[0] == '\0')
            {
                g_free (cmd);
                return FALSE;
            }
            *out_command = cmd;
            *out_label = NULL;
            if (idx >= 0 && (guint) idx < presets->len)
            {
                panelize_preset_t *p = g_ptr_array_index (presets, (guint) idx);
                if (strcmp (p->command, cmd) == 0)
                    *out_label = g_strdup (p->label);
            }
            return TRUE;

        case PANELIZE_DLG_ADD:
            panelize_action_add (presets, cmd);
            g_free (cmd);
            break;

        case PANELIZE_DLG_REMOVE:
            panelize_action_remove (presets, idx);
            if (selected >= (int) presets->len)
                selected = (int) presets->len - 1;
            g_free (cmd);
            break;

        case PANELIZE_DLG_EDIT:
            panelize_action_edit (presets, idx, cmd);
            g_free (cmd);
            break;

        default:
            g_free (cmd);
            return FALSE;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Read newline- or NUL-separated paths. */
static guint
panelize_load_paths_from_file (panelize_data_t *data, const char *file, gboolean nul_sep)
{
    gchar *content = NULL;
    gsize len = 0;
    gsize start;
    gsize i;
    guint added = 0;
    char sep;

    if (!g_file_get_contents (file, &content, &len, NULL))
        return 0;

    sep = nul_sep ? '\0' : '\n';
    start = 0;
    for (i = 0; i < len; i++)
    {
        if (content[i] == sep)
        {
            if (i > start)
            {
                const char *name = content + start;
                gsize n = i - start;
                /* trim trailing CR for CRLF line endings */
                if (!nul_sep && n > 0 && name[n - 1] == '\r')
                    n--;
                if (n > 0)
                {
                    if (n >= 2 && name[0] == '.' && name[1] == '/')
                    {
                        name += 2;
                        n -= 2;
                    }
                    if (n > 0 && panelize_paths_add_owned (data->paths, g_strndup (name, n)))
                        added++;
                }
            }
            start = i + 1;
        }
    }
    if (start < len)
    {
        const char *name = content + start;
        gsize n = len - start;
        if (!nul_sep && n > 0 && name[n - 1] == '\r')
            n--;
        if (n >= 2 && name[0] == '.' && name[1] == '/')
        {
            name += 2;
            n -= 2;
        }
        if (n > 0 && panelize_paths_add_owned (data->paths, g_strndup (name, n)))
            added++;
    }

    g_free (content);
    return added;
}

/* --------------------------------------------------------------------------------------------- */

/* URL-driven open: panelize:from-file=<path>[;label=...][;nul] */
static void *
panelize_open_from_url (mc_panel_host_t *host, const panelize_url_t *url)
{
    panelize_data_t *data;

    data = g_new0 (panelize_data_t, 1);
    data->host = host;
    data->paths = g_ptr_array_new_with_free_func (g_free);
    data->command = NULL;
    data->label = (url->label != NULL) ? g_strdup (url->label) : NULL;

    panelize_load_paths_from_file (data, url->file, url->nul_sep);

    if (data->paths->len == 0)
    {
        if (host->message != NULL)
            host->message (host, 0, _ ("Panelize"), _ ("No paths to panelize."));
        panelize_close (data);
        return NULL;
    }

    panelize_set_panel_cwd_if_absolute (data);
    return data;
}

/* --------------------------------------------------------------------------------------------- */

/* File-list entry point: copy the caller-owned paths into a new panel. */
static void *
panelize_open_file_list (mc_panel_host_t *host, const char *const *paths, size_t count,
                         const char *label)
{
    panelize_data_t *data;
    size_t i;

    if (host == NULL || paths == NULL || count == 0)
        return NULL;

    data = g_new0 (panelize_data_t, 1);
    data->host = host;
    data->paths = g_ptr_array_new_with_free_func (g_free);
    data->command = NULL;
    data->label = (label != NULL && label[0] != '\0') ? g_strdup (label) : NULL;

    for (i = 0; i < count; i++)
    {
        const char *p = paths[i];

        if (p == NULL || p[0] == '\0')
            continue;
        /* Normalize leading "./". */
        if (p[0] == '.' && p[1] == '/' && p[2] != '\0')
            p += 2;
        panelize_paths_add_owned (data->paths, g_strdup (p));
    }

    if (data->paths->len == 0)
    {
        if (host->message != NULL)
            host->message (host, 0, _ ("Panelize"), _ ("No paths to panelize."));
        panelize_close (data);
        return NULL;
    }

    panelize_set_panel_cwd_if_absolute (data);
    return data;
}

/* --------------------------------------------------------------------------------------------- */

/* Open a URL-provided list or an empty panel. */
static void *
panelize_open (mc_panel_host_t *host, const char *open_path)
{
    panelize_url_t url;

    if (panelize_url_parse (open_path, &url))
    {
        void *result = panelize_open_from_url (host, &url);
        panelize_url_clear (&url);
        return result;
    }
    panelize_url_clear (&url);

    return panelize_action_empty (host, open_path);
}

/* --------------------------------------------------------------------------------------------- */

static void
panelize_close (void *plugin_data)
{
    panelize_data_t *data = (panelize_data_t *) plugin_data;

    if (data == NULL)
        return;

    g_ptr_array_free (data->paths, TRUE);
    g_free (data->command);
    g_free (data->label);
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
panelize_get_items (void *plugin_data, void *list_ptr)
{
    panelize_data_t *data = (panelize_data_t *) plugin_data;
    dir_list *list = (dir_list *) list_ptr;
    guint i;

    for (i = 0; i < data->paths->len; i++)
    {
        const char *name = (const char *) g_ptr_array_index (data->paths, i);
        struct stat st;
        gboolean link_to_dir, stale_link;

        if (!handle_path (name, &st, &link_to_dir, &stale_link))
            continue;

        /* Directory names carry their marker because the "type" column is hidden. */
        if (S_ISDIR (st.st_mode) || link_to_dir)
        {
            size_t nlen = strlen (name);

            if (nlen > 0 && name[nlen - 1] != '/')
            {
                char *with_slash = g_strconcat (name, "/", NULL);
                dir_list_append (list, with_slash, &st, link_to_dir, stale_link);
                g_free (with_slash);
                continue;
            }
        }
        dir_list_append (list, name, &st, link_to_dir, stale_link);
    }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/* Hide the "type" column to keep absolute paths readable. */
static const char *
panelize_get_default_format (void *plugin_data)
{
    (void) plugin_data;
    return "half name | size | mtime";
}

/* --------------------------------------------------------------------------------------------- */

/* Only command-backed panels can be rebuilt. */
static mc_pp_result_t
panelize_reload (void *plugin_data)
{
    panelize_data_t *data = (panelize_data_t *) plugin_data;

    if (data == NULL || data->command == NULL)
        return MC_PPR_OK;

    g_ptr_array_set_size (data->paths, 0);
    panelize_run_command (data, data->command);
    panelize_set_panel_cwd_if_absolute (data);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
panelize_get_title (void *plugin_data)
{
    panelize_data_t *data = (panelize_data_t *) plugin_data;

    if (data == NULL)
        return "panelize";
    if (data->label != NULL && data->label[0] != '\0')
        return data->label;
    if (data->command != NULL && data->command[0] != '\0')
        return data->command;
    return "panelize";
}

/* --------------------------------------------------------------------------------------------- */

static void *
panelize_action_empty (mc_panel_host_t *host, const char *open_path)
{
    panelize_data_t *data;

    (void) open_path;

    data = g_new0 (panelize_data_t, 1);
    data->host = host;
    data->paths = g_ptr_array_new_with_free_func (g_free);
    data->command = NULL;
    data->label = g_strdup (_ ("(empty)"));
    return data;
}

/* --------------------------------------------------------------------------------------------- */

/* Copy/Move into panelize appends paths only. */
static mc_pp_result_t
panelize_put_file (void *plugin_data, const char *local_path, const char *dest_name)
{
    panelize_data_t *data = (panelize_data_t *) plugin_data;

    (void) dest_name;

    if (data == NULL || local_path == NULL || local_path[0] == '\0')
        return MC_PPR_FAILED;

    /* Existing paths are a no-op. */
    (void) panelize_paths_add_owned (data->paths, g_strdup (local_path));
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
panelize_delete_items (void *plugin_data, const char **names, int count)
{
    panelize_data_t *data = (panelize_data_t *) plugin_data;
    int i;

    if (data == NULL || names == NULL)
        return MC_PPR_FAILED;

    for (i = 0; i < count; i++)
    {
        const char *target = names[i];
        size_t tlen;
        guint j;

        if (target == NULL)
            continue;
        tlen = strlen (target);
        /* Directory fnames carry a trailing display slash. */
        if (tlen > 0 && target[tlen - 1] == '/')
            tlen--;

        for (j = 0; j < data->paths->len; j++)
        {
            const char *p = g_ptr_array_index (data->paths, j);

            if (p != NULL && strlen (p) == tlen && strncmp (p, target, tlen) == 0)
            {
                g_ptr_array_remove_index (data->paths, j);
                break;
            }
        }
    }
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static void *
panelize_action_dialog (mc_panel_host_t *host, const char *open_path)
{
    panelize_data_t *data;
    GPtrArray *presets;
    char *command = NULL;
    char *label = NULL;
    gboolean ok;

    (void) open_path;

    presets = panelize_config_load ();
    ok = panelize_dialog_loop (presets, &command, &label);
    panelize_config_free (presets);

    if (!ok || command == NULL)
    {
        g_free (command);
        g_free (label);
        return NULL;
    }

    data = g_new0 (panelize_data_t, 1);
    data->host = host;
    data->paths = g_ptr_array_new_with_free_func (g_free);
    data->command = command;
    data->label = label;

    panelize_run_command (data, data->command);

    if (data->paths->len == 0)
    {
        if (host->message != NULL)
            host->message (host, 0, _ ("Panelize"),
                           _ ("Command produced no output; nothing to panelize."));
        panelize_close (data);
        return NULL;
    }

    panelize_set_panel_cwd_if_absolute (data);
    return data;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *mc_panel_plugin_register (void);

const mc_panel_plugin_t *
mc_panel_plugin_register (void)
{
    return &panelize_plugin;
}

/* --------------------------------------------------------------------------------------------- */
