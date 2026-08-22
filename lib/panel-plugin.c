/*
   Panel plugin registry.

   Copyright (C) 2025
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

/** \file panel-plugin.c
 *  \brief Source: panel plugin registry
 *
 *  Maintains a list of registered mc_panel_plugin_t descriptors.
 *  Plugins are registered via mc_panel_plugin_add() - typically called
 *  from the dynamic loader (panel-plugin-loader.c).
 */

#include <config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/editor-plugin.h"
#include "lib/panel-plugin.h"
#include "lib/plugin-prefs.h"

#include "src/filemanager/dir.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

static GSList *panel_plugin_registry = NULL;
static GSList *editor_plugin_registry = NULL;

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_pp_dir_list_grow (dir_list *list, int delta)
{
    int size;

    if (list == NULL || delta == 0)
        return (list != NULL);

    size = list->size + delta;
    if (size <= 0)
        size = 128;

    if (size != list->size)
    {
        file_entry_t *fe;

        fe = g_try_renew (file_entry_t, list->list, size);
        if (fe == NULL)
            return FALSE;

        list->list = fe;
        list->size = size;
    }

    list->len = MIN (list->len, list->size);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_pp_dir_list_append (dir_list *list, const char *fname, const struct stat *st)
{
    file_entry_t *fentry;

    if (list->len == list->size && !mc_pp_dir_list_grow (list, 128))
        return FALSE;

    fentry = &list->list[list->len];
    fentry->fname = g_string_new (fname);
    fentry->f.marked = 0;
    fentry->f.link_to_dir = S_ISDIR (st->st_mode) ? 1 : 0;
    fentry->f.stale_link = 0;
    fentry->f.dir_size_computed = 0;
    fentry->st = *st;
    fentry->name_sort_key = NULL;
    fentry->extension_sort_key = NULL;

    list->len++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean pp_quiet_messages = FALSE;

gboolean
mc_pp_set_quiet_messages (gboolean quiet)
{
    gboolean prev = pp_quiet_messages;

    pp_quiet_messages = quiet;
    return prev;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_pp_quiet_messages (void)
{
    return pp_quiet_messages;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_pp_input_stream_free (mc_pp_input_stream_t *stream)
{
    if (stream != NULL && stream->ops != NULL && stream->ops->free != NULL)
        stream->ops->free (stream);
}

/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    mc_pp_input_stream_t base;
    char *path;
    gboolean own_file;
} mc_pp_file_stream_t;

static mc_pp_result_t
mc_pp_file_stream_open (mc_pp_input_stream_t *stream, void **handle, GError **error)
{
    mc_pp_file_stream_t *source = (mc_pp_file_stream_t *) stream;
    int fd;

    *handle = NULL;

    fd = open (source->path, O_RDONLY);
    if (fd == -1)
    {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s: %s", source->path,
                     g_strerror (errno));
        return MC_PPR_FAILED;
    }

    *handle = GINT_TO_POINTER (fd);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static gssize
mc_pp_file_stream_read (mc_pp_input_stream_t *stream, void *handle, void *buf, gsize size,
                        GError **error)
{
    ssize_t bytes;

    (void) stream;

    do
        bytes = read (GPOINTER_TO_INT (handle), buf, size);
    while (bytes < 0 && errno == EINTR);

    if (bytes < 0)
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s",
                     g_strerror (errno));

    return (gssize) bytes;
}

/* --------------------------------------------------------------------------------------------- */

static gint64
mc_pp_file_stream_seek (mc_pp_input_stream_t *stream, void *handle, gint64 offset, int whence,
                        GError **error)
{
    off_t position;

    (void) stream;

    position = lseek (GPOINTER_TO_INT (handle), (off_t) offset, whence);
    if (position == (off_t) -1)
    {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s",
                     g_strerror (errno));
        return -1;
    }

    return (gint64) position;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_pp_file_stream_close (mc_pp_input_stream_t *stream, void *handle)
{
    (void) stream;

    close (GPOINTER_TO_INT (handle));
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_pp_file_stream_free (mc_pp_input_stream_t *stream)
{
    mc_pp_file_stream_t *source = (mc_pp_file_stream_t *) stream;

    if (source->own_file)
        unlink (source->path);
    g_free (source->path);
    g_free (source);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_pp_input_stream_ops_t mc_pp_file_stream_ops = {
    .open = mc_pp_file_stream_open,
    .read = mc_pp_file_stream_read,
    .seek = mc_pp_file_stream_seek,
    .close = mc_pp_file_stream_close,
    .free = mc_pp_file_stream_free,
};

/* --------------------------------------------------------------------------------------------- */

mc_pp_input_stream_t *
mc_pp_input_stream_new_for_file (const char *path, gboolean own_file)
{
    mc_pp_file_stream_t *source;

    if (path == NULL)
        return NULL;

    source = g_new0 (mc_pp_file_stream_t, 1);
    source->base.ops = &mc_pp_file_stream_ops;
    source->path = g_strdup (path);
    source->own_file = own_file;

    return &source->base;
}

/* --------------------------------------------------------------------------------------------- */

const char *
mc_pp_input_stream_local_path (const mc_pp_input_stream_t *stream, gboolean *is_temporary)
{
    const mc_pp_file_stream_t *source = (const mc_pp_file_stream_t *) stream;

    if (stream == NULL || stream->ops != &mc_pp_file_stream_ops)
        return NULL;

    if (is_temporary != NULL)
        *is_temporary = source->own_file;

    return source->path;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_pp_write_temp_file (const char *tmpl, const void *data, gssize len, char **local_path)
{
    GError *error = NULL;
    const char *buf = (const char *) data;
    size_t left;
    int fd;

    if (tmpl == NULL || local_path == NULL)
        return FALSE;

    *local_path = NULL;

    fd = g_file_open_tmp (tmpl, local_path, &error);
    if (fd == -1)
    {
        if (error != NULL)
            g_error_free (error);
        return FALSE;
    }

    if (buf == NULL)
        buf = "";
    left = (len < 0) ? strlen (buf) : (size_t) len;

    while (left > 0)
    {
        ssize_t written = write (fd, buf, left);

        if (written <= 0)
        {
            if (written == -1 && errno == EINTR)
                continue;

            break;
        }

        buf += written;
        left -= (size_t) written;
    }

    /* A network file system reports a write error only at close(). */
    if (close (fd) != 0 || left > 0)
    {
        unlink (*local_path);
        g_free (*local_path);
        *local_path = NULL;
        return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_pp_rename_with_ext (char **local_path, const char *fname)
{
    const char *slash;
    const char *base;
    const char *ext;
    char *ext_path;

    if (local_path == NULL || *local_path == NULL || fname == NULL)
        return;

    slash = strrchr (fname, '/');
    base = (slash != NULL) ? slash + 1 : fname;
    ext = strrchr (base, '.');
    if (ext == NULL)
        return;

    ext_path = g_strconcat (*local_path, ext, NULL);
    if (rename (*local_path, ext_path) == 0)
    {
        g_free (*local_path);
        *local_path = ext_path;
    }
    else
        g_free (ext_path);
}

/* --------------------------------------------------------------------------------------------- */

void
mc_pp_add_entry (void *list, const char *name, mode_t mode, off_t size, time_t mtime)
{
    struct stat st;

    memset (&st, 0, sizeof (st));
    st.st_mode = mode;
    st.st_size = size;
    st.st_mtime = mtime;
    st.st_uid = getuid ();
    st.st_gid = getgid ();
    st.st_nlink = 1;

    (void) mc_pp_dir_list_append ((dir_list *) list, name, &st);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_panel_plugin_add (const mc_panel_plugin_t *plugin)
{
    if (plugin == NULL)
        return FALSE;

    if (plugin->api_version != MC_PANEL_PLUGIN_API_VERSION)
    {
        fprintf (stderr, "Panel plugin \"%s\": API version %d, expected %d\n",
                 plugin->name != NULL ? plugin->name : "(null)", plugin->api_version,
                 MC_PANEL_PLUGIN_API_VERSION);
        return FALSE;
    }

    if (plugin->name == NULL || (plugin->open == NULL && plugin->open_with_plugin == NULL)
        || plugin->close == NULL || plugin->get_items == NULL)
    {
        fprintf (stderr, "Panel plugin \"%s\": missing required callbacks\n",
                 plugin->name != NULL ? plugin->name : "(null)");
        return FALSE;
    }

    /* User opted to disable this plugin via Manage Plugins. */
    if (mc_plugin_prefs_is_disabled (MC_PLUGIN_KIND_PANEL, plugin->name))
        return FALSE;

    /* Manage Plugins may register already-loaded plugins again. */
    if (mc_panel_plugin_find_by_name (plugin->name) != NULL)
        return FALSE;

    panel_plugin_registry = g_slist_append (panel_plugin_registry, (gpointer) plugin);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_panel_plugin_remove (const mc_panel_plugin_t *plugin)
{
    GSList *link;

    if (plugin == NULL)
        return FALSE;
    link = g_slist_find (panel_plugin_registry, plugin);
    if (link == NULL)
        return FALSE;
    panel_plugin_registry = g_slist_delete_link (panel_plugin_registry, link);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

const GSList *
mc_panel_plugin_list (void)
{
    return panel_plugin_registry;
}

/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *
mc_panel_plugin_find_by_name (const char *name)
{
    const GSList *iter;

    if (name == NULL)
        return NULL;

    for (iter = panel_plugin_registry; iter != NULL; iter = g_slist_next (iter))
    {
        const mc_panel_plugin_t *p = (const mc_panel_plugin_t *) iter->data;

        if (strcmp (p->name, name) == 0)
            return p;
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *
mc_panel_plugin_find_by_prefix (const char *prefix)
{
    const GSList *iter;

    if (prefix == NULL)
        return NULL;

    for (iter = panel_plugin_registry; iter != NULL; iter = g_slist_next (iter))
    {
        const mc_panel_plugin_t *p = (const mc_panel_plugin_t *) iter->data;

        if (p->prefix != NULL && strcmp (p->prefix, prefix) == 0)
            return p;
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_panel_plugins_shutdown (void)
{
    g_slist_free (panel_plugin_registry);
    panel_plugin_registry = NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_editor_plugin_add (const mc_editor_plugin_t *plugin)
{
    if (plugin == NULL)
        return FALSE;

    if (plugin->api_version != MC_EDITOR_PLUGIN_API_VERSION)
    {
        fprintf (stderr, "Editor plugin \"%s\": API version %d, expected %d\n",
                 plugin->name != NULL ? plugin->name : "(null)", plugin->api_version,
                 MC_EDITOR_PLUGIN_API_VERSION);
        return FALSE;
    }

    if (plugin->name == NULL || plugin->open == NULL || plugin->close == NULL)
    {
        fprintf (stderr, "Editor plugin \"%s\": missing required callbacks\n",
                 plugin->name != NULL ? plugin->name : "(null)");
        return FALSE;
    }

    /* User opted to disable this plugin via Manage Plugins. */
    if (mc_plugin_prefs_is_disabled (MC_PLUGIN_KIND_EDITOR, plugin->name))
        return FALSE;

    // See the panel-plugin counterpart: this is the idempotent path used
    // by Manage Plugins, must not write to stderr.
    if (mc_editor_plugin_find_by_name (plugin->name) != NULL)
        return FALSE;

    editor_plugin_registry = g_slist_append (editor_plugin_registry, (gpointer) plugin);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

const GSList *
mc_editor_plugin_list (void)
{
    return editor_plugin_registry;
}

/* --------------------------------------------------------------------------------------------- */

const mc_editor_plugin_t *
mc_editor_plugin_find_by_name (const char *name)
{
    const GSList *iter;

    if (name == NULL)
        return NULL;

    for (iter = editor_plugin_registry; iter != NULL; iter = g_slist_next (iter))
    {
        const mc_editor_plugin_t *p = (const mc_editor_plugin_t *) iter->data;

        if (strcmp (p->name, name) == 0)
            return p;
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_editor_plugins_shutdown (void)
{
    g_slist_free (editor_plugin_registry);
    editor_plugin_registry = NULL;
}

/* --------------------------------------------------------------------------------------------- */
