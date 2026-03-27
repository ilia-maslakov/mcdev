/*
   tests for tty_build_key_name modifier-aware key names

   Copyright (C) 2026
   Free Software Foundation, Inc.
*/

#define TEST_SUITE_NAME "/src/learn"

#include "tests/mctest.h"

#include "lib/tty/key.h"

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_build_key_name_without_modifiers)
{
    char *name;

    name = tty_build_key_name ("up", 0);
    mctest_assert_str_eq (name, "up");
    g_free (name);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_build_key_name_single_modifiers)
{
    char *name;

    name = tty_build_key_name ("f9", KEY_M_CTRL);
    mctest_assert_str_eq (name, "ctrl-f9");
    g_free (name);

    name = tty_build_key_name ("right", KEY_M_ALT);
    mctest_assert_str_eq (name, "alt-right");
    g_free (name);

    name = tty_build_key_name ("left", KEY_M_SHIFT);
    mctest_assert_str_eq (name, "shift-left");
    g_free (name);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_build_key_name_combined_modifiers)
{
    char *name;

    name = tty_build_key_name ("up", KEY_M_CTRL | KEY_M_SHIFT);
    mctest_assert_str_eq (name, "ctrl-shift-up");
    g_free (name);

    name = tty_build_key_name ("pgdn", KEY_M_CTRL | KEY_M_ALT | KEY_M_SHIFT);
    mctest_assert_str_eq (name, "ctrl-alt-shift-pgdn");
    g_free (name);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");
    tcase_add_test (tc_core, test_build_key_name_without_modifiers);
    tcase_add_test (tc_core, test_build_key_name_single_modifiers);
    tcase_add_test (tc_core, test_build_key_name_combined_modifiers);

    return mctest_run_all (tc_core);
}
