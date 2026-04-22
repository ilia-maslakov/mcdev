/*
   Internal file viewer for the Midnight Commander
   Line filter: grep-style filtered view over the same datasource.

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
#include <string.h>

#include "lib/global.h"
#include "lib/search.h"
#include "lib/strutil.h"
#include "lib/charsets.h"
#include "lib/tty/tty.h"
#include "lib/tty/color.h"
#include "lib/widget.h"
#include "lib/event-types.h"
#include "lib/event.h"

#include "src/history.h"

#include "internal.h"

/*** global variables ****************************************************************************/

/* Default filter options.  case_sens=FALSE by default (matches typical log-filtering expectations).
 */
mcview_filter_options_t mcview_filter_options = {
    .type = MC_SEARCH_T_NORMAL,
    .case_sens = FALSE,
    .whole_words = FALSE,
    .all_codepages = FALSE,
};

/*** file scope macro definitions ****************************************************************/

/* Extra button code for [ Check pattern ] in the filter dialog. */
#define B_CHECK (B_USER + 1)

/* Width of the Regex Check dialog. */
#define REGEX_CHECK_COLS 58
/* Left/right content margin inside the Regex Check dialog. */
#define REGEX_CHECK_UX 3

/*** file scope type declarations ****************************************************************/

/* Preview line with optional highlighted match. */
typedef struct
{
    Widget widget;
    char *text;
    int match_start; /* byte offset of match, -1 = no highlight */
    int match_len;   /* byte length of match */
} WPreviewLine;

typedef struct
{
    WView *view;
    WInput *input;
    WLabel *status_label;
    WPreviewLine **preview_lines;
    int preview_rows;
    WButton *test_button;
    mcview_filter_options_t opts;
} regex_check_ctx_t;

typedef struct
{
    WView *view;
    WInput *input;
    WRadio *type_radio;
    WCheck *case_check;
    WCheck *words_check;
    WCheck *charset_check;
} filter_dlg_ctx_t;

/*** forward declarations (file scope functions) *************************************************/

static cb_ret_t preview_line_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm,
                                       void *data);
static int regex_check_test_cb (WButton *button, int action);
static cb_ret_t regex_check_dlg_cb (Widget *w, Widget *sender, widget_msg_t msg, int parm,
                                    void *data);
static int filter_check_btn_cb (WButton *button, int action);

/*** file scope variables ************************************************************************/

static regex_check_ctx_t regex_check_ctx;
static filter_dlg_ctx_t filter_dlg_ctx;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

/* Return the byte offset from which to start drawing a preview line so that
 * the match at byte offset match_start is visible within cols terminal columns.
 * Uses display-column arithmetic to handle wide (multi-column) characters. */
static int
preview_line_shift_start (const char *text, int match_start, int cols)
{
    char *prefix;
    int prefix_w, ctx_cols, target_col;

    if (match_start <= 0)
        return 0;

    prefix = g_strndup (text, match_start);
    prefix_w = str_term_width1 (prefix);
    g_free (prefix);

    if (prefix_w < cols)
        return 0;

    ctx_cols = MIN (4, cols / 4);
    target_col = MAX (0, prefix_w - ctx_cols);
    /* str_column_to_pos returns a byte offset aligned to a char boundary. */
    return str_column_to_pos (text, (size_t) target_col);
}

/* --------------------------------------------------------------------------------------------- */

static void
preview_line_print_chunk (const char *text, int len, int cols, int *drawn, int color)
{
    char *seg;
    int seg_w;

    if (len <= 0 || *drawn >= cols)
        return;

    tty_setcolor (color);

    seg = g_strndup (text, len);
    seg_w = str_term_width1 (seg);
    tty_print_string (str_fit_to_term (seg, MIN (seg_w, cols - *drawn), J_LEFT));
    *drawn += MIN (seg_w, cols - *drawn);
    g_free (seg);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
preview_line_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    WPreviewLine *pl = (WPreviewLine *) w;

    (void) sender;
    (void) parm;
    (void) data;

    switch (msg)
    {
    case MSG_DRAW:
    {
        const char *text = pl->text != NULL ? pl->text : "";
        int tlen = (int) strlen (text);
        int cols = w->rect.cols;
        int drawn = 0;

        widget_gotoyx (w, 0, 0);

        if (pl->match_start < 0 || pl->match_len <= 0 || pl->match_start >= tlen)
        {
            tty_setcolor (dialog_colors[DLG_COLOR_NORMAL]);
            tty_print_string (str_fit_to_term (text, cols, J_LEFT));
        }
        else
        {
            int ms = MIN (pl->match_start, tlen);
            int me = MIN (ms + pl->match_len, tlen);
            int view_start;

            view_start = preview_line_shift_start (text, ms, cols);
            if (view_start != 0)
            {
                tty_setcolor (dialog_colors[DLG_COLOR_NORMAL]);
                tty_print_char ('~');
                drawn = 1;
            }

            preview_line_print_chunk (text + view_start, ms - view_start, cols, &drawn,
                                      dialog_colors[DLG_COLOR_NORMAL]);
            preview_line_print_chunk (text + ms, me - ms, cols, &drawn, input_colors[WINPUTC_MARK]);

            if (drawn < cols)
            {
                tty_setcolor (dialog_colors[DLG_COLOR_NORMAL]);
                tty_print_string (str_fit_to_term (text + me, cols - drawn, J_LEFT));
            }
        }
    }
        return MSG_HANDLED;

    case MSG_DESTROY:
        g_free (pl->text);
        pl->text = NULL;
        return MSG_HANDLED;

    default:
        return widget_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static WPreviewLine *
preview_line_new (int y, int x, int cols)
{
    WPreviewLine *pl;

    pl = g_new0 (WPreviewLine, 1);
    widget_init (WIDGET (pl), &(WRect) { y, x, 1, cols }, preview_line_callback, NULL);
    pl->match_start = -1;
    return pl;
}

/* --------------------------------------------------------------------------------------------- */

static void
preview_line_set (WPreviewLine *pl, const char *text, int match_start, int match_len)
{
    g_free (pl->text);
    pl->text = text != NULL ? g_strdup (text) : NULL;
    pl->match_start = match_start;
    pl->match_len = match_len;
    widget_draw (WIDGET (pl));
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

int
mcview_filter_preview_scan (WView *view, const char *pattern, const mcview_filter_options_t *opts,
                            mcview_preview_match_t *out, int max_matches, gchar **err)
{
    mc_search_t *engine;
    off_t pos, bol, filesize;
    int lines_scanned = 0;
    off_t bytes_scanned = 0;
    int found = 0;

    if (err != NULL)
        *err = NULL;

    engine = mc_search_new (pattern, NULL);
    if (engine == NULL)
    {
        if (err != NULL)
            *err = g_strdup (_ ("Error: out of memory"));
        return -1;
    }

    engine->search_type = opts->type;
    engine->is_case_sensitive = opts->case_sens;
    engine->whole_words = opts->whole_words;
    engine->is_all_charsets = opts->all_codepages;

    if (!mc_search_prepare (engine))
    {
        if (err != NULL)
            *err = g_strdup_printf (_ ("Error: %s"),
                                    engine->error_str != NULL ? engine->error_str : _ ("unknown"));
        mc_search_free (engine);
        return -1;
    }

    filesize = mcview_get_filesize (view);
    bol = view->dpy_start;
    pos = bol;

    while (pos < filesize && found < max_matches)
    {
        int c;
        gsize found_len;
        off_t len, j;
        int bc;
        gchar *buf;

        if (bytes_scanned >= MCVIEW_FILTER_BUDGET_BYTES
            || lines_scanned >= MCVIEW_FILTER_BUDGET_LINES)
            break;

        if (!mcview_get_byte (view, pos, &c))
            break;

        bytes_scanned++;
        pos++;

        if (c != '\n')
            continue;

        len = pos - bol;
        buf = g_new (gchar, len + 1);
        for (j = 0; j < len; j++)
        {
            if (!mcview_get_byte (view, bol + j, &bc))
                break;
            buf[j] = (gchar) bc;
        }
        buf[j] = '\0';

        found_len = 0;
        if (mc_search_run (engine, buf, 0, j, &found_len))
        {
            if (j > 0 && buf[j - 1] == '\n')
                buf[j - 1] = '\0';
            out[found].text = buf;
            out[found].match_start = (int) engine->normal_offset;
            out[found].match_len = (int) found_len;
            found++;
        }
        else
            g_free (buf);

        lines_scanned++;
        bol = pos;
    }

    /* Flush last partial line (file without trailing newline). */
    if (pos > bol && found < max_matches)
    {
        off_t len = pos - bol;
        gsize found_len;
        off_t j;
        int bc;
        gchar *buf;

        buf = g_new (gchar, len + 1);
        for (j = 0; j < len; j++)
        {
            if (!mcview_get_byte (view, bol + j, &bc))
                break;
            buf[j] = (gchar) bc;
        }
        buf[j] = '\0';

        found_len = 0;
        if (mc_search_run (engine, buf, 0, j, &found_len))
        {
            out[found].text = buf;
            out[found].match_start = (int) engine->normal_offset;
            out[found].match_len = (int) found_len;
            found++;
        }
        else
            g_free (buf);
    }

    mc_search_free (engine);
    return found;
}

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions (continued) ***********************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Scan from dpy_start until preview_rows matches or scan budget is hit. */
static void
regex_check_run (void)
{
    regex_check_ctx_t *ctx = &regex_check_ctx;
    const char *pattern;
    mcview_preview_match_t *matches;
    int n, i;
    gchar *err = NULL;

    for (i = 0; i < ctx->preview_rows; i++)
        preview_line_set (ctx->preview_lines[i], "", -1, 0);

    pattern = input_get_text (ctx->input);

    if (pattern == NULL || pattern[0] == '\0')
    {
        label_set_text (ctx->status_label, _ ("First matches:"));
        widget_show (WIDGET (ctx->test_button));
        return;
    }

    matches = g_new0 (mcview_preview_match_t, ctx->preview_rows);
    n = mcview_filter_preview_scan (ctx->view, pattern, &ctx->opts, matches, ctx->preview_rows,
                                    &err);

    if (n < 0)
    {
        label_set_text (ctx->status_label, err != NULL ? err : _ ("Error"));
        g_free (err);
        g_free (matches);
        return;
    }

    for (i = 0; i < n; i++)
    {
        preview_line_set (ctx->preview_lines[i], matches[i].text, matches[i].match_start,
                          matches[i].match_len);
        g_free (matches[i].text);
    }
    g_free (matches);

    label_set_text (ctx->status_label, n > 0 ? _ ("First matches:") : _ ("No matches found."));
    widget_show (WIDGET (ctx->test_button));
}

/* --------------------------------------------------------------------------------------------- */

static int
regex_check_test_cb (WButton *button, int action)
{
    (void) button;
    (void) action;
    regex_check_run ();
    return 0; /* keep dialog open */
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
regex_check_dlg_cb (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_KEY && parm == KEY_F (1))
    {
        ev_help_t event_data = { NULL, "[Regex Quick Reference]" };
        mc_event_raise (MCEVENT_GROUP_CORE, "help", &event_data);
        return MSG_HANDLED;
    }
    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

/* Open the Regex Check helper dialog pre-filled with @initial_pattern.
 * Options are taken from @opts (inherited from Filter dialog, not editable here).
 * Returns TRUE and stores result in *out_pattern (caller must g_free) if user pressed OK.
 * Returns FALSE on Cancel. */
static gboolean
mcview_regex_check_dialog (WView *view, const char *initial_pattern,
                           const mcview_filter_options_t *opts, char **out_pattern)
{
    WDialog *dlg;
    WGroup *g;
    int preview_rows, dlg_lines;
    int y, btn_x_test, btn_x_ok, btn_x_cancel;
    int result;
    int i;
    regex_check_ctx_t *ctx = &regex_check_ctx;
    int content_width = REGEX_CHECK_COLS - 2 * REGEX_CHECK_UX;

    preview_rows = CLAMP (LINES - 12, 4, 12);
    dlg_lines = preview_rows + 10;

    dlg = dlg_create (TRUE, 0, 0, dlg_lines, REGEX_CHECK_COLS, WPOS_CENTER, FALSE, dialog_colors,
                      regex_check_dlg_cb, NULL, NULL, _ ("Filter Check"));
    g = GROUP (dlg);

    y = 2;

    {
        const char *mode_name;
        char *mode_label;

        switch (opts->type)
        {
        case MC_SEARCH_T_NORMAL:
            mode_name = _ ("Normal");
            break;
        case MC_SEARCH_T_REGEX:
            mode_name = _ ("Regular expression");
            break;
        case MC_SEARCH_T_GLOB:
            mode_name = _ ("Wildcard search");
            break;
        default:
            mode_name = _ ("Expression");
            break;
        }
        mode_label = g_strdup_printf ("%s:", mode_name);
        group_add_widget (g, label_new (y++, REGEX_CHECK_UX, mode_label));
        g_free (mode_label);
    }

    ctx->input = input_new (y++, REGEX_CHECK_UX, input_colors, content_width,
                            initial_pattern != NULL ? initial_pattern : "", "mc.view.filter",
                            INPUT_COMPLETE_NONE);
    group_add_widget (g, ctx->input);

    group_add_widget (g, hline_new (y++, 1, -1));

    ctx->status_label = label_new (y++, REGEX_CHECK_UX, _ ("First matches:"));
    WIDGET (ctx->status_label)->rect.cols = content_width;
    ctx->status_label->auto_adjust_cols = FALSE;
    group_add_widget (g, ctx->status_label);

    ctx->preview_lines = g_new0 (WPreviewLine *, preview_rows);
    ctx->preview_rows = preview_rows;
    for (i = 0; i < preview_rows; i++)
    {
        ctx->preview_lines[i] = preview_line_new (y++, REGEX_CHECK_UX, content_width);
        group_add_widget (g, ctx->preview_lines[i]);
    }

    group_add_widget (g, hline_new (y++, 1, -1));

    btn_x_test = (REGEX_CHECK_COLS - 30) / 2;
    btn_x_ok = btn_x_test + 10 + 2;
    btn_x_cancel = btn_x_ok + 6 + 2;

    ctx->test_button =
        button_new (y, btn_x_test, B_USER, DEFPUSH_BUTTON, _ ("Test"), regex_check_test_cb);
    group_add_widget (g, ctx->test_button);
    group_add_widget (g, button_new (y, btn_x_ok, B_ENTER, NORMAL_BUTTON, _ ("OK"), NULL));
    group_add_widget (g, button_new (y, btn_x_cancel, B_CANCEL, NORMAL_BUTTON, _ ("Cancel"), NULL));

    ctx->view = view;
    ctx->opts = *opts;

    widget_select (WIDGET (ctx->input));

    result = dlg_run (dlg);

    if (result == B_ENTER && out_pattern != NULL)
        *out_pattern = g_strdup (input_get_text (ctx->input));

    g_free (ctx->preview_lines);
    ctx->preview_lines = NULL;
    ctx->preview_rows = 0;

    widget_destroy (WIDGET (dlg));

    return (result == B_ENTER);
}

/* --------------------------------------------------------------------------------------------- */

static int
filter_check_btn_cb (WButton *button, int action)
{
    filter_dlg_ctx_t *ctx = &filter_dlg_ctx;
    mcview_filter_options_t opts;
    const char *pat;
    char *new_pat = NULL;

    (void) button;
    (void) action;

    opts.type = (mc_search_type_t) ctx->type_radio->sel;
    opts.case_sens = ctx->case_check->state;
    opts.whole_words = ctx->words_check->state;
    opts.all_codepages = ctx->charset_check->state;

    if (opts.type == MC_SEARCH_T_HEX)
    {
        message (D_NORMAL, _ ("Filter Check"), _ ("Preview is not available in hexadecimal mode."));
        return 0;
    }

    pat = input_get_text (ctx->input);
    if (mcview_regex_check_dialog (ctx->view, pat != NULL ? pat : "", &opts, &new_pat))
    {
        input_assign_text (ctx->input, new_pat != NULL ? new_pat : "");
        g_free (new_pat);
    }

    return 0; /* keep filter dialog open */
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/**
 * Show "Filter:" dialog (same layout as F7 Search, without Backwards).
 * On OK: activates filter with chosen options.
 * On empty input: deactivates any current filter.
 * On Cancel: leaves filter and options unchanged.
 *
 * @return TRUE if filter state was changed (activated or deactivated).
 */
gboolean
mcview_filter_dialog (WView *view)
{
    char *exp = NULL;
    int result;
    size_t num_of_types = 0;
    gchar **list_of_types;
    mcview_filter_options_t opts;
    WDialog *dlg;
    WGroup *grp;
    WInput *inp;
    WRadio *radio;
    WCheck *case_chk, *words_chk, *charset_chk;
    filter_dlg_ctx_t *ctx = &filter_dlg_ctx;
    const int dlg_cols = 58;
    const int ux = 3;
    const int x2 = 29;
    const int inp_w = 42;
    int dlg_lines, hline_row, btn_row, blen, btn_x;

    /* Work on a local copy -- committed to global only on successful activation. */
    opts = mcview_filter_options;
    list_of_types = mc_search_get_types_strings_array (&num_of_types);

    hline_row = 5 + (int) num_of_types;
    btn_row = 6 + (int) num_of_types;
    dlg_lines = btn_row + 3;

    dlg = dlg_create (TRUE, 0, 0, dlg_lines, dlg_cols, WPOS_CENTER, FALSE, dialog_colors, NULL,
                      NULL, "[Input Line Keys]", _ ("Filter"));
    grp = GROUP (dlg);

    group_add_widget (grp, label_new (2, ux, _ ("Filter pattern (empty = clear):")));
    inp = input_new (3, ux, input_colors, inp_w,
                     view->filter_pattern != NULL ? view->filter_pattern : "", "mc.view.filter",
                     INPUT_COMPLETE_NONE);
    group_add_widget (grp, inp);
    group_add_widget (
        grp,
        button_new (3, ux + inp_w + 1, B_CHECK, NORMAL_BUTTON, _ ("C&heck"), filter_check_btn_cb));
    group_add_widget (grp, hline_new (4, 1, -1));
    radio = radio_new (5, ux, (int) num_of_types, (const char **) list_of_types);
    radio->pos = radio->sel = (int) opts.type;
    group_add_widget (grp, radio);
    g_strfreev (list_of_types);

    case_chk = check_new (5, x2, opts.case_sens, _ ("Cas&e sensitive"));
    group_add_widget (grp, case_chk);
    words_chk = check_new (6, x2, opts.whole_words, _ ("&Whole words"));
    group_add_widget (grp, words_chk);
    charset_chk = check_new (7, x2, opts.all_codepages, _ ("&All charsets"));
    group_add_widget (grp, charset_chk);
    group_add_widget (grp, hline_new (hline_row, 1, -1));
    blen = 8 + 1 + 10;
    btn_x = (dlg_cols - blen) / 2;
    group_add_widget (grp, button_new (btn_row, btn_x, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
    group_add_widget (
        grp, button_new (btn_row, btn_x + 9, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    ctx->view = view;
    ctx->input = inp;
    ctx->type_radio = radio;
    ctx->case_check = case_chk;
    ctx->words_check = words_chk;
    ctx->charset_check = charset_chk;

    widget_select (WIDGET (inp));
    result = dlg_run (dlg);

    if (result == B_ENTER)
        exp = g_strdup (input_get_text (inp));

    opts.type = (mc_search_type_t) radio->sel;
    opts.case_sens = case_chk->state;
    opts.whole_words = words_chk->state;
    opts.all_codepages = charset_chk->state;

    widget_destroy (WIDGET (dlg));

    if (result == B_CANCEL)
    {
        g_free (exp);
        return FALSE;
    }

    if (exp == NULL || exp[0] == '\0')
    {
        g_free (exp);
        /* Commit options even on clear so next open reflects what user set. */
        mcview_filter_options = opts;
        if (view->filter_active)
        {
            mcview_filter_deactivate (view);
            return TRUE;
        }
        return FALSE;
    }

    {
        GString *tmp;

        tmp = str_convert_to_input (exp);
        g_free (exp);
        exp = (tmp != NULL) ? g_string_free (tmp, FALSE) : g_strdup ("");
    }

    {
        gchar *err = NULL;

        if (!mcview_filter_activate (view, exp, &opts, &err))
        {
            message (D_ERROR, MSG_ERROR, _ ("Filter error: %s"),
                     err != NULL ? err : _ ("unknown error"));
            g_free (err);
            g_free (exp);
            return FALSE;
        }

        mcview_filter_options = opts;
    }

    g_free (exp);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Activate filter mode with the given pattern and options.
 * O(1): does not scan the datasource -- mcview_filter_update() does that
 * incrementally over successive redraw ticks.
 *
 * Compile-first, commit-on-success: the old filter state is not touched until
 * the new engine passes mc_search_prepare().  This means an invalid regex or
 * hex pattern leaves the active filter running.
 *
 * NOTE: filter matching is line-by-line (within a single line only).
 * Cross-line patterns in regex/wildcard/hex will never produce matches.
 *
 * @param opts  Search options to apply (caller's local copy, not the global).
 * @param err_msg  If non-NULL, receives a newly-allocated error string on
 *                 failure (caller must g_free()).  Set to NULL on success.
 * @return TRUE on success, FALSE if the engine could not be prepared.
 */
gboolean
mcview_filter_activate (WView *view, const char *pattern, const mcview_filter_options_t *opts,
                        gchar **err_msg)
{
    mc_search_t *engine;

    if (err_msg != NULL)
        *err_msg = NULL;

    engine = mc_search_new (pattern, NULL);
    if (engine == NULL)
        return FALSE;

    engine->search_type = opts->type;
    engine->is_case_sensitive = opts->case_sens;
    engine->whole_words = opts->whole_words;
    engine->is_all_charsets = opts->all_codepages;

    /* mc_search_new() does not compile the pattern; mc_search_prepare() does. */
    if (!mc_search_prepare (engine))
    {
        if (err_msg != NULL)
            *err_msg = g_strdup (engine->error_str);
        mc_search_free (engine);
        return FALSE;
    }

    /* Pattern is valid -- now it is safe to tear down old state. */
    if (view->filter_active)
        mcview_filter_deactivate (view);

    view->filter_engine = engine;

    g_free (view->filter_pattern);
    view->filter_pattern = g_strdup (pattern);

    if (view->filter_offsets == NULL)
        view->filter_offsets = g_array_new (FALSE, FALSE, sizeof (off_t));
    else
        g_array_set_size (view->filter_offsets, 0);

    view->filter_scanned_up_to = 0;
    view->filter_partial_scan_offset = 0;
    view->filter_skipping_long_line = FALSE;
    view->filter_active = TRUE;

    /* Disable wrap mode while filter is active (simplifies rendering). */
    view->filter_prev_wrap = view->mode_flags.wrap;
    if (view->mode_flags.wrap)
    {
        view->mode_flags.wrap = FALSE;
        view->dpy_wrap_dirty = TRUE;
    }

    view->dirty++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Deactivate filter mode, restore previous wrap state, keep dpy_start
 * on the nearest real line to the current filtered position.
 */
void
mcview_filter_deactivate (WView *view)
{
    if (!view->filter_active)
        return;

    view->filter_active = FALSE;
    view->filter_follow = FALSE;

    if (view->filter_engine != NULL)
    {
        mc_search_free (view->filter_engine);
        view->filter_engine = NULL;
    }

    g_free (view->filter_pattern);
    view->filter_pattern = NULL;

    if (view->filter_offsets != NULL)
    {
        g_array_free (view->filter_offsets, TRUE);
        view->filter_offsets = NULL;
    }

    view->filter_scanned_up_to = 0;
    view->filter_partial_scan_offset = 0;
    view->filter_skipping_long_line = FALSE;

    /* Restore wrap mode. */
    if (view->filter_prev_wrap && !view->mode_flags.wrap)
    {
        view->mode_flags.wrap = TRUE;
        view->dpy_wrap_dirty = TRUE;
    }

    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Scan a budget-limited chunk of the datasource and append matching line
 * offsets to filter_offsets.  Called from the redraw tick so the UI stays
 * responsive even during log bursts.
 *
 * Also handles follow mode: if the last known match was visible before the
 * update (last_off < dpy_end), scroll to the new last match after appending.
 */
void
mcview_filter_update (WView *view)
{
    off_t filesize;
    off_t scan_end;
    off_t bol, pos;
    int lines_done = 0;
    off_t bytes_done = 0;
    gboolean budget_hit = FALSE;
    off_t last_before;

    if (!view->filter_active || view->filter_engine == NULL)
        return;

    filesize = mcview_get_filesize (view);
    if (filesize <= view->filter_partial_scan_offset)
        return;

    /* Remember last match offset before update for follow-mode check. */
    last_before = (view->filter_offsets != NULL && view->filter_offsets->len > 0)
        ? g_array_index (view->filter_offsets, off_t, view->filter_offsets->len - 1)
        : -1;

    scan_end = filesize;
    /* bol tracks the BOL of the line being scanned; filter_scanned_up_to
       only advances when a complete line is processed.
       pos is the byte-level scan cursor and may be mid-line. */
    bol = view->filter_scanned_up_to;
    pos = view->filter_partial_scan_offset;

    while (pos < scan_end)
    {
        int c;

        /* Per-byte budget: each byte costs one unit so even a single huge
           line cannot exceed MCVIEW_FILTER_BUDGET_BYTES bytes per tick. */
        if (bytes_done >= MCVIEW_FILTER_BUDGET_BYTES || lines_done >= MCVIEW_FILTER_BUDGET_LINES)
        {
            budget_hit = TRUE;
            break;
        }

        if (!mcview_get_byte (view, pos, &c))
            break; /* No data yet (growing buffer). */

        bytes_done++;
        pos++;

        if (c == '\n')
        {
            if (!view->filter_skipping_long_line)
            {
                /* Build NUL-terminated line buffer [bol, pos) and match. */
                off_t len = pos - bol;
                gchar *buf;
                off_t i;
                int bc;

                buf = g_new (gchar, len + 1);
                for (i = 0; i < len; i++)
                {
                    if (!mcview_get_byte (view, bol + i, &bc))
                        break;
                    buf[i] = (gchar) bc;
                }
                buf[i] = '\0';

                if (mc_search_run (view->filter_engine, buf, 0, i, NULL))
                    g_array_append_val (view->filter_offsets, bol);
                g_free (buf);
            }

            lines_done++;
            bol = pos;
            view->filter_skipping_long_line = FALSE;
        }
        else if (!view->filter_skipping_long_line && (pos - bol) > MCVIEW_FILTER_BUDGET_BYTES)
        {
            /* Line exceeds hard cap: continue scanning to find \n but
               skip matching -- avoids a huge allocation at line end. */
            view->filter_skipping_long_line = TRUE;
        }
    }

    /* Last line of a static file with no trailing newline: match it now
       that we know no more bytes are coming. */
    if (!budget_hit && pos > bol && !mcview_may_still_grow (view)
        && !view->filter_skipping_long_line)
    {
        off_t len = pos - bol;
        gchar *buf;
        off_t i;
        int c;

        buf = g_new (gchar, len + 1);
        for (i = 0; i < len; i++)
        {
            if (!mcview_get_byte (view, bol + i, &c))
                break;
            buf[i] = (gchar) c;
        }
        buf[i] = '\0';

        if (mc_search_run (view->filter_engine, buf, 0, i, NULL))
            g_array_append_val (view->filter_offsets, bol);
        g_free (buf);

        bol = pos; /* Mark as processed. */
    }

    view->filter_scanned_up_to = bol;
    view->filter_partial_scan_offset = pos;

    /* If we hit the budget and there is more data, request another tick. */
    if (budget_hit && pos < filesize)
        view->dirty++;

    /* Transition out of empty-state: position on first match when it appears. */
    if (view->filter_offsets != NULL && view->filter_offsets->len > 0)
    {
        off_t first = g_array_index (view->filter_offsets, off_t, 0);

        if (last_before == -1)
        {
            /* First match ever -- jump there. */
            view->dpy_start = first;
            view->dpy_paragraph_skip_lines = 0;
            view->dpy_wrap_dirty = TRUE;
        }
        else if (view->filter_follow)
        {
            off_t last_now =
                g_array_index (view->filter_offsets, off_t, view->filter_offsets->len - 1);

            /* Follow: scroll only if the last match was visible before the update. */
            if (last_before < view->dpy_end)
            {
                view->dpy_start = last_now;
                view->dpy_paragraph_skip_lines = 0;
                view->dpy_wrap_dirty = TRUE;
            }
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Binary search: return the index in filter_offsets of the entry whose
 * value is closest to (and <= ) @offset.
 * Returns 0 when filter_offsets is empty or offset is before the first entry.
 */
guint
mcview_filter_idx (WView *view, off_t offset)
{
    guint lo, hi, mid;

    if (view->filter_offsets == NULL || view->filter_offsets->len == 0)
        return 0;

    lo = 0;
    hi = view->filter_offsets->len - 1;

    if (offset <= g_array_index (view->filter_offsets, off_t, 0))
        return 0;
    if (offset >= g_array_index (view->filter_offsets, off_t, hi))
        return hi;

    while (lo + 1 < hi)
    {
        mid = lo + (hi - lo) / 2;
        if (g_array_index (view->filter_offsets, off_t, mid) <= offset)
            lo = mid;
        else
            hi = mid;
    }

    return lo;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Return the file offset stored at filter_offsets[idx], or (off_t)-1 if
 * the index is out of range.
 */
off_t
mcview_filter_offset (WView *view, guint idx)
{
    if (view->filter_offsets == NULL || idx >= view->filter_offsets->len)
        return (off_t) -1;
    return g_array_index (view->filter_offsets, off_t, idx);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_filter_follow_toggle (WView *view)
{
    if (!view->filter_active)
        return;

    view->filter_follow = !view->filter_follow;
    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_filter_nav_next (WView *view)
{
    guint idx;
    off_t target;

    if (!view->filter_active || view->filter_offsets == NULL || view->filter_offsets->len == 0)
        return;

    idx = mcview_filter_idx (view, view->dpy_start);

    if (g_array_index (view->filter_offsets, off_t, idx) == view->dpy_start)
        idx++;

    target = mcview_filter_offset (view, idx);
    if (target != (off_t) -1)
    {
        view->dpy_start = target;
        view->dpy_paragraph_skip_lines = 0;
        view->dpy_wrap_dirty = TRUE;
        view->dirty++;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_filter_nav_prev (WView *view)
{
    guint idx;

    if (!view->filter_active || view->filter_offsets == NULL || view->filter_offsets->len == 0)
        return;

    idx = mcview_filter_idx (view, view->dpy_start);

    if (idx > 0)
        idx--;

    view->dpy_start = g_array_index (view->filter_offsets, off_t, idx);
    view->dpy_paragraph_skip_lines = 0;
    view->dpy_wrap_dirty = TRUE;
    view->dirty++;
}

/* --------------------------------------------------------------------------------------------- */
