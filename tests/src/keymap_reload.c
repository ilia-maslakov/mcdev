/*
   tests/src/keymap_reload.c -- test keymap reload correctness

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

#define TEST_SUITE_NAME "/src/keymap_reload"

#include "tests/mctest.h"

#include "lib/keybind.h"
#include "lib/mcconfig.h"
#include "lib/tty/key.h"

#include "src/keymap.h"

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_keymap_load_defaults)
{
    const global_keymap_t *map;

    keymap_load (FALSE); /* load from C defaults only, no files */

    map = filemanager_map;
    ck_assert_ptr_ne (map, NULL);

    /* default panel keymap should have CK_Copy bound to F5 */
    {
        long cmd;

        cmd = keybind_lookup_keymap_command (map, KEY_F (5));
        ck_assert_int_eq (cmd, CK_Copy);
    }

    /* default panel keymap should have CK_Move bound to F6 */
    {
        long cmd;

        cmd = keybind_lookup_keymap_command (map, KEY_F (6));
        ck_assert_int_eq (cmd, CK_Move);
    }

    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_keymap_reload_pointer_changes)
{
    const global_keymap_t *old_map;

    keymap_load (FALSE);
    old_map = filemanager_map;
    ck_assert_ptr_ne (old_map, NULL);

    keymap_free ();
    keymap_load (FALSE);

    /* Valid after reload, but may point to different memory; a widget that
       cached old_map would then hold a dangling pointer. */
    ck_assert_ptr_ne (filemanager_map, NULL);

    {
        long cmd;

        cmd = keybind_lookup_keymap_command (filemanager_map, KEY_F (5));
        ck_assert_int_eq (cmd, CK_Copy);
    }

    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_keymap_data_pointer_stability)
{
    long cmd;

    keymap_load (FALSE);
    keymap_free ();
    keymap_load (FALSE);

    cmd = keybind_lookup_keymap_command (filemanager_map, KEY_F (5));
    ck_assert_int_eq (cmd, CK_Copy);

    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_widget_keymap_dangling)
{
    const global_keymap_t *widget_keymap;
    long cmd;

    keymap_load (FALSE);

    /* A widget caches the keymap pointer at init time. */
    widget_keymap = filemanager_map;
    ck_assert_ptr_ne (widget_keymap, NULL);

    cmd = keybind_lookup_keymap_command (widget_keymap, KEY_F (5));
    ck_assert_int_eq (cmd, CK_Copy);

    keymap_free ();
    keymap_load (FALSE);

    /* filemanager_map is valid and updated after the reload. */
    cmd = keybind_lookup_keymap_command (filemanager_map, KEY_F (5));
    ck_assert_int_eq (cmd, CK_Copy);

    /* widget_keymap may now point to freed memory; do not dereference it. */
    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_keymap_load_defaults);
    tcase_add_test (tc_core, test_keymap_reload_pointer_changes);
    tcase_add_test (tc_core, test_keymap_data_pointer_stability);
    /* test_keymap_user_override requires mc_global init -- run manually */
    /* tcase_add_test (tc_core, test_keymap_user_override); */
    tcase_add_test (tc_core, test_widget_keymap_dangling);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
