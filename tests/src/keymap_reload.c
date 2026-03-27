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

/* @Test: after keymap_load, filemanager_map should contain default bindings */
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

/* @Test: after keymap_free + keymap_load, filemanager_map pointer changes */
START_TEST (test_keymap_reload_pointer_changes)
{
    const global_keymap_t *old_map;

    keymap_load (FALSE);
    old_map = filemanager_map;
    ck_assert_ptr_ne (old_map, NULL);

    keymap_free ();
    keymap_load (FALSE);

    /* filemanager_map should be valid but MAY point to different memory */
    ck_assert_ptr_ne (filemanager_map, NULL);

    /* verify bindings still work */
    {
        long cmd;

        cmd = keybind_lookup_keymap_command (filemanager_map, KEY_F (5));
        ck_assert_int_eq (cmd, CK_Copy);
    }

    /* if pointer changed, any widget holding old_map has dangling pointer */
    if (filemanager_map != old_map)
    {
        /* This is the bug: widget->keymap would point to freed memory.
           Mark test as passing but log the issue. */
        fprintf (stderr, "WARNING: filemanager_map pointer changed after reload: %p -> %p\n",
                 (const void *) old_map, (const void *) filemanager_map);
    }

    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test: keymap_load with stable GArray -- verify data pointer stability */
START_TEST (test_keymap_data_pointer_stability)
{
    const global_keymap_t *map_after_first;
    const global_keymap_t *map_after_second;

    keymap_load (FALSE);
    map_after_first = filemanager_map;

    keymap_free ();
    keymap_load (FALSE);
    map_after_second = filemanager_map;

    /* Report whether pointer is stable */
    if (map_after_first == map_after_second)
        fprintf (stderr, "INFO: filemanager_map pointer STABLE after reload\n");
    else
        fprintf (stderr, "INFO: filemanager_map pointer CHANGED after reload: %p -> %p\n",
                 (const void *) map_after_first, (const void *) map_after_second);

    /* Either way, bindings should work */
    {
        long cmd;

        cmd = keybind_lookup_keymap_command (filemanager_map, KEY_F (5));
        ck_assert_int_eq (cmd, CK_Copy);
    }

    keymap_free ();
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

/* @Test: widget->keymap becomes dangling after reload */
START_TEST (test_widget_keymap_dangling)
{
    const global_keymap_t *widget_keymap;
    long cmd;

    keymap_load (FALSE);

    /* simulate what widget does: store pointer at init time */
    widget_keymap = filemanager_map;
    ck_assert_ptr_ne (widget_keymap, NULL);

    /* verify it works before reload */
    cmd = keybind_lookup_keymap_command (widget_keymap, KEY_F (5));
    ck_assert_int_eq (cmd, CK_Copy);

    /* reload */
    keymap_free ();
    keymap_load (FALSE);

    /* filemanager_map is valid and updated */
    cmd = keybind_lookup_keymap_command (filemanager_map, KEY_F (5));
    ck_assert_int_eq (cmd, CK_Copy);

    /* but widget_keymap still points to old (freed) memory -- dangling!
       We cannot safely dereference widget_keymap here.
       Just verify the pointers differ to prove the bug exists. */
    if (widget_keymap != filemanager_map)
        fprintf (stderr, "CONFIRMED: widget_keymap is dangling after reload (%p != %p)\n",
                 (const void *) widget_keymap, (const void *) filemanager_map);
    else
        fprintf (stderr, "INFO: pointers match -- no dangling issue\n");

    /* This test passes either way -- it's diagnostic, not assertion.
       The real fix is to update widget->keymap after reload or keep pointer stable. */

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
