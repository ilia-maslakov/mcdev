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

#include "lib/file-entry.h"
#include "lib/global.h"

#include "lib/extension-runtime.h"
#include "lib/runtime-events.h"
#include "lib/vfs/vfs.h"
#include "lib/widget.h"

#include "editor/edit-impl.h"
#include "editor/editwidget.h"
#include "events_init.h"
#include "filemanager/filemanager.h"
#include "filemanager/layout.h"
#include "viewer/internal.h"

#include "runtime-host.h"

/* --------------------------------------------------------------------------------------------- */

static WEdit *runtime_host_current_editor = NULL;
static WView *runtime_host_current_viewer = NULL;

/* --------------------------------------------------------------------------------------------- */

static gboolean
runtime_host_set_error (const char **error, const char *message)
{
    if (error != NULL)
        *error = message;
    return FALSE;
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
    *column = (guint64) MAX (editor->curs_col, 0) + 1;
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
    editor->force |= REDRAW_PAGE;
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

    *readonly = (editor->stat1.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0;
    return TRUE;
#else
    (void) handle;
    (void) readonly;
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

static gboolean
runtime_host_editor_insert (const mc_runtime_handle_t *handle, const char *text, const char **error)
{
#ifdef USE_INTERNAL_EDIT
    WEdit *editor = runtime_host_editor_resolve (handle, error);
    const char *cursor;

    if (editor == NULL)
        return FALSE;
    if (text == NULL)
        return runtime_host_set_error (error, "invalid_argument");
    if ((editor->stat1.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0)
        return runtime_host_set_error (error, "readonly");

    for (cursor = text; *cursor != '\0'; cursor++)
        edit_insert (editor, (unsigned char) *cursor);
    editor->force |= REDRAW_PAGE;
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
    if ((editor->stat1.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0)
        return runtime_host_set_error (error, "readonly");
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

static void
runtime_host_log (const char *source, const char *level, const char *message)
{
    fprintf (stderr, "%s %s: %s\n", source != NULL ? source : "runtime",
             level != NULL ? level : "info", message != NULL ? message : "");
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
