/*
   MongoDB panel plugin -- filter-builder wizard.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.

   One-way code generator: builds a flat list of conditions, joins them with
   $and/$or (AND binds tighter than OR) and produces a JSON string for the
   Filter field. The current row is edited in place by live widgets; other rows
   are drawn as text. Values use Extended JSON ($oid/$date) so
   bson_new_from_json accepts them.
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <bson/bson.h>

#include "lib/global.h"
#include "lib/tty/tty.h"
#include "lib/skin.h"
#include "lib/keybind.h"  // KEY_*
#include "lib/widget.h"

#include "mongo_wizard.h"
#include "mongo_wizard_priv.h"  // wiz_rule_t, s_rules, JSON/parser helpers

/*** file scope types ****************************************************************************/

typedef enum
{
    WIZ_TEMPLATE_STATIC = 0,
    WIZ_TEMPLATE_DATE_RANGE,
} wiz_template_kind_t;

/*** file scope variables ************************************************************************/

static const char *wiz_op_labels[WIZ_OP_COUNT] = {
    N_ ("== Equals ($eq)"),
    N_ ("!= Not equal ($ne)"),
    N_ ("> Greater ($gt)"),
    N_ (">= Greater or equal ($gte)"),
    N_ ("< Less ($lt)"),
    N_ ("<= Less or equal ($lte)"),
    N_ ("IN array ($in)"),
    N_ ("NOT IN array ($nin)"),
    N_ ("Regex - raw pattern ($regex)"),
    N_ ("LIKE - wildcard * ? ($regex)"),
    N_ ("Exists ($exists)"),
};

static const char *wiz_op_short[WIZ_OP_COUNT] = { "==", "!=",  ">",  ">=",   "<",     "<=",
                                                  "IN", "!IN", "=~", "like", "exists" };

static const char *wiz_logic_short[WIZ_LOGIC_COUNT] = { "AND", "OR", "END" };

/* Common query patterns offered by the Template picker (Extended JSON so
   bson_new_from_json accepts them). Importable ones become editable rules;
   the rest report that they are too complex for the builder. */
static const struct
{
    const char *label;
    const char *snippet;
    wiz_template_kind_t kind;
    const char *fallback_field;
} wiz_templates[] = {
    { N_ ("Find by ObjectId"), "{ \"_id\": { \"$oid\": \"000000000000000000000000\" } }",
      WIZ_TEMPLATE_STATIC, NULL },
    { N_ ("Find by date range"), NULL, WIZ_TEMPLATE_DATE_RANGE, "createdAt" },
};

/* Grid geometry (dialog-relative). */
#define WIZ_W             76
#define WIZ_H             22
#define WIZ_ROW0          3           /* first data row */
#define WIZ_MAXVIS        11          /* visible rows (scrolling window) */
#define WIZ_MAXROWS       64          /* hard cap on total conditions */
#define WIZ_X_MARK        (WIZ_W - 1) /* scrollbar column, drawn on the right frame */
#define WIZ_X_FIELD       4
#define WIZ_W_FIELD       16
#define WIZ_FIELD_TEXT    13
#define WIZ_X_OP          21
#define WIZ_W_OP          13
#define WIZ_X_VAL         35
#define WIZ_W_VAL         28
#define WIZ_X_LOGIC       64
#define WIZ_W_LOGIC       8

#define WIZ_PREVIEW_LINE  (WIZ_ROW0 + WIZ_MAXVIS)
#define WIZ_PREVIEW_ROW   (WIZ_PREVIEW_LINE + 1)
#define WIZ_COMMANDS_LINE (WIZ_H - 3)
#define WIZ_BUTTON_ROW    (WIZ_H - 2)

#define WIZ_PREVIEW_MAX   66

/* State of the one open wizard dialog (modal, not reentrant). */
GArray *s_rules = NULL; /* wiz_rule_t; invariant: len >= 1; shared, see mongo_wizard_priv.h */
static int s_cur = 0;   /* current row */
static int s_top = 0;   /* first visible row (scroll offset) */
static WDialog *s_dlg = NULL;
static WButton *e_field = NULL;
static WButton *e_op = NULL;
static WInput *e_value = NULL;
static WButton *e_logic = NULL;
static WLabel *s_preview = NULL;
static const char *const *s_fields = NULL; /* schema field names, not owned */
static mongo_wizard_values_fn s_values_fn = NULL;
static gpointer s_values_ctx = NULL;
static WListbox *s_ms_list = NULL; /* listbox of the open multi-select picker */

/*** file scope functions ************************************************************************/

static void
wiz_rule_clear (gpointer p)
{
    wiz_rule_t *r = (wiz_rule_t *) p;
    g_free (r->field);
    g_free (r->value);
    g_free (r->options);
}

/* --------------------------------------------------------------------------------------------- */
/* Templates and rule helpers                                                                    */
/* --------------------------------------------------------------------------------------------- */

static const char *
wiz_current_field_or (const char *fallback)
{
    if (s_rules != NULL && s_cur >= 0 && (guint) s_cur < s_rules->len)
    {
        const wiz_rule_t *r = &g_array_index (s_rules, wiz_rule_t, (guint) s_cur);

        if (r->field != NULL && r->field[0] != '\0')
            return r->field;
    }

    if (s_fields != NULL && s_fields[0] != NULL && s_fields[0][0] != '\0')
        return s_fields[0];

    return fallback;
}

static char *
wiz_template_snippet (gsize idx)
{
    if (wiz_templates[idx].kind == WIZ_TEMPLATE_DATE_RANGE)
    {
        char *field = wiz_json_quote (wiz_current_field_or (wiz_templates[idx].fallback_field));
        char *snippet =
            g_strdup_printf ("{ %s: { \"$gte\": { \"$date\": \"2024-01-01T00:00:00Z\" },"
                             " \"$lte\": { \"$date\": \"2024-12-31T23:59:59Z\" } } }",
                             field);

        g_free (field);
        return snippet;
    }

    return g_strdup (wiz_templates[idx].snippet);
}

static gboolean
wiz_rule_is_empty_eq (const wiz_rule_t *r)
{
    return r->op == WIZ_OP_EQ && r->options == NULL && (r->value == NULL || r->value[0] == '\0');
}

/* --------------------------------------------------------------------------------------------- */
/* UI                                                                                            */
/* --------------------------------------------------------------------------------------------- */

static void
wiz_preview_update (void)
{
    char *json = wiz_generate ();

    if (json == NULL)
        label_set_text (s_preview, "{}");
    else if (strlen (json) > WIZ_PREVIEW_MAX)
    {
        size_t cut = WIZ_PREVIEW_MAX;
        char *clip;

        while (cut > 0 && ((unsigned char) json[cut] & 0xC0) == 0x80)
            cut--;
        clip = g_strdup_printf ("%.*s...", (int) cut, json);
        label_set_text (s_preview, clip);
        g_free (clip);
    }
    else
        label_set_text (s_preview, json);
    g_free (json);
}

static const char *
wiz_field_label (const char *field)
{
    return field != NULL && field[0] != '\0' ? str_fit_to_term (field, WIZ_FIELD_TEXT, J_LEFT_FIT)
                                             : "...";
}

static void
wiz_commit_value (void)
{
    wiz_rule_t *r;
    char *t;

    if (s_cur < 0 || (guint) s_cur >= s_rules->len)
        return;
    r = &g_array_index (s_rules, wiz_rule_t, (guint) s_cur);
    t = input_get_text (e_value);
    g_free (r->value);
    r->value = t;
}

static void
wiz_load_row (int idx)
{
    const wiz_rule_t *r;
    int y;

    if (idx < 0)
        idx = 0;
    if ((guint) idx >= s_rules->len)
        idx = (int) s_rules->len - 1;
    s_cur = idx;

    if (s_cur < s_top)
        s_top = s_cur;
    else if (s_cur >= s_top + WIZ_MAXVIS)
        s_top = s_cur - WIZ_MAXVIS + 1;

    r = &g_array_index (s_rules, wiz_rule_t, (guint) idx);
    y = WIDGET (s_dlg)->rect.y + WIZ_ROW0 + (idx - s_top);

    {
        WRect rc = WIDGET (e_field)->rect;
        rc.y = y;
        widget_set_size_rect (WIDGET (e_field), &rc);
        rc = WIDGET (e_op)->rect;
        rc.y = y;
        widget_set_size_rect (WIDGET (e_op), &rc);
        rc = WIDGET (e_value)->rect;
        rc.y = y;
        widget_set_size_rect (WIDGET (e_value), &rc);
        rc = WIDGET (e_logic)->rect;
        rc.y = y;
        widget_set_size_rect (WIDGET (e_logic), &rc);
    }

    button_set_text (e_field, wiz_field_label (r->field));
    button_set_text (e_op, wiz_op_short[r->op]);
    input_assign_text (e_value, r->value != NULL ? r->value : "");
    button_set_text (e_logic, wiz_logic_short[r->logic]);
}

/* --------------------------------------------------------------------------------------------- */

static int
wiz_field_cb (WButton *button, int action)
{
    wiz_rule_t *r;
    gsize nfields = 0;
    char *chosen = NULL;

    (void) action;
    if (s_cur < 0 || (guint) s_cur >= s_rules->len)
        return 0;
    r = &g_array_index (s_rules, wiz_rule_t, (guint) s_cur);

    if (s_fields != NULL)
        while (s_fields[nfields] != NULL)
            nfields++;

    if (nfields == 0)
        chosen = input_dialog (_ ("Field name"), _ ("Field name:"), "mongo-wiz-field",
                               r->field != NULL ? r->field : "", INPUT_COMPLETE_NONE);
    else
    {
        Listbox *lb =
            listbox_window_new ((int) nfields + 3, 44, _ ("Field name"), "[MongoDB Plugin]");
        gsize n;
        int sel;
        for (n = 0; n < nfields; n++)
            listbox_add_item (lb->list, LISTBOX_APPEND_AT_END, 0, s_fields[n], NULL, FALSE);
        listbox_add_item (lb->list, LISTBOX_APPEND_AT_END, 0, _ ("(type a path, e.g. a.b)"), NULL,
                          FALSE);
        sel = listbox_run (lb);
        if (sel >= 0 && (gsize) sel < nfields)
            chosen = g_strdup (s_fields[sel]);
        else if (sel == (int) nfields)
            chosen = input_dialog (_ ("Field name"), _ ("Field name (dot-path for nested):"),
                                   "mongo-wiz-field", r->field != NULL ? r->field : "",
                                   INPUT_COMPLETE_NONE);
    }

    if (chosen != NULL)
    {
        g_free (r->field);
        r->field = chosen;
        button_set_text (button, wiz_field_label (r->field));
        wiz_preview_update ();
        widget_draw (WIDGET (s_dlg));
    }
    return 0;
}

/* Multi-select dialog callback: Ins (or space) toggles the [*] mark on the
   current entry and moves down, like marking files on the panel. */
static cb_ret_t
wiz_ms_dlg_cb (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_UNHANDLED_KEY && (parm == KEY_IC || parm == ' ') && s_ms_list != NULL)
    {
        WLEntry *e = listbox_get_nth_entry (s_ms_list, s_ms_list->current);
        if (e != NULL && e->text != NULL && e->text[0] == '[')
        {
            e->text[1] = (e->text[1] == '*') ? ' ' : '*';
            listbox_set_current (s_ms_list, s_ms_list->current + 1);
            widget_draw (WIDGET (s_ms_list));
        }
        return MSG_HANDLED;
    }
    return dlg_default_callback (w, sender, msg, parm, data);
}

/* Pop a checklist of @items. Returns a JSON array with the marked items (caller
   g_free), or the highlighted one if none were marked, or NULL if cancelled. */
static char *
wiz_multiselect (const char *title, char *const *items, gsize n, const char *preselect)
{
    WDialog *dlg;
    WListbox *list;
    int lines = MIN ((int) n, 18);
    char **pre = NULL;
    char *out = NULL;
    gsize i;
    int rc;

    if (preselect != NULL && preselect[0] != '\0')
    {
        pre = g_strsplit (preselect, ",", -1);
        for (i = 0; pre[i] != NULL; i++)
            g_strstrip (pre[i]);
    }

    dlg = dlg_create (TRUE, 0, 0, lines + 4, 54, WPOS_CENTER | WPOS_TRYUP, FALSE, listbox_colors,
                      wiz_ms_dlg_cb, NULL, "[MongoDB Plugin]", title);
    list = listbox_new (2, 2, lines, 50, FALSE, NULL);
    for (i = 0; i < n; i++)
    {
        gboolean marked = FALSE;
        char *t;

        if (pre != NULL)
        {
            gsize j;
            for (j = 0; pre[j] != NULL; j++)
                if (strcmp (pre[j], items[i]) == 0)
                {
                    marked = TRUE;
                    break;
                }
        }
        t = g_strdup_printf ("[%c] %s", marked ? '*' : ' ', items[i]);
        listbox_add_item (list, LISTBOX_APPEND_AT_END, 0, t, NULL, FALSE);
        g_free (t);
    }
    group_add_widget (GROUP (dlg), list);

    s_ms_list = list;
    rc = dlg_run (dlg);
    s_ms_list = NULL;

    if (rc != B_CANCEL)
    {
        GString *acc = g_string_new ("[");
        int len = listbox_get_length (list);
        int k;

        for (k = 0; k < len; k++)
        {
            WLEntry *e = listbox_get_nth_entry (list, k);
            if (e != NULL && e->text != NULL && e->text[0] == '[' && e->text[1] == '*')
            {
                char *sc = wiz_picker_scalar_json (e->text + 4);

                if (acc->len > 1)
                    g_string_append (acc, ", ");
                g_string_append (acc, sc);
                g_free (sc);
            }
        }
        if (acc->len == 1)
        {
            WLEntry *e = listbox_get_nth_entry (list, list->current);
            if (e != NULL && e->text != NULL && strlen (e->text) > 4)
            {
                char *sc = wiz_picker_scalar_json (e->text + 4);

                g_string_append (acc, sc);
                g_free (sc);
            }
        }
        g_string_append_c (acc, ']');
        out = acc->len > 2 ? g_string_free (acc, FALSE) : (g_string_free (acc, TRUE), NULL);
    }

    widget_destroy (WIDGET (dlg));
    if (pre != NULL)
        g_strfreev (pre);
    return out;
}

/* List distinct values of the current row's field and let the user pick one
   (or several, for IN) into the value input. */
static void
wiz_pick_value (void)
{
    wiz_rule_t *r;
    char **values;
    char *err = NULL;
    gboolean capped = FALSE;
    gsize n = 0;
    const char *title;

    if (s_values_fn == NULL || s_cur < 0 || (guint) s_cur >= s_rules->len)
        return;
    r = &g_array_index (s_rules, wiz_rule_t, (guint) s_cur);
    if (r->field == NULL || r->field[0] == '\0')
    {
        message (D_ERROR, _ ("Field values"), "%s", _ ("Choose a field first."));
        return;
    }

    values = s_values_fn (s_values_ctx, r->field, &capped, &err);
    if (values == NULL)
    {
        message (D_ERROR, _ ("Field values"), "%s", err != NULL ? err : _ ("No values found."));
        g_free (err);
        return;
    }
    while (values[n] != NULL)
        n++;
    if (n == 0)
    {
        message (D_NORMAL, _ ("Field values"), "%s", _ ("No values in current scope."));
        g_strfreev (values);
        return;
    }

    title = capped ? _ ("Field values (truncated)") : _ ("Field values");

    if (r->op == WIZ_OP_IN || r->op == WIZ_OP_NIN)
    {
        /* IN/NOT IN take a list: mark several with Ins, comma-joined. */
        char *picked = wiz_multiselect (title, values, n, r->value);
        if (picked != NULL)
        {
            wiz_commit_value ();
            g_free (r->value);
            r->value = picked;
            input_assign_text (e_value, r->value);
            wiz_preview_update ();
            widget_draw (WIDGET (s_dlg));
        }
    }
    else
    {
        Listbox *lb =
            listbox_window_new ((int) MIN (n, (gsize) 18) + 2, 50, title, "[MongoDB Plugin]");
        int sel;
        gsize i;

        for (i = 0; i < n; i++)
            listbox_add_item (lb->list, LISTBOX_APPEND_AT_END, 0, values[i], NULL, FALSE);
        sel = listbox_run (lb);
        if (sel >= 0 && (gsize) sel < n)
        {
            wiz_commit_value ();
            g_free (r->value);
            r->value = wiz_picker_value_text (values[sel]);
            input_assign_text (e_value, r->value);
            wiz_preview_update ();
            widget_draw (WIDGET (s_dlg));
        }
    }
    g_strfreev (values);
}

static int
wiz_template_cb (WButton *button, int action)
{
    Listbox *lb;
    int sel;
    gsize i;
    guint old, first_new;
    gboolean replace_current;
    int n;
    char *snippet;

    (void) button;
    (void) action;

    lb = listbox_window_new ((int) G_N_ELEMENTS (wiz_templates) + 2, 52,
                             _ ("Insert query template"), "[MongoDB Plugin]");
    for (i = 0; i < G_N_ELEMENTS (wiz_templates); i++)
        listbox_add_item (lb->list, LISTBOX_APPEND_AT_END, 0, _ (wiz_templates[i].label), NULL,
                          FALSE);
    sel = listbox_run (lb);
    if (sel < 0 || (gsize) sel >= G_N_ELEMENTS (wiz_templates))
        return 0;

    wiz_commit_value ();
    replace_current = s_cur >= 0 && (guint) s_cur < s_rules->len
        && wiz_rule_is_empty_eq (&g_array_index (s_rules, wiz_rule_t, (guint) s_cur));

    old = s_rules->len;
    snippet = wiz_template_snippet ((gsize) sel);
    n = wiz_import_filter (snippet);
    g_free (snippet);

    if (n == 0)
    {
        message (D_NORMAL, _ ("Template"), "%s",
                 _ ("This template cannot be represented as builder rules.\n"
                    "Use Edit as File in the find dialog to insert it as raw JSON."));
        return 0;
    }

    if (replace_current)
    {
        g_array_remove_index (s_rules, (guint) s_cur);
        old--;
    }

    first_new = replace_current ? old : s_rules->len - (guint) n;
    if (first_new > 0)
    {
        wiz_rule_t *prev = &g_array_index (s_rules, wiz_rule_t, first_new - 1);

        if (prev->logic == WIZ_END)
            prev->logic = WIZ_AND;
    }

    s_top = 0;
    wiz_load_row ((int) first_new);
    wiz_preview_update ();
    widget_draw (WIDGET (s_dlg));
    return 0;
}

static int
wiz_op_cb (WButton *button, int action)
{
    wiz_rule_t *r;
    Listbox *lb;
    int sel;
    int i;

    (void) action;
    if (s_cur < 0 || (guint) s_cur >= s_rules->len)
        return 0;
    r = &g_array_index (s_rules, wiz_rule_t, (guint) s_cur);

    lb = listbox_window_new (WIZ_OP_COUNT + 2, 30, _ ("Operator"), "[MongoDB Plugin]");
    for (i = 0; i < WIZ_OP_COUNT; i++)
        listbox_add_item (lb->list, LISTBOX_APPEND_AT_END, 0, _ (wiz_op_labels[i]), NULL, FALSE);
    listbox_set_current (lb->list, (int) r->op);
    sel = listbox_run (lb);
    if (sel >= 0)
    {
        r->op = (wiz_op_t) sel;
        if (r->op != WIZ_OP_REGEX)
        {
            g_free (r->options);
            r->options = NULL;
        }
        button_set_text (button, wiz_op_short[r->op]);
        wiz_preview_update ();
        widget_draw (WIDGET (s_dlg));
    }
    return 0;
}

static int
wiz_logic_cb (WButton *button, int action)
{
    wiz_rule_t *r;

    (void) action;
    if (s_cur < 0 || (guint) s_cur >= s_rules->len)
        return 0;
    r = &g_array_index (s_rules, wiz_rule_t, (guint) s_cur);
    r->logic = (wiz_logic_t) (((int) r->logic + 1) % WIZ_LOGIC_COUNT);
    button_set_text (button, wiz_logic_short[r->logic]);
    wiz_preview_update ();
    widget_draw (WIDGET (s_dlg));
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
wiz_add_row (void)
{
    wiz_rule_t r = { NULL, WIZ_OP_EQ, NULL, NULL, WIZ_END };
    wiz_rule_t *prev;

    if (s_rules->len >= WIZ_MAXROWS)
        return;
    if (s_fields != NULL && s_fields[0] != NULL)
        r.field = g_strdup (s_fields[0]);
    wiz_commit_value ();
    prev = &g_array_index (s_rules, wiz_rule_t, s_rules->len - 1);
    if (prev->logic == WIZ_END)
        prev->logic = WIZ_AND;
    g_array_append_val (s_rules, r);
    wiz_load_row ((int) s_rules->len - 1);
    wiz_preview_update ();
    widget_draw (WIDGET (s_dlg));
}

static void
wiz_delete_row (void)
{
    if (s_rules->len <= 1)
    {
        wiz_rule_t *r = &g_array_index (s_rules, wiz_rule_t, 0);
        g_free (r->field);
        r->field = NULL;
        g_free (r->value);
        r->value = NULL;
        g_free (r->options);
        r->options = NULL;
        r->op = WIZ_OP_EQ;
        r->logic = WIZ_END;
        wiz_load_row (0);
    }
    else
    {
        int idx = s_cur;
        g_array_remove_index (s_rules, (guint) idx);
        wiz_load_row (idx);
    }
    wiz_preview_update ();
    widget_draw (WIDGET (s_dlg));
}

/* --------------------------------------------------------------------------------------------- */

static void
wiz_draw_rows (void)
{
    Widget *w = WIDGET (s_dlg);
    guint i;

    tty_setcolor (DIALOG_NORMAL_COLOR);
    widget_gotoyx (w, WIZ_ROW0 - 2, WIZ_X_FIELD);
    tty_print_string (_ ("Field"));
    widget_gotoyx (w, WIZ_ROW0 - 2, WIZ_X_OP);
    tty_print_string (_ ("Type"));
    widget_gotoyx (w, WIZ_ROW0 - 2, WIZ_X_VAL);
    tty_print_string (_ ("Value"));
    widget_gotoyx (w, WIZ_ROW0 - 2, WIZ_X_LOGIC);
    tty_print_string (_ ("Join"));

    for (i = 0; i < WIZ_MAXVIS; i++)
    {
        guint ridx = (guint) s_top + i;
        const wiz_rule_t *r;
        int y = WIZ_ROW0 + (int) i;
        char buf[WIZ_W];

        if (ridx >= s_rules->len)
            break;
        if ((int) ridx == s_cur)
            continue;

        r = &g_array_index (s_rules, wiz_rule_t, ridx);

        g_snprintf (buf, sizeof (buf), "[ %s ]", wiz_field_label (r->field));
        widget_gotoyx (w, y, WIZ_X_FIELD);
        tty_print_string (buf);

        g_snprintf (buf, sizeof (buf), "[ %s ]", wiz_op_short[r->op]);
        widget_gotoyx (w, y, WIZ_X_OP);
        tty_print_string (buf);

        g_snprintf (buf, sizeof (buf), "%.27s", r->value != NULL ? r->value : "");
        widget_gotoyx (w, y, WIZ_X_VAL);
        tty_print_string (buf);

        g_snprintf (buf, sizeof (buf), "[ %s ]", wiz_logic_short[r->logic]);
        widget_gotoyx (w, y, WIZ_X_LOGIC);
        tty_print_string (buf);
    }

    /* Scrollbar in the WListbox style: a full-height vertical track with ^/v
       end caps and a '*' thumb at the current row's relative position. Drawn
       only when the rules overflow the visible window. */
    if ((int) s_rules->len > WIZ_MAXVIS)
    {
        int len = (int) s_rules->len;
        int last = WIZ_MAXVIS - 1;
        int thumb;
        int row;

        widget_gotoyx (w, WIZ_ROW0, WIZ_X_MARK);
        if (s_top == 0)
            tty_print_one_vline (TRUE);
        else
            tty_print_char ('^');

        widget_gotoyx (w, WIZ_ROW0 + last, WIZ_X_MARK);
        if (s_top + WIZ_MAXVIS >= len)
            tty_print_one_vline (TRUE);
        else
            tty_print_char ('v');

        thumb = 1 + (s_cur * (WIZ_MAXVIS - 2)) / len;
        for (row = 1; row < last; row++)
        {
            widget_gotoyx (w, WIZ_ROW0 + row, WIZ_X_MARK);
            if (row == thumb)
                tty_print_char ('*');
            else
                tty_print_one_vline (TRUE);
        }
    }
}

static cb_ret_t
wiz_dlg_cb (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_DRAW:
    {
        cb_ret_t r = dlg_default_callback (w, sender, msg, parm, data);
        wiz_draw_rows ();
        return r;
    }

    case MSG_UNHANDLED_KEY:
        switch (parm)
        {
        case KEY_UP:
            if (s_cur > 0)
            {
                wiz_commit_value ();
                wiz_load_row (s_cur - 1);
                widget_draw (w);
            }
            return MSG_HANDLED;
        case KEY_DOWN:
            if ((guint) s_cur + 1 < s_rules->len)
            {
                wiz_commit_value ();
                wiz_load_row (s_cur + 1);
                widget_draw (w);
            }
            return MSG_HANDLED;
        case KEY_IC:
            wiz_add_row ();
            return MSG_HANDLED;
        case KEY_DC:
            wiz_commit_value ();
            wiz_delete_row ();
            return MSG_HANDLED;
        case KEY_F (3):
            wiz_pick_value ();
            return MSG_HANDLED;
        default:
            return MSG_NOT_HANDLED;
        }

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Mouse on the rows area: a click selects the row under the cursor, the wheel
   moves the selection. Events over the live edit widgets are dispatched to
   those widgets and never reach here. */
static void
wiz_dlg_mouse_cb (Widget *w, mouse_msg_t msg, mouse_event_t *event)
{
    switch (msg)
    {
    case MSG_MOUSE_SCROLL_UP:
        if (s_cur > 0)
        {
            wiz_commit_value ();
            wiz_load_row (s_cur - 1);
            widget_draw (w);
        }
        break;

    case MSG_MOUSE_SCROLL_DOWN:
        if ((guint) s_cur + 1 < s_rules->len)
        {
            wiz_commit_value ();
            wiz_load_row (s_cur + 1);
            widget_draw (w);
        }
        break;

    case MSG_MOUSE_CLICK:
    {
        int vis = event->y - WIZ_ROW0;

        if (vis >= 0 && vis < WIZ_MAXVIS)
        {
            int idx = s_top + vis;

            if (idx >= 0 && (guint) idx < s_rules->len && idx != s_cur)
            {
                wiz_commit_value ();
                wiz_load_row (idx);
                widget_draw (w);
            }
        }
        else
            event->result.abort = TRUE;
        break;
    }

    default:
        event->result.abort = TRUE;
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

char *
mongo_wizard_run (const char *const *fields, mongo_wizard_values_fn values_fn, gpointer values_ctx,
                  const char *initial_json)
{
    WGroup *g;
    char *result = NULL;
    int rc;

    s_fields = fields;
    s_values_fn = values_fn;
    s_values_ctx = values_ctx;
    s_cur = 0;
    s_top = 0;
    s_rules = g_array_new (FALSE, FALSE, sizeof (wiz_rule_t));
    g_array_set_clear_func (s_rules, wiz_rule_clear);

    if (wiz_import_filter (initial_json) == 0)
    {
        wiz_rule_t first = { NULL, WIZ_OP_EQ, NULL, NULL, WIZ_END };

        if (s_fields != NULL && s_fields[0] != NULL)
            first.field = g_strdup (s_fields[0]);
        g_array_append_val (s_rules, first);
    }

    s_dlg = dlg_create (TRUE, (LINES - WIZ_H) / 2, (COLS - WIZ_W) / 2, WIZ_H, WIZ_W,
                        WPOS_KEEP_DEFAULT, TRUE, dialog_colors, wiz_dlg_cb, wiz_dlg_mouse_cb,
                        "[MongoDB Plugin]", _ ("Filter Builder"));
    g = GROUP (s_dlg);

    e_field = button_new (WIZ_ROW0, WIZ_X_FIELD, B_USER + 1, NORMAL_BUTTON, "...", wiz_field_cb);
    e_op = button_new (WIZ_ROW0, WIZ_X_OP, B_USER + 2, NORMAL_BUTTON, "==", wiz_op_cb);
    e_value = input_new (WIZ_ROW0, WIZ_X_VAL, input_colors, WIZ_W_VAL, "", "mongo-wiz-value",
                         INPUT_COMPLETE_NONE);
    e_logic = button_new (WIZ_ROW0, WIZ_X_LOGIC, B_USER + 3, NORMAL_BUTTON, "END", wiz_logic_cb);
    group_add_widget (g, e_field);
    group_add_widget (g, e_op);
    group_add_widget (g, e_value);
    group_add_widget (g, e_logic);
    group_add_widget (g, hline_new (WIZ_ROW0 - 1, -1, -1));

    {
        WHLine *hl;

        hl = hline_new (WIZ_PREVIEW_LINE, -1, -1);
        hline_set_text (hl, _ (" Preview "));
        group_add_widget (g, hl);
    }

    s_preview = label_new (WIZ_PREVIEW_ROW, 2, "{}");
    group_add_widget (g, s_preview);

    {
        WHLine *hl;

        hl = hline_new (WIZ_COMMANDS_LINE, -1, -1);
        hline_set_text (hl, _ (" Ins: add   Del: remove F3: values "));
        group_add_widget (g, hl);
    }

    group_add_widget (
        g, button_new (WIZ_BUTTON_ROW, 10, B_ENTER, DEFPUSH_BUTTON, _ ("&Apply to Filter"), NULL));
    group_add_widget (g,
                      button_new (WIZ_BUTTON_ROW, 34, B_USER + 9, NORMAL_BUTTON, _ ("&Template"),
                                  wiz_template_cb));
    group_add_widget (
        g, button_new (WIZ_BUTTON_ROW, 50, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    wiz_load_row (0);
    wiz_preview_update ();
    widget_select (WIDGET (e_value));

    rc = dlg_run (s_dlg);
    if (rc == B_ENTER)
    {
        wiz_commit_value ();
        result = wiz_generate ();
    }

    widget_destroy (WIDGET (s_dlg));

    g_array_free (s_rules, TRUE);
    s_rules = NULL;
    s_dlg = NULL;
    e_field = e_op = e_logic = NULL;
    e_value = NULL;
    s_preview = NULL;
    s_fields = NULL;
    s_values_fn = NULL;
    s_values_ctx = NULL;
    return result;
}

char *
mongo_wizard_doc_to_filter (const char *sample_doc_json)
{
    char *result = NULL;

    s_rules = g_array_new (FALSE, FALSE, sizeof (wiz_rule_t));
    g_array_set_clear_func (s_rules, wiz_rule_clear);
    if (wiz_import_doc_fields (sample_doc_json) > 0)
        result = wiz_generate ();
    g_array_free (s_rules, TRUE);
    s_rules = NULL;
    return result;
}

/* --------------------------------------------------------------------------------------------- */
