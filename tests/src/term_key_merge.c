/*
   tests/src/term_key_merge.c -- test trie merge: system defaults + user term file

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

#define TEST_SUITE_NAME "/src/term_key_merge"

#include "tests/mctest.h"

#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>

#include "lib/tty/tty.h"
#include "lib/tty/key.h"
#include "lib/terminal.h" /* convert_controls() */
#include "lib/mcconfig.h"
#include "lib/strutil.h"

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_define_and_lookup_basic)
{
    int code = KEY_M_ALT | KEY_F (1);
    char *raw;
    char *seq;
    int matched;

    /* ESC [ 1 ; 3 P  -- xterm Alt-F1 */
    raw = convert_controls ("\\e[1;3P");
    ck_assert_ptr_ne (raw, NULL);
    ck_assert_int_eq ((int) strlen (raw), 6);
    ck_assert_int_eq ((unsigned char) raw[0], 0x1b);

    /* add to trie */
    ck_assert (define_sequence (code, raw, MCKEY_NOACTION));

    /* forward match: raw bytes -> keycode */
    matched = tty_match_seq_to_keycode (raw, (int) strlen (raw));
    ck_assert_int_eq (matched, code);

    /* reverse lookup: keycode -> escape notation */
    seq = tty_key_lookup_sequence (code);
    ck_assert_ptr_ne (seq, NULL);
    mctest_assert_str_eq (seq, "\\e[1;3P");
    g_free (seq);

    g_free (raw);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_system_then_user_override)
{
    int code_altf1 = KEY_M_ALT | KEY_F (1);
    char *raw_system;
    char *raw_user;
    char *seq;
    int matched;

    /* Step 1: system default -- \e[17~ for Alt-F1 (linux console) */
    raw_system = convert_controls ("\\e[17~");
    ck_assert (define_sequence (code_altf1, raw_system, MCKEY_NOACTION));

    /* verify forward match works */
    matched = tty_match_seq_to_keycode (raw_system, (int) strlen (raw_system));
    ck_assert_int_eq (matched, code_altf1);

    /* Step 2: user term file -- \e[1;3P for Alt-F1 (xterm) */
    raw_user = convert_controls ("\\e[1;3P");
    ck_assert (define_sequence (code_altf1, raw_user, MCKEY_NOACTION));

    /* verify user sequence matches */
    matched = tty_match_seq_to_keycode (raw_user, (int) strlen (raw_user));
    ck_assert_int_eq (matched, code_altf1);

    /* verify system sequence still matches the same code */
    matched = tty_match_seq_to_keycode (raw_system, (int) strlen (raw_system));
    ck_assert_int_eq (matched, code_altf1);

    /* reverse lookup must return the latest learned path for this keycode */
    seq = tty_key_lookup_sequence (code_altf1);
    ck_assert_ptr_ne (seq, NULL);
    mctest_assert_str_eq (seq, "\\e[1;3P");

    g_free (seq);
    g_free (raw_system);
    g_free (raw_user);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_shift_function_key_reverse_lookup_is_normalized)
{
    char *raw;
    char *seq;

    raw = convert_controls ("\\e[1;2R");
    ck_assert (define_sequence (KEY_M_SHIFT | KEY_F (3), raw, MCKEY_NOACTION));

    seq = tty_key_lookup_sequence (KEY_F (13));
    ck_assert_ptr_ne (seq, NULL);
    mctest_assert_str_eq (seq, "\\e[1;2R");

    g_free (seq);
    g_free (raw);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_prefix_extension_short_still_works)
{
    int code_short = KEY_F (6);             /* \e[17~ */
    int code_long = KEY_M_CTRL | KEY_F (6); /* \e[17;5~ */
    char *raw_short;
    char *raw_long;
    char *seq;
    int matched;

    raw_short = convert_controls ("\\e[17~");
    raw_long = convert_controls ("\\e[17;5~");

    /* add short first */
    ck_assert (define_sequence (code_short, raw_short, MCKEY_NOACTION));

    /* verify it works */
    matched = tty_match_seq_to_keycode (raw_short, (int) strlen (raw_short));
    ck_assert_int_eq (matched, code_short);

    /* add longer extension */
    ck_assert (define_sequence (code_long, raw_long, MCKEY_NOACTION));

    /* longer must work */
    matched = tty_match_seq_to_keycode (raw_long, (int) strlen (raw_long));
    ck_assert_int_eq (matched, code_long);

    /* Shorter sequence remains matchable after adding an extension. */
    matched = tty_match_seq_to_keycode (raw_short, (int) strlen (raw_short));
    ck_assert_int_eq (matched, code_short);

    /* reverse lookup for short */
    seq = tty_key_lookup_sequence (code_short);
    ck_assert_ptr_ne (seq, NULL);
    mctest_assert_str_eq (seq, "\\e[17~");
    g_free (seq);

    /* reverse lookup for long */
    seq = tty_key_lookup_sequence (code_long);
    ck_assert_ptr_ne (seq, NULL);
    mctest_assert_str_eq (seq, "\\e[17;5~");
    g_free (seq);

    g_free (raw_short);
    g_free (raw_long);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    str_init_strings ("UTF-8");

    tc_core = tcase_create ("Core");
    tcase_add_test (tc_core, test_define_and_lookup_basic);
    tcase_add_test (tc_core, test_system_then_user_override);
    tcase_add_test (tc_core, test_shift_function_key_reverse_lookup_is_normalized);
    tcase_add_test (tc_core, test_prefix_extension_short_still_works);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
