/*
   MongoDB panel plugin -- hierarchical _id bucketing for large collections.

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
#include <time.h>

#include <bson/bson.h>

#include "lib/global.h"

#include "mongo_internal.h"
#include "mongo_conn.h"
#include "mongo_ui.h"

/*** file scope functions ************************************************************************/

/* Extract the 4-byte Unix timestamp prefix from an ObjectId. */
static gint64
mongo_oid_unix (const bson_oid_t *oid)
{
    return ((gint64) (guint8) oid->bytes[0] << 24) | ((gint64) (guint8) oid->bytes[1] << 16)
        | ((gint64) (guint8) oid->bytes[2] << 8) | (gint64) (guint8) oid->bytes[3];
}

/* Build an OID with @unix_time in the 4-byte prefix and zeroes after. */
static void
mongo_oid_value_at_unix (gint64 unix_time, bson_value_t *out)
{
    memset (out, 0, sizeof (*out));
    out->value_type = BSON_TYPE_OID;
    out->value.v_oid.bytes[0] = (unix_time >> 24) & 0xFF;
    out->value.v_oid.bytes[1] = (unix_time >> 16) & 0xFF;
    out->value.v_oid.bytes[2] = (unix_time >> 8) & 0xFF;
    out->value.v_oid.bytes[3] = unix_time & 0xFF;
}

/* --------------------------------------------------------------------------------------------- */

/* Format @n into "NNN.NNN.NNN", zero-padded to @width digits. */
static void
mongo_pos_format (gint64 n, int width, char *buf, gsize buflen)
{
    char raw[32];
    int len, src, dst, leading;

    g_snprintf (raw, sizeof (raw), "%0*" G_GINT64_FORMAT, width, n);
    len = (int) strlen (raw);
    leading = len % 3;
    dst = 0;
    for (src = 0; src < len; src++)
    {
        if (src > 0 && ((src - leading) % 3 == 0))
            if ((gsize) dst + 1 < buflen)
                buf[dst++] = '.';
        if ((gsize) dst + 1 < buflen)
            buf[dst++] = raw[src];
    }
    if ((gsize) dst < buflen)
        buf[dst] = '\0';
    else if (buflen > 0)
        buf[buflen - 1] = '\0';
}

/* --------------------------------------------------------------------------------------------- */

/* Max docs a flat list may hold (2 * leaf_size). Filtered views are capped
   here since we cannot cheaply pre-count filtered results. */
gint64
mongo_flat_cap (const mongo_data_t *data)
{
    int leaf = (data != NULL && data->cfg != NULL && data->cfg->bucket_leaf_size > 0)
        ? data->cfg->bucket_leaf_size
        : 1000;
    return (gint64) (2 * leaf);
}

/* Top bucket's count, or the whole-collection count if the stack is empty. */
gint64
mongo_scope_count (const mongo_data_t *data)
{
    if (data == NULL)
        return -1;
    if (data->bucket_stack != NULL && data->bucket_stack->len > 0)
    {
        const mongo_bucket_t *top =
            &g_array_index (data->bucket_stack, mongo_bucket_t, data->bucket_stack->len - 1);
        return top->count;
    }
    return data->coll_total_count;
}

/* --------------------------------------------------------------------------------------------- */

/* TRUE when @scope_count exceeds 2 * leaf_size. */
gboolean
mongo_should_split (const mongo_data_t *data, gint64 scope_count)
{
    int leaf;

    if (data == NULL || data->cfg == NULL)
        return FALSE;
    if (data->cfg->bucket_strategy == MONGO_BUCKET_NONE)
        return FALSE;
    leaf = data->cfg->bucket_leaf_size > 0 ? data->cfg->bucket_leaf_size : 1000;
    return scope_count > (gint64) (2 * leaf);
}

/* --------------------------------------------------------------------------------------------- */

/* min(cfg.fanout, ceil(count / leaf)). */
static gint64
mongo_pick_fanout (const mongo_data_t *data, gint64 scope_count)
{
    gint64 cfg_fanout;
    gint64 leaf;
    gint64 by_count;

    cfg_fanout = data != NULL && data->cfg != NULL && data->cfg->bucket_fanout > 0
        ? data->cfg->bucket_fanout
        : 10;
    leaf = data != NULL && data->cfg != NULL && data->cfg->bucket_leaf_size > 0
        ? data->cfg->bucket_leaf_size
        : 1000;
    if (scope_count <= 0)
        return cfg_fanout;
    by_count = (scope_count + leaf - 1) / leaf;
    if (by_count < 2)
        by_count = 2;
    return MIN (cfg_fanout, by_count);
}

/* --------------------------------------------------------------------------------------------- */

/* Build bucket_view via $bucketAuto with positional labels ("000.001 ..
   000.870") derived from cumulative counts. */
static gboolean
mongo_build_positional_bucket_view (mongo_data_t *data)
{
    GArray *raw;
    char *err = NULL;
    gint64 scope_count;
    gint64 fanout;
    GArray *new_view;
    gint64 cumulative_pos;
    int label_width;
    guint i;

    if (data == NULL || data->client == NULL || data->db_name == NULL || data->coll_name == NULL)
        return FALSE;

    scope_count = mongo_scope_count (data);
    fanout = mongo_pick_fanout (data, scope_count);

    raw = mongo_conn_bucket_auto (data->client, data->db_name, data->coll_name,
                                  mongo_current_lo (data), mongo_current_hi (data), NULL, fanout,
                                  &err);
    if (raw == NULL)
    {
        char *text =
            g_strdup_printf (_ ("Cannot build bucket view: %s"), err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        return FALSE;
    }

    /* Padding width: digits in scope_count, rounded up to a multiple of 3. */
    {
        gint64 n = scope_count > 0 ? scope_count : 1;
        label_width = 0;
        while (n > 0)
        {
            label_width++;
            n /= 10;
        }
        while (label_width % 3 != 0)
            label_width++;
        if (label_width < 3)
            label_width = 3;
    }

    new_view = g_array_new (FALSE, FALSE, sizeof (mongo_bucket_t));
    g_array_set_clear_func (new_view, mongo_bucket_clear);

    cumulative_pos = 0;
    for (i = 0; i < raw->len; i++)
    {
        const mongo_bucket_info_t *src = &g_array_index (raw, mongo_bucket_info_t, i);
        mongo_bucket_t b = { 0 };
        gint64 cnt = src->count > 0 ? src->count : 0;

        bson_value_copy (&src->lo, &b.lo);
        b.has_lo = TRUE;
        if (src->has_hi)
        {
            bson_value_copy (&src->hi, &b.hi);
            b.has_hi = TRUE;
        }
        b.count = src->count;
        b.start_pos = cumulative_pos + 1;

        {
            char start_buf[32];
            char end_buf[32];
            mongo_pos_format (b.start_pos, label_width, start_buf, sizeof (start_buf));
            mongo_pos_format (cumulative_pos + cnt, label_width, end_buf, sizeof (end_buf));
            b.label = g_strdup_printf ("%s .. %s", start_buf, end_buf);
        }

        cumulative_pos += cnt;
        g_array_append_val (new_view, b);
    }
    g_array_free (raw, TRUE);

    mongo_bucket_view_free (data);
    data->bucket_view = new_view;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Append [lo, hi) to @view if non-empty. */
static void
mongo_time_bucket_append (mongo_data_t *data, GArray *view, gint64 start_unix, gint64 end_unix,
                          const char *label)
{
    bson_value_t lo_v, hi_v;
    gint64 count = -1;
    mongo_bucket_t b = { 0 };

    mongo_oid_value_at_unix (start_unix, &lo_v);
    mongo_oid_value_at_unix (end_unix, &hi_v);

    (void) mongo_conn_count_range (data->client, data->db_name, data->coll_name, &lo_v, &hi_v, NULL,
                                   &count, NULL);
    if (count <= 0)
    {
        bson_value_destroy (&lo_v);
        bson_value_destroy (&hi_v);
        return;
    }
    bson_value_copy (&lo_v, &b.lo);
    b.has_lo = TRUE;
    bson_value_copy (&hi_v, &b.hi);
    b.has_hi = TRUE;
    bson_value_destroy (&lo_v);
    bson_value_destroy (&hi_v);
    b.count = count;
    b.label = g_strdup (label);
    g_array_append_val (view, b);
}

/* --------------------------------------------------------------------------------------------- */

/* Enumerate time buckets at @depth (0=years, 1=months, 2=days). */
static GArray *
mongo_enumerate_time_buckets (mongo_data_t *data, int depth)
{
    bson_value_t min_id, max_id;
    gboolean found = FALSE;
    char *err = NULL;
    struct tm tm_min, tm_max;
    GArray *view;

    if (!mongo_conn_min_max_id (data->client, data->db_name, data->coll_name,
                                mongo_current_lo (data), mongo_current_hi (data), &min_id, &max_id,
                                &found, &err))
    {
        g_free (err);
        return NULL;
    }
    if (!found || min_id.value_type != BSON_TYPE_OID || max_id.value_type != BSON_TYPE_OID)
    {
        if (found)
        {
            bson_value_destroy (&min_id);
            bson_value_destroy (&max_id);
        }
        return NULL;
    }
    {
        time_t t_min = (time_t) mongo_oid_unix (&min_id.value.v_oid);
        time_t t_max = (time_t) mongo_oid_unix (&max_id.value.v_oid);
        gmtime_r (&t_min, &tm_min);
        gmtime_r (&t_max, &tm_max);
    }
    bson_value_destroy (&min_id);
    bson_value_destroy (&max_id);

    view = g_array_new (FALSE, FALSE, sizeof (mongo_bucket_t));
    g_array_set_clear_func (view, mongo_bucket_clear);

    if (depth == 0)
    {
        int y;
        for (y = tm_min.tm_year + 1900; y <= tm_max.tm_year + 1900; y++)
        {
            struct tm s, e;
            char label[16];
            memset (&s, 0, sizeof (s));
            memset (&e, 0, sizeof (e));
            s.tm_year = y - 1900;
            s.tm_mday = 1;
            e.tm_year = (y + 1) - 1900;
            e.tm_mday = 1;
            g_snprintf (label, sizeof (label), "%04d", y);
            mongo_time_bucket_append (data, view, (gint64) timegm (&s), (gint64) timegm (&e),
                                      label);
        }
    }
    else if (depth == 1)
    {
        int m;
        int year = tm_min.tm_year + 1900;
        for (m = 0; m < 12; m++)
        {
            struct tm s, e;
            char label[16];
            memset (&s, 0, sizeof (s));
            memset (&e, 0, sizeof (e));
            s.tm_year = year - 1900;
            s.tm_mon = m;
            s.tm_mday = 1;
            if (m == 11)
            {
                e.tm_year = (year + 1) - 1900;
                e.tm_mon = 0;
            }
            else
            {
                e.tm_year = year - 1900;
                e.tm_mon = m + 1;
            }
            e.tm_mday = 1;
            g_snprintf (label, sizeof (label), "%04d-%02d", year, m + 1);
            mongo_time_bucket_append (data, view, (gint64) timegm (&s), (gint64) timegm (&e),
                                      label);
        }
    }
    else if (depth == 2)
    {
        int year = tm_min.tm_year + 1900;
        int month = tm_min.tm_mon;
        int d;
        for (d = 1; d <= 31; d++)
        {
            struct tm s, e;
            char label[16];
            time_t t_s, t_e;
            memset (&s, 0, sizeof (s));
            memset (&e, 0, sizeof (e));
            s.tm_year = year - 1900;
            s.tm_mon = month;
            s.tm_mday = d;
            e = s;
            e.tm_mday = d + 1;
            t_s = timegm (&s);
            t_e = timegm (&e);
            if (t_s >= t_e)
                continue;
            {
                struct tm chk;
                time_t tt = t_s;
                gmtime_r (&tt, &chk);
                if (chk.tm_mon != month)
                    break;
            }
            g_snprintf (label, sizeof (label), "%04d-%02d-%02d", year, month + 1, d);
            mongo_time_bucket_append (data, view, (gint64) t_s, (gint64) t_e, label);
        }
    }
    return view;
}

/* --------------------------------------------------------------------------------------------- */

/* TRUE if the stack has at least one user-pushed (non-auto) node. ".." pops
   only when this holds; otherwise it leaves the collection entirely. */
gboolean
mongo_stack_has_user_pushed (const mongo_data_t *data)
{
    guint i;
    if (data == NULL || data->bucket_stack == NULL)
        return FALSE;
    for (i = 0; i < data->bucket_stack->len; i++)
    {
        const mongo_bucket_t *b = &g_array_index (data->bucket_stack, mongo_bucket_t, i);
        if (!b->auto_pushed)
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* Push @src as an auto-collapsed node; transfers ownership of bounds/label. */
static void
mongo_stack_push_auto (mongo_data_t *data, mongo_bucket_t *src)
{
    if (data->bucket_stack == NULL)
    {
        data->bucket_stack = g_array_new (FALSE, FALSE, sizeof (mongo_bucket_t));
        g_array_set_clear_func (data->bucket_stack, mongo_bucket_clear);
    }
    src->auto_pushed = TRUE;
    g_array_append_val (data->bucket_stack, *src);
    memset (src, 0, sizeof (*src));
}

/* --------------------------------------------------------------------------------------------- */

/* Time hierarchy with single-child auto-collapse, positional fallback. */
gboolean
mongo_build_bucket_view (mongo_data_t *data)
{
    int max_time_depth = 3;

    if (data == NULL)
        return FALSE;

    if (!data->id_is_oid)
        return mongo_build_positional_bucket_view (data);

    while (TRUE)
    {
        int depth;
        GArray *view;

        depth = data->bucket_stack != NULL ? (int) data->bucket_stack->len : 0;
        if (depth >= max_time_depth)
            break;

        view = mongo_enumerate_time_buckets (data, depth);
        if (view == NULL)
            return mongo_build_positional_bucket_view (data);
        if (view->len == 0)
        {
            g_array_free (view, TRUE);
            return mongo_build_positional_bucket_view (data);
        }
        /* Auto-collapse single-child year (depth=0) and month (depth=1)
           levels; day level (depth>=2) always commits even if singleton. */
        if (view->len >= 2 || depth >= 2)
        {
            mongo_bucket_view_free (data);
            data->bucket_view = view;
            return TRUE;
        }
        {
            mongo_bucket_t *only = &g_array_index (view, mongo_bucket_t, 0);
            mongo_stack_push_auto (data, only);
            g_array_free (view, TRUE);
        }
    }

    return mongo_build_positional_bucket_view (data);
}

/* --------------------------------------------------------------------------------------------- */

/* FLAT or BUCKETS depending on count. */
mongo_level_t
mongo_choose_collection_level (mongo_data_t *data, gint64 count)
{
    if (data == NULL || data->cfg == NULL)
        return MONGO_LEVEL_DOCS;
    if (!mongo_should_split (data, count))
        return MONGO_LEVEL_DOCS;
    return MONGO_LEVEL_BUCKETS;
}

/* --------------------------------------------------------------------------------------------- */

mongo_bucket_t *
mongo_bucket_view_find (mongo_data_t *data, const char *label)
{
    guint i;
    if (data == NULL || data->bucket_view == NULL || label == NULL)
        return NULL;
    for (i = 0; i < data->bucket_view->len; i++)
    {
        mongo_bucket_t *b = &g_array_index (data->bucket_view, mongo_bucket_t, i);
        if (g_strcmp0 (b->label, label) == 0)
            return b;
    }
    return NULL;
}
