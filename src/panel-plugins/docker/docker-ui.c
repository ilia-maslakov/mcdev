/*
   Docker panel UI helpers.

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

#include <config.h>

#include <string.h>
#include <unistd.h>

#include "src/panel-plugins/docker/docker-internal.h"
#include "src/viewer/mcviewer.h"

static void docker_ui_append_nroff_bold (GString *buf, const char *text);
static void docker_ui_append_preview_line (GString *buf, const char *label, const char *value);
static void docker_ui_append_preview_block (GString *buf, const char *title, const char *text);
static void docker_ui_append_preview_inline_block (GString *buf, const char *title,
                                                   const char *text);
static gboolean docker_ui_view_nroff_preview (const char *contents);

static void
docker_ui_append_nroff_bold (GString *buf, const char *text)
{
    const unsigned char *p;

    if (text == NULL || *text == '\0')
        return;

    for (p = (const unsigned char *) text; *p != '\0'; p++)
    {
        if (*p == ' ' || *p == '\t')
            g_string_append_c (buf, (char) *p);
        else
            g_string_append_printf (buf, "%c\b%c", (char) *p, (char) *p);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_ui_append_preview_line (GString *buf, const char *label, const char *value)
{
    docker_ui_append_nroff_bold (buf, label);
    g_string_append (buf, ": ");
    g_string_append (buf, (value != NULL && *value != '\0') ? value : "-");
    g_string_append_c (buf, '\n');
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_ui_append_preview_block (GString *buf, const char *title, const char *text)
{
    if (text == NULL || *text == '\0')
        return;

    docker_ui_append_nroff_bold (buf, title);
    g_string_append (buf, ":\n");
    g_string_append (buf, text);
    if (text[strlen (text) - 1] != '\n')
        g_string_append_c (buf, '\n');
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_ui_append_preview_inline_block (GString *buf, const char *title, const char *text)
{
    const char *p;

    if (text == NULL || *text == '\0')
        return;

    docker_ui_append_nroff_bold (buf, title);
    g_string_append (buf, ": ");

    for (p = text; *p != '\0'; p++)
        g_string_append_c (buf, *p == '\n' ? ' ' : *p);

    g_string_append_c (buf, '\n');
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
docker_ui_view_nroff_preview (const char *contents)
{
    char *tmp_path = NULL;
    gboolean old_nroff;

    if (!write_temp_content ("mc-docker-preview-XXXXXX", contents, &tmp_path))
        return FALSE;

    {
        vfs_path_t *tmp_vpath = vfs_path_from_str (tmp_path);

        old_nroff = mcview_global_flags.nroff;
        mcview_global_flags.nroff = TRUE;
        (void) mcview_viewer (NULL, tmp_vpath, 0, 0, 0);
        mcview_global_flags.nroff = old_nroff;
        vfs_path_free (tmp_vpath, TRUE);
    }

    unlink (tmp_path);
    g_free (tmp_path);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_ui_view_container_summary (const docker_container_summary_t *summary)
{
    GString *preview;
    gboolean ok;

    if (summary == NULL)
        return FALSE;

    preview = g_string_sized_new (1024);
    docker_ui_append_nroff_bold (preview, "Container Summary");
    g_string_append (preview, "\n\n");

    docker_ui_append_preview_line (preview, "Name", summary->name);
    docker_ui_append_preview_line (preview, "ID", summary->id);
    docker_ui_append_preview_line (preview, "Image", summary->image);
    g_string_append_c (preview, '\n');

    docker_ui_append_preview_line (preview, "Status", summary->status);
    docker_ui_append_preview_line (preview, "Started", summary->started);
    docker_ui_append_preview_line (preview, "Restart count", summary->restart_count);
    g_string_append_c (preview, '\n');

    docker_ui_append_preview_line (preview, "IP", summary->ip);
    docker_ui_append_preview_inline_block (preview, "Ports", summary->ports);
    g_string_append_c (preview, '\n');

    docker_ui_append_preview_line (preview, "Memory limit", summary->memory_limit);
    docker_ui_append_preview_line (preview, "CPU shares", summary->cpu_shares);
    g_string_append_c (preview, '\n');

    docker_ui_append_preview_line (preview, "Entrypoint", summary->entrypoint);
    g_string_append_c (preview, '\n');

    docker_ui_append_preview_line (preview, "Command", summary->command);
    g_string_append_c (preview, '\n');

    docker_ui_append_preview_block (preview, "Mounts", summary->mounts);

    if (summary->stats != NULL && *summary->stats != '\0')
    {
        g_string_append_c (preview, '\n');
        docker_ui_append_nroff_bold (preview, "Stats");
        g_string_append (preview, "\n\n");
        g_string_append (preview, summary->stats);
        g_string_append_c (preview, '\n');
    }

    ok = docker_ui_view_nroff_preview (preview->str);
    g_string_free (preview, TRUE);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_ui_view_volume_summary (const docker_volume_summary_t *summary)
{
    GString *preview;
    gboolean ok;

    if (summary == NULL)
        return FALSE;

    preview = g_string_sized_new (512);
    docker_ui_append_nroff_bold (preview, "Volume Summary");
    g_string_append (preview, "\n\n");

    docker_ui_append_preview_line (preview, "Name", summary->name);
    docker_ui_append_preview_line (preview, "Scope", summary->scope);
    docker_ui_append_preview_line (preview, "Mountpoint", summary->mountpoint);
    docker_ui_append_preview_line (preview, "Created", summary->created);
    docker_ui_append_preview_line (preview, "State", summary->state);
    g_string_append_c (preview, '\n');
    docker_ui_append_preview_block (preview, "Labels", summary->labels);
    g_string_append_c (preview, '\n');
    docker_ui_append_preview_block (preview, "Options", summary->options);

    ok = docker_ui_view_nroff_preview (preview->str);
    g_string_free (preview, TRUE);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_ui_viewer_command (const char *cmd)
{
    if (cmd == NULL || *cmd == '\0')
        return FALSE;

    (void) mcview_viewer (cmd, NULL, 0, 0, 0);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_ui_show_create_container_dialog (char **image, char **name, char **command, gboolean *detach)
{
    /* clang-format off */
    quick_widget_t quick_widgets[] = {
        QUICK_LABELED_INPUT (N_("Image:"), input_label_above,
                            *image != NULL ? *image : "", "docker-create-image",
                            image, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_SEPARATOR (FALSE),
        QUICK_LABELED_INPUT (N_("Container name:"), input_label_above,
                            *name != NULL ? *name : "", "docker-create-name",
                            name, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_SEPARATOR (FALSE),
        QUICK_LABELED_INPUT (N_("Command:"), input_label_above,
                            *command != NULL ? *command : "", "docker-create-cmd",
                            command, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_SEPARATOR (FALSE),
        QUICK_CHECKBOX (N_("Run in &detached mode (-d)"), detach, NULL),
        QUICK_BUTTONS_OK_CANCEL,
        QUICK_END,
    };
    /* clang-format on */

    WRect r = { -1, -1, 0, 56 };

    quick_dialog_t qdlg = {
        .rect = r,
        .title = N_ ("Create Docker Container"),
        .help = "[Docker Plugin]",
        .widgets = quick_widgets,
        .callback = NULL,
        .mouse_callback = NULL,
    };

    return (quick_dialog (&qdlg) == B_ENTER);
}
