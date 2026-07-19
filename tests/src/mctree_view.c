/*
   Tests for mctree view state.

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

#define TEST_SUITE_NAME "/src/mctree-view"

#include "tests/mctest.h"

#include "src/mctree/mctree-view.h"

/* --------------------------------------------------------------------------------------------- */

static mctree_model_t *
make_sample_model (void)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *alpha;
    mctree_node_t *beta;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "root", NULL);
    alpha = mctree_model_add_node (model, root, MCTREE_NODE_FIELD, "alpha", NULL);
    mctree_model_add_node (model, alpha, MCTREE_NODE_SCALAR, NULL, "first");
    beta = mctree_model_add_node (model, root, MCTREE_NODE_FIELD, "beta", NULL);
    mctree_model_add_node (model, beta, MCTREE_NODE_SCALAR, NULL, "second");
    mctree_model_expand_to_depth (model, 1);

    return model;
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_cursor_and_page_bounds_are_clamped)
{
    mctree_model_t *model;
    mctree_view_t *view;

    model = make_sample_model ();
    view = mctree_view_new ();
    mctree_view_set_page_rows (view, 1);
    mctree_view_set_model (view, model);

    ck_assert_uint_eq (mctree_view_row_count (view), 2);
    mctree_view_move (view, 20);
    ck_assert_int_eq (view->cursor, 1);
    ck_assert_int_eq (view->top, 1);
    mctree_view_move (view, -20);
    ck_assert_int_eq (view->cursor, 0);
    ck_assert_int_eq (view->top, 0);

    mctree_view_free (view);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_expand_and_collapse_rebuild_visible_rows)
{
    mctree_model_t *model;
    mctree_view_t *view;

    model = make_sample_model ();
    view = mctree_view_new ();
    mctree_view_set_model (view, model);

    ck_assert_uint_eq (mctree_view_row_count (view), 2);
    mctest_assert_true (mctree_view_expand_current (view));
    ck_assert_uint_eq (mctree_view_row_count (view), 3);
    mctest_assert_true (mctree_view_collapse_current (view));
    ck_assert_uint_eq (mctree_view_row_count (view), 2);

    mctree_view_free (view);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_collapse_from_transparent_child_focuses_visible_parent)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *customer;
    mctree_node_t *object;
    mctree_view_t *view;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "root", NULL);
    mctree_model_add_node (model, root, MCTREE_NODE_FIELD, "alpha", "first");
    customer = mctree_model_add_node (model, root, MCTREE_NODE_FIELD, "customer", NULL);
    object = mctree_model_add_node (model, customer, MCTREE_NODE_OBJECT, NULL, NULL);
    mctree_model_add_node (model, object, MCTREE_NODE_FIELD, "first_name", "Dorothy");
    mctree_model_expand_to_depth (model, 2);

    view = mctree_view_new ();
    mctree_view_set_model (view, model);

    ck_assert_uint_eq (mctree_view_row_count (view), 3);
    mctree_view_move (view, 2);
    ck_assert_str_eq (mctree_view_current_node (view)->key, "first_name");
    mctest_assert_true (mctree_view_collapse_current (view));
    ck_assert_int_eq (view->cursor, 1);
    ck_assert_ptr_eq (mctree_view_current_node (view), customer);

    mctree_view_free (view);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_search_wraps_from_cursor)
{
    mctree_model_t *model;
    mctree_view_t *view;

    model = make_sample_model ();
    view = mctree_view_new ();
    mctree_view_set_model (view, model);

    ck_assert_str_eq (mctree_view_current_node (view)->key, "alpha");
    mctest_assert_true (mctree_view_search (view, "beta"));
    ck_assert_str_eq (mctree_view_current_node (view)->key, "beta");
    mctest_assert_true (mctree_view_search (view, "alpha"));
    ck_assert_str_eq (mctree_view_current_node (view)->key, "alpha");

    mctree_view_free (view);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_toggle_current_expands_and_collapses)
{
    mctree_model_t *model;
    mctree_view_t *view;

    model = make_sample_model ();
    view = mctree_view_new ();
    mctree_view_set_model (view, model);

    ck_assert_uint_eq (mctree_view_row_count (view), 2);
    mctest_assert_true (mctree_view_toggle_current (view));
    ck_assert_uint_eq (mctree_view_row_count (view), 3);
    mctest_assert_true (mctree_view_toggle_current (view));
    ck_assert_uint_eq (mctree_view_row_count (view), 2);

    // leaf: nothing to toggle
    mctree_view_move (view, 1);
    mctest_assert_true (mctree_view_expand_current (view));
    mctree_view_move (view, 1);
    mctest_assert_false (mctree_view_toggle_current (view));

    mctree_view_free (view);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_expand_all_and_collapse_all_keep_cursor_node)
{
    mctree_model_t *model;
    mctree_view_t *view;

    model = make_sample_model ();
    view = mctree_view_new ();
    mctree_view_set_model (view, model);

    mctree_view_move (view, 1);  // "beta"
    mctree_view_expand_all (view);
    ck_assert_uint_eq (mctree_view_row_count (view), 4);
    ck_assert_str_eq (mctree_view_current_node (view)->key, "beta");

    // move into beta's scalar child, collapse all: cursor falls back to "beta"
    mctree_view_move (view, 1);
    mctree_view_collapse_all (view);
    ck_assert_uint_eq (mctree_view_row_count (view), 2);
    ck_assert_str_eq (mctree_view_current_node (view)->key, "beta");

    mctree_view_free (view);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_search_model_finds_collapsed_node_and_expands_path)
{
    mctree_model_t *model;
    mctree_view_t *view;

    model = make_sample_model ();
    view = mctree_view_new ();
    mctree_view_set_model (view, model);

    // "second" lives under collapsed "beta": invisible to the row search
    mctest_assert_false (mctree_view_search (view, "second"));
    mctest_assert_true (mctree_view_search_model (view, "second"));
    ck_assert_uint_eq (mctree_view_row_count (view), 3);
    ck_assert_str_eq (mctree_view_current_node (view)->value, "second");

    mctest_assert_false (mctree_view_search_model (view, "no-such-text"));

    mctree_view_free (view);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_cursor_and_page_bounds_are_clamped);
    tcase_add_test (tc_core, test_expand_and_collapse_rebuild_visible_rows);
    tcase_add_test (tc_core, test_collapse_from_transparent_child_focuses_visible_parent);
    tcase_add_test (tc_core, test_search_wraps_from_cursor);
    tcase_add_test (tc_core, test_toggle_current_expands_and_collapses);
    tcase_add_test (tc_core, test_expand_all_and_collapse_all_keep_cursor_node);
    tcase_add_test (tc_core, test_search_model_finds_collapsed_node_and_expands_path);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
