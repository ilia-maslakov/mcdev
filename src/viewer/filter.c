/*
   Internal file viewer for the Midnight Commander
   Line filter: grep-style filtered view over the same datasource.

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

#include <config.h>
#include <string.h>

#include "lib/global.h"
#include "lib/search.h"
#include "lib/widget.h"

#include "src/history.h"

#include "internal.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/**
 * Show "Filter:" input dialog.  On OK stores pattern into view and activates filter.
 * On empty input, deactivates any current filter.  Cancel leaves filter unchanged.
 *
 * @return TRUE if filter was changed (activated or deactivated).
 */
gboolean
mcview_filter_dialog (WView *view)
{
    char *exp = NULL;
    int qd_result;

    {
        quick_widget_t quick_widgets[] = {
            // clang-format off
            QUICK_LABELED_INPUT (_ ("Filter pattern (empty = clear):"), input_label_above,
                                 view->filter_pattern != NULL ? view->filter_pattern : "",
                                 "mc.view.filter", &exp, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
            // clang-format on
        };

        WRect r = { -1, -1, 0, 50 };

        quick_dialog_t qdlg = {
            .rect = r,
            .title = _ ("Filter"),
            .help = "[Input Line Keys]",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        qd_result = quick_dialog (&qdlg);
    }

    if (qd_result == B_CANCEL)
    {
        g_free (exp);
        return FALSE;
    }

    if (exp == NULL || exp[0] == '\0')
    {
        g_free (exp);
        if (view->filter_active)
        {
            mcview_filter_deactivate (view);
            return TRUE;
        }
        return FALSE;
    }

    mcview_filter_activate (view, exp);
    g_free (exp);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Activate filter mode with the given plain-text pattern.
 * O(1): does not scan the datasource -- mcview_filter_update() does that
 * incrementally over successive redraw ticks.
 */
void
mcview_filter_activate (WView *view, const char *pattern)
{
    /* Deactivate any current filter first to free old state. */
    if (view->filter_active)
        mcview_filter_deactivate (view);

    view->filter_engine = mc_search_new (pattern, NULL);
    if (view->filter_engine == NULL)
        return;

    view->filter_engine->search_type = MC_SEARCH_T_NORMAL;
    view->filter_engine->is_case_sensitive = TRUE;
    view->filter_engine->whole_words = FALSE;

    g_free (view->filter_pattern);
    view->filter_pattern = g_strdup (pattern);

    if (view->filter_offsets == NULL)
        view->filter_offsets = g_array_new (FALSE, FALSE, sizeof (off_t));
    else
        g_array_set_size (view->filter_offsets, 0);

    view->filter_scanned_up_to = 0;
    view->filter_partial_scan_offset = 0;
    view->filter_skipping_long_line = FALSE;
    view->filter_active = TRUE;

    /* Disable wrap mode while filter is active (simplifies rendering). */
    view->filter_prev_wrap = view->mode_flags.wrap;
    if (view->mode_flags.wrap)
    {
        view->mode_flags.wrap = FALSE;
        view->dpy_wrap_dirty = TRUE;
    }

    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Deactivate filter mode, restore previous wrap state, keep dpy_start
 * on the nearest real line to the current filtered position.
 */
void
mcview_filter_deactivate (WView *view)
{
    if (!view->filter_active)
        return;

    view->filter_active = FALSE;
    view->filter_follow = FALSE;

    if (view->filter_engine != NULL)
    {
        mc_search_free (view->filter_engine);
        view->filter_engine = NULL;
    }

    g_free (view->filter_pattern);
    view->filter_pattern = NULL;

    if (view->filter_offsets != NULL)
    {
        g_array_free (view->filter_offsets, TRUE);
        view->filter_offsets = NULL;
    }

    view->filter_scanned_up_to = 0;
    view->filter_partial_scan_offset = 0;
    view->filter_skipping_long_line = FALSE;

    /* Restore wrap mode. */
    if (view->filter_prev_wrap && !view->mode_flags.wrap)
    {
        view->mode_flags.wrap = TRUE;
        view->dpy_wrap_dirty = TRUE;
    }

    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Scan a budget-limited chunk of the datasource and append matching line
 * offsets to filter_offsets.  Called from the redraw tick so the UI stays
 * responsive even during log bursts.
 *
 * Also handles follow mode: if the last known match was visible before the
 * update (last_off < dpy_end), scroll to the new last match after appending.
 */
void
mcview_filter_update (WView *view)
{
    off_t filesize;
    off_t scan_end;
    off_t bol, pos;
    int lines_done = 0;
    off_t bytes_done = 0;
    gboolean budget_hit = FALSE;
    off_t last_before;

    if (!view->filter_active || view->filter_engine == NULL)
        return;

    filesize = mcview_get_filesize (view);
    if (filesize <= view->filter_partial_scan_offset)
        return;

    /* Remember last match offset before update for follow-mode check. */
    last_before = (view->filter_offsets != NULL && view->filter_offsets->len > 0)
        ? g_array_index (view->filter_offsets, off_t, view->filter_offsets->len - 1)
        : -1;

    scan_end = filesize;
    /* bol tracks the BOL of the line being scanned; filter_scanned_up_to
       only advances when a complete line is processed.
       pos is the byte-level scan cursor and may be mid-line. */
    bol = view->filter_scanned_up_to;
    pos = view->filter_partial_scan_offset;

    while (pos < scan_end)
    {
        int c;

        /* Per-byte budget: each byte costs one unit so even a single huge
           line cannot exceed MCVIEW_FILTER_BUDGET_BYTES bytes per tick. */
        if (bytes_done >= MCVIEW_FILTER_BUDGET_BYTES || lines_done >= MCVIEW_FILTER_BUDGET_LINES)
        {
            budget_hit = TRUE;
            break;
        }

        if (!mcview_get_byte (view, pos, &c))
            break; /* No data yet (growing buffer). */

        bytes_done++;
        pos++;

        if (c == '\n')
        {
            if (!view->filter_skipping_long_line)
            {
                /* Build NUL-terminated line buffer [bol, pos) and match. */
                off_t len = pos - bol;
                gchar *buf;
                off_t i;
                int bc;

                buf = g_new (gchar, len + 1);
                for (i = 0; i < len; i++)
                {
                    if (!mcview_get_byte (view, bol + i, &bc))
                        break;
                    buf[i] = (gchar) bc;
                }
                buf[i] = '\0';

                if (mc_search_run (view->filter_engine, buf, 0, i, NULL))
                    g_array_append_val (view->filter_offsets, bol);
                g_free (buf);
            }

            lines_done++;
            bol = pos;
            view->filter_skipping_long_line = FALSE;
        }
        else if (!view->filter_skipping_long_line && (pos - bol) > MCVIEW_FILTER_BUDGET_BYTES)
        {
            /* Line exceeds hard cap: continue scanning to find \n but
               skip matching -- avoids a huge allocation at line end. */
            view->filter_skipping_long_line = TRUE;
        }
    }

    /* Last line of a static file with no trailing newline: match it now
       that we know no more bytes are coming. */
    if (!budget_hit && pos > bol && !mcview_may_still_grow (view)
        && !view->filter_skipping_long_line)
    {
        off_t len = pos - bol;
        gchar *buf;
        off_t i;
        int c;

        buf = g_new (gchar, len + 1);
        for (i = 0; i < len; i++)
        {
            if (!mcview_get_byte (view, bol + i, &c))
                break;
            buf[i] = (gchar) c;
        }
        buf[i] = '\0';

        if (mc_search_run (view->filter_engine, buf, 0, i, NULL))
            g_array_append_val (view->filter_offsets, bol);
        g_free (buf);

        bol = pos; /* Mark as processed. */
    }

    view->filter_scanned_up_to = bol;
    view->filter_partial_scan_offset = pos;

    /* If we hit the budget and there is more data, request another tick. */
    if (budget_hit && pos < filesize)
        view->dirty++;

    /* Transition out of empty-state: position on first match when it appears. */
    if (view->filter_offsets != NULL && view->filter_offsets->len > 0)
    {
        off_t first = g_array_index (view->filter_offsets, off_t, 0);

        if (last_before == -1)
        {
            /* First match ever -- jump there. */
            view->dpy_start = first;
            view->dpy_paragraph_skip_lines = 0;
            view->dpy_wrap_dirty = TRUE;
        }
        else if (view->filter_follow)
        {
            off_t last_now =
                g_array_index (view->filter_offsets, off_t, view->filter_offsets->len - 1);

            /* Follow: scroll only if the last match was visible before the update. */
            if (last_before < view->dpy_end)
            {
                view->dpy_start = last_now;
                view->dpy_paragraph_skip_lines = 0;
                view->dpy_wrap_dirty = TRUE;
            }
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Binary search: return the index in filter_offsets of the entry whose
 * value is closest to (and <= ) @offset.
 * Returns 0 when filter_offsets is empty or offset is before the first entry.
 */
guint
mcview_filter_idx (WView *view, off_t offset)
{
    guint lo, hi, mid;

    if (view->filter_offsets == NULL || view->filter_offsets->len == 0)
        return 0;

    lo = 0;
    hi = view->filter_offsets->len - 1;

    if (offset <= g_array_index (view->filter_offsets, off_t, 0))
        return 0;
    if (offset >= g_array_index (view->filter_offsets, off_t, hi))
        return hi;

    while (lo + 1 < hi)
    {
        mid = lo + (hi - lo) / 2;
        if (g_array_index (view->filter_offsets, off_t, mid) <= offset)
            lo = mid;
        else
            hi = mid;
    }

    return lo;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Return the file offset stored at filter_offsets[idx], or (off_t)-1 if
 * the index is out of range.
 */
off_t
mcview_filter_offset (WView *view, guint idx)
{
    if (view->filter_offsets == NULL || idx >= view->filter_offsets->len)
        return (off_t) -1;
    return g_array_index (view->filter_offsets, off_t, idx);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_filter_follow_toggle (WView *view)
{
    if (!view->filter_active)
        return;

    view->filter_follow = !view->filter_follow;
    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_filter_nav_next (WView *view)
{
    guint idx;
    off_t target;

    if (!view->filter_active || view->filter_offsets == NULL || view->filter_offsets->len == 0)
        return;

    idx = mcview_filter_idx (view, view->dpy_start);

    if (g_array_index (view->filter_offsets, off_t, idx) == view->dpy_start)
        idx++;

    target = mcview_filter_offset (view, idx);
    if (target != (off_t) -1)
    {
        view->dpy_start = target;
        view->dpy_paragraph_skip_lines = 0;
        view->dpy_wrap_dirty = TRUE;
        view->dirty++;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_filter_nav_prev (WView *view)
{
    guint idx;

    if (!view->filter_active || view->filter_offsets == NULL || view->filter_offsets->len == 0)
        return;

    idx = mcview_filter_idx (view, view->dpy_start);

    if (idx > 0)
        idx--;

    view->dpy_start = g_array_index (view->filter_offsets, off_t, idx);
    view->dpy_paragraph_skip_lines = 0;
    view->dpy_wrap_dirty = TRUE;
    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */
