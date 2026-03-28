/*
   libmc - checks for WTable datasource contract

   Copyright (C) 2026
   The Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026.

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

#define TEST_SUITE_NAME "lib/widget/table"

#include "tests/mctest.h"

#include "lib/widget.h"

/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    int nrows;
} test_data_t;

/* --------------------------------------------------------------------------------------------- */

static int
test_get_nrows (const void *data)
{
    return ((const test_data_t *) data)->nrows;
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_table_set_datasource_empty_resets_position)
{
    static const table_column_def_t cols[] = {
        { 10, J_LEFT, TABLE_COL_TEXT },
    };
    test_data_t data = { 0 };
    table_datasource_t ds = { test_get_nrows, NULL, NULL, NULL, &data };
    WTable *t;

    t = table_new (0, 0, 3, 10, G_N_ELEMENTS (cols), cols);
    t->top = 5;
    t->current = 7;

    table_set_datasource (t, ds);

    ck_assert_int_eq (table_get_current (t), 0);
    ck_assert_int_eq (t->top, 0);

    widget_destroy (WIDGET (t));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_table_set_datasource_shrink_clamps_current_and_top)
{
    static const table_column_def_t cols[] = {
        { 10, J_LEFT, TABLE_COL_TEXT },
    };
    test_data_t data = { 10 };
    table_datasource_t ds = { test_get_nrows, NULL, NULL, NULL, &data };
    WTable *t;

    t = table_new (0, 0, 3, 10, G_N_ELEMENTS (cols), cols);
    table_set_datasource (t, ds);
    table_set_current (t, 9);

    ck_assert_int_eq (table_get_current (t), 9);
    ck_assert_int_eq (t->top, 7);

    data.nrows = 8;
    table_set_datasource (t, ds);

    ck_assert_int_eq (table_get_current (t), 7);
    ck_assert_int_eq (t->top, 5);

    widget_destroy (WIDGET (t));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_table_set_datasource_empty_resets_position);
    tcase_add_test (tc_core, test_table_set_datasource_shrink_clamps_current_and_top);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
