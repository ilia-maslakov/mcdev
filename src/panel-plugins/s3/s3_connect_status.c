/*
   S3 panel plugin -- connection status dialog with progress.

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
#include "lib/tty/tty.h"
#include "lib/widget.h"

#include "s3_types.h"

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
s3_connect_status_init_cb (status_msg_t *sm)
{
    simple_status_msg_t *ssm = SIMPLE_STATUS_MSG (sm);
    s3_connect_status_msg_t *fsm = (s3_connect_status_msg_t *) sm;
    Widget *wd = WIDGET (sm->dlg);
    WGroup *wg = GROUP (sm->dlg);
    WRect r;
    const char *b_name = _ ("&Abort");
    int b_width, wd_width, y;

    b_width = str_term_width1 (b_name) + 4;
    wd_width = MAX (wd->rect.cols, b_width + 6);

    y = 2;
    ssm->label = label_new (y++, 3, NULL);
    group_add_widget_autopos (wg, ssm->label, WPOS_KEEP_TOP | WPOS_CENTER_HORZ, NULL);

    fsm->hline_w = WIDGET (hline_new (y++, -1, -1));
    group_add_widget (wg, fsm->hline_w);

    fsm->button_w = WIDGET (button_new (y++, 3, B_CANCEL, NORMAL_BUTTON, b_name, NULL));
    group_add_widget_autopos (wg, fsm->button_w, WPOS_KEEP_TOP | WPOS_CENTER_HORZ, NULL);

    r = wd->rect;
    r.lines = y + 2;
    r.cols = wd_width;
    widget_set_size_rect (wd, &r);
}

/* --------------------------------------------------------------------------------------------- */

void
s3_connect_status_deinit_cb (status_msg_t *sm)
{
    (void) sm;
}

/* --------------------------------------------------------------------------------------------- */

int
s3_connect_status_update_cb (status_msg_t *sm)
{
    simple_status_msg_t *ssm = SIMPLE_STATUS_MSG (sm);
    s3_connect_status_msg_t *fsm = (s3_connect_status_msg_t *) sm;
    Widget *wd = WIDGET (sm->dlg);
    Widget *lw = WIDGET (ssm->label);
    const char *text;
    int label_lines;
    WRect r;

    text = (fsm->log != NULL && fsm->log->len > 0) ? fsm->log->str : _ ("Please wait...");
    label_set_text (ssm->label, text);

    label_lines = lw->rect.lines;
    r = wd->rect;
    r.lines = MAX (r.lines, label_lines + 6);
    r.cols = MAX (r.cols, lw->rect.cols + 6);
    r.y = (LINES - r.lines) / 2;
    r.x = (COLS - r.cols) / 2;
    widget_set_size_rect (wd, &r);

    if (fsm->hline_w != NULL)
    {
        WRect hr = fsm->hline_w->rect;

        hr.y = r.y + 2 + label_lines;
        widget_set_size_rect (fsm->hline_w, &hr);
    }

    if (fsm->button_w != NULL)
    {
        WRect br = fsm->button_w->rect;

        br.y = r.y + 3 + label_lines;
        br.x = r.x + (r.cols - br.cols) / 2;
        widget_set_size_rect (fsm->button_w, &br);
    }

    return status_msg_common_update (sm);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
s3_connect_status_set_stage (s3_connect_status_msg_t *fsm, const char *fmt, ...)
{
    va_list ap;
    char *line;

    va_start (ap, fmt);
    line = g_strdup_vprintf (fmt, ap);
    va_end (ap);

    if (fsm->log->len > 0)
        g_string_append_c (fsm->log, '\n');
    g_string_append (fsm->log, line);
    g_free (line);

    return (STATUS_MSG (fsm)->update (STATUS_MSG (fsm)) != B_CANCEL);
}

/* --------------------------------------------------------------------------------------------- */

void
s3_connect_status_wait_close (s3_connect_status_msg_t *fsm)
{
    status_msg_t *sm = STATUS_MSG (fsm);

    if (fsm == NULL || sm->dlg == NULL)
        return;

    if (widget_get_state (WIDGET (sm->dlg), WST_CONSTRUCT))
        dlg_init (sm->dlg);

    if (fsm->button_w != NULL)
    {
        button_set_text (BUTTON (fsm->button_w), _ ("&Close"));
        widget_select (fsm->button_w);
    }

    sm->dlg->ret_value = B_CANCEL;
    (void) dlg_run (sm->dlg);
}

/* --------------------------------------------------------------------------------------------- */

#if LIBCURL_VERSION_NUM >= 0x072000
int
s3_connect_progress_cb (void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                        curl_off_t ulnow)
#else
int
s3_connect_progress_cb (void *clientp, double dltotal, double dlnow, double ultotal, double ulnow)
#endif
{
    s3_connect_progress_t *progress = (s3_connect_progress_t *) clientp;

    (void) dltotal;
    (void) dlnow;
    (void) ultotal;
    (void) ulnow;

    if (progress == NULL || progress->sm == NULL || progress->sm->update == NULL)
        return 0;

    return (progress->sm->update (progress->sm) == B_CANCEL) ? 1 : 0;
}

/* --------------------------------------------------------------------------------------------- */
