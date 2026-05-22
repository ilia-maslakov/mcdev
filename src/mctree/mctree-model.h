/** \file mctree-model.h
 *  \brief Header: structured tree model for document-like content
 */

#ifndef MC__MCTREE_MODEL_H
#define MC__MCTREE_MODEL_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define MCTREE_DEFAULT_SCALAR_PREVIEW_LIMIT 160

/*** enums ***************************************************************************************/

typedef enum
{
    MCTREE_NODE_ROOT,
    MCTREE_NODE_OBJECT,
    MCTREE_NODE_ARRAY,
    MCTREE_NODE_FIELD,
    MCTREE_NODE_ITEM,
    MCTREE_NODE_SCALAR,
    MCTREE_NODE_ELEMENT,
    MCTREE_NODE_ATTRIBUTE,
    MCTREE_NODE_TEXT
} mctree_node_type_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct mctree_node_t mctree_node_t;
typedef struct mctree_model_t mctree_model_t;

typedef struct
{
    mctree_node_t *node;
    int depth;
} mctree_visible_row_t;

struct mctree_node_t
{
    mctree_node_type_t type;
    char *key;
    char *value;
    gsize original_value_len;
    gboolean value_truncated;
    gboolean expanded;
    mctree_node_t *parent;
    GPtrArray *children;
};

struct mctree_model_t
{
    GPtrArray *nodes;
    mctree_node_t *root;
    gsize scalar_preview_limit;
};

/*** declarations of public functions ************************************************************/

mctree_model_t *mctree_model_new (gsize scalar_preview_limit);
void mctree_model_free (mctree_model_t *model);

mctree_node_t *mctree_model_add_node (mctree_model_t *model, mctree_node_t *parent,
                                      mctree_node_type_t type, const char *key,
                                      const char *value);

void mctree_model_expand_to_depth (mctree_model_t *model, int depth);
GArray *mctree_model_build_visible_rows (const mctree_model_t *model);

guint mctree_node_child_count (const mctree_node_t *node);
guint mctree_node_descendant_count (const mctree_node_t *node);
const char *mctree_node_type_name (mctree_node_type_t type);

#endif
