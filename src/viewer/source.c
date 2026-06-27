/*
   Internal file viewer -- plugin source-controller plumbing.

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

/** \file source.c
 *  \brief Source: plugin source-controller two-phase swap helpers.
 */

#include <config.h>

#include <fcntl.h>  // O_RDONLY, O_NONBLOCK
#include <sys/stat.h>

#include "lib/global.h"
#include "lib/util.h"  // mc_pipe_t, mc_popen, mc_pclose
#include "lib/vfs/vfs.h"
#include "lib/widget.h"

#include "internal.h"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    enum
    {
        SRC_PIPE,
        SRC_FILE
    } kind;
    mc_pipe_t *pipe;  // SRC_PIPE
    int fd;           // SRC_FILE
    struct stat st;   // SRC_FILE
} mcview_source_handle_t;

/*** file scope functions ************************************************************************/

static mcview_source_handle_t *
mcview_try_open_source (const mcview_source_spec_t *spec, char **err_out)
{
    mcview_source_handle_t *h;

    if (err_out != NULL)
        *err_out = NULL;

    if (spec == NULL || (spec->command == NULL && spec->file == NULL))
    {
        if (err_out != NULL)
            *err_out = g_strdup (_ ("Source spec must set either command or file."));
        return NULL;
    }

    h = g_new0 (mcview_source_handle_t, 1);

    if (spec->command != NULL)
    {
        GError *gerr = NULL;
        mc_pipe_t *p;

        p = mc_popen (spec->command, TRUE, FALSE, &gerr);
        if (p == NULL)
        {
            if (err_out != NULL)
                *err_out =
                    g_strdup (gerr != NULL ? gerr->message : _ ("Cannot open source command."));
            if (gerr != NULL)
                g_error_free (gerr);
            g_free (h);
            return NULL;
        }
        h->kind = SRC_PIPE;
        h->pipe = p;
        return h;
    }

    {
        vfs_path_t *vpath = vfs_path_from_str (spec->file);
        int fd = mc_open (vpath, O_RDONLY | O_NONBLOCK);

        if (fd == -1)
        {
            vfs_path_free (vpath, TRUE);
            if (err_out != NULL)
                *err_out = g_strdup_printf (_ ("Cannot open %s"), spec->file);
            g_free (h);
            return NULL;
        }
        if (mc_fstat (fd, &h->st) == -1)
        {
            mc_close (fd);
            vfs_path_free (vpath, TRUE);
            if (err_out != NULL)
                *err_out = g_strdup_printf (_ ("Cannot stat %s"), spec->file);
            g_free (h);
            return NULL;
        }
        vfs_path_free (vpath, TRUE);
        h->kind = SRC_FILE;
        h->fd = fd;
        return h;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mcview_install_source (WView *view, mcview_source_handle_t *handle,
                       const mcview_source_spec_t *spec)
{
    if (handle->kind == SRC_PIPE)
    {
        mcview_set_datasource_stdio_pipe (view, handle->pipe);
        mcview_stream_start (view);

        g_free (view->command);
        view->command = g_strdup (spec->command);
        vfs_path_free (view->filename_vpath, TRUE);
        view->filename_vpath = NULL;
        vfs_path_free (view->workdir_vpath, TRUE);
        view->workdir_vpath = NULL;
    }
    else
    {
        mcview_set_datasource_file (view, handle->fd, &handle->st);

        g_free (view->command);
        view->command = NULL;
        vfs_path_free (view->filename_vpath, TRUE);
        view->filename_vpath = vfs_path_from_str (spec->file);
        vfs_path_free (view->workdir_vpath, TRUE);
        view->workdir_vpath = NULL;
    }

    view->dpy_start = 0;
    view->dpy_paragraph_skip_lines = 0;
    mcview_state_machine_init (&view->dpy_state_top, 0);
    view->dpy_wrap_dirty = FALSE;
    view->force_max = -1;
    view->dpy_text_column = 0;

    /* Refresh the converter for the installed buffer. */
    mcview_set_codeset (view);

    mcview_compute_areas (view);
    mcview_update_bytes_per_line (view);

    g_free (handle); /* handle struct itself is transient; datasource owns the fd/pipe */
}

/* --------------------------------------------------------------------------------------------- */

static int
mcview_capture_percent (WView *view)
{
    return mcview_calc_percent (view, view->dpy_start);
}

/* --------------------------------------------------------------------------------------------- */

static void
mcview_restore_percent (WView *view, int percent)
{
    off_t total;
    off_t target;

    if (percent < 0)
        return;
    total = mcview_get_filesize (view);
    if (total <= 0)
        return;
    if (percent >= 100)
        target = total > 0 ? total - 1 : 0;
    else
        target = (off_t) ((double) percent * (double) total / 100.0);
    view->dpy_start = mcview_bol (view, target, 0);
    view->dpy_wrap_dirty = TRUE;
}

/*** public functions ****************************************************************************/

mcview_source_spec_t *
mcview_source_spec_clone (const mcview_source_spec_t *src)
{
    mcview_source_spec_t *dst;

    if (src == NULL)
        return NULL;
    dst = g_new0 (mcview_source_spec_t, 1);
    dst->command = g_strdup (src->command);
    dst->file = g_strdup (src->file);
    dst->title = g_strdup (src->title);
    dst->auto_scroll_bottom = src->auto_scroll_bottom;
    return dst;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_source_spec_free (mcview_source_spec_t *s)
{
    if (s == NULL)
        return;
    g_free (s->command);
    g_free (s->file);
    g_free (s->title);
    g_free (s);
}

/* --------------------------------------------------------------------------------------------- */

/* Drop datasource-owned state before installing another source.
 * Keep source_spec/controller/ctx and filter snapshot state. */
void
mcview_reset_for_source_swap (WView *view)
{
    mcview_close_datasource (view);

    vfs_path_free (view->filename_vpath, TRUE);
    view->filename_vpath = NULL;
    vfs_path_free (view->workdir_vpath, TRUE);
    view->workdir_vpath = NULL;
    g_free (view->command);
    view->command = NULL;

    view->hexedit_lownibble = FALSE;
    view->hexview_in_text = FALSE;
    view->hex_cursor = 0;
    mcview_hexedit_free_change_list (view);

    if (view->search != NULL)
    {
        mc_search_free (view->search);
        view->search = NULL;
    }
    g_free (view->last_search_string);
    view->last_search_string = NULL;
    view->search_start = 0;
    view->search_end = 0;

    if (view->vterm != NULL)
    {
        mcview_vterm_free (view->vterm);
        view->vterm = NULL;
    }

    /* Offset-derived caches are rebuilt lazily for the new buffer. */
    if (view->coord_cache != NULL)
    {
        g_ptr_array_free (view->coord_cache, TRUE);
        view->coord_cache = NULL;
    }

    view->mode_flags.hex = FALSE;
    view->mode_flags.terminal = FALSE;
    view->hexedit_mode = FALSE;

    memset (view->marks, 0, sizeof (view->marks));
    view->marker = 0;
}

/* --------------------------------------------------------------------------------------------- */

/* Generic options/swap flow. The current source stays visible until the
   replacement command/file has been prepared and opened successfully. */
void
mcview_source_options (WView *view)
{
    mcview_source_spec_t *draft;
    const mcview_source_controller_t *c;
    void *ctx;
    char *err = NULL;
    mcview_source_handle_t *handle;
    mcview_filter_snapshot_t snap;
    int percent;

    if (view == NULL || view->source_controller == NULL || view->source_spec == NULL)
        return;
    c = view->source_controller;
    ctx = view->source_ctx;
    if (c->open_options == NULL || c->prepare == NULL || c->commit == NULL || c->rollback == NULL)
        return;

    draft = mcview_source_spec_clone (view->source_spec);
    if (!c->open_options (ctx, draft))
    {
        c->rollback (ctx);
        mcview_source_spec_free (draft);
        return;
    }

    if (!c->prepare (ctx, draft, &err))
    {
        message (D_ERROR, MSG_ERROR, "%s", err != NULL ? err : _ ("Source prepare failed."));
        g_free (err);
        c->rollback (ctx);
        mcview_source_spec_free (draft);
        return;
    }

    handle = mcview_try_open_source (draft, &err);
    if (handle == NULL)
    {
        message (D_ERROR, MSG_ERROR, "%s", err != NULL ? err : _ ("Cannot open new source."));
        g_free (err);
        c->rollback (ctx);
        mcview_source_spec_free (draft);
        return;
    }

    mcview_filter_take_snapshot (view, &snap);
    percent = mcview_capture_percent (view);

    mcview_reset_for_source_swap (view);
    mcview_install_source (view, handle, draft);

    c->commit (ctx);
    mcview_source_spec_free (view->source_spec);
    view->source_spec = draft;

    (void) mcview_filter_restore (view, &snap);
    mcview_filter_snapshot_clear (&snap);

    if (draft->auto_scroll_bottom)
        mcview_moveto_bottom (view);
    else
        mcview_restore_percent (view, percent);

    view->dirty++;

    /* Options dialogs can leave stale frame cells after a source swap. */
    repaint_screen ();
}

/* --------------------------------------------------------------------------------------------- */
