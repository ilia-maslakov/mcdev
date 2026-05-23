/*
   MongoDB panel plugin -- navigation: cluster/db/collection/bucket/doc levels.

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
#include "lib/panel-plugin.h"  // mc_pp_add_entry, mc_pp_result_t

#include "mongo_internal.h"
#include "mongo_conn.h"
#include "mongo_config.h"
#include "mongo_filter.h"
#include "mongo_ui.h"

/*** file scope functions ************************************************************************/

static gboolean
mongo_enter_cluster (mongo_data_t *data, const char *cluster_name)
{
    const mongo_cluster_t *c;
    mongoc_client_t *client;
    char **dbs;
    char *err = NULL;
    mongo_status_t st;
    char *stage_msg;

    c = mongo_config_find_cluster (data->cfg, cluster_name);
    if (c == NULL)
    {
        char *text = g_strdup_printf (_ ("Cluster '%s' not found in mongo.ini."), cluster_name);
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        return FALSE;
    }

    mongo_status_open (&st, _ ("MongoDB"));
    stage_msg = g_strdup_printf (_ ("Connecting to '%s'..."), cluster_name);
    mongo_status_set (&st, stage_msg);
    g_free (stage_msg);

    client = mongo_conn_connect (c, data->cfg, &err);
    if (client == NULL)
    {
        char *text;
        mongo_status_close (&st);
        text = g_strdup_printf (_ ("Cannot connect to '%s': %s"), cluster_name,
                                err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        return FALSE;
    }

    mongo_status_set (&st, _ ("Listing databases..."));
    dbs = mongo_conn_list_databases (client, &err);
    mongo_status_close (&st);
    if (dbs == NULL)
    {
        char *text =
            g_strdup_printf (_ ("Cannot list databases: %s"), err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        mongo_conn_disconnect (client);
        return FALSE;
    }

    mongo_session_free (data);
    data->cluster = c;
    data->client = client;
    data->db_names = dbs;
    data->level = MONGO_LEVEL_DBS;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mongo_enter_database (mongo_data_t *data, const char *db_name)
{
    GArray *colls;
    char *err = NULL;
    mongo_status_t st;
    char *stage_msg;

    if (data->client == NULL)
    {
        mongo_show_message (data->host, TRUE, _ ("Not connected to a cluster."));
        return FALSE;
    }

    mongo_status_open (&st, _ ("MongoDB"));
    stage_msg = g_strdup_printf (_ ("Listing collections in '%s'..."), db_name);
    mongo_status_set (&st, stage_msg);
    g_free (stage_msg);

    colls = mongo_conn_list_collections (data->client, db_name, &err);
    mongo_status_close (&st);
    if (colls == NULL)
    {
        char *text = g_strdup_printf (_ ("Cannot list collections in '%s': %s"), db_name,
                                      err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        return FALSE;
    }

    mongo_colls_free (data);
    data->db_name = g_strdup (db_name);
    data->colls = colls;
    data->level = MONGO_LEVEL_COLLS;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Load every document inside the current scope as a single flat list. */
gboolean
mongo_load_page (mongo_data_t *data, const char *coll_name, gint64 skip)
{
    GArray *ids;
    gboolean has_more = FALSE;
    char *err = NULL;
    gint64 scope_count;
    gint64 fetch_limit;

    (void) skip;

    if (data->client == NULL || data->db_name == NULL || coll_name == NULL)
    {
        mongo_show_message (data->host, TRUE, _ ("Not inside a database."));
        return FALSE;
    }

    scope_count = mongo_scope_count (data);
    fetch_limit = mongo_flat_cap (data) + 1;
    if (data->filter != NULL && data->filter->limit > 0)
        fetch_limit = data->filter->limit;
    else if (scope_count > 0 && scope_count < fetch_limit)
        fetch_limit = scope_count;

    {
        mongo_status_t st;
        char *stage_msg;
        const bson_t *fextra = data->filter != NULL ? data->filter->filter : NULL;
        const bson_t *fsort = data->filter != NULL ? data->filter->sort : NULL;

        mongo_status_open (&st, _ ("MongoDB"));
        stage_msg = g_strdup_printf (_ ("Loading %s (%" G_GINT64_FORMAT " docs)..."), coll_name,
                                     fetch_limit);
        mongo_status_set (&st, stage_msg);
        g_free (stage_msg);

        ids = mongo_conn_list_documents (data->client, data->db_name, coll_name,
                                         mongo_current_lo (data), mongo_current_hi (data), fextra,
                                         fsort, 0, fetch_limit, &has_more, &err);
        mongo_status_close (&st);
    }
    if (ids == NULL)
    {
        char *text = g_strdup_printf (_ ("Cannot list documents in '%s': %s"), coll_name,
                                      err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        return FALSE;
    }

    if (data->coll_name == NULL || strcmp (data->coll_name, coll_name) != 0)
    {
        g_free (data->coll_name);
        data->coll_name = g_strdup (coll_name);
    }
    if (data->doc_ids != NULL)
        g_array_free (data->doc_ids, TRUE);
    data->doc_ids = ids;
    data->filter_truncated = data->filter != NULL && has_more;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_enter_collection (mongo_data_t *data, const char *coll_name)
{
    gint64 count = -1;
    char *err = NULL;
    mongo_level_t target;
    mongo_status_t st;
    char *stage_msg;

    mongo_docs_free (data);
    mongo_bucket_stack_free (data);
    mongo_filter_drop (data);

    g_free (data->coll_name);
    data->coll_name = g_strdup (coll_name);

    mongo_status_open (&st, _ ("MongoDB"));
    stage_msg = g_strdup_printf (_ ("Counting documents in '%s'..."), coll_name);
    mongo_status_set (&st, stage_msg);
    g_free (stage_msg);

    if (!mongo_conn_count_range (data->client, data->db_name, coll_name, NULL, NULL, NULL, &count,
                                 &err))
    {
        mongo_status_close (&st);
        {
            char *text = g_strdup_printf (_ ("Cannot count documents in '%s': %s"), coll_name,
                                          err != NULL ? err : "unknown");
            mongo_show_message (data->host, TRUE, text);
            g_free (text);
            g_free (err);
        }
        g_free (data->coll_name);
        data->coll_name = NULL;
        return FALSE;
    }

    data->coll_total_count = count;

    {
        bson_type_t t = BSON_TYPE_EOD;
        data->id_is_oid =
            mongo_conn_sample_id_type (data->client, data->db_name, coll_name, &t, NULL)
            && t == BSON_TYPE_OID;
    }

    target = mongo_choose_collection_level (data, count);

    if (target == MONGO_LEVEL_DOCS)
    {
        mongo_status_close (&st);
        if (!mongo_load_page (data, coll_name, 0))
            return FALSE;
        data->level = MONGO_LEVEL_DOCS;
        return TRUE;
    }

    mongo_status_set (&st, _ ("Building bucket view..."));
    if (!mongo_build_bucket_view (data))
    {
        mongo_status_close (&st);
        g_free (data->coll_name);
        data->coll_name = NULL;
        return FALSE;
    }
    mongo_status_close (&st);
    data->level = MONGO_LEVEL_BUCKETS;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Push @label's bucket onto the stack and deepen or land on docs. */
static gboolean
mongo_enter_bucket (mongo_data_t *data, const char *label)
{
    mongo_bucket_t *src;
    mongo_bucket_t pushed = { 0 };

    src = mongo_bucket_view_find (data, label);
    if (src == NULL)
    {
        char *text = g_strdup_printf (_ ("Bucket '%s' not found."), label);
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        return FALSE;
    }

    bson_value_copy (&src->lo, &pushed.lo);
    pushed.has_lo = TRUE;
    if (src->has_hi)
    {
        bson_value_copy (&src->hi, &pushed.hi);
        pushed.has_hi = TRUE;
    }
    pushed.count = src->count;
    pushed.label = g_strdup (src->label);

    if (data->bucket_stack == NULL)
    {
        data->bucket_stack = g_array_new (FALSE, FALSE, sizeof (mongo_bucket_t));
        g_array_set_clear_func (data->bucket_stack, mongo_bucket_clear);
    }
    g_array_append_val (data->bucket_stack, pushed);

    if (!mongo_should_split (data, pushed.count))
    {
        mongo_bucket_view_free (data);
        if (!mongo_load_page (data, data->coll_name, 0))
            return FALSE;
        data->level = MONGO_LEVEL_DOCS;
        return TRUE;
    }

    if (!mongo_build_bucket_view (data))
        return FALSE;
    if (data->bucket_view == NULL || data->bucket_view->len < 2)
    {
        mongo_bucket_view_free (data);
        if (!mongo_load_page (data, data->coll_name, 0))
            return FALSE;
        data->level = MONGO_LEVEL_DOCS;
        return TRUE;
    }
    data->level = MONGO_LEVEL_BUCKETS;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Pop the user-pushed top plus any auto_pushed nodes beneath it, so ".."
   undoes the whole single-child collapse chain at once. */
static gboolean
mongo_pop_bucket (mongo_data_t *data)
{
    char *focus = NULL;

    if (data == NULL || data->bucket_stack == NULL || data->bucket_stack->len == 0)
        return FALSE;

    {
        mongo_bucket_t *top =
            &g_array_index (data->bucket_stack, mongo_bucket_t, data->bucket_stack->len - 1);
        focus = g_strdup (top->label);
        g_array_remove_index (data->bucket_stack, data->bucket_stack->len - 1);
    }
    while (data->bucket_stack->len > 0)
    {
        mongo_bucket_t *top =
            &g_array_index (data->bucket_stack, mongo_bucket_t, data->bucket_stack->len - 1);
        if (!top->auto_pushed)
            break;
        g_array_remove_index (data->bucket_stack, data->bucket_stack->len - 1);
    }

    if (!mongo_build_bucket_view (data))
    {
        g_free (focus);
        return FALSE;
    }
    data->level = MONGO_LEVEL_BUCKETS;
    g_free (data->focus_after_up);
    data->focus_after_up = focus;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
mongo_get_items (void *plugin_data, void *list_ptr)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    guint i;

    if (data == NULL || data->cfg == NULL)
        return MC_PPR_FAILED;

    switch (data->level)
    {
    case MONGO_LEVEL_CLUSTERS:
        for (i = 0; i < data->cfg->clusters->len; i++)
        {
            const mongo_cluster_t *c =
                (const mongo_cluster_t *) g_ptr_array_index (data->cfg->clusters, i);
            mc_pp_add_entry (list_ptr, c->name, S_IFDIR | 0755, 0, time (NULL));
        }
        return MC_PPR_OK;

    case MONGO_LEVEL_DBS:
        if (data->db_names != NULL)
            for (char **n = data->db_names; *n != NULL; n++)
                mc_pp_add_entry (list_ptr, *n, S_IFDIR | 0755, 0, time (NULL));
        return MC_PPR_OK;

    case MONGO_LEVEL_COLLS:
        if (data->colls != NULL)
            for (i = 0; i < data->colls->len; i++)
            {
                const mongo_coll_info_t *ci = &g_array_index (data->colls, mongo_coll_info_t, i);
                off_t size = (ci->count >= 0) ? (off_t) ci->count : 0;
                mc_pp_add_entry (list_ptr, ci->name, S_IFDIR | 0755, size, time (NULL));
            }
        return MC_PPR_OK;

    case MONGO_LEVEL_BUCKETS:
        if (data->bucket_view != NULL)
            for (i = 0; i < data->bucket_view->len; i++)
            {
                const mongo_bucket_t *b = &g_array_index (data->bucket_view, mongo_bucket_t, i);
                char fname[128];
                off_t size = (b->count >= 0) ? (off_t) b->count : 0;
                g_snprintf (fname, sizeof (fname), "#bucket:%s", b->label);
                mc_pp_add_entry (list_ptr, fname, S_IFDIR | 0755, size, time (NULL));
            }
        return MC_PPR_OK;

    case MONGO_LEVEL_DOCS:
        if (data->doc_ids != NULL)
            for (i = 0; i < data->doc_ids->len; i++)
            {
                char slot_fname[32];
                g_snprintf (slot_fname, sizeof (slot_fname), "doc:%05u", i);
                mc_pp_add_entry (list_ptr, slot_fname, S_IFREG | 0644, 0, time (NULL));
            }
        return MC_PPR_OK;

    default:
        return MC_PPR_FAILED;
    }
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
mongo_chdir (void *plugin_data, const char *path)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;

    if (data == NULL || path == NULL || path[0] == '\0')
        return MC_PPR_FAILED;

    if (strcmp (path, "..") == 0)
    {
        switch (data->level)
        {
        case MONGO_LEVEL_CLUSTERS:
            return MC_PPR_CLOSE;

        case MONGO_LEVEL_DBS:
        {
            char *name_to_focus = data->cluster != NULL ? g_strdup (data->cluster->name) : NULL;
            mongo_session_free (data);
            data->level = MONGO_LEVEL_CLUSTERS;
            g_free (data->focus_after_up);
            data->focus_after_up = name_to_focus;
            return MC_PPR_OK;
        }

        case MONGO_LEVEL_COLLS:
        {
            char *name_to_focus = data->db_name != NULL ? g_strdup (data->db_name) : NULL;
            mongo_colls_free (data);
            data->level = MONGO_LEVEL_DBS;
            g_free (data->focus_after_up);
            data->focus_after_up = name_to_focus;
            return MC_PPR_OK;
        }

        case MONGO_LEVEL_BUCKETS:
        {
            if (mongo_stack_has_user_pushed (data))
            {
                if (!mongo_pop_bucket (data))
                    return MC_PPR_FAILED;
                return MC_PPR_OK;
            }
            {
                char *name_to_focus = data->coll_name != NULL ? g_strdup (data->coll_name) : NULL;
                mongo_bucket_stack_free (data);
                mongo_docs_free (data);
                mongo_filter_drop (data);
                g_free (data->coll_name);
                data->coll_name = NULL;
                data->level = MONGO_LEVEL_COLLS;
                g_free (data->focus_after_up);
                data->focus_after_up = name_to_focus;
                return MC_PPR_OK;
            }
        }

        case MONGO_LEVEL_DOCS:
        {
            char *name_to_focus = data->coll_name != NULL ? g_strdup (data->coll_name) : NULL;
            if (mongo_stack_has_user_pushed (data))
            {
                mongo_bucket_t *top = &g_array_index (data->bucket_stack, mongo_bucket_t,
                                                      data->bucket_stack->len - 1);
                g_free (name_to_focus);
                name_to_focus = g_strdup_printf ("#bucket:%s", top->label);
                mongo_docs_free (data);
                if (!mongo_pop_bucket (data))
                {
                    g_free (name_to_focus);
                    return MC_PPR_FAILED;
                }
                g_free (data->focus_after_up);
                data->focus_after_up = name_to_focus;
                return MC_PPR_OK;
            }
            mongo_docs_free (data);
            mongo_filter_drop (data);
            g_free (data->coll_name);
            data->coll_name = NULL;
            data->level = MONGO_LEVEL_COLLS;
            g_free (data->focus_after_up);
            data->focus_after_up = name_to_focus;
            return MC_PPR_OK;
        }

        default:
            return MC_PPR_FAILED;
        }
    }

    switch (data->level)
    {
    case MONGO_LEVEL_CLUSTERS:
        return mongo_enter_cluster (data, path) ? MC_PPR_OK : MC_PPR_FAILED;

    case MONGO_LEVEL_DBS:
        return mongo_enter_database (data, path) ? MC_PPR_OK : MC_PPR_FAILED;

    case MONGO_LEVEL_COLLS:
        return mongo_enter_collection (data, path) ? MC_PPR_OK : MC_PPR_FAILED;

    case MONGO_LEVEL_BUCKETS:
        if (strncmp (path, "#bucket:", 8) == 0)
            return mongo_enter_bucket (data, path + 8) ? MC_PPR_OK : MC_PPR_FAILED;
        return MC_PPR_FAILED;

    case MONGO_LEVEL_DOCS:
        return MC_PPR_FAILED;

    default:
        return MC_PPR_FAILED;
    }
}
