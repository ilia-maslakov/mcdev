/** \file mctree-resolver.h
 *  \brief Header: structured tree content resolver and provider registry
 */

#ifndef MC__MCTREE_RESOLVER_H
#define MC__MCTREE_RESOLVER_H

#include "src/mctree/mctree-model.h"

/*** enums ***************************************************************************************/

typedef enum
{
    MCTREE_CONTENT_UNKNOWN,
    MCTREE_CONTENT_JSON,
    MCTREE_CONTENT_XML,
    MCTREE_CONTENT_YAML
} mctree_content_type_t;

typedef enum
{
    MCTREE_PROVIDER_ENABLED,
    MCTREE_PROVIDER_MISSING  // optional dependency not compiled in
} mctree_provider_state_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct mctree_provider_t mctree_provider_t;

typedef struct
{
    gboolean use_extension;
    gboolean use_magic;
    gboolean use_probe;
    gsize max_parse_size;
    gsize max_depth;
    gsize max_alias_nodes;  // total node budget for YAML alias expansion
    int default_expand_depth;
    gsize scalar_preview_limit;
} mctree_resolver_config_t;

typedef struct
{
    mctree_content_type_t content_type;
    const mctree_provider_t *provider;
    char *diagnostic;
    gboolean too_large;
} mctree_resolver_result_t;

struct mctree_provider_t
{
    mctree_content_type_t content_type;
    const char *name;
    mctree_provider_state_t state;
    gboolean (*probe) (const unsigned char *data, gsize len);
    mctree_model_t *(*parse) (const unsigned char *data, gsize len,
                              const mctree_resolver_config_t *config, GError **error);
};

/*** declarations of public functions ************************************************************/

void mctree_resolver_config_init (mctree_resolver_config_t *config);
void mctree_resolver_result_clear (mctree_resolver_result_t *result);

const mctree_provider_t *mctree_provider_for_type (mctree_content_type_t type);
const char *mctree_content_type_name (mctree_content_type_t type);

mctree_model_t *mctree_resolve_file (const char *path, const mctree_resolver_config_t *config,
                                     mctree_resolver_result_t *result, GError **error);

#endif
