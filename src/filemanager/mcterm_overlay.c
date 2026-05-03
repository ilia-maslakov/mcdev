/*
   mcterm overlay helpers for Midnight Commander filemanager panels.

   Copyright (C) 2026
   Free Software Foundation, Inc.

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

#include "lib/global.h"
#include "lib/widget.h"
#include "lib/widget/dialog-switch.h"
#include "lib/widget/mouse.h"

#include "src/mcterm/mcterm.h"

#include "filemanager.h"
#include "layout.h"
#include "mcterm_overlay.h"

/*** file scope functions ************************************************************************/

static gboolean
mcterm_overlay_is_visible (WMcTerm *term)
{
    return (term != NULL && widget_get_state (mcterm_widget (term), WST_VISIBLE));
}

/* --------------------------------------------------------------------------------------------- */

static Widget *
mcterm_overlay_get_visible_panel (int idx)
{
    Widget *pw;

    pw = get_panel_widget (idx);
    return (pw != NULL && widget_get_state (pw, WST_VISIBLE)) ? pw : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static int
mcterm_overlay_mouse_handler (Widget *w, Gpm_Event *event)
{
    Widget *pw;

    pw = mcterm_overlay_get_visible_panel (0);
    if (pw != NULL && mouse_global_in_widget (event, pw))
        return MOU_UNHANDLED;

    pw = mcterm_overlay_get_visible_panel (1);
    if (pw != NULL && mouse_global_in_widget (event, pw))
        return MOU_UNHANDLED;

    return mouse_handle_event (w, event);
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_overlay_after_redraw_cb (void *data)
{
    GList *l;
    WMcTerm *term = (WMcTerm *) data;

    mcterm_overlay_redraw_visible_panels (term);

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

/*** public functions ****************************************************************************/

void
mcterm_overlay_install (WMcTerm *term)
{
    if (term == NULL)
        return;

    mcterm_set_after_redraw_callback (term, mcterm_overlay_after_redraw_cb, term);
    mcterm_widget (term)->mouse_handler = mcterm_overlay_mouse_handler;
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_overlay_redraw_visible_panels (WMcTerm *term)
{
    Widget *pw;
    WPanel *active_panel;
    int i;

    if (!mcterm_overlay_is_visible (term))
        return;

    active_panel = (current_panel != NULL && widget_get_state (WIDGET (current_panel), WST_VISIBLE))
        ? current_panel
        : NULL;

    for (i = 0; i < 2; i++)
    {
        pw = mcterm_overlay_get_visible_panel (i);
        if (pw != NULL)
        {
            WPanel *p = PANEL (pw);
            gboolean saved = p->active;

            p->active = (p == active_panel);
            widget_draw (pw);
            p->active = saved;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_overlay_cycle_focus (WMcTerm *term)
{
    WGroup *g;
    Widget *focused;
    Widget *pw0, *pw1;
    gboolean p0_vis, p1_vis;
    Widget *next;

    if (!mcterm_overlay_is_visible (term))
        return;

    g = GROUP (filemanager);
    focused = g->current != NULL ? WIDGET (g->current->data) : NULL;
    pw0 = get_panel_widget (0);
    pw1 = get_panel_widget (1);
    p0_vis = pw0 != NULL && widget_get_state (pw0, WST_VISIBLE);
    p1_vis = pw1 != NULL && widget_get_state (pw1, WST_VISIBLE);

    if (focused == pw0)
        next = p1_vis ? pw1 : mcterm_widget (term);
    else if (focused == pw1)
        next = p0_vis ? pw0 : mcterm_widget (term);
    else
    {
        Widget *cur = current_panel != NULL ? WIDGET (current_panel) : NULL;

        if (cur == pw0)
            next = p1_vis ? pw1 : (p0_vis ? pw0 : mcterm_widget (term));
        else if (cur == pw1)
            next = p0_vis ? pw0 : (p1_vis ? pw1 : mcterm_widget (term));
        else
            next = p0_vis ? pw0 : (p1_vis ? pw1 : NULL);
    }

    if (next != NULL)
        widget_select (next);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_overlay_panel_has_focus (void)
{
    WGroup *g;
    Widget *focused;
    Widget *pw;

    g = GROUP (filemanager);
    focused = g->current != NULL ? WIDGET (g->current->data) : NULL;

    pw = mcterm_overlay_get_visible_panel (0);
    if (pw == focused)
        return TRUE;

    pw = mcterm_overlay_get_visible_panel (1);
    return (pw == focused);
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_overlay_show_other_panel_after_open (WMcTerm *term, gboolean right_command)
{
    Widget *pw;

    if (!mcterm_overlay_is_visible (term))
        return;

    pw = get_panel_widget (right_command ? 0 : 1);
    if (pw == NULL)
        return;

    widget_show (pw);

    if (current_panel == NULL || !widget_get_state (WIDGET (current_panel), WST_VISIBLE))
        current_panel = PANEL (pw);
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_overlay_toggle_panel (WMcTerm *term, int idx)
{
    Widget *pw;

    if (!mcterm_overlay_is_visible (term))
        return;

    pw = get_panel_widget (idx);
    if (pw == NULL)
        return;

    if (widget_get_state (pw, WST_VISIBLE))
    {
        widget_hide (pw);
        if (current_panel == PANEL (pw))
        {
            Widget *other = mcterm_overlay_get_visible_panel (1 - idx);

            current_panel = (other != NULL) ? PANEL (other) : PANEL (pw);
        }
        widget_select (mcterm_widget (term));
        widget_draw (mcterm_widget (term));
    }
    else
        widget_show (pw);
}

/* --------------------------------------------------------------------------------------------- */
