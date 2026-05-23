/*
   MongoDB panel plugin -- document IO: local copy, view, save, put, delete.

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
#include <unistd.h>

#include <bson/bson.h>

#include "lib/global.h"
#include "lib/vfs/vfs.h"       // vfs_path_from_str
#include "lib/panel-plugin.h"  // mc_pp_result_t, mc_pp_rename_with_ext

#include "src/viewer/mcviewer.h"  // mcview_viewer

#include "mongo_internal.h"
#include "mongo_conn.h"
#include "mongo_render.h"
#include "mongo_ui.h"

/*** file scope functions ************************************************************************/

/* Render one document as pretty Relaxed Extended JSON. Projection and base64
   trimming are view-only; edit/copy paths fetch the full document. */
static char *
mongo_doc_json (mongo_data_t *data, int slot, gboolean trim_base64, gboolean apply_projection)
{
    const bson_value_t *id_value;
    bson_t *doc;
    char *raw;
    char *json;
    char *err = NULL;

    id_value = &g_array_index (data->doc_ids, bson_value_t, slot);
    {
        const bson_t *proj =
            (apply_projection && data->filter != NULL) ? data->filter->projection : NULL;
        doc = mongo_conn_get_document (data->client, data->db_name, data->coll_name, id_value, proj,
                                       &err);
    }
    if (doc == NULL)
    {
        char *text =
            g_strdup_printf (_ ("Cannot fetch document: %s"), err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        return NULL;
    }

    raw = bson_as_relaxed_extended_json (doc, NULL);
    bson_destroy (doc);
    if (raw == NULL)
    {
        mongo_show_message (data->host, TRUE, _ ("Failed to render document as JSON."));
        return NULL;
    }

    if (trim_base64)
    {
        char *trimmed = mongo_render_truncate_base64 (raw, 24);
        bson_free (raw);
        if (trimmed == NULL)
        {
            mongo_show_message (data->host, TRUE, _ ("Failed to render document as JSON."));
            return NULL;
        }
        json = mongo_render_pretty_json (trimmed, 2);
        g_free (trimmed);
    }
    else
    {
        json = mongo_render_pretty_json (raw, 2);
        bson_free (raw);
    }

    if (json == NULL)
        mongo_show_message (data->host, TRUE, _ ("Failed to render document as JSON."));
    return json;
}

/* --------------------------------------------------------------------------------------------- */

/* Write @json to a fresh temp file (renamed to keep a .json extension).
   Returns the path (caller frees) or NULL after showing a message. */
static char *
mongo_json_to_temp (mongo_data_t *data, const char *json)
{
    char *tmp_path = NULL;
    GError *gerr = NULL;
    int tmp_fd;
    size_t json_len = strlen (json);
    size_t off = 0;

    tmp_fd = g_file_open_tmp ("mc-mongo-XXXXXX", &tmp_path, &gerr);
    if (tmp_fd < 0)
    {
        char *text = g_strdup_printf (_ ("Cannot open temp file: %s"),
                                      gerr != NULL ? gerr->message : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        if (gerr != NULL)
            g_error_free (gerr);
        g_free (tmp_path);
        return NULL;
    }

    while (off < json_len)
    {
        ssize_t n = write (tmp_fd, json + off, json_len - off);
        if (n <= 0)
        {
            close (tmp_fd);
            unlink (tmp_path);
            g_free (tmp_path);
            mongo_show_message (data->host, TRUE, _ ("Failed to write temp file."));
            return NULL;
        }
        off += (size_t) n;
    }
    close (tmp_fd);

    mc_pp_rename_with_ext (&tmp_path, "doc.json");
    return tmp_path;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
mongo_get_local_copy (void *plugin_data, const char *fname, char **local_path)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    int slot;
    char *json;
    char *tmp_path;

    if (local_path != NULL)
        *local_path = NULL;
    if (data == NULL || fname == NULL || data->level != MONGO_LEVEL_DOCS || data->doc_ids == NULL
        || data->coll_name == NULL || mongo_is_virtual_fname (fname))
        return MC_PPR_NOT_SUPPORTED;

    slot = mongo_slot_from_fname (fname);
    if (slot < 0 || (guint) slot >= data->doc_ids->len)
        return MC_PPR_NOT_SUPPORTED;

    /* Edit/copy paths use the full document. */
    json = mongo_doc_json (data, slot, FALSE, FALSE);
    if (json == NULL)
        return MC_PPR_FAILED;

    tmp_path = mongo_json_to_temp (data, json);
    g_free (json);
    if (tmp_path == NULL)
        return MC_PPR_FAILED;

    if (local_path != NULL)
        *local_path = tmp_path;
    else
    {
        unlink (tmp_path);
        g_free (tmp_path);
    }
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/* F3 view: render the document with base64 values trimmed (display only) and
   open it in the internal viewer. */
mc_pp_result_t
mongo_view (void *plugin_data, const char *fname, const struct stat *st, gboolean plain_view)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    int slot;
    char *json;
    char *tmp_path;

    (void) st;
    (void) plain_view;

    if (data == NULL || fname == NULL || data->level != MONGO_LEVEL_DOCS || data->doc_ids == NULL
        || data->coll_name == NULL || mongo_is_virtual_fname (fname))
        return MC_PPR_NOT_SUPPORTED;

    slot = mongo_slot_from_fname (fname);
    if (slot < 0 || (guint) slot >= data->doc_ids->len)
        return MC_PPR_NOT_SUPPORTED;

    json = mongo_doc_json (data, slot, TRUE, TRUE);
    if (json == NULL)
        return MC_PPR_FAILED;

    tmp_path = mongo_json_to_temp (data, json);
    g_free (json);
    if (tmp_path == NULL)
        return MC_PPR_FAILED;

    {
        vfs_path_t *tmp_vpath = vfs_path_from_str (tmp_path);
        (void) mcview_viewer (NULL, tmp_vpath, 0, 0, 0);
        vfs_path_free (tmp_vpath, TRUE);
    }
    unlink (tmp_path);
    g_free (tmp_path);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
mongo_save_file (void *plugin_data, const char *local_path, const char *remote_name)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    int slot;
    const bson_value_t *original_id;
    gchar *file_data = NULL;
    gsize file_len = 0;
    GError *gerr = NULL;
    bson_error_t berr;
    bson_t *parsed;
    char *err = NULL;
    gboolean ok;

    if (data == NULL || data->level != MONGO_LEVEL_DOCS || data->doc_ids == NULL
        || data->coll_name == NULL || remote_name == NULL || local_path == NULL
        || mongo_is_virtual_fname (remote_name) || strcmp (remote_name, "..") == 0)
        return MC_PPR_NOT_SUPPORTED;

    if (data->cluster != NULL && data->cluster->read_only)
    {
        mongo_show_message (data->host, TRUE, _ ("Cluster is read-only."));
        return MC_PPR_FAILED;
    }

    slot = mongo_slot_from_fname (remote_name);
    if (slot < 0 || (guint) slot >= data->doc_ids->len)
        return MC_PPR_NOT_SUPPORTED;
    original_id = &g_array_index (data->doc_ids, bson_value_t, slot);

    if (!g_file_get_contents (local_path, &file_data, &file_len, &gerr))
    {
        char *text = g_strdup_printf (_ ("Cannot read edited file: %s"),
                                      gerr != NULL ? gerr->message : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        if (gerr != NULL)
            g_error_free (gerr);
        return MC_PPR_FAILED;
    }

    parsed = bson_new_from_json ((const uint8_t *) file_data, (ssize_t) file_len, &berr);
    g_free (file_data);
    if (parsed == NULL)
    {
        char *text = g_strdup_printf (_ ("JSON parse error: %s"), berr.message);
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        return MC_PPR_FAILED;
    }

    ok = mongo_conn_replace_document (data->client, data->db_name, data->coll_name, original_id,
                                      parsed, &err);
    bson_destroy (parsed);

    if (!ok)
    {
        char *text = g_strdup_printf (_ ("Update failed: %s"), err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        return MC_PPR_FAILED;
    }
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/* Re-fetch the current document page after a local mutation. */
void
mongo_reload_current_page (mongo_data_t *data)
{
    if (data == NULL || data->level != MONGO_LEVEL_DOCS || data->coll_name == NULL)
        return;
    (void) mongo_load_page (data, data->coll_name, 0);
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
mongo_put_file (void *plugin_data, const char *local_path, const char *dest_name)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    gchar *file_data = NULL;
    gsize file_len = 0;
    GError *gerr = NULL;
    bson_error_t berr;
    bson_t *parsed;
    char *err = NULL;
    gboolean ok;

    (void) dest_name;

    if (data == NULL || data->level != MONGO_LEVEL_DOCS || data->coll_name == NULL
        || local_path == NULL)
        return MC_PPR_NOT_SUPPORTED;

    if (data->cluster != NULL && data->cluster->read_only)
    {
        mongo_show_message (data->host, TRUE, _ ("Cluster is read-only."));
        return MC_PPR_FAILED;
    }

    if (!g_file_get_contents (local_path, &file_data, &file_len, &gerr))
    {
        char *text = g_strdup_printf (_ ("Cannot read source file: %s"),
                                      gerr != NULL ? gerr->message : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        if (gerr != NULL)
            g_error_free (gerr);
        return MC_PPR_FAILED;
    }

    parsed = bson_new_from_json ((const uint8_t *) file_data, (ssize_t) file_len, &berr);
    g_free (file_data);
    if (parsed == NULL)
    {
        char *text = g_strdup_printf (_ ("JSON parse error: %s"), berr.message);
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        return MC_PPR_FAILED;
    }

    ok = mongo_conn_insert_document (data->client, data->db_name, data->coll_name, parsed, &err);
    bson_destroy (parsed);
    if (!ok)
    {
        char *text = g_strdup_printf (_ ("Insert failed: %s"), err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        return MC_PPR_FAILED;
    }

    mongo_reload_current_page (data);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
mongo_delete_items (void *plugin_data, const char **names, int count)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    const bson_value_t **ids;
    int collected;
    int i;
    char *err = NULL;
    gint64 deleted = 0;
    gboolean ok;

    if (data == NULL || data->level != MONGO_LEVEL_DOCS || data->doc_ids == NULL
        || data->coll_name == NULL || names == NULL || count <= 0)
        return MC_PPR_NOT_SUPPORTED;

    if (data->cluster != NULL && data->cluster->read_only)
    {
        mongo_show_message (data->host, TRUE, _ ("Cluster is read-only."));
        return MC_PPR_FAILED;
    }

    ids = g_new0 (const bson_value_t *, (gsize) count);
    collected = 0;
    for (i = 0; i < count; i++)
    {
        int slot;
        if (names[i] == NULL || mongo_is_virtual_fname (names[i]) || strcmp (names[i], "..") == 0)
            continue;
        slot = mongo_slot_from_fname (names[i]);
        if (slot < 0 || (guint) slot >= data->doc_ids->len)
            continue;
        ids[collected++] = &g_array_index (data->doc_ids, bson_value_t, slot);
    }

    if (collected == 0)
    {
        g_free (ids);
        return MC_PPR_NOT_SUPPORTED;
    }

    ok = mongo_conn_delete_documents (data->client, data->db_name, data->coll_name, ids,
                                      (gsize) collected, &deleted, &err);
    g_free (ids);

    if (!ok)
    {
        char *text = g_strdup_printf (_ ("Delete failed: %s"), err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        return MC_PPR_FAILED;
    }

    (void) deleted;
    (void) mongo_load_page (data, data->coll_name, 0);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
