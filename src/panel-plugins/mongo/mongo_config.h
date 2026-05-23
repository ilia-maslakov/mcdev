/** \file mongo_config.h
 *  \brief MongoDB plugin: ~/.config/mc/mongo.ini reader.
 */

#ifndef MC_PANEL_MONGO_CONFIG_H
#define MC_PANEL_MONGO_CONFIG_H

#include "lib/global.h"

/*** structures ********************************************************************************/

typedef struct
{
    char *name;
    char *uri;
    char *description;
    gboolean read_only;
} mongo_cluster_t;

typedef enum
{
    MONGO_BUCKET_AUTO = 0,
    MONGO_BUCKET_NONE,
} mongo_bucket_strategy_t;

typedef struct
{
    char *default_cluster;
    int page_size;
    int server_selection_timeout_ms;
    int connect_timeout_ms;
    int socket_timeout_ms;
    int op_max_time_ms;

    /* bucket_leaf_size: max docs shown as a flat list (split if count > 2*leaf).
       bucket_fanout: max sub-buckets per level (actual = min(fanout,
       ceil(count/leaf_size))). */
    mongo_bucket_strategy_t bucket_strategy;
    int bucket_leaf_size;
    int bucket_fanout;

    GPtrArray *clusters;
} mongo_config_t;

/*** API ***************************************************************************************/

/* Load $XDG_CONFIG_HOME/mc/mongo.ini; NULL if missing or empty. */
mongo_config_t *mongo_config_load (void);

/* A config with default [General] options and no clusters, used when no
   mongo.ini exists yet so connections can be created from an empty list. */
mongo_config_t *mongo_config_new_empty (void);

void mongo_config_free (mongo_config_t *cfg);

/* Free a single cluster entry (GDestroyNotify for the clusters array). */
void mongo_cluster_free (gpointer p);

const mongo_cluster_t *mongo_config_find_cluster (const mongo_config_t *cfg, const char *name);

/* default_cluster setting, falling back to the first cluster. */
const mongo_cluster_t *mongo_config_default_cluster (const mongo_config_t *cfg);

/* Caller frees. */
char *mongo_config_path (void);

/* Write the [General] options from @cfg back to mongo.ini, preserving the
   existing [Cluster.*] sections. Returns FALSE with @err_out set on failure
   (caller frees). Connection/cluster definitions are not touched. */
gboolean mongo_config_save_general (const mongo_config_t *cfg, char **err_out);

/* Write @c as a [Cluster.<name>] section in mongo.ini, preserving every other
   section. Creates the file (mode 0600) if it does not exist yet. Returns
   FALSE with @err_out set on failure (caller frees). */
gboolean mongo_config_save_cluster (const mongo_cluster_t *c, char **err_out);

/* Remove the [Cluster.<name>] section from mongo.ini. Returns FALSE with
   @err_out set on failure (caller frees). Missing section is not an error. */
gboolean mongo_config_delete_cluster (const char *name, char **err_out);

#endif
