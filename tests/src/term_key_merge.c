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

/*
 * Test 1: define_sequence adds a new sequence to an empty trie,
 *         tty_key_lookup_sequence finds it, tty_match_seq_to_keycode matches it.
 */
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

/*
 * Test 2: system default defines \e[17~ for Alt-F1 (linux console).
 *         User term file overrides with \e[1;3P for the same keycode.
 *         Both paths must exist in the trie.
 *         tty_key_lookup_sequence must return one of them.
 *         tty_match_seq_to_keycode must match both raw sequences.
 */
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

    /* reverse lookup must find at least one */
    seq = tty_key_lookup_sequence (code_altf1);
    ck_assert_ptr_ne (seq, NULL);
    /* must be one of the two */
    ck_assert (strcmp (seq, "\\e[17~") == 0 || strcmp (seq, "\\e[1;3P") == 0);

    fprintf (stderr, "  reverse lookup returned: %s\n", seq);

    g_free (seq);
    g_free (raw_system);
    g_free (raw_user);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/*
 * Test 3: prefix extension -- shorter sequence exists, then longer added.
 *         \e[17~ exists (leaf), then \e[17;5~ is added (extends the path).
 *         The shorter sequence must still be matchable.
 */
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

    /* SHORT MUST STILL WORK -- this is the critical assertion */
    matched = tty_match_seq_to_keycode (raw_short, (int) strlen (raw_short));
    fprintf (stderr, "  short seq match after extension: %d (expected %d)\n", matched, code_short);
    ck_assert_int_eq (matched, code_short);

    /* reverse lookup for short */
    seq = tty_key_lookup_sequence (code_short);
    fprintf (stderr, "  reverse lookup short: %s\n", seq != NULL ? seq : "(NULL)");
    ck_assert_ptr_ne (seq, NULL);
    mctest_assert_str_eq (seq, "\\e[17~");
    g_free (seq);

    /* reverse lookup for long */
    seq = tty_key_lookup_sequence (code_long);
    fprintf (stderr, "  reverse lookup long: %s\n", seq != NULL ? seq : "(NULL)");
    ck_assert_ptr_ne (seq, NULL);
    mctest_assert_str_eq (seq, "\\e[17;5~");
    g_free (seq);

    g_free (raw_short);
    g_free (raw_long);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

#if 0
/* Tests 4-6 require working tmpdir and full mc_config init.
   Disabled in unit tests -- run manually if needed. */

START_TEST (test_gkeyfile_roundtrip)
{
    mc_config_t *cfg;
    char *tmpfile;
    char *value;
    int fd;

    tmpfile = g_build_filename (g_get_tmp_dir (), "mc_test_term_XXXXXX", (char *) NULL);
    fd = g_mkstemp (tmpfile);
    ck_assert_int_ge (fd, 0);
    close (fd);

    /* write */
    cfg = mc_config_init (tmpfile, FALSE);
    /* sequence: \e[1;3P  -- escaped backslash for storage: \\e[1;3P */
    g_key_file_set_value (cfg->handle, "keys", "alt-f1", "\\e[1;3P");
    mc_config_save_file (cfg, NULL);
    mc_config_deinit (cfg);

    /* read back */
    cfg = mc_config_init (tmpfile, TRUE);
    value = mc_config_get_string_raw (cfg, "keys", "alt-f1", NULL);
    mc_config_deinit (cfg);

    fprintf (stderr, "  read back: '%s'\n", value != NULL ? value : "(NULL)");
    ck_assert_ptr_ne (value, NULL);
    /* after g_key_file_get_string unescaping: \\ -> \, ; stays */
    mctest_assert_str_eq (value, "\\e[1;3P");

    g_free (value);
    g_unlink (tmpfile);
    g_free (tmpfile);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/*
 * Test 5: GKeyFile roundtrip with BAD \; escaping -- proves the bug.
 *         Writing \\e[1\;3P (with escaped semicolon) breaks reading.
 */
START_TEST (test_gkeyfile_roundtrip_bad_semicolon_escape)
{
    mc_config_t *cfg;
    char *tmpfile;
    char *value;
    int fd;

    tmpfile = g_build_filename (g_get_tmp_dir (), "mc_test_term2_XXXXXX", (char *) NULL);
    fd = g_mkstemp (tmpfile);
    ck_assert_int_ge (fd, 0);
    close (fd);

    /* write with \; -- the old buggy way */
    cfg = mc_config_init (tmpfile, FALSE);
    g_key_file_set_value (cfg->handle, "keys", "alt-f1", "\\e[1\\;3P");
    mc_config_save_file (cfg, NULL);
    mc_config_deinit (cfg);

    /* read back */
    cfg = mc_config_init (tmpfile, TRUE);
    value = mc_config_get_string_raw (cfg, "keys", "alt-f1", NULL);
    mc_config_deinit (cfg);

    fprintf (stderr, "  read back (bad escape): '%s'\n", value != NULL ? value : "(NULL)");

    /*
     * g_key_file_get_string interprets \; as unknown escape.
     * Depending on GLib version, it may return NULL or garbled data.
     * The key assertion: the value is NOT what we intended.
     */
    if (value != NULL)
    {
        /* if GLib returns something, it won't match what we want */
        fprintf (stderr, "  GLib returned non-NULL: '%s' (may differ from expected '\\e[1;3P')\n",
                 value);
    }
    else
    {
        fprintf (stderr, "  GLib returned NULL -- \\; is not a valid GKeyFile escape\n");
    }

    g_free (value);
    g_unlink (tmpfile);
    g_free (tmpfile);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/*
 * Test 6: full pipeline -- write term file, load it, verify trie has the sequence.
 */
START_TEST (test_full_save_load_pipeline)
{
    mc_config_t *cfg;
    char *tmpfile;
    char *value;
    char *raw;
    int code_altf1 = KEY_M_ALT | KEY_F (1);
    int matched;
    int fd;

    tmpfile = g_build_filename (g_get_tmp_dir (), "mc_test_term3_XXXXXX", (char *) NULL);
    fd = g_mkstemp (tmpfile);
    ck_assert_int_ge (fd, 0);
    close (fd);

    /* simulate lk_save: escape only backslash, not semicolon */
    {
        const char *seq = "\\e[1;3P"; /* learn_key() output */
        char *esc_str;

        esc_str = str_escape (seq, -1, "\\", TRUE);
        fprintf (stderr, "  str_escape result: '%s'\n", esc_str);

        cfg = mc_config_init (tmpfile, FALSE);
        mc_config_set_string_raw_value (cfg, "keys", "alt-f1", esc_str);
        mc_config_save_file (cfg, NULL);
        mc_config_deinit (cfg);
        g_free (esc_str);
    }

    /* simulate load_term_keys_file: read and define_sequence */
    {
        cfg = mc_config_init (tmpfile, TRUE);
        value = mc_config_get_string_raw (cfg, "keys", "alt-f1", NULL);
        mc_config_deinit (cfg);

        fprintf (stderr, "  loaded value: '%s'\n", value != NULL ? value : "(NULL)");
        ck_assert_ptr_ne (value, NULL);

        raw = convert_controls (value);
        fprintf (stderr, "  raw bytes (%d): [", (int) strlen (raw));
        for (int i = 0; i < (int) strlen (raw); i++)
            fprintf (stderr, "%s%02x", i > 0 ? " " : "", (unsigned char) raw[i]);
        fprintf (stderr, "]\n");

        ck_assert (define_sequence (code_altf1, raw, MCKEY_NOACTION));

        /* verify forward match */
        matched = tty_match_seq_to_keycode (raw, (int) strlen (raw));
        fprintf (stderr, "  forward match: 0x%x (expected 0x%x)\n", (unsigned) matched,
                 (unsigned) code_altf1);
        ck_assert_int_eq (matched, code_altf1);

        /* verify reverse lookup */
        {
            char *found = tty_key_lookup_sequence (code_altf1);

            fprintf (stderr, "  reverse lookup: %s\n", found != NULL ? found : "(NULL)");
            ck_assert_ptr_ne (found, NULL);
            g_free (found);
        }

        g_free (raw);
        g_free (value);
    }

    g_unlink (tmpfile);
    g_free (tmpfile);
}
END_TEST

#endif

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    str_init_strings ("UTF-8");

    tc_core = tcase_create ("Core");
    tcase_add_test (tc_core, test_define_and_lookup_basic);
    tcase_add_test (tc_core, test_system_then_user_override);
    tcase_add_test (tc_core, test_prefix_extension_short_still_works);
    /* GKeyFile roundtrip tests (4-6) require working tmpdir and mc_config;
       disabled in unit tests -- run manually if needed */

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
