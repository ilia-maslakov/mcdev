/*
   Ctags UI dialogs.

   Copyright (C) 2025-2026
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

#include <stdio.h>
#include <string.h>

#include "lib/global.h"
#include "lib/tty/tty.h"
#include "lib/tty/color.h"
#include "lib/strutil.h"
#include "lib/widget.h"
#include "lib/widget/table.h"
#include "lib/keybind.h"

#include "src/editor-plugins/ctags/ctags-history.h"

#include "ctags-parser.h"
#include "ctags-repository.h"
#include "ctags-fuzzy.h"
#include "ctags-ui.h"

/*** file scope macro definitions ****************************************************************/

#define CTAGS_LIST_MAX_HEIGHT 13
#define CTAGS_PREVIEW_BEFORE  3 /* context lines above the symbol */
#define CTAGS_PREVIEW_AFTER   5 /* context lines below the symbol */
#define CTAGS_PREVIEW_HEIGHT  (CTAGS_PREVIEW_BEFORE + 1 + CTAGS_PREVIEW_AFTER) /* 7 */

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/* Static state shared with the dialog callback; safe because the dialog is modal. */
static WInput *ctags_sel_input = NULL;
static WListbox *ctags_sel_list = NULL;
static WListbox *ctags_sel_preview = NULL;
static GPtrArray *ctags_sel_entries = NULL;           /* borrowed */
static char *ctags_sel_last_filter = NULL;            /* last text that triggered refilter */
static const char *ctags_sel_root_dir = NULL;         /* borrowed, set by ctags_ui_set_root_dir */
static ctags_entry_t *ctags_sel_preview_entry = NULL; /* entry currently shown in preview */

static WTable *ctags_refs_table = NULL;
static GPtrArray *ctags_refs_entries = NULL; /* borrowed */

static WTable *ctags_members_table = NULL;
static GPtrArray *ctags_members_rows = NULL; /* GPtrArray<ctags_member_row_t*>, owned */

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    ctags_entry_t *entry;
    int score;
} ctags_sel_match_t;

/* --------------------------------------------------------------------------------------------- */

static int
ctags_sel_match_cmp (const void *a, const void *b)
{
    return ((const ctags_sel_match_t *) b)->score - ((const ctags_sel_match_t *) a)->score;
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_sel_update_preview (void)
{
    void *data = NULL;
    ctags_entry_t *e = NULL;
    char *file_path = NULL;
    FILE *f;
    char buf[1024];

    if (ctags_sel_list == NULL || ctags_sel_preview == NULL)
        return;

    listbox_get_current (ctags_sel_list, NULL, &data);
    e = (ctags_entry_t *) data;

    if (e == ctags_sel_preview_entry)
        return;

    ctags_sel_preview_entry = e;
    listbox_remove_list (ctags_sel_preview);

    if (e == NULL || e->file == NULL)
    {
        widget_draw (WIDGET (ctags_sel_preview));
        return;
    }

    if (g_path_is_absolute (e->file))
        file_path = g_strdup (e->file);
    else if (ctags_sel_root_dir != NULL)
        file_path = g_build_filename (ctags_sel_root_dir, e->file, NULL);
    else
        file_path = g_strdup (e->file);

    f = fopen (file_path, "r");

    if (f == NULL)
    {
        g_free (file_path);
        widget_draw (WIDGET (ctags_sel_preview));
        return;
    }

    {
        long sym = ctags_entry_resolve_line (e, file_path);
        long before = CTAGS_PREVIEW_BEFORE;
        long after = CTAGS_PREVIEW_AFTER;
        long start = sym > before ? sym - before : 1;
        long cur_line = 0;

        g_free (file_path);

        while (fgets (buf, (int) sizeof (buf), f) != NULL)
        {
            cur_line++;
            if (cur_line < start)
                continue;
            if (cur_line > sym + after)
                break;

            buf[strcspn (buf, "\n")] = '\0';
            listbox_add_item (ctags_sel_preview, LISTBOX_APPEND_AT_END, 0, buf, NULL, FALSE);
        }

        listbox_set_current (ctags_sel_preview, (int) (sym - start));
    }

    fclose (f);
    widget_draw (WIDGET (ctags_sel_preview));
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_sel_refilter (void)
{
    const char *text;
    guint i;
    GArray *matches;

    if (ctags_sel_input == NULL || ctags_sel_list == NULL || ctags_sel_entries == NULL)
        return;

    text = input_get_ctext (ctags_sel_input);

    /* Skip refilter if text hasn't changed to preserve list cursor position */
    if (ctags_sel_last_filter != NULL && strcmp (ctags_sel_last_filter, text) == 0)
        return;

    g_free (ctags_sel_last_filter);
    ctags_sel_last_filter = g_strdup (text);

    listbox_remove_list (ctags_sel_list);

    matches = g_array_new (FALSE, FALSE, sizeof (ctags_sel_match_t));

    for (i = 0; i < ctags_sel_entries->len; i++)
    {
        ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (ctags_sel_entries, i);
        ctags_sel_match_t m;

        if (e->name == NULL)
            continue;

        if (*text == '\0')
            m.score = 0;
        else
        {
            m.score = ctags_fuzzy_score (e->name, text);
            if (m.score == 0)
                continue;
        }

        m.entry = e;
        g_array_append_val (matches, m);
    }

    if (*text != '\0' && matches->len > 1)
        g_array_sort (matches, ctags_sel_match_cmp);

    for (i = 0; i < matches->len; i++)
    {
        ctags_entry_t *e = g_array_index (matches, ctags_sel_match_t, i).entry;
        char *label;

        if (e->file != NULL)
        {
            const char *base = strrchr (e->file, G_DIR_SEPARATOR);
            base = (base != NULL) ? base + 1 : e->file;

            if (e->line > 0)
                label = g_strdup_printf ("%-32s  %s:%ld", e->name, base, e->line);
            else
                label = g_strdup_printf ("%-32s  %s", e->name, base);
        }
        else
            label = g_strdup (e->name);

        listbox_add_item_take (ctags_sel_list, LISTBOX_APPEND_AT_END, 0, label, e, FALSE);
    }

    g_array_free (matches, TRUE);
    listbox_select_first (ctags_sel_list);
    widget_draw (WIDGET (ctags_sel_list));
}

/* --------------------------------------------------------------------------------------------- */

static lcback_ret_t
ctags_sel_list_activate (WListbox *l)
{
    (void) l;
    return LISTBOX_DONE;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
ctags_sel_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    (void) sender;
    (void) data;

    switch (msg)
    {
    case MSG_INIT:
    {
        cb_ret_t ret = dlg_default_callback (w, sender, msg, parm, data);
        if (ctags_sel_input != NULL)
            widget_select (WIDGET (ctags_sel_input));
        return ret;
    }

    case MSG_KEY:
        /* Route list-navigation keys to the listbox while keeping focus on the input */
        if (ctags_sel_list != NULL
            && (parm == KEY_UP || parm == KEY_DOWN || parm == KEY_PPAGE || parm == KEY_NPAGE
                || parm == KEY_HOME || parm == KEY_END))
        {
            send_message (WIDGET (ctags_sel_list), w, MSG_KEY, parm, data);
            return MSG_HANDLED;
        }
        return dlg_default_callback (w, sender, msg, parm, data);

    case MSG_NOTIFY:
        if (ctags_sel_list != NULL && sender == WIDGET (ctags_sel_list))
        {
            ctags_sel_update_preview ();
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    case MSG_POST_KEY:
        ctags_sel_refilter ();
        return MSG_HANDLED;

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

ctags_entry_t *
ctags_ui_select (const char *title, GPtrArray *entries, const char *initial_filter)
{
    WDialog *dlg;
    WInput *inp;
    WListbox *lst;
    ctags_entry_t *result = NULL;
    int list_h, dlg_h, dlg_w, dlg_y, dlg_x;
    int x_ok, x_cancel;

    if (entries == NULL || entries->len == 0)
    {
        message (D_NORMAL, title != NULL ? title : _ ("Ctags"), _ ("No matching symbols found."));
        return NULL;
    }

    /* border(1)+input(1)+gap(1)+list(N)+hline(1)+preview(P)+hline(1)+buttons(1)+border(1) */
    {
        int max_list_h = MAX (3, LINES - 4 - CTAGS_PREVIEW_HEIGHT - 7);

        list_h = (int) MIN ((guint) CTAGS_LIST_MAX_HEIGHT, (guint) max_list_h);
        list_h = MIN ((int) entries->len, list_h);
        list_h = MAX (list_h, 3);
    }
    dlg_h = list_h + CTAGS_PREVIEW_HEIGHT + 7;
    dlg_w = COLS - 4;
    dlg_y = (LINES - dlg_h) / 2;
    dlg_x = 2;

    dlg = dlg_create (TRUE, dlg_y, dlg_x, dlg_h, dlg_w, WPOS_KEEP_DEFAULT, TRUE, dialog_colors,
                      ctags_sel_dlg_callback, NULL, "[Ctags]",
                      title != NULL ? title : _ ("Select symbol"));

    /* Filter input on row 1.  Clear the "first keystroke" flag so the pre-filled
     * symbol can be appended to (rather than wiped) by the user's next char. */
    inp = input_new (1, 1, input_colors, dlg_w - 2, initial_filter != NULL ? initial_filter : "",
                     MC_HISTORY_CTAGS_SYMBOL, INPUT_COMPLETE_NONE);
    if (initial_filter != NULL && *initial_filter != '\0')
        inp->first = FALSE;
    group_add_widget (GROUP (dlg), inp);

    /* Listbox on row 3; row 2 is an empty gap after the input */
    lst = listbox_new (3, 1, list_h, dlg_w - 2, FALSE, ctags_sel_list_activate);
    group_add_widget (GROUP (dlg), lst);

    /* Separator before preview */
    group_add_widget (GROUP (dlg), hline_new (list_h + 3, -1, -1));

    /* Preview panel */
    {
        WListbox *prev;

        prev = listbox_new (list_h + 4, 1, CTAGS_PREVIEW_HEIGHT, dlg_w - 2, FALSE, NULL);
        widget_set_options (WIDGET (prev), WOP_SELECTABLE, FALSE);
        group_add_widget (GROUP (dlg), prev);
        ctags_sel_preview = prev;
    }

    /* Separator + OK / Cancel */
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));

    x_ok = (dlg_w - 20) / 2;
    x_cancel = x_ok + 10;
    group_add_widget (GROUP (dlg),
                      button_new (dlg_h - 2, x_ok, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, x_cancel, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    /* Wire up static state (must be before dlg_run so MSG_INIT sees them) */
    ctags_sel_input = inp;
    ctags_sel_list = lst;
    ctags_sel_entries = entries;
    ctags_sel_last_filter = NULL;
    ctags_sel_preview_entry = NULL;

    /* Initial fill */
    ctags_sel_refilter ();

    if (dlg_run (dlg) == B_ENTER)
    {
        char *text = NULL;

        listbox_get_current (lst, &text, (void **) &result);
    }

    /* Clear static state before destroying widgets */
    ctags_sel_input = NULL;
    ctags_sel_list = NULL;
    ctags_sel_preview = NULL;
    ctags_sel_entries = NULL;
    ctags_sel_preview_entry = NULL;
    ctags_sel_root_dir = NULL;
    g_free (ctags_sel_last_filter);
    ctags_sel_last_filter = NULL;

    widget_destroy (WIDGET (dlg));
    return result;
}

/* --------------------------------------------------------------------------------------------- */

void
ctags_ui_set_root_dir (const char *root_dir)
{
    ctags_sel_root_dir = root_dir;
}

/* --------------------------------------------------------------------------------------------- */

char *
ctags_ui_prompt_symbol (const char *initial)
{
    return input_dialog (_ ("Search Symbol"), _ ("Symbol name:"), MC_HISTORY_CTAGS_SYMBOL,
                         initial != NULL ? initial : "", INPUT_COMPLETE_NONE);
}

/* --------------------------------------------------------------------------------------------- */

char *
ctags_ui_prompt_file (const char *initial)
{
    return input_dialog (_ ("Search File"), _ ("File name or path:"), MC_HISTORY_CTAGS_FILE,
                         initial != NULL ? initial : "", INPUT_COMPLETE_NONE);
}

/* --------------------------------------------------------------------------------------------- */

char *
ctags_ui_prompt_scope (const char *initial)
{
    return input_dialog (_ ("Class/Struct Members"), _ ("Scope name (class, struct, namespace):"),
                         MC_HISTORY_CTAGS_SYMBOL, initial != NULL ? initial : "",
                         INPUT_COMPLETE_NONE);
}

/* --------------------------------------------------------------------------------------------- */

char *
ctags_ui_prompt_tags_path (void)
{
    return input_dialog (_ ("Load Tags File"), _ ("Tags file path:"), MC_HISTORY_CTAGS_TAGS_PATH,
                         "", INPUT_COMPLETE_FILENAMES);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
ctags_ui_manage_repos (GSList **repos)
{
    WDialog *dlg;
    WListbox *lst;
    gboolean changed = FALSE;
    int n_repos, list_h, dlg_h, dlg_w;

    if (repos == NULL)
        return FALSE;

    n_repos = (int) g_slist_length (*repos);
    list_h = CLAMP (n_repos, 3, MIN (LINES - 9, 16));
    dlg_h = list_h + 4; /* border + list + hline + button + border */
    dlg_w = MIN (COLS - 4, 72);

    dlg = dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w,
                      WPOS_KEEP_DEFAULT, TRUE, dialog_colors, dlg_default_callback, NULL, "[Ctags]",
                      _ ("Manage Tag Repositories"));

    lst = listbox_new (1, 1, list_h, dlg_w - 2, TRUE, NULL);

    {
        GSList *l;

        for (l = *repos; l != NULL; l = g_slist_next (l))
        {
            ctags_repo_t *repo = (ctags_repo_t *) l->data;

            listbox_add_item (lst, LISTBOX_APPEND_AT_END, 0,
                              repo->tags_path != NULL ? repo->tags_path : _ ("(unknown)"), repo,
                              FALSE);
        }
    }

    group_add_widget (GROUP (dlg), lst);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, (dlg_w - 10) / 2, B_CANCEL, NORMAL_BUTTON, _ ("&Close"), NULL));

    dlg_run (dlg);

    {
        GSList *l = *repos;

        while (l != NULL)
        {
            ctags_repo_t *repo = (ctags_repo_t *) l->data;
            GSList *next = g_slist_next (l);

            if (listbox_search_data (lst, repo) < 0)
            {
                *repos = g_slist_remove (*repos, repo);
                ctags_repo_free (repo);
                changed = TRUE;
            }
            l = next;
        }
    }

    widget_destroy (WIDGET (dlg));
    return changed;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
ctags_kind_label (char kind)
{
    switch (kind)
    {
    case 'f':
        return "func";
    case 'p':
        return "proto";
    case 'm':
        return "field";
    case 'v':
        return "var";
    case 'd':
        return "macro";
    case 't':
        return "typedef";
    case 's':
        return "struct";
    case 'u':
        return "union";
    case 'c':
        return "class";
    case 'g':
        return "enum";
    case 'e':
        return "enum val";
    default:
        return "";
    }
}

/* --------------------------------------------------------------------------------------------- */

static const char *
ctags_typeref_value (const char *typeref)
{
    const char *colon;

    if (typeref == NULL)
        return NULL;
    colon = strchr (typeref, ':');
    return colon != NULL ? colon + 1 : typeref;
}

/* --------------------------------------------------------------------------------------------- */

void
ctags_member_row_free (ctags_member_row_t *row)
{
    if (row == NULL)
        return;
    g_free (row->display_name);
    g_free (row->note);
    g_free (row);
}

/* --------------------------------------------------------------------------------------------- */

static int
ctags_members_get_nrows (const void *data)
{
    (void) data;
    return ctags_members_rows != NULL ? (int) ctags_members_rows->len : 0;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
ctags_members_get_text (const void *data, int row, int col)
{
    const ctags_member_row_t *r;
    const ctags_entry_t *e;

    (void) data;

    if (ctags_members_rows == NULL || row < 0 || row >= (int) ctags_members_rows->len)
        return "";

    r = (const ctags_member_row_t *) g_ptr_array_index (ctags_members_rows, (guint) row);
    e = r->original;

    switch (col)
    {
    case 0:
        return r->is_group ? "group" : (e != NULL ? ctags_kind_label (e->kind) : "");
    case 1:
        return r->display_name != NULL ? r->display_name : "";
    case 2:
        if (r->is_group)
            return "struct";
        if (e == NULL)
            return "";
        {
            const char *type = ctags_typeref_value (e->typeref);
            if (type != NULL)
                return type;
            return e->signature != NULL ? e->signature : "";
        }
    case 3:
        return r->note != NULL ? r->note : "";
    default:
        return "";
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
ctags_refs_get_nrows (const void *data)
{
    (void) data;
    return ctags_refs_entries != NULL ? (int) ctags_refs_entries->len : 0;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
ctags_refs_get_text (const void *data, int row, int col)
{
    static char linebuf[16];
    ctags_entry_t *e;

    (void) data;

    if (ctags_refs_entries == NULL || row < 0 || row >= (int) ctags_refs_entries->len)
        return "";

    e = (ctags_entry_t *) g_ptr_array_index (ctags_refs_entries, (guint) row);

    switch (col)
    {
    case 0:
        return e->name != NULL ? e->name : "";
    case 1:
        if (e->file != NULL)
        {
            const char *base = strrchr (e->file, G_DIR_SEPARATOR);
            return base != NULL ? base + 1 : e->file;
        }
        return "";
    case 2:
        if (e->line > 0)
        {
            g_snprintf (linebuf, sizeof (linebuf), "%ld", e->line);
            return linebuf;
        }
        return "";
    default:
        return "";
    }
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
ctags_refs_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    (void) data;

    switch (msg)
    {
    case MSG_NOTIFY:
        if (ctags_refs_table != NULL && sender == WIDGET (ctags_refs_table) && parm == CK_Enter)
        {
            DIALOG (w)->ret_value = B_ENTER;
            dlg_close (DIALOG (w));
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;
    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

ctags_entry_t *
ctags_ui_select_refs (const char *title, GPtrArray *entries)
{
    WDialog *dlg;
    WTable *tbl;
    ctags_entry_t *result = NULL;
    int n_entries, table_h, dlg_h, dlg_w, table_w;
    int code_w, fname_w, line_w;
    int x_ok, x_cancel;
    table_column_def_t col_defs[3];
    table_datasource_t ds;

    if (entries == NULL || entries->len == 0)
    {
        message (D_NORMAL, title != NULL ? title : _ ("Ctags"), _ ("No references found."));
        return NULL;
    }

    n_entries = (int) entries->len;
    table_h = MIN (n_entries, CTAGS_LIST_MAX_HEIGHT);
    table_h = MAX (table_h, 3);

    /* border(1) + table(N) + hline(1) + buttons(1) + border(1) */
    dlg_h = table_h + 4;
    dlg_w = COLS - 4;

    /* table: x=2, width=dlg_w-2; columns: code | filename(24) | line(6)
     * layout: 1(margin) + code_w + 1(sep) + fname_w + 1(sep) + line_w = table_w - 1 (scrollbar) */
    table_w = dlg_w - 2;
    fname_w = 24;
    line_w = 6;
    code_w = table_w - fname_w - line_w - 4; /* 4 = 1 margin + 2 seps + 1 scrollbar */
    if (code_w < 20)
        code_w = 20;

    col_defs[0].width = code_w;
    col_defs[0].align = J_LEFT;
    col_defs[0].type = TABLE_COL_TEXT;
    col_defs[1].width = fname_w;
    col_defs[1].align = J_LEFT;
    col_defs[1].type = TABLE_COL_TEXT;
    col_defs[2].width = line_w;
    col_defs[2].align = J_RIGHT;
    col_defs[2].type = TABLE_COL_TEXT;

    dlg = dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w,
                      WPOS_KEEP_DEFAULT, TRUE, dialog_colors, ctags_refs_dlg_callback, NULL,
                      "[Ctags]", title != NULL ? title : _ ("Find References"));

    tbl = table_new (1, 2, table_h, table_w, 3, col_defs);
    tbl->scrollbar = TRUE;

    ds.get_nrows = ctags_refs_get_nrows;
    ds.get_text = ctags_refs_get_text;
    ds.get_checked = NULL;
    ds.set_checked = NULL;
    ds.data = NULL;
    table_set_datasource (tbl, ds);

    group_add_widget (GROUP (dlg), tbl);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));

    x_ok = (dlg_w - 20) / 2;
    x_cancel = x_ok + 10;
    group_add_widget (GROUP (dlg),
                      button_new (dlg_h - 2, x_ok, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, x_cancel, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    ctags_refs_table = tbl;
    ctags_refs_entries = entries;

    if (dlg_run (dlg) == B_ENTER)
    {
        int row = table_get_current (tbl);

        if (row >= 0 && row < (int) entries->len)
            result = (ctags_entry_t *) g_ptr_array_index (entries, (guint) row);
    }

    ctags_refs_table = NULL;
    ctags_refs_entries = NULL;

    widget_destroy (WIDGET (dlg));
    return result;
}

/* --------------------------------------------------------------------------------------------- */

ctags_entry_t *
ctags_ui_select_members (const char *scope, GPtrArray *rows)
{
    WDialog *dlg;
    WTable *tbl;
    ctags_entry_t *result = NULL;
    int n_rows, table_h, dlg_h, dlg_w, table_w;
    int kind_w, name_w, type_w, note_w;
    int x_ok, x_cancel;
    table_column_def_t col_defs[4];
    table_datasource_t ds;
    char *subtitle;
    const char *common_file = NULL;
    gboolean mixed_files = FALSE;
    guint i;

    if (rows == NULL || rows->len == 0)
        return NULL;

    /* Determine common file from real (non-group) rows. */
    for (i = 0; i < rows->len; i++)
    {
        const ctags_member_row_t *r = (const ctags_member_row_t *) g_ptr_array_index (rows, i);
        const char *base;

        if (r->is_group || r->original == NULL || r->original->file == NULL)
            continue;

        base = strrchr (r->original->file, G_DIR_SEPARATOR);
        base = base != NULL ? base + 1 : r->original->file;

        if (common_file == NULL)
            common_file = base;
        else if (strcmp (common_file, base) != 0)
        {
            mixed_files = TRUE;
            break;
        }
    }

    if (!mixed_files && common_file != NULL)
        subtitle = g_strdup_printf ("%s  from %s", scope != NULL ? scope : "", common_file);
    else
        subtitle = g_strdup (scope != NULL ? scope : "");

    n_rows = (int) rows->len;
    table_h = MIN (n_rows, CTAGS_LIST_MAX_HEIGHT);
    table_h = MAX (table_h, 3);

    /* border(1) + subtitle(1) + table(N) + hline(1) + buttons(1) + border(1) */
    dlg_h = table_h + 5;
    dlg_w = COLS - 4;

    /* columns: kind(7) | name(30) | type(22) | note(rest)
     * layout: 1(margin) + kind_w + 1(sep) + name_w + 1(sep) + type_w + 1(sep) + note_w
     *         = table_w - 1(scrollbar) */
    table_w = dlg_w - 3;
    kind_w = 7;
    name_w = 30;
    type_w = 22;
    note_w = table_w - kind_w - name_w - type_w - 5;
    if (note_w < 8)
        note_w = 8;

    col_defs[0].width = kind_w;
    col_defs[0].align = J_LEFT;
    col_defs[0].type = TABLE_COL_TEXT;
    col_defs[1].width = name_w;
    col_defs[1].align = J_LEFT;
    col_defs[1].type = TABLE_COL_TEXT;
    col_defs[2].width = type_w;
    col_defs[2].align = J_LEFT;
    col_defs[2].type = TABLE_COL_TEXT;
    col_defs[3].width = note_w;
    col_defs[3].align = J_LEFT;
    col_defs[3].type = TABLE_COL_TEXT;

    dlg =
        dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w, WPOS_KEEP_DEFAULT,
                    TRUE, dialog_colors, ctags_refs_dlg_callback, NULL, "[Ctags]", _ ("Members"));

    group_add_widget (GROUP (dlg), label_new (1, 2, subtitle));
    g_free (subtitle);

    tbl = table_new (2, 2, table_h, table_w, 4, col_defs);
    tbl->scrollbar = TRUE;

    ds.get_nrows = ctags_members_get_nrows;
    ds.get_text = ctags_members_get_text;
    ds.get_checked = NULL;
    ds.set_checked = NULL;
    ds.data = NULL;
    table_set_datasource (tbl, ds);

    group_add_widget (GROUP (dlg), tbl);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));

    x_ok = (dlg_w - 20) / 2;
    x_cancel = x_ok + 10;
    group_add_widget (GROUP (dlg),
                      button_new (dlg_h - 2, x_ok, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, x_cancel, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    ctags_members_table = tbl;
    ctags_members_rows = rows;

    if (dlg_run (dlg) == B_ENTER)
    {
        int row = table_get_current (tbl);

        if (row >= 0 && row < (int) rows->len)
        {
            const ctags_member_row_t *r =
                (const ctags_member_row_t *) g_ptr_array_index (rows, (guint) row);
            if (!r->is_group && r->original != NULL)
                result = (ctags_entry_t *) r->original;
        }
    }

    ctags_members_table = NULL;
    ctags_members_rows = NULL;

    widget_destroy (WIDGET (dlg));
    return result;
}

/* --------------------------------------------------------------------------------------------- */
