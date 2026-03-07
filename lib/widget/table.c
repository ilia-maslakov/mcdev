/*
   Widgets for the Midnight Commander

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

/** \file table.c
 *  \brief Source: WTable widget
 */

#include <config.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lib/global.h"

#include "lib/tty/tty.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/widget.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
table_free_row (gpointer data)
{
    g_strfreev ((char **) data);
}

/* --------------------------------------------------------------------------------------------- */

static void
table_free_check_row (gpointer data)
{
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */

static void
table_drawscroll (const WTable *t)
{
    const WRect *w = &CONST_WIDGET (t)->rect;
    int max_line = w->lines - 1;
    int line = 0;
    int i;

    /* top arrow */
    widget_gotoyx (t, 0, w->cols);
    if (t->top == 0)
        tty_print_one_vline (TRUE);
    else
        tty_print_char ('^');

    /* bottom arrow */
    widget_gotoyx (t, max_line, w->cols);
    if (t->top + w->lines >= t->nrows || w->lines >= t->nrows)
        tty_print_one_vline (TRUE);
    else
        tty_print_char ('v');

    /* thumb position */
    if (t->nrows != 0)
        line = 1 + ((t->current * (w->lines - 2)) / t->nrows);

    for (i = 1; i < max_line; i++)
    {
        widget_gotoyx (t, i, w->cols);
        if (i != line)
            tty_print_one_vline (TRUE);
        else
            tty_print_char ('*');
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
table_draw (WTable *t, gboolean focused)
{
    Widget *wt = WIDGET (t);
    const WRect *w = &CONST_WIDGET (t)->rect;
    const int *colors;
    gboolean disabled;
    int normalc, selc, scrollbarc;
    int i;
    int sel_line = -1;

    colors = widget_get_colors (wt);

    disabled = widget_get_state (wt, WST_DISABLED);
    if (t->color_idx >= 0)
        normalc = disabled ? CORE_DISABLED_COLOR : colors[t->color_idx];
    else
        normalc = disabled ? CORE_DISABLED_COLOR : colors[DLG_COLOR_NORMAL];
    selc = disabled ? CORE_DISABLED_COLOR
                    : colors[focused ? DLG_COLOR_SELECTED_FOCUS : DLG_COLOR_SELECTED_NORMAL];
    scrollbarc = disabled ? CORE_DISABLED_COLOR : colors[DLG_COLOR_FRAME];

    for (i = 0; i < w->lines; i++)
    {
        int row_idx = t->top + i;
        int row_color;
        int col_x = 1;
        int c;

        if (row_idx == t->current && sel_line == -1)
        {
            sel_line = i;
            row_color = selc;
        }
        else
            row_color = normalc;

        tty_setcolor (row_color);

        /* clear the line first */
        widget_gotoyx (t, i, 0);
        tty_print_string (str_fit_to_term ("", w->cols, J_LEFT));

        for (c = 0; c < t->ncols; c++)
        {
            const char *cell_text = "";

            if (row_idx < t->nrows)
            {
                char **row = (char **) g_ptr_array_index (t->rows, (guint) row_idx);

                cell_text = row[c];
            }

            tty_setcolor (row_color);
            widget_gotoyx (t, i, col_x);

            if (t->col_defs[c].type == TABLE_COL_CHECK && t->checks != NULL && row_idx < t->nrows)
            {
                const gboolean *chk =
                    (const gboolean *) g_ptr_array_index (t->checks, (guint) row_idx);

                tty_print_string (chk[c] ? "[x]" : "[ ]");
            }
            else
                tty_print_string (
                    str_fit_to_term (cell_text, t->col_defs[c].width, t->col_defs[c].align));

            col_x += t->col_defs[c].width;

            /* draw column separator except after last column */
            if (c < t->ncols - 1)
            {
                tty_setcolor (row_color);
                widget_gotoyx (t, i, col_x);
                tty_print_one_vline (TRUE);
                col_x++;
            }
        }
    }

    t->cursor_y = sel_line;

    if (t->scrollbar && t->nrows > w->lines)
    {
        tty_setcolor (scrollbarc);
        table_drawscroll (t);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
table_fwd (WTable *t, gboolean wrap)
{
    if (t->nrows == 0)
        return;

    if (t->current + 1 < t->nrows)
        table_set_current (t, t->current + 1);
    else if (wrap)
        table_set_current (t, 0);
}

/* --------------------------------------------------------------------------------------------- */

static void
table_back (WTable *t, gboolean wrap)
{
    if (t->nrows == 0)
        return;

    if (t->current > 0)
        table_set_current (t, t->current - 1);
    else if (wrap)
        table_set_current (t, t->nrows - 1);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
table_execute_cmd (WTable *t, long command)
{
    cb_ret_t ret = MSG_HANDLED;
    const WRect *w = &CONST_WIDGET (t)->rect;

    if (t->nrows == 0)
        return MSG_NOT_HANDLED;

    switch (command)
    {
    case CK_Up:
        table_back (t, TRUE);
        break;
    case CK_Down:
        table_fwd (t, TRUE);
        break;
    case CK_Top:
        table_set_current (t, 0);
        break;
    case CK_Bottom:
        table_set_current (t, t->nrows - 1);
        break;
    case CK_PageUp:
        table_set_current (t, MAX (t->current - (w->lines - 1), 0));
        break;
    case CK_PageDown:
        table_set_current (t, MIN (t->current + (w->lines - 1), t->nrows - 1));
        break;
    case CK_Enter:
        ret = send_message (WIDGET (t)->owner, t, MSG_NOTIFY, command, NULL);
        break;
    default:
        ret = MSG_NOT_HANDLED;
    }

    return ret;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
table_key (WTable *t, int key)
{
    const WRect *w = &CONST_WIDGET (t)->rect;

    if (t->nrows == 0)
        return MSG_NOT_HANDLED;

    switch (key)
    {
    case KEY_UP:
        table_back (t, TRUE);
        return MSG_HANDLED;
    case KEY_DOWN:
        table_fwd (t, TRUE);
        return MSG_HANDLED;
    case KEY_HOME:
        table_set_current (t, 0);
        return MSG_HANDLED;
    case KEY_END:
        table_set_current (t, t->nrows - 1);
        return MSG_HANDLED;
    case KEY_PPAGE:
        table_set_current (t, MAX (t->current - (w->lines - 1), 0));
        return MSG_HANDLED;
    case KEY_NPAGE:
        table_set_current (t, MIN (t->current + (w->lines - 1), t->nrows - 1));
        return MSG_HANDLED;
    case ' ':
        if (t->has_check_cols && t->checks != NULL && t->current < t->nrows)
        {
            gboolean *chk = (gboolean *) g_ptr_array_index (t->checks, (guint) t->current);
            int c;

            for (c = 0; c < t->ncols; c++)
                if (t->col_defs[c].type == TABLE_COL_CHECK)
                {
                    chk[c] = !chk[c];
                    break;
                }
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;
    default:
        return MSG_NOT_HANDLED;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
table_on_change (WTable *t)
{
    table_draw (t, TRUE);
    send_message (WIDGET (t)->owner, t, MSG_NOTIFY, 0, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
table_destroy (WTable *t)
{
    g_free (t->col_defs);
    t->col_defs = NULL;

    if (t->rows != NULL)
    {
        g_ptr_array_free (t->rows, TRUE);
        t->rows = NULL;
    }

    if (t->checks != NULL)
    {
        g_ptr_array_free (t->checks, TRUE);
        t->checks = NULL;
    }

    t->nrows = 0;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
table_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    WTable *t = TABLE (w);

    switch (msg)
    {
    case MSG_KEY:
    {
        cb_ret_t ret_code;

        ret_code = table_key (t, parm);
        if (ret_code != MSG_NOT_HANDLED)
            table_on_change (t);
        return ret_code;
    }

    case MSG_ACTION:
        return table_execute_cmd (t, parm);

    case MSG_CURSOR:
        widget_gotoyx (t, t->cursor_y, 0);
        return MSG_HANDLED;

    case MSG_DRAW:
        table_draw (t, widget_get_state (w, WST_FOCUSED));
        return MSG_HANDLED;

    case MSG_DESTROY:
        table_destroy (t);
        return MSG_HANDLED;

    default:
        return widget_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
table_mouse_callback (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    WTable *t = TABLE (w);
    int old_current;

    old_current = t->current;

    switch (msg)
    {
    case MSG_MOUSE_DOWN:
        widget_select (w);
        table_set_current (t, MIN (t->top + event->y, t->nrows - 1));
        break;

    case MSG_MOUSE_SCROLL_UP:
        table_back (t, FALSE);
        break;

    case MSG_MOUSE_SCROLL_DOWN:
        table_fwd (t, FALSE);
        break;

    case MSG_MOUSE_CLICK:
        if (event->count == GPM_DOUBLE)
            table_execute_cmd (t, CK_Enter);
        break;

    case MSG_MOUSE_DRAG:
        event->result.repeat = TRUE;
        table_set_current (t, MIN (t->top + event->y, t->nrows - 1));
        break;

    default:
        break;
    }

    if (t->current != old_current)
        table_on_change (t);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

WTable *
table_new (int y, int x, int height, int width, int ncols, const table_column_def_t *col_defs)
{
    WRect r = { y, x, 1, width };
    WTable *t;
    Widget *w;

    t = g_new0 (WTable, 1);
    w = WIDGET (t);
    r.lines = height > 0 ? height : 1;
    widget_init (w, &r, table_callback, table_mouse_callback);
    w->options |= WOP_SELECTABLE;

    t->ncols = ncols;
    t->col_defs = g_new (table_column_def_t, ncols);
    memcpy (t->col_defs, col_defs, sizeof (table_column_def_t) * (size_t) ncols);

    t->rows = g_ptr_array_new_with_free_func (table_free_row);
    t->nrows = 0;
    t->top = 0;
    t->current = 0;
    t->cursor_y = 0;
    t->scrollbar = !mc_global.tty.slow_terminal;
    t->color_idx = -1;

    /* detect CHECK columns */
    t->has_check_cols = FALSE;
    {
        int c;

        for (c = 0; c < ncols; c++)
            if (col_defs[c].type == TABLE_COL_CHECK)
            {
                t->has_check_cols = TRUE;
                break;
            }
    }
    t->checks = t->has_check_cols ? g_ptr_array_new_with_free_func (table_free_check_row) : NULL;

    return t;
}

/* --------------------------------------------------------------------------------------------- */

void
table_add_row (WTable *t, ...)
{
    va_list ap;
    char **row;
    int i;

    row = g_new (char *, t->ncols + 1);

    va_start (ap, t);
    for (i = 0; i < t->ncols; i++)
    {
        const char *s = va_arg (ap, const char *);
        row[i] = g_strdup (s != NULL ? s : "");
    }
    va_end (ap);

    row[t->ncols] = NULL; /* null-terminate for g_strfreev */

    g_ptr_array_add (t->rows, row);

    if (t->checks != NULL)
        g_ptr_array_add (t->checks, g_new0 (gboolean, t->ncols));

    t->nrows = (int) t->rows->len;
}

/* --------------------------------------------------------------------------------------------- */

void
table_clear (WTable *t)
{
    if (t->rows != NULL)
        g_ptr_array_set_size (t->rows, 0);

    if (t->checks != NULL)
        g_ptr_array_set_size (t->checks, 0);

    t->nrows = 0;
    t->top = 0;
    t->current = 0;
}

/* --------------------------------------------------------------------------------------------- */

int
table_get_current (const WTable *t)
{
    return t->current;
}

/* --------------------------------------------------------------------------------------------- */

void
table_set_current (WTable *t, int pos)
{
    int lines;

    if (t->nrows == 0)
        return;

    if (pos < 0)
        pos = 0;
    if (pos >= t->nrows)
        pos = t->nrows - 1;

    t->current = pos;

    lines = WIDGET (t)->rect.lines;

    /* adjust top so current is visible */
    if (t->current < t->top)
        t->top = t->current;
    else if (t->current - t->top >= lines)
        t->top = t->current - lines + 1;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
table_get_checked (const WTable *t, int row, int col)
{
    const gboolean *chk;

    if (t->checks == NULL || row < 0 || row >= t->nrows || col < 0 || col >= t->ncols)
        return FALSE;

    chk = (const gboolean *) g_ptr_array_index (t->checks, (guint) row);
    return chk[col];
}

/* --------------------------------------------------------------------------------------------- */

void
table_set_checked (WTable *t, int row, int col, gboolean val)
{
    gboolean *chk;

    if (t->checks == NULL || row < 0 || row >= t->nrows || col < 0 || col >= t->ncols)
        return;

    chk = (gboolean *) g_ptr_array_index (t->checks, (guint) row);
    chk[col] = val;
}

/* --------------------------------------------------------------------------------------------- */
