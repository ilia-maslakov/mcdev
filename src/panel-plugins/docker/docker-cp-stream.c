/*
   Docker cp stream helpers.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026.

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

#include <config.h>

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/global.h"

#include "docker-cp-stream.h"

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_cp_stream_open (const char *container_id, const char *container_path,
                       docker_cp_stream_t *stream, char **err_text)
{
    char *copy_src;
    char *quoted_id;
    char *quoted_src;
    char *cmd;
    int pipefd[2];
    int errpipe[2] = { -1, -1 };

    stream->fd = -1;
    stream->errfd = -1;
    stream->child_pid = -1;

    copy_src = (strcmp (container_path, "/") == 0) ? g_strdup ("/.")
                                                   : g_strdup_printf ("%s/.", container_path);
    quoted_id = g_shell_quote (container_id);
    quoted_src = g_shell_quote (copy_src);
    cmd = g_strdup_printf ("docker cp %s:%s -", quoted_id, quoted_src);
    g_free (quoted_src);
    g_free (quoted_id);
    g_free (copy_src);

    if (pipe (pipefd) != 0)
    {
        if (err_text != NULL)
            *err_text = g_strdup ("pipe() failed");
        g_free (cmd);
        return FALSE;
    }

    if (pipe (errpipe) != 0)
    {
        errpipe[0] = -1;
        errpipe[1] = -1;
    }

    stream->child_pid = fork ();
    if (stream->child_pid < 0)
    {
        close (pipefd[0]);
        close (pipefd[1]);
        if (errpipe[0] >= 0)
            close (errpipe[0]);
        if (errpipe[1] >= 0)
            close (errpipe[1]);
        if (err_text != NULL)
            *err_text = g_strdup ("fork() failed");
        g_free (cmd);
        return FALSE;
    }

    if (stream->child_pid == 0)
    {
        close (pipefd[0]);
        if (pipefd[1] != STDOUT_FILENO)
        {
            dup2 (pipefd[1], STDOUT_FILENO);
            close (pipefd[1]);
        }
        if (errpipe[0] >= 0)
            close (errpipe[0]);
        if (errpipe[1] >= 0 && errpipe[1] != STDERR_FILENO)
        {
            dup2 (errpipe[1], STDERR_FILENO);
            close (errpipe[1]);
        }
        execl ("/bin/sh", "sh", "-c", cmd, (char *) NULL);
        _exit (127);
    }

    close (pipefd[1]);
    if (errpipe[1] >= 0)
        close (errpipe[1]);
    g_free (cmd);

    stream->fd = pipefd[0];
    stream->errfd = errpipe[0];

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_cp_stream_read_stderr (docker_cp_stream_t *stream)
{
    GString *errmsg;
    char errbuf[1024];
    ssize_t n;

    if (stream->errfd < 0)
        return NULL;

    errmsg = g_string_new (NULL);

    while (TRUE)
    {
        do
        {
            n = read (stream->errfd, errbuf, sizeof (errbuf));
        }
        while (n == -1 && errno == EINTR);

        if (n <= 0)
            break;
        g_string_append_len (errmsg, errbuf, n);
    }

    close (stream->errfd);
    stream->errfd = -1;

    while (errmsg->len > 0
           && (errmsg->str[errmsg->len - 1] == '\n' || errmsg->str[errmsg->len - 1] == '\r'))
        g_string_truncate (errmsg, errmsg->len - 1);

    if (errmsg->len == 0)
    {
        g_string_free (errmsg, TRUE);
        return NULL;
    }

    return g_string_free (errmsg, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

void
docker_cp_stream_reap (docker_cp_stream_t *stream)
{
    int i;
    pid_t w;

    if (stream->child_pid < 0)
        return;

    /* reap an exited child without sending signals */
    w = waitpid (stream->child_pid, NULL, WNOHANG);
    if (w == stream->child_pid || (w == -1 && errno != EINTR))
        goto done;

    /* wait briefly for normal exit */
    for (i = 0; i < 10; i++)
    {
        usleep (10000); /* 10ms, total max 100ms */
        w = waitpid (stream->child_pid, NULL, WNOHANG);
        if (w == stream->child_pid || (w == -1 && errno != EINTR))
            goto done;
    }

    /* terminate if still running */
    kill (stream->child_pid, SIGTERM);
    for (i = 0; i < 10; i++)
    {
        usleep (10000);
        w = waitpid (stream->child_pid, NULL, WNOHANG);
        if (w == stream->child_pid || (w == -1 && errno != EINTR))
            goto done;
    }

    /* force kill as last resort */
    kill (stream->child_pid, SIGKILL);
    do
    {
        w = waitpid (stream->child_pid, NULL, 0);
    }
    while (w == -1 && errno == EINTR);

done:
    stream->child_pid = -1;
}
