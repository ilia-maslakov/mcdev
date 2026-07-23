/*
   Internal file viewer for the Midnight Commander
   Structured (tree) display mode for JSON/YAML/XML content

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

#include "lib/global.h"
#include "lib/event.h"
#include "lib/skin.h"
#include "lib/strutil.h"
#include "lib/tty/tty.h"
#include "lib/util.h"
#include "lib/widget.h"

#include "src/history.h"
#include "src/keymap.h"

#include "internal.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Print at most max_width terminal columns of text, return columns used. */

static int
mcview_structured_print_span (const char *text, int max_width)
{
    int width;

    if (text == NULL || text[0] == '\0' || max_width <= 0)
        return 0;

    width = str_term_width1 (text);
    if (width <= max_width)
    {
        tty_print_string (text);
        return width;
    }

    tty_print_string (str_fit_to_term (text, max_width, J_LEFT_FIT));
    return max_width;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mcview_structured_node_value (const mctree_node_t *node)
{
    if (node->value != NULL)
        return g_strdup (node->value);

    if (mctree_node_child_count (node) > 0 && !node->expanded)
        return g_strdup_printf ("(%u children)", mctree_node_descendant_count (node));

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
mcview_structured_draw_row (WView *view, const mctree_visible_row_t *row, int y, gboolean current)
{
    const WRect *r = &view->data_area;
    const mctree_node_t *node = row->node;
    int remaining = r->cols;
    int base_color;
    int key_color;
    int value_color;
    char *value;
    const char *marker;
    const char *label;
    char *prefix;

    if (current && view->struct_tree->focused)
    {
        base_color = VIEWER_SELECTED_COLOR;
        key_color = base_color;
        value_color = base_color;
    }
    else
    {
        base_color = VIEWER_NORMAL_COLOR;
        key_color = MCTREE_KEY_COLOR;
        value_color = MCTREE_VALUE_COLOR;
    }

    widget_gotoyx (view, r->y + y, r->x);

    marker = mctree_node_child_count (node) == 0 ? "  " : (node->expanded ? "- " : "+ ");
    label = node->key != NULL ? node->key : mctree_node_type_name (node->type);
    value = mcview_structured_node_value (node);

    prefix = g_strnfill ((gsize) row->depth * 2, ' ');
    tty_setcolor (base_color);
    remaining -= mcview_structured_print_span (prefix, remaining);
    remaining -= mcview_structured_print_span (marker, remaining);
    g_free (prefix);

    tty_setcolor (key_color);
    remaining -= mcview_structured_print_span (label, remaining);

    if (value != NULL)
    {
        tty_setcolor (base_color);
        remaining -= mcview_structured_print_span (": ", remaining);
        tty_setcolor (value_color);
        remaining -= mcview_structured_print_span (value, remaining);
        g_free (value);
    }

    if (remaining > 0)
    {
        tty_setcolor (base_color);
        tty_print_string (str_fit_to_term ("", remaining, J_LEFT));
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mcview_structured_do_search (WView *view, gboolean ask)
{
    if (ask || view->struct_needle == NULL)
    {
        char *needle;

        needle = input_dialog (_ ("Search"), _ ("Enter search string:"), MC_HISTORY_SHARED_SEARCH,
                               view->struct_needle != NULL ? view->struct_needle : "",
                               INPUT_COMPLETE_NONE);
        if (needle == NULL || needle[0] == '\0')
        {
            g_free (needle);
            return;
        }

        g_free (view->struct_needle);
        view->struct_needle = needle;
    }

    if (!mctree_view_search_model (view->struct_tree, view->struct_needle))
        message (D_NORMAL, _ ("Search"), _ ("Search string not found"));

    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */

static void
mcview_structured_copy_path (WView *view)
{
    char *path;

    path = mcview_structured_current_path (view);
    if (path == NULL)
        return;

    mc_event_raise (MCEVENT_GROUP_CORE, "clipboard_text_to_file", (gpointer) path);
    mc_event_raise (MCEVENT_GROUP_CORE, "clipboard_file_to_ext_clip", NULL);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

static void
mcview_structured_show_value (WView *view)
{
    const mctree_node_t *node;
    const char *label;

    node = mctree_view_current_node (view->struct_tree);
    if (node == NULL || node->value == NULL)
        return;

    label = node->key != NULL ? node->key : mctree_node_type_name (node->type);
    if (node->value_truncated)
        message (D_NORMAL, label, _ ("%s\n\n(value truncated, %" G_GSIZE_FORMAT " bytes total)"),
                 node->value, node->original_value_len);
    else
        message (D_NORMAL, label, "%s", node->value);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Quick extension check used by the auto-enter option. */

gboolean
mcview_structured_auto_candidate (const WView *view)
{
    const char *path;
    const char *ext;
    static const char *const exts[] = { ".json", ".yaml", ".yml", ".xml", ".html", ".htm", NULL };
    int i;

    if (view->filename_vpath == NULL)
        return FALSE;

    path = vfs_path_get_last_path_str (view->filename_vpath);
    if (path == NULL)
        return FALSE;

    ext = strrchr (path, '.');
    if (ext == NULL)
        return FALSE;

    for (i = 0; exts[i] != NULL; i++)
        if (g_ascii_strcasecmp (ext, exts[i]) == 0)
            return TRUE;

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

/* Drop all structured-mode state and fall back to text display. */

void
mcview_structured_reset (WView *view)
{
    view->mode_flags.structured = FALSE;
    g_clear_pointer (&view->struct_tree, mctree_view_free);
    g_clear_pointer (&view->struct_model, mctree_model_free);
    MC_PTR_FREE (view->struct_needle);
    view->struct_content_type = MCTREE_CONTENT_UNKNOWN;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcview_structured_try_enter (WView *view, gboolean quiet)
{
    mctree_resolver_config_t config;
    mctree_resolver_result_t result;
    mctree_model_t *model;
    GError *error = NULL;
    const char *path;

    if (view->mode_flags.structured)
        return TRUE;

    if (view->filename_vpath == NULL || !vfs_file_is_local (view->filename_vpath))
    {
        if (!quiet)
            message (D_ERROR, _ ("Structured view"),
                     _ ("Structured view works on local files only"));
        return FALSE;
    }

    path = vfs_path_get_last_path_str (view->filename_vpath);

    mctree_resolver_config_init (&config);
    memset (&result, 0, sizeof (result));

    model = mctree_resolve_file (path, &config, &result, &error);
    if (model == NULL)
    {
        if (!quiet)
        {
            if (error != NULL)
                message (D_ERROR, _ ("Structured view"), "%s", error->message);
            else if (result.too_large)
                message (D_ERROR, _ ("Structured view"),
                         _ ("File is too large for structured view"));
            else if (result.diagnostic != NULL)
                message (D_ERROR, _ ("Structured view"), "%s", result.diagnostic);
            else
                message (D_ERROR, _ ("Structured view"), _ ("Cannot build structured view"));
        }
        g_clear_error (&error);
        mctree_resolver_result_clear (&result);
        return FALSE;
    }

    // tree replaces the text display; line filter and terminal mode are text-only
    if (view->filter_active)
        mcview_filter_deactivate (view);
    if (view->mode_flags.terminal)
    {
        view->mode_flags.terminal = FALSE;
        if (view->vterm != NULL)
        {
            mcview_vterm_free (view->vterm);
            view->vterm = NULL;
        }
    }

    view->struct_model = model;
    view->struct_content_type = result.content_type;
    mctree_resolver_result_clear (&result);

    view->struct_tree = mctree_view_new ();
    mctree_view_set_model (view->struct_tree, model);
    mctree_view_set_focused (view->struct_tree, TRUE);

    view->mode_flags.structured = TRUE;
    view->dpy_bbar_dirty = TRUE;
    view->dirty++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_structured_leave (WView *view)
{
    if (!view->mode_flags.structured)
        return;

    mcview_structured_reset (view);
    view->dpy_bbar_dirty = TRUE;
    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_toggle_structured_mode (WView *view)
{
    if (view->mode_flags.structured)
        mcview_structured_leave (view);
    else if (mcview_structured_try_enter (view, FALSE))
        view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_display_structured (WView *view)
{
    const WRect *r = &view->data_area;
    mctree_view_t *tree = view->struct_tree;
    guint row_count;
    int y;

    if (tree == NULL)
        return;

    mctree_view_set_page_rows (tree, r->lines);
    row_count = mctree_view_row_count (tree);

    for (y = 0; y < r->lines; y++)
    {
        guint idx = (guint) tree->top + (guint) y;

        if (idx < row_count)
            mcview_structured_draw_row (view,
                                        &g_array_index (tree->rows, mctree_visible_row_t, idx), y,
                                        (int) idx == tree->cursor);
        else
        {
            widget_gotoyx (view, r->y + y, r->x);
            tty_setcolor (VIEWER_NORMAL_COLOR);
            tty_print_string (str_fit_to_term ("", r->cols, J_LEFT));
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/* jq-style path of the current node: .spec.containers[0].image */

char *
mcview_structured_current_path (WView *view)
{
    GString *path;
    const mctree_node_t *node;
    GSList *chain = NULL;
    GSList *item;

    if (view->struct_tree == NULL)
        return NULL;

    node = mctree_view_current_node (view->struct_tree);
    if (node == NULL)
        return NULL;

    for (; node != NULL && node->parent != NULL; node = node->parent)
        chain = g_slist_prepend (chain, (gpointer) node);

    path = g_string_new (NULL);

    for (item = chain; item != NULL; item = g_slist_next (item))
    {
        const mctree_node_t *n = (const mctree_node_t *) item->data;

        if (n->key != NULL)
        {
            // providers label array items "[N]": an index, not a field name
            if (n->key[0] == '[')
                g_string_append (path, n->key);
            else
                g_string_append_printf (path, ".%s", n->key);
        }
        else if (n->type == MCTREE_NODE_ITEM)
        {
            guint i;

            for (i = 0; i < n->parent->children->len; i++)
                if (g_ptr_array_index (n->parent->children, i) == n)
                {
                    g_string_append_printf (path, "[%u]", i);
                    break;
                }
        }
        // keyless container/scalar wrappers are structural, not part of the path
    }

    if (path->len == 0)
        g_string_append_c (path, '.');

    g_slist_free (chain);
    return g_string_free (path, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* Right-hand side of the status line: content type and current node path. */

const char *
mcview_structured_status (WView *view)
{
    static char buffer[BUF_MEDIUM];
    char *path;

    path = mcview_structured_current_path (view);
    g_snprintf (buffer, sizeof (buffer), "%s %s",
                mctree_content_type_name (view->struct_content_type), path != NULL ? path : "");
    g_free (path);

    return buffer;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcview_structured_handle_char (WView *view, int key)
{
    if (!view->mode_flags.structured || view->struct_tree == NULL)
        return FALSE;

    if (key < '1' || key > '9')
        return FALSE;

    mctree_view_expand_to_depth (view->struct_tree, key - '0');
    view->dirty++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

cb_ret_t
mcview_structured_execute_cmd (WView *view, long command)
{
    mctree_view_t *tree = view->struct_tree;

    if (tree == NULL)
        return MSG_NOT_HANDLED;

    switch (command)
    {
    case CK_StructMode:
        mcview_structured_leave (view);
        break;
    case CK_StructToggleNode:
        if (!mctree_view_toggle_current (tree))
            mcview_structured_show_value (view);
        break;
    case CK_Right:
        mctree_view_expand_current (tree);
        break;
    case CK_Left:
        mctree_view_collapse_current (tree);
        break;
    case CK_StructExpandSub:
        mctree_view_expand_subtree_current (tree);
        break;
    case CK_StructExpandAll:
        mctree_view_expand_all (tree);
        break;
    case CK_StructCollapseAll:
        mctree_view_collapse_all (tree);
        break;
    case CK_StructCopyPath:
        mcview_structured_copy_path (view);
        break;
    case CK_Up:
        mctree_view_move (tree, -1);
        break;
    case CK_Down:
        mctree_view_move (tree, 1);
        break;
    case CK_PageUp:
        mctree_view_page_up (tree);
        break;
    case CK_PageDown:
        mctree_view_page_down (tree);
        break;
    case CK_Home:
        mctree_view_home (tree);
        break;
    case CK_End:
        mctree_view_end (tree);
        break;
    case CK_Search:
    case CK_SearchForward:
        mcview_structured_do_search (view, TRUE);
        break;
    case CK_SearchContinue:
        mcview_structured_do_search (view, FALSE);
        break;
    default:
        return MSG_NOT_HANDLED;
    }

    view->dirty++;
    return MSG_HANDLED;
}

/* --------------------------------------------------------------------------------------------- */
