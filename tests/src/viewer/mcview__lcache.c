/*
   src/viewer - unit tests for the unwrap-mode layout cache

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

#define TEST_SUITE_NAME "/src/viewer/lcache"

#include "tests/mctest.h"

#include <fcntl.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/strutil.h"
#include "lib/util.h"  // mc_pipe_t

#include "src/setup.h"  // option_tab_spacing

#include "src/viewer/internal.h"

/*** mocks ***********************************************************************************/

void
mcview_compute_areas (WView *view)
{
    (void) view;
}

void
mcview_update_bytes_per_line (WView *view)
{
    (void) view;
}

void
mcview_display (WView *view)
{
    (void) view;
}

void
mcview_show_error (WView *view, const char *format, const char *filename)
{
    (void) view;
    (void) format;
    (void) filename;
}

/*** fixtures ********************************************************************************/

static WView test_view;
static int saved_tab_spacing;

static void stream_close (void);

/* @Before */
static void
setup (void)
{
    str_init_strings (NULL);

    memset (&test_view, 0, sizeof (test_view));
    test_view.converter = str_cnv_from_term;
    test_view.force_max = -1;
    test_view.data_area.lines = 25;
    test_view.data_area.cols = 80;

    saved_tab_spacing = option_tab_spacing;
    option_tab_spacing = 8;
}

/* @After */
static void
teardown (void)
{
    option_tab_spacing = saved_tab_spacing;
    stream_close ();
    mcview_close_datasource (&test_view);
    str_uninit_strings ();
}

/*** helpers *********************************************************************************/

static void
load_string (const char *s)
{
    mcview_set_datasource_string (&test_view, s);
    test_view.dpy_start = 0;
    mcview_state_machine_init (&test_view.dpy_state_bottom, 0);
}

/* A still-growing datasource: a non-blocking pipe in streaming mode.
 * Feed chunks with stream_feed(), the write end stays open in between. */

static int stream_wfd = -1;

static void
stream_open (void)
{
    int pipefd[2];
    int flags;
    mc_pipe_t *p;

    ck_assert_int_eq (pipe (pipefd), 0);
    flags = fcntl (pipefd[0], F_GETFL);
    fcntl (pipefd[0], F_SETFL, flags | O_NONBLOCK);
    stream_wfd = pipefd[1];

    p = g_new0 (mc_pipe_t, 1);
    p->out.fd = pipefd[0];
    p->err.fd = -1;
    p->child_pid = -1;

    mcview_set_datasource_stdio_pipe (&test_view, p);
    test_view.streaming = TRUE;
    test_view.dpy_start = 0;
    mcview_state_machine_init (&test_view.dpy_state_bottom, 0);
}

static void
stream_feed (const char *data)
{
    const ssize_t len = (ssize_t) strlen (data);

    ck_assert_int_eq ((int) write (stream_wfd, data, len), (int) len);
    ck_assert (mcview_growbuf_read_available (&test_view));
}

static void
stream_close (void)
{
    if (stream_wfd >= 0)
    {
        close (stream_wfd);
        stream_wfd = -1;
    }
}

/*** tests ***********************************************************************************/

/* Stepping down and back up must visit the same BOL offsets, cold and warm. */
START_TEST (test_move_down_up_roundtrip)
{
    const off_t bols[] = { 0, 4, 9, 11, 12, 19 };
    size_t i;
    int pass;

    load_string ("aaa\nbbbb\nc\n\ndddddd\n");

    for (pass = 0; pass < 2; pass++)
    {
        for (i = 1; i < G_N_ELEMENTS (bols); i++)
        {
            mcview_ascii_move_down (&test_view, 1);
            ck_assert_msg (test_view.dpy_start == bols[i],
                           "pass %d down %zu: dpy_start %jd, expected %jd", pass, i,
                           (intmax_t) test_view.dpy_start, (intmax_t) bols[i]);
        }

        for (i = G_N_ELEMENTS (bols) - 1; i > 0; i--)
        {
            mcview_ascii_move_up (&test_view, 1);
            ck_assert_msg (test_view.dpy_start == bols[i - 1],
                           "pass %d up to %zu: dpy_start %jd, expected %jd", pass, i - 1,
                           (intmax_t) test_view.dpy_start, (intmax_t) bols[i - 1]);
        }

        // second pass repeats the walk with a warm cache
        mcview_state_machine_init (&test_view.dpy_state_bottom, 0);
    }
}
END_TEST

/* Multi-line steps through the prev map. */
START_TEST (test_move_up_many)
{
    load_string ("one\ntwo\nthree\nfour\nfive\n");

    mcview_ascii_move_down (&test_view, 4);
    ck_assert_int_eq ((int) test_view.dpy_start, 19);

    mcview_ascii_move_up (&test_view, 2);
    ck_assert_int_eq ((int) test_view.dpy_start, 8);

    mcview_ascii_move_up (&test_view, 10);
    ck_assert_int_eq ((int) test_view.dpy_start, 0);
}
END_TEST

/* moveto_eol: measured width must be identical when answered from the cache. */
START_TEST (test_moveto_eol_width_cached)
{
    off_t first;

    // tab at column 3 advances to 8, "def" ends at 11; cols=5 -> scroll to 6
    load_string ("abc\tdef\nshort\n");
    test_view.data_area.cols = 5;

    mcview_ascii_moveto_eol (&test_view);
    first = test_view.dpy_text_column;
    ck_assert_int_eq ((int) first, 6);

    test_view.dpy_text_column = 0;
    mcview_ascii_moveto_eol (&test_view);
    ck_assert_int_eq ((int) test_view.dpy_text_column, (int) first);
}
END_TEST

/* Changing a parse parameter must drop cached layout, not reuse it. */
START_TEST (test_flush_on_tab_spacing_change)
{
    load_string ("abc\tdef\nshort\n");
    test_view.data_area.cols = 5;

    mcview_ascii_moveto_eol (&test_view);
    ck_assert_int_eq ((int) test_view.dpy_text_column, 6);

    // tab now advances only to 4, "def" ends at 7 -> scroll to 2
    option_tab_spacing = 4;
    test_view.dpy_text_column = 0;
    mcview_ascii_moveto_eol (&test_view);
    ck_assert_int_eq ((int) test_view.dpy_text_column, 2);
}
END_TEST

/* A long line walk: down past it, then back up, cold and warm. */
START_TEST (test_long_line_roundtrip)
{
    GString *s;
    off_t second_bol;
    int pass;

    s = g_string_new (NULL);
    for (int i = 0; i < 100000; i++)
        g_string_append_c (s, 'x');
    g_string_append (s, "\nshort\n");
    second_bol = 100001;

    load_string (s->str);
    g_string_free (s, TRUE);

    for (pass = 0; pass < 2; pass++)
    {
        mcview_ascii_move_down (&test_view, 1);
        ck_assert_msg (test_view.dpy_start == second_bol, "pass %d: dpy_start %jd", pass,
                       (intmax_t) test_view.dpy_start);

        mcview_ascii_move_up (&test_view, 1);
        ck_assert_msg (test_view.dpy_start == 0, "pass %d: dpy_start %jd", pass,
                       (intmax_t) test_view.dpy_start);

        mcview_state_machine_init (&test_view.dpy_state_bottom, 0);
    }
}
END_TEST

/* A CRLF split across growbuf chunks must not leave a stale line boundary:
 * after "a\r" the boundary may still become \r\n, so it must not be cached. */
START_TEST (test_crlf_split_across_chunks)
{
    stream_open ();
    stream_feed ("a\r");

    /* Step down while only "a\r" has arrived: the line provisionally ends at 2. */
    mcview_ascii_move_down (&test_view, 1);
    ck_assert_int_eq ((int) test_view.dpy_start, 2);

    /* The missing \n arrives: the true line is "a\r\n", next paragraph starts at 3. */
    stream_feed ("\nb\n");

    mcview_ascii_move_up (&test_view, 1);
    ck_assert_int_eq ((int) test_view.dpy_start, 0);

    mcview_ascii_move_down (&test_view, 1);
    ck_assert_int_eq ((int) test_view.dpy_start, 3);

    mcview_ascii_move_up (&test_view, 1);
    ck_assert_int_eq ((int) test_view.dpy_start, 0);
}
END_TEST

/* Checkpoint resume at dpy_text_column > 0: measuring the same line cold
 * (no cache) and warm (resuming from checkpoints built over a partial line,
 * then parsing the newly arrived tail) must agree. */
START_TEST (test_checkpoint_resume_cold_vs_warm)
{
    GString *part1;
    GString *part2;
    off_t warm1, warm2, cold;
    int i;

    part1 = g_string_new (NULL);
    for (i = 0; i < 500; i++)
        g_string_append (part1, "abc\tdef");  // tabs make columns depend on parser state
    part2 = g_string_new (NULL);
    for (i = 0; i < 100; i++)
        g_string_append (part2, "ghij\tk");
    g_string_append_c (part2, '\n');

    stream_open ();
    stream_feed (part1->str);

    /* Builds checkpoints over the partial line; width must not be cached yet. */
    mcview_ascii_moveto_eol (&test_view);
    ck_assert (test_view.dpy_text_column > 0);

    stream_feed (part2->str);

    /* Warm: resumes from a checkpoint at dpy_text_column and parses the tail. */
    mcview_ascii_moveto_eol (&test_view);
    warm1 = test_view.dpy_text_column;

    /* Second warm call answers from the now-cached width. */
    test_view.dpy_text_column = 1;
    mcview_ascii_moveto_eol (&test_view);
    warm2 = test_view.dpy_text_column;

    /* Cold: full reparse from the paragraph start. */
    mcview_lcache_flush (&test_view);
    test_view.dpy_text_column = 0;
    mcview_ascii_moveto_eol (&test_view);
    cold = test_view.dpy_text_column;

    ck_assert_msg (warm1 == cold, "warm %jd != cold %jd", (intmax_t) warm1, (intmax_t) cold);
    ck_assert_msg (warm2 == cold, "warm2 %jd != cold %jd", (intmax_t) warm2, (intmax_t) cold);

    g_string_free (part1, TRUE);
    g_string_free (part2, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_checked_fixture (tc_core, setup, teardown);

    tcase_add_test (tc_core, test_move_down_up_roundtrip);
    tcase_add_test (tc_core, test_move_up_many);
    tcase_add_test (tc_core, test_moveto_eol_width_cached);
    tcase_add_test (tc_core, test_flush_on_tab_spacing_change);
    tcase_add_test (tc_core, test_long_line_roundtrip);
    tcase_add_test (tc_core, test_crlf_split_across_chunks);
    tcase_add_test (tc_core, test_checkpoint_resume_cold_vs_warm);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
