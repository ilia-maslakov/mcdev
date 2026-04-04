/*
   tests for docker-tar.c -- tar header parsing helpers

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

#define TEST_SUITE_NAME "/src/panel-plugins/docker/tar"

#include "tests/mctest.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "src/panel-plugins/docker/docker-tar.h"

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
/* tar_parse_octal */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_parse_octal_valid_mode)
{
    gboolean ok = FALSE;
    guint64 val = docker_tar_parse_octal ("0000644\0", 8, &ok);

    ck_assert (ok);
    ck_assert_uint_eq (val, 0644);
}
END_TEST

START_TEST (test_parse_octal_leading_spaces)
{
    gboolean ok = FALSE;
    guint64 val = docker_tar_parse_octal ("   644\0", 7, &ok);

    ck_assert (ok);
    ck_assert_uint_eq (val, 0644);
}
END_TEST

START_TEST (test_parse_octal_size_1000)
{
    gboolean ok = FALSE;
    guint64 val = docker_tar_parse_octal ("00000001750\0", 12, &ok);

    ck_assert (ok);
    ck_assert_uint_eq (val, 1000);
}
END_TEST

START_TEST (test_parse_octal_zero)
{
    gboolean ok = FALSE;
    guint64 val = docker_tar_parse_octal ("0000000\0", 8, &ok);

    ck_assert (ok);
    ck_assert_uint_eq (val, 0);
}
END_TEST

START_TEST (test_parse_octal_all_spaces)
{
    gboolean ok = FALSE;
    guint64 val = docker_tar_parse_octal ("       \0", 8, &ok);

    ck_assert (ok);
    ck_assert_uint_eq (val, 0);
}
END_TEST

START_TEST (test_parse_octal_non_octal)
{
    gboolean ok = TRUE;
    guint64 val = docker_tar_parse_octal ("0000x44\0", 8, &ok);

    ck_assert (!ok);
    ck_assert_uint_eq (val, 0);
}
END_TEST

START_TEST (test_parse_octal_binary_extension)
{
    gboolean ok = TRUE;
    char field[12];

    memset (field, 0, sizeof (field));
    field[0] = (char) 0x80; /* binary extension marker */
    field[1] = 0x01;

    guint64 val = docker_tar_parse_octal (field, 12, &ok);

    ck_assert (!ok);
    ck_assert_uint_eq (val, 0);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* tar_is_zero_block */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_zero_block_all_zeros)
{
    unsigned char block[DOCKER_TAR_BLOCK_SIZE];

    memset (block, 0, sizeof (block));
    ck_assert (docker_tar_is_zero_block (block));
}
END_TEST

START_TEST (test_zero_block_nonzero_at_start)
{
    unsigned char block[DOCKER_TAR_BLOCK_SIZE];

    memset (block, 0, sizeof (block));
    block[0] = 1;
    ck_assert (!docker_tar_is_zero_block (block));
}
END_TEST

START_TEST (test_zero_block_nonzero_at_end)
{
    unsigned char block[DOCKER_TAR_BLOCK_SIZE];

    memset (block, 0, sizeof (block));
    block[DOCKER_TAR_BLOCK_SIZE - 1] = 1;
    ck_assert (!docker_tar_is_zero_block (block));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* tar_read_full */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_read_full_exact)
{
    int pipefd[2];
    char buf[5];
    ssize_t written;

    ck_assert_int_eq (pipe (pipefd), 0);
    written = write (pipefd[1], "hello", 5);
    (void) written;
    close (pipefd[1]);

    ck_assert (docker_tar_read_full (pipefd[0], buf, 5));
    ck_assert_int_eq (memcmp (buf, "hello", 5), 0);
    close (pipefd[0]);
}
END_TEST

START_TEST (test_read_full_short_eof)
{
    int pipefd[2];
    char buf[10];
    ssize_t written;

    ck_assert_int_eq (pipe (pipefd), 0);
    written = write (pipefd[1], "abc", 3);
    (void) written;
    close (pipefd[1]);

    ck_assert (!docker_tar_read_full (pipefd[0], buf, 10));
    close (pipefd[0]);
}
END_TEST

START_TEST (test_read_full_immediate_eof)
{
    int pipefd[2];
    char buf[1];

    ck_assert_int_eq (pipe (pipefd), 0);
    close (pipefd[1]);

    ck_assert (!docker_tar_read_full (pipefd[0], buf, 1));
    close (pipefd[0]);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* tar_skip */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_skip_zero)
{
    int pipefd[2];

    ck_assert_int_eq (pipe (pipefd), 0);
    close (pipefd[1]);

    ck_assert (docker_tar_skip (pipefd[0], 0));
    close (pipefd[0]);
}
END_TEST

START_TEST (test_skip_512)
{
    int pipefd[2];
    char data[512];
    ssize_t written;

    memset (data, 'x', sizeof (data));
    ck_assert_int_eq (pipe (pipefd), 0);
    written = write (pipefd[1], data, sizeof (data));
    (void) written;
    close (pipefd[1]);

    ck_assert (docker_tar_skip (pipefd[0], 512));
    close (pipefd[0]);
}
END_TEST

START_TEST (test_skip_truncated)
{
    int pipefd[2];
    ssize_t written;

    ck_assert_int_eq (pipe (pipefd), 0);
    written = write (pipefd[1], "short", 5);
    (void) written;
    close (pipefd[1]);

    ck_assert (!docker_tar_skip (pipefd[0], 512));
    close (pipefd[0]);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* tar_read_longname */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_longname_normal)
{
    int pipefd[2];
    char block[DOCKER_TAR_BLOCK_SIZE];
    char *result;
    ssize_t written;

    memset (block, 0, sizeof (block));
    memcpy (block, "this_is_a_long_filename.txt", 27);

    ck_assert_int_eq (pipe (pipefd), 0);
    written = write (pipefd[1], block, sizeof (block));
    (void) written;
    close (pipefd[1]);

    result = docker_tar_read_longname (pipefd[0], 27);
    ck_assert_ptr_ne (result, NULL);
    ck_assert_str_eq (result, "this_is_a_long_filename.txt");
    g_free (result);
    close (pipefd[0]);
}
END_TEST

START_TEST (test_longname_oversized)
{
    int pipefd[2];
    char block[DOCKER_TAR_BLOCK_SIZE];
    char *result;
    ssize_t written;

    /* write a block that would be the payload of an oversized longname */
    memset (block, 'a', sizeof (block));
    ck_assert_int_eq (pipe (pipefd), 0);
    written = write (pipefd[1], block, sizeof (block));
    (void) written;
    close (pipefd[1]);

    /* size > 65536: should drain payload and return "" */
    result = docker_tar_read_longname (pipefd[0], 70000);
    /* pipe only has 512 bytes, so drain will fail -> NULL */
    /* but size 70000 padded > 512, so skip_full will fail */
    ck_assert_ptr_eq (result, NULL);
    close (pipefd[0]);
}
END_TEST

START_TEST (test_longname_zero_size)
{
    int pipefd[2];
    char *result;

    ck_assert_int_eq (pipe (pipefd), 0);
    close (pipefd[1]);

    result = docker_tar_read_longname (pipefd[0], 0);
    ck_assert_ptr_ne (result, NULL);
    ck_assert_str_eq (result, "");
    g_free (result);
    close (pipefd[0]);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* tar_header_get_path */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_path_name_only)
{
    struct docker_tar_header hdr;
    char *path;

    memset (&hdr, 0, sizeof (hdr));
    strcpy (hdr.name, "simple.txt");

    path = docker_tar_header_get_path (&hdr, NULL);
    ck_assert_str_eq (path, "simple.txt");
    g_free (path);
}
END_TEST

START_TEST (test_path_prefix_and_name)
{
    struct docker_tar_header hdr;
    char *path;

    memset (&hdr, 0, sizeof (hdr));
    strcpy (hdr.prefix, "usr/local");
    strcpy (hdr.name, "bin/mc");

    path = docker_tar_header_get_path (&hdr, NULL);
    ck_assert_str_eq (path, "usr/local/bin/mc");
    g_free (path);
}
END_TEST

START_TEST (test_path_longname_override)
{
    struct docker_tar_header hdr;
    char *path;

    memset (&hdr, 0, sizeof (hdr));
    strcpy (hdr.name, "short.txt");

    path = docker_tar_header_get_path (&hdr, "very/long/path/override.txt");
    ck_assert_str_eq (path, "very/long/path/override.txt");
    g_free (path);
}
END_TEST

START_TEST (test_path_empty_longname_uses_header)
{
    struct docker_tar_header hdr;
    char *path;

    memset (&hdr, 0, sizeof (hdr));
    strcpy (hdr.name, "fromheader.txt");

    path = docker_tar_header_get_path (&hdr, "");
    ck_assert_str_eq (path, "fromheader.txt");
    g_free (path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */
/* tar_header_get_linkname */
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_linkname_present)
{
    struct docker_tar_header hdr;
    char *link;

    memset (&hdr, 0, sizeof (hdr));
    strcpy (hdr.linkname, "/usr/bin/target");

    link = docker_tar_header_get_linkname (&hdr, NULL);
    ck_assert_str_eq (link, "/usr/bin/target");
    g_free (link);
}
END_TEST

START_TEST (test_linkname_empty)
{
    struct docker_tar_header hdr;
    char *link;

    memset (&hdr, 0, sizeof (hdr));

    link = docker_tar_header_get_linkname (&hdr, NULL);
    ck_assert_ptr_eq (link, NULL);
}
END_TEST

START_TEST (test_linkname_override)
{
    struct docker_tar_header hdr;
    char *link;

    memset (&hdr, 0, sizeof (hdr));
    strcpy (hdr.linkname, "short");

    link = docker_tar_header_get_linkname (&hdr, "/long/link/override");
    ck_assert_str_eq (link, "/long/link/override");
    g_free (link);
}
END_TEST

START_TEST (test_linkname_empty_override_uses_header)
{
    struct docker_tar_header hdr;
    char *link;

    memset (&hdr, 0, sizeof (hdr));
    strcpy (hdr.linkname, "fromheader");

    link = docker_tar_header_get_linkname (&hdr, "");
    ck_assert_str_eq (link, "fromheader");
    g_free (link);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc;

    tc = tcase_create ("docker-tar");

    tcase_add_checked_fixture (tc, setup, teardown);

    /* tar_parse_octal */
    tcase_add_test (tc, test_parse_octal_valid_mode);
    tcase_add_test (tc, test_parse_octal_leading_spaces);
    tcase_add_test (tc, test_parse_octal_size_1000);
    tcase_add_test (tc, test_parse_octal_zero);
    tcase_add_test (tc, test_parse_octal_all_spaces);
    tcase_add_test (tc, test_parse_octal_non_octal);
    tcase_add_test (tc, test_parse_octal_binary_extension);

    /* tar_is_zero_block */
    tcase_add_test (tc, test_zero_block_all_zeros);
    tcase_add_test (tc, test_zero_block_nonzero_at_start);
    tcase_add_test (tc, test_zero_block_nonzero_at_end);

    /* tar_read_full */
    tcase_add_test (tc, test_read_full_exact);
    tcase_add_test (tc, test_read_full_short_eof);
    tcase_add_test (tc, test_read_full_immediate_eof);

    /* tar_skip */
    tcase_add_test (tc, test_skip_zero);
    tcase_add_test (tc, test_skip_512);
    tcase_add_test (tc, test_skip_truncated);

    /* tar_read_longname */
    tcase_add_test (tc, test_longname_normal);
    tcase_add_test (tc, test_longname_oversized);
    tcase_add_test (tc, test_longname_zero_size);

    /* tar_header_get_path */
    tcase_add_test (tc, test_path_name_only);
    tcase_add_test (tc, test_path_prefix_and_name);
    tcase_add_test (tc, test_path_longname_override);
    tcase_add_test (tc, test_path_empty_longname_uses_header);

    /* tar_header_get_linkname */
    tcase_add_test (tc, test_linkname_present);
    tcase_add_test (tc, test_linkname_empty);
    tcase_add_test (tc, test_linkname_override);
    tcase_add_test (tc, test_linkname_empty_override_uses_header);

    return mctest_run_all (tc);
}

/* --------------------------------------------------------------------------------------------- */
