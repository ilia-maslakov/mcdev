/*
   src/editor - navigation/layout-cache consistency probe

   Compares the cached bol/eol/column/line answers against slow reference
   computations while replaying randomized cursor navigation, and checks that
   a bulk cursor move restores exactly on undo.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.
 */

#define TEST_SUITE_NAME "/src/editor"
#include "tests/mctest.h"
#include "lib/charsets.h"
#include "src/selcodepage.h"
#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor/editcmd_private.h"
#include "lib/keybind.h"
#include <stdio.h>

// probe workload (kept small so this stays a fast unit test; bump for deeper local fuzzing).
// LONG_LINE must stay above the layout cache step (EDIT_LAYOUT_CACHE_STEP) to exercise checkpoints.
#define SEQ_STEPS  400
#define UNDO_STEPS 100
#define LONG_LINE  2400

// a real WDialog (not a bare WGroup): edit_execute_cmd() reads DIALOG(owner)->data.p, which sits
// past the WGroup base, so a bare group would read out of bounds.  data.p == NULL disables plugins.
static WDialog owner;
static WEdit *ed;

/* Self-contained xorshift32 PRNG so a given seed replays the same sequence on every platform
   (libc rand() is not portable, so a fixed seed alone would not be reproducible). */
static guint32 rng_state;

static void
rng_seed (guint32 seed)
{
    rng_state = seed != 0 ? seed : 0x9e3779b9u;
}

static guint32
rng_next (void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* uniform value in [0, n) */
static int
rng_below (int n)
{
    return (int) (rng_next () % (guint32) n);
}

static void
setup (void)
{
    WRect r;
    str_init_strings (NULL);
    mc_global.sysconfig_dir = (char *) TEST_SHARE_DIR;
    load_codepages_list ();
    edit_options.filesize_threshold = (char *) "64M";
    edit_options.group_undo = FALSE;
    edit_options.persistent_selections = TRUE;
    edit_options.fake_half_tabs = FALSE;
    edit_options.backspace_through_tabs = FALSE;
    rect_init (&r, 0, 0, 24, 80);
    ed = edit_init (NULL, &r, NULL);
    memset (&owner, 0, sizeof (owner));
    group_add_widget (GROUP (&owner), WIDGET (ed));
    mc_global.source_codepage = 0;
    mc_global.display_codepage = 0;
    cp_source = "ASCII";
    cp_display = "ASCII";
    do_set_codepage (0);
    edit_set_codeset (ed);
}
static void
teardown (void)
{
    edit_clean (ed);
    group_remove_widget (ed);
    g_free (ed);
    free_codepages_list ();
    str_uninit_strings ();
}

static void
load (const char *s)
{
    for (const char *p = s; *p != '\0'; p++)
    {
        edit_buffer_insert (&ed->buffer, *p);
        if (*p == '\n')
            ed->buffer.lines++;
    }
    edit_cursor_move (ed, -ed->buffer.curs1);  // back to bol 0
    ed->buffer.curs_line = 0;
}

static int failures = 0;

static void
check (const char *tag)
{
    off_t curs1 = ed->buffer.curs1;
    long ref_line = edit_buffer_count_lines (&ed->buffer, 0, curs1);
    off_t ref_bol = edit_buffer_get_bol (&ed->buffer, curs1);
    off_t ref_eol = edit_buffer_get_eol (&ed->buffer, curs1);
    long ref_col = (long) edit_move_forward3 (ed, ref_bol, 0, curs1);

    off_t got_bol = edit_get_current_bol (ed);
    off_t got_eol = edit_get_current_eol (ed);
    long got_col = edit_get_col (ed);

    if (ed->buffer.curs_line != ref_line)
    {
        printf ("BUG[%s] curs_line=%ld ref=%ld curs1=%ld\n", tag, ed->buffer.curs_line, ref_line,
                (long) curs1);
        failures++;
    }
    if (got_bol != ref_bol)
    {
        printf ("BUG[%s] bol=%ld ref=%ld curs1=%ld\n", tag, (long) got_bol, (long) ref_bol,
                (long) curs1);
        failures++;
    }
    if (got_eol != ref_eol)
    {
        printf ("BUG[%s] eol=%ld ref=%ld curs1=%ld\n", tag, (long) got_eol, (long) ref_eol,
                (long) curs1);
        failures++;
    }
    if (got_col != ref_col)
    {
        printf ("BUG[%s] col=%ld ref=%ld curs1=%ld bol=%ld\n", tag, got_col, ref_col, (long) curs1,
                (long) ref_bol);
        failures++;
    }
}

static void
fresh (void)
{
    WRect r;
    if (ed != NULL)
    {
        edit_clean (ed);
        g_free (ed);
    }
    memset (&owner, 0, sizeof (owner));
    rect_init (&r, 0, 0, 24, 80);
    ed = edit_init (NULL, &r, NULL);
    group_add_widget (GROUP (&owner), WIDGET (ed));
    edit_set_codeset (ed);
}

static void
run_seq (const char *content, gboolean beyond, unsigned seed)
{
    fresh ();
    edit_options.cursor_beyond_eol = beyond;
    load (content);
    rng_seed (seed);
    check ("init");
    for (int i = 0; i < SEQ_STEPS; i++)
    {
        int op = rng_below (7);
        switch (op)
        {
        case 0:
            edit_move_up (ed, 1 + rng_below (3), FALSE);
            break;
        case 1:
            edit_move_down (ed, 1 + rng_below (3), FALSE);
            break;
        case 2:
            edit_cursor_move (ed, 1);
            break;
        case 3:
            edit_cursor_move (ed, -1);
            break;
        case 4:
            edit_execute_cmd (ed, CK_Home, -1);
            break;
        case 5:
            edit_execute_cmd (ed, CK_End, -1);
            break;
        case 6:
            edit_cursor_move (ed, rng_below (200) - 100);
            break;
        default:
            break;
        }
        check ("seq");
        if (failures > 20)
            return;
    }
}

/* cross-line undo restoration test */
static void
run_undo (const char *content, unsigned seed)
{
    fresh ();
    edit_options.cursor_beyond_eol = FALSE;
    load (content);
    rng_seed (seed);
    /* record a keypress boundary, do a bulk cursor move, then undo and verify */
    for (int i = 0; i < UNDO_STEPS; i++)
    {
        off_t before = ed->buffer.curs1;
        long before_line = ed->buffer.curs_line;
        /* push a KEY_PRESS marker so undo has a boundary */
        edit_push_key_press (ed);
        off_t dest = (off_t) (rng_next () % (guint32) (ed->buffer.size + 1));
        edit_cursor_move (ed, dest - ed->buffer.curs1);
        check ("undo-move");
        edit_execute_cmd (ed, CK_Undo, -1);
        if (ed->buffer.curs1 != before || ed->buffer.curs_line != before_line)
        {
            printf ("BUG[undo] after undo curs1=%ld (want %ld) line=%ld (want %ld) dest=%ld\n",
                    (long) ed->buffer.curs1, (long) before, ed->buffer.curs_line, before_line,
                    (long) dest);
            failures++;
        }
        check ("undo-after");
        if (failures > 20)
            return;
    }
}

START_TEST (probe)
{
    /* line lengths mixing tabs, short/long lines, and a very long line (> cache step) */
    GString *lng = g_string_new ("");
    for (int i = 0; i < LONG_LINE; i++)
        g_string_append_c (lng, "abc\tdef "[i % 8]);
    char *content =
        g_strdup_printf ("hello world\n\tindented line here\nshort\n\n"
                         "a line with\ttabs\tinside it and more text\n%s\nlast\ttab line\nx",
                         lng->str);

    run_seq (content, FALSE, 1);
    run_seq (content, TRUE, 2);
    run_seq (content, FALSE, 3);
    run_seq (content, TRUE, 4);
    run_undo (content, 5);
    run_undo (content, 6);

    g_free (content);
    g_string_free (lng, TRUE);

    printf ("TOTAL FAILURES: %d\n", failures);
    ck_assert_int_eq (failures, 0);
}
END_TEST

int
main (void)
{
    int nf;
    Suite *s = suite_create (TEST_SUITE_NAME);
    TCase *tc = tcase_create ("Core");
    tcase_add_checked_fixture (tc, setup, teardown);
    tcase_add_test (tc, probe);
    suite_add_tcase (s, tc);
    SRunner *sr = srunner_create (s);
    srunner_set_fork_status (sr, CK_NOFORK);
    srunner_run_all (sr, CK_NORMAL);
    nf = srunner_ntests_failed (sr);
    srunner_free (sr);
    return nf == 0 ? 0 : 1;
}
