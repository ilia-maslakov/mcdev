/*
   Tests for mctree model.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.
 */

#define TEST_SUITE_NAME "/src/mctree-model"

#include "tests/mctest.h"

#include "src/mctree/mctree-model.h"

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_visible_rows_follow_expanded_state)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *field;
    GArray *rows;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "JSON", NULL);
    field = mctree_model_add_node (model, root, MCTREE_NODE_FIELD, "name", NULL);
    mctree_model_add_node (model, field, MCTREE_NODE_SCALAR, NULL, "value");

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_eq (rows->len, 0);
    g_array_free (rows, TRUE);

    mctree_model_expand_to_depth (model, 2);
    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_eq (rows->len, 2);
    ck_assert_ptr_eq (g_array_index (rows, mctree_visible_row_t, 0).node, field);
    ck_assert_int_eq (g_array_index (rows, mctree_visible_row_t, 1).depth, 1);

    g_array_free (rows, TRUE);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_scalar_preview_is_bounded)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *scalar;

    model = mctree_model_new (8);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "root", NULL);
    scalar = mctree_model_add_node (model, root, MCTREE_NODE_SCALAR, NULL, "abcdefghijklmnop");

    ck_assert_str_eq (scalar->value, "abcde...");
    ck_assert_uint_eq (scalar->original_value_len, 16);
    mctest_assert_true (scalar->value_truncated);

    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_transparent_object_containers_are_hidden)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *root_object;
    mctree_node_t *field;
    mctree_node_t *object;
    mctree_node_t *leaf;
    GArray *rows;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "YAML", NULL);
    root_object = mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, NULL, NULL);
    field = mctree_model_add_node (model, root_object, MCTREE_NODE_FIELD, "customer", NULL);
    object = mctree_model_add_node (model, field, MCTREE_NODE_OBJECT, NULL, NULL);
    leaf = mctree_model_add_node (model, object, MCTREE_NODE_FIELD, "first_name", "Dorothy");

    root->expanded = TRUE;
    field->expanded = TRUE;

    ck_assert_uint_eq (mctree_node_child_count (field), 1);
    ck_assert_uint_eq (mctree_node_descendant_count (field), 1);

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_eq (rows->len, 2);
    ck_assert_ptr_eq (g_array_index (rows, mctree_visible_row_t, 0).node, field);
    ck_assert_int_eq (g_array_index (rows, mctree_visible_row_t, 0).depth, 0);
    ck_assert_ptr_eq (g_array_index (rows, mctree_visible_row_t, 1).node, leaf);
    ck_assert_int_eq (g_array_index (rows, mctree_visible_row_t, 1).depth, 1);

    g_array_free (rows, TRUE);
    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_child_and_descendant_counts)
{
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *left;

    model = mctree_model_new (0);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "root", NULL);
    left = mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, "left", NULL);
    mctree_model_add_node (model, left, MCTREE_NODE_SCALAR, "leaf", "1");
    mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, "right", NULL);

    ck_assert_uint_eq (mctree_node_child_count (root), 2);
    ck_assert_uint_eq (mctree_node_descendant_count (root), 3);

    mctree_model_free (model);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_visible_rows_follow_expanded_state);
    tcase_add_test (tc_core, test_scalar_preview_is_bounded);
    tcase_add_test (tc_core, test_transparent_object_containers_are_hidden);
    tcase_add_test (tc_core, test_child_and_descendant_counts);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
