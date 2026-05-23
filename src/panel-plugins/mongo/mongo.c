/*
   MongoDB panel plugin -- cluster/db/collection/document navigation.

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
#include <sys/stat.h>
#include <time.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <bson/bson.h>

#include "lib/global.h"
#include "lib/keybind.h"  // CK_Edit
#include "lib/tty/tty.h"  // COLS
#include "lib/vfs/vfs.h"  // vfs_path_from_str
#include "lib/widget.h"   // simple_status_msg_t, status_msg_init/deinit
#include "lib/panel-plugin.h"

#include "src/viewer/mcviewer.h"  // mcview_viewer

#include "mongo_internal.h"
#include "mongo_conn.h"
#include "mongo_config.h"
#include "mongo_filter.h"
#include "mongo_render.h"
#include "mongo_settings.h"
#include "mongo_ui.h"

/*** forward declarations ************************************************************************/

static void *mongo_open (mc_panel_host_t *host, const char *open_path);
static void mongo_close (void *plugin_data);
static const char *mongo_get_title (void *plugin_data);
static const char *mongo_get_focus_name (void *plugin_data);
static mc_pp_result_t mongo_create_item (void *plugin_data);
static mc_pp_result_t mongo_handle_key (void *plugin_data, int key);
static mc_pp_result_t mongo_get_help_info (void *plugin_data, const char **filename,
                                           const char **node);

const mc_panel_plugin_t *mc_panel_plugin_register (void);

/*** file scope variables ************************************************************************/

static char title_buf[256];

static const mc_panel_plugin_t mongo_plugin = {
    .api_version = MC_PANEL_PLUGIN_API_VERSION,
    .name = "mongo",
    .display_name = N_ ("MongoDB"),
    .proto = "mongo",
    .prefix = "mongo:",
    .flags = MC_PPF_NAVIGATE | MC_PPF_GET_FILES | MC_PPF_PUT_FILES | MC_PPF_DELETE | MC_PPF_CREATE
        | MC_PPF_CUSTOM_TITLE | MC_PPF_SHOW_IN_DRIVE_MENU,

    .open = mongo_open,
    .close = mongo_close,
    .get_items = mongo_get_items,
    .chdir = mongo_chdir,
    .handle_key = mongo_handle_key,

    .get_title = mongo_get_title,
    .get_focus_name = mongo_get_focus_name,
    .get_columns = mongo_get_columns,
    .get_column_value = mongo_get_column_value,
    .get_default_format = mongo_get_default_format,
    .get_local_copy = mongo_get_local_copy,
    .view = mongo_view,
    .save_file = mongo_save_file,
    .put_file = mongo_put_file,
    .delete_items = mongo_delete_items,
    .create_item = mongo_create_item,
    .configure = mongo_configure,
    .get_help_info = mongo_get_help_info,

    .default_sort_id = "name",
};

/*** file scope functions ************************************************************************/

static void *
mongo_open (mc_panel_host_t *host, const char *open_path)
{
    mongo_config_t *cfg;
    mongo_data_t *data;

    (void) open_path;

    if (host == NULL)
        return NULL;

    cfg = mongo_config_load ();
    if (cfg == NULL)
        cfg = mongo_config_new_empty ();

    data = g_new0 (mongo_data_t, 1);
    data->host = host;
    data->cfg = cfg;
    data->level = MONGO_LEVEL_CLUSTERS;

    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void
mongo_close (void *plugin_data)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    if (data == NULL)
        return;
    mongo_session_free (data);
    g_free (data->focus_after_up);
    g_free (data->pending_coll);
    mongo_config_free (data->cfg);
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mongo_get_title (void *plugin_data)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    if (data == NULL)
        return "MongoDB";

    switch (data->level)
    {
    case MONGO_LEVEL_CLUSTERS:
        return "Clusters";
    case MONGO_LEVEL_DBS:
        g_snprintf (title_buf, sizeof (title_buf), "%s: databases",
                    data->cluster != NULL ? data->cluster->name : "(unknown)");
        return title_buf;
    case MONGO_LEVEL_COLLS:
        g_snprintf (title_buf, sizeof (title_buf), "%s/%s: collections",
                    data->cluster != NULL ? data->cluster->name : "(unknown)",
                    data->db_name != NULL ? data->db_name : "(unknown)");
        return title_buf;
    case MONGO_LEVEL_BUCKETS:
    {
        guint depth = data->bucket_stack != NULL ? data->bucket_stack->len : 0;
        guint n = data->bucket_view != NULL ? data->bucket_view->len : 0;
        g_snprintf (title_buf, sizeof (title_buf), "%s/%s/%s: %u bucket%s at depth %u",
                    data->cluster != NULL ? data->cluster->name : "(unknown)",
                    data->db_name != NULL ? data->db_name : "(unknown)",
                    data->coll_name != NULL ? data->coll_name : "(unknown)", n, n == 1 ? "" : "s",
                    depth);
        if (!mongo_filter_is_empty (data->filter))
            g_strlcat (title_buf, " [filtered]", sizeof (title_buf));
        return title_buf;
    }
    case MONGO_LEVEL_DOCS:
    {
        guint n = data->doc_ids != NULL ? data->doc_ids->len : 0;
        const char *flt = !mongo_filter_is_empty (data->filter)
            ? (data->filter_truncated ? " [filtered, limited]" : " [filtered]")
            : "";
        g_snprintf (title_buf, sizeof (title_buf), "%s/%s/%s: %u doc%s%s",
                    data->cluster != NULL ? data->cluster->name : "(unknown)",
                    data->db_name != NULL ? data->db_name : "(unknown)",
                    data->coll_name != NULL ? data->coll_name : "(unknown)", n, n == 1 ? "" : "s",
                    flt);
        return title_buf;
    }
    default:
        return "MongoDB";
    }
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mongo_get_focus_name (void *plugin_data)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    if (data == NULL)
        return NULL;
    if (data->focus_after_up != NULL)
    {
        static char buf[256];
        g_strlcpy (buf, data->focus_after_up, sizeof (buf));
        g_free (data->focus_after_up);
        data->focus_after_up = NULL;
        return buf;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
mongo_create_item (void *plugin_data)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    bson_t *doc;
    char *err = NULL;
    gboolean ok;

    if (data == NULL)
        return MC_PPR_NOT_SUPPORTED;

    if (data->level == MONGO_LEVEL_CLUSTERS)
        return mongo_create_connection (data);

    if (data->level != MONGO_LEVEL_DOCS || data->coll_name == NULL)
        return MC_PPR_NOT_SUPPORTED;

    if (data->cluster != NULL && data->cluster->read_only)
    {
        mongo_show_message (data->host, TRUE, _ ("Cluster is read-only."));
        return MC_PPR_FAILED;
    }

    doc = bson_new ();
    ok = mongo_conn_insert_document (data->client, data->db_name, data->coll_name, doc, &err);
    bson_destroy (doc);

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

static mc_pp_result_t
mongo_get_help_info (void *plugin_data, const char **filename, const char **node)
{
    static const char help_path[] = MC_PLUGIN_DIR "/mongo_panel.hlp";

    (void) plugin_data;
    if (filename != NULL)
        *filename = g_file_test (help_path, G_FILE_TEST_IS_REGULAR) ? help_path : NULL;
    if (node != NULL)
        *node = "[MongoDB Plugin]";
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
mongo_handle_key (void *plugin_data, int key)
{
    mongo_data_t *data = (mongo_data_t *) plugin_data;
    gboolean read_only;

    if (data == NULL)
        return MC_PPR_NOT_SUPPORTED;

    /* F4 on the cluster list edits the selected connection. General plugin
       options live in the Manage Plugins dialog (see mongo_configure). */
    if (key == CK_Edit && data->level == MONGO_LEVEL_CLUSTERS)
        return mongo_edit_connection (data);

    if (key == CK_Move)
    {
        if (data->level == MONGO_LEVEL_COLLS)
        {
            mongo_filter_from_colls (data);
            return MC_PPR_OK;
        }
        if (data->level == MONGO_LEVEL_BUCKETS || data->level == MONGO_LEVEL_DOCS)
        {
            mongo_run_filter_dialog (data);
            return MC_PPR_OK;
        }
        return MC_PPR_NOT_SUPPORTED;
    }

    if (data->level != MONGO_LEVEL_DOCS)
        return MC_PPR_NOT_SUPPORTED;

    read_only = data->cluster != NULL && data->cluster->read_only;
    if (!read_only)
        return MC_PPR_NOT_SUPPORTED;

    if (key == CK_Edit || key == CK_Delete || key == CK_MakeDir)
    {
        mongo_show_message (data->host, FALSE, _ ("Cluster is read-only."));
        return MC_PPR_OK;
    }
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */
/*** plugin entry point **************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *
mc_panel_plugin_register (void)
{
    return &mongo_plugin;
}

/* --------------------------------------------------------------------------------------------- */
