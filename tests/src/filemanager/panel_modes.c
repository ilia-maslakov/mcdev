/*
   src/filemanager - panel view modes

   Copyright (C) 2026
   Free Software Foundation, Inc.

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

#define TEST_SUITE_NAME "/src/filemanager"

#include "tests/mctest.h"

#include "lib/mcconfig.h"

#include "src/filemanager/panel_modes.h"

/* --------------------------------------------------------------------------------------------- */

static void
my_setup (void)
{
    str_init_strings (NULL);
}

static void
my_teardown (void)
{
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

static panel_mode_t
make_mode (const char *types, const char *widths, const char *status_types,
           const char *status_widths)
{
    panel_mode_t m;

    m.id = 1;
    m.name = (char *) "test";
    m.types = (char *) types;
    m.widths = (char *) widths;
    m.status_types = (char *) status_types;
    m.status_widths = (char *) status_widths;
    return m;
}

/* --------------------------------------------------------------------------------------------- */

/* panel_mode_to_format() builds the mc listing-format string from a mode. */
START_TEST (test_to_format_single_column)
{
    panel_mode_t m = make_mode ("name", "0", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half name");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_multiple_columns)
{
    panel_mode_t m = make_mode ("type name,size,mtime", "0,0,0", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half type name | size | mtime");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_repeated_columns_flow)
{
    panel_mode_t m = make_mode ("name,name", "0,0", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half 2 name");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_width_only_when_nonzero)
{
    panel_mode_t m = make_mode ("name,size", "0,8", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half name | size:8");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_repeated_columns_flow_width)
{
    panel_mode_t m = make_mode ("name,name", "8,8", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half 2 name:8");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_repeated_multifield_columns_flow)
{
    panel_mode_t m = make_mode ("type name,type name", "0,0", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half 2 type name");
    g_free (s);
}
END_TEST

/* Whitespace around the commas must not defeat the repeated-column collapse. */
START_TEST (test_to_format_repeated_columns_whitespace_collapse)
{
    panel_mode_t m = make_mode ("type name , type name", "0,0", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half 2 type name");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_mixed_columns_do_not_collapse)
{
    panel_mode_t m = make_mode ("name,name,size", "0,0,0", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half name | name | size");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_empty_falls_back_to_name)
{
    panel_mode_t m = make_mode ("", "", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half name");
    g_free (s);
}
END_TEST

START_TEST (test_to_format_status_line)
{
    panel_mode_t m = make_mode ("name", "0", "name,size", "0,6");
    char *s;

    s = panel_mode_to_format (&m, TRUE);
    ck_assert_str_eq (s, "half name | size:6");
    g_free (s);
}
END_TEST

/* Missing widths mean auto. */
START_TEST (test_to_format_widths_shorter_than_types)
{
    panel_mode_t m = make_mode ("name,size,mtime", "0", "", "");
    char *s;

    s = panel_mode_to_format (&m, FALSE);
    ck_assert_str_eq (s, "half name | size | mtime");
    g_free (s);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A pasted listing format is split into the types and widths lists. */
START_TEST (test_normalize_pasted_format_string)
{
    char *nt = NULL;
    char *nw = NULL;

    panel_mode_normalize ("half name | size:7 | type mode:3", "", &nt, &nw);
    ck_assert_str_eq (nt, "name,size,type mode");
    ck_assert_str_eq (nw, "0,7,3");
    g_free (nt);
    g_free (nw);
}
END_TEST

START_TEST (test_normalize_flowed_format_string)
{
    char *nt = NULL;
    char *nw = NULL;

    panel_mode_normalize ("half 2 type name", "", &nt, &nw);
    ck_assert_str_eq (nt, "type name,type name");
    ck_assert_str_eq (nw, "");
    g_free (nt);
    g_free (nw);
}
END_TEST

/* Plain editor input passes through untouched. */
START_TEST (test_normalize_plain_lists_unchanged)
{
    char *nt = NULL;
    char *nw = NULL;

    panel_mode_normalize ("name,size", "0,4", &nt, &nw);
    ck_assert_str_eq (nt, "name,size");
    ck_assert_str_eq (nw, "0,4");
    g_free (nt);
    g_free (nw);
}
END_TEST

/* A ":width" suffix wins over the widths entry for that column. */
START_TEST (test_normalize_suffix_overrides_width_entry)
{
    char *nt = NULL;
    char *nw = NULL;

    panel_mode_normalize ("name,size:9", "5,4", &nt, &nw);
    ck_assert_str_eq (nt, "name,size");
    ck_assert_str_eq (nw, "5,9");
    g_free (nt);
    g_free (nw);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* panel_mode_validate() accepts known field ids and non-negative widths. */
START_TEST (test_validate_accepts_known_fields)
{
    char *err = NULL;

    ck_assert (panel_mode_validate ("name,size", "0,0", &err));
    ck_assert (err == NULL);
}
END_TEST

START_TEST (test_validate_accepts_multifield_column)
{
    char *err = NULL;

    ck_assert (panel_mode_validate ("type name", "0", &err));
    ck_assert (err == NULL);
}
END_TEST

START_TEST (test_validate_rejects_unknown_field)
{
    char *err = NULL;

    ck_assert (!panel_mode_validate ("nosuchfield", "0", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

START_TEST (test_validate_rejects_non_numeric_width)
{
    char *err = NULL;

    ck_assert (!panel_mode_validate ("name", "abc", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

START_TEST (test_validate_rejects_negative_width)
{
    char *err = NULL;

    ck_assert (!panel_mode_validate ("name", "-1", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

START_TEST (test_validate_rejects_empty_types)
{
    char *err = NULL;

    ck_assert (!panel_mode_validate ("", "", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

START_TEST (test_validate_rejects_whitespace_only_types)
{
    char *err = NULL;

    ck_assert (!panel_mode_validate ("   ", "0", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

/* The listing format keeps the column count in a single digit, so more than
   9 columns cannot be expressed and must be rejected up front. */
START_TEST (test_validate_rejects_too_many_columns)
{
    char *err = NULL;

    ck_assert (panel_mode_validate ("name,name,name,name,name,name,name,name,name", "", &err));
    ck_assert (err == NULL);
    ck_assert (
        !panel_mode_validate ("name,name,name,name,name,name,name,name,name,name", "", &err));
    ck_assert (err != NULL);
    g_free (err);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* List operations: add appends with a stable unique id, dup deep-copies
   under a fresh id. */
START_TEST (test_list_ops_add_dup)
{
    guint base;
    panel_mode_t *added;
    panel_mode_t *dup;

    panel_modes_init ();
    base = panel_modes_count ();
    ck_assert (base >= 2);

    /* add: count grows, new id is unique and not reused */
    added = panel_modes_add ("scratch");
    ck_assert (added != NULL);
    ck_assert_uint_eq (panel_modes_count (), base + 1);
    ck_assert (panel_modes_get_by_id (added->id) == added);
    ck_assert_int_eq (panel_modes_index_by_id (added->id), (int) base);

    /* dup: deep copy under a brand-new id, "(copy)" suffix */
    dup = panel_modes_dup (0);
    ck_assert (dup != NULL);
    ck_assert (dup->id != panel_modes_get (0)->id);
    ck_assert (dup->types != panel_modes_get (0)->types);
    ck_assert_str_eq (dup->types, panel_modes_get (0)->types);

    panel_modes_deinit ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Removing every mode must not leave the list empty: the defaults are reseeded
   so the feature always has at least one mode to apply. */
START_TEST (test_remove_last_reseeds_defaults)
{
    panel_modes_init ();
    /* drain down to the very last mode */
    while (panel_modes_count () > 1)
        panel_modes_remove (0);
    ck_assert_uint_eq (panel_modes_count (), 1);
    /* removing the last one reseeds the defaults rather than leaving it empty */
    panel_modes_remove (0);
    ck_assert_uint_gt (panel_modes_count (), 0);
    panel_modes_deinit ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* A persisted [Panel modes] group with count=0 still yields the defaults. */
START_TEST (test_load_empty_group_seeds_defaults)
{
    mc_global.main_config = mc_config_init (NULL, FALSE);
    mc_config_set_int (mc_global.main_config, "Panel modes", "count", 0);

    panel_modes_init ();
    ck_assert_uint_gt (panel_modes_count (), 0);

    panel_modes_deinit ();
    mc_config_deinit (mc_global.main_config);
    mc_global.main_config = NULL;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Missing ids must not collide with stored ids. */
START_TEST (test_load_missing_id_no_collision)
{
    guint id0;
    guint id1;

    mc_global.main_config = mc_config_init (NULL, FALSE);
    mc_config_set_int (mc_global.main_config, "Panel modes", "count", 2);
    mc_config_set_string (mc_global.main_config, "Panel modes", "0_name", "NoId");
    mc_config_set_string (mc_global.main_config, "Panel modes", "1_name", "StoredId");
    mc_config_set_int (mc_global.main_config, "Panel modes", "1_id", 1);

    panel_modes_init ();
    ck_assert_uint_eq (panel_modes_count (), 2);
    id0 = panel_modes_get (0)->id;
    id1 = panel_modes_get (1)->id;
    ck_assert_uint_eq (id1, 1);
    ck_assert (id0 != id1);
    ck_assert_int_eq (panel_modes_index_by_id (1), 1);

    panel_modes_deinit ();
    mc_config_deinit (mc_global.main_config);
    mc_global.main_config = NULL;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* save -> deinit -> init reloads names/types/widths and the stable id. */
START_TEST (test_save_load_roundtrip)
{
    panel_mode_t *added;
    panel_mode_t *reloaded;
    guint saved_id;
    int idx;

    mc_global.main_config = mc_config_init (NULL, FALSE);

    panel_modes_init ();
    added = panel_modes_add ("Custom");
    panel_mode_set (added, "Custom", "name,size", "10,6", "name", "0");
    saved_id = added->id;
    panel_modes_save ();

    panel_modes_deinit ();
    panel_modes_init ();  // reloads from the in-memory config

    idx = panel_modes_index_by_id (saved_id);
    ck_assert_int_ge (idx, 0);
    reloaded = panel_modes_get ((guint) idx);
    ck_assert_str_eq (reloaded->name, "Custom");
    ck_assert_str_eq (reloaded->types, "name,size");
    ck_assert_str_eq (reloaded->widths, "10,6");
    ck_assert_str_eq (reloaded->status_types, "name");
    ck_assert_uint_eq (reloaded->id, saved_id);

    panel_modes_deinit ();
    mc_config_deinit (mc_global.main_config);
    mc_global.main_config = NULL;
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, my_setup, my_teardown);

    tcase_add_test (tc_core, test_to_format_single_column);
    tcase_add_test (tc_core, test_to_format_multiple_columns);
    tcase_add_test (tc_core, test_to_format_repeated_columns_flow);
    tcase_add_test (tc_core, test_to_format_width_only_when_nonzero);
    tcase_add_test (tc_core, test_to_format_repeated_columns_flow_width);
    tcase_add_test (tc_core, test_to_format_repeated_multifield_columns_flow);
    tcase_add_test (tc_core, test_to_format_repeated_columns_whitespace_collapse);
    tcase_add_test (tc_core, test_to_format_mixed_columns_do_not_collapse);
    tcase_add_test (tc_core, test_to_format_empty_falls_back_to_name);
    tcase_add_test (tc_core, test_to_format_status_line);
    tcase_add_test (tc_core, test_to_format_widths_shorter_than_types);
    tcase_add_test (tc_core, test_normalize_pasted_format_string);
    tcase_add_test (tc_core, test_normalize_flowed_format_string);
    tcase_add_test (tc_core, test_normalize_plain_lists_unchanged);
    tcase_add_test (tc_core, test_normalize_suffix_overrides_width_entry);
    tcase_add_test (tc_core, test_validate_accepts_known_fields);
    tcase_add_test (tc_core, test_validate_accepts_multifield_column);
    tcase_add_test (tc_core, test_validate_rejects_unknown_field);
    tcase_add_test (tc_core, test_validate_rejects_non_numeric_width);
    tcase_add_test (tc_core, test_validate_rejects_negative_width);
    tcase_add_test (tc_core, test_validate_rejects_empty_types);
    tcase_add_test (tc_core, test_validate_rejects_whitespace_only_types);
    tcase_add_test (tc_core, test_validate_rejects_too_many_columns);
    tcase_add_test (tc_core, test_list_ops_add_dup);
    tcase_add_test (tc_core, test_remove_last_reseeds_defaults);
    tcase_add_test (tc_core, test_load_empty_group_seeds_defaults);
    tcase_add_test (tc_core, test_load_missing_id_no_collision);
    tcase_add_test (tc_core, test_save_load_roundtrip);
    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
