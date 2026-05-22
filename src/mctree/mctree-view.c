/*
   Structured tree view state.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.
 */

#include <config.h>

#include <string.h>

#include "src/mctree/mctree-view.h"

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static int
mctree_view_find_node (const mctree_view_t *view, const mctree_node_t *node)
{
    guint i;

    if (view == NULL || view->rows == NULL || node == NULL)
        return -1;

    for (i = 0; i < view->rows->len; i++)
        if (g_array_index (view->rows, mctree_visible_row_t, i).node == node)
            return (int) i;

    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static mctree_node_t *
mctree_view_find_visible_ancestor (const mctree_view_t *view, mctree_node_t *node)
{
    while (node != NULL)
    {
        if (mctree_view_find_node (view, node) >= 0)
            return node;

        node = node->parent;
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_view_clamp (mctree_view_t *view)
{
    int row_count;

    if (view == NULL)
        return;

    row_count = (int) mctree_view_row_count (view);

    if (row_count <= 0)
    {
        view->cursor = 0;
        view->top = 0;
        return;
    }

    view->cursor = CLAMP (view->cursor, 0, row_count - 1);

    if (view->page_rows <= 0)
        view->page_rows = 1;

    if (view->cursor < view->top)
        view->top = view->cursor;
    else if (view->cursor >= view->top + view->page_rows)
        view->top = view->cursor - view->page_rows + 1;

    view->top = CLAMP (view->top, 0, MAX (0, row_count - 1));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_view_node_matches (const mctree_node_t *node, const char *needle)
{
    char *haystack;
    char *lower;
    gboolean found;

    if (node == NULL || needle == NULL || *needle == '\0')
        return FALSE;

    haystack = g_strdup_printf ("%s %s %s", node->key != NULL ? node->key : "",
                                node->value != NULL ? node->value : "",
                                mctree_node_type_name (node->type));
    lower = g_ascii_strdown (haystack, -1);
    found = strstr (lower, needle) != NULL;

    g_free (lower);
    g_free (haystack);
    return found;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

mctree_view_t *
mctree_view_new (void)
{
    mctree_view_t *view;

    view = g_new0 (mctree_view_t, 1);
    view->page_rows = 1;
    view->rows = g_array_new (FALSE, FALSE, sizeof (mctree_visible_row_t));

    return view;
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_free (mctree_view_t *view)
{
    if (view == NULL)
        return;

    if (view->rows != NULL)
        g_array_free (view->rows, TRUE);
    g_free (view);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_set_model (mctree_view_t *view, mctree_model_t *model)
{
    if (view == NULL)
        return;

    view->model = model;
    view->cursor = 0;
    view->top = 0;
    mctree_view_rebuild (view);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_rebuild (mctree_view_t *view)
{
    if (view == NULL)
        return;

    if (view->rows != NULL)
        g_array_free (view->rows, TRUE);
    view->rows = mctree_model_build_visible_rows (view->model);
    mctree_view_clamp (view);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_set_page_rows (mctree_view_t *view, int page_rows)
{
    if (view == NULL)
        return;

    view->page_rows = MAX (1, page_rows);
    mctree_view_clamp (view);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_set_focused (mctree_view_t *view, gboolean focused)
{
    if (view != NULL)
        view->focused = focused;
}

/* --------------------------------------------------------------------------------------------- */

guint
mctree_view_row_count (const mctree_view_t *view)
{
    return (view == NULL || view->rows == NULL) ? 0 : view->rows->len;
}

/* --------------------------------------------------------------------------------------------- */

mctree_node_t *
mctree_view_current_node (const mctree_view_t *view)
{
    if (view == NULL || view->rows == NULL || view->rows->len == 0)
        return NULL;

    return g_array_index (view->rows, mctree_visible_row_t, view->cursor).node;
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_move (mctree_view_t *view, int delta)
{
    if (view == NULL)
        return;

    view->cursor += delta;
    mctree_view_clamp (view);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_home (mctree_view_t *view)
{
    if (view == NULL)
        return;

    view->cursor = 0;
    mctree_view_clamp (view);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_end (mctree_view_t *view)
{
    if (view == NULL)
        return;

    view->cursor = (int) mctree_view_row_count (view) - 1;
    mctree_view_clamp (view);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_page_up (mctree_view_t *view)
{
    if (view != NULL)
        mctree_view_move (view, -view->page_rows);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_page_down (mctree_view_t *view)
{
    if (view != NULL)
        mctree_view_move (view, view->page_rows);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mctree_view_expand_current (mctree_view_t *view)
{
    mctree_node_t *node;

    node = mctree_view_current_node (view);
    if (node == NULL || mctree_node_child_count (node) == 0 || node->expanded)
        return FALSE;

    node->expanded = TRUE;
    mctree_view_rebuild (view);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mctree_view_collapse_current (mctree_view_t *view)
{
    mctree_node_t *node;
    mctree_node_t *target;

    node = mctree_view_current_node (view);
    if (node == NULL)
        return FALSE;

    if (node->expanded && mctree_node_child_count (node) > 0)
    {
        node->expanded = FALSE;
        mctree_view_rebuild (view);
        return TRUE;
    }

    target = mctree_view_find_visible_ancestor (view, node->parent);
    if (target == NULL || target == view->model->root)
        return FALSE;

    view->cursor = mctree_view_find_node (view, target);
    mctree_view_clamp (view);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mctree_view_search (mctree_view_t *view, const char *text)
{
    char *needle;
    guint i, row_count;

    if (view == NULL || view->rows == NULL || text == NULL || *text == '\0')
        return FALSE;

    needle = g_ascii_strdown (text, -1);
    row_count = view->rows->len;

    for (i = 1; i <= row_count; i++)
    {
        guint index;
        mctree_node_t *node;

        index = ((guint) view->cursor + i) % row_count;
        node = g_array_index (view->rows, mctree_visible_row_t, index).node;
        if (mctree_view_node_matches (node, needle))
        {
            view->cursor = (int) index;
            mctree_view_clamp (view);
            g_free (needle);
            return TRUE;
        }
    }

    g_free (needle);
    return FALSE;
}
