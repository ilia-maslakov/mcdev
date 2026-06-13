/*
   MongoDB panel plugin -- panel column definitions and cell values.

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

#include <bson/bson.h>

#include "lib/global.h"
#include "lib/panel-plugin.h"  // mc_panel_column_t

#include "mongo_internal.h"
#include "mongo_render.h"

/*** file scope variables ************************************************************************/

static const mc_panel_column_t mongo_docs_columns[] = {
    { "id_render", "_id", 24, TRUE, J_LEFT_FIT, TRUE },
    { "id_type", "T", 3, FALSE, J_CENTER, TRUE },
};

static const mc_panel_column_t mongo_buckets_columns[] = {
    { "bucket_label", "_id range", 32, TRUE, J_LEFT_FIT, TRUE },
    { "bucket_count", "Count", 10, FALSE, J_RIGHT, TRUE },
};

/*** public functions ****************************************************************************/

const mc_panel_column_t *
mongo_get_columns (void *plugin_data, gsize *count)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;

    if (data == NULL)
    {
        if (count != NULL)
            *count = 0;
        return NULL;
    }
    if (data->level == MONGO_LEVEL_DOCS)
    {
        if (count != NULL)
            *count = G_N_ELEMENTS (mongo_docs_columns);
        return mongo_docs_columns;
    }
    if (data->level == MONGO_LEVEL_BUCKETS)
    {
        if (count != NULL)
            *count = G_N_ELEMENTS (mongo_buckets_columns);
        return mongo_buckets_columns;
    }
    if (count != NULL)
        *count = 0;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
mongo_get_column_value (void *plugin_data, const char *fname, const char *column_id)
{
    static char value_buf[128];
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    int slot;
    const bson_value_t *v;

    if (data == NULL || fname == NULL || column_id == NULL)
        return "";

    if (data->level == MONGO_LEVEL_BUCKETS)
    {
        if (strcmp (fname, "..") == 0)
        {
            if (strcmp (column_id, "bucket_label") == 0)
                return "..";
            return "";
        }
        if (strncmp (fname, "#bucket:", 8) != 0 || data->bucket_view == NULL)
            return "";
        {
            const char *label = fname + 8;
            guint i;
            for (i = 0; i < data->bucket_view->len; i++)
            {
                const mongo_bucket_t *b = &g_array_index (data->bucket_view, mongo_bucket_t, i);
                if (g_strcmp0 (b->label, label) != 0)
                    continue;
                if (strcmp (column_id, "bucket_label") == 0)
                {
                    g_snprintf (value_buf, sizeof (value_buf), "/%s", b->label);
                    return value_buf;
                }
                if (strcmp (column_id, "bucket_count") == 0)
                {
                    if (b->count < 0)
                        g_strlcpy (value_buf, "-", sizeof (value_buf));
                    else
                        g_snprintf (value_buf, sizeof (value_buf), "%" G_GINT64_FORMAT, b->count);
                    return value_buf;
                }
                break;
            }
            return "";
        }
    }

    if (data->level != MONGO_LEVEL_DOCS || data->doc_ids == NULL)
        return "";

    if (strcmp (fname, "..") == 0)
    {
        if (strcmp (column_id, "id_render") == 0)
            return "..";
        return "";
    }

    slot = mongo_slot_from_fname (data, fname);
    if (slot < 0 || (guint) slot >= data->doc_ids->len)
        return "";

    v = &g_array_index (data->doc_ids, bson_value_t, slot);

    if (strcmp (column_id, "id_render") == 0)
    {
        mongo_render_value (v, value_buf, sizeof (value_buf));
        return value_buf;
    }
    if (strcmp (column_id, "id_type") == 0)
    {
        mongo_render_type (v, value_buf, sizeof (value_buf));
        return value_buf;
    }
    return "";
}

/* --------------------------------------------------------------------------------------------- */

const char *
mongo_get_default_format (void *plugin_data)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    if (data != NULL && data->level == MONGO_LEVEL_DOCS)
        return "half id_render | id_type";
    if (data != NULL && data->level == MONGO_LEVEL_BUCKETS)
        return "half bucket_label | bucket_count";
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
