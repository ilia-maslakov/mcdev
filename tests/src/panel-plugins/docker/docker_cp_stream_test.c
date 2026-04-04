/*
   tests for docker-cp-stream.c -- child process lifecycle

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

#define TEST_SUITE_NAME "/src/panel-plugins/docker/cp-stream"

#include "tests/mctest.h"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "src/panel-plugins/docker/docker-cp-stream.h"

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
}

/* @After */
static void
teardown (void)
{
}

/* --------------------------------------------------------------------------------------------- */
/* docker_cp_stream_reap */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_reap_already_exited)
{
    docker_cp_stream_t stream;
    pid_t child;
    pid_t result;

    child = fork ();
    ck_assert (child >= 0);

    if (child == 0)
        _exit (0);

    /* let child exit */
    usleep (100000);

    stream.fd = -1;
    stream.errfd = -1;
    stream.child_pid = child;

    docker_cp_stream_reap (&stream);

    ck_assert_int_eq (stream.child_pid, -1);

    /* verify child is gone */
    errno = 0;
    result = waitpid (child, NULL, WNOHANG);
    ck_assert_int_eq ((int) result, -1);
    ck_assert_int_eq (errno, ECHILD);
}
END_TEST

START_TEST (test_reap_pid_negative)
{
    docker_cp_stream_t stream;

    stream.fd = -1;
    stream.errfd = -1;
    stream.child_pid = -1;

    /* should be no-op, no crash */
    docker_cp_stream_reap (&stream);
    ck_assert_int_eq (stream.child_pid, -1);
}
END_TEST

START_TEST (test_reap_sleeping_child)
{
    docker_cp_stream_t stream;
    pid_t child;
    pid_t result;

    child = fork ();
    ck_assert (child >= 0);

    if (child == 0)
    {
        /* put child in its own process group so SIGTERM does not propagate to test runner */
        setpgid (0, 0);
        pause ();
        _exit (0);
    }
    /* wait for child to set its process group */
    usleep (50000);

    stream.fd = -1;
    stream.errfd = -1;
    stream.child_pid = child;

    docker_cp_stream_reap (&stream);

    ck_assert_int_eq (stream.child_pid, -1);

    /* verify child is gone */
    errno = 0;
    result = waitpid (child, NULL, WNOHANG);
    ck_assert_int_eq ((int) result, -1);
    ck_assert_int_eq (errno, ECHILD);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* docker_cp_stream_read_stderr */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_stderr_with_data)
{
    docker_cp_stream_t stream;
    int pipefd[2];
    char *result;
    ssize_t written;

    ck_assert_int_eq (pipe (pipefd), 0);
    written = write (pipefd[1], "error message\n", 14);
    (void) written;
    close (pipefd[1]);

    stream.fd = -1;
    stream.errfd = pipefd[0];
    stream.child_pid = -1;

    result = docker_cp_stream_read_stderr (&stream);
    ck_assert_ptr_ne (result, NULL);
    ck_assert_str_eq (result, "error message");
    g_free (result);

    ck_assert_int_eq (stream.errfd, -1);
}
END_TEST

START_TEST (test_stderr_empty)
{
    docker_cp_stream_t stream;
    int pipefd[2];
    char *result;

    ck_assert_int_eq (pipe (pipefd), 0);
    close (pipefd[1]);

    stream.fd = -1;
    stream.errfd = pipefd[0];
    stream.child_pid = -1;

    result = docker_cp_stream_read_stderr (&stream);
    ck_assert_ptr_eq (result, NULL);
    ck_assert_int_eq (stream.errfd, -1);
}
END_TEST

START_TEST (test_stderr_long_message)
{
    docker_cp_stream_t stream;
    int pipefd[2];
    char *result;
    char buf[2048];
    ssize_t written;

    memset (buf, 'E', sizeof (buf) - 1);
    buf[sizeof (buf) - 1] = '\0';

    ck_assert_int_eq (pipe (pipefd), 0);
    written = write (pipefd[1], buf, sizeof (buf) - 1);
    (void) written;
    close (pipefd[1]);

    stream.fd = -1;
    stream.errfd = pipefd[0];
    stream.child_pid = -1;

    result = docker_cp_stream_read_stderr (&stream);
    ck_assert_ptr_ne (result, NULL);
    ck_assert_uint_eq (strlen (result), sizeof (buf) - 1);
    g_free (result);
}
END_TEST

START_TEST (test_stderr_fd_negative)
{
    docker_cp_stream_t stream;
    char *result;

    stream.fd = -1;
    stream.errfd = -1;
    stream.child_pid = -1;

    result = docker_cp_stream_read_stderr (&stream);
    ck_assert_ptr_eq (result, NULL);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc;

    tc = tcase_create ("docker-cp-stream");
    tcase_set_timeout (tc, 5);

    tcase_add_checked_fixture (tc, setup, teardown);

    /* reap */
    tcase_add_test (tc, test_reap_already_exited);
    tcase_add_test (tc, test_reap_pid_negative);
    tcase_add_test (tc, test_reap_sleeping_child);

    /* read_stderr */
    tcase_add_test (tc, test_stderr_with_data);
    tcase_add_test (tc, test_stderr_empty);
    tcase_add_test (tc, test_stderr_long_message);
    tcase_add_test (tc, test_stderr_fd_negative);

    return mctest_run_all (tc);
}

/* --------------------------------------------------------------------------------------------- */
