/*
   MongoDB panel plugin -- session/state lifecycle and small helpers.

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

#include <stdlib.h>
#include <string.h>

#include "lib/global.h"

#include "mongo_internal.h"
#include "mongo_conn.h"
#include "mongo_filter.h"

/*** public functions ****************************************************************************/

/* GDestroyNotify for bucket arrays. */
void
mongo_bucket_clear (gpointer p)
{
    mongo_bucket_t *b = (mongo_bucket_t *) p;
    if (b == NULL)
        return;
    if (b->has_lo)
        bson_value_destroy (&b->lo);
    if (b->has_hi)
        bson_value_destroy (&b->hi);
    g_free (b->label);
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_bucket_view_free (mongo_data_t *data)
{
    if (data == NULL || data->bucket_view == NULL)
        return;
    g_array_free (data->bucket_view, TRUE);
    data->bucket_view = NULL;
}

void
mongo_bucket_stack_free (mongo_data_t *data)
{
    if (data == NULL)
        return;
    mongo_bucket_view_free (data);
    if (data->bucket_stack != NULL)
    {
        g_array_free (data->bucket_stack, TRUE);
        data->bucket_stack = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Bounds of the deepest stacked bucket, or NULL if none. */
const bson_value_t *
mongo_current_lo (const mongo_data_t *data)
{
    const mongo_bucket_t *top;
    if (data == NULL || data->bucket_stack == NULL || data->bucket_stack->len == 0)
        return NULL;
    top = &g_array_index (data->bucket_stack, mongo_bucket_t, data->bucket_stack->len - 1);
    return top->has_lo ? &top->lo : NULL;
}

const bson_value_t *
mongo_current_hi (const mongo_data_t *data)
{
    const mongo_bucket_t *top;
    if (data == NULL || data->bucket_stack == NULL || data->bucket_stack->len == 0)
        return NULL;
    top = &g_array_index (data->bucket_stack, mongo_bucket_t, data->bucket_stack->len - 1);
    return top->has_hi ? &top->hi : NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Drop the doc-list snapshot. coll_name stays -- "../" from DOCS may
   need it to rebuild the parent bucket view. */
void
mongo_docs_free (mongo_data_t *data)
{
    if (data == NULL)
        return;
    if (data->doc_ids != NULL)
    {
        g_array_free (data->doc_ids, TRUE);
        data->doc_ids = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_filter_drop (mongo_data_t *data)
{
    if (data == NULL || data->filter == NULL)
        return;
    mongo_filter_free (data->filter);
    data->filter = NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_colls_free (mongo_data_t *data)
{
    if (data == NULL)
        return;
    mongo_docs_free (data);
    mongo_bucket_stack_free (data);
    mongo_filter_drop (data);
    g_free (data->coll_name);
    data->coll_name = NULL;
    if (data->colls != NULL)
    {
        g_array_free (data->colls, TRUE);
        data->colls = NULL;
    }
    g_free (data->db_name);
    data->db_name = NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Parse "doc:NNNNN" into a slot index, or -1. */
int
mongo_slot_from_fname (const char *fname)
{
    char *end;
    long n;

    if (fname == NULL || strncmp (fname, "doc:", 4) != 0)
        return -1;
    n = strtol (fname + 4, &end, 10);
    if (end == fname + 4 || *end != '\0' || n < 0)
        return -1;
    return (int) n;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_is_virtual_fname (const char *fname)
{
    return fname != NULL && fname[0] == '#';
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_session_free (mongo_data_t *data)
{
    if (data == NULL)
        return;
    mongo_colls_free (data);
    if (data->db_names != NULL)
    {
        g_strfreev (data->db_names);
        data->db_names = NULL;
    }
    if (data->client != NULL)
    {
        mongo_conn_disconnect (data->client);
        data->client = NULL;
    }
    data->cluster = NULL;
}

/* --------------------------------------------------------------------------------------------- */
