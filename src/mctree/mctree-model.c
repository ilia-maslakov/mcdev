/*
   Structured tree model for document-like content.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.
 */

#include <config.h>

#include <string.h>

#include "src/mctree/mctree-model.h"

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
mctree_node_free (mctree_node_t *node)
{
    if (node == NULL)
        return;

    g_free (node->key);
    g_free (node->value);
    if (node->children != NULL)
        g_ptr_array_free (node->children, TRUE);
    g_free (node);
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_value_preview (const char *value, gsize limit, gsize *original_len, gboolean *truncated)
{
    gsize len;

    if (original_len != NULL)
        *original_len = 0;
    if (truncated != NULL)
        *truncated = FALSE;

    if (value == NULL)
        return NULL;

    len = strlen (value);
    if (original_len != NULL)
        *original_len = len;

    if (limit == 0 || len <= limit)
        return g_strdup (value);

    if (truncated != NULL)
        *truncated = TRUE;

    if (limit <= 3)
        return g_strndup (value, limit);

    return g_strdup_printf ("%.*s...", (int) (limit - 3), value);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_node_is_transparent_container (const mctree_node_t *node)
{
    if (node == NULL || node->key != NULL || node->value != NULL || node->parent == NULL)
        return FALSE;

    if (node->type != MCTREE_NODE_OBJECT && node->type != MCTREE_NODE_ARRAY)
        return FALSE;

    return node->parent->type == MCTREE_NODE_ROOT || node->parent->type == MCTREE_NODE_FIELD
        || node->parent->type == MCTREE_NODE_ITEM;
}

/* --------------------------------------------------------------------------------------------- */

static void mctree_model_add_visible_transparent_children (const mctree_node_t *node, int depth,
                                                           GArray *rows);
static void mctree_model_add_visible_children (const mctree_node_t *node, int depth, GArray *rows);

static void
mctree_model_add_visible_node (const mctree_node_t *node, int depth, GArray *rows)
{
    mctree_visible_row_t row;

    if (mctree_node_is_transparent_container (node))
    {
        mctree_model_add_visible_transparent_children (node, depth, rows);
        return;
    }

    row.node = (mctree_node_t *) node;
    row.depth = depth;
    g_array_append_val (rows, row);
    mctree_model_add_visible_children (node, depth + 1, rows);
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_model_add_visible_transparent_children (const mctree_node_t *node, int depth, GArray *rows)
{
    guint i;

    if (node == NULL || node->children == NULL)
        return;

    for (i = 0; i < node->children->len; i++)
        mctree_model_add_visible_node (g_ptr_array_index (node->children, i), depth, rows);
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_model_add_visible_children (const mctree_node_t *node, int depth, GArray *rows)
{
    if (node == NULL || node->children == NULL || !node->expanded)
        return;

    mctree_model_add_visible_transparent_children (node, depth, rows);
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_node_expand_to_depth (mctree_node_t *node, int current_depth, int target_depth)
{
    guint i;

    if (node == NULL)
        return;

    node->expanded = current_depth < target_depth;

    if (node->children == NULL)
        return;

    for (i = 0; i < node->children->len; i++)
        mctree_node_expand_to_depth (g_ptr_array_index (node->children, i), current_depth + 1,
                                     target_depth);
}

/* --------------------------------------------------------------------------------------------- */

static guint
mctree_node_descendant_count_int (const mctree_node_t *node)
{
    guint i, count;

    if (node == NULL || node->children == NULL)
        return 0;

    count = 0;
    for (i = 0; i < node->children->len; i++)
    {
        mctree_node_t *child = g_ptr_array_index (node->children, i);

        if (!mctree_node_is_transparent_container (child))
            count++;
        count += mctree_node_descendant_count_int (child);
    }

    return count;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

mctree_model_t *
mctree_model_new (gsize scalar_preview_limit)
{
    mctree_model_t *model;

    model = g_new0 (mctree_model_t, 1);
    model->nodes = g_ptr_array_new_with_free_func ((GDestroyNotify) mctree_node_free);
    model->scalar_preview_limit =
        (scalar_preview_limit == 0) ? MCTREE_DEFAULT_SCALAR_PREVIEW_LIMIT : scalar_preview_limit;

    return model;
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_model_free (mctree_model_t *model)
{
    if (model == NULL)
        return;

    if (model->nodes != NULL)
        g_ptr_array_free (model->nodes, TRUE);
    g_free (model);
}

/* --------------------------------------------------------------------------------------------- */

mctree_node_t *
mctree_model_add_node (mctree_model_t *model, mctree_node_t *parent, mctree_node_type_t type,
                       const char *key, const char *value)
{
    mctree_node_t *node;

    if (model == NULL)
        return NULL;

    node = g_new0 (mctree_node_t, 1);
    node->type = type;
    node->key = g_strdup (key);
    node->value = mctree_value_preview (value, model->scalar_preview_limit,
                                        &node->original_value_len, &node->value_truncated);
    node->expanded = FALSE;
    node->parent = parent;
    node->children = g_ptr_array_new ();

    g_ptr_array_add (model->nodes, node);

    if (parent != NULL)
        g_ptr_array_add (parent->children, node);
    else if (model->root == NULL)
        model->root = node;

    return node;
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_model_expand_to_depth (mctree_model_t *model, int depth)
{
    if (model == NULL || model->root == NULL)
        return;

    if (depth < 0)
        depth = 0;

    mctree_node_expand_to_depth (model->root, 0, depth);
}

/* --------------------------------------------------------------------------------------------- */

GArray *
mctree_model_build_visible_rows (const mctree_model_t *model)
{
    GArray *rows;

    rows = g_array_new (FALSE, FALSE, sizeof (mctree_visible_row_t));

    if (model == NULL || model->root == NULL)
        return rows;

    mctree_model_add_visible_children (model->root, 0, rows);
    return rows;
}

/* --------------------------------------------------------------------------------------------- */

guint
mctree_node_child_count (const mctree_node_t *node)
{
    guint i;
    guint count = 0;

    if (node == NULL || node->children == NULL)
        return 0;

    for (i = 0; i < node->children->len; i++)
    {
        mctree_node_t *child = g_ptr_array_index (node->children, i);

        if (mctree_node_is_transparent_container (child))
            count += mctree_node_child_count (child);
        else
            count++;
    }

    return count;
}

/* --------------------------------------------------------------------------------------------- */

guint
mctree_node_descendant_count (const mctree_node_t *node)
{
    return mctree_node_descendant_count_int (node);
}

/* --------------------------------------------------------------------------------------------- */

const char *
mctree_node_type_name (mctree_node_type_t type)
{
    switch (type)
    {
    case MCTREE_NODE_ROOT:
        return "root";
    case MCTREE_NODE_OBJECT:
        return "object";
    case MCTREE_NODE_ARRAY:
        return "array";
    case MCTREE_NODE_FIELD:
        return "field";
    case MCTREE_NODE_ITEM:
        return "item";
    case MCTREE_NODE_SCALAR:
        return "scalar";
    case MCTREE_NODE_ELEMENT:
        return "element";
    case MCTREE_NODE_ATTRIBUTE:
        return "attribute";
    case MCTREE_NODE_TEXT:
        return "text";
    default:
        return "unknown";
    }
}
