/*
   src/viewer - tests for streaming datasource (non-blocking pipe)

   Copyright (C) 2025
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

#define TEST_SUITE_NAME "/src/viewer/stream"

#include "tests/mctest.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/vfs/vfs.h"
#include "src/vfs/local/local.c"

#include "src/viewer/internal.h"

/* --------------------------------------------------------------------------------------------- */

/* mock: mcview_compute_areas -- no-op in test */
void
mcview_compute_areas (WView *view)
{
    (void) view;
}

/* mock: mcview_update_bytes_per_line -- no-op in test */
void
mcview_update_bytes_per_line (WView *view)
{
    (void) view;
}

/* mock: mcview_set_codeset -- no-op in test */
void
mcview_set_codeset (WView *view)
{
    (void) view;
}

/* mock: mcview_display -- no-op in test */
void
mcview_display (WView *view)
{
    (void) view;
}

/* mock: mcview_show_error -- no-op in test */
void
mcview_show_error (WView *view, const char *format, const char *filename)
{
    (void) view;
    (void) format;
    (void) filename;
}

/* mock: load_file_position -- no-op in test */
void
load_file_position (const vfs_path_t *filename_vpath, long *line, long *column, off_t *offset,
                    GArray **bookmarks)
{
    (void) filename_vpath;
    (void) line;
    (void) column;
    (void) offset;
    (void) bookmarks;
}

/* mock: message -- no-op in test */
void
message (int flags, const char *title, const char *text, ...)
{
    (void) flags;
    (void) title;
    (void) text;
}

/* --------------------------------------------------------------------------------------------- */

static WView test_view;

/* @Before */
static void
setup (void)
{
    str_init_strings (NULL);
    vfs_init ();
    vfs_init_localfs ();
    vfs_setup_work_dir ();

    memset (&test_view, 0, sizeof (test_view));
    mcview_init (&test_view);
}

/* @After */
static void
teardown (void)
{
    mcview_close_datasource (&test_view);
    vfs_path_free (test_view.filename_vpath, TRUE);
    test_view.filename_vpath = NULL;
    vfs_path_free (test_view.workdir_vpath, TRUE);
    test_view.workdir_vpath = NULL;
    g_free (test_view.command);
    test_view.command = NULL;

    vfs_shut ();
    str_uninit_strings ();
}

/* --------------------------------------------------------------------------------------------- */

/* @Test: mcview_growbuf_read_available reads data from non-blocking pipe */
START_TEST (test_growbuf_read_available_reads_pipe_data)
{
    int pipefd[2];
    mc_pipe_t *p;
    const char *test_data = "hello stream\n";
    gboolean got_data;

    /* create a real pipe and write test data */
    ck_assert_int_eq (pipe (pipefd), 0);

    /* write side */
    {
        ssize_t written = write (pipefd[1], test_data, strlen (test_data));
        (void) written;
    }
    close (pipefd[1]);

    /* set read side to non-blocking */
    {
        int flags = fcntl (pipefd[0], F_GETFL);
        fcntl (pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    /* set up mc_pipe_t with read fd */
    p = g_new0 (mc_pipe_t, 1);
    p->out.fd = pipefd[0];
    p->err.fd = -1;
    p->child_pid = -1;

    /* configure view as streaming DS_STDIO_PIPE */
    mcview_set_datasource_stdio_pipe (&test_view, p);
    test_view.streaming = TRUE;

    got_data = mcview_growbuf_read_available (&test_view);

    ck_assert (got_data);
    ck_assert_int_eq ((int) mcview_growbuf_filesize (&test_view), (int) strlen (test_data));

    {
        int ch = -1;
        gboolean ok = mcview_get_byte_growing_buffer (&test_view, 0, &ch);
        ck_assert (ok);
        ck_assert_int_eq (ch, 'h');
    }
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test: mcview_growbuf_read_available returns FALSE on EAGAIN (no data) */
START_TEST (test_growbuf_read_available_eagain)
{
    int pipefd[2];
    mc_pipe_t *p;
    gboolean got_data;

    /* create pipe but don't write anything */
    ck_assert_int_eq (pipe (pipefd), 0);

    /* set read side to non-blocking */
    {
        int flags = fcntl (pipefd[0], F_GETFL);
        fcntl (pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    p = g_new0 (mc_pipe_t, 1);
    p->out.fd = pipefd[0];
    p->err.fd = -1;
    p->child_pid = -1;

    mcview_set_datasource_stdio_pipe (&test_view, p);
    test_view.streaming = TRUE;

    got_data = mcview_growbuf_read_available (&test_view);

    ck_assert (!got_data);
    ck_assert_int_eq ((int) mcview_growbuf_filesize (&test_view), 0);
    ck_assert (!test_view.growbuf_finished);

    /* close write end so teardown can finish */
    close (pipefd[1]);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test: mcview_growbuf_read_available detects EOF */
START_TEST (test_growbuf_read_available_eof)
{
    int pipefd[2];
    mc_pipe_t *p;

    ck_assert_int_eq (pipe (pipefd), 0);

    /* close write end immediately -- read will get EOF */
    close (pipefd[1]);

    {
        int flags = fcntl (pipefd[0], F_GETFL);
        fcntl (pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    p = g_new0 (mc_pipe_t, 1);
    p->out.fd = pipefd[0];
    p->err.fd = -1;
    p->child_pid = -1;

    mcview_set_datasource_stdio_pipe (&test_view, p);
    test_view.streaming = TRUE;

    mcview_growbuf_read_available (&test_view);

    /* EOF should mark growbuf finished */
    ck_assert (test_view.growbuf_finished);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test: mcview_growbuf_read_until is no-op in streaming mode */
START_TEST (test_growbuf_read_until_noop_streaming)
{
    int pipefd[2];
    mc_pipe_t *p;

    ck_assert_int_eq (pipe (pipefd), 0);

    /* write data but don't close */
    {
        ssize_t written = write (pipefd[1], "data\n", 5);
        (void) written;
    }

    p = g_new0 (mc_pipe_t, 1);
    p->out.fd = pipefd[0];
    p->err.fd = -1;
    p->child_pid = -1;

    mcview_set_datasource_stdio_pipe (&test_view, p);
    test_view.streaming = TRUE;

    /* this must not block even though data is available */
    mcview_growbuf_read_until (&test_view, 1000);

    /* no data should have been read */
    ck_assert_int_eq ((int) mcview_growbuf_filesize (&test_view), 0);

    close (pipefd[1]);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test: streaming shutdown reaps an exited child */
START_TEST (test_stream_shutdown_no_zombie)
{
    mc_pipe_t *p;
    pid_t child_pid;
    int pipefd[2];
    pid_t result;

    /* child exits immediately */
    ck_assert_int_eq (pipe (pipefd), 0);

    child_pid = fork ();
    ck_assert (child_pid >= 0);

    if (child_pid == 0)
    {
        close (pipefd[0]);
        close (pipefd[1]);
        _exit (0);
    }

    /* keep read end for datasource */
    close (pipefd[1]);

    /* let child exit before closing datasource */
    usleep (100000);

    {
        int flags = fcntl (pipefd[0], F_GETFL);
        fcntl (pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    p = g_new0 (mc_pipe_t, 1);
    p->out.fd = pipefd[0];
    p->err.fd = -1;
    p->child_pid = child_pid;

    mcview_set_datasource_stdio_pipe (&test_view, p);
    test_view.streaming = TRUE;
    test_view.stream_active = FALSE; /* no select channel in test */

    mcview_close_datasource (&test_view);

    /* child should already be reaped */
    errno = 0;
    result = waitpid (child_pid, NULL, WNOHANG);
    ck_assert_int_eq ((int) result, -1);
    ck_assert_int_eq (errno, ECHILD);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc;

    tc = tcase_create ("stream");
    tcase_set_timeout (tc, 2);

    tcase_add_checked_fixture (tc, setup, teardown);

    tcase_add_test (tc, test_growbuf_read_available_reads_pipe_data);
    tcase_add_test (tc, test_growbuf_read_available_eagain);
    tcase_add_test (tc, test_growbuf_read_available_eof);
    tcase_add_test (tc, test_growbuf_read_until_noop_streaming);
    tcase_add_test (tc, test_stream_shutdown_no_zombie);

    return mctest_run_all (tc);
}

/* --------------------------------------------------------------------------------------------- */
