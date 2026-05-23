/** \file mongo_internal.h
 *  \brief MongoDB plugin: shared state types and cross-module prototypes.
 *
 *  Private to the mongo plugin translation units (mongo*.c). The plugin's
 *  modules all operate on a single mutable mongo_data_t; this header is the
 *  shared contract between them.
 */

#ifndef MC_PANEL_MONGO_INTERNAL_H
#define MC_PANEL_MONGO_INTERNAL_H

#include <bson/bson.h>

#include "lib/global.h"
#include "lib/panel-plugin.h"  // mc_panel_host_t, mc_pp_result_t, mc_panel_column_t

#include "mongo_conn.h"    // mongoc_client_t
#include "mongo_config.h"  // mongo_config_t, mongo_cluster_t
#include "mongo_filter.h"  // mongo_filter_t

/*** types ***************************************************************************************/

typedef enum
{
    MONGO_LEVEL_CLUSTERS = 0,
    MONGO_LEVEL_DBS,
    MONGO_LEVEL_COLLS,
    MONGO_LEVEL_BUCKETS,
    MONGO_LEVEL_DOCS,
} mongo_level_t;

#define MONGO_PAGE_SIZE_DEFAULT 1000

/* One node on the path from collection root to the displayed level. */
typedef struct
{
    bson_value_t lo;
    bson_value_t hi;
    gboolean has_hi;
    gboolean has_lo;
    char *label;
    gint64 count;
    gint64 start_pos;
    gboolean auto_pushed; /* pushed by the single-child collapse; ".." pops
                             the whole auto chain at once */
} mongo_bucket_t;

typedef struct
{
    mc_panel_host_t *host;
    mongo_config_t *cfg;
    mongo_level_t level;

    const mongo_cluster_t *cluster;
    mongoc_client_t *client;

    char **db_names;

    char *db_name;
    GArray *colls;

    char *coll_name;
    GArray *doc_ids;

    GArray *bucket_stack;
    GArray *bucket_view;
    gint64 coll_total_count;
    gboolean id_is_oid;

    mongo_filter_t *filter;
    gboolean filter_truncated;
    char *pending_coll; /* transient: collection targeted by F6 at COLLS level */

    char *focus_after_up;
} mongo_data_t;

/*** mongo_session.c *****************************************************************************/

void mongo_bucket_clear (gpointer p);  // GDestroyNotify for bucket arrays
void mongo_bucket_view_free (mongo_data_t *data);
void mongo_bucket_stack_free (mongo_data_t *data);
const bson_value_t *mongo_current_lo (const mongo_data_t *data);
const bson_value_t *mongo_current_hi (const mongo_data_t *data);
void mongo_docs_free (mongo_data_t *data);
void mongo_filter_drop (mongo_data_t *data);
void mongo_colls_free (mongo_data_t *data);
int mongo_slot_from_fname (const char *fname);
gboolean mongo_is_virtual_fname (const char *fname);
void mongo_session_free (mongo_data_t *data);

/*** mongo_nav.c *********************************************************************************/

gboolean mongo_load_page (mongo_data_t *data, const char *coll_name, gint64 skip);
gboolean mongo_enter_collection (mongo_data_t *data, const char *coll_name);
mc_pp_result_t mongo_get_items (void *plugin_data, void *list_ptr);
mc_pp_result_t mongo_chdir (void *plugin_data, const char *path);

/*** mongo_docops.c ******************************************************************************/

mc_pp_result_t mongo_get_local_copy (void *plugin_data, const char *fname, char **local_path);
mc_pp_result_t mongo_view (void *plugin_data, const char *fname, const struct stat *st,
                           gboolean plain_view);
mc_pp_result_t mongo_save_file (void *plugin_data, const char *local_path, const char *remote_name);
mc_pp_result_t mongo_put_file (void *plugin_data, const char *local_path, const char *dest_name);
mc_pp_result_t mongo_delete_items (void *plugin_data, const char **names, int count);
void mongo_reload_current_page (mongo_data_t *data);

/*** mongo_connections.c *************************************************************************/

mc_pp_result_t mongo_create_connection (mongo_data_t *data);
mc_pp_result_t mongo_edit_connection (mongo_data_t *data);
void mongo_configure (void);

/*** mongo_columns.c *****************************************************************************/

const mc_panel_column_t *mongo_get_columns (void *plugin_data, gsize *count);
const char *mongo_get_column_value (void *plugin_data, const char *fname, const char *column_id);
const char *mongo_get_default_format (void *plugin_data);

/*** mongo_bucket.c ******************************************************************************/

gint64 mongo_flat_cap (const mongo_data_t *data);
gint64 mongo_scope_count (const mongo_data_t *data);
gboolean mongo_should_split (const mongo_data_t *data, gint64 scope_count);
mongo_level_t mongo_choose_collection_level (mongo_data_t *data, gint64 count);
gboolean mongo_build_bucket_view (mongo_data_t *data);
mongo_bucket_t *mongo_bucket_view_find (mongo_data_t *data, const char *label);
gboolean mongo_stack_has_user_pushed (const mongo_data_t *data);

/*** mongo_filter.c (panel glue) ****************************************************************/

void mongo_run_filter_dialog (mongo_data_t *data);
void mongo_filter_from_colls (mongo_data_t *data);

#endif
