/*
   Panel plugin UI -- activation, host interface, drive menu.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
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

/** \file panel-plugin-ui.c
 *  \brief Source: panel plugin activation, host callbacks, drive menu
 */

#include <config.h>

#include <string.h>
#include <unistd.h>

#include "lib/global.h"

#include "lib/strutil.h"
#include "lib/vfs/vfs.h"
#include "lib/widget.h"
#include "lib/panel-plugin.h"

#include "src/execute.h"
#ifdef USE_DIFF_VIEW
#include "src/diffviewer/ydiff.h"
#endif

#include "dir.h"
#include "layout.h"
#include "cd.h"

#include "panel.h"
#include "filemanager.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
host_refresh_impl (mc_panel_host_t *host)
{
    WPanel *panel = (WPanel *) host->host_data;
    panel->dirty = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
host_set_hint_impl (mc_panel_host_t *host, const char *text)
{
    (void) host;
    set_hintbar (text);
}

/* --------------------------------------------------------------------------------------------- */

static void
host_message_impl (mc_panel_host_t *host, int flags, const char *title, const char *text)
{
    (void) host;
    if (mc_pp_quiet_messages ())
        return;
    message (flags, title, "%s", text);
}

/* --------------------------------------------------------------------------------------------- */

static void
host_run_command_impl (mc_panel_host_t *host, const char *command, int flags)
{
    (void) host;
    shell_execute (command, flags);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
host_open_diff_impl (mc_panel_host_t *host, const char *left_path, const char *right_path)
{
    (void) host;

    if (left_path == NULL || right_path == NULL || *left_path == '\0' || *right_path == '\0')
        return FALSE;

#ifdef USE_DIFF_VIEW
    return (diff_view (left_path, right_path, left_path, right_path) != 0);
#else
    return FALSE;
#endif
}

/* --------------------------------------------------------------------------------------------- */

static void
host_close_plugin_impl (mc_panel_host_t *host, const char *dir_path)
{
    WPanel *panel = (WPanel *) host->host_data;
    char *plugin_path = NULL;
    char *target_dir = NULL;

    if (panel != NULL && panel->is_plugin_panel && panel->plugin != NULL
        && panel->plugin->prefix != NULL && panel->plugin->get_title != NULL
        && panel->plugin_data != NULL)
    {
        const char *title = panel->plugin->get_title (panel->plugin_data);

        if (title != NULL && title[0] != '\0')
            plugin_path = g_strdup_printf ("%s%s", panel->plugin->prefix, title);
    }

    if (dir_path != NULL && dir_path[0] != '\0')
        target_dir = g_strdup (dir_path);

    if (plugin_path != NULL && target_dir != NULL)
        panel_directory_history_add_path (panel, plugin_path);

    /* Explicit target_dir overrides the saved pre-plugin cwd. */
    if (target_dir != NULL && panel != NULL && panel->plugin_pre_cwd_vpath != NULL)
    {
        vfs_path_free (panel->plugin_pre_cwd_vpath, TRUE);
        panel->plugin_pre_cwd_vpath = NULL;
    }

    panel_plugin_close (panel);

    if (target_dir != NULL)
    {
        vfs_path_t *cd_vpath;

        cd_vpath = vfs_path_from_str (target_dir);
        if (!panel_do_cd (panel, cd_vpath, cd_parse_command))
            cd_error_message (target_dir);
        vfs_path_free (cd_vpath, TRUE);
    }

    g_free (plugin_path);
    g_free (target_dir);
}

/* --------------------------------------------------------------------------------------------- */

static int
host_get_marked_count_impl (mc_panel_host_t *host)
{
    WPanel *panel = (WPanel *) host->host_data;
    return panel->marked;
}

/* --------------------------------------------------------------------------------------------- */

static const GString *
host_get_next_marked_impl (mc_panel_host_t *host, int *current)
{
    WPanel *panel = (WPanel *) host->host_data;
    return panel_get_marked_file (panel, current);
}

/* --------------------------------------------------------------------------------------------- */

static const GString *
host_get_current_impl (mc_panel_host_t *host)
{
    WPanel *panel = (WPanel *) host->host_data;
    const file_entry_t *fe;

    fe = panel_current_entry (panel);
    if (fe == NULL)
        return NULL;

    return fe->fname;
}

/* --------------------------------------------------------------------------------------------- */

static void
host_add_history_impl (mc_panel_host_t *host, const char *path)
{
    WPanel *panel = (WPanel *) host->host_data;
    panel_directory_history_add_path (panel, path);
}

/* --------------------------------------------------------------------------------------------- */

static void
host_navigate_other_panel_impl (mc_panel_host_t *host, const char *dir_path, const char *focus_file)
{
    WPanel *op;
    vfs_path_t *vpath;

    (void) host;

    op = get_other_panel ();
    if (op == NULL || op->is_plugin_panel || dir_path == NULL)
        return;

    vpath = vfs_path_from_str (dir_path);
    if (panel_do_cd (op, vpath, cd_exact) && focus_file != NULL)
        panel_set_current_by_name (op, focus_file);
    vfs_path_free (vpath, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
host_set_cwd_impl (mc_panel_host_t *host, const char *path)
{
    WPanel *panel = (WPanel *) host->host_data;
    vfs_path_t *vpath;

    if (panel == NULL || path == NULL || path[0] == '\0')
        return;

    vpath = vfs_path_from_str (path);
    panel_set_cwd (panel, vpath);
    if (panel == current_panel)
        (void) mc_chdir (vpath);
    vfs_path_free (vpath, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_plugin_init_host (mc_panel_host_t *host, WPanel *panel)
{
    host->refresh = host_refresh_impl;
    host->set_hint = host_set_hint_impl;
    host->message = host_message_impl;
    host->run_command = host_run_command_impl;
    host->open_diff = host_open_diff_impl;
    host->close_plugin = host_close_plugin_impl;
    host->add_history = host_add_history_impl;
    host->get_marked_count = host_get_marked_count_impl;
    host->get_next_marked = host_get_next_marked_impl;
    host->get_current = host_get_current_impl;
    host->navigate_other_panel = host_navigate_other_panel_impl;
    host->set_cwd = host_set_cwd_impl;
    host->host_data = panel;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Suppress (or restore) plugin modal messages. Returns the previous setting so
   callers can save and restore it around a passive plugin call. */
gboolean
panel_plugin_set_quiet_messages (gboolean quiet)
{
    return mc_pp_set_quiet_messages (quiet);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_plugin_reload_internal (WPanel *panel, gboolean call_plugin_reload)
{
    gboolean was_dotdot = FALSE;
    char *focus_name = NULL;

    if (!panel->is_plugin_panel || panel->plugin == NULL || panel->plugin_data == NULL)
        return;

    {
        const file_entry_t *fe = panel_current_entry (panel);
        if (fe != NULL && fe->fname != NULL && fe->fname->str != NULL)
        {
            if (strcmp (fe->fname->str, "..") == 0)
                was_dotdot = TRUE;
            else
                focus_name = g_strdup (fe->fname->str);
        }
    }

    if (call_plugin_reload && panel->plugin->reload != NULL)
        panel->plugin->reload (panel->plugin_data);

    panel_clean_dir (panel);
    /* panel_clean_dir resets is_panelized; restore plugin state */
    panel->is_panelized = TRUE;
    panel->is_plugin_panel = TRUE;

    panel_plugin_apply_default_columns_format (panel);

    dir_list_init (&panel->dir);
    panel->plugin->get_items (panel->plugin_data, &panel->dir);

    panel_re_sort (panel);

    if (focus_name != NULL)
        panel_set_current_by_name (panel, focus_name);
    else if (was_dotdot && panel->dir.len > 1)
        panel_set_current (panel, 1);

    g_free (focus_name);
    panel->dirty = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_plugin_reload (WPanel *panel)
{
    panel_plugin_reload_internal (panel, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

void
panel_plugin_refresh (WPanel *panel)
{
    panel_plugin_reload_internal (panel, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_plugin_suspended_free (gpointer data)
{
    panel_plugin_suspended_t *source = (panel_plugin_suspended_t *) data;

    if (source->plugin != NULL && source->data != NULL)
        source->plugin->close (source->data);
    if (source->host != NULL)
        g_free (source->host->focus_after);
    g_free (source->host);
    g_free (source);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_plugin_dispose_stream_sources (WPanel *panel)
{
    g_slist_free_full (panel->stream_sources, panel_plugin_suspended_free);
    panel->stream_sources = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_plugin_suspend_current (WPanel *panel)
{
    panel_plugin_suspended_t *source;

    /* An archive opened from inside another one suspends that one in turn, so
       the panel the outermost came from is still there to go back to. */
    source = g_new0 (panel_plugin_suspended_t, 1);
    panel->stream_sources = g_slist_prepend (panel->stream_sources, source);

    source->plugin = panel->plugin;
    source->data = panel->plugin_data;
    source->host = panel->plugin_host;
    source->base_list_format = panel->plugin_base_list_format;
    source->sort_info = panel->sort_info;
    source->sort_field = panel->sort_field;

    panel->plugin = NULL;
    panel->plugin_data = NULL;
    panel->plugin_host = NULL;
    panel->is_plugin_panel = FALSE;
    panel->is_panelized = FALSE;

    /* Restore the normal format while the stream consumer becomes active. */
    set_panel_formats (panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_plugin_activate_finish (WPanel *panel, const mc_panel_plugin_t *plugin, mc_panel_host_t *host,
                              void *new_data, gboolean add_history, gboolean suspend_current)
{
    if (panel->is_plugin_panel)
    {
        if (suspend_current)
        {
            /* The reader reopens its source, so keep that source alive. */
            panel_plugin_suspend_current (panel);
        }
        else
        {
            /* Keep pre-plugin cwd while replacing the plugin instance. */
            vfs_path_t *keep = panel->plugin_pre_cwd_vpath;
            panel->plugin_pre_cwd_vpath = NULL;
            panel_plugin_close (panel);
            panel->plugin_pre_cwd_vpath = keep;
        }
    }

    panel->plugin_data = new_data;
    panel->plugin = plugin;
    panel->plugin_host = host;
    panel->is_plugin_panel = TRUE;
    panel->is_panelized = TRUE;
    panel->plugin_base_list_format = panel->list_format;

    panel_clean_dir (panel);
    /* panel_clean_dir() resets these flags. */
    panel->is_panelized = TRUE;
    panel->is_plugin_panel = TRUE;

    panel_plugin_apply_default_columns_format (panel);

    if (plugin->default_sort_id != NULL)
    {
        const panel_field_t *sf;

        sf = panel_get_field_by_id (plugin->default_sort_id);
        if (sf != NULL)
        {
            panel_set_sort_order (panel, sf);
            panel->sort_info.reverse = plugin->default_sort_reverse ? 1 : 0;
        }
    }

    dir_list_init (&panel->dir);
    plugin->get_items (panel->plugin_data, &panel->dir);

    panel_re_sort (panel);
    panel->dirty = TRUE;

    if (add_history && panel->plugin->proto != NULL && panel->plugin->get_title != NULL)
    {
        const char *title = panel->plugin->get_title (panel->plugin_data);
        if (title != NULL)
        {
            const char *path_tail = (title[0] != '\0') ? title : "/";
            char *plugin_path = g_strdup_printf ("%s:%s", panel->plugin->proto, path_tail);
            panel_directory_history_add_path (panel, plugin_path);
            g_free (plugin_path);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

void
panel_plugin_activate (WPanel *panel, const mc_panel_plugin_t *plugin, const char *open_path)
{
    mc_panel_host_t *host;
    void *new_data;

    if (panel == NULL || plugin == NULL)
        return;

    /* Keep the previous plugin active until the new open() returns data. */
    host = g_new0 (mc_panel_host_t, 1);
    panel_plugin_init_host (host, panel);

    /* Capture the cwd before open(); the plugin may update the panel cwd
       while building its listing. */
    {
        vfs_path_t *saved_pre_cwd = panel->plugin_pre_cwd_vpath;
        gboolean was_plugin = panel->is_plugin_panel;
        panel->plugin_pre_cwd_vpath =
            was_plugin ? saved_pre_cwd : vfs_path_clone (panel->cwd_vpath);

        new_data = plugin->open_with_plugin != NULL
            ? plugin->open_with_plugin (plugin, host, open_path)
            : plugin->open (host, open_path);
        if (new_data == NULL)
        {
            /* Activation aborted; keep the current panel intact. */
            if (!was_plugin)
            {
                vfs_path_free (panel->plugin_pre_cwd_vpath, TRUE);
                panel->plugin_pre_cwd_vpath = saved_pre_cwd;
            }
            g_free (host);
            return;
        }
        (void) saved_pre_cwd;
    }

    panel_plugin_activate_finish (panel, plugin, host, new_data, TRUE, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_activate_input_stream (WPanel *panel, const mc_panel_plugin_t *plugin,
                                    const char *display_name, mc_pp_input_stream_t *stream)
{
    mc_panel_host_t *host;
    void *new_data;

    if (panel == NULL || plugin == NULL || plugin->open_input_stream == NULL || stream == NULL)
        return FALSE;

    host = g_new0 (mc_panel_host_t, 1);
    panel_plugin_init_host (host, panel);

    {
        vfs_path_t *saved_pre_cwd = panel->plugin_pre_cwd_vpath;
        gboolean was_plugin = panel->is_plugin_panel;
        panel->plugin_pre_cwd_vpath =
            was_plugin ? saved_pre_cwd : vfs_path_clone (panel->cwd_vpath);

        new_data = plugin->open_input_stream (host, display_name, stream);
        if (new_data == NULL)
        {
            if (!was_plugin)
            {
                vfs_path_free (panel->plugin_pre_cwd_vpath, TRUE);
                panel->plugin_pre_cwd_vpath = saved_pre_cwd;
            }
            g_free (host);
            return FALSE;
        }
        (void) saved_pre_cwd;
    }

    /* An input stream cannot be reconstructed from a panel-history path. */
    panel_plugin_activate_finish (panel, plugin, host, new_data, FALSE, TRUE);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
panel_plugin_activate_file_operation (WPanel *panel, const mc_panel_plugin_t *plugin,
                                      const mc_pp_file_operation_t *operation,
                                      const char *display_name, mc_pp_input_stream_t *stream)
{
    mc_panel_host_t *host;
    void *new_data;

    if (panel == NULL || plugin == NULL || operation == NULL
        || operation->kind != MC_PP_FILE_OPERATION_OPEN || operation->open_input_stream == NULL
        || stream == NULL)
        return FALSE;

    host = g_new0 (mc_panel_host_t, 1);
    panel_plugin_init_host (host, panel);

    {
        vfs_path_t *saved_pre_cwd = panel->plugin_pre_cwd_vpath;
        gboolean was_plugin = panel->is_plugin_panel;
        panel->plugin_pre_cwd_vpath =
            was_plugin ? saved_pre_cwd : vfs_path_clone (panel->cwd_vpath);

        new_data = operation->open_input_stream (host, display_name, stream);
        if (new_data == NULL)
        {
            if (!was_plugin)
            {
                vfs_path_free (panel->plugin_pre_cwd_vpath, TRUE);
                panel->plugin_pre_cwd_vpath = saved_pre_cwd;
            }
            g_free (host);
            return FALSE;
        }
        (void) saved_pre_cwd;
    }

    panel_plugin_activate_finish (panel, plugin, host, new_data, FALSE, TRUE);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static const mc_pp_file_operation_t *
panel_plugin_find_file_operation (const mc_panel_plugin_t *plugin, const char *operation_name)
{
    int i;

    if (plugin == NULL || operation_name == NULL || plugin->file_operations == NULL)
        return NULL;

    for (i = 0; i < plugin->file_operation_count; i++)
        if (plugin->file_operations[i].name != NULL
            && strcmp (plugin->file_operations[i].name, operation_name) == 0)
            return &plugin->file_operations[i];

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_open_entry_by_operation (WPanel *panel, const char *fname, const char *plugin_name,
                                      const char *operation_name, char *local_copy)
{
    const mc_panel_plugin_t *target;
    const mc_pp_file_operation_t *operation;
    mc_pp_input_stream_t *stream = NULL;

    if (panel == NULL || fname == NULL || plugin_name == NULL || operation_name == NULL
        || !panel->is_plugin_panel || panel->plugin == NULL || panel->plugin_data == NULL)
    {
        if (local_copy != NULL)
        {
            unlink (local_copy);
            g_free (local_copy);
        }
        return FALSE;
    }

    target = mc_panel_plugin_find_by_name (plugin_name);
    operation = panel_plugin_find_file_operation (target, operation_name);
    if (operation == NULL || operation->kind != MC_PP_FILE_OPERATION_OPEN
        || operation->open_input_stream == NULL)
    {
        if (local_copy != NULL)
        {
            unlink (local_copy);
            g_free (local_copy);
        }
        return FALSE;
    }

    if (panel->plugin->get_input_stream != NULL
        && panel->plugin->get_input_stream (panel->plugin_data, fname, &stream) == MC_PPR_OK
        && stream != NULL)
    {
        if (local_copy != NULL)
        {
            unlink (local_copy);
            g_free (local_copy);
        }
        if (panel_plugin_activate_file_operation (panel, target, operation, fname, stream))
            return TRUE;

        mc_pp_input_stream_free (stream);
        return FALSE;
    }

    if (local_copy == NULL)
    {
        if (panel->plugin->get_local_copy == NULL || operation->may_open_name == NULL
            || !operation->may_open_name (fname))
            return FALSE;

        if (panel->plugin->get_local_copy (panel->plugin_data, fname, &local_copy) != MC_PPR_OK
            || local_copy == NULL)
            return FALSE;
    }

    stream = mc_pp_input_stream_new_for_file (local_copy, TRUE);
    g_free (local_copy);
    if (stream == NULL)
        return FALSE;

    if (panel_plugin_activate_file_operation (panel, target, operation, fname, stream))
        return TRUE;

    mc_pp_input_stream_free (stream);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_open_local_file_by_operation (WPanel *panel, const char *display_name,
                                           const char *local_path, const char *plugin_name,
                                           const char *operation_name)
{
    const mc_panel_plugin_t *target;
    const mc_pp_file_operation_t *operation;
    mc_pp_input_stream_t *stream;

    if (panel == NULL || display_name == NULL || local_path == NULL || plugin_name == NULL
        || operation_name == NULL)
        return FALSE;

    target = mc_panel_plugin_find_by_name (plugin_name);
    operation = panel_plugin_find_file_operation (target, operation_name);
    if (operation == NULL || operation->kind != MC_PP_FILE_OPERATION_OPEN
        || operation->open_input_stream == NULL)
        return FALSE;

    stream = mc_pp_input_stream_new_for_file (local_path, FALSE);
    if (stream == NULL)
        return FALSE;

    if (panel_plugin_activate_file_operation (panel, target, operation, display_name, stream))
        return TRUE;

    mc_pp_input_stream_free (stream);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
panel_plugin_view_file_operation (WPanel *panel, const mc_pp_file_operation_t *operation,
                                  const char *display_name, mc_pp_input_stream_t *stream,
                                  char **local_path)
{
    mc_panel_host_t *host;
    mc_pp_result_t result;

    if (panel == NULL || operation == NULL || operation->kind != MC_PP_FILE_OPERATION_VIEW
        || operation->view_input_stream == NULL || display_name == NULL || stream == NULL
        || local_path == NULL)
        return FALSE;

    *local_path = NULL;
    host = g_new0 (mc_panel_host_t, 1);
    panel_plugin_init_host (host, panel);
    result = operation->view_input_stream (host, display_name, stream, local_path);

    g_free (host->focus_after);
    g_free (host);

    if (result != MC_PPR_OK)
        return FALSE;

    /* Success took the stream with it, so this is not a rejection to hand
       back however little the operation left behind. */
    if (*local_path == NULL)
        message (D_ERROR, MSG_ERROR, _ ("%s produced nothing to view for %s"), operation->name,
                 display_name);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_view_entry_by_operation (WPanel *panel, const char *fname, const char *plugin_name,
                                      const char *operation_name, char *local_copy,
                                      char **view_path)
{
    const mc_panel_plugin_t *target;
    const mc_pp_file_operation_t *operation;
    mc_pp_input_stream_t *stream = NULL;

    if (view_path != NULL)
        *view_path = NULL;

    if (panel == NULL || fname == NULL || plugin_name == NULL || operation_name == NULL
        || view_path == NULL || !panel->is_plugin_panel || panel->plugin == NULL
        || panel->plugin_data == NULL)
    {
        if (local_copy != NULL)
        {
            unlink (local_copy);
            g_free (local_copy);
        }
        return FALSE;
    }

    target = mc_panel_plugin_find_by_name (plugin_name);
    operation = panel_plugin_find_file_operation (target, operation_name);
    if (operation == NULL || operation->kind != MC_PP_FILE_OPERATION_VIEW
        || operation->view_input_stream == NULL)
    {
        if (local_copy != NULL)
        {
            unlink (local_copy);
            g_free (local_copy);
        }
        return FALSE;
    }

    if (panel->plugin->get_input_stream != NULL
        && panel->plugin->get_input_stream (panel->plugin_data, fname, &stream) == MC_PPR_OK
        && stream != NULL)
    {
        if (local_copy != NULL)
        {
            unlink (local_copy);
            g_free (local_copy);
        }
        if (panel_plugin_view_file_operation (panel, operation, fname, stream, view_path))
            return TRUE;

        mc_pp_input_stream_free (stream);
        return FALSE;
    }

    if (local_copy == NULL)
    {
        if (panel->plugin->get_local_copy == NULL || operation->may_open_name == NULL
            || !operation->may_open_name (fname))
            return TRUE;

        if (panel->plugin->get_local_copy (panel->plugin_data, fname, &local_copy) != MC_PPR_OK
            || local_copy == NULL)
            return FALSE;
    }

    stream = mc_pp_input_stream_new_for_file (local_copy, TRUE);
    g_free (local_copy);
    if (stream == NULL)
        return TRUE;

    if (panel_plugin_view_file_operation (panel, operation, fname, stream, view_path))
        return TRUE;

    mc_pp_input_stream_free (stream);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_view_local_file_by_operation (WPanel *panel, const char *display_name,
                                           const char *local_path, const char *plugin_name,
                                           const char *operation_name, char **view_path)
{
    const mc_panel_plugin_t *target;
    const mc_pp_file_operation_t *operation;
    mc_pp_input_stream_t *stream;

    if (view_path != NULL)
        *view_path = NULL;

    if (panel == NULL || display_name == NULL || local_path == NULL || plugin_name == NULL
        || operation_name == NULL || view_path == NULL)
        return FALSE;

    target = mc_panel_plugin_find_by_name (plugin_name);
    operation = panel_plugin_find_file_operation (target, operation_name);
    if (operation == NULL || operation->kind != MC_PP_FILE_OPERATION_VIEW
        || operation->view_input_stream == NULL)
        return FALSE;

    stream = mc_pp_input_stream_new_for_file (local_path, FALSE);
    if (stream == NULL)
        return TRUE;

    if (panel_plugin_view_file_operation (panel, operation, display_name, stream, view_path))
        return TRUE;

    mc_pp_input_stream_free (stream);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_restore_stream_source (WPanel *panel)
{
    panel_plugin_suspended_t *top;
    panel_plugin_suspended_t source;

    if (panel == NULL || panel->stream_sources == NULL)
        return FALSE;

    top = (panel_plugin_suspended_t *) panel->stream_sources->data;
    if (top->plugin == NULL || top->data == NULL || top->host == NULL)
        return FALSE;

    source = *top;
    g_free (top);
    panel->stream_sources = g_slist_delete_link (panel->stream_sources, panel->stream_sources);

    if (panel->is_plugin_panel && panel->plugin != NULL && panel->plugin_data != NULL)
        panel->plugin->close (panel->plugin_data);
    if (panel->plugin_host != NULL)
        g_free (panel->plugin_host->focus_after);
    g_free (panel->plugin_host);

    panel->plugin = NULL;
    panel->plugin_data = NULL;
    panel->plugin_host = NULL;
    panel->is_plugin_panel = FALSE;
    panel->is_panelized = FALSE;
    set_panel_formats (panel);

    panel->plugin = source.plugin;
    panel->plugin_data = source.data;
    panel->plugin_host = source.host;
    panel->plugin_base_list_format = source.base_list_format;
    panel->sort_info = source.sort_info;
    panel->sort_field = source.sort_field;
    panel->is_plugin_panel = TRUE;
    panel->is_panelized = TRUE;

    panel_clean_dir (panel);
    panel->is_panelized = TRUE;
    panel->is_plugin_panel = TRUE;
    panel_plugin_apply_default_columns_format (panel);

    dir_list_init (&panel->dir);
    panel->plugin->get_items (panel->plugin_data, &panel->dir);
    panel_re_sort (panel);
    panel->dirty = TRUE;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_plugin_dispose (WPanel *panel)
{
    if (panel == NULL)
        return;

    if (panel->is_plugin_panel)
    {
        if (panel->plugin != NULL && panel->plugin_data != NULL)
            panel->plugin->close (panel->plugin_data);

        panel->plugin = NULL;
        panel->plugin_data = NULL;
        panel->is_plugin_panel = FALSE;

        if (panel->plugin_host != NULL)
            g_free (panel->plugin_host->focus_after);
        g_free (panel->plugin_host);
        panel->plugin_host = NULL;
    }

    panel_plugin_dispose_stream_sources (panel);
}

/* --------------------------------------------------------------------------------------------- */

void
panel_plugin_close (WPanel *panel)
{
    vfs_path_t *restore_to = NULL;

    if (panel == NULL || !panel->is_plugin_panel)
        return;

    if (panel->plugin != NULL && panel->plugin_data != NULL)
        panel->plugin->close (panel->plugin_data);

    panel->plugin = NULL;
    panel->plugin_data = NULL;
    panel->is_plugin_panel = FALSE;
    panel->plugin_base_list_format = panel->list_format;

    if (panel->plugin_host != NULL)
        g_free (panel->plugin_host->focus_after);
    g_free (panel->plugin_host);
    panel->plugin_host = NULL;

    panel_plugin_dispose_stream_sources (panel);

    panel->is_panelized = FALSE;

    /* Take ownership of the saved cwd before we touch the panel so that
       panel_do_cd / set_panel_formats / panel_reload see the right state. */
    restore_to = panel->plugin_pre_cwd_vpath;
    panel->plugin_pre_cwd_vpath = NULL;

    set_panel_formats (panel);

    if (restore_to != NULL)
    {
        /* Return to the directory the user was in before activating the plugin.
           panel_do_cd handles cwd, lwd, mc_chdir, dir_list reload, and history. */
        if (!panel_do_cd (panel, restore_to, cd_exact))
            panel_reload (panel);
        vfs_path_free (restore_to, TRUE);
    }
    else
    {
        panel_reload (panel);
    }
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_activate_by_name (WPanel *panel, const char *plugin_name, const char *open_path)
{
    const mc_panel_plugin_t *plugin;

    if (panel == NULL || plugin_name == NULL || plugin_name[0] == '\0')
        return FALSE;

    plugin = mc_panel_plugin_find_by_name (plugin_name);
    if (plugin == NULL)
        return FALSE;

    panel_plugin_activate (panel, plugin, open_path);
    return (panel->is_plugin_panel && panel->plugin == plugin);
}

/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *
panel_plugin_find_by_path (const char *open_path)
{
    const mc_panel_plugin_t *plugin;
    const char *colon;
    char *prefix;

    if (open_path == NULL || open_path[0] == '\0')
        return NULL;

    colon = strchr (open_path, ':');
    if (colon == NULL)
        return NULL;

    prefix = g_strndup (open_path, (gsize) (colon - open_path + 1));
    plugin = mc_panel_plugin_find_by_prefix (prefix);
    g_free (prefix);

    return plugin;
}

gboolean
panel_plugin_activate_by_path (WPanel *panel, const char *open_path)
{
    const mc_panel_plugin_t *plugin;

    if (panel == NULL)
        return FALSE;

    plugin = panel_plugin_find_by_path (open_path);
    if (plugin == NULL)
        return FALSE;

    panel_plugin_activate (panel, plugin, open_path);
    return (panel->is_plugin_panel && panel->plugin == plugin);
}

/* --------------------------------------------------------------------------------------------- */

/* Returned list owns only the GSList nodes. */
static GSList *
panel_plugin_collect_file_list_sinks (void)
{
    const GSList *iter;
    GSList *sinks = NULL;

    for (iter = mc_panel_plugin_list (); iter != NULL; iter = g_slist_next (iter))
    {
        const mc_panel_plugin_t *pp = (const mc_panel_plugin_t *) iter->data;

        if ((pp->flags & MC_PPF_ACCEPTS_FILE_LIST) != 0 && pp->open_file_list != NULL)
            sinks = g_slist_prepend (sinks, (gpointer) pp);
    }

    return g_slist_reverse (sinks);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_have_file_list_sink (void)
{
    const GSList *iter;

    for (iter = mc_panel_plugin_list (); iter != NULL; iter = g_slist_next (iter))
    {
        const mc_panel_plugin_t *pp = (const mc_panel_plugin_t *) iter->data;

        if ((pp->flags & MC_PPF_ACCEPTS_FILE_LIST) != 0 && pp->open_file_list != NULL)
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* Activate @plugin's open_file_list on @panel using @paths/@count/@label. */
static gboolean
panel_plugin_open_file_list_one (WPanel *panel, const mc_panel_plugin_t *plugin,
                                 const char *const *paths, size_t count, const char *label)
{
    mc_panel_host_t *host;
    void *new_data;

    if (plugin->open_file_list == NULL)
        return FALSE;

    host = g_new0 (mc_panel_host_t, 1);
    panel_plugin_init_host (host, panel);

    /* Capture the cwd before the callback; the plugin may update the panel cwd
       while building its listing. */
    {
        vfs_path_t *saved_pre_cwd = panel->plugin_pre_cwd_vpath;
        gboolean was_plugin = panel->is_plugin_panel;
        panel->plugin_pre_cwd_vpath =
            was_plugin ? saved_pre_cwd : vfs_path_clone (panel->cwd_vpath);

        new_data = plugin->open_file_list (host, paths, count, label);
        if (new_data == NULL)
        {
            if (!was_plugin)
            {
                vfs_path_free (panel->plugin_pre_cwd_vpath, TRUE);
                panel->plugin_pre_cwd_vpath = saved_pre_cwd;
            }
            g_free (host);
            return FALSE;
        }
        (void) saved_pre_cwd;
    }

    if (panel->is_plugin_panel)
    {
        /* Keep pre-plugin cwd while replacing the plugin instance. */
        vfs_path_t *keep = panel->plugin_pre_cwd_vpath;
        panel->plugin_pre_cwd_vpath = NULL;
        panel_plugin_close (panel);
        panel->plugin_pre_cwd_vpath = keep;
    }

    panel->plugin_data = new_data;
    panel->plugin = plugin;
    panel->plugin_host = host;
    panel->is_plugin_panel = TRUE;
    panel->is_panelized = TRUE;
    panel->plugin_base_list_format = panel->list_format;

    panel_clean_dir (panel);
    panel->is_panelized = TRUE;
    panel->is_plugin_panel = TRUE;

    panel_plugin_apply_default_columns_format (panel);

    if (plugin->default_sort_id != NULL)
    {
        const panel_field_t *sf;

        sf = panel_get_field_by_id (plugin->default_sort_id);
        if (sf != NULL)
        {
            panel_set_sort_order (panel, sf);
            panel->sort_info.reverse = plugin->default_sort_reverse ? 1 : 0;
        }
    }

    dir_list_init (&panel->dir);
    plugin->get_items (panel->plugin_data, &panel->dir);

    panel_re_sort (panel);
    panel->dirty = TRUE;

    if (plugin->proto != NULL && plugin->get_title != NULL)
    {
        const char *title = plugin->get_title (panel->plugin_data);
        if (title != NULL)
        {
            const char *path_tail = (title[0] != '\0') ? title : "/";
            char *plugin_path = g_strdup_printf ("%s:%s", plugin->proto, path_tail);
            panel_directory_history_add_path (panel, plugin_path);
            g_free (plugin_path);
        }
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_open_file_list (WPanel *panel, const char *const *paths, size_t count,
                             const char *label)
{
    GSList *sinks;
    const mc_panel_plugin_t *chosen = NULL;
    gboolean activated;

    if (panel == NULL || paths == NULL || count == 0)
        return FALSE;

    sinks = panel_plugin_collect_file_list_sinks ();
    if (sinks == NULL)
        return FALSE;

    if (g_slist_next (sinks) == NULL)
    {
        /* Exactly one sink -- activate directly. */
        chosen = (const mc_panel_plugin_t *) sinks->data;
    }
    else
    {
        /* Multiple sinks -- let the user pick. */
        Listbox *lb;
        int result;
        const GSList *it;

        lb = listbox_window_new (12, 40, _ ("Send to panel"), "[Panel Plugins]");
        for (it = sinks; it != NULL; it = g_slist_next (it))
        {
            const mc_panel_plugin_t *pp = (const mc_panel_plugin_t *) it->data;
            const char *label_text = pp->display_name != NULL ? pp->display_name : pp->name;

            listbox_add_item (lb->list, LISTBOX_APPEND_AT_END, 0, label_text, (void *) pp, FALSE);
        }
        result = listbox_run (lb);
        if (result >= 0)
            chosen = (const mc_panel_plugin_t *) g_slist_nth_data (sinks, (guint) result);
    }

    activated =
        (chosen != NULL) && panel_plugin_open_file_list_one (panel, chosen, paths, count, label);

    g_slist_free (sinks);
    return activated;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_plugin_run_action (WPanel *panel, const mc_panel_plugin_t *plugin, int action_index)
{
    mc_panel_host_t *host;
    void *pdata;
    const char *path;

    if (panel == NULL || plugin == NULL)
        return;
    if (plugin->actions == NULL || action_index < 0 || action_index >= plugin->action_count
        || (plugin->actions[action_index].callback == NULL
            && plugin->run_action_with_plugin == NULL))
        return;

    /* Keep the previous plugin active until the action returns replacement data. */
    host = g_new0 (mc_panel_host_t, 1);
    panel_plugin_init_host (host, panel);

    /* Capture the cwd before the callback; the action may update the panel cwd
       while building its listing. */
    {
        vfs_path_t *saved_pre_cwd = panel->plugin_pre_cwd_vpath;
        gboolean was_plugin = panel->is_plugin_panel;
        panel->plugin_pre_cwd_vpath =
            was_plugin ? saved_pre_cwd : vfs_path_clone (panel->cwd_vpath);

        path = vfs_path_as_str (panel->cwd_vpath);
        pdata = plugin->run_action_with_plugin != NULL
            ? plugin->run_action_with_plugin (plugin,
                                              panel->plugin == plugin ? panel->plugin_data : NULL,
                                              host, path, action_index)
            : plugin->actions[action_index].callback (host, path);
        if (pdata == NULL)
        {
            /* action performed standalone operation (e.g. dialog), no panel activation */
            if (!was_plugin)
            {
                vfs_path_free (panel->plugin_pre_cwd_vpath, TRUE);
                panel->plugin_pre_cwd_vpath = saved_pre_cwd;
            }
            if (host->focus_after != NULL)
            {
                /* Keep plugin panels in plugin mode. */
                if (was_plugin)
                    panel_plugin_refresh (panel);
                else
                    panel_reload (panel);
                panel_set_current_by_name (panel, host->focus_after);
                g_free (host->focus_after);
            }
            g_free (host);
            return;
        }
        (void) saved_pre_cwd;
    }

    if (panel->is_plugin_panel)
    {
        /* Keep pre-plugin cwd while replacing the plugin instance. */
        vfs_path_t *keep = panel->plugin_pre_cwd_vpath;
        panel->plugin_pre_cwd_vpath = NULL;
        panel_plugin_close (panel);
        panel->plugin_pre_cwd_vpath = keep;
    }

    panel->plugin_data = pdata;
    panel->plugin = plugin;
    panel->plugin_host = host;
    panel->is_plugin_panel = TRUE;
    panel->is_panelized = TRUE;
    panel->plugin_base_list_format = panel->list_format;

    /* populate dir list */
    panel_clean_dir (panel);
    /* panel_clean_dir resets flags - restore them */
    panel->is_panelized = TRUE;
    panel->is_plugin_panel = TRUE;

    panel_plugin_apply_default_columns_format (panel);

    if (plugin->default_sort_id != NULL)
    {
        const panel_field_t *sf;

        sf = panel_get_field_by_id (plugin->default_sort_id);
        if (sf != NULL)
        {
            panel_set_sort_order (panel, sf);
            panel->sort_info.reverse = plugin->default_sort_reverse ? 1 : 0;
        }
    }

    dir_list_init (&panel->dir);
    plugin->get_items (panel->plugin_data, &panel->dir);

    panel_re_sort (panel);
    panel->dirty = TRUE;

    /* Record plugin panel in directory history. */
    if (panel->plugin->proto != NULL && panel->plugin->get_title != NULL)
    {
        const char *title = panel->plugin->get_title (panel->plugin_data);
        if (title != NULL)
        {
            const char *path_tail = (title[0] != '\0') ? title : "/";
            char *plugin_path = g_strdup_printf ("%s:%s", panel->plugin->proto, path_tail);
            panel_directory_history_add_path (panel, plugin_path);
            g_free (plugin_path);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_run_action_by_name (WPanel *panel, const char *plugin_name, int action_index)
{
    const mc_panel_plugin_t *p;

    if (panel == NULL || plugin_name == NULL)
        return FALSE;

    p = mc_panel_plugin_find_by_name (plugin_name);
    if (p == NULL || p->actions == NULL || action_index < 0 || action_index >= p->action_count)
        return FALSE;

    panel_plugin_run_action (panel, p, action_index);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_plugin_select_and_activate (WPanel *panel)
{
    const GSList *plugins;
    const GSList *iter;
    Listbox *listbox;
    int count = 0;
    int result;

    plugins = mc_panel_plugin_list ();
    if (plugins == NULL)
    {
        message (D_ERROR, _ ("Plugin panel"), "%s", _ ("No panel plugins are loaded."));
        return;
    }

    listbox = listbox_window_new (12, 40, _ ("Plugin panel"), "[Panel Plugins]");

    for (iter = plugins; iter != NULL; iter = g_slist_next (iter))
    {
        const mc_panel_plugin_t *p = (const mc_panel_plugin_t *) iter->data;
        const char *label = p->display_name != NULL ? p->display_name : p->name;

        listbox_add_item (listbox->list, LISTBOX_APPEND_AT_END, 0, label, (void *) p, FALSE);
        count++;
    }

    if (count == 0)
    {
        widget_destroy (WIDGET (listbox->dlg));
        return;
    }

    result = listbox_run (listbox);
    if (result >= 0)
    {
        const mc_panel_plugin_t *selected = NULL;
        int i = 0;

        for (iter = plugins; iter != NULL; iter = g_slist_next (iter), i++)
        {
            if (i == result)
            {
                selected = (const mc_panel_plugin_t *) iter->data;
                break;
            }
        }

        if (selected != NULL)
            panel_plugin_activate (panel, selected, vfs_path_as_str (panel->cwd_vpath));
    }
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_plugin_drive_change (WPanel *panel)
{
    const GSList *plugins;
    const GSList *iter;
    Listbox *listbox;
    int count = 0;
    int max_label_len = 0;
    int lines, cols;
    int result;
    const WRect *r;
    int center_y, center_x;
    const char *title_str;

    plugins = mc_panel_plugin_list ();
    if (plugins == NULL)
    {
        message (D_ERROR, _ ("Plugin panel"), "%s", _ ("No panel plugins are loaded."));
        return FALSE;
    }

    /* first pass: count items and find max label width */
    for (iter = plugins; iter != NULL; iter = g_slist_next (iter))
    {
        const mc_panel_plugin_t *p = (const mc_panel_plugin_t *) iter->data;
        int len;

        if ((p->flags & MC_PPF_SHOW_IN_DRIVE_MENU) == 0)
            continue;

        len = str_term_width1 (p->display_name != NULL ? p->display_name : p->name);
        if (len > max_label_len)
            max_label_len = len;
        count++;
    }

    if (count == 0)
    {
        message (D_ERROR, _ ("Change drive"), "%s",
                 _ ("No plugins with drive menu support are loaded."));
        return FALSE;
    }

    title_str = _ ("Change drive");
    cols = MAX (max_label_len + 4, str_term_width1 (title_str) + 4);
    lines = count;

    r = &CONST_WIDGET (panel)->rect;
    center_y = r->y + r->lines / 2;
    center_x = r->x + r->cols / 2;

    listbox =
        listbox_window_centered_new (center_y, center_x, lines, cols, title_str, "[Panel Plugins]");

    /* second pass: populate */
    for (iter = plugins; iter != NULL; iter = g_slist_next (iter))
    {
        const mc_panel_plugin_t *p = (const mc_panel_plugin_t *) iter->data;

        if ((p->flags & MC_PPF_SHOW_IN_DRIVE_MENU) == 0)
            continue;

        {
            const char *label = p->display_name != NULL ? p->display_name : p->name;

            listbox_add_item (listbox->list, LISTBOX_APPEND_AT_END, 0, label, (void *) p, FALSE);
        }
    }

    result = listbox_run (listbox);
    if (result < 0)
        return FALSE;

    {
        const mc_panel_plugin_t *selected = NULL;
        int i = 0;

        for (iter = plugins; iter != NULL; iter = g_slist_next (iter))
        {
            const mc_panel_plugin_t *p = (const mc_panel_plugin_t *) iter->data;

            if ((p->flags & MC_PPF_SHOW_IN_DRIVE_MENU) == 0)
                continue;

            if (i == result)
            {
                selected = p;
                break;
            }
            i++;
        }

        if (selected == NULL)
            return FALSE;

        panel_plugin_activate (panel, selected, vfs_path_as_str (panel->cwd_vpath));
        return TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Navigate panel to a history path: plugin or filesystem.
 *
 * If path starts with a known plugin prefix, the appropriate plugin is activated.
 * Otherwise the panel navigates to the filesystem path.  If the panel is currently
 * in plugin mode it is closed before the filesystem cd.
 *
 * @param panel          target panel
 * @param path           history entry string
 * @param add_to_history TRUE  -- use panel_do_cd (records new history entry);
 *                       FALSE -- use panel_do_cd_int (silent, used for prev/next)
 * @param show_error     show cd_error_message on filesystem cd failure
 * @return               TRUE on success
 */
gboolean
panel_navigate_to_path (WPanel *panel, const char *path, gboolean add_to_history,
                        gboolean show_error)
{
    if (panel == NULL || path == NULL)
        return FALSE;

    if (panel_plugin_find_by_path (path) != NULL)
        return panel_plugin_activate_by_path (panel, path);

    if (panel->is_plugin_panel)
        panel_plugin_close (panel);

    vfs_path_t *vpath = vfs_path_from_str (path);
    gboolean ok;

    if (add_to_history)
        ok = panel_do_cd (panel, vpath, cd_exact);
    else
        ok = panel_do_cd_int (panel, vpath, cd_exact);

    vfs_path_free (vpath, TRUE);

    if (!ok && show_error)
        cd_error_message (path);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

/* Returns owned menu_entry_t nodes in an owned GList. */
GList *
panel_plugin_collect_menu_entries (const char *menu_name)
{
    const GSList *plugins;
    GList *entries = NULL;
    gsize plugin_idx = 0;

    if (menu_name == NULL)
        return NULL;

    for (plugins = mc_panel_plugin_list (); plugins != NULL;
         plugins = g_slist_next (plugins), plugin_idx++)
    {
        const mc_panel_plugin_t *pp = (const mc_panel_plugin_t *) plugins->data;
        int i;

        if (pp->cmd_menu_entries == NULL || pp->cmd_menu_entry_count <= 0)
            continue;

        for (i = 0; i < pp->cmd_menu_entry_count; i++)
        {
            const mc_pp_cmd_menu_entry_t *e = &pp->cmd_menu_entries[i];
            const char *target = e->menu_name != NULL ? e->menu_name : MC_PP_MENU_COMMAND;
            menu_entry_t *me;
            long cmd;

            if (g_strcmp0 (target, menu_name) != 0)
                continue;

            if (e->label == NULL)
            {
                entries = g_list_prepend (entries, menu_separator_new ());
                continue;
            }

            if (pp->actions == NULL || e->action_index < 0 || e->action_index >= pp->action_count)
                continue;

            cmd = CK_PluginActionBase + (long) plugin_idx * 16 + (long) e->action_index;
            me = menu_entry_new (_ (e->label), cmd);
            if (e->shortcut != NULL)
                menu_entry_set_shortcut (me, e->shortcut);
            entries = g_list_prepend (entries, me);
        }
    }

    return entries != NULL ? g_list_reverse (entries) : NULL;
}

/* --------------------------------------------------------------------------------------------- */
