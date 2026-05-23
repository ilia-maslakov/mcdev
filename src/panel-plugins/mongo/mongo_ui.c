/*
   MongoDB panel plugin -- message wrapping and modeless status dialog.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/tty/tty.h"  // COLS
#include "lib/widget.h"

#include "mongo_ui.h"

/*** file scope functions ************************************************************************/

/* Greedy word-wrap @text to @width columns so long one-line messages do not
   stretch the dialog across the whole screen. Existing newlines are kept;
   runs of blanks collapse to one; words longer than @width are hard-broken.
   Returns a newly-allocated string (caller g_free). */
static char *
mongo_wrap_text (const char *text, gsize width)
{
    GString *out;
    const char *p;
    gsize col = 0;

    if (text == NULL)
        return NULL;
    if (width < 16)
        width = 16;

    out = g_string_new (NULL);
    for (p = text; *p != '\0';)
    {
        const char *w;
        gsize wl;

        if (*p == '\n')
        {
            g_string_append_c (out, '\n');
            col = 0;
            p++;
            continue;
        }
        if (*p == ' ' || *p == '\t' || *p == '\r')
        {
            p++;
            continue;
        }

        w = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
            p++;
        wl = (gsize) (p - w);

        if (col > 0)
        {
            if (col + 1 + wl > width)
            {
                g_string_append_c (out, '\n');
                col = 0;
            }
            else
            {
                g_string_append_c (out, ' ');
                col++;
            }
        }

        if (wl > width)
        {
            const char *q;
            for (q = w; q < p; q++)
            {
                if (col == width)
                {
                    g_string_append_c (out, '\n');
                    col = 0;
                }
                g_string_append_c (out, *q);
                col++;
            }
        }
        else
        {
            g_string_append_len (out, w, (gssize) wl);
            col += wl;
        }
    }

    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* dlg_init spin-loops without a focusable widget; mark the label SELECTABLE. */
static void
mongo_status_init_cb (status_msg_t *sm)
{
    simple_status_msg_t *ssm = SIMPLE_STATUS_MSG (sm);
    Widget *wd = WIDGET (sm->dlg);
    WGroup *wg = GROUP (sm->dlg);
    WRect r;

    ssm->label = label_new (2, 3, NULL);
    widget_set_options (WIDGET (ssm->label), WOP_SELECTABLE, TRUE);
    group_add_widget_autopos (wg, ssm->label, WPOS_KEEP_TOP | WPOS_CENTER_HORZ, NULL);

    r = wd->rect;
    r.lines = 5;
    r.cols = MAX (wd->rect.cols, 56);
    widget_set_size_rect (wd, &r);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
mongo_show_message (mc_panel_host_t *host, gboolean error, const char *text)
{
    gsize width;
    char *wrapped;

    if (host == NULL || host->message == NULL || text == NULL)
        return;

    /* Wrap to keep the dialog narrow; clamp to the terminal and a sane max. */
    width = (COLS > 24) ? (gsize) (COLS - 12) : 24;
    if (width > 64)
        width = 64;

    wrapped = mongo_wrap_text (text, width);
    host->message (host, error ? 1 : 0, _ ("MongoDB"), wrapped != NULL ? wrapped : text);
    g_free (wrapped);
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_status_open (mongo_status_t *st, const char *title)
{
    if (st == NULL)
        return;
    memset (st, 0, sizeof (*st));
    status_msg_init (STATUS_MSG (st), title, 0.0, mongo_status_init_cb, status_msg_common_update,
                     NULL);
    st->active = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_status_set (mongo_status_t *st, const char *text)
{
    simple_status_msg_t *ssm;
    Widget *wd;
    Widget *lw;

    if (st == NULL || !st->active)
        return;
    ssm = SIMPLE_STATUS_MSG (st);
    if (ssm->label == NULL)
        return;

    label_set_text (ssm->label, text);

    /* Recenter manually: WPOS_CENTER_HORZ saw a zero-width label at init. */
    wd = WIDGET (STATUS_MSG (st)->dlg);
    lw = WIDGET (ssm->label);
    if (lw->rect.cols < wd->rect.cols - 4)
    {
        WRect lr = lw->rect;
        lr.x = wd->rect.x + (wd->rect.cols - lr.cols) / 2;
        widget_set_size_rect (lw, &lr);
    }
    else
    {
        WRect dr = wd->rect;
        dr.cols = lw->rect.cols + 6;
        dr.x = (COLS - dr.cols) / 2;
        widget_set_size_rect (wd, &dr);
        {
            WRect lr = lw->rect;
            lr.x = dr.x + (dr.cols - lr.cols) / 2;
            widget_set_size_rect (lw, &lr);
        }
    }

    /* Old label position bleeds through without a full redraw. */
    widget_draw (wd);
    (void) status_msg_common_update (STATUS_MSG (st));
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_status_close (mongo_status_t *st)
{
    if (st == NULL || !st->active)
        return;
    status_msg_deinit (STATUS_MSG (st));
    st->active = FALSE;
}

/* --------------------------------------------------------------------------------------------- */
