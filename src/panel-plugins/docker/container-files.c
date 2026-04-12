/*
   Docker container file and mount domain logic.

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

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "src/panel-plugins/docker/docker-internal.h"

static char *files_cache_key (const char *container_id, const char *cwd);
static gboolean files_cache_has_container (const docker_data_t *data, const char *container_id);
static GPtrArray *files_cache_lookup (const docker_data_t *data, const char *container_id,
                                      const char *cwd);
static GPtrArray *files_cache_get_or_create_dir (docker_data_t *data, const char *container_id,
                                                 const char *cwd);
static void files_cache_add_dir_item (docker_data_t *data, const char *container_id,
                                      const char *cwd, const char *name, gboolean is_dir,
                                      gboolean is_link, off_t size, const char *link_target);
static void files_cache_add_entry_path (docker_data_t *data, const char *container_id,
                                        const char *entry_path, char typeflag, guint64 size,
                                        const char *linkname);
static void mount_item_set_local_type (docker_item_t *item, const char *source, const char *kind);
static char *mount_item_build_display_name (const char *dest, const char *source, gboolean is_dir);
static void docker_files_status_init_cb (status_msg_t *sm);
static int docker_files_status_update_cb (status_msg_t *sm);
static gboolean cp_stream_extract_single_file (int stream_fd, int out_fd, char **err_text);
static gboolean load_container_listing_from_exec (docker_data_t *data, const char *container_id);
static gboolean load_container_tree_from_tar (docker_data_t *data, const char *container_id,
                                              char **err_text);
static void add_mount_item_unique (docker_data_t *data, GHashTable *seen, const char *source,
                                   const char *dest, const char *kind);
static gboolean split_bind_spec (const char *spec, char **source, char **dest);
static void load_mounts_from_output (docker_data_t *data, GHashTable *seen, const char *output);
static void load_mounts_from_binds_output (docker_data_t *data, GHashTable *seen,
                                           const char *output);

static char *
files_cache_key (const char *container_id, const char *cwd)
{
    return g_strdup_printf ("%s\t%s", container_id, cwd);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
files_cache_has_container (const docker_data_t *data, const char *container_id)
{
    char *key;
    gboolean found;

    if (data->files_cache == NULL)
        return FALSE;

    key = files_cache_key (container_id, "/");
    found = g_hash_table_contains (data->files_cache, key);
    g_free (key);

    return found;
}

/* --------------------------------------------------------------------------------------------- */

static GPtrArray *
files_cache_lookup (const docker_data_t *data, const char *container_id, const char *cwd)
{
    char *key;
    GPtrArray *cached;

    if (data->files_cache == NULL)
        return NULL;

    key = files_cache_key (container_id, cwd);
    cached = (GPtrArray *) g_hash_table_lookup (data->files_cache, key);
    g_free (key);

    if (cached == NULL)
        return NULL;

    return docker_items_clone (cached);
}

/* --------------------------------------------------------------------------------------------- */

static GPtrArray *
files_cache_get_or_create_dir (docker_data_t *data, const char *container_id, const char *cwd)
{
    char *key;
    GPtrArray *items;

    if (data->files_cache == NULL)
        return NULL;

    key = files_cache_key (container_id, cwd);
    items = (GPtrArray *) g_hash_table_lookup (data->files_cache, key);
    if (items == NULL)
    {
        items = g_ptr_array_new_with_free_func (docker_item_free);
        g_hash_table_replace (data->files_cache, key, items);
        return items;
    }

    g_free (key);
    return items;
}

/* --------------------------------------------------------------------------------------------- */

static void
files_cache_add_dir_item (docker_data_t *data, const char *container_id, const char *cwd,
                          const char *name, gboolean is_dir, gboolean is_link, off_t size,
                          const char *link_target)
{
    GPtrArray *items;
    guint i;

    items = files_cache_get_or_create_dir (data, container_id, cwd);
    if (items == NULL)
        return;

    for (i = 0; i < items->len; i++)
    {
        docker_item_t *item = (docker_item_t *) g_ptr_array_index (items, i);

        if (strcmp (item->name, name) == 0)
        {
            item->is_dir = is_dir;
            item->is_link = is_link;
            if (size != 0 || (!is_dir && !is_link))
                item->size = size;
            g_free (item->link_target);
            item->link_target = g_strdup (link_target);
            return;
        }
    }

    {
        docker_item_t *item = g_new0 (docker_item_t, 1);

        item->name = g_strdup (name);
        item->id = g_strdup (name);
        item->is_dir = is_dir;
        item->is_link = is_link;
        item->size = size;
        item->link_target = g_strdup (link_target);
        g_ptr_array_add (items, item);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
files_cache_add_entry_path (docker_data_t *data, const char *container_id, const char *entry_path,
                            char typeflag, guint64 size, const char *linkname)
{
    char *normalized;
    char **parts;
    int n_parts = 0;
    char *dir_path = NULL;
    int i;
    gboolean is_dir;
    gboolean is_link;

    normalized = g_strdup (entry_path);
    if (normalized[0] == '.' && normalized[1] == '/')
        memmove (normalized, normalized + 2, strlen (normalized + 2) + 1);
    else if (strcmp (normalized, ".") == 0)
    {
        g_free (normalized);
        return;
    }

    while (normalized[0] != '\0' && normalized[strlen (normalized) - 1] == '/')
        normalized[strlen (normalized) - 1] = '\0';

    if (normalized[0] == '\0')
    {
        g_free (normalized);
        return;
    }

    parts = g_strsplit (normalized, "/", -1);
    while (parts[n_parts] != NULL)
        n_parts++;

    if (n_parts == 0)
    {
        g_strfreev (parts);
        g_free (normalized);
        return;
    }

    files_cache_get_or_create_dir (data, container_id, "/");

    dir_path = g_strdup ("/");
    for (i = 0; i < n_parts - 1; i++)
    {
        files_cache_add_dir_item (data, container_id, dir_path, parts[i], TRUE, FALSE, 0, NULL);

        {
            char *next_dir = mc_pp_join_path (dir_path, parts[i]);

            g_free (dir_path);
            dir_path = next_dir;
        }

        files_cache_get_or_create_dir (data, container_id, dir_path);
    }

    is_dir = (typeflag == '5');
    is_link = (typeflag == '2');
    files_cache_add_dir_item (data, container_id, dir_path, parts[n_parts - 1], is_dir, is_link,
                              (off_t) size, linkname);

    if (is_dir)
    {
        char *subdir = mc_pp_join_path (dir_path, parts[n_parts - 1]);

        files_cache_get_or_create_dir (data, container_id, subdir);
        g_free (subdir);
    }

    g_free (dir_path);
    g_strfreev (parts);
    g_free (normalized);
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_files_status_init_cb (status_msg_t *sm)
{
    simple_status_msg_t *ssm = SIMPLE_STATUS_MSG (sm);
    Widget *wd = WIDGET (sm->dlg);
    WGroup *wg = GROUP (sm->dlg);
    WRect r;

    const char *b_name = _ ("&Abort");
    const char *label_text = _ ("Reading directory listing...");
    int b_width;
    int label_width;
    int wd_width, y;
    Widget *b;

    b_width = str_term_width1 (b_name) + 4;
    label_width = str_term_width1 (label_text);
    wd_width = MAX (label_width + 6, b_width + 6);

    y = 2;
    ssm->label = label_new (y++, 3, label_text);
    group_add_widget (wg, ssm->label);
    group_add_widget (wg, hline_new (y++, -1, -1));
    b = WIDGET (button_new (y++, 3, B_CANCEL, NORMAL_BUTTON, b_name, NULL));
    group_add_widget_autopos (wg, b, WPOS_KEEP_TOP | WPOS_CENTER_HORZ, NULL);

    r = wd->rect;
    r.lines = y + 2;
    r.cols = wd_width;
    widget_set_size_rect (wd, &r);
}

/* --------------------------------------------------------------------------------------------- */

static int
docker_files_status_update_cb (status_msg_t *sm)
{
    docker_files_status_msg_t *fsm = (docker_files_status_msg_t *) sm;
    simple_status_msg_t *ssm = SIMPLE_STATUS_MSG (sm);

    label_set_text (ssm->label, fsm->text != NULL ? fsm->text : _ ("Please wait..."));
    return status_msg_common_update (sm);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
cp_stream_extract_single_file (int stream_fd, int out_fd, char **err_text)
{
    while (TRUE)
    {
        struct docker_tar_header hdr;
        gboolean size_ok;
        guint64 size;
        guint64 padded;

        if (!docker_tar_read_full (stream_fd, &hdr, DOCKER_TAR_BLOCK_SIZE))
        {
            if (err_text != NULL)
                *err_text = g_strdup (_ ("Unexpected end of TAR stream"));
            return FALSE;
        }

        if (docker_tar_is_zero_block (&hdr))
        {
            if (err_text != NULL)
                *err_text = g_strdup (_ ("File not found in TAR stream"));
            return FALSE;
        }

        size = docker_tar_parse_octal (hdr.size, sizeof (hdr.size), &size_ok);
        if (!size_ok)
        {
            if (err_text != NULL)
                *err_text = g_strdup (_ ("Corrupt TAR header: invalid size field"));
            return FALSE;
        }
        padded = (size + DOCKER_TAR_BLOCK_SIZE - 1) & ~(guint64) (DOCKER_TAR_BLOCK_SIZE - 1);

        if (hdr.typeflag == '0' || hdr.typeflag == '\0')
        {
            guint64 remaining = size;

            while (remaining > 0)
            {
                char buf[8192];
                size_t to_read = (remaining > sizeof (buf)) ? sizeof (buf) : (size_t) remaining;
                ssize_t n;
                const char *p;
                size_t left;

                do
                {
                    n = read (stream_fd, buf, to_read);
                }
                while (n == -1 && errno == EINTR);

                if (n <= 0)
                {
                    if (err_text != NULL)
                        *err_text = g_strdup (_ ("Read error from TAR stream"));
                    return FALSE;
                }

                p = buf;
                left = (size_t) n;

                while (left > 0)
                {
                    ssize_t w;

                    do
                    {
                        w = write (out_fd, p, left);
                    }
                    while (w == -1 && errno == EINTR);

                    if (w <= 0)
                    {
                        if (err_text != NULL)
                            *err_text = g_strdup (_ ("Write error to temp file"));
                        return FALSE;
                    }
                    p += (size_t) w;
                    left -= (size_t) w;
                }

                remaining -= (guint64) n;
            }

            return TRUE;
        }

        /* Skip non-regular entries (directories, symlinks, etc.) */
        if (!docker_tar_skip (stream_fd, padded))
        {
            if (err_text != NULL)
                *err_text = g_strdup (_ ("Skip error in TAR stream"));
            return FALSE;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
load_container_listing_from_exec (docker_data_t *data, const char *container_id)
{
    char *quoted_id;
    char *docker_args;
    char *output = NULL;
    char *exec_err = NULL;
    char **lines;
    int i;
    gboolean ok;

    quoted_id = g_shell_quote (container_id);
    /* Prune virtual filesystems; keep regular mounts visible. */
    docker_args =
        g_strdup_printf ("exec %s sh -c "
                         "'find / \\( -path /proc -o -path /sys -o -path /dev \\) -prune -o"
                         " -printf \"%%y\\t%%s\\t%%l\\t%%P\\n\" 2>/dev/null'",
                         quoted_id);
    g_free (quoted_id);

    ok = docker_conn_run (data->active_conn, docker_args, &output, &exec_err);
    g_free (docker_args);
    g_free (exec_err);

    if (!ok || output == NULL || output[0] == '\0')
    {
        g_free (output);
        return FALSE;
    }

    /* Ensure the root directory entry exists even if find output is empty */
    files_cache_get_or_create_dir (data, container_id, "/");

    lines = g_strsplit (output, "\n", -1);
    g_free (output);

    for (i = 0; lines[i] != NULL; i++)
    {
        char **fields;
        char type_char;
        guint64 size;
        const char *linkname;
        const char *rel_path;
        char typeflag;

        if (lines[i][0] == '\0')
            continue;

        /* Format: type TAB size TAB linkname TAB path */
        fields = g_strsplit (lines[i], "\t", 4);
        if (fields[0] == NULL || fields[1] == NULL || fields[2] == NULL || fields[3] == NULL)
        {
            g_strfreev (fields);
            continue;
        }

        type_char = fields[0][0];
        size = g_ascii_strtoull (fields[1], NULL, 10);
        linkname = fields[2];
        rel_path = fields[3];

        if (rel_path[0] == '\0')
        {
            /* Starting point itself (root "/"): already created above */
            g_strfreev (fields);
            continue;
        }

        switch (type_char)
        {
        case 'd':
            typeflag = '5';
            break;
        case 'l':
            typeflag = '2';
            break;
        default:
            typeflag = '0';
            break;
        }

        files_cache_add_entry_path (data, container_id, rel_path, typeflag, size,
                                    linkname[0] != '\0' ? linkname : NULL);
        g_strfreev (fields);
    }

    g_strfreev (lines);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
load_container_tree_from_tar (docker_data_t *data, const char *container_id, char **err_text)
{
    docker_cp_stream_t stream;
    docker_files_status_msg_t status;
    char *pending_longname = NULL;
    char *pending_longlink = NULL;
    char *cp_stderr = NULL;

    memset (&status, 0, sizeof (status));
    memset (&stream, 0, sizeof (stream));
    stream.fd = -1;
    stream.errfd = -1;
    stream.child_pid = -1;

    {
        char *quoted_id = g_shell_quote (container_id);
        char *docker_args = g_strdup_printf ("cp %s:/. -", quoted_id);
        char *cmd = docker_conn_build_pipe_cmd (data->active_conn, docker_args);
        gboolean opened;

        g_free (quoted_id);
        g_free (docker_args);
        opened = docker_cp_stream_open (cmd, &stream, err_text);
        g_free (cmd);

        if (!opened)
            goto fail;
    }

    status.text = _ ("Reading directory listing...");
    status_msg_init (STATUS_MSG (&status), _ ("Docker"), 0.0, docker_files_status_init_cb,
                     docker_files_status_update_cb, NULL);
    docker_files_status_update_cb (STATUS_MSG (&status));
    mc_refresh ();

    {
        gboolean stream_ok = TRUE;

        while (stream_ok)
        {
            struct docker_tar_header hdr;
            char typeflag;
            guint64 size;
            guint64 padded_size;
            char *path;
            char *linkname;

            if (docker_files_status_update_cb (STATUS_MSG (&status)) == B_CANCEL)
            {
                if (err_text != NULL)
                    *err_text = g_strdup (_ ("Aborted"));
                stream_ok = FALSE;
                break;
            }

            if (!docker_tar_read_full (stream.fd, &hdr, DOCKER_TAR_BLOCK_SIZE))
            {
                stream_ok = FALSE;
                break;
            }

            if (docker_tar_is_zero_block (&hdr))
                break;

            typeflag = hdr.typeflag;
            {
                gboolean size_ok;

                size = docker_tar_parse_octal (hdr.size, sizeof (hdr.size), &size_ok);
                if (!size_ok)
                {
                    stream_ok = FALSE;
                    break;
                }
            }
            padded_size =
                (size + DOCKER_TAR_BLOCK_SIZE - 1) & ~(guint64) (DOCKER_TAR_BLOCK_SIZE - 1);

            if (typeflag == 'L')
            {
                g_free (pending_longname);
                pending_longname = docker_tar_read_longname (stream.fd, size);
                if (pending_longname == NULL)
                {
                    stream_ok = FALSE;
                    break;
                }
                continue;
            }

            if (typeflag == 'K')
            {
                g_free (pending_longlink);
                pending_longlink = docker_tar_read_longname (stream.fd, size);
                if (pending_longlink == NULL)
                {
                    stream_ok = FALSE;
                    break;
                }
                continue;
            }

            path = docker_tar_header_get_path (&hdr, pending_longname);
            linkname = docker_tar_header_get_linkname (&hdr, pending_longlink);

            g_free (pending_longname);
            pending_longname = NULL;
            g_free (pending_longlink);
            pending_longlink = NULL;

            files_cache_add_entry_path (data, container_id, path, typeflag, size, linkname);

            g_free (path);
            g_free (linkname);

            if (padded_size > 0 && !docker_tar_skip (stream.fd, padded_size))
            {
                stream_ok = FALSE;
                break;
            }
        }

        if (stream.fd >= 0)
        {
            close (stream.fd);
            stream.fd = -1;
        }

        cp_stderr = docker_cp_stream_read_stderr (&stream);
        docker_cp_stream_reap (&stream);

        if (!stream_ok)
        {
            if (err_text != NULL)
            {
                *err_text = (cp_stderr != NULL)
                    ? cp_stderr
                    : g_strdup (_ ("tar stream from docker cp was truncated"));
                cp_stderr = NULL;
            }
            else
                g_free (cp_stderr);
            goto fail;
        }

        if (cp_stderr != NULL)
        {
            if (!files_cache_has_container (data, container_id) && err_text != NULL)
            {
                *err_text = cp_stderr;
                cp_stderr = NULL;
                goto fail;
            }

            g_free (cp_stderr);
            cp_stderr = NULL;
        }
    }

    status_msg_deinit (STATUS_MSG (&status));

    files_cache_get_or_create_dir (data, container_id, "/");
    g_free (pending_longname);
    g_free (pending_longlink);

    return TRUE;

fail:
    if (status.status_msg.status_msg.dlg != NULL)
        status_msg_deinit (STATUS_MSG (&status));
    g_free (pending_longname);
    g_free (pending_longlink);
    if (stream.fd >= 0)
        close (stream.fd);
    docker_cp_stream_reap (&stream);
    if (cp_stderr == NULL)
        cp_stderr = docker_cp_stream_read_stderr (&stream);
    g_free (cp_stderr);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mount_item_build_display_name (const char *dest, const char *source, gboolean is_dir)
{
    if (is_dir && source != NULL && *source != '\0' && source[strlen (source) - 1] != '/')
        return g_strdup_printf ("%s -> %s/", dest, source);

    return g_strdup_printf ("%s -> %s", dest, source);
}

/* --------------------------------------------------------------------------------------------- */

static void
mount_item_set_local_type (docker_item_t *item, const char *source, const char *kind)
{
    struct stat st;
    struct stat target_st;

    if (item == NULL || source == NULL || *source == '\0')
        return;

    if (lstat (source, &st) != 0)
    {
        if (kind != NULL && strcmp (kind, "volume") == 0)
            item->is_dir = TRUE;
        return;
    }

    item->size = st.st_size;

    if (S_ISDIR (st.st_mode))
    {
        item->is_dir = TRUE;
        item->size = 0;
        return;
    }

    if (!S_ISLNK (st.st_mode))
        return;

    item->is_link = TRUE;

    if (stat (source, &target_st) != 0)
        return;

    if (S_ISDIR (target_st.st_mode))
    {
        item->is_dir = TRUE;
        item->size = 0;
    }
    else
        item->size = target_st.st_size;
}

/* --------------------------------------------------------------------------------------------- */

static void
add_mount_item_unique (docker_data_t *data, GHashTable *seen, const char *source, const char *dest,
                       const char *kind)
{
    char *key;
    docker_item_t *item;
    char *source_clean;
    char *dest_clean;

    source_clean = g_strstrip (g_strdup (source));
    dest_clean = g_strstrip (g_strdup (dest));

    key = g_strdup_printf ("%s\t%s", source_clean, dest_clean);
    if (g_hash_table_contains (seen, key))
    {
        g_free (source_clean);
        g_free (dest_clean);
        g_free (key);
        return;
    }

    g_hash_table_add (seen, key);

    item = g_new0 (docker_item_t, 1);
    item->id = g_strdup (source_clean);
    mount_item_set_local_type (item, source_clean, kind);
    item->name = mount_item_build_display_name (dest_clean, source_clean, item->is_dir);
    g_ptr_array_add (data->items, item);

    g_free (source_clean);
    g_free (dest_clean);
    (void) kind;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
split_bind_spec (const char *spec, char **source, char **dest)
{
    const char *sep;
    char *rest;
    char *last_colon;

    *source = NULL;
    *dest = NULL;

    if (spec == NULL || *spec == '\0')
        return FALSE;

    sep = strstr (spec, ":/");
    if (sep == NULL)
        return FALSE;

    *source = g_strndup (spec, (gsize) (sep - spec));
    rest = g_strdup (sep + 1);
    g_strstrip (rest);

    last_colon = strrchr (rest, ':');
    if (last_colon != NULL && strchr (last_colon + 1, '/') == NULL)
        *last_colon = '\0';

    *dest = g_strdup (rest);
    g_free (rest);

    if ((*source)[0] == '\0' || (*dest)[0] == '\0')
    {
        g_free (*source);
        g_free (*dest);
        *source = NULL;
        *dest = NULL;
        return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
load_mounts_from_output (docker_data_t *data, GHashTable *seen, const char *output)
{
    char **lines;
    int i;

    if (output == NULL || *output == '\0')
        return;

    lines = g_strsplit (output, "\n", -1);
    for (i = 0; lines[i] != NULL; i++)
    {
        char **parts;
        int part_count = 0;

        if (lines[i][0] == '\0')
            continue;

        parts = g_strsplit (lines[i], "\t", -1);
        while (parts[part_count] != NULL)
            part_count++;

        if (part_count >= 3)
            add_mount_item_unique (data, seen, parts[1], parts[2], parts[0]);

        g_strfreev (parts);
    }
    g_strfreev (lines);
}

/* --------------------------------------------------------------------------------------------- */

static void
load_mounts_from_binds_output (docker_data_t *data, GHashTable *seen, const char *output)
{
    char **lines;
    int i;

    if (output == NULL || *output == '\0')
        return;

    lines = g_strsplit (output, "\n", -1);
    for (i = 0; lines[i] != NULL; i++)
    {
        char *source;
        char *dest;

        if (lines[i][0] == '\0')
            continue;

        if (split_bind_spec (lines[i], &source, &dest))
        {
            add_mount_item_unique (data, seen, source, dest, "bind");
            g_free (source);
            g_free (dest);
        }
    }
    g_strfreev (lines);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_container_files_reload (docker_data_t *data, char **err_text)
{
    const char *cwd;

    if (data->current_container_id == NULL)
        return TRUE;

    cwd = (data->files_cwd != NULL) ? data->files_cwd : "/";
    if (!files_cache_has_container (data, data->current_container_id))
    {
        gboolean loaded = FALSE;

        /* SSH prefers a metadata-only listing and falls back to TAR. */
        if (data->active_conn != NULL && data->active_conn->type == DOCKER_CONN_SSH)
            loaded = load_container_listing_from_exec (data, data->current_container_id);

        if (!loaded && !load_container_tree_from_tar (data, data->current_container_id, err_text))
            return FALSE;
    }

    data->items = files_cache_lookup (data, data->current_container_id, cwd);
    if (data->items == NULL)
        data->items = g_ptr_array_new_with_free_func (docker_item_free);

    if (data->pending_focus == NULL && data->files_focus_cache != NULL)
    {
        char *focus_key = files_cache_key (data->current_container_id, cwd);
        const char *focus_name =
            (const char *) g_hash_table_lookup (data->files_focus_cache, focus_key);

        if (focus_name != NULL && focus_name[0] != '\0')
            data->pending_focus = g_strdup (focus_name);

        g_free (focus_key);
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_container_mounts_reload (docker_data_t *data, char **err_text)
{
    char *output = NULL;
    char *quoted_id;
    char *cmd;
    GHashTable *seen;

    if (data->current_container_id == NULL)
        return TRUE;

    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    quoted_id = g_shell_quote (data->current_container_id);

    cmd = g_strdup_printf ("inspect --format"
                           " '{{range .Mounts}}{{printf \"%%s\\t%%s\\t%%s\\n\" .Type .Source "
                           ".Destination}}{{end}}' %s",
                           quoted_id);

    if (!docker_conn_run (data->active_conn, cmd, &output, err_text))
    {
        g_free (cmd);
        g_free (quoted_id);
        g_hash_table_destroy (seen);
        g_free (output);
        return FALSE;
    }

    load_mounts_from_output (data, seen, output);
    g_free (cmd);
    g_free (output);
    output = NULL;

    if (data->items->len == 0)
    {
        cmd = g_strdup_printf ("inspect --format '"
                               "{{range .HostConfig.Binds}}{{printf \"%%s\\n\" .}}{{end}}' %s",
                               quoted_id);

        if (!docker_conn_run (data->active_conn, cmd, &output, err_text))
        {
            g_free (cmd);
            g_free (quoted_id);
            g_hash_table_destroy (seen);
            g_free (output);
            return FALSE;
        }

        load_mounts_from_binds_output (data, seen, output);
        g_free (cmd);
        g_free (output);
    }

    g_free (quoted_id);
    g_hash_table_destroy (seen);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_container_files_chdir (docker_data_t *data, const char *path)
{
    if (data->view == DOCKER_VIEW_CONTAINER_FILES)
    {
        const docker_item_t *item = find_item_by_name (data, path);

        if (item == NULL || !item->is_dir)
            return MC_PPR_FAILED;

        {
            const char *cwd = (data->files_cwd != NULL) ? data->files_cwd : "/";
            char *new_path = mc_pp_join_path (cwd, item->name);

            g_free (data->files_cwd);
            data->files_cwd = new_path;
        }

        set_view (data, DOCKER_VIEW_CONTAINER_FILES);
        return MC_PPR_OK;
    }

    return MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_container_files_enter_mounts (docker_data_t *data, const char *name)
{
    if (data->view == DOCKER_VIEW_CONTAINER_MOUNTS)
    {
        const docker_item_t *item;

        if (strcmp (name, "..") == 0)
            return MC_PPR_OK;

        item = find_item_by_name (data, name);
        if (item != NULL && item->is_dir && item->id != NULL)
        {
            data->host->close_plugin (data->host, item->id);
            return MC_PPR_OK;
        }

        return MC_PPR_FAILED;
    }

    if (data->view == DOCKER_VIEW_CONTAINER_FILES)
        return docker_container_files_chdir (data, name);

    return MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_container_files_get_local_copy (docker_data_t *data, const char *fname, char **local_path)
{
    const docker_item_t *item = find_item_by_name (data, fname);
    char *container_path;
    const char *cwd;
    GError *error = NULL;
    int fd;
    char *tmp_path = NULL;
    char *quoted_id;
    gboolean ok = FALSE;

    if (item == NULL || item->is_dir || data->current_container_id == NULL)
        return MC_PPR_FAILED;

    cwd = (data->files_cwd != NULL) ? data->files_cwd : "/";
    container_path = mc_pp_join_path (cwd, item->name);
    quoted_id = g_shell_quote (data->current_container_id);

    fd = g_file_open_tmp ("mc-docker-file-XXXXXX", &tmp_path, &error);
    if (fd == -1)
    {
        if (error != NULL)
            g_error_free (error);
        g_free (container_path);
        g_free (quoted_id);
        return MC_PPR_FAILED;
    }

    if (data->active_conn != NULL && data->active_conn->type == DOCKER_CONN_SSH)
    {
        /* SSH: docker cp writes to remote filesystem; stream via TAR pipe instead */
        docker_cp_stream_t stream;
        char *quoted_src = g_shell_quote (container_path);
        char *docker_args = g_strdup_printf ("cp %s:%s -", quoted_id, quoted_src);
        char *cmd = docker_conn_build_pipe_cmd (data->active_conn, docker_args);
        char *err_text = NULL;

        g_free (quoted_src);
        g_free (docker_args);

        stream.fd = -1;
        stream.errfd = -1;
        stream.child_pid = -1;

        if (!docker_cp_stream_open (cmd, &stream, &err_text))
        {
            g_free (cmd);
            close (fd);
            unlink (tmp_path);
            g_free (tmp_path);
            g_free (container_path);
            g_free (quoted_id);
            if (err_text != NULL && err_text[0] != '\0')
                message (D_ERROR, MSG_ERROR, "%s", err_text);
            g_free (err_text);
            return MC_PPR_FAILED;
        }
        g_free (cmd);

        ok = cp_stream_extract_single_file (stream.fd, fd, &err_text);
        close (fd);
        fd = -1;
        /* Close the read end before reap so docker cp can finish cleanly. */
        if (stream.fd >= 0)
        {
            close (stream.fd);
            stream.fd = -1;
        }
        {
            char *stderr_text = docker_cp_stream_read_stderr (&stream);

            /* On failure with no message yet, prefer docker's stderr output */
            if (!ok && err_text == NULL && stderr_text != NULL && stderr_text[0] != '\0')
                err_text = stderr_text;
            else
                g_free (stderr_text);
        }
        docker_cp_stream_reap (&stream);

        if (!ok)
        {
            if (err_text != NULL && err_text[0] != '\0')
                message (D_ERROR, MSG_ERROR, "%s", err_text);
            g_free (err_text);
            unlink (tmp_path);
            g_free (tmp_path);
            g_free (container_path);
            g_free (quoted_id);
            return MC_PPR_FAILED;
        }
        g_free (err_text);
    }
    else
    {
        /* Local: docker cp copies directly to a path on this machine */
        char *cp_cmd;
        char *output = NULL;
        char *err_text = NULL;

        close (fd);
        fd = -1;

        {
            char *quoted_src = g_shell_quote (container_path);
            char *quoted_dst = g_shell_quote (tmp_path);

            cp_cmd = g_strdup_printf ("cp %s:%s %s", quoted_id, quoted_src, quoted_dst);
            g_free (quoted_src);
            g_free (quoted_dst);
        }

        ok = docker_conn_run (data->active_conn, cp_cmd, &output, &err_text);
        g_free (cp_cmd);

        if (!ok)
        {
            if (err_text != NULL && err_text[0] != '\0')
                message (D_ERROR, MSG_ERROR, "%s", err_text);
            g_free (output);
            g_free (err_text);
            unlink (tmp_path);
            g_free (tmp_path);
            g_free (container_path);
            g_free (quoted_id);
            return MC_PPR_FAILED;
        }
        g_free (output);
        g_free (err_text);
    }

    g_free (container_path);
    g_free (quoted_id);
    *local_path = tmp_path;
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_container_files_get_default_format (docker_data_t *data)
{
    if (data->view == DOCKER_VIEW_CONTAINER_MOUNTS)
        return "name | size | perm";

    return NULL;
}
