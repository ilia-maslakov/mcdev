/*
   Docker panel plugin -- logs viewer source-controller.

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

#include <stdlib.h>  // strtol
#include <string.h>

#include "lib/global.h"
#include "lib/widget.h"

#include "src/viewer/mcviewer.h"

#include "docker-internal.h"  // docker_connection_*, docker_conn_build_pipe_cmd
#include "docker-logs.h"

/*** file scope type declarations ****************************************************************/

/* Dialog-tunable params. live = current state; pending = under edit. */
typedef struct
{
    char *since; /* "5m"; "" = unset */
    int tail;    /* 0 = unset; >0 -> docker logs --tail=N */
    int head;    /* 0 = unset; >0 -> pipe through `head -n N` */
    gboolean follow;
    char *pipe_through; /* "" = no formatter */
} docker_logs_params_t;

typedef struct
{
    /* Identity (immutable for the life of the controller). */
    docker_connection_t *conn; /* owned clone */
    char *container_id;
    char *container_name;
    char *help_file;
    int key_options; /* keycode that opens the options dialog (0 = none) */

    /* Two-phase state: live = last successfully loaded, pending = under
       edit. pending is non-NULL only between open_options() and
       commit()/rollback(). */
    docker_logs_params_t live;
    docker_logs_params_t *pending;
} docker_logs_ctx_t;

/*** file scope functions ************************************************************************/

static docker_logs_params_t *
docker_logs_params_dup (const docker_logs_params_t *src)
{
    docker_logs_params_t *dst = g_new0 (docker_logs_params_t, 1);
    dst->since = g_strdup (src->since);
    dst->tail = src->tail;
    dst->head = src->head;
    dst->follow = src->follow;
    dst->pipe_through = g_strdup (src->pipe_through);
    return dst;
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_logs_params_clear (docker_logs_params_t *p)
{
    if (p == NULL)
        return;
    g_free (p->since);
    g_free (p->pipe_through);
    memset (p, 0, sizeof (*p));
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_logs_params_free (docker_logs_params_t *p)
{
    if (p == NULL)
        return;
    docker_logs_params_clear (p);
    g_free (p);
}

/* --------------------------------------------------------------------------------------------- */

/* live = pending; pending freed. */
static void
docker_logs_params_move (docker_logs_params_t *live, docker_logs_params_t *pending)
{
    docker_logs_params_clear (live);
    *live = *pending;
    g_free (pending);
}

/* --------------------------------------------------------------------------------------------- */

/* Build the shell command from identity + params. The docker invocation is
 * wrapped by docker_conn_build_pipe_cmd (local or ssh); head/formatter and the
 * announcement run locally on top of it. */
static char *
docker_logs_build_command (const docker_logs_ctx_t *c, const docker_logs_params_t *p)
{
    GString *args;
    GString *s;
    gchar *q;
    char *base;

    /* docker subcommand args, wrapped by the connection (local or ssh). */
    args = g_string_new ("logs");
    if (p->follow)
        g_string_append (args, " --follow");
    if (p->since != NULL && p->since[0] != '\0')
    {
        q = g_shell_quote (p->since);
        g_string_append_printf (args, " --since=%s", q);
        g_free (q);
    }
    if (p->tail > 0)
        g_string_append_printf (args, " --tail=%d", p->tail);
    q = g_shell_quote (c->container_id);
    g_string_append_printf (args, " %s", q);
    g_free (q);
    /* Merge stderr into stdout before optional formatting. */
    g_string_append (args, " 2>&1");

    base = docker_conn_build_pipe_cmd (c->conn, args->str);
    g_string_free (args, TRUE);

    s = g_string_new (base);
    g_free (base);

    if (p->head > 0)
        g_string_append_printf (s, " | head -n %d", p->head);

    if (p->pipe_through != NULL && p->pipe_through[0] != '\0')
    {
        g_string_append (s, " | ");
        g_string_append (s, p->pipe_through);
        g_string_append (s, " 2>&1");
    }

    /* Keep the announcement outside the formatter pipe. */
    {
        const char *who = c->container_name != NULL && c->container_name[0] != '\0'
            ? c->container_name
            : c->container_id;
        GString *ann = g_string_new ("# docker logs: ");
        gchar *qann;
        GString *wrapped;

        g_string_append (ann, who);
        if (p->follow)
            g_string_append (ann, " [-f]");
        if (p->since != NULL && p->since[0] != '\0')
            g_string_append_printf (ann, " since=%s", p->since);
        if (p->tail > 0)
            g_string_append_printf (ann, " tail=%d", p->tail);
        if (p->head > 0)
            g_string_append_printf (ann, " head=%d", p->head);
        if (p->pipe_through != NULL && p->pipe_through[0] != '\0')
            g_string_append_printf (ann, " | %s", p->pipe_through);
        g_string_append (ann, " ... waiting for first output");

        qann = g_shell_quote (ann->str);
        g_string_free (ann, TRUE);

        wrapped = g_string_new ("{ printf '%s\\n' ");
        g_string_append (wrapped, qann);
        g_string_append (wrapped, " ; ");
        g_string_append (wrapped, s->str);
        g_string_append (wrapped, " ; }");
        g_free (qann);
        g_string_assign (s, wrapped->str);
        g_string_free (wrapped, TRUE);
    }

    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static char *
docker_logs_build_title (const docker_logs_ctx_t *c, const docker_logs_params_t *p)
{
    const char *who = c->container_name != NULL && c->container_name[0] != '\0' ? c->container_name
                                                                                : c->container_id;
    GString *s = g_string_new ("docker logs: ");
    g_string_append (s, who != NULL ? who : "?");
    if (p->follow)
        g_string_append (s, " [-f]");
    if (p->since != NULL && p->since[0] != '\0')
        g_string_append_printf (s, " since=%s", p->since);
    if (p->tail > 0)
        g_string_append_printf (s, " tail=%d", p->tail);
    if (p->head > 0)
        g_string_append_printf (s, " head=%d", p->head);
    return g_string_free (s, FALSE);
}

/*** controller callbacks ************************************************************************/

static gboolean
docker_logs_open_options (void *ctx, mcview_source_spec_t *draft)
{
    docker_logs_ctx_t *c = (docker_logs_ctx_t *) ctx;
    docker_logs_params_t *p;
    char *show_in = NULL;
    char *pipe_in = NULL;
    int follow_state;
    char show_buf[64];
    int r;

    (void) draft;

    /* Seed pending from live. */
    c->pending = docker_logs_params_dup (&c->live);
    p = c->pending;

    follow_state = p->follow ? 1 : 0;

    if (p->since != NULL && p->since[0] != '\0')
        g_strlcpy (show_buf, p->since, sizeof (show_buf));
    else if (p->head > 0)
        g_snprintf (show_buf, sizeof (show_buf), "head %d", p->head);
    else if (p->tail > 0)
        g_snprintf (show_buf, sizeof (show_buf), "tail %d", p->tail);
    else
        show_buf[0] = '\0';

    {
        /* clang-format off */
        quick_widget_t quick_widgets[] = {
            QUICK_CHECKBOX (N_ ("Follow"), &follow_state, NULL),
            QUICK_LABELED_INPUT (
                N_ ("Show (head [n], tail [n], 30s, 5m, 1h, 24h; empty = unset):"),
                input_label_above, show_buf, "docker-logs-show", &show_in, NULL,
                FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Pipe through (shell command, empty = raw):"),
                                 input_label_above,
                                 p->pipe_through != NULL ? p->pipe_through : "",
                                 "docker-logs-pipe", &pipe_in, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
        };
        /* clang-format on */

        WRect rect = { -1, -1, 0, 72 };
        quick_dialog_t qdlg = {
            .rect = rect,
            .title = N_ ("Container logs source"),
            .help = "[Container logs source]",
            .help_file = c->help_file,
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        r = quick_dialog (&qdlg);
    }

    if (r != B_ENTER)
    {
        g_free (show_in);
        g_free (pipe_in);
        /* Caller treats FALSE as cancel -> rollback. */
        docker_logs_params_free (c->pending);
        c->pending = NULL;
        return FALSE;
    }

    {
        const char *s = show_in != NULL ? show_in : "";
        char *new_since = NULL;
        int new_tail = 0;
        int new_head = 0;

        while (*s == ' ' || *s == '\t')
            s++;

        if (*s == '\0')
        {
            new_since = g_strdup ("");
        }
        else if (g_str_has_prefix (s, "head"))
        {
            const char *rest = s + 4;
            while (*rest == ' ' || *rest == '\t')
                rest++;
            new_since = g_strdup ("");
            if (*rest == '\0')
                new_head = 20;
            else
            {
                char *endp = NULL;
                long n = strtol (rest, &endp, 10);
                if (endp == rest || *endp != '\0' || n <= 0 || n > G_MAXINT)
                {
                    message (D_ERROR, MSG_ERROR, _ ("Show: expected 'head' or 'head <N>'; got %s."),
                             s);
                    g_free (new_since);
                    g_free (show_in);
                    g_free (pipe_in);
                    docker_logs_params_free (c->pending);
                    c->pending = NULL;
                    return FALSE;
                }
                new_head = (int) n;
            }
        }
        else if (g_str_has_prefix (s, "tail"))
        {
            const char *rest = s + 4;
            while (*rest == ' ' || *rest == '\t')
                rest++;
            new_since = g_strdup ("");
            if (*rest == '\0')
                new_tail = 100;
            else
            {
                char *endp = NULL;
                long n = strtol (rest, &endp, 10);
                if (endp == rest || *endp != '\0' || n < 0 || n > G_MAXINT)
                {
                    message (D_ERROR, MSG_ERROR, _ ("Show: expected 'tail' or 'tail <N>'; got %s."),
                             s);
                    g_free (new_since);
                    g_free (show_in);
                    g_free (pipe_in);
                    docker_logs_params_free (c->pending);
                    c->pending = NULL;
                    return FALSE;
                }
                new_tail = (int) n;
            }
        }
        else
        {
            new_since = g_strdup (s);
        }

        g_free (p->since);
        p->since = new_since;
        p->tail = new_tail;
        p->head = new_head;
    }
    g_free (show_in);

    g_free (p->pipe_through);
    p->pipe_through = pipe_in != NULL ? pipe_in : g_strdup ("");
    p->follow = follow_state != 0;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
docker_logs_prepare (void *ctx, mcview_source_spec_t *draft, char **err_out)
{
    docker_logs_ctx_t *c = (docker_logs_ctx_t *) ctx;
    const docker_logs_params_t *p;
    char *cmd;
    char *title;

    if (err_out != NULL)
        *err_out = NULL;

    if (c->pending == NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("internal: prepare called without pending state");
        return FALSE;
    }
    p = c->pending;

    cmd = docker_logs_build_command (c, p);
    if (cmd == NULL)
        return FALSE;

    title = docker_logs_build_title (c, p);

    g_free (draft->command);
    draft->command = cmd;
    g_free (draft->file);
    draft->file = NULL;
    g_free (draft->title);
    draft->title = title;
    draft->auto_scroll_bottom = p->follow;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_logs_commit (void *ctx)
{
    docker_logs_ctx_t *c = (docker_logs_ctx_t *) ctx;
    if (c->pending == NULL)
        return;
    docker_logs_params_move (&c->live, c->pending);
    c->pending = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_logs_rollback (void *ctx)
{
    docker_logs_ctx_t *c = (docker_logs_ctx_t *) ctx;
    if (c->pending == NULL)
        return;
    docker_logs_params_free (c->pending);
    c->pending = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
docker_logs_free (void *ctx)
{
    docker_logs_ctx_t *c = (docker_logs_ctx_t *) ctx;
    if (c == NULL)
        return;
    if (c->pending != NULL)
    {
        docker_logs_params_free (c->pending);
        c->pending = NULL;
    }
    docker_logs_params_clear (&c->live);
    if (c->conn != NULL)
        docker_connection_free (c->conn);
    g_free (c->container_id);
    g_free (c->container_name);
    g_free (c->help_file);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

/* Plugin owns its hotkey; the viewer delegates unhandled keys here. */
static mcv_key_result_t
docker_logs_handle_key (void *ctx, int key)
{
    docker_logs_ctx_t *c = (docker_logs_ctx_t *) ctx;

    if (c->key_options != 0 && key == c->key_options)
        return MCV_KEY_OPEN_OPTIONS;
    return MCV_KEY_PASS;
}

/*** controller vtable ***************************************************************************/

static const mcview_source_controller_t docker_logs_controller = {
    .open_options = docker_logs_open_options,
    .prepare = docker_logs_prepare,
    .commit = docker_logs_commit,
    .rollback = docker_logs_rollback,
    .free = docker_logs_free,
    .handle_key = docker_logs_handle_key,
};

/*** public entry ********************************************************************************/

gboolean
docker_logs_open (const docker_logs_identity_t *id)
{
    docker_logs_ctx_t *c;
    mcview_source_spec_t *spec;
    char *err = NULL;

    if (id == NULL)
        return FALSE;
    if (id->container_id == NULL || id->container_id[0] == '\0')
    {
        message (D_ERROR, MSG_ERROR, "%s", _ ("No container selected."));
        return FALSE;
    }

    c = g_new0 (docker_logs_ctx_t, 1);
    c->conn = id->conn != NULL ? docker_connection_clone (id->conn) : NULL;
    c->container_id = g_strdup (id->container_id);
    c->container_name = g_strdup (id->container_name);
    c->help_file = g_strdup (id->help_file);
    c->key_options = id->options_key;

    c->live.since = g_strdup (id->initial_since != NULL ? id->initial_since : "");
    c->live.tail = id->initial_tail;
    c->live.follow = id->initial_follow;
    c->live.pipe_through =
        g_strdup (id->initial_pipe_through != NULL ? id->initial_pipe_through : "");

    /* Initial prepare uses the same pending path as reconfiguration. */
    c->pending = docker_logs_params_dup (&c->live);

    spec = g_new0 (mcview_source_spec_t, 1);
    if (!docker_logs_prepare (c, spec, &err))
    {
        message (D_ERROR, MSG_ERROR, "%s", err != NULL ? err : _ ("Cannot prepare logs source."));
        g_free (err);
        docker_logs_rollback (c);
        mcview_source_spec_free (spec);
        docker_logs_free (c);
        return FALSE;
    }
    docker_logs_commit (c);

    /* mcview_viewer_with_controller takes ownership of spec, ctx, and calls
       controller->free on exit. */
    return mcview_viewer_with_controller (spec, &docker_logs_controller, c, 0);
}

/* --------------------------------------------------------------------------------------------- */
