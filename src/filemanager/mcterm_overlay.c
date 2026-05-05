/*
   File manager mcterm overlay controller.

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

/** \file mcterm_overlay.c
 *  \brief Source: file manager mcterm overlay controller
 */

#include <config.h>

#include "mcterm_overlay.h"

#ifdef ENABLE_MCTERM

#include "lib/global.h"
#include "lib/keybind.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/vfs/vfs.h"
#include "lib/widget/dialog-switch.h"
#include "lib/widget/mouse.h"

#include "src/execute.h"
#ifdef ENABLE_SUBSHELL
#include "src/subshell/subshell.h"
#endif
#include "src/mcterm/mcterm.h"
#include "src/mcterm/mcterm_cwd.h"

#include "command.h"
#include "filemanager.h"
#include "layout.h"

/*** file scope variables ************************************************************************/

static WMcTerm *mcterm_panel = NULL;
static gboolean mcterm_mode = FALSE;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
mcterm_overlay_live (void)
{
    return (mcterm_panel != NULL && mcterm_is_alive (mcterm_panel));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mcterm_overlay_ready (void)
{
    return (mcterm_overlay_live () && mcterm_shell_at_prompt (mcterm_panel)
            && mcterm_osc7_capable (mcterm_panel));
}

/* --------------------------------------------------------------------------------------------- */

static Widget *
mcterm_overlay_widget (void)
{
    return mcterm_widget (mcterm_panel);
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_overlay_rect (const WRect *mwr, WRect *r)
{
    int start_y = mwr->y + (menubar_visible ? 1 : 0);
    int height = mwr->lines - (menubar_visible ? 1 : 0) - (mc_global.keybar_visible ? 1 : 0)
        - (command_prompt ? 1 : 0);

    *r = (WRect) { start_y, mwr->x, MAX (height, 1), mwr->cols };
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_overlay_draw_cmdline_row (void)
{
    int cmdline_y, cursor_col;
    const WRect *mwr;

    if (!command_prompt || !mcterm_overlay_ready ())
        return;

    mwr = &CONST_WIDGET (filemanager)->rect;
    cmdline_y = WIDGET (cmdline)->rect.y;
    cursor_col = mcterm_cursor_col (mcterm_panel);
    if (cursor_col < 0 || cursor_col >= mwr->cols)
        cursor_col = 0;

    mcterm_draw_prompt_row (mcterm_panel, cmdline_y);
    widget_set_size (WIDGET (cmdline), cmdline_y, mwr->x + cursor_col, 1, mwr->cols - cursor_col);
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_overlay_sync_panel_from_shell (void)
{
    char *new_cwd;

    if (!mcterm_mode || current_panel == NULL || !mcterm_overlay_ready ()
        || !vfs_file_is_local (current_panel->cwd_vpath))
        return;

    new_cwd = mcterm_cwd_on_exit (mcterm_panel, vfs_path_as_str (current_panel->cwd_vpath));
    if (new_cwd != NULL)
    {
        vfs_path_t *vp = vfs_path_from_str (new_cwd);

        if (vp != NULL)
        {
            panel_cd (current_panel, vp, cd_exact);
            vfs_path_free (vp, TRUE);
        }
        g_free (new_cwd);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_overlay_sync_shell_to_panel (void)
{
    const char *panel_cwd;
    char *shell_cwd;

    if (current_panel == NULL || !vfs_file_is_local (current_panel->cwd_vpath)
        || !mcterm_overlay_ready ())
        return;

    panel_cwd = vfs_path_as_str (current_panel->cwd_vpath);
    shell_cwd = mcterm_cwd_on_exit (mcterm_panel, panel_cwd);

    if (shell_cwd != NULL)
    {
        char *quoted = g_shell_quote (panel_cwd);
        char *cmd = g_strdup_printf ("cd %s", quoted);

        mcterm_send_internal_line (mcterm_panel, cmd);
        g_free (cmd);
        g_free (quoted);
        g_free (shell_cwd);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_overlay_prompt_ready_cb (void *data)
{
    (void) data;

    if (!mcterm_mode || mcterm_panel == NULL)
        return;

    mcterm_overlay_sync_panel_from_shell ();

    if (!command_prompt)
        return;

    mcterm_overlay_draw_cmdline_row ();
    send_message (WIDGET (cmdline), NULL, MSG_CURSOR, 0, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_overlay_after_redraw_cb (void *data)
{
    GList *l;

    (void) data;

    mcterm_overlay_draw_visible_panels ();

    if (the_menubar != NULL && the_menubar->is_dropped)
        widget_draw (WIDGET (the_menubar));

    for (l = top_dlg; l != NULL; l = g_list_next (l))
        if (WIDGET (l->data) == WIDGET (filemanager))
            break;
    if (l == NULL)
        return;
    for (l = g_list_previous (l); l != NULL; l = g_list_previous (l))
        widget_draw (WIDGET (l->data));
}

/* --------------------------------------------------------------------------------------------- */

static int
mcterm_overlay_mouse_handler (Widget *w, Gpm_Event *event)
{
    Widget *pw;

    pw = get_panel_widget (0);
    if (pw != NULL && widget_get_state (pw, WST_VISIBLE) && mouse_global_in_widget (event, pw))
        return MOU_UNHANDLED;

    pw = get_panel_widget (1);
    if (pw != NULL && widget_get_state (pw, WST_VISIBLE) && mouse_global_in_widget (event, pw))
        return MOU_UNHANDLED;

    return mouse_handle_event (w, event);
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_overlay_discard_dead_terminal (void)
{
    if (mcterm_panel != NULL && !mcterm_is_alive (mcterm_panel))
    {
        group_remove_widget (mcterm_overlay_widget ());
        mcterm_free (mcterm_panel);
        mcterm_panel = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mcterm_overlay_panel_focused (Widget *focused)
{
    Widget *pw;

    pw = get_panel_widget (0);
    if (pw != NULL && pw == focused && widget_get_state (pw, WST_VISIBLE))
        return TRUE;

    pw = get_panel_widget (1);
    return (pw != NULL && pw == focused && widget_get_state (pw, WST_VISIBLE));
}

/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_overlay_active (void)
{
    return mcterm_mode;
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_overlay_toggle (void)
{
    WGroup *g = GROUP (filemanager);
    const WRect *mwr = &CONST_WIDGET (filemanager)->rect;

    if (!mcterm_mode)
    {
        WRect r;

        mcterm_overlay_rect (mwr, &r);
        mcterm_overlay_discard_dead_terminal ();

        if (mcterm_panel == NULL)
        {
            const char *start_dir =
                (current_panel != NULL && vfs_file_is_local (current_panel->cwd_vpath))
                ? vfs_path_as_str (current_panel->cwd_vpath)
                : NULL;

            mcterm_panel = mcterm_new (&r, start_dir);
            if (mcterm_panel == NULL)
            {
                toggle_subshell ();
                return;
            }

            mcterm_set_prompt_callback (mcterm_panel, mcterm_overlay_prompt_ready_cb, NULL);
            mcterm_set_after_redraw_callback (mcterm_panel, mcterm_overlay_after_redraw_cb, NULL);
            mcterm_overlay_widget ()->mouse_handler = mcterm_overlay_mouse_handler;
            group_add_widget (g, mcterm_overlay_widget ());
        }
        else
        {
            widget_set_size_rect (mcterm_overlay_widget (), &r);
            widget_show (mcterm_overlay_widget ());
            mcterm_set_after_redraw_callback (mcterm_panel, mcterm_overlay_after_redraw_cb, NULL);
            mcterm_overlay_widget ()->mouse_handler = mcterm_overlay_mouse_handler;
            mcterm_overlay_sync_shell_to_panel ();
        }

        widget_hide (get_panel_widget (0));
        widget_hide (get_panel_widget (1));
        widget_hide (WIDGET (the_hint));
        if (command_prompt)
        {
            widget_hide (WIDGET (the_prompt));
            widget_set_size (WIDGET (cmdline), WIDGET (cmdline)->rect.y, mwr->x, 1, mwr->cols);
        }

#ifdef ENABLE_SUBSHELL
        if (mc_global.tty.use_subshell)
            delete_select_channel (mc_global.tty.subshell_pty);
#endif

        mcterm_mode = TRUE;
        widget_set_options (WIDGET (filemanager), WOP_WANT_TAB, TRUE);
        widget_select (mcterm_overlay_widget ());

        do_refresh ();
    }
    else
    {
        if (mcterm_panel != NULL)
        {
            if (!mcterm_is_alive (mcterm_panel))
            {
                group_remove_widget (mcterm_overlay_widget ());
                mcterm_free (mcterm_panel);
                mcterm_panel = NULL;
            }
            else
            {
                widget_hide (mcterm_overlay_widget ());
                mcterm_overlay_sync_panel_from_shell ();
            }
        }

        widget_show (get_panel_widget (0));
        widget_show (get_panel_widget (1));
        widget_set_visibility (WIDGET (the_hint), mc_global.message_visible);
        if (command_prompt)
            widget_show (WIDGET (the_prompt));

#ifdef ENABLE_SUBSHELL
        if (mc_global.tty.use_subshell)
            add_select_channel (mc_global.tty.subshell_pty, load_prompt, NULL);
#endif

        mcterm_mode = FALSE;
        widget_set_options (WIDGET (filemanager), WOP_WANT_TAB, FALSE);
        layout_change ();
        widget_select (WIDGET (current_panel));
        do_refresh ();
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_overlay_destroy (void)
{
    mcterm_panel = NULL;
    mcterm_mode = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_overlay_draw_visible_panels (void)
{
    Widget *pw;
    WPanel *active_panel;

    if (!mcterm_mode || mcterm_panel == NULL)
        return;

    active_panel = (current_panel != NULL && widget_get_state (WIDGET (current_panel), WST_VISIBLE))
        ? current_panel
        : NULL;

    pw = get_panel_widget (0);
    if (pw != NULL && widget_get_state (pw, WST_VISIBLE))
    {
        WPanel *p = PANEL (pw);
        gboolean saved = p->active;

        p->active = (p == active_panel);
        widget_draw (pw);
        p->active = saved;
    }

    pw = get_panel_widget (1);
    if (pw != NULL && widget_get_state (pw, WST_VISIBLE))
    {
        WPanel *p = PANEL (pw);
        gboolean saved = p->active;

        p->active = (p == active_panel);
        widget_draw (pw);
        p->active = saved;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_overlay_after_filemanager_draw (void)
{
    mcterm_overlay_draw_visible_panels ();

    if (mcterm_mode && mcterm_overlay_ready ())
    {
        mcterm_overlay_draw_cmdline_row ();
        send_message (WIDGET (cmdline), NULL, MSG_CURSOR, 0, NULL);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_overlay_resize (const WRect *r)
{
    WRect tr;

    if (!mcterm_mode || mcterm_panel == NULL)
        return;

    mcterm_overlay_rect (r, &tr);
    widget_set_size_rect (mcterm_overlay_widget (), &tr);
    if (command_prompt)
        widget_set_size (WIDGET (cmdline), WIDGET (cmdline)->rect.y, r->x, 1, r->cols);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_overlay_complete_or_cycle_focus (void)
{
    const char *text;

    if (!mcterm_mode || !mcterm_overlay_ready ())
        return FALSE;

    text = input_get_ctext (cmdline);
    if (text != NULL && *text != '\0')
    {
        if (mcterm_send_tab_complete (mcterm_panel, text))
        {
            const WRect *mwr = &CONST_WIDGET (filemanager)->rect;

            input_assign_text (cmdline, "");
            widget_set_size (WIDGET (cmdline), WIDGET (cmdline)->rect.y, mwr->x, 1, mwr->cols);
            widget_draw (mcterm_overlay_widget ());
            mcterm_overlay_draw_visible_panels ();
            send_message (mcterm_overlay_widget (), NULL, MSG_CURSOR, 0, NULL);
            tty_refresh ();
        }

        return TRUE;
    }
    else
    {
        WGroup *g_fm = GROUP (filemanager);
        Widget *focused = g_fm->current != NULL ? WIDGET (g_fm->current->data) : NULL;
        Widget *pw0 = get_panel_widget (0);
        Widget *pw1 = get_panel_widget (1);
        gboolean p0_vis = pw0 != NULL && widget_get_state (pw0, WST_VISIBLE);
        gboolean p1_vis = pw1 != NULL && widget_get_state (pw1, WST_VISIBLE);
        Widget *next;

        if (focused == pw0)
            next = p1_vis ? pw1 : mcterm_overlay_widget ();
        else if (focused == pw1)
            next = p0_vis ? pw0 : mcterm_overlay_widget ();
        else
        {
            Widget *cur = current_panel != NULL ? WIDGET (current_panel) : NULL;

            if (cur == pw0)
                next = p1_vis ? pw1 : (p0_vis ? pw0 : mcterm_overlay_widget ());
            else if (cur == pw1)
                next = p0_vis ? pw0 : (p1_vis ? pw1 : mcterm_overlay_widget ());
            else
                next = p0_vis ? pw0 : (p1_vis ? pw1 : NULL);
        }

        if (next != NULL)
        {
            widget_select (next);
            mcterm_overlay_draw_visible_panels ();
            tty_refresh ();
        }

        return TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */

cb_ret_t
mcterm_overlay_send_enter_if_cmdline_empty (void)
{
    if (mcterm_mode && mcterm_overlay_ready () && input_is_empty (cmdline))
        return send_message (mcterm_overlay_widget (), NULL, MSG_KEY, '\n', NULL);

    return MSG_NOT_HANDLED;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_overlay_show_panel_if_hidden (int idx)
{
    Widget *pw;

    if (!mcterm_mode || mcterm_panel == NULL)
        return FALSE;

    pw = get_panel_widget (idx);
    if (pw == NULL || widget_get_state (pw, WST_VISIBLE))
        return FALSE;

    widget_show (pw);
    mcterm_overlay_draw_visible_panels ();
    tty_refresh ();

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_overlay_toggle_panel_command (gboolean right_panel_command)
{
    if (!mcterm_mode)
    {
        mcterm_overlay_toggle ();
        if (mcterm_mode && mcterm_panel != NULL)
        {
            int other_idx = right_panel_command ? 0 : 1;
            Widget *pw = get_panel_widget (other_idx);

            if (pw != NULL)
            {
                widget_show (pw);
                if (current_panel == NULL
                    || !widget_get_state (WIDGET (current_panel), WST_VISIBLE))
                    current_panel = PANEL (pw);
            }

            mcterm_overlay_draw_visible_panels ();
            tty_refresh ();
        }
    }
    else if (mcterm_panel != NULL)
    {
        int idx = right_panel_command ? 1 : 0;
        Widget *pw = get_panel_widget (idx);

        if (pw != NULL)
        {
            if (widget_get_state (pw, WST_VISIBLE))
            {
                widget_hide (pw);
                if (current_panel == PANEL (pw))
                {
                    Widget *other = get_panel_widget (1 - idx);

                    current_panel = (other != NULL && widget_get_state (other, WST_VISIBLE))
                        ? PANEL (other)
                        : PANEL (pw);
                }

                widget_select (mcterm_overlay_widget ());
                widget_draw (mcterm_overlay_widget ());
            }
            else
            {
                widget_show (pw);
                if (current_panel != NULL
                    && !widget_get_state (WIDGET (current_panel), WST_VISIBLE))
                    current_panel = PANEL (pw);
            }

            mcterm_overlay_draw_visible_panels ();
            tty_refresh ();
        }
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mcterm_overlay_cmdline_result_t
mcterm_overlay_run_cmdline (const char *cmd, gboolean is_cd, gboolean is_exit)
{
    if ((is_cd && !mcterm_mode) || (is_exit && !mcterm_mode) || !mcterm_overlay_ready ())
        return MCTERM_OVERLAY_CMDLINE_NOT_APPLICABLE;

    if (!mcterm_mode)
        mcterm_overlay_toggle ();

    if (!mcterm_overlay_live ())
        return MCTERM_OVERLAY_CMDLINE_NOT_APPLICABLE;

    return mcterm_send_line (mcterm_panel, cmd) ? MCTERM_OVERLAY_CMDLINE_SENT
                                                : MCTERM_OVERLAY_CMDLINE_HANDLED;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_overlay_panel_exec (const char *cmd)
{
    Widget *pw;
    gboolean panels_hidden = FALSE;

    if (!mcterm_mode)
        return FALSE;

    if (mcterm_panel == NULL || !mcterm_is_alive (mcterm_panel)
        || !mcterm_osc7_capable (mcterm_panel))
        return FALSE;

    if (!mcterm_shell_at_prompt (mcterm_panel))
    {
        message (D_ERROR, MSG_ERROR, "%s", _ ("The terminal is already running a command"));
        return TRUE;
    }

    pw = get_panel_widget (0);
    if (pw != NULL && widget_get_state (pw, WST_VISIBLE))
    {
        widget_hide (pw);
        panels_hidden = TRUE;
    }

    pw = get_panel_widget (1);
    if (pw != NULL && widget_get_state (pw, WST_VISIBLE))
    {
        widget_hide (pw);
        panels_hidden = TRUE;
    }

    if (panels_hidden)
    {
        widget_draw (mcterm_overlay_widget ());
        tty_refresh ();
    }

    return mcterm_send_line (mcterm_panel, cmd);
}

/* --------------------------------------------------------------------------------------------- */

cb_ret_t
mcterm_overlay_handle_key (Widget *w, int parm, mcterm_overlay_command_cb_t execute_command,
                           mcterm_overlay_enter_cb_t execute_cmdline_enter, void *data)
{
    long cmd;

    if (!mcterm_mode || mcterm_panel == NULL)
        return MSG_NOT_HANDLED;

    {
        WGroup *g = GROUP (filemanager);
        Widget *focused = g->current != NULL ? WIDGET (g->current->data) : NULL;

        if (mcterm_overlay_panel_focused (focused))
        {
            if (parm == '\n' && command_prompt && !input_is_empty (cmdline))
                return execute_cmdline_enter (data);

            cmd = widget_lookup_key (w, parm);
            if (cmd == CK_ChangePanel)
                return execute_command (cmd, data);

            return MSG_NOT_HANDLED;
        }
    }

    {
        gboolean in_alt = mcterm_in_alt_screen (mcterm_panel);
        gboolean at_prompt =
            mcterm_shell_at_prompt (mcterm_panel) && mcterm_osc7_capable (mcterm_panel);

        if ((parm == 0x0F || parm == XCTRL ('O')) && !in_alt)
            return MSG_NOT_HANDLED;

        if (!in_alt && at_prompt && input_is_empty (cmdline))
        {
            cmd = widget_lookup_key (w, parm);
            if (cmd == CK_ChangePanel)
                return execute_command (cmd, data);
        }

        if (in_alt || !at_prompt)
            return send_message (mcterm_overlay_widget (), NULL, MSG_KEY, parm, NULL);
    }

    cmd = widget_lookup_key (w, parm);
    if (cmd != CK_IgnoreKey)
        return execute_command (cmd, data);

    if (parm == KEY_UP)
    {
        send_message (WIDGET (cmdline), NULL, MSG_ACTION, CK_HistoryPrev, NULL);
        return MSG_HANDLED;
    }

    if (parm == KEY_DOWN)
    {
        send_message (WIDGET (cmdline), NULL, MSG_ACTION, CK_HistoryNext, NULL);
        return MSG_HANDLED;
    }

    send_message (WIDGET (cmdline), NULL, MSG_KEY, parm, NULL);
    return MSG_HANDLED;
}

#else /* !ENABLE_MCTERM */

#include "src/execute.h"

gboolean
mcterm_overlay_active (void)
{
    return FALSE;
}

void
mcterm_overlay_toggle (void)
{
    toggle_subshell ();
}

void
mcterm_overlay_destroy (void)
{
}

void
mcterm_overlay_draw_visible_panels (void)
{
}

void
mcterm_overlay_after_filemanager_draw (void)
{
}

void
mcterm_overlay_resize (const WRect *r)
{
    (void) r;
}

gboolean
mcterm_overlay_complete_or_cycle_focus (void)
{
    return FALSE;
}

cb_ret_t
mcterm_overlay_send_enter_if_cmdline_empty (void)
{
    return MSG_NOT_HANDLED;
}

gboolean
mcterm_overlay_show_panel_if_hidden (int idx)
{
    (void) idx;
    return FALSE;
}

gboolean
mcterm_overlay_toggle_panel_command (gboolean right_panel_command)
{
    (void) right_panel_command;
    return FALSE;
}

mcterm_overlay_cmdline_result_t
mcterm_overlay_run_cmdline (const char *cmd, gboolean is_cd, gboolean is_exit)
{
    (void) cmd;
    (void) is_cd;
    (void) is_exit;
    return MCTERM_OVERLAY_CMDLINE_NOT_APPLICABLE;
}

gboolean
mcterm_overlay_panel_exec (const char *cmd)
{
    (void) cmd;
    return FALSE;
}

cb_ret_t
mcterm_overlay_handle_key (Widget *w, int parm, mcterm_overlay_command_cb_t execute_command,
                           mcterm_overlay_enter_cb_t execute_cmdline_enter, void *data)
{
    (void) w;
    (void) parm;
    (void) execute_command;
    (void) execute_cmdline_enter;
    (void) data;
    return MSG_NOT_HANDLED;
}

#endif /* ENABLE_MCTERM */
