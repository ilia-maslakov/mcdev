/** \file mctree-view.h
 *  \brief Header: structured tree view state
 */

#ifndef MC__MCTREE_VIEW_H
#define MC__MCTREE_VIEW_H

#include "src/mctree/mctree-model.h"

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct
{
    mctree_model_t *model;
    GArray *rows;
    int cursor;
    int top;
    int page_rows;
    gboolean focused;
} mctree_view_t;

/*** declarations of public functions ************************************************************/

mctree_view_t *mctree_view_new (void);
void mctree_view_free (mctree_view_t *view);

void mctree_view_set_model (mctree_view_t *view, mctree_model_t *model);
void mctree_view_rebuild (mctree_view_t *view);
void mctree_view_set_page_rows (mctree_view_t *view, int page_rows);
void mctree_view_set_focused (mctree_view_t *view, gboolean focused);

guint mctree_view_row_count (const mctree_view_t *view);
mctree_node_t *mctree_view_current_node (const mctree_view_t *view);

void mctree_view_move (mctree_view_t *view, int delta);
void mctree_view_home (mctree_view_t *view);
void mctree_view_end (mctree_view_t *view);
void mctree_view_page_up (mctree_view_t *view);
void mctree_view_page_down (mctree_view_t *view);

gboolean mctree_view_expand_current (mctree_view_t *view);
gboolean mctree_view_collapse_current (mctree_view_t *view);
gboolean mctree_view_toggle_current (mctree_view_t *view);
gboolean mctree_view_expand_subtree_current (mctree_view_t *view);
void mctree_view_expand_all (mctree_view_t *view);
void mctree_view_collapse_all (mctree_view_t *view);
void mctree_view_expand_to_depth (mctree_view_t *view, int depth);
gboolean mctree_view_search (mctree_view_t *view, const char *text);
gboolean mctree_view_search_model (mctree_view_t *view, const char *text);

#endif
