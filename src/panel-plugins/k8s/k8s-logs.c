/*
   Kubernetes panel plugin -- logs viewer source-controller.

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

#include <stdlib.h>  // atoi
#include <string.h>

#include "lib/global.h"
#include "lib/widget.h"
#include "lib/util.h"  // mc_pipe / popen helpers via mcviewer.h dependency

#include "src/viewer/mcviewer.h"

#include "k8s-internal.h"  // k8s_run_cmd, k8s_capture_output
#include "k8s-logs.h"

/*** file scope type declarations ****************************************************************/

/* Dialog-tunable params. live = current state; pending = under edit. */
typedef struct
{
    char *container; /* "" = first/default */
    char *since;     /* "5m"; "" = unset */
    int tail;        /* 0 = unset; >0 -> kubectl --tail=N */
    int head;        /* 0 = unset; >0 -> pipe through `head -n N` */
    gboolean previous;
    gboolean follow;
    char *pipe_through; /* "" = no formatter */
} k8s_logs_params_t;

typedef struct
{
    /* Identity (immutable for the life of the controller). */
    char *kubectl;
    char *kubeconfig;
    char *context;
    char *namespace;
    char *pod;
    char *help_file;
    int key_options; /* keycode that opens the options dialog (0 = none) */

    /* Two-phase state: live = last successfully loaded, pending = under
       edit. pending is non-NULL only between open_options() and
       commit()/rollback(). */
    k8s_logs_params_t live;
    k8s_logs_params_t *pending;

    /* Containers cache: regular + init containers, lazy-loaded on first
       open_options. NULL until populated. */
    GPtrArray *containers;
} k8s_logs_ctx_t;

/*** file scope functions ************************************************************************/

static k8s_logs_params_t *
k8s_logs_params_dup (const k8s_logs_params_t *src)
{
    k8s_logs_params_t *dst = g_new0 (k8s_logs_params_t, 1);
    dst->container = g_strdup (src->container);
    dst->since = g_strdup (src->since);
    dst->tail = src->tail;
    dst->head = src->head;
    dst->previous = src->previous;
    dst->follow = src->follow;
    dst->pipe_through = g_strdup (src->pipe_through);
    return dst;
}

/* --------------------------------------------------------------------------------------------- */

static void
k8s_logs_params_clear (k8s_logs_params_t *p)
{
    if (p == NULL)
        return;
    g_free (p->container);
    g_free (p->since);
    g_free (p->pipe_through);
    memset (p, 0, sizeof (*p));
}

/* --------------------------------------------------------------------------------------------- */

static void
k8s_logs_params_free (k8s_logs_params_t *p)
{
    if (p == NULL)
        return;
    k8s_logs_params_clear (p);
    g_free (p);
}

/* --------------------------------------------------------------------------------------------- */

/* live = pending; pending freed. */
static void
k8s_logs_params_move (k8s_logs_params_t *live, k8s_logs_params_t *pending)
{
    k8s_logs_params_clear (live);
    *live = *pending;
    /* Strings have been transferred; only the wrapper goes away. */
    g_free (pending);
}

/* --------------------------------------------------------------------------------------------- */

/* Build the shell command from identity + params. Dialog values are quoted;
 * kubectl and pipe_through are shell fragments. */
static char *
k8s_logs_build_command (const k8s_logs_ctx_t *c, const k8s_logs_params_t *p, char **err_out)
{
    GString *s;
    gchar *q;

    if (err_out != NULL)
        *err_out = NULL;

    if (p->previous && p->follow)
    {
        if (err_out != NULL)
            *err_out = g_strdup (_ ("--follow and --previous are mutually exclusive."));
        return NULL;
    }

    s = g_string_new (NULL);

    /* c->kubectl may be a shell fragment, e.g. "minikube kubectl --". */
    g_string_append (s, c->kubectl != NULL ? c->kubectl : "kubectl");

    if (c->kubeconfig != NULL && c->kubeconfig[0] != '\0')
    {
        q = g_shell_quote (c->kubeconfig);
        g_string_append_printf (s, " --kubeconfig=%s", q);
        g_free (q);
    }

    g_string_append (s, " logs");

    if (p->follow)
        g_string_append (s, " -f");
    if (p->previous)
        g_string_append (s, " --previous");

    if (p->container != NULL && p->container[0] != '\0')
    {
        /* Strip the " (init)" suffix the dialog hint adds for visibility --
           kubectl wants just the bare container name. */
        char *bare = g_strdup (p->container);
        char *paren = strstr (bare, " (init)");
        if (paren != NULL)
            *paren = '\0';
        g_strstrip (bare);
        q = g_shell_quote (bare);
        g_string_append_printf (s, " -c %s", q);
        g_free (q);
        g_free (bare);
    }

    if (p->since != NULL && p->since[0] != '\0')
    {
        q = g_shell_quote (p->since);
        g_string_append_printf (s, " --since=%s", q);
        g_free (q);
    }

    if (p->tail > 0)
        g_string_append_printf (s, " --tail=%d", p->tail);

    /* kubectl logs <pod> -n <ns> --context <ctx>  */
    q = g_shell_quote (c->pod);
    g_string_append_printf (s, " %s", q);
    g_free (q);
    q = g_shell_quote (c->namespace);
    g_string_append_printf (s, " -n %s", q);
    g_free (q);
    q = g_shell_quote (c->context);
    g_string_append_printf (s, " --context %s", q);
    g_free (q);

    /* Merge kubectl stderr into stdout before optional formatting. */
    g_string_append (s, " 2>&1");

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
        GString *ann = g_string_new ("# k8s logs: ");
        gchar *qann;
        GString *wrapped;

        g_string_append (ann, c->namespace);
        g_string_append_c (ann, '/');
        g_string_append (ann, c->pod);
        if (p->container != NULL && p->container[0] != '\0')
        {
            g_string_append_c (ann, ':');
            g_string_append (ann, p->container);
        }
        if (p->follow)
            g_string_append (ann, " [-f]");
        if (p->previous)
            g_string_append (ann, " [prev]");
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

/* Build a friendly title for the viewer header. */
static char *
k8s_logs_build_title (const k8s_logs_ctx_t *c, const k8s_logs_params_t *p)
{
    GString *s = g_string_new ("k8s logs: ");
    g_string_append_printf (s, "%s/%s", c->namespace ? c->namespace : "?", c->pod ? c->pod : "?");
    if (p->container != NULL && p->container[0] != '\0')
        g_string_append_printf (s, ":%s", p->container);
    if (p->previous)
        g_string_append (s, " [prev]");
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

/* --------------------------------------------------------------------------------------------- */

/* Populate ctx->containers (regular + init) using kubectl. Idempotent. */
static void
k8s_logs_load_containers (k8s_logs_ctx_t *c)
{
    GString *cmd;
    char *out;
    gchar *q;

    if (c->containers != NULL)
        return;
    c->containers = g_ptr_array_new_with_free_func (g_free);

    cmd = g_string_new (NULL);
    /* c->kubectl may be a shell fragment, e.g. "minikube kubectl --". */
    g_string_append (cmd, c->kubectl != NULL ? c->kubectl : "kubectl");
    if (c->kubeconfig != NULL && c->kubeconfig[0] != '\0')
    {
        q = g_shell_quote (c->kubeconfig);
        g_string_append_printf (cmd, " --kubeconfig=%s", q);
        g_free (q);
    }
    g_string_append (cmd, " get pod");
    q = g_shell_quote (c->pod);
    g_string_append_printf (cmd, " %s", q);
    g_free (q);
    q = g_shell_quote (c->namespace);
    g_string_append_printf (cmd, " -n %s", q);
    g_free (q);
    q = g_shell_quote (c->context);
    g_string_append_printf (cmd, " --context %s", q);
    g_free (q);
    /* Regular containers, one per line, then init containers tagged " (init)". */
    g_string_append (cmd,
                     " -o jsonpath='{range .spec.containers[*]}{.name}{\"\\n\"}{end}"
                     "{range .spec.initContainers[*]}{.name} (init){\"\\n\"}{end}' 2>/dev/null");

    out = k8s_capture_output (cmd->str);
    g_string_free (cmd, TRUE);

    if (out != NULL)
    {
        gchar **lines = g_strsplit (out, "\n", -1);
        for (int i = 0; lines[i] != NULL; i++)
        {
            g_strstrip (lines[i]);
            if (lines[i][0] != '\0')
                g_ptr_array_add (c->containers, g_strdup (lines[i]));
        }
        g_strfreev (lines);
        g_free (out);
    }
}

/*** controller callbacks ************************************************************************/

/* Build a "Available: a, b, init1 (init)" hint for the container input. */
static char *
k8s_logs_containers_hint (const k8s_logs_ctx_t *c)
{
    GString *s;
    guint i;

    if (c->containers == NULL || c->containers->len == 0)
        return g_strdup (_ ("Container (empty = first):"));
    s = g_string_new (_ ("Container (available: "));
    for (i = 0; i < c->containers->len; i++)
    {
        const char *n = g_ptr_array_index (c->containers, i);
        if (i > 0)
            g_string_append (s, ", ");
        g_string_append (s, n);
    }
    g_string_append (s, "):");
    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
k8s_logs_open_options (void *ctx, mcview_source_spec_t *draft)
{
    k8s_logs_ctx_t *c = (k8s_logs_ctx_t *) ctx;
    k8s_logs_params_t *p;
    char *container_in = NULL;
    char *show_in = NULL;
    char *pipe_in = NULL;
    int follow_state, previous_state;
    char show_buf[64];
    char *container_hint;
    int r;

    (void) draft;

    /* Lazy-load containers list before showing dropdown hint. */
    k8s_logs_load_containers (c);

    /* Seed pending from live. */
    c->pending = k8s_logs_params_dup (&c->live);
    p = c->pending;

    follow_state = p->follow ? 1 : 0;
    previous_state = p->previous ? 1 : 0;

    if (p->since != NULL && p->since[0] != '\0')
        g_strlcpy (show_buf, p->since, sizeof (show_buf));
    else if (p->head > 0)
        g_snprintf (show_buf, sizeof (show_buf), "head %d", p->head);
    else if (p->tail > 0)
        g_snprintf (show_buf, sizeof (show_buf), "tail %d", p->tail);
    else
        show_buf[0] = '\0';

    container_hint = k8s_logs_containers_hint (c);

    {
        /* clang-format off */
        quick_widget_t quick_widgets[] = {
            QUICK_CHECKBOX (N_ ("Follow"), &follow_state, NULL),
            QUICK_CHECKBOX (N_ ("Previous container instance"), &previous_state, NULL),
            QUICK_LABELED_INPUT (
                N_ ("Show (head [n], tail [n], 30s, 5m, 1h, 24h; empty = unset):"),
                input_label_above, show_buf, "k8s-logs-show", &show_in, NULL,
                FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (container_hint, input_label_above,
                                 p->container != NULL ? p->container : "",
                                 "k8s-logs-container", &container_in, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Pipe through (shell command, empty = raw):"),
                                 input_label_above,
                                 p->pipe_through != NULL ? p->pipe_through : "",
                                 "k8s-logs-pipe", &pipe_in, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
        };
        /* clang-format on */

        WRect rect = { -1, -1, 0, 72 };
        quick_dialog_t qdlg = {
            .rect = rect,
            .title = N_ ("Pod logs source"),
            .help = "[Pod logs source]",
            .help_file = c->help_file,
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        r = quick_dialog (&qdlg);
    }

    g_free (container_hint);

    if (r != B_ENTER)
    {
        g_free (container_in);
        g_free (show_in);
        g_free (pipe_in);
        /* Caller treats FALSE as cancel -> rollback. */
        k8s_logs_params_free (c->pending);
        c->pending = NULL;
        return FALSE;
    }

    if (follow_state && previous_state)
    {
        message (D_ERROR, MSG_ERROR, "%s", _ ("--follow and --previous are mutually exclusive."));
        g_free (container_in);
        g_free (show_in);
        g_free (pipe_in);
        k8s_logs_params_free (c->pending);
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
                    g_free (container_in);
                    g_free (show_in);
                    g_free (pipe_in);
                    k8s_logs_params_free (c->pending);
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
                    g_free (container_in);
                    g_free (show_in);
                    g_free (pipe_in);
                    k8s_logs_params_free (c->pending);
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

    g_free (p->container);
    p->container = container_in != NULL ? container_in : g_strdup ("");
    g_free (p->pipe_through);
    p->pipe_through = pipe_in != NULL ? pipe_in : g_strdup ("");
    p->follow = follow_state != 0;
    p->previous = previous_state != 0;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
k8s_logs_prepare (void *ctx, mcview_source_spec_t *draft, char **err_out)
{
    k8s_logs_ctx_t *c = (k8s_logs_ctx_t *) ctx;
    const k8s_logs_params_t *p;
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

    cmd = k8s_logs_build_command (c, p, err_out);
    if (cmd == NULL)
        return FALSE;

    title = k8s_logs_build_title (c, p);

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
k8s_logs_commit (void *ctx)
{
    k8s_logs_ctx_t *c = (k8s_logs_ctx_t *) ctx;
    if (c->pending == NULL)
        return;
    k8s_logs_params_move (&c->live, c->pending);
    c->pending = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
k8s_logs_rollback (void *ctx)
{
    k8s_logs_ctx_t *c = (k8s_logs_ctx_t *) ctx;
    if (c->pending == NULL)
        return;
    k8s_logs_params_free (c->pending);
    c->pending = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
k8s_logs_free (void *ctx)
{
    k8s_logs_ctx_t *c = (k8s_logs_ctx_t *) ctx;
    if (c == NULL)
        return;
    if (c->pending != NULL)
    {
        k8s_logs_params_free (c->pending);
        c->pending = NULL;
    }
    k8s_logs_params_clear (&c->live);
    g_free (c->kubectl);
    g_free (c->kubeconfig);
    g_free (c->context);
    g_free (c->namespace);
    g_free (c->pod);
    g_free (c->help_file);
    if (c->containers != NULL)
        g_ptr_array_free (c->containers, TRUE);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

/* Plugin owns its hotkey; the viewer delegates unhandled keys here. */
static mcv_key_result_t
k8s_logs_handle_key (void *ctx, int key)
{
    k8s_logs_ctx_t *c = (k8s_logs_ctx_t *) ctx;

    if (c->key_options != 0 && key == c->key_options)
        return MCV_KEY_OPEN_OPTIONS;
    return MCV_KEY_PASS;
}

/*** controller vtable ***************************************************************************/

static const mcview_source_controller_t k8s_logs_controller = {
    .open_options = k8s_logs_open_options,
    .prepare = k8s_logs_prepare,
    .commit = k8s_logs_commit,
    .rollback = k8s_logs_rollback,
    .free = k8s_logs_free,
    .handle_key = k8s_logs_handle_key,
};

/*** public entry ********************************************************************************/

gboolean
k8s_logs_open (const k8s_logs_identity_t *id)
{
    k8s_logs_ctx_t *c;
    mcview_source_spec_t *spec;
    char *err = NULL;

    if (id == NULL)
        return FALSE;
    if (id->context == NULL || id->context[0] == '\0')
    {
        message (D_ERROR, MSG_ERROR, "%s", _ ("No kubectl context set for logs."));
        return FALSE;
    }
    if (id->namespace == NULL || id->namespace[0] == '\0')
    {
        message (D_ERROR, MSG_ERROR, "%s", _ ("No namespace set for logs."));
        return FALSE;
    }
    if (id->pod == NULL || id->pod[0] == '\0')
    {
        message (D_ERROR, MSG_ERROR, "%s", _ ("No pod selected."));
        return FALSE;
    }

    c = g_new0 (k8s_logs_ctx_t, 1);
    c->kubectl = g_strdup (id->kubectl);
    c->kubeconfig = g_strdup (id->kubeconfig);
    c->context = g_strdup (id->context);
    c->namespace = g_strdup (id->namespace);
    c->pod = g_strdup (id->pod);
    c->help_file = g_strdup (id->help_file);
    c->key_options = id->options_key;

    c->live.container = g_strdup (id->initial_container != NULL ? id->initial_container : "");
    c->live.since = g_strdup (id->initial_since != NULL ? id->initial_since : "5m");
    c->live.tail = id->initial_tail;
    c->live.previous = id->initial_previous;
    c->live.follow = id->initial_follow;
    c->live.pipe_through =
        g_strdup (id->initial_pipe_through != NULL ? id->initial_pipe_through : "");

    /* Initial prepare uses the same pending path as reconfiguration. */
    c->pending = k8s_logs_params_dup (&c->live);

    spec = g_new0 (mcview_source_spec_t, 1);
    if (!k8s_logs_prepare (c, spec, &err))
    {
        message (D_ERROR, MSG_ERROR, "%s", err != NULL ? err : _ ("Cannot prepare logs source."));
        g_free (err);
        k8s_logs_rollback (c);
        mcview_source_spec_free (spec);
        k8s_logs_free (c);
        return FALSE;
    }
    k8s_logs_commit (c);

    /* mcview_viewer_with_controller takes ownership of spec, ctx, and calls
       controller->free on exit. */
    return mcview_viewer_with_controller (spec, &k8s_logs_controller, c, 0);
}
