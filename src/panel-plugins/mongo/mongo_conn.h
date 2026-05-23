/** \file mongo_conn.h
 *  \brief MongoDB plugin: libmongoc wrapper.
 */

#ifndef MC_PANEL_MONGO_CONN_H
#define MC_PANEL_MONGO_CONN_H

#include "lib/global.h"

#include "mongo_config.h"

struct _mongoc_client_t;
typedef struct _mongoc_client_t mongoc_client_t;

/*** API ***************************************************************************************/

void mongo_conn_global_init (void);

/* Connect with @cfg timeouts, ping the deployment. */
mongoc_client_t *mongo_conn_connect (const mongo_cluster_t *cluster, const mongo_config_t *cfg,
                                     char **err_out);

void mongo_conn_disconnect (mongoc_client_t *client);

/* Sorted database names, excluding admin/local/config. Caller g_strfreev. */
char **mongo_conn_list_databases (mongoc_client_t *client, char **err_out);

typedef struct
{
    char *name;
    gint64 count; /* -1 on per-collection count failure */
} mongo_coll_info_t;

/* Sorted collections with estimated_document_count. */
GArray *mongo_conn_list_collections (mongoc_client_t *client, const char *db_name, char **err_out);

struct _bson_t;
typedef struct _bson_t bson_t;
struct _bson_value_t;
typedef struct _bson_value_t bson_value_t;

/* Page within [@lo, @hi); @filter_extra AND-merged into match; @sort_override
   replaces default {_id:1} when non-empty. @has_more TRUE if page is full. */
GArray *mongo_conn_list_documents (mongoc_client_t *client, const char *db_name,
                                   const char *coll_name, const bson_value_t *lo,
                                   const bson_value_t *hi, const bson_t *filter_extra,
                                   const bson_t *sort_override, gint64 skip, gint64 limit,
                                   gboolean *has_more, char **err_out);

/* findOne({_id: @id_value}) with optional projection; caller bson_destroy. */
bson_t *mongo_conn_get_document (mongoc_client_t *client, const char *db_name,
                                 const char *coll_name, const bson_value_t *id_value,
                                 const bson_t *projection_override, char **err_out);

/* replaceOne({_id: @original_id}, @replacement). Refuses locally if
   @replacement's _id differs from @original_id. */
gboolean mongo_conn_replace_document (mongoc_client_t *client, const char *db_name,
                                      const char *coll_name, const bson_value_t *original_id,
                                      const bson_t *replacement, char **err_out);

/* deleteMany({_id: {$in: [ids...]}}). */
gboolean mongo_conn_delete_documents (mongoc_client_t *client, const char *db_name,
                                      const char *coll_name, const bson_value_t *const *ids,
                                      gsize count, gint64 *deleted_out, char **err_out);

/* insertOne; libmongoc generates _id if missing. */
gboolean mongo_conn_insert_document (mongoc_client_t *client, const char *db_name,
                                     const char *coll_name, const bson_t *doc, char **err_out);

/* countDocuments over [@lo, @hi) plus optional AND-ed @filter_extra. */
gboolean mongo_conn_count_range (mongoc_client_t *client, const char *db_name,
                                 const char *coll_name, const bson_value_t *lo,
                                 const bson_value_t *hi, const bson_t *filter_extra,
                                 gint64 *count_out, char **err_out);

typedef struct
{
    bson_value_t lo;
    bson_value_t hi;
    gboolean has_hi;
    gint64 count;
} mongo_bucket_info_t;

/* $bucketAuto over [@lo, @hi) AND @filter_extra; sorted ascending by lo. */
GArray *mongo_conn_bucket_auto (mongoc_client_t *client, const char *db_name, const char *coll_name,
                                const bson_value_t *lo, const bson_value_t *hi,
                                const bson_t *filter_extra, gint64 target_fanout, char **err_out);

/* Sample one _id's type to pick bucketing strategy. */
gboolean mongo_conn_sample_id_type (mongoc_client_t *client, const char *db_name,
                                    const char *coll_name, bson_type_t *type_out, char **err_out);

/* min/max _id inside [@lo, @hi); @found_out FALSE if empty. */
gboolean mongo_conn_min_max_id (mongoc_client_t *client, const char *db_name, const char *coll_name,
                                const bson_value_t *lo, const bson_value_t *hi,
                                bson_value_t *min_out, bson_value_t *max_out, gboolean *found_out,
                                char **err_out);

/* Fetch one document from [@lo, @hi) AND @filter_extra, sorted by _id.
   Returns NULL with @err_out set on error, or NULL with @err_out NULL when
   the scope is empty. Caller bson_destroy. */
bson_t *mongo_conn_sample_one_doc (mongoc_client_t *client, const char *db_name,
                                   const char *coll_name, const bson_value_t *lo,
                                   const bson_value_t *hi, const bson_t *filter_extra,
                                   char **err_out);

#endif
