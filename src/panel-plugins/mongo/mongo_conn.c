/*
   MongoDB panel plugin -- libmongoc wrapper.

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

#include <mongoc/mongoc.h>
#include <bson/bson.h>

#include "lib/global.h"

#include "mongo_conn.h"

/*** file scope variables ************************************************************************/

static gboolean mongoc_inited = FALSE;

/*** file scope functions ************************************************************************/

/* Silence libmongoc's stderr while mc owns the terminal. */
static void
mongo_log_noop (mongoc_log_level_t level, const char *log_domain, const char *message,
                void *user_data)
{
    (void) level;
    (void) log_domain;
    (void) message;
    (void) user_data;
}

/* --------------------------------------------------------------------------------------------- */

static int
cmp_str (const void *a, const void *b)
{
    return strcmp (*(const char *const *) a, *(const char *const *) b);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
is_system_db (const char *name)
{
    return strcmp (name, "admin") == 0 || strcmp (name, "local") == 0
        || strcmp (name, "config") == 0;
}

/* --------------------------------------------------------------------------------------------- */

/* Append {_id: {$gte: lo, $lt: hi}} or @extra (or both via $and) into @filter. */
static void
build_id_range_filter (bson_t *filter, const bson_value_t *lo, const bson_value_t *hi,
                       const bson_t *extra)
{
    bson_t id_doc;
    gboolean has_range = lo != NULL || hi != NULL;
    gboolean has_extra = extra != NULL && bson_count_keys (extra) > 0;

    if (has_range && has_extra)
    {
        bson_t arr, slot0, slot1;
        BSON_APPEND_ARRAY_BEGIN (filter, "$and", &arr);
        BSON_APPEND_DOCUMENT_BEGIN (&arr, "0", &slot0);
        BSON_APPEND_DOCUMENT_BEGIN (&slot0, "_id", &id_doc);
        if (lo != NULL)
            BSON_APPEND_VALUE (&id_doc, "$gte", lo);
        if (hi != NULL)
            BSON_APPEND_VALUE (&id_doc, "$lt", hi);
        bson_append_document_end (&slot0, &id_doc);
        bson_append_document_end (&arr, &slot0);
        BSON_APPEND_DOCUMENT_BEGIN (&arr, "1", &slot1);
        bson_concat (&slot1, extra);
        bson_append_document_end (&arr, &slot1);
        bson_append_array_end (filter, &arr);
        return;
    }
    if (has_range)
    {
        BSON_APPEND_DOCUMENT_BEGIN (filter, "_id", &id_doc);
        if (lo != NULL)
            BSON_APPEND_VALUE (&id_doc, "$gte", lo);
        if (hi != NULL)
            BSON_APPEND_VALUE (&id_doc, "$lt", hi);
        bson_append_document_end (filter, &id_doc);
        return;
    }
    if (has_extra)
        bson_concat (filter, extra);
}

/*** public functions ****************************************************************************/

void
mongo_conn_global_init (void)
{
    if (mongoc_inited)
        return;
    mongoc_init ();
    mongoc_log_set_handler (mongo_log_noop, NULL);
    mongoc_inited = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mongoc_client_t *
mongo_conn_connect (const mongo_cluster_t *cluster, const mongo_config_t *cfg, char **err_out)
{
    bson_error_t err;
    mongoc_uri_t *uri;
    mongoc_client_t *client;
    bson_t ping = BSON_INITIALIZER;
    bson_t reply;

    if (err_out != NULL)
        *err_out = NULL;
    if (cluster == NULL || cluster->uri == NULL || cluster->uri[0] == '\0')
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Cluster URI is empty.");
        return NULL;
    }

    mongo_conn_global_init ();

    uri = mongoc_uri_new_with_error (cluster->uri, &err);
    if (uri == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup_printf ("Invalid URI: %s", err.message);
        return NULL;
    }

    if (cfg != NULL)
    {
        mongoc_uri_set_option_as_int32 (uri, MONGOC_URI_SERVERSELECTIONTIMEOUTMS,
                                        cfg->server_selection_timeout_ms);
        mongoc_uri_set_option_as_int32 (uri, MONGOC_URI_CONNECTTIMEOUTMS, cfg->connect_timeout_ms);
        mongoc_uri_set_option_as_int32 (uri, MONGOC_URI_SOCKETTIMEOUTMS, cfg->socket_timeout_ms);
    }

#if MONGOC_CHECK_VERSION(1, 24, 0)
    client = mongoc_client_new_from_uri_with_error (uri, &err);
#else
    client = mongoc_client_new_from_uri (uri);
    if (client == NULL)
        bson_set_error (&err, MONGOC_ERROR_CLIENT, MONGOC_ERROR_CLIENT_INVALID_ENCRYPTION_ARG,
                        "mongoc_client_new_from_uri failed");
#endif
    mongoc_uri_destroy (uri);
    if (client == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        return NULL;
    }

    /* Ping so auth / network failures surface here, not on first query. */
    BSON_APPEND_INT32 (&ping, "ping", 1);
    if (!mongoc_client_command_simple (client, "admin", &ping, NULL, &reply, &err))
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        bson_destroy (&ping);
        bson_destroy (&reply);
        mongoc_client_destroy (client);
        return NULL;
    }
    bson_destroy (&ping);
    bson_destroy (&reply);

    return client;
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_conn_disconnect (mongoc_client_t *client)
{
    if (client != NULL)
        mongoc_client_destroy (client);
}

/* --------------------------------------------------------------------------------------------- */

char **
mongo_conn_list_databases (mongoc_client_t *client, char **err_out)
{
    bson_error_t err;
    char **names;
    char **filtered;
    gsize keep, total;

    if (err_out != NULL)
        *err_out = NULL;
    if (client == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Not connected.");
        return NULL;
    }

    names = mongoc_client_get_database_names_with_opts (client, NULL, &err);
    if (names == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        return NULL;
    }

    /* Hide system databases. Copy kept names into GLib-owned storage so the
       caller can g_strfreev() the result; the original vector is BSON-allocated
       and must be released with bson_strfreev(). */
    for (total = 0; names[total] != NULL; total++)
        ;
    filtered = g_new (char *, total + 1);
    keep = 0;
    for (gsize i = 0; i < total; i++)
        if (!is_system_db (names[i]))
            filtered[keep++] = g_strdup (names[i]);
    filtered[keep] = NULL;
    bson_strfreev (names);

    qsort (filtered, keep, sizeof (char *), cmp_str);
    return filtered;
}

/* --------------------------------------------------------------------------------------------- */

static void
coll_info_clear (gpointer p)
{
    mongo_coll_info_t *c = (mongo_coll_info_t *) p;
    if (c == NULL)
        return;
    g_free (c->name);
}

/* --------------------------------------------------------------------------------------------- */

static int
cmp_coll_info_name (const void *a, const void *b)
{
    const mongo_coll_info_t *ca = (const mongo_coll_info_t *) a;
    const mongo_coll_info_t *cb = (const mongo_coll_info_t *) b;
    return strcmp (ca->name, cb->name);
}

/* --------------------------------------------------------------------------------------------- */

GArray *
mongo_conn_list_collections (mongoc_client_t *client, const char *db_name, char **err_out)
{
    bson_error_t err;
    mongoc_database_t *db;
    char **names;
    GArray *out;
    gsize i;

    if (err_out != NULL)
        *err_out = NULL;
    if (client == NULL || db_name == NULL || db_name[0] == '\0')
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to list_collections.");
        return NULL;
    }

    db = mongoc_client_get_database (client, db_name);
    names = mongoc_database_get_collection_names_with_opts (db, NULL, &err);
    if (names == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        mongoc_database_destroy (db);
        return NULL;
    }

    out = g_array_new (FALSE, FALSE, sizeof (mongo_coll_info_t));
    g_array_set_clear_func (out, coll_info_clear);

    for (i = 0; names[i] != NULL; i++)
    {
        mongoc_collection_t *coll;
        mongo_coll_info_t info;
        gint64 n;

        coll = mongoc_database_get_collection (db, names[i]);
        n = mongoc_collection_estimated_document_count (coll, NULL, NULL, NULL, &err);
        mongoc_collection_destroy (coll);

        /* coll_info_clear frees this with g_free, so copy into GLib storage. */
        info.name = g_strdup (names[i]);
        info.count = (n >= 0) ? n : -1;
        g_array_append_val (out, info);
    }
    bson_strfreev (names);

    g_array_sort (out, cmp_coll_info_name);

    mongoc_database_destroy (db);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

static void
bson_value_clear_in_place (gpointer p)
{
    bson_value_t *v = (bson_value_t *) p;
    if (v != NULL)
        bson_value_destroy (v);
}

/* --------------------------------------------------------------------------------------------- */

GArray *
mongo_conn_list_documents (mongoc_client_t *client, const char *db_name, const char *coll_name,
                           const bson_value_t *lo, const bson_value_t *hi,
                           const bson_t *filter_extra, const bson_t *sort_override, gint64 skip,
                           gint64 limit, gboolean *has_more, char **err_out)
{
    bson_t filter = BSON_INITIALIZER;
    bson_t opts = BSON_INITIALIZER;
    bson_t projection;
    bson_t sort;
    mongoc_collection_t *coll;
    mongoc_cursor_t *cursor;
    const bson_t *doc;
    GArray *out;
    bson_error_t err;
    gint64 received = 0;

    if (err_out != NULL)
        *err_out = NULL;
    if (has_more != NULL)
        *has_more = FALSE;
    if (client == NULL || db_name == NULL || coll_name == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to list_documents.");
        return NULL;
    }

    build_id_range_filter (&filter, lo, hi, filter_extra);

    BSON_APPEND_INT64 (&opts, "skip", skip);
    BSON_APPEND_INT64 (&opts, "limit", limit);

    if (sort_override != NULL && bson_count_keys (sort_override) > 0)
        BSON_APPEND_DOCUMENT (&opts, "sort", sort_override);
    else
    {
        bson_init (&sort);
        BSON_APPEND_INT32 (&sort, "_id", 1);
        BSON_APPEND_DOCUMENT (&opts, "sort", &sort);
        bson_destroy (&sort);
    }

    bson_init (&projection);
    BSON_APPEND_INT32 (&projection, "_id", 1);
    BSON_APPEND_DOCUMENT (&opts, "projection", &projection);
    bson_destroy (&projection);

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    cursor = mongoc_collection_find_with_opts (coll, &filter, &opts, NULL);

    out = g_array_new (FALSE, FALSE, sizeof (bson_value_t));
    g_array_set_clear_func (out, bson_value_clear_in_place);

    while (mongoc_cursor_next (cursor, &doc))
    {
        bson_iter_t it;
        if (bson_iter_init_find (&it, doc, "_id"))
        {
            bson_value_t copy;
            bson_value_copy (bson_iter_value (&it), &copy);
            g_array_append_val (out, copy);
            received++;
        }
    }

    if (mongoc_cursor_error (cursor, &err))
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        g_array_free (out, TRUE);
        out = NULL;
    }
    else if (has_more != NULL)
        *has_more = (received == limit);

    mongoc_cursor_destroy (cursor);
    mongoc_collection_destroy (coll);
    bson_destroy (&opts);
    bson_destroy (&filter);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

/* Scalar BSON equality; unsupported types return FALSE. */
static gboolean
bson_value_equal_scalar (const bson_value_t *a, const bson_value_t *b)
{
    if (a == NULL || b == NULL || a->value_type != b->value_type)
        return FALSE;
    switch (a->value_type)
    {
    case BSON_TYPE_OID:
        return bson_oid_equal (&a->value.v_oid, &b->value.v_oid);
    case BSON_TYPE_UTF8:
        if (a->value.v_utf8.str == NULL || b->value.v_utf8.str == NULL)
            return a->value.v_utf8.str == b->value.v_utf8.str;
        return strcmp (a->value.v_utf8.str, b->value.v_utf8.str) == 0;
    case BSON_TYPE_INT32:
        return a->value.v_int32 == b->value.v_int32;
    case BSON_TYPE_INT64:
        return a->value.v_int64 == b->value.v_int64;
    case BSON_TYPE_BOOL:
        return a->value.v_bool == b->value.v_bool;
    case BSON_TYPE_DOUBLE:
        /* _id replacement check requires bitwise identity for doubles. */
        return memcmp (&a->value.v_double, &b->value.v_double, sizeof (double)) == 0;
    case BSON_TYPE_NULL:
        return TRUE;
    default:
        return FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

bson_t *
mongo_conn_get_document (mongoc_client_t *client, const char *db_name, const char *coll_name,
                         const bson_value_t *id_value, const bson_t *projection_override,
                         char **err_out)
{
    bson_t filter = BSON_INITIALIZER;
    bson_t opts = BSON_INITIALIZER;
    mongoc_collection_t *coll;
    mongoc_cursor_t *cursor;
    const bson_t *doc;
    bson_t *out = NULL;
    bson_error_t err;

    if (err_out != NULL)
        *err_out = NULL;
    if (client == NULL || db_name == NULL || coll_name == NULL || id_value == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to get_document.");
        return NULL;
    }

    BSON_APPEND_VALUE (&filter, "_id", id_value);
    BSON_APPEND_INT64 (&opts, "limit", 1);
    if (projection_override != NULL && bson_count_keys (projection_override) > 0)
        BSON_APPEND_DOCUMENT (&opts, "projection", projection_override);

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    cursor = mongoc_collection_find_with_opts (coll, &filter, &opts, NULL);

    if (mongoc_cursor_next (cursor, &doc))
        out = bson_copy (doc);

    if (mongoc_cursor_error (cursor, &err))
    {
        if (out != NULL)
        {
            bson_destroy (out);
            out = NULL;
        }
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
    }
    else if (out == NULL && err_out != NULL)
        *err_out = g_strdup ("Document not found.");

    mongoc_cursor_destroy (cursor);
    mongoc_collection_destroy (coll);
    bson_destroy (&opts);
    bson_destroy (&filter);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_conn_replace_document (mongoc_client_t *client, const char *db_name, const char *coll_name,
                             const bson_value_t *original_id, const bson_t *replacement,
                             char **err_out)
{
    bson_t filter = BSON_INITIALIZER;
    mongoc_collection_t *coll;
    bson_iter_t it;
    bson_error_t err;
    gboolean ok;

    if (err_out != NULL)
        *err_out = NULL;
    if (client == NULL || db_name == NULL || coll_name == NULL || original_id == NULL
        || replacement == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to replace_document.");
        return FALSE;
    }

    if (bson_iter_init_find (&it, replacement, "_id"))
    {
        const bson_value_t *new_id = bson_iter_value (&it);
        if (!bson_value_equal_scalar (new_id, original_id))
        {
            if (err_out != NULL)
                *err_out = g_strdup ("_id is immutable; restore it or remove the field");
            return FALSE;
        }
    }

    BSON_APPEND_VALUE (&filter, "_id", original_id);
    coll = mongoc_client_get_collection (client, db_name, coll_name);
    ok = mongoc_collection_replace_one (coll, &filter, replacement, NULL, NULL, &err);
    mongoc_collection_destroy (coll);
    bson_destroy (&filter);

    if (!ok && err_out != NULL)
        *err_out = g_strdup (err.message);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_conn_delete_documents (mongoc_client_t *client, const char *db_name, const char *coll_name,
                             const bson_value_t *const *ids, gsize count, gint64 *deleted_out,
                             char **err_out)
{
    bson_t filter = BSON_INITIALIZER;
    bson_t in_doc = BSON_INITIALIZER;
    bson_t in_arr = BSON_INITIALIZER;
    mongoc_collection_t *coll;
    bson_error_t err;
    bson_t reply;
    gboolean ok;
    gsize i;
    char idx[16];

    if (err_out != NULL)
        *err_out = NULL;
    if (deleted_out != NULL)
        *deleted_out = 0;
    if (client == NULL || db_name == NULL || coll_name == NULL || ids == NULL || count == 0)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to delete_documents.");
        return FALSE;
    }

    for (i = 0; i < count; i++)
    {
        if (ids[i] == NULL)
            continue;
        g_snprintf (idx, sizeof (idx), "%zu", i);
        BSON_APPEND_VALUE (&in_arr, idx, ids[i]);
    }
    BSON_APPEND_ARRAY (&in_doc, "$in", &in_arr);
    BSON_APPEND_DOCUMENT (&filter, "_id", &in_doc);

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    ok = mongoc_collection_delete_many (coll, &filter, NULL, &reply, &err);
    if (ok && deleted_out != NULL)
    {
        bson_iter_t it;
        if (bson_iter_init_find (&it, &reply, "deletedCount") && BSON_ITER_HOLDS_INT64 (&it))
            *deleted_out = bson_iter_int64 (&it);
        else if (bson_iter_init_find (&it, &reply, "deletedCount") && BSON_ITER_HOLDS_INT32 (&it))
            *deleted_out = (gint64) bson_iter_int32 (&it);
    }
    /* reply is always initialized by mongoc_collection_delete_many (even on
       failure), so destroy it unconditionally. */
    bson_destroy (&reply);
    mongoc_collection_destroy (coll);
    bson_destroy (&filter);
    bson_destroy (&in_doc);
    bson_destroy (&in_arr);

    if (!ok && err_out != NULL)
        *err_out = g_strdup (err.message);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_conn_insert_document (mongoc_client_t *client, const char *db_name, const char *coll_name,
                            const bson_t *doc, char **err_out)
{
    mongoc_collection_t *coll;
    bson_error_t err;
    gboolean ok;

    if (err_out != NULL)
        *err_out = NULL;
    if (client == NULL || db_name == NULL || coll_name == NULL || doc == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to insert_document.");
        return FALSE;
    }

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    ok = mongoc_collection_insert_one (coll, doc, NULL, NULL, &err);
    mongoc_collection_destroy (coll);

    if (!ok && err_out != NULL)
        *err_out = g_strdup (err.message);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_conn_count_range (mongoc_client_t *client, const char *db_name, const char *coll_name,
                        const bson_value_t *lo, const bson_value_t *hi, const bson_t *filter_extra,
                        gint64 *count_out, char **err_out)
{
    bson_t filter = BSON_INITIALIZER;
    mongoc_collection_t *coll;
    bson_error_t err;
    gint64 n;

    if (err_out != NULL)
        *err_out = NULL;
    if (count_out != NULL)
        *count_out = -1;
    if (client == NULL || db_name == NULL || coll_name == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to count_range.");
        return FALSE;
    }

    build_id_range_filter (&filter, lo, hi, filter_extra);

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    n = mongoc_collection_count_documents (coll, &filter, NULL, NULL, NULL, &err);
    mongoc_collection_destroy (coll);
    bson_destroy (&filter);

    if (n < 0)
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        return FALSE;
    }
    if (count_out != NULL)
        *count_out = n;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
bucket_info_clear (gpointer p)
{
    mongo_bucket_info_t *b = (mongo_bucket_info_t *) p;
    if (b == NULL)
        return;
    bson_value_destroy (&b->lo);
    if (b->has_hi)
        bson_value_destroy (&b->hi);
}

/* --------------------------------------------------------------------------------------------- */

GArray *
mongo_conn_bucket_auto (mongoc_client_t *client, const char *db_name, const char *coll_name,
                        const bson_value_t *lo, const bson_value_t *hi, const bson_t *filter_extra,
                        gint64 target_fanout, char **err_out)
{
    bson_t pipeline = BSON_INITIALIZER;
    bson_t stage_match, match_filter;
    bson_t stage_bucket, bucket_args;
    bson_array_builder_t *arr;
    mongoc_collection_t *coll;
    mongoc_cursor_t *cursor;
    const bson_t *doc;
    GArray *out;
    bson_error_t err;
    gboolean has_extra = filter_extra != NULL && bson_count_keys (filter_extra) > 0;

    if (err_out != NULL)
        *err_out = NULL;
    if (client == NULL || db_name == NULL || coll_name == NULL || target_fanout <= 0)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to bucket_auto.");
        return NULL;
    }

    bson_append_array_builder_begin (&pipeline, "pipeline", -1, &arr);

    if (lo != NULL || hi != NULL || has_extra)
    {
        bson_array_builder_append_document_begin (arr, &stage_match);
        BSON_APPEND_DOCUMENT_BEGIN (&stage_match, "$match", &match_filter);
        build_id_range_filter (&match_filter, lo, hi, filter_extra);
        bson_append_document_end (&stage_match, &match_filter);
        bson_array_builder_append_document_end (arr, &stage_match);
    }

    bson_array_builder_append_document_begin (arr, &stage_bucket);
    BSON_APPEND_DOCUMENT_BEGIN (&stage_bucket, "$bucketAuto", &bucket_args);
    BSON_APPEND_UTF8 (&bucket_args, "groupBy", "$_id");
    BSON_APPEND_INT64 (&bucket_args, "buckets", target_fanout);
    bson_append_document_end (&stage_bucket, &bucket_args);
    bson_array_builder_append_document_end (arr, &stage_bucket);

    bson_append_array_builder_end (&pipeline, arr);

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    cursor = mongoc_collection_aggregate (coll, MONGOC_QUERY_NONE, &pipeline, NULL, NULL);

    out = g_array_new (FALSE, FALSE, sizeof (mongo_bucket_info_t));
    g_array_set_clear_func (out, bucket_info_clear);

    while (mongoc_cursor_next (cursor, &doc))
    {
        bson_iter_t it, sub;
        mongo_bucket_info_t info = { 0 };

        /* {_id: {min: <v>, max: <v>}, count: <n>} */
        if (bson_iter_init_find (&it, doc, "_id") && BSON_ITER_HOLDS_DOCUMENT (&it)
            && bson_iter_recurse (&it, &sub))
        {
            while (bson_iter_next (&sub))
            {
                const char *k = bson_iter_key (&sub);
                if (strcmp (k, "min") == 0)
                    bson_value_copy (bson_iter_value (&sub), &info.lo);
                else if (strcmp (k, "max") == 0)
                {
                    bson_value_copy (bson_iter_value (&sub), &info.hi);
                    info.has_hi = TRUE;
                }
            }
        }
        if (bson_iter_init_find (&it, doc, "count")
            && (BSON_ITER_HOLDS_INT32 (&it) || BSON_ITER_HOLDS_INT64 (&it)))
            info.count =
                BSON_ITER_HOLDS_INT64 (&it) ? bson_iter_int64 (&it) : bson_iter_int32 (&it);
        else
            info.count = -1;

        g_array_append_val (out, info);
    }

    if (mongoc_cursor_error (cursor, &err))
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        g_array_free (out, TRUE);
        out = NULL;
    }

    mongoc_cursor_destroy (cursor);
    mongoc_collection_destroy (coll);
    bson_destroy (&pipeline);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

/* Scalar BSON value -> display string (caller g_free), or NULL to skip
   non-scalar values (documents, arrays, oid, date, ...). */
static char *
distinct_value_to_string (bson_iter_t *it)
{
    switch (bson_iter_type (it))
    {
    case BSON_TYPE_UTF8:
    {
        uint32_t len = 0;
        const char *s = bson_iter_utf8 (it, &len);
        return g_strndup (s, len);
    }
    case BSON_TYPE_INT32:
        return g_strdup_printf ("%d", bson_iter_int32 (it));
    case BSON_TYPE_INT64:
        return g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) bson_iter_int64 (it));
    case BSON_TYPE_DOUBLE:
    {
        char buf[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr (buf, sizeof (buf), bson_iter_double (it));
        return g_strdup (buf);
    }
    case BSON_TYPE_BOOL:
        return g_strdup (bson_iter_bool (it) ? "true" : "false");
    default:
        return NULL;
    }
}

static gint
distinct_str_cmp (gconstpointer a, gconstpointer b)
{
    return strcmp (*(const char *const *) a, *(const char *const *) b);
}

char **
mongo_conn_distinct_values (mongoc_client_t *client, const char *db_name, const char *coll_name,
                            const char *field, const bson_t *filter_extra, gint64 limit,
                            gboolean *capped_out, char **err_out)
{
    bson_t pipeline = BSON_INITIALIZER;
    bson_t stage, body;
    bson_array_builder_t *arr;
    char *path;
    mongoc_collection_t *coll;
    mongoc_cursor_t *cursor;
    const bson_t *doc;
    GPtrArray *vals;
    bson_error_t err;
    gint64 ndocs = 0;
    gboolean has_extra = filter_extra != NULL && bson_count_keys (filter_extra) > 0;

    if (capped_out != NULL)
        *capped_out = FALSE;
    if (err_out != NULL)
        *err_out = NULL;
    if (client == NULL || db_name == NULL || coll_name == NULL || field == NULL || field[0] == '\0'
        || limit <= 0)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to distinct_values.");
        return NULL;
    }

    path = g_strconcat ("$", field, NULL);

    bson_append_array_builder_begin (&pipeline, "pipeline", -1, &arr);

    if (has_extra)
    {
        bson_array_builder_append_document_begin (arr, &stage);
        BSON_APPEND_DOCUMENT (&stage, "$match", filter_extra);
        bson_array_builder_append_document_end (arr, &stage);
    }

    /* $unwind handles arrays (one row per element) and, since 3.2, treats a
       scalar as a single-element array; missing paths are dropped. */
    bson_array_builder_append_document_begin (arr, &stage);
    BSON_APPEND_UTF8 (&stage, "$unwind", path);
    bson_array_builder_append_document_end (arr, &stage);

    bson_array_builder_append_document_begin (arr, &stage);
    BSON_APPEND_DOCUMENT_BEGIN (&stage, "$group", &body);
    BSON_APPEND_UTF8 (&body, "_id", path);
    bson_append_document_end (&stage, &body);
    bson_array_builder_append_document_end (arr, &stage);

    /* Fetch one past the limit so the caller can tell the list was truncated. */
    bson_array_builder_append_document_begin (arr, &stage);
    BSON_APPEND_INT64 (&stage, "$limit", limit + 1);
    bson_array_builder_append_document_end (arr, &stage);

    bson_append_array_builder_end (&pipeline, arr);

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    cursor = mongoc_collection_aggregate (coll, MONGOC_QUERY_NONE, &pipeline, NULL, NULL);

    vals = g_ptr_array_new_with_free_func (g_free);
    while (mongoc_cursor_next (cursor, &doc))
    {
        bson_iter_t it;
        char *s;

        ndocs++;
        if (!bson_iter_init_find (&it, doc, "_id"))
            continue;
        s = distinct_value_to_string (&it);
        if (s != NULL)
            g_ptr_array_add (vals, s);
    }

    if (mongoc_cursor_error (cursor, &err))
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        g_ptr_array_free (vals, TRUE);
        vals = NULL;
    }

    mongoc_cursor_destroy (cursor);
    mongoc_collection_destroy (coll);
    bson_destroy (&pipeline);
    g_free (path);

    if (vals == NULL)
        return NULL;

    if (ndocs > limit)
    {
        if (capped_out != NULL)
            *capped_out = TRUE;
        while ((gint64) vals->len > limit)
            g_ptr_array_remove_index_fast (vals, vals->len - 1);
    }

    g_ptr_array_sort (vals, distinct_str_cmp);
    g_ptr_array_add (vals, NULL);
    return (char **) g_ptr_array_free (vals, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* find().sort(direction).limit(1) within [@lo, @hi); copy _id into out. */
static gboolean
find_one_id (mongoc_client_t *client, const char *db_name, const char *coll_name,
             const bson_value_t *lo, const bson_value_t *hi, int direction, bson_value_t *out_value,
             char **err_out)
{
    bson_t filter = BSON_INITIALIZER;
    bson_t opts = BSON_INITIALIZER;
    bson_t projection;
    bson_t sort;
    mongoc_collection_t *coll;
    mongoc_cursor_t *cursor;
    const bson_t *doc;
    bson_iter_t it;
    bson_error_t err;
    gboolean found = FALSE;

    build_id_range_filter (&filter, lo, hi, NULL);

    bson_init (&projection);
    BSON_APPEND_INT32 (&projection, "_id", 1);
    BSON_APPEND_DOCUMENT (&opts, "projection", &projection);
    bson_destroy (&projection);

    bson_init (&sort);
    BSON_APPEND_INT32 (&sort, "_id", direction);
    BSON_APPEND_DOCUMENT (&opts, "sort", &sort);
    bson_destroy (&sort);

    BSON_APPEND_INT64 (&opts, "limit", 1);

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    cursor = mongoc_collection_find_with_opts (coll, &filter, &opts, NULL);

    if (mongoc_cursor_next (cursor, &doc) && bson_iter_init_find (&it, doc, "_id"))
    {
        bson_value_copy (bson_iter_value (&it), out_value);
        found = TRUE;
    }

    if (mongoc_cursor_error (cursor, &err))
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        if (found)
            bson_value_destroy (out_value);
        found = FALSE;
    }

    mongoc_cursor_destroy (cursor);
    mongoc_collection_destroy (coll);
    bson_destroy (&opts);
    bson_destroy (&filter);
    return found;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_conn_min_max_id (mongoc_client_t *client, const char *db_name, const char *coll_name,
                       const bson_value_t *lo, const bson_value_t *hi, bson_value_t *min_out,
                       bson_value_t *max_out, gboolean *found_out, char **err_out)
{
    char *err1 = NULL, *err2 = NULL;
    gboolean got_min, got_max;

    if (err_out != NULL)
        *err_out = NULL;
    if (found_out != NULL)
        *found_out = FALSE;
    if (client == NULL || db_name == NULL || coll_name == NULL || min_out == NULL
        || max_out == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to min_max_id.");
        return FALSE;
    }

    got_min = find_one_id (client, db_name, coll_name, lo, hi, 1, min_out, &err1);
    got_max = find_one_id (client, db_name, coll_name, lo, hi, -1, max_out, &err2);

    if (err1 != NULL || err2 != NULL)
    {
        if (got_min)
            bson_value_destroy (min_out);
        if (got_max)
            bson_value_destroy (max_out);
        if (err_out != NULL)
        {
            *err_out = err1 != NULL ? err1 : err2;
            if (err1 != NULL)
                g_free (err2);
        }
        else
        {
            g_free (err1);
            g_free (err2);
        }
        return FALSE;
    }

    if (!got_min || !got_max)
    {
        if (got_min)
            bson_value_destroy (min_out);
        if (got_max)
            bson_value_destroy (max_out);
        return TRUE;
    }
    if (found_out != NULL)
        *found_out = TRUE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

bson_t *
mongo_conn_sample_one_doc (mongoc_client_t *client, const char *db_name, const char *coll_name,
                           const bson_value_t *lo, const bson_value_t *hi,
                           const bson_t *filter_extra, char **err_out)
{
    bson_t filter = BSON_INITIALIZER;
    bson_t opts = BSON_INITIALIZER;
    bson_t sort;
    mongoc_collection_t *coll;
    mongoc_cursor_t *cursor;
    const bson_t *doc;
    bson_t *out = NULL;
    bson_error_t err;

    if (err_out != NULL)
        *err_out = NULL;
    if (client == NULL || db_name == NULL || coll_name == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to sample_one_doc.");
        return NULL;
    }

    build_id_range_filter (&filter, lo, hi, filter_extra);

    bson_init (&sort);
    BSON_APPEND_INT32 (&sort, "_id", 1);
    BSON_APPEND_DOCUMENT (&opts, "sort", &sort);
    bson_destroy (&sort);
    BSON_APPEND_INT64 (&opts, "limit", 1);

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    cursor = mongoc_collection_find_with_opts (coll, &filter, &opts, NULL);

    if (mongoc_cursor_next (cursor, &doc))
        out = bson_copy (doc);

    if (mongoc_cursor_error (cursor, &err))
    {
        if (out != NULL)
        {
            bson_destroy (out);
            out = NULL;
        }
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
    }

    mongoc_cursor_destroy (cursor);
    mongoc_collection_destroy (coll);
    bson_destroy (&opts);
    bson_destroy (&filter);
    return out;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_conn_sample_id_type (mongoc_client_t *client, const char *db_name, const char *coll_name,
                           bson_type_t *type_out, char **err_out)
{
    bson_t filter = BSON_INITIALIZER;
    bson_t opts = BSON_INITIALIZER;
    bson_t projection;
    mongoc_collection_t *coll;
    mongoc_cursor_t *cursor;
    const bson_t *doc;
    bson_iter_t it;
    bson_error_t err;
    gboolean ok = FALSE;

    if (err_out != NULL)
        *err_out = NULL;
    if (type_out != NULL)
        *type_out = BSON_TYPE_EOD;
    if (client == NULL || db_name == NULL || coll_name == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Internal: bad arguments to sample_id_type.");
        return FALSE;
    }

    BSON_APPEND_INT64 (&opts, "limit", 1);
    bson_init (&projection);
    BSON_APPEND_INT32 (&projection, "_id", 1);
    BSON_APPEND_DOCUMENT (&opts, "projection", &projection);
    bson_destroy (&projection);

    coll = mongoc_client_get_collection (client, db_name, coll_name);
    cursor = mongoc_collection_find_with_opts (coll, &filter, &opts, NULL);

    if (mongoc_cursor_next (cursor, &doc) && bson_iter_init_find (&it, doc, "_id"))
    {
        if (type_out != NULL)
            *type_out = bson_iter_type (&it);
        ok = TRUE;
    }

    if (mongoc_cursor_error (cursor, &err))
    {
        if (err_out != NULL)
            *err_out = g_strdup (err.message);
        ok = FALSE;
    }
    else if (!ok && err_out != NULL)
        *err_out = g_strdup ("Collection is empty.");

    mongoc_cursor_destroy (cursor);
    mongoc_collection_destroy (coll);
    bson_destroy (&opts);
    bson_destroy (&filter);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */
