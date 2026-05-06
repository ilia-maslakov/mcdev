/*
   Midnight Commander - mcterm PTY terminal widget.

   Embeds a shell inside the filemanager as a PTY-backed terminal widget
   with vterm emulation and OSC 7 panel synchronization.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

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
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_PTY_H
#include <pty.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif

#include "lib/global.h"
#include "lib/widget.h"
#include "lib/tty/tty.h"
#include "lib/tty/key.h"
#include "lib/skin.h"

#include "src/viewer/vterm.h"
#include "src/viewer/terminal_buffer.h"

#include "mcterm.h"
#include "mcterm_key.h"

/*** file scope variables ************************************************************************/

#define MCTERM_INTERNAL_SYNC_MAX_STALLED_READS 16

struct WMcTerm
{
    Widget base;
    mcview_vterm_t *vterm;
    int pty_master;
    pid_t child_pid;
    gboolean child_dead;
    int child_exit_status;
    /* Valid only when osc7_capable is TRUE. */
    gboolean shell_at_prompt;
    guint last_osc7_gen;
    gboolean osc7_capable;
    void (*on_prompt_ready) (void *data);
    void *on_prompt_ready_data;
    void (*on_after_redraw) (void *data);
    void *on_after_redraw_data;
    gboolean pending_internal_sync;
    mcview_terminal_buffer_t *sync_snapshot_buf; /* owned, freed in mcterm_free */
    int sync_snapshot_cursor_row;
    int pending_internal_sync_reads;
};

/*** forward declarations ************************************************************************/

static cb_ret_t mcterm_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data);
static gboolean mcterm_handle_osc7_generation (WMcTerm *t);
static gboolean mcterm_handle_stalled_internal_sync (WMcTerm *t);
static int mcterm_resolve_top_row_for_buf (const WMcTerm *t, const mcview_terminal_buffer_t *buf,
                                           int rows);

/*** file scope functions ************************************************************************/

static int
mcterm_pty_ready_cb (int fd, void *info)
{
    WMcTerm *t = (WMcTerm *) info;
    unsigned char buf[65536];
    ssize_t n;
    gboolean suppress_draw = FALSE;

    (void) fd;

    /* Single non-retrying read: EINTR just loses one wakeup; the next
       select() re-triggers, so no data is lost and no retry is needed. */
    n = read (t->pty_master, buf, sizeof (buf));

    if (n > 0)
    {
        ssize_t i;

        for (i = 0; i < n; i++)
        {
            vterm_event_t ev;

            ev = mcview_vterm_feed (t->vterm, buf[i]);
            mcview_vterm_apply_event (t->vterm, &ev);
            mcterm_handle_osc7_generation (t);
        }

        suppress_draw = !mcterm_handle_stalled_internal_sync (t);

        if (widget_get_state (WIDGET (t), WST_VISIBLE) && !suppress_draw)
        {
            widget_draw (WIDGET (t));
            if (t->shell_at_prompt && t->osc7_capable && t->on_prompt_ready != NULL)
                t->on_prompt_ready (t->on_prompt_ready_data);
            else
                send_message (WIDGET (t), NULL, MSG_CURSOR, 0, NULL);
            if (t->on_after_redraw != NULL)
                t->on_after_redraw (t->on_after_redraw_data);
            tty_refresh ();
        }
    }
    else if (n == 0 || (n < 0 && errno == EIO))
    {
        t->child_dead = TRUE;
        delete_select_channel (t->pty_master);
        close (t->pty_master);
        t->pty_master = -1;
        if (t->child_pid > 0)
        {
            if (waitpid (t->child_pid, &t->child_exit_status, WNOHANG) > 0)
                t->child_pid = -1;
        }
        if (widget_get_state (WIDGET (t), WST_VISIBLE))
        {
            widget_draw (WIDGET (t));
            if (t->on_after_redraw != NULL)
                t->on_after_redraw (t->on_after_redraw_data);
            tty_refresh ();
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* Keep snapshot restore ordered with OSC 7 inside a PTY read. */
static gboolean
mcterm_handle_osc7_generation (WMcTerm *t)
{
    guint gen;

    if (!t->osc7_capable)
        return FALSE;

    gen = mcview_vterm_osc7_generation (t->vterm);
    if (gen == t->last_osc7_gen)
        return FALSE;

    t->last_osc7_gen = gen;
    t->shell_at_prompt = TRUE;
    t->pending_internal_sync_reads = 0;

    if (t->pending_internal_sync)
    {
        t->pending_internal_sync = FALSE;
        if (t->sync_snapshot_buf != NULL)
        {
            mcview_vterm_restore_sync_snapshot (t->vterm, t->sync_snapshot_buf,
                                                t->sync_snapshot_cursor_row);
            t->sync_snapshot_buf = NULL;
        }
    }

    return TRUE;
}

static gboolean
mcterm_handle_stalled_internal_sync (WMcTerm *t)
{
    if (!t->pending_internal_sync)
        return TRUE;

    t->pending_internal_sync_reads++;
    if (t->pending_internal_sync_reads < MCTERM_INTERNAL_SYNC_MAX_STALLED_READS)
        return FALSE;

    t->pending_internal_sync = FALSE;
    t->pending_internal_sync_reads = 0;
    mcview_terminal_buffer_free (t->sync_snapshot_buf);
    t->sync_snapshot_buf = NULL;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static int
mcterm_resolve_top_row_for_buf (const WMcTerm *t, const mcview_terminal_buffer_t *buf, int rows)
{
    int top;
    int max;

    top = mcview_vterm_dpy_top_row (t->vterm);
    if (top >= 0)
        return top;

    max = mcview_terminal_buffer_max_row (buf);
    if (max < 0)
        return 0;

    top = max - rows + 1;
    return (top > 0) ? top : 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_exec_shell (int pty_slave, const char *start_dir)
{
    const char *shell;

    /* exec() preserves SIG_IGN; the child shell needs normal signal handling. */
    signal (SIGINT, SIG_DFL);
    signal (SIGQUIT, SIG_DFL);
    signal (SIGPIPE, SIG_DFL);

    if (setsid () < 0)
        _exit (1);
    if (ioctl (pty_slave, TIOCSCTTY, 0) < 0)
        _exit (1);

    dup2 (pty_slave, STDIN_FILENO);
    dup2 (pty_slave, STDOUT_FILENO);
    dup2 (pty_slave, STDERR_FILENO);

    if (pty_slave > STDERR_FILENO)
        close (pty_slave);

    /* Close all fds inherited from MC so they do not leak into the shell. */
    {
        int maxfd = (int) sysconf (_SC_OPEN_MAX);
        int i;

        if (maxfd <= 0)
            maxfd = 1024;
        for (i = STDERR_FILENO + 1; i < maxfd; i++)
            close (i);
    }

    shell = g_getenv ("SHELL");
    if (shell == NULL || *shell == '\0')
        shell = "/bin/sh";

    g_setenv ("TERM", "xterm-256color", TRUE);

    if (start_dir != NULL && chdir (start_dir) != 0)
    { /* fallback: shell starts in mc's cwd */
    }

    execl (shell, shell, NULL);
    _exit (127);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mcterm_write_silent (int master, const char *data, size_t len)
{
    struct termios tt;
    gboolean echo_was_on = FALSE;
    gboolean ok = TRUE;

    if (tcgetattr (master, &tt) == 0 && (tt.c_lflag & ECHO) != 0)
    {
        struct termios tt_noecho = tt;

        tt_noecho.c_lflag &= ~(tcflag_t) ECHO;
        if (tcsetattr (master, TCSANOW, &tt_noecho) == 0)
            echo_was_on = TRUE;
    }

    {
        const char *p = data;
        size_t remaining = len;

        while (remaining > 0)
        {
            ssize_t nw = write (master, p, remaining);
            if (nw < 0)
            {
                if (errno == EINTR)
                    continue;
                ok = FALSE;
                break;
            }
            if (nw == 0)
            {
                ok = FALSE;
                break;
            }
            p += nw;
            remaining -= (size_t) nw;
        }
    }

    if (echo_was_on)
        (void) tcsetattr (master, TCSANOW, &tt);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_do_draw (WMcTerm *t)
{
    const WRect *r = &WIDGET (t)->rect;

    if (t->child_dead)
    {
        int row;

        tty_setcolor (VIEWER_NORMAL_COLOR);
        for (row = 0; row < r->lines; row++)
        {
            int col;

            tty_gotoyx (r->y + row, r->x);
            for (col = 0; col < r->cols; col++)
                tty_print_char (' ');
        }
        tty_gotoyx (r->y, r->x);
        tty_print_string ("[ Process exited ]");
        return;
    }

    if (t->vterm != NULL)
    {
        gboolean use_sync_snapshot = t->pending_internal_sync && t->sync_snapshot_buf != NULL;
        mcview_terminal_buffer_t *buf =
            use_sync_snapshot ? t->sync_snapshot_buf : mcview_vterm_buf (t->vterm);
        int top_row = use_sync_snapshot ? mcterm_resolve_top_row_for_buf (t, buf, r->lines)
                                        : mcview_vterm_resolve_top_row (t->vterm, r->lines);
        int max_row = mcview_terminal_buffer_max_row (buf);
        int cursor_row =
            use_sync_snapshot ? t->sync_snapshot_cursor_row : mcview_vterm_cursor_row (t->vterm);
        int effective_max;
        if (t->shell_at_prompt && t->osc7_capable && !mcview_vterm_in_alt_screen (t->vterm))
            effective_max = cursor_row - 1;
        else
            effective_max = (cursor_row > max_row) ? cursor_row : max_row;
        if (effective_max >= top_row + r->lines)
            top_row = effective_max - r->lines + 1;
        int content_rows = (effective_max >= top_row) ? (effective_max - top_row + 1) : 0;
        if (content_rows > r->lines)
            content_rows = r->lines;
        int blank_above = (!mcview_vterm_in_alt_screen (t->vterm) && content_rows < r->lines)
            ? (r->lines - content_rows)
            : 0;

        if (blank_above > 0)
        {
            int row, col;

            tty_setcolor (VIEWER_NORMAL_COLOR);
            for (row = 0; row < blank_above; row++)
            {
                tty_gotoyx (r->y + row, r->x);
                for (col = 0; col < r->cols; col++)
                    tty_print_char (' ');
            }
        }

        if (content_rows > 0)
            mcview_render_terminal_canvas (buf, top_row, r->y + blank_above, r->x, content_rows,
                                           r->cols);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mcterm_write_all (int master, const unsigned char *data, size_t len)
{
    const unsigned char *p = data;
    size_t remaining = len;

    while (remaining > 0)
    {
        ssize_t nw = write (master, p, remaining);
        if (nw < 0)
        {
            if (errno == EINTR)
                continue;
            return FALSE;
        }
        if (nw == 0)
            return FALSE;
        p += nw;
        remaining -= (size_t) nw;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_waitpid_reap (pid_t pid)
{
    while (waitpid (pid, NULL, 0) < 0 && errno == EINTR)
        ;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mcterm_send_encoded_key (WMcTerm *t, int key)
{
    unsigned char buf[64];
    gboolean app_cursor = mcview_vterm_app_cursor_keys (t->vterm);
    size_t n = mcterm_encode_key_xterm (key, buf, sizeof (buf), app_cursor);

    if (n > 0)
        return mcterm_write_all (t->pty_master, buf, n);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
mcterm_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    WMcTerm *t = (WMcTerm *) w;

    (void) sender;
    (void) parm;

    switch (msg)
    {
    case MSG_DRAW:
        mcterm_do_draw (t);
        return MSG_HANDLED;

    case MSG_CURSOR:
        if (t->shell_at_prompt && t->osc7_capable)
            return MSG_NOT_HANDLED;
        if (t->vterm != NULL && !t->child_dead)
        {
            const WRect *r = &w->rect;
            mcview_terminal_buffer_t *buf = mcview_vterm_buf (t->vterm);
            int top_row = mcview_vterm_resolve_top_row (t->vterm, r->lines);
            int max_row = mcview_terminal_buffer_max_row (buf);
            int cursor_row = mcview_vterm_cursor_row (t->vterm);
            int effective_max = (cursor_row > max_row) ? cursor_row : max_row;
            if (effective_max >= top_row + r->lines)
                top_row = effective_max - r->lines + 1;
            int content_rows = (effective_max >= top_row) ? (effective_max - top_row + 1) : 0;
            int blank_above = (!mcview_vterm_in_alt_screen (t->vterm) && content_rows < r->lines)
                ? (r->lines - content_rows)
                : 0;
            int crow = cursor_row - top_row + blank_above;
            int ccol = mcview_vterm_cursor_col (t->vterm);

            if (crow < 0)
                crow = 0;
            if (crow >= r->lines)
                crow = r->lines - 1;
            if (ccol < 0)
                ccol = 0;
            if (ccol >= r->cols)
                ccol = r->cols - 1;
            tty_gotoyx (r->y + crow, r->x + ccol);
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    case MSG_RESIZE:
        widget_default_callback (w, NULL, MSG_RESIZE, 0, data);
        mcview_vterm_set_size (t->vterm, w->rect.lines, w->rect.cols);
        if (!t->child_dead && t->pty_master >= 0)
        {
            struct winsize ws;

            ws.ws_row = (unsigned short) w->rect.lines;
            ws.ws_col = (unsigned short) w->rect.cols;
            ws.ws_xpixel = 0;
            ws.ws_ypixel = 0;
            ioctl (t->pty_master, TIOCSWINSZ, &ws);
        }
        return MSG_HANDLED;

    case MSG_HOTKEY:
        if (!widget_get_state (w, WST_FOCUSED))
            return MSG_NOT_HANDLED;
        if (t->child_dead || t->pty_master < 0)
            return MSG_NOT_HANDLED;
        if ((parm == 0x0F || parm == XCTRL ('O'))
            && (t->vterm == NULL || !mcview_vterm_in_alt_screen (t->vterm)))
            return MSG_NOT_HANDLED;
        return mcterm_send_encoded_key (t, parm) ? MSG_HANDLED : MSG_NOT_HANDLED;

    case MSG_KEY:
        /* Alt-screen applications own Ctrl+O. */
        if ((parm == 0x0F || parm == XCTRL ('O'))
            && (t->vterm == NULL || !mcview_vterm_in_alt_screen (t->vterm)))
            return MSG_NOT_HANDLED;
        if (t->child_dead || t->pty_master < 0)
            return MSG_NOT_HANDLED;
        return mcterm_send_encoded_key (t, parm) ? MSG_HANDLED : MSG_NOT_HANDLED;

    case MSG_DESTROY:
        if (t->pty_master >= 0)
        {
            delete_select_channel (t->pty_master);
            close (t->pty_master);
            t->pty_master = -1;
        }
        if (t->child_pid > 0)
        {
            kill (t->child_pid, SIGTERM);
            {
                struct timespec ts = { 0, 10 * 1000 * 1000 }; /* 10ms */
                int i;

                for (i = 0; i < 5; i++)
                {
                    pid_t ret = waitpid (t->child_pid, NULL, WNOHANG);

                    if (ret > 0 || (ret < 0 && errno == ECHILD))
                    {
                        t->child_pid = -1;
                        break;
                    }
                    nanosleep (&ts, NULL);
                }
            }
            if (t->child_pid > 0)
            {
                kill (t->child_pid, SIGKILL);
                mcterm_waitpid_reap (t->child_pid);
                t->child_pid = -1;
            }
        }
        mcview_vterm_free (t->vterm);
        t->vterm = NULL;
        t->last_osc7_gen = 0;
        mcview_terminal_buffer_free (t->sync_snapshot_buf);
        t->sync_snapshot_buf = NULL;
        return MSG_HANDLED;

    default:
        return widget_default_callback (w, sender, msg, parm, data);
    }
}

/*** public functions ****************************************************************************/

WMcTerm *
mcterm_new (const WRect *r, const char *start_dir)
{
    WMcTerm *t;
    Widget *w;
    int master = -1, slave = -1;
    pid_t pid;

    mcterm_key_table_init (mc_global.profile_name, mc_global.main_config);

    {
        struct winsize ws;

        ws.ws_row = (unsigned short) r->lines;
        ws.ws_col = (unsigned short) r->cols;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        if (openpty (&master, &slave, NULL, NULL, &ws) < 0)
            return NULL;
    }

    pid = fork ();
    if (pid < 0)
    {
        close (master);
        close (slave);
        return NULL;
    }

    if (pid == 0)
    {
        close (master);
        mcterm_exec_shell (slave, start_dir);
        /* not reached */
    }

    close (slave);
    slave = -1;

    t = g_new0 (WMcTerm, 1);
    w = WIDGET (t);

    widget_init (w, r, mcterm_callback, NULL);
    w->options |= WOP_SELECTABLE | WOP_WANT_CURSOR | WOP_WANT_HOTKEY;

    t->pty_master = master;
    t->child_pid = pid;
    t->child_dead = FALSE;
    t->shell_at_prompt = TRUE;
    t->last_osc7_gen = 0;
    t->osc7_capable = FALSE;
    t->vterm = mcview_vterm_new ();
    mcview_vterm_set_size (t->vterm, r->lines, r->cols);

    {
        struct winsize ws;

        ws.ws_row = (unsigned short) r->lines;
        ws.ws_col = (unsigned short) r->cols;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;
        ioctl (master, TIOCSWINSZ, &ws);
    }

    add_select_channel (master, mcterm_pty_ready_cb, t);

    {
        const char *shell_name = g_getenv ("SHELL");
        const char *base;

        base = (shell_name != NULL) ? strrchr (shell_name, '/') : NULL;
        base = (base != NULL) ? base + 1 : shell_name;

        if (base != NULL && strcmp (base, "bash") == 0)
        {
            /* Percent-encoder for $PWD: safe chars pass through, everything
             * else becomes %XX.  Defined once and used in PROMPT_COMMAND. */
            static const char osc7_setup[] =
                "__mc_pe(){"
                " local s=$1 o= i c;"
                " for((i=0;i<${#s};i++)); do"
                " c=${s:i:1};"
                " case $c in"
                " [a-zA-Z0-9/_~.-]) o+=$c;;"
                " *) printf -v o '%s%%%02X' \"$o\" \"'$c\";;"
                " esac; done;"
                " printf '%s' \"$o\";"
                " }; \\\n"
                " if test $BASH_VERSINFO -ge 5"
                " && [[ ${PROMPT_COMMAND@a} == *a* ]] 2>/dev/null; then \\\n"
                "  PROMPT_COMMAND+=(\"printf '\\033]7;file://%s\\007'"
                " \\\"\\$(__mc_pe \\\"\\$PWD\\\")\\\"\"); \\\n"
                " else \\\n"
                "  PROMPT_COMMAND=\"${PROMPT_COMMAND:+$PROMPT_COMMAND; }"
                "printf '\\033]7;file://%s\\007'"
                " \\\"\\$(__mc_pe \\\"\\$PWD\\\")\\\"\"; \\\n"
                " fi\r";
            t->osc7_capable = TRUE;
            t->shell_at_prompt = FALSE; /* wait for the first real OSC 7 before allowing send */
            t->sync_snapshot_buf = mcview_terminal_buffer_copy (mcview_vterm_buf (t->vterm));
            t->sync_snapshot_cursor_row = mcview_vterm_cursor_row (t->vterm);
            t->pending_internal_sync = TRUE;
            if (!mcterm_write_silent (master, osc7_setup, sizeof (osc7_setup) - 1))
            {
                /* Write failed: demote to dumb mode so the terminal stays usable. */
                t->osc7_capable = FALSE;
                t->shell_at_prompt = TRUE;
                t->pending_internal_sync = FALSE;
                mcview_terminal_buffer_free (t->sync_snapshot_buf);
                t->sync_snapshot_buf = NULL;
            }
        }
    }

    return t;
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_free (WMcTerm *t)
{
    if (t == NULL)
        return;
    send_message (WIDGET (t), NULL, MSG_DESTROY, 0, NULL);
    g_free (t);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_is_alive (const WMcTerm *t)
{
    return (t != NULL && !t->child_dead);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_in_alt_screen (const WMcTerm *t)
{
    return (t != NULL && t->vterm != NULL && mcview_vterm_in_alt_screen (t->vterm));
}

/* --------------------------------------------------------------------------------------------- */

Widget *
mcterm_widget (WMcTerm *t)
{
    return WIDGET (t);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_send_line (WMcTerm *t, const char *line)
{
    if (t == NULL || t->child_dead || t->pty_master < 0)
        return FALSE;

    if (line != NULL && *line != '\0')
    {
        if (!mcterm_write_silent (t->pty_master, line, strlen (line)))
            return FALSE;
    }
    if (!mcterm_write_silent (t->pty_master, "\r", 1))
        return FALSE;
    if (t->osc7_capable)
    {
        t->shell_at_prompt = FALSE;
        t->last_osc7_gen = mcview_vterm_osc7_generation (t->vterm);
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_send_internal_line (WMcTerm *t, const char *line)
{
    if (t == NULL || line == NULL)
        return FALSE;

    mcview_terminal_buffer_free (t->sync_snapshot_buf);

    t->sync_snapshot_buf = mcview_terminal_buffer_copy (mcview_vterm_buf (t->vterm));
    t->sync_snapshot_cursor_row = mcview_vterm_cursor_row (t->vterm);
    t->pending_internal_sync = TRUE;
    t->pending_internal_sync_reads = 0;

    return mcterm_send_line (t, line);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_shell_at_prompt (const WMcTerm *t)
{
    return (t != NULL && !t->child_dead && t->shell_at_prompt);
}

/* --------------------------------------------------------------------------------------------- */

const char *
mcterm_osc7_raw (const WMcTerm *t)
{
    if (t == NULL || t->vterm == NULL)
        return NULL;
    return mcview_vterm_osc7_raw (t->vterm);
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_set_prompt_callback (WMcTerm *t, void (*cb) (void *), void *data)
{
    if (t == NULL)
        return;
    t->on_prompt_ready = cb;
    t->on_prompt_ready_data = data;
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_set_after_redraw_callback (WMcTerm *t, void (*cb) (void *), void *data)
{
    if (t == NULL)
        return;
    t->on_after_redraw = cb;
    t->on_after_redraw_data = data;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_osc7_capable (const WMcTerm *t)
{
    return (t != NULL && t->osc7_capable);
}

/* --------------------------------------------------------------------------------------------- */

int
mcterm_cursor_col (const WMcTerm *t)
{
    if (t == NULL || t->vterm == NULL)
        return 0;
    return mcview_vterm_cursor_col (t->vterm);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcterm_send_tab_complete (WMcTerm *t, const char *text)
{
    if (t == NULL || t->child_dead || t->pty_master < 0)
        return FALSE;

    if (text != NULL && *text != '\0')
    {
        if (!mcterm_write_all (t->pty_master, (const unsigned char *) text, strlen (text)))
            return FALSE;
    }
    if (!mcterm_write_all (t->pty_master, (const unsigned char *) "\t", 1))
        return FALSE;

    if (t->osc7_capable)
    {
        t->shell_at_prompt = FALSE;
        t->last_osc7_gen = mcview_vterm_osc7_generation (t->vterm);
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
mcterm_draw_prompt_row (const WMcTerm *t, int screen_y)
{
    const WRect *r;
    mcview_terminal_buffer_t *buf;
    int cursor_row;

    if (t == NULL || t->vterm == NULL || t->child_dead)
        return;

    r = &CONST_WIDGET (t)->rect;
    buf = mcview_vterm_buf (t->vterm);
    cursor_row = mcview_vterm_cursor_row (t->vterm);
    mcview_render_terminal_canvas (buf, cursor_row, screen_y, r->x, 1, r->cols);
}
