/*
   tests/src/mcterm_key_encode.c -- test mcterm key encoding

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

#define TEST_SUITE_NAME "/src/mcterm_key_encode"

#include "tests/mctest.h"

#include <string.h>

#include "lib/strutil.h"
#include "lib/terminal.h"  // convert_controls()
#include "lib/mcconfig.h"
#include "lib/tty/key.h"
#include "src/mcterm/mcterm_key.h"

/* --------------------------------------------------------------------------------------------- */

static void
init_mcterm_key_table (void)
{
    mc_config_t *cfg;
    const gchar *f13[] = { "\\e[25~", "\\e[1;2R" };
    const gchar *f15[] = { "\\e[15;2~" };
    const gchar *alt_f3[] = { "\\e[1;3R" };
    const gchar *f20[] = { "\\e[19;2~" };

    cfg = mc_config_init (NULL, FALSE);
    if (cfg == NULL)
        return;

    mc_config_set_string_list (cfg, "terminal:xterm", "f13", f13, G_N_ELEMENTS (f13));
    mc_config_set_string_list (cfg, "terminal:xterm", "f15", f15, G_N_ELEMENTS (f15));
    mc_config_set_string_list (cfg, "terminal:xterm", "alt-f3", alt_f3, G_N_ELEMENTS (alt_f3));
    mc_config_set_string_list (cfg, "terminal:xterm", "f20", f20, G_N_ELEMENTS (f20));
    mc_config_set_string (cfg, "terminal:xterm-256color", "copy", "xterm");

    mcterm_key_table_init (NULL, cfg);
    mc_config_deinit (cfg);
}

/* --------------------------------------------------------------------------------------------- */

static void
assert_encoded (int key, gboolean app_cursor, const char *expected)
{
    unsigned char buf[32];
    char *raw;
    size_t len;

    raw = convert_controls (expected);
    ck_assert_ptr_ne (raw, NULL);

    memset (buf, 0, sizeof (buf));
    len = mcterm_encode_key_xterm (key, buf, sizeof (buf), app_cursor);

    ck_assert_uint_eq (len, strlen (raw));
    ck_assert_mem_eq (buf, raw, len);

    g_free (raw);
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_function_keys_use_encoding_map)
{
    init_mcterm_key_table ();

    assert_encoded (KEY_F (13), FALSE, "\\e[1;2R");
    assert_encoded (KEY_F (15), FALSE, "\\e[15;2~");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_alt_function_key_uses_encoding_map)
{
    init_mcterm_key_table ();

    assert_encoded (KEY_M_ALT | KEY_F (3), FALSE, "\\e[1;3R");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_unknown_function_key_has_no_builtin_fallback)
{
    unsigned char buf[32];
    size_t len;

    init_mcterm_key_table ();

    memset (buf, 0, sizeof (buf));
    len = mcterm_encode_key_xterm (KEY_F (7), buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_application_cursor_plain_arrows)
{
    init_mcterm_key_table ();

    assert_encoded (KEY_UP, TRUE, "\\eOA");
    assert_encoded (KEY_DOWN, TRUE, "\\eOB");
    assert_encoded (KEY_RIGHT, TRUE, "\\eOC");
    assert_encoded (KEY_LEFT, TRUE, "\\eOD");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_enter_maps_to_cr)
{
    unsigned char buf[4];
    size_t len;

    len = mcterm_encode_key_xterm ('\n', buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 1);
    ck_assert_uint_eq (buf[0], '\r');

    len = mcterm_encode_key_xterm ('\r', buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 1);
    ck_assert_uint_eq (buf[0], '\r');

    len = mcterm_encode_key_xterm (KEY_ENTER, buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 1);
    ck_assert_uint_eq (buf[0], '\r');
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_backspace_maps_to_del)
{
    unsigned char buf[4];
    size_t len;

    len = mcterm_encode_key_xterm (KEY_BACKSPACE, buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 1);
    ck_assert_uint_eq (buf[0], 0x7F);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_utf8_bytes_pass_through)
{
    unsigned char buf[4];
    size_t len;

    len = mcterm_encode_key_xterm (0x80, buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 1);
    ck_assert_uint_eq (buf[0], 0x80);

    len = mcterm_encode_key_xterm (0xC3, buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 1);
    ck_assert_uint_eq (buf[0], 0xC3);

    len = mcterm_encode_key_xterm (0xFF, buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 1);
    ck_assert_uint_eq (buf[0], 0xFF);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_alt_ascii_uses_esc_prefix)
{
    unsigned char buf[8];
    size_t len;

    len = mcterm_encode_key_xterm (KEY_M_ALT | 'x', buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 2);
    ck_assert_uint_eq (buf[0], 0x1B);
    ck_assert_uint_eq (buf[1], 'x');
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_small_buffer_returns_zero)
{
    unsigned char buf[2];
    size_t len;

    init_mcterm_key_table ();

    len = mcterm_encode_key_xterm (KEY_F (20), buf, sizeof (buf), FALSE);
    ck_assert_uint_eq (len, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    str_init_strings ("UTF-8");

    tc_core = tcase_create ("Core");
    tcase_add_test (tc_core, test_function_keys_use_encoding_map);
    tcase_add_test (tc_core, test_alt_function_key_uses_encoding_map);
    tcase_add_test (tc_core, test_unknown_function_key_has_no_builtin_fallback);
    tcase_add_test (tc_core, test_application_cursor_plain_arrows);
    tcase_add_test (tc_core, test_enter_maps_to_cr);
    tcase_add_test (tc_core, test_backspace_maps_to_del);
    tcase_add_test (tc_core, test_utf8_bytes_pass_through);
    tcase_add_test (tc_core, test_alt_ascii_uses_esc_prefix);
    tcase_add_test (tc_core, test_small_buffer_returns_zero);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
