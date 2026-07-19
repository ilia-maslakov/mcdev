/*
   Structured tree view state.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
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

static void
mctree_view_set_expanded_recursive (mctree_node_t *node, gboolean expanded)
{
    guint i;

    if (node == NULL)
        return;

    node->expanded = expanded;

    if (node->children == NULL)
        return;

    for (i = 0; i < node->children->len; i++)
        mctree_view_set_expanded_recursive (g_ptr_array_index (node->children, i), expanded);
}

/* --------------------------------------------------------------------------------------------- */

/* Restore the cursor to the given node after a visibility change,
 * falling back to its nearest visible ancestor. */
static void
mctree_view_cursor_to_node (mctree_view_t *view, mctree_node_t *node)
{
    mctree_node_t *target;
    int idx;

    target = mctree_view_find_visible_ancestor (view, node);
    if (target == NULL)
        return;

    idx = mctree_view_find_node (view, target);
    if (idx >= 0)
        view->cursor = idx;
    mctree_view_clamp (view);
}

/* --------------------------------------------------------------------------------------------- */

/* Append node and all its descendants to flat in depth-first pre-order. */
static void
mctree_view_flatten (mctree_node_t *node, GPtrArray *flat)
{
    guint i;

    if (node == NULL)
        return;

    g_ptr_array_add (flat, node);

    if (node->children == NULL)
        return;

    for (i = 0; i < node->children->len; i++)
        mctree_view_flatten (g_ptr_array_index (node->children, i), flat);
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
mctree_view_toggle_current (mctree_view_t *view)
{
    mctree_node_t *node;

    node = mctree_view_current_node (view);
    if (node == NULL || mctree_node_child_count (node) == 0)
        return FALSE;

    if (!node->expanded)
        return mctree_view_expand_current (view);

    node->expanded = FALSE;
    mctree_view_rebuild (view);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mctree_view_expand_subtree_current (mctree_view_t *view)
{
    mctree_node_t *node;

    node = mctree_view_current_node (view);
    if (node == NULL || mctree_node_child_count (node) == 0)
        return FALSE;

    mctree_view_set_expanded_recursive (node, TRUE);
    mctree_view_rebuild (view);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_expand_all (mctree_view_t *view)
{
    mctree_node_t *node;

    if (view == NULL || view->model == NULL)
        return;

    node = mctree_view_current_node (view);
    mctree_view_set_expanded_recursive (view->model->root, TRUE);
    mctree_view_rebuild (view);
    mctree_view_cursor_to_node (view, node);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_collapse_all (mctree_view_t *view)
{
    mctree_view_expand_to_depth (view, 1);
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_view_expand_to_depth (mctree_view_t *view, int depth)
{
    mctree_node_t *node;

    if (view == NULL || view->model == NULL)
        return;

    node = mctree_view_current_node (view);
    mctree_model_expand_to_depth (view->model, depth);
    mctree_view_rebuild (view);
    mctree_view_cursor_to_node (view, node);
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

/* --------------------------------------------------------------------------------------------- */

/* Search the whole model (collapsed nodes included) in depth-first order,
 * wrapping around from the node after the cursor. On a match, expand the
 * ancestors of the found node and place the cursor on it. */
gboolean
mctree_view_search_model (mctree_view_t *view, const char *text)
{
    char *needle;
    GPtrArray *flat;
    mctree_node_t *current;
    mctree_node_t *found = NULL;
    guint start = 0;
    guint i;

    if (view == NULL || view->model == NULL || text == NULL || *text == '\0')
        return FALSE;

    flat = g_ptr_array_new ();
    mctree_view_flatten (view->model->root, flat);

    current = mctree_view_current_node (view);
    if (current != NULL)
        for (i = 0; i < flat->len; i++)
            if (g_ptr_array_index (flat, i) == current)
            {
                start = i;
                break;
            }

    needle = g_ascii_strdown (text, -1);

    for (i = 1; i <= flat->len; i++)
    {
        mctree_node_t *node;

        node = g_ptr_array_index (flat, (start + i) % flat->len);
        if (node != view->model->root && mctree_view_node_matches (node, needle))
        {
            found = node;
            break;
        }
    }

    g_free (needle);
    g_ptr_array_free (flat, TRUE);

    if (found == NULL)
        return FALSE;

    {
        mctree_node_t *parent;

        for (parent = found->parent; parent != NULL; parent = parent->parent)
            parent->expanded = TRUE;
    }

    mctree_view_rebuild (view);
    view->cursor = mctree_view_find_node (view, found);
    mctree_view_clamp (view);
    return TRUE;
}
