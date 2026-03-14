/*
   Panel plugin UI -- activation, host interface, drive menu.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.ru>, 2026

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

#include "lib/global.h"

#include "lib/strutil.h"
#include "lib/vfs/vfs.h"
#include "lib/widget.h"
#include "lib/panel-plugin.h"

#include "src/execute.h"

#include "dir.h"
#include "layout.h"
#include "cd.h"

#include "panel.h"

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
    char *q_left, *q_right, *cmd;

    (void) host;

    if (left_path == NULL || right_path == NULL || *left_path == '\0' || *right_path == '\0')
        return FALSE;

    if (g_find_program_in_path ("mcdiff") == NULL)
        return FALSE;

    q_left = g_shell_quote (left_path);
    q_right = g_shell_quote (right_path);
    cmd = g_strdup_printf ("mcdiff %s %s", q_left, q_right);
    shell_execute (cmd, EXECUTE_INTERNAL);
    g_free (cmd);
    g_free (q_right);
    g_free (q_left);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
host_close_plugin_impl (mc_panel_host_t *host, const char *dir_path)
{
    WPanel *panel = (WPanel *) host->host_data;

    panel_plugin_close (panel);

    if (dir_path != NULL && dir_path[0] != '\0')
    {
        vfs_path_t *cd_vpath;

        cd_vpath = vfs_path_from_str (dir_path);
        if (!panel_do_cd (panel, cd_vpath, cd_parse_command))
            cd_error_message (dir_path);
        vfs_path_free (cd_vpath, TRUE);
    }
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
panel_plugin_init_host (mc_panel_host_t *host, WPanel *panel)
{
    host->refresh = host_refresh_impl;
    host->set_hint = host_set_hint_impl;
    host->message = host_message_impl;
    host->run_command = host_run_command_impl;
    host->open_diff = host_open_diff_impl;
    host->close_plugin = host_close_plugin_impl;
    host->get_marked_count = host_get_marked_count_impl;
    host->get_next_marked = host_get_next_marked_impl;
    host->get_current = host_get_current_impl;
    host->host_data = panel;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
panel_plugin_reload (WPanel *panel)
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

    panel_clean_dir (panel);
    // panel_clean_dir resets is_panelized; restore plugin state
    panel->is_panelized = TRUE;
    panel->is_plugin_panel = TRUE;

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
panel_plugin_activate (WPanel *panel, const mc_panel_plugin_t *plugin, const char *open_path)
{
    mc_panel_host_t *host;

    if (panel == NULL || plugin == NULL)
        return;

    // close any previous plugin
    if (panel->is_plugin_panel)
        panel_plugin_close (panel);

    // build host interface
    host = g_new0 (mc_panel_host_t, 1);
    panel_plugin_init_host (host, panel);

    panel->plugin_data = plugin->open (host, open_path);
    if (panel->plugin_data == NULL)
    {
        g_free (host);
        return;
    }

    panel->plugin = plugin;
    panel->plugin_host = host;
    panel->is_plugin_panel = TRUE;
    panel->is_panelized = TRUE;

    // populate dir list
    panel_clean_dir (panel);
    // panel_clean_dir resets flags - restore them
    panel->is_panelized = TRUE;
    panel->is_plugin_panel = TRUE;

    panel_plugin_apply_default_columns_format (panel);

    dir_list_init (&panel->dir);
    plugin->get_items (panel->plugin_data, &panel->dir);

    panel_re_sort (panel);
    panel->dirty = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_plugin_close (WPanel *panel)
{
    if (panel == NULL || !panel->is_plugin_panel)
        return;

    if (panel->plugin != NULL && panel->plugin_data != NULL)
        panel->plugin->close (panel->plugin_data);

    panel->plugin = NULL;
    panel->plugin_data = NULL;
    panel->is_plugin_panel = FALSE;

    if (panel->plugin_host != NULL)
        g_free (panel->plugin_host->focus_after);
    g_free (panel->plugin_host);
    panel->plugin_host = NULL;

    panel->is_panelized = FALSE;
    panel_reload (panel);
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

void
panel_plugin_run_action (WPanel *panel, const mc_panel_plugin_t *plugin, int action_index)
{
    mc_panel_host_t *host;
    void *pdata;
    const char *path;

    if (panel == NULL || plugin == NULL)
        return;
    if (plugin->actions == NULL || action_index < 0 || action_index >= plugin->action_count)
        return;

    /* close any previous plugin */
    if (panel->is_plugin_panel)
        panel_plugin_close (panel);

    /* build host interface */
    host = g_new0 (mc_panel_host_t, 1);
    panel_plugin_init_host (host, panel);

    path = vfs_path_as_str (panel->cwd_vpath);
    pdata = plugin->actions[action_index].callback (host, path);
    if (pdata == NULL)
    {
        /* action performed standalone operation (e.g. dialog), no panel activation */
        if (host->focus_after != NULL)
        {
            panel_reload (panel);
            panel_set_current_by_name (panel, host->focus_after);
            g_free (host->focus_after);
        }
        g_free (host);
        return;
    }

    panel->plugin_data = pdata;
    panel->plugin = plugin;
    panel->plugin_host = host;
    panel->is_plugin_panel = TRUE;
    panel->is_panelized = TRUE;

    /* populate dir list */
    panel_clean_dir (panel);
    /* panel_clean_dir resets flags - restore them */
    panel->is_panelized = TRUE;
    panel->is_plugin_panel = TRUE;

    panel_plugin_apply_default_columns_format (panel);

    dir_list_init (&panel->dir);
    plugin->get_items (panel->plugin_data, &panel->dir);

    panel_re_sort (panel);
    panel->dirty = TRUE;
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
        // shouldn't happen since we checked above, but be safe
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
        {
            if (selected->actions != NULL && selected->action_count > 0)
            {
                /* show second listbox with available actions */
                Listbox *action_lb;
                int action_result;
                int ai;
                const char *title =
                    selected->display_name != NULL ? selected->display_name : selected->name;

                action_lb = listbox_window_new (12, 40, title, "[Panel Plugins]");

                for (ai = 0; ai < selected->action_count; ai++)
                    listbox_add_item (action_lb->list, LISTBOX_APPEND_AT_END, 0,
                                      _ (selected->actions[ai].label), NULL, FALSE);

                action_result = listbox_run (action_lb);
                if (action_result >= 0 && action_result < selected->action_count)
                    panel_plugin_run_action (panel, selected, action_result);
            }
            else
                panel_plugin_activate (panel, selected, vfs_path_as_str (panel->cwd_vpath));
        }
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

        if (selected->actions != NULL && selected->action_count > 0)
        {
            Listbox *action_lb;
            int action_result;
            int ai;
            int action_max_len = 0;
            int action_cols;
            const char *action_title =
                selected->display_name != NULL ? selected->display_name : selected->name;

            for (ai = 0; ai < selected->action_count; ai++)
            {
                int len = str_term_width1 (_ (selected->actions[ai].label));

                if (len > action_max_len)
                    action_max_len = len;
            }

            action_cols = MAX (action_max_len + 4, str_term_width1 (action_title) + 4);

            action_lb = listbox_window_centered_new (center_y, center_x, selected->action_count,
                                                     action_cols, action_title, "[Panel Plugins]");

            for (ai = 0; ai < selected->action_count; ai++)
                listbox_add_item (action_lb->list, LISTBOX_APPEND_AT_END, 0,
                                  _ (selected->actions[ai].label), NULL, FALSE);

            action_result = listbox_run (action_lb);
            if (action_result >= 0 && action_result < selected->action_count)
            {
                panel_plugin_run_action (panel, selected, action_result);
                return TRUE;
            }
            return FALSE;
        }

        panel_plugin_activate (panel, selected, vfs_path_as_str (panel->cwd_vpath));
        return TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */
