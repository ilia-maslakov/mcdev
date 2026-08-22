/*
   Host services exposed to application-wide runtime extensions.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

#include "lib/file-entry.h"
#include "lib/global.h"

#include "lib/extension-runtime.h"
#include "lib/runtime-events.h"
#include "lib/strutil.h"
#include "lib/tty/key.h"
#include "lib/tty/tty.h"
#include "lib/util.h"
#include "lib/vfs/vfs.h"
#include "lib/widget.h"

#include "editor/edit-impl.h"
#include "editor/editwidget.h"
#include "events_init.h"
#include "filemanager/filemanager.h"
#include "filemanager/layout.h"
#include "viewer/internal.h"

#include "runtime-host.h"
#include "runtime-panel-provider.h"
#include "runtime-viewer-source.h"

/* --------------------------------------------------------------------------------------------- */

static WEdit *runtime_host_current_editor = NULL;
static WView *runtime_host_current_viewer = NULL;

typedef struct
{
    char *runtime_name;
    char *package_id;
    mc_runtime_error_phase_t phase;
    char *summary;
    char *details;
    guint count;
    gboolean status_reported;
} runtime_host_error_t;

static GHashTable *runtime_host_errors = NULL;
static gboolean runtime_host_dialog_active = FALSE;
static struct runtime_host_dialog_builder *runtime_host_active_dialog_builder = NULL;

#define RUNTIME_HOST_EDITOR_SELECTION_TEXT_LIMIT  (1024U * 1024U)
#define RUNTIME_HOST_EDITOR_TEXT_LIMIT            (64U * 1024U * 1024U)
#define RUNTIME_HOST_EDITOR_EDIT_CHANGES_MAX      1024U
#define RUNTIME_HOST_EDITOR_EDIT_TEXT_LIMIT       (64U * 1024U * 1024U)
#define RUNTIME_HOST_PROCESS_OUTPUT_LIMIT_DEFAULT (8U * 1024U * 1024U)
#define RUNTIME_HOST_PROCESS_OUTPUT_LIMIT_MAX     (64U * 1024U * 1024U)

typedef struct
{
    const mc_runtime_dialog_control_t *control;
    unsigned long widget_id;
    gboolean checked;
    char *text;
    int selected;
    const char **items;
} runtime_host_dialog_field_t;

typedef struct
{
    int action;
    const char *id;
} runtime_host_dialog_button_t;

typedef struct runtime_host_dialog_builder
{
    GArray *widgets;
    GPtrArray *fields;
    GArray *buttons;
    int next_action;
    gboolean want_tab;
} runtime_host_dialog_builder_t;

/* --------------------------------------------------------------------------------------------- */

static const char *
runtime_host_error_phase_name (mc_runtime_error_phase_t phase)
{
    switch (phase)
    {
    case MC_RUNTIME_ERROR_PHASE_STARTUP:
        return "startup";
    case MC_RUNTIME_ERROR_PHASE_EVENT:
        return "event";
    case MC_RUNTIME_ERROR_PHASE_MACRO:
        return "macro";
    default:
        return "unknown";
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_error_destroy (runtime_host_error_t *error)
{
    if (error == NULL)
        return;

    g_free (error->runtime_name);
    g_free (error->package_id);
    g_free (error->summary);
    g_free (error->details);
    g_free (error);
}

/* --------------------------------------------------------------------------------------------- */

static char *
runtime_host_error_key (const char *runtime_name, const char *package_id,
                        mc_runtime_error_phase_t phase)
{
    return g_strdup_printf ("%s%c%s%c%d", runtime_name != NULL ? runtime_name : "runtime", '\n',
                            package_id != NULL ? package_id : "?", '\n', (int) phase);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_set_error (const char **error, const char *message)
{
    if (error != NULL)
        *error = message;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_process_append (GString *target, const char *data, gsize length, gsize limit,
                             gboolean *truncated)
{
    gsize available = target->len < limit ? limit - target->len : 0;
    gsize copied = MIN (available, length);

    if (copied > 0)
        g_string_append_len (target, data, copied);
    if (copied < length)
        *truncated = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_process_result_free (mc_runtime_process_result_t *result)
{
    if (result == NULL)
        return;
    g_free (result->out.data);
    g_free (result->err.data);
    memset (result, 0, sizeof (*result));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_process_run_shell (const char *command, gsize max_output,
                                mc_runtime_process_result_t *result, const char **error)
{
    mc_pipe_t *pipe;
    GString *out;
    GString *err;
    GError *mcerror = NULL;
    gboolean out_done = FALSE;
    gboolean err_done = FALSE;
    gboolean ok = TRUE;
    int status;

    if (error != NULL)
        *error = NULL;
    if (command == NULL || command[0] == '\0' || result == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    memset (result, 0, sizeof (*result));
    result->exit_code = -1;
    if (max_output == 0)
        max_output = RUNTIME_HOST_PROCESS_OUTPUT_LIMIT_DEFAULT;
    max_output = MIN (max_output, RUNTIME_HOST_PROCESS_OUTPUT_LIMIT_MAX);

    pipe = mc_popen (command, TRUE, TRUE, &mcerror);
    if (pipe == NULL)
    {
        g_clear_error (&mcerror);
        return runtime_host_set_error (error, "process_start_failed");
    }

    out = g_string_sized_new (4096);
    err = g_string_sized_new (1024);
    while (!out_done || !err_done)
    {
        pipe->out.len = MC_PIPE_BUFSIZE;
        pipe->err.len = MC_PIPE_BUFSIZE;
        mc_pread (pipe, &mcerror);
        if (mcerror != NULL)
        {
            ok = FALSE;
            break;
        }

        if (!out_done && pipe->out.len > 0)
            runtime_host_process_append (out, pipe->out.buf, (gsize) pipe->out.len, max_output,
                                         &result->out_truncated);
        else if (!out_done
                 && (pipe->out.len == MC_PIPE_STREAM_EOF || pipe->out.len == MC_PIPE_ERROR_READ))
        {
            out_done = TRUE;
            close (pipe->out.fd);
            pipe->out.fd = -1;
            if (pipe->out.len == MC_PIPE_ERROR_READ)
                ok = FALSE;
        }

        if (!err_done && pipe->err.len > 0)
            runtime_host_process_append (err, pipe->err.buf, (gsize) pipe->err.len, max_output,
                                         &result->err_truncated);
        else if (!err_done
                 && (pipe->err.len == MC_PIPE_STREAM_EOF || pipe->err.len == MC_PIPE_ERROR_READ))
        {
            err_done = TRUE;
            close (pipe->err.fd);
            pipe->err.fd = -1;
            if (pipe->err.len == MC_PIPE_ERROR_READ)
                ok = FALSE;
        }
    }

    g_clear_error (&mcerror);
    status = mc_pclose_status (pipe, &mcerror);
    if (status < 0 || mcerror != NULL)
        ok = FALSE;
    else if (WIFEXITED (status))
        result->exit_code = WEXITSTATUS (status);
    else if (WIFSIGNALED (status))
        result->term_signal = WTERMSIG (status);

    result->out.length = out->len;
    result->out.data = g_string_free (out, FALSE);
    result->err.length = err->len;
    result->err.data = g_string_free (err, FALSE);
    g_clear_error (&mcerror);

    if (!ok)
    {
        runtime_host_process_result_free (result);
        return runtime_host_set_error (error, "process_io_failed");
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_objects_are_ready (const char **error)
{
    if (events_runtime_is_started ())
        return TRUE;

    return runtime_host_set_error (error, "not_ready");
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_string_set (mc_runtime_string_t *string, char *value)
{
    if (string == NULL)
    {
        g_free (value);
        return;
    }

    string->data = value != NULL ? value : g_strdup ("");
    string->length = strlen (string->data);
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_string_free (mc_runtime_string_t *string)
{
    if (string == NULL)
        return;

    g_free (string->data);
    string->data = NULL;
    string->length = 0;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_file_snapshot_t *
runtime_host_panel_file_snapshot_new (const WPanel *panel, const file_entry_t *entry)
{
    mc_runtime_file_snapshot_t *snapshot;
    vfs_path_t *path;

    if (panel == NULL || entry == NULL || entry->fname == NULL || entry->fname->str == NULL)
        return NULL;

    snapshot = mc_runtime_file_snapshot_new ();
    snapshot->name = g_strdup (entry->fname->str);
    if (panel->cwd_vpath != NULL)
    {
        path = vfs_path_append_new (panel->cwd_vpath, entry->fname->str, (char *) NULL);
        snapshot->path = vfs_path_to_str_flags (path, 0, VPF_STRIP_PASSWORD);
        vfs_path_free (path, TRUE);
    }
    else
        snapshot->path = g_strdup (entry->fname->str);
    snapshot->is_dir = S_ISDIR (entry->st.st_mode) || entry->f.link_to_dir != 0;
    snapshot->size = entry->st.st_size > 0 ? (guint64) entry->st.st_size : 0;
    snapshot->mtime = (gint64) entry->st.st_mtime;
    snapshot->marked = entry->f.marked != 0;

    return snapshot;
}

/* --------------------------------------------------------------------------------------------- */

static WPanel *
runtime_host_panel_resolve (const mc_runtime_handle_t *handle, const char **error)
{
    WPanel *panel;

    if (!runtime_host_objects_are_ready (error))
        return NULL;

    panel = (WPanel *) mc_runtime_handle_resolve (handle, MC_RUNTIME_HANDLE_PANEL);
    if (panel == NULL)
        runtime_host_set_error (error, "closed");

    return panel;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
runtime_host_panel_active (void)
{
    mc_runtime_handle_t invalid = { MC_RUNTIME_HANDLE_INVALID, 0, 0 };

    if (!events_runtime_is_started () || mc_global.mc_run_mode != MC_RUN_FULL
        || current_panel == NULL)
        return invalid;

    return mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_PANEL, current_panel);
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
runtime_host_panel_passive (void)
{
    WPanel *panel;
    mc_runtime_handle_t invalid = { MC_RUNTIME_HANDLE_INVALID, 0, 0 };

    if (!events_runtime_is_started () || mc_global.mc_run_mode != MC_RUN_FULL
        || current_panel == NULL)
        return invalid;

    panel = get_other_panel ();
    if (panel == NULL)
        return invalid;

    return mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_PANEL, panel);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_panel_cwd (const mc_runtime_handle_t *handle, mc_runtime_string_t *path,
                        const char **error)
{
    WPanel *panel = runtime_host_panel_resolve (handle, error);

    if (panel == NULL)
        return FALSE;
    if (path == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    runtime_host_string_set (path, vfs_path_to_str_flags (panel->cwd_vpath, 0, VPF_STRIP_PASSWORD));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_panel_current (const mc_runtime_handle_t *handle, mc_runtime_file_snapshot_t **file,
                            const char **error)
{
    WPanel *panel = runtime_host_panel_resolve (handle, error);

    if (file != NULL)
        *file = NULL;
    if (panel == NULL)
        return FALSE;
    if (file == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    *file = runtime_host_panel_file_snapshot_new (panel, panel_current_entry (panel));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_panel_selected (const mc_runtime_handle_t *handle, mc_runtime_file_list_t *files,
                             const char **error)
{
    WPanel *panel = runtime_host_panel_resolve (handle, error);
    guint i;
    guint len = 0;

    if (files != NULL)
        memset (files, 0, sizeof (*files));
    if (panel == NULL)
        return FALSE;
    if (files == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    for (i = 0; i < (guint) panel->dir.len; i++)
        if (panel->dir.list[i].f.marked != 0)
            files->total_count++;

    len = MIN (files->total_count, MC_RUNTIME_EVENT_SELECTED_LIMIT);
    if (len != 0)
        files->items = g_new0 (mc_runtime_file_snapshot_t *, len);

    for (i = 0; i < (guint) panel->dir.len && files->len < len; i++)
    {
        mc_runtime_file_snapshot_t *file;

        if (panel->dir.list[i].f.marked == 0)
            continue;
        file = runtime_host_panel_file_snapshot_new (panel, &panel->dir.list[i]);
        if (file != NULL)
            files->items[files->len++] = file;
    }

    files->truncated = files->total_count > files->len;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_panel_refresh (const mc_runtime_handle_t *handle, const char **error)
{
    WPanel *panel = runtime_host_panel_resolve (handle, error);

    if (panel == NULL)
        return FALSE;

    panel_reload (panel);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_panel_chdir (const mc_runtime_handle_t *handle, const char *path, const char **error)
{
    WPanel *panel = runtime_host_panel_resolve (handle, error);
    vfs_path_t *vpath;
    gboolean result;

    if (panel == NULL)
        return FALSE;
    if (path == NULL || path[0] == '\0')
        return runtime_host_set_error (error, "invalid_argument");

    vpath = vfs_path_from_str (path);
    if (vpath == NULL)
        return runtime_host_set_error (error, "invalid_path");

    result = panel_cd (panel, vpath, cd_runtime_plugin);
    vfs_path_free (vpath, TRUE);

    if (!result)
        return runtime_host_set_error (error, "failed");
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static WEdit *
runtime_host_editor_resolve (const mc_runtime_handle_t *handle, const char **error)
{
    WEdit *editor;

    if (!runtime_host_objects_are_ready (error))
        return NULL;

    editor = (WEdit *) mc_runtime_handle_resolve (handle, MC_RUNTIME_HANDLE_EDITOR);
    if (editor == NULL)
        runtime_host_set_error (error, "closed");

    return editor;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
runtime_host_editor_current (void)
{
    mc_runtime_handle_t invalid = { MC_RUNTIME_HANDLE_INVALID, 0, 0 };

    if (!events_runtime_is_started () || runtime_host_current_editor == NULL)
        return invalid;

    return mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_EDITOR, runtime_host_current_editor);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_path (const mc_runtime_handle_t *handle, mc_runtime_string_t *path,
                          const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);

    if (editor == NULL)
        return FALSE;
    if (path == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    runtime_host_string_set (
        path,
        editor->filename_vpath != NULL
            ? vfs_path_to_str_flags (editor->filename_vpath, 0, VPF_STRIP_PASSWORD)
            : g_strdup (""));
    return TRUE;
#else
    (void) handle;
    (void) path;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_editor_info_free (mc_runtime_editor_info_t *info)
{
    if (info == NULL)
        return;
    g_free (info->path);
    g_free (info->name);
    memset (info, 0, sizeof (*info));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_info (const mc_runtime_handle_t *handle, mc_runtime_editor_info_t *info,
                          const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);
    const char *name;

    if (editor == NULL)
        return FALSE;
    if (info == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    memset (info, 0, sizeof (*info));
    if (editor->filename_vpath != NULL)
    {
        info->path = vfs_path_to_str_flags (editor->filename_vpath, 0, VPF_STRIP_PASSWORD);
        info->path_length = strlen (info->path);
        info->has_path = info->path_length != 0;
        name = vfs_path_get_last_path_str (editor->filename_vpath);
        info->name = g_strdup (name != NULL ? name : "");
    }
    else
        info->name = g_strdup ("");
    info->name_length = strlen (info->name);
    info->modified = editor->modified != 0;
    /* WEdit buffers remain mutable even when their current backing file is not
     * writable; save/save-as policy is handled by the editor itself. */
    info->readonly = FALSE;
    info->revision = MAX (editor->runtime_revision, 1);
    info->byte_length = (guint64) MAX (editor->buffer.size, 0);
    info->line_count = (guint64) MAX (editor->buffer.lines, 0) + 1;
    return TRUE;
#else
    (void) handle;
    (void) info;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

#ifdef USE_INTERNAL_EDIT
static void
runtime_host_editor_redraw (WEdit *editor)
{
    Widget *widget = WIDGET (editor);

    editor->force |= REDRAW_PAGE;
    if (widget->owner != NULL)
        widget_draw (WIDGET (widget->owner));
    else
        widget_draw (widget);
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_editor_position (const WEdit *editor, off_t offset,
                              mc_runtime_editor_position_t *position)
{
    off_t current;
    guint64 line = 1;
    guint64 column = 1;

    offset = CLAMP (offset, 0, editor->buffer.size);
    for (current = 0; current < offset;)
    {
        const int current_byte = edit_buffer_get_byte (&editor->buffer, current);
        int char_length = 1;

        if (current_byte == '\n')
        {
            line++;
            column = 1;
            current++;
            continue;
        }
        if (editor->utf8)
        {
            (void) edit_buffer_get_utf (&editor->buffer, current, &char_length);
            if (char_length < 1 || current + char_length > offset)
                char_length = 1;
        }
        current += char_length;
        column++;
    }

    position->offset = (guint64) offset;
    position->line = line;
    position->column = column;
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_editor_selection_copy_text (const WEdit *editor,
                                         mc_runtime_editor_selection_t *selection)
{
    guint64 total = selection->ranges_count > 0 ? selection->ranges_count - 1 : 0;
    guint range_index;
    char *cursor;

    for (range_index = 0; range_index < selection->ranges_count; range_index++)
        total += selection->ranges[range_index].to - selection->ranges[range_index].from;

    if (total > RUNTIME_HOST_EDITOR_SELECTION_TEXT_LIMIT || total > G_MAXSIZE - 1)
    {
        selection->text_truncated = TRUE;
        return;
    }

    selection->text_length = (gsize) total;
    selection->text = g_malloc (selection->text_length + 1);
    cursor = selection->text;
    for (range_index = 0; range_index < selection->ranges_count; range_index++)
    {
        const mc_runtime_editor_range_t *range = &selection->ranges[range_index];

        if (range_index != 0)
            *cursor++ = '\n';
        for (guint64 offset = range->from; offset < range->to; offset++)
            *cursor++ = (char) edit_buffer_get_byte (&editor->buffer, (off_t) offset);
    }
    *cursor = '\0';
    selection->has_text = TRUE;
}
#endif

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_editor_selection_free (mc_runtime_editor_selection_t *selection)
{
    if (selection == NULL)
        return;
    g_free (selection->ranges);
    g_free (selection->text);
    memset (selection, 0, sizeof (*selection));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_selection (const mc_runtime_handle_t *handle,
                               mc_runtime_editor_selection_t *selection, const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);
    off_t anchor;
    off_t cursor;
    off_t start;
    off_t finish;

    if (editor == NULL)
        return FALSE;
    if (selection == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    memset (selection, 0, sizeof (*selection));
    selection->revision = MAX (editor->runtime_revision, 1);
    cursor = editor->end_mark_curs >= 0 ? editor->end_mark_curs : editor->buffer.curs1;
    anchor = editor->mark1;

    if (!eval_marks (editor, &start, &finish))
    {
        selection->kind = MC_RUNTIME_EDITOR_SELECTION_NONE;
        runtime_host_editor_position (editor, editor->buffer.curs1, &selection->anchor);
        selection->cursor = selection->anchor;
        return TRUE;
    }

    anchor = CLAMP (anchor, 0, editor->buffer.size);
    cursor = CLAMP (editor->mark2 >= 0 ? editor->mark2 : cursor, 0, editor->buffer.size);
    runtime_host_editor_position (editor, anchor, &selection->anchor);
    runtime_host_editor_position (editor, cursor, &selection->cursor);

    if (!editor->column_highlight)
    {
        selection->kind = MC_RUNTIME_EDITOR_SELECTION_LINEAR;
        selection->ranges_count = 1;
        selection->ranges = g_new (mc_runtime_editor_range_t, 1);
        selection->ranges[0].from = (guint64) start;
        selection->ranges[0].to = (guint64) finish;
    }
    else
    {
        GArray *ranges = g_array_new (FALSE, FALSE, sizeof (mc_runtime_editor_range_t));
        const long column1 = MIN (editor->column1, editor->column2);
        const long column2 = MAX (editor->column1, editor->column2);
        off_t bol;

        selection->kind = MC_RUNTIME_EDITOR_SELECTION_COLUMN;
        for (bol = edit_buffer_get_bol (&editor->buffer, start); bol < finish;)
        {
            const off_t eol = edit_buffer_get_eol (&editor->buffer, bol);
            mc_runtime_editor_range_t range;
            off_t next;

            range.from = (guint64) MAX (edit_get_line_offset (editor, bol, column1, NULL), start);
            range.to = (guint64) MIN (edit_get_line_offset (editor, bol, column2, NULL),
                                      MIN (eol, finish));
            if (range.to < range.from)
                range.to = range.from;
            g_array_append_val (ranges, range);

            if (eol >= editor->buffer.size)
                break;
            next = eol + 1;
            if (next <= bol)
                break;
            bol = next;
        }
        selection->ranges_count = ranges->len;
        selection->ranges = (mc_runtime_editor_range_t *) g_array_free (ranges, FALSE);
    }

    runtime_host_editor_selection_copy_text (editor, selection);
    return TRUE;
#else
    (void) handle;
    (void) selection;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    const char *data;
    gsize length;
} runtime_host_text_slice_t;

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_replace (const mc_runtime_handle_t *handle, guint64 from, guint64 to,
                             const char *text, gsize text_length,
                             mc_runtime_editor_edit_result_t *result, const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);

    if (editor == NULL)
        return FALSE;
    if ((text == NULL && text_length != 0) || result == NULL || from > to
        || to > (guint64) editor->buffer.size)
        return runtime_host_set_error (error, "invalid_range");
    if (text == NULL)
        text = "";
    if (!edit_runtime_begin (editor))
        return runtime_host_set_error (error, "edit_active");

    edit_cursor_move (editor, (off_t) from - editor->buffer.curs1);
    for (guint64 offset = from; offset < to; offset++)
        (void) edit_delete (editor, TRUE);
    for (gsize i = 0; i < text_length; i++)
        edit_insert (editor, (unsigned char) text[i]);

    edit_runtime_end (editor);
    runtime_host_editor_redraw (editor);
    memset (result, 0, sizeof (*result));
    result->revision = MAX (editor->runtime_revision, 1);
    runtime_host_editor_position (editor, editor->buffer.curs1, &result->cursor);
    return TRUE;
#else
    (void) handle;
    (void) from;
    (void) to;
    (void) text;
    (void) text_length;
    (void) result;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

gboolean
runtime_host_editor_text (const mc_runtime_handle_t *handle, const mc_runtime_editor_range_t *range,
                          gboolean has_revision, guint64 revision, mc_runtime_string_t *text,
                          const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);
    guint64 from = 0;
    guint64 to;
    gsize length;

    if (editor == NULL)
        return FALSE;
    if (text == NULL)
        return runtime_host_set_error (error, "invalid_argument");
    memset (text, 0, sizeof (*text));
    if (has_revision && revision != MAX (editor->runtime_revision, 1))
        return runtime_host_set_error (error, "stale_revision");

    to = (guint64) editor->buffer.size;
    if (range != NULL)
    {
        from = range->from;
        to = range->to;
    }
    if (from > to || to > (guint64) editor->buffer.size)
        return runtime_host_set_error (error, "invalid_range");
    if (to - from > RUNTIME_HOST_EDITOR_TEXT_LIMIT || to - from > G_MAXSIZE - 1)
        return runtime_host_set_error (error, "too_large");

    length = (gsize) (to - from);
    text->data = g_malloc (length + 1);
    for (gsize i = 0; i < length; i++)
        text->data[i] = (char) edit_buffer_get_byte (&editor->buffer, (off_t) from + (off_t) i);
    text->data[length] = '\0';
    text->length = length;
    return TRUE;
#else
    (void) handle;
    (void) range;
    (void) has_revision;
    (void) revision;
    (void) text;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

#ifdef USE_INTERNAL_EDIT
static int
runtime_host_editor_change_compare (const void *left, const void *right)
{
    const mc_runtime_editor_change_t *a = (const mc_runtime_editor_change_t *) left;
    const mc_runtime_editor_change_t *b = (const mc_runtime_editor_change_t *) right;

    if (a->from < b->from)
        return -1;
    if (a->from > b->from)
        return 1;
    if (a->to < b->to)
        return -1;
    return a->to > b->to ? 1 : 0;
}
#endif

gboolean
runtime_host_editor_edit (const mc_runtime_handle_t *handle,
                          const mc_runtime_editor_edit_t *edit_spec,
                          mc_runtime_editor_edit_result_t *result, const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);
    mc_runtime_editor_change_t *changes;
    guint64 final_size;
    guint64 aggregate = 0;

    if (editor == NULL)
        return FALSE;
    if (edit_spec == NULL || result == NULL || edit_spec->changes == NULL
        || edit_spec->changes_count == 0
        || edit_spec->changes_count > RUNTIME_HOST_EDITOR_EDIT_CHANGES_MAX)
        return runtime_host_set_error (error, "invalid_argument");
    if (edit_spec->revision != MAX (editor->runtime_revision, 1))
        return runtime_host_set_error (error, "stale_revision");

    changes = g_memdup2 (edit_spec->changes, sizeof (*changes) * (gsize) edit_spec->changes_count);
    qsort (changes, edit_spec->changes_count, sizeof (*changes),
           runtime_host_editor_change_compare);
    final_size = (guint64) editor->buffer.size;
    for (guint i = 0; i < edit_spec->changes_count; i++)
    {
        const mc_runtime_editor_change_t *change = &changes[i];

        if (change->from > change->to || change->to > (guint64) editor->buffer.size
            || (change->text == NULL && change->text_length != 0)
            || (i != 0 && changes[i - 1].to > change->from)
            || change->text_length > RUNTIME_HOST_EDITOR_EDIT_TEXT_LIMIT
            || aggregate > RUNTIME_HOST_EDITOR_EDIT_TEXT_LIMIT - change->text_length)
        {
            g_free (changes);
            return runtime_host_set_error (error, "invalid_edit");
        }
        aggregate += change->text_length;
        final_size -= change->to - change->from;
        if (final_size > G_MAXUINT64 - change->text_length)
        {
            g_free (changes);
            return runtime_host_set_error (error, "too_large");
        }
        final_size += change->text_length;
    }
    if (edit_spec->has_cursor && edit_spec->cursor.offset > final_size)
    {
        g_free (changes);
        return runtime_host_set_error (error, "invalid_position");
    }
    if (!edit_runtime_begin (editor))
    {
        g_free (changes);
        return runtime_host_set_error (error, "edit_active");
    }

    for (guint index = edit_spec->changes_count; index > 0; index--)
    {
        const mc_runtime_editor_change_t *change = &changes[index - 1];

        edit_cursor_move (editor, (off_t) change->from - editor->buffer.curs1);
        for (guint64 offset = change->from; offset < change->to; offset++)
            (void) edit_delete (editor, TRUE);
        for (gsize i = 0; i < change->text_length; i++)
            edit_insert (editor, (unsigned char) change->text[i]);
    }
    if (edit_spec->has_cursor)
        edit_cursor_move (editor, (off_t) edit_spec->cursor.offset - editor->buffer.curs1);
    edit_set_markers (editor, 0, 0, 0, 0);
    editor->end_mark_curs = -1;
    editor->highlight = 0;
    editor->column_highlight = 0;
    editor->word_highlight = FALSE;
    editor->line_highlight = FALSE;
    edit_runtime_end (editor);
    runtime_host_editor_redraw (editor);

    memset (result, 0, sizeof (*result));
    result->revision = MAX (editor->runtime_revision, 1);
    runtime_host_editor_position (editor, editor->buffer.curs1, &result->cursor);
    g_free (changes);
    return TRUE;
#else
    (void) handle;
    (void) edit_spec;
    (void) result;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_replace_selection_v2 (const mc_runtime_handle_t *handle, guint64 revision,
                                          const char *text, gsize text_length,
                                          mc_runtime_editor_edit_result_t *result,
                                          const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);

    if (editor == NULL)
        return FALSE;
    if (revision != MAX (editor->runtime_revision, 1))
        return runtime_host_set_error (error, "stale_revision");
    return runtime_host_editor_replace_selection (handle, text, text_length, result, error);
#else
    (void) handle;
    (void) revision;
    (void) text;
    (void) text_length;
    (void) result;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

gboolean
runtime_host_editor_replace_selection (const mc_runtime_handle_t *handle, const char *text,
                                       gsize text_length, mc_runtime_editor_edit_result_t *result,
                                       const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);
    mc_runtime_editor_selection_t selection = { 0 };
    GArray *rows = NULL;
    guint range_index;
    long column_start = 0;

    if (editor == NULL)
        return FALSE;
    if ((text == NULL && text_length != 0) || result == NULL)
        return runtime_host_set_error (error, "invalid_argument");
    if (text == NULL)
        text = "";
    if (!runtime_host_editor_selection (handle, &selection, error))
        return FALSE;
    if (selection.kind == MC_RUNTIME_EDITOR_SELECTION_NONE)
    {
        runtime_host_editor_selection_free (&selection);
        return runtime_host_set_error (error, "no_selection");
    }

    rows = g_array_new (FALSE, FALSE, sizeof (runtime_host_text_slice_t));
    if (selection.kind == MC_RUNTIME_EDITOR_SELECTION_COLUMN)
    {
        gsize row_start = 0;

        column_start = MIN (editor->column1, editor->column2);

        for (gsize i = 0; i <= text_length; i++)
            if (i == text_length || text[i] == '\n')
            {
                runtime_host_text_slice_t row = { text + row_start, i - row_start };

                g_array_append_val (rows, row);
                row_start = i + 1;
            }
        if (rows->len != 1 && rows->len != selection.ranges_count)
        {
            g_array_free (rows, TRUE);
            runtime_host_editor_selection_free (&selection);
            return runtime_host_set_error (error, "invalid_replacement");
        }
    }
    else
    {
        runtime_host_text_slice_t row = { text, text_length };

        g_array_append_val (rows, row);
    }

    if (!edit_runtime_begin (editor))
    {
        g_array_free (rows, TRUE);
        runtime_host_editor_selection_free (&selection);
        return runtime_host_set_error (error, "edit_active");
    }

    for (range_index = selection.ranges_count; range_index > 0; range_index--)
    {
        const mc_runtime_editor_range_t *range = &selection.ranges[range_index - 1];
        const guint row_index = rows->len == 1 ? 0 : range_index - 1;
        const runtime_host_text_slice_t *row =
            &g_array_index (rows, runtime_host_text_slice_t, row_index);

        edit_cursor_move (editor, (off_t) range->from - editor->buffer.curs1);
        if (selection.kind == MC_RUNTIME_EDITOR_SELECTION_COLUMN && row->length != 0
            && range->from == range->to)
        {
            long reached = 0;

            (void) edit_get_line_offset (editor, edit_buffer_get_current_bol (&editor->buffer),
                                         column_start, &reached);
            for (; reached < column_start; reached++)
                edit_insert (editor, ' ');
        }
        for (guint64 offset = range->from; offset < range->to; offset++)
            (void) edit_delete (editor, TRUE);
        for (gsize i = 0; i < row->length; i++)
            edit_insert (editor, (unsigned char) row->data[i]);
    }
    edit_set_markers (editor, 0, 0, 0, 0);
    editor->end_mark_curs = -1;
    editor->highlight = 0;
    editor->column_highlight = 0;
    editor->word_highlight = FALSE;
    editor->line_highlight = FALSE;
    edit_runtime_end (editor);
    runtime_host_editor_redraw (editor);

    memset (result, 0, sizeof (*result));
    result->revision = MAX (editor->runtime_revision, 1);
    runtime_host_editor_position (editor, editor->buffer.curs1, &result->cursor);
    g_array_free (rows, TRUE);
    runtime_host_editor_selection_free (&selection);
    return TRUE;
#else
    (void) handle;
    (void) text;
    (void) text_length;
    (void) result;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_cursor (const mc_runtime_handle_t *handle, guint64 *line, guint64 *column,
                            const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);

    if (editor == NULL)
        return FALSE;
    if (line == NULL || column == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    edit_update_curs_col (editor);
    *line = (guint64) MAX (editor->buffer.curs_line, 0) + 1;
    *column = (guint64) MAX (editor->curs_col + editor->over_col, 0) + 1;
    return TRUE;
#else
    (void) handle;
    (void) line;
    (void) column;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_set_cursor (const mc_runtime_handle_t *handle, guint64 line, guint64 column,
                                const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);

    if (editor == NULL)
        return FALSE;
    if (line == 0 || column == 0 || line > (guint64) G_MAXLONG || column > (guint64) G_MAXLONG)
        return runtime_host_set_error (error, "invalid_argument");

    edit_move_to_line (editor, (long) line - 1);
    editor->prev_col = (long) column - 1;
    editor->over_col = 0;
    edit_move_to_prev_col (editor, edit_buffer_get_current_bol (&editor->buffer));
    runtime_host_editor_redraw (editor);
    return TRUE;
#else
    (void) handle;
    (void) line;
    (void) column;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_is_readonly (const mc_runtime_handle_t *handle, gboolean *readonly,
                                 const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);

    if (editor == NULL)
        return FALSE;
    if (readonly == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    *readonly = FALSE;
    return TRUE;
#else
    (void) handle;
    (void) readonly;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_tab_width (const mc_runtime_handle_t *handle, guint *tab_width,
                               const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);

    if (editor == NULL)
        return FALSE;
    if (tab_width == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    *tab_width = (guint) MAX (TAB_SIZE, 1);
    return TRUE;
#else
    (void) handle;
    (void) tab_width;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_get_text (const mc_runtime_handle_t *handle, gint64 from, gint64 to,
                              mc_runtime_string_t *text, const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);
    off_t first;
    off_t last;
    gsize length;
    gsize i;
    char *contents;

    if (editor == NULL)
        return FALSE;
    if (text == NULL || from < 1 || to < from || to > editor->buffer.size)
        return runtime_host_set_error (error, "invalid_range");

    first = (off_t) from - 1;
    last = (off_t) to - 1;
    length = (gsize) (last - first + 1);
    contents = g_malloc (length + 1);
    for (i = 0; i < length; i++)
        contents[i] = (char) edit_buffer_get_byte (&editor->buffer, first + (off_t) i);
    contents[length] = '\0';
    text->data = contents;
    text->length = length;
    return TRUE;
#else
    (void) handle;
    (void) from;
    (void) to;
    (void) text;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_selected_text (const mc_runtime_handle_t *handle, mc_runtime_string_t *text,
                                   const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);
    off_t start;
    off_t finish;
    gsize length;
    gsize i;
    char *contents;

    if (editor == NULL)
        return FALSE;
    if (text == NULL)
        return runtime_host_set_error (error, "invalid_argument");
    if (!eval_marks (editor, &start, &finish))
        return runtime_host_set_error (error, "no_selection");
    if (editor->column_highlight)
        return runtime_host_set_error (error, "column_selection_not_supported");
    if (finish <= start || (guint64) (finish - start) > G_MAXSIZE - 1)
        return runtime_host_set_error (error, "invalid_range");

    length = (gsize) (finish - start);
    contents = g_malloc (length + 1);
    for (i = 0; i < length; i++)
        contents[i] = (char) edit_buffer_get_byte (&editor->buffer, start + (off_t) i);
    contents[length] = '\0';
    text->data = contents;
    text->length = length;
    return TRUE;
#else
    (void) handle;
    (void) text;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

gboolean
runtime_host_editor_insert (const mc_runtime_handle_t *handle, const char *text, const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);
    const char *cursor;

    if (editor == NULL)
        return FALSE;
    if (text == NULL)
        return runtime_host_set_error (error, "invalid_argument");
    if (text[0] == '\0')
        return TRUE;
    if (!edit_runtime_begin (editor))
        return runtime_host_set_error (error, "edit_active");

    edit_insert_over (editor);
    for (cursor = text; *cursor != '\0'; cursor++)
        edit_insert (editor, (unsigned char) *cursor);
    edit_runtime_end (editor);
    runtime_host_editor_redraw (editor);
    return TRUE;
#else
    (void) handle;
    (void) text;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_editor_save (const mc_runtime_handle_t *handle, const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);

    if (editor == NULL)
        return FALSE;
    if (!edit_runtime_save (editor))
        return runtime_host_set_error (error, "failed");
    return TRUE;
#else
    (void) handle;
    return runtime_host_set_error (error, "not_supported");
#endif
}

/* --------------------------------------------------------------------------------------------- */

static WView *
runtime_host_viewer_resolve (const mc_runtime_handle_t *handle, const char **error)
{
    WView *viewer;

    if (!runtime_host_objects_are_ready (error))
        return NULL;

    viewer = (WView *) mc_runtime_handle_resolve (handle, MC_RUNTIME_HANDLE_VIEWER);
    if (viewer == NULL)
        runtime_host_set_error (error, "closed");

    return viewer;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
runtime_host_viewer_current (void)
{
    mc_runtime_handle_t invalid = { MC_RUNTIME_HANDLE_INVALID, 0, 0 };

    if (!events_runtime_is_started () || runtime_host_current_viewer == NULL)
        return invalid;

    return mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_VIEWER, runtime_host_current_viewer);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_viewer_path (const mc_runtime_handle_t *handle, mc_runtime_string_t *path,
                          const char **error)
{
    WView *viewer = runtime_host_viewer_resolve (handle, error);

    if (viewer == NULL)
        return FALSE;
    if (path == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    runtime_host_string_set (
        path,
        viewer->filename_vpath != NULL
            ? vfs_path_to_str_flags (viewer->filename_vpath, 0, VPF_STRIP_PASSWORD)
            : g_strdup (""));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_viewer_position (const mc_runtime_handle_t *handle, gint64 *offset, const char **error)
{
    WView *viewer = runtime_host_viewer_resolve (handle, error);

    if (viewer == NULL)
        return FALSE;
    if (offset == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    *offset = (gint64) (viewer->mode_flags.hex ? viewer->hex_cursor : viewer->dpy_start);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_viewer_goto (const mc_runtime_handle_t *handle, gint64 offset, const char **error)
{
    WView *viewer = runtime_host_viewer_resolve (handle, error);

    if (viewer == NULL)
        return FALSE;
    if (offset < 0)
        return runtime_host_set_error (error, "invalid_argument");

    mcview_moveto_offset (viewer, (off_t) offset);
    mcview_update (viewer);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_viewer_mode (const mc_runtime_handle_t *handle, mc_runtime_string_t *mode,
                          const char **error)
{
    WView *viewer = runtime_host_viewer_resolve (handle, error);
    const char *name;

    if (viewer == NULL)
        return FALSE;
    if (mode == NULL)
        return runtime_host_set_error (error, "invalid_argument");

    if (viewer->mode_flags.terminal)
        name = "terminal";
    else if (viewer->mode_flags.structured)
        name = "structured";
    else if (viewer->mode_flags.hex)
        name = "hex";
    else
        name = "text";
    runtime_host_string_set (mode, g_strdup (name));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_file_snapshot_free (mc_runtime_file_snapshot_t *file)
{
    mc_runtime_file_snapshot_free (file);
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_file_list_free (mc_runtime_file_list_t *files)
{
    guint i;

    if (files == NULL)
        return;

    for (i = 0; i < files->len; i++)
        mc_runtime_file_snapshot_free (files->items[i]);
    g_free (files->items);
    memset (files, 0, sizeof (*files));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_ui_status (const char *text)
{
    if (!events_runtime_is_started () || mc_global.mc_run_mode != MC_RUN_FULL || the_hint == NULL
        || WIDGET (the_hint)->owner == NULL)
        return FALSE;

    set_hintbar (text != NULL ? text : "");
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_ui_refresh (const char *area)
{
    if (!events_runtime_is_started () || area == NULL)
        return;

    if (strcmp (area, "editor") == 0 && runtime_host_current_editor != NULL
        && WIDGET (runtime_host_current_editor)->owner != NULL)
    {
        widget_draw (WIDGET (runtime_host_current_editor));
        tty_refresh ();
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_ui_message (const char *title, const char *text)
{
    if (!events_runtime_is_started ())
        return FALSE;

    message (D_NORMAL, title != NULL && title[0] != '\0' ? title : "Lua", "%s",
             text != NULL ? text : "");
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_ui_text_width (const char *text, gsize text_length, guint *width, const char **error)
{
    char *copy;
    int measured;

    if ((text == NULL && text_length != 0) || width == NULL
        || (text != NULL && memchr (text, '\0', text_length) != NULL))
        return runtime_host_set_error (error, "invalid_argument");
    if (text == NULL)
        text = "";
    if (!g_utf8_validate (text, (gssize) text_length, NULL))
        return runtime_host_set_error (error, "invalid_utf8");

    copy = g_strndup (text, text_length);
    measured = str_term_width1 (copy);
    g_free (copy);
    if (measured < 0)
        return runtime_host_set_error (error, "invalid_utf8");
    *width = (guint) measured;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_dialog_field_free (runtime_host_dialog_field_t *field)
{
    if (field == NULL)
        return;
    g_free (field->text);
    g_free (field->items);
    g_free (field);
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_dialog_add_widget (runtime_host_dialog_builder_t *builder, quick_widget_t widget)
{
    g_array_append_val (builder->widgets, widget);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_dialog_add_button (runtime_host_dialog_builder_t *builder,
                                const mc_runtime_dialog_control_t *control)
{
    runtime_host_dialog_button_t button;
    quick_widget_t widget = { 0 };

    button.action = control->cancel_button
        ? B_CANCEL
        : (control->default_button ? B_ENTER : builder->next_action++);
    button.id = control->id;
    g_array_append_val (builder->buttons, button);

    widget.widget_type = quick_button;
    widget.options = WOP_DEFAULT;
    widget.pos_flags = WPOS_KEEP_DEFAULT;
    widget.u.button.text = control->label;
    widget.u.button.action = button.action;
    runtime_host_dialog_add_widget (builder, widget);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_dialog_input_completion (const mc_runtime_dialog_control_t *control,
                                      input_complete_t *flags)
{
    guint i;

    *flags = INPUT_COMPLETE_NONE;
    for (i = 0; i < control->options_count; i++)
    {
        const char *name = control->options[i].id;

        if (g_strcmp0 (name, "files") == 0)
            *flags |= INPUT_COMPLETE_FILENAMES;
        else if (g_strcmp0 (name, "hosts") == 0)
            *flags |= INPUT_COMPLETE_HOSTNAMES;
        else if (g_strcmp0 (name, "commands") == 0)
            *flags |= INPUT_COMPLETE_COMMANDS;
        else if (g_strcmp0 (name, "variables") == 0)
            *flags |= INPUT_COMPLETE_VARIABLES;
        else if (g_strcmp0 (name, "users") == 0)
            *flags |= INPUT_COMPLETE_USERNAMES;
        else if (g_strcmp0 (name, "cd") == 0)
            *flags |= INPUT_COMPLETE_CD;
        else if (g_strcmp0 (name, "shell") == 0)
            *flags |= INPUT_COMPLETE_SHELL_ESC;
        else
            return FALSE;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_dialog_add_controls (runtime_host_dialog_builder_t *builder,
                                  const mc_runtime_dialog_control_t *controls, guint count,
                                  const char **error)
{
    guint i;

    for (i = 0; i < count; i++)
    {
        const mc_runtime_dialog_control_t *control = &controls[i];
        quick_widget_t widget = { 0 };
        runtime_host_dialog_field_t *field;

        widget.options = WOP_DEFAULT;
        widget.pos_flags = WPOS_KEEP_DEFAULT;
        switch (control->type)
        {
        case MC_RUNTIME_DIALOG_LABEL:
            widget.widget_type = quick_label;
            widget.u.label.text = control->text;
            runtime_host_dialog_add_widget (builder, widget);
            break;

        case MC_RUNTIME_DIALOG_INPUT:
        {
            input_complete_t completion_flags;

            if (!runtime_host_dialog_input_completion (control, &completion_flags))
                return runtime_host_set_error (error, "invalid_dialog");
            field = g_new0 (runtime_host_dialog_field_t, 1);
            field->control = control;
            g_ptr_array_add (builder->fields, field);
            widget.widget_type = quick_input;
            widget.id = &field->widget_id;
            widget.u.input.text = control->value != NULL ? control->value : "";
            widget.u.input.histname = control->text != NULL ? control->text : "";
            widget.u.input.result = &field->text;
            widget.u.input.completion_flags = completion_flags;
            runtime_host_dialog_add_widget (builder, widget);
            builder->want_tab |= control->checked;
            break;
        }

        case MC_RUNTIME_DIALOG_CHECKBOX:
            field = g_new0 (runtime_host_dialog_field_t, 1);
            field->control = control;
            field->checked = control->checked;
            g_ptr_array_add (builder->fields, field);
            widget.widget_type = quick_checkbox;
            widget.u.checkbox.text = control->label;
            widget.u.checkbox.state = &field->checked;
            runtime_host_dialog_add_widget (builder, widget);
            break;

        case MC_RUNTIME_DIALOG_SELECT:
        {
            guint j;
            field = g_new0 (runtime_host_dialog_field_t, 1);
            field->control = control;
            field->items = g_new0 (const char *, control->options_count + 1);
            for (j = 0; j < control->options_count; j++)
            {
                field->items[j] = control->options[j].label;
                if (g_strcmp0 (control->value, control->options[j].id) == 0)
                    field->selected = (int) j;
            }
            g_ptr_array_add (builder->fields, field);
            if (control->label != NULL && control->label[0] != '\0')
            {
                quick_widget_t label = { 0 };
                label.widget_type = quick_label;
                label.options = WOP_DEFAULT;
                label.pos_flags = WPOS_KEEP_DEFAULT;
                label.u.label.text = control->label;
                runtime_host_dialog_add_widget (builder, label);
            }
            widget.widget_type = quick_radio;
            widget.u.radio.count = (int) control->options_count;
            widget.u.radio.items = field->items;
            widget.u.radio.value = &field->selected;
            runtime_host_dialog_add_widget (builder, widget);
            break;
        }

        case MC_RUNTIME_DIALOG_SEPARATOR:
            widget.widget_type = quick_separator;
            widget.u.separator.space = TRUE;
            widget.u.separator.line = TRUE;
            runtime_host_dialog_add_widget (builder, widget);
            break;

        case MC_RUNTIME_DIALOG_HBOX:
        {
            gboolean buttons_only = TRUE;
            guint j;
            for (j = 0; j < control->controls_count; j++)
                if (control->controls[j].type != MC_RUNTIME_DIALOG_BUTTON
                    && control->controls[j].type != MC_RUNTIME_DIALOG_SPACER)
                    buttons_only = FALSE;
            if (!buttons_only)
                return runtime_host_set_error (error, "invalid_dialog");
            widget.widget_type = quick_buttons;
            widget.u.separator.space = TRUE;
            widget.u.separator.line = TRUE;
            runtime_host_dialog_add_widget (builder, widget);
            for (j = 0; j < control->controls_count; j++)
                if (control->controls[j].type == MC_RUNTIME_DIALOG_BUTTON)
                    (void) runtime_host_dialog_add_button (builder, &control->controls[j]);
            break;
        }

        case MC_RUNTIME_DIALOG_VBOX:
            if (!runtime_host_dialog_add_controls (builder, control->controls,
                                                   control->controls_count, error))
                return FALSE;
            break;

        case MC_RUNTIME_DIALOG_SPACER:
            break;

        case MC_RUNTIME_DIALOG_BUTTON:
            (void) runtime_host_dialog_add_button (builder, control);
            break;

        default:
            return runtime_host_set_error (error, "invalid_dialog");
        }
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
runtime_host_dialog_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    runtime_host_dialog_builder_t *builder = runtime_host_active_dialog_builder;

    if (msg == MSG_INIT && builder != NULL && builder->want_tab)
        widget_want_tab (w, TRUE);
    else if (msg == MSG_KEY && builder != NULL && builder->want_tab)
    {
        WGroup *group = GROUP (w);

        if (parm == '\t')
        {
            guint i;
            unsigned long current_id = group_get_current_widget_id (group);

            for (i = 0; i < builder->fields->len; i++)
            {
                const runtime_host_dialog_field_t *field =
                    (const runtime_host_dialog_field_t *) g_ptr_array_index (builder->fields, i);

                if (field->widget_id == current_id
                    && field->control->type == MC_RUNTIME_DIALOG_INPUT && field->control->checked)
                    return send_message (WIDGET (group->current->data), NULL, MSG_ACTION,
                                         CK_Complete, NULL);
            }

            group_select_next_widget (group);
            return MSG_HANDLED;
        }
        if ((parm & ~(KEY_M_SHIFT | KEY_M_CTRL)) == '\t')
        {
            group_select_prev_widget (group);
            return MSG_HANDLED;
        }
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_dialog_result_free (mc_runtime_dialog_result_t *result)
{
    guint i;

    if (result == NULL)
        return;
    g_free (result->button_id);
    for (i = 0; i < result->values_count; i++)
    {
        g_free ((char *) result->values[i].id);
        g_free ((char *) result->values[i].value);
    }
    g_free (result->values);
    memset (result, 0, sizeof (*result));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_ui_dialog (const mc_runtime_dialog_t *dialog, mc_runtime_dialog_result_t *result,
                        const char **error)
{
    runtime_host_dialog_builder_t builder;
    quick_widget_t end = { 0 };
    quick_dialog_t qdlg;
    WRect rect;
    int action;
    guint i;

    if (!events_runtime_is_started ())
        return runtime_host_set_error (error, "not_ready");
    if (runtime_host_dialog_active)
        return runtime_host_set_error (error, "dialog_active");
    if (dialog == NULL || result == NULL)
        return runtime_host_set_error (error, "invalid_dialog");

    memset (result, 0, sizeof (*result));
    builder.widgets = g_array_new (FALSE, TRUE, sizeof (quick_widget_t));
    builder.fields =
        g_ptr_array_new_with_free_func ((GDestroyNotify) runtime_host_dialog_field_free);
    builder.buttons = g_array_new (FALSE, TRUE, sizeof (runtime_host_dialog_button_t));
    builder.next_action = B_USER;
    if (!runtime_host_dialog_add_controls (&builder, dialog->controls, dialog->controls_count,
                                           error))
        goto fail;
    end.widget_type = quick_end;
    runtime_host_dialog_add_widget (&builder, end);

    rect.x = -1;
    rect.y = -1;
    rect.lines = dialog->has_height ? (int) dialog->height : 0;
    rect.cols = dialog->has_width ? (int) dialog->width : 0;
    qdlg.rect = rect;
    qdlg.title = dialog->title;
    qdlg.help = NULL;
    qdlg.help_file = NULL;
    qdlg.widgets = (quick_widget_t *) builder.widgets->data;
    qdlg.callback = runtime_host_dialog_callback;
    qdlg.mouse_callback = NULL;

    runtime_host_dialog_active = TRUE;
    runtime_host_active_dialog_builder = &builder;
    action = quick_dialog (&qdlg);
    runtime_host_active_dialog_builder = NULL;
    runtime_host_dialog_active = FALSE;
    if (action == B_CANCEL)
    {
        if (error != NULL)
            *error = "cancelled";
        goto fail;
    }

    for (i = 0; i < builder.buttons->len; i++)
    {
        const runtime_host_dialog_button_t *button =
            &g_array_index (builder.buttons, runtime_host_dialog_button_t, i);
        if (button->action == action)
            result->button_id = g_strdup (button->id);
    }
    if (result->button_id == NULL)
    {
        (void) runtime_host_set_error (error, "invalid_dialog");
        goto fail;
    }

    result->values_count = builder.fields->len;
    result->values = g_new0 (mc_runtime_dialog_value_t, result->values_count);
    for (i = 0; i < builder.fields->len; i++)
    {
        const runtime_host_dialog_field_t *field =
            (const runtime_host_dialog_field_t *) g_ptr_array_index (builder.fields, i);
        mc_runtime_dialog_value_t *value = &result->values[i];
        value->id = g_strdup (field->control->id);
        if (field->control->type == MC_RUNTIME_DIALOG_CHECKBOX)
        {
            value->is_boolean = TRUE;
            value->checked = field->checked;
        }
        else if (field->control->type == MC_RUNTIME_DIALOG_SELECT)
            value->value = g_strdup (field->control->options[field->selected].id);
        else
            value->value = g_strdup (field->text != NULL ? field->text : "");
    }

    g_array_free (builder.widgets, TRUE);
    g_ptr_array_free (builder.fields, TRUE);
    g_array_free (builder.buttons, TRUE);
    return TRUE;

fail:
    runtime_host_active_dialog_builder = NULL;
    runtime_host_dialog_active = FALSE;
    runtime_host_dialog_result_free (result);
    g_array_free (builder.widgets, TRUE);
    g_ptr_array_free (builder.fields, TRUE);
    g_array_free (builder.buttons, TRUE);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_log (const char *source, const char *level, const char *message)
{
    fprintf (stderr, "%s %s: %s\n", source != NULL ? source : "runtime",
             level != NULL ? level : "info", message != NULL ? message : "");
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_report_status (runtime_host_error_t *error)
{
    char *text;

    if (error == NULL || error->status_reported)
        return;

    text = g_strdup_printf ("%s/%s: %s", error->runtime_name, error->package_id, error->summary);
    if (runtime_host_ui_status (text))
        error->status_reported = TRUE;
    g_free (text);
}

/* --------------------------------------------------------------------------------------------- */

static void
runtime_host_runtime_error (const char *runtime_name, const char *package_id,
                            mc_runtime_error_phase_t phase, const char *summary,
                            const char *details)
{
    runtime_host_error_t *error;
    char *key;
    char *source;

    key = runtime_host_error_key (runtime_name, package_id, phase);
    if (runtime_host_errors == NULL)
        runtime_host_errors = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                     (GDestroyNotify) runtime_host_error_destroy);

    error = (runtime_host_error_t *) g_hash_table_lookup (runtime_host_errors, key);
    if (error == NULL)
    {
        error = g_new0 (runtime_host_error_t, 1);
        error->runtime_name = g_strdup (runtime_name != NULL ? runtime_name : "runtime");
        error->package_id = g_strdup (package_id != NULL ? package_id : "?");
        error->phase = phase;
        error->summary = g_strdup (summary != NULL ? summary : "Runtime callback failed");
        error->details = g_strdup (details != NULL ? details : error->summary);
        g_hash_table_insert (runtime_host_errors, key, error);
    }
    else
    {
        g_free (key);
        error->count++;
    }

    if (error->count == 0)
        error->count = 1;

    source = g_strdup_printf ("%s/%s/%s", error->runtime_name, error->package_id,
                              runtime_host_error_phase_name (error->phase));
    runtime_host_log (source, "error", details != NULL ? details : error->summary);
    g_free (source);

    if (events_runtime_is_started ())
        runtime_host_report_status (error);
}

/* --------------------------------------------------------------------------------------------- */

void
runtime_host_flush_errors (void)
{
    GHashTableIter iter;
    gpointer value;
    guint startup_errors = 0;
    guint total_errors = 0;
    GString *text;

    if (runtime_host_errors == NULL)
        return;

    g_hash_table_iter_init (&iter, runtime_host_errors);
    while (g_hash_table_iter_next (&iter, NULL, &value))
    {
        runtime_host_error_t *error = (runtime_host_error_t *) value;

        total_errors++;
        if (error->phase == MC_RUNTIME_ERROR_PHASE_STARTUP)
            startup_errors++;
    }

    if (startup_errors == 0)
    {
        g_hash_table_iter_init (&iter, runtime_host_errors);
        while (g_hash_table_iter_next (&iter, NULL, &value))
            runtime_host_report_status ((runtime_host_error_t *) value);
        return;
    }

    text = g_string_new (
        "Runtime errors were reported while loading extensions. See the log for full tracebacks.");
    g_string_append_printf (text, "\n\nAffected packages: %u", startup_errors);
    if (total_errors > startup_errors)
        g_string_append_printf (text, "\nOther runtime errors: %u", total_errors - startup_errors);

    (void) runtime_host_ui_message (_ ("Runtime errors"), text->str);
    g_string_free (text, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

void
runtime_host_clear_errors (void)
{
    if (runtime_host_errors != NULL)
    {
        g_hash_table_destroy (runtime_host_errors);
        runtime_host_errors = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
runtime_host_services_init (void)
{
    static mc_runtime_host_services_v1_t services = {
        .abi_version = MC_RUNTIME_PLUGIN_ABI_VERSION,
        .struct_size = sizeof (mc_runtime_host_services_v1_t),
        .ui_status = runtime_host_ui_status,
        .ui_message = runtime_host_ui_message,
        .log = runtime_host_log,
        .panel_active = runtime_host_panel_active,
        .panel_passive = runtime_host_panel_passive,
        .panel_cwd = runtime_host_panel_cwd,
        .panel_current = runtime_host_panel_current,
        .panel_selected = runtime_host_panel_selected,
        .panel_refresh = runtime_host_panel_refresh,
        .panel_chdir = runtime_host_panel_chdir,
        .editor_current = runtime_host_editor_current,
        .editor_path = runtime_host_editor_path,
        .editor_cursor = runtime_host_editor_cursor,
        .editor_set_cursor = runtime_host_editor_set_cursor,
        .editor_is_readonly = runtime_host_editor_is_readonly,
        .editor_get_text = runtime_host_editor_get_text,
        .editor_insert = runtime_host_editor_insert,
        .editor_save = runtime_host_editor_save,
        .viewer_current = runtime_host_viewer_current,
        .viewer_path = runtime_host_viewer_path,
        .viewer_position = runtime_host_viewer_position,
        .viewer_goto = runtime_host_viewer_goto,
        .viewer_mode = runtime_host_viewer_mode,
        .string_free = runtime_host_string_free,
        .file_snapshot_free = runtime_host_file_snapshot_free,
        .file_list_free = runtime_host_file_list_free,
        .editor_selected_text = runtime_host_editor_selected_text,
        .runtime_error = runtime_host_runtime_error,
        .ui_dialog = runtime_host_ui_dialog,
        .dialog_result_free = runtime_host_dialog_result_free,
        .editor_info = runtime_host_editor_info,
        .editor_info_free = runtime_host_editor_info_free,
        .editor_selection = runtime_host_editor_selection,
        .editor_selection_free = runtime_host_editor_selection_free,
        .editor_replace_selection = runtime_host_editor_replace_selection,
        .editor_replace = runtime_host_editor_replace,
        .process_run_shell = runtime_host_process_run_shell,
        .process_result_free = runtime_host_process_result_free,
        .ui_refresh = runtime_host_ui_refresh,
        .editor_tab_width = runtime_host_editor_tab_width,
        .editor_text = runtime_host_editor_text,
        .editor_edit = runtime_host_editor_edit,
        .editor_replace_selection_v2 = runtime_host_editor_replace_selection_v2,
        .ui_text_width = runtime_host_ui_text_width,
        .panel_provider_register = runtime_panel_provider_register,
        .panel_provider_unregister = runtime_panel_provider_unregister,
        .viewer_controller_open = runtime_viewer_controller_open,
    };

    /* Capabilities describe what this invocation can actually open, rather
       than merely which functions happened to be linked into mc. */
    if (mc_global.mc_run_mode != MC_RUN_FULL)
    {
        services.panel_active = NULL;
        services.panel_passive = NULL;
        services.panel_cwd = NULL;
        services.panel_current = NULL;
        services.panel_selected = NULL;
        services.panel_refresh = NULL;
        services.panel_chdir = NULL;
        services.panel_provider_register = NULL;
        services.panel_provider_unregister = NULL;
        services.viewer_controller_open = NULL;
    }

#ifdef USE_INTERNAL_EDIT
    if (mc_global.mc_run_mode != MC_RUN_FULL && mc_global.mc_run_mode != MC_RUN_EDITOR)
#else
    if (TRUE)
#endif
    {
        services.editor_current = NULL;
        services.editor_path = NULL;
        services.editor_cursor = NULL;
        services.editor_set_cursor = NULL;
        services.editor_is_readonly = NULL;
        services.editor_get_text = NULL;
        services.editor_insert = NULL;
        services.editor_save = NULL;
        services.editor_selected_text = NULL;
        services.editor_info = NULL;
        services.editor_info_free = NULL;
        services.editor_selection = NULL;
        services.editor_selection_free = NULL;
        services.editor_replace_selection = NULL;
        services.editor_replace = NULL;
        services.editor_text = NULL;
        services.editor_edit = NULL;
        services.editor_replace_selection_v2 = NULL;
    }

    if (mc_global.mc_run_mode != MC_RUN_FULL && mc_global.mc_run_mode != MC_RUN_VIEWER)
    {
        services.viewer_current = NULL;
        services.viewer_path = NULL;
        services.viewer_position = NULL;
        services.viewer_goto = NULL;
        services.viewer_mode = NULL;
    }

    mc_runtime_plugins_set_host_services (&services);
}

/* --------------------------------------------------------------------------------------------- */

void
runtime_host_set_current_editor (WEdit *edit)
{
    runtime_host_current_editor = edit;
}

/* --------------------------------------------------------------------------------------------- */

void
runtime_host_clear_current_editor (WEdit *edit)
{
    if (runtime_host_current_editor == edit)
        runtime_host_current_editor = NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
runtime_host_set_current_viewer (WView *view)
{
    runtime_host_current_viewer = view;
}

/* --------------------------------------------------------------------------------------------- */

void
runtime_host_clear_current_viewer (WView *view)
{
    if (runtime_host_current_viewer == view)
        runtime_host_current_viewer = NULL;
}

/* --------------------------------------------------------------------------------------------- */
