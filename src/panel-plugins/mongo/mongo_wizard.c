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

/*** file scope types ****************************************************************************/

typedef enum
{
    WIZ_OP_EQ = 0,
    WIZ_OP_NE,
    WIZ_OP_GT,
    WIZ_OP_GTE,
    WIZ_OP_LT,
    WIZ_OP_LTE,
    WIZ_OP_IN,
    WIZ_OP_NIN,
    WIZ_OP_REGEX,
    WIZ_OP_LIKE,
    WIZ_OP_EXISTS,
    WIZ_OP_COUNT
} wiz_op_t;

typedef enum
{
    WIZ_AND = 0, /* chain with the next row inside one $and group */
    WIZ_OR,      /* close the group; the next row starts a new $or member */
    WIZ_END,     /* close the group (terminator); same grouping as OR */
    WIZ_LOGIC_COUNT
} wiz_logic_t;

typedef struct
{
    char *field;
    wiz_op_t op;
    char *value;
    char *options;
    wiz_logic_t logic; /* how this row joins the next */
} wiz_rule_t;

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
#define WIZ_ROW0          3  /* first data row */
#define WIZ_MAXVIS        11 /* visible rows (scrolling window) */
#define WIZ_MAXROWS       64 /* hard cap on total conditions */
#define WIZ_X_MARK        2
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
static GArray *s_rules = NULL; /* wiz_rule_t; invariant: len >= 1 */
static int s_cur = 0;          /* current row */
static int s_top = 0;          /* first visible row (scroll offset) */
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
/* JSON generation                                                                               */
/* --------------------------------------------------------------------------------------------- */

static char *
wiz_json_quote (const char *s)
{
    GString *o = g_string_new ("\"");
    const char *p;

    for (p = s != NULL ? s : ""; *p != '\0'; p++)
        switch (*p)
        {
        case '"':
            g_string_append (o, "\\\"");
            break;
        case '\\':
            g_string_append (o, "\\\\");
            break;
        case '\n':
            g_string_append (o, "\\n");
            break;
        case '\r':
            g_string_append (o, "\\r");
            break;
        case '\t':
            g_string_append (o, "\\t");
            break;
        default:
            g_string_append_c (o, *p);
        }
    g_string_append_c (o, '"');
    return g_string_free (o, FALSE);
}

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

static gboolean
wiz_is_number (const char *v)
{
    char *end = NULL;

    if (v[0] == '\0')
        return FALSE;
    if (!(g_ascii_isdigit (v[0]) || ((v[0] == '-' || v[0] == '+') && g_ascii_isdigit (v[1]))))
        return FALSE;
    (void) g_ascii_strtod (v, &end);
    return end != NULL && *end == '\0';
}

static gboolean
wiz_has_edge_space (const char *s)
{
    gsize len;

    if (s == NULL || s[0] == '\0')
        return FALSE;
    len = strlen (s);
    return g_ascii_isspace (s[0]) || g_ascii_isspace (s[len - 1]);
}

/* Bare token inside ObjectId("..")/ISODate("..") with quotes stripped. */
static char *
wiz_ctor_inner (const char *v)
{
    const char *open = strchr (v, '(');
    const char *close = strrchr (v, ')');
    char *inner;
    char *t;
    gsize len;

    if (open == NULL || close == NULL || close <= open)
        return g_strdup ("");
    inner = g_strstrip (g_strndup (open + 1, (gsize) (close - open - 1)));
    len = strlen (inner);
    if (len >= 2 && (inner[0] == '"' || inner[0] == '\'') && (inner[len - 1] == inner[0]))
    {
        t = g_strndup (inner + 1, len - 2);
        g_free (inner);
        return t;
    }
    return inner;
}

/* TRUE if @v is a complete, well-formed JSON string literal: opens and closes
   with '"', the closing quote is the last character, and backslash escapes are
   balanced. Used to decide whether a value can be emitted verbatim. */
static gboolean
wiz_is_json_string (const char *v)
{
    gsize i, len;

    if (v == NULL || v[0] != '"')
        return FALSE;
    len = strlen (v);
    if (len < 2)
        return FALSE;
    for (i = 1; i < len; i++)
    {
        if (v[i] == '\\')
        {
            i++;
            if (i >= len)
                return FALSE; /* dangling escape */
            continue;
        }
        if (v[i] == '"')
            return i == len - 1; /* a closing quote is valid only as the last char */
    }
    return FALSE; /* never closed */
}

/* JSON literal for one scalar value (caller g_free). */
static char *
wiz_scalar_json (const char *raw)
{
    char *v = g_strstrip (g_strdup (raw != NULL ? raw : ""));
    char *out;

    if (v[0] == '\0')
        out = g_strdup ("\"\"");
    else if (v[0] == '"' && wiz_is_json_string (v))
        out = g_strdup (v);
    else if (strcmp (v, "true") == 0 || strcmp (v, "false") == 0 || strcmp (v, "null") == 0)
        out = g_strdup (v);
    else if (g_str_has_prefix (v, "ObjectId(") || g_str_has_prefix (v, "ISODate("))
    {
        const char *key = v[0] == 'O' ? "$oid" : "$date";
        char *inner = wiz_ctor_inner (v);
        char *q = wiz_json_quote (inner);
        out = g_strdup_printf ("{ \"%s\": %s }", key, q);
        g_free (inner);
        g_free (q);
    }
    else if (wiz_is_number (v))
        /* JSON has no leading '+', so drop it; the rest is already canonical. */
        out = g_strdup (v[0] == '+' ? v + 1 : v);
    else
        out = wiz_json_quote (v);

    g_free (v);
    return out;
}

static char *
wiz_picker_scalar_json (const char *raw)
{
    if (wiz_has_edge_space (raw))
        return wiz_json_quote (raw);
    return wiz_scalar_json (raw);
}

static char *
wiz_picker_value_text (const char *raw)
{
    if (wiz_has_edge_space (raw))
        return wiz_json_quote (raw);
    return g_strdup (raw != NULL ? raw : "");
}

static gboolean
wiz_is_json_array_text (const char *raw)
{
    char *v = g_strstrip (g_strdup (raw != NULL ? raw : ""));
    gboolean ok = FALSE;

    if (v[0] == '[')
    {
        char *wrapped = g_strdup_printf ("{ \"v\": %s }", v);
        bson_t *doc = bson_new_from_json ((const uint8_t *) wrapped, -1, NULL);
        bson_iter_t it;

        ok = doc != NULL && bson_iter_init_find (&it, doc, "v") && BSON_ITER_HOLDS_ARRAY (&it);
        if (doc != NULL)
            bson_destroy (doc);
        g_free (wrapped);
    }
    g_free (v);
    return ok;
}

static char *
wiz_bson_value_json (bson_iter_t *it)
{
    switch (bson_iter_type (it))
    {
    case BSON_TYPE_UTF8:
    {
        uint32_t l = 0;
        const char *s = bson_iter_utf8 (it, &l);
        char *copy = g_strndup (s, l);
        char *out = wiz_json_quote (copy);

        g_free (copy);
        return out;
    }
    case BSON_TYPE_INT32:
        return g_strdup_printf ("%d", bson_iter_int32 (it));
    case BSON_TYPE_INT64:
        return g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) bson_iter_int64 (it));
    case BSON_TYPE_DOUBLE:
    {
        char b[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr (b, sizeof (b), bson_iter_double (it));
        return g_strdup (b);
    }
    case BSON_TYPE_BOOL:
        return g_strdup (bson_iter_bool (it) ? "true" : "false");
    case BSON_TYPE_NULL:
        return g_strdup ("null");
    case BSON_TYPE_OID:
    {
        char s[25];
        bson_oid_to_string (bson_iter_oid (it), s);
        return g_strdup_printf ("{ \"$oid\": \"%s\" }", s);
    }
    case BSON_TYPE_DATE_TIME:
    {
        gint64 ms = bson_iter_date_time (it);
        GDateTime *dt = g_date_time_new_from_unix_utc (ms / 1000);
        char *iso;
        char *out;

        if (dt == NULL)
            return NULL;
        iso = g_date_time_format (dt, "%Y-%m-%dT%H:%M:%SZ");
        g_date_time_unref (dt);
        out = g_strdup_printf ("{ \"$date\": \"%s\" }", iso != NULL ? iso : "");
        g_free (iso);
        return out;
    }
    default:
        return NULL;
    }
}

/* Translate a shell-style wildcard pattern to a regex: '*' -> '.*', '?' -> '.',
   every other regex metacharacter is escaped so it matches literally. */
static char *
wiz_glob_to_regex (const char *v)
{
    GString *o = g_string_new (NULL);
    const char *p;

    for (p = v != NULL ? v : ""; *p != '\0'; p++)
        switch (*p)
        {
        case '*':
            g_string_append (o, ".*");
            break;
        case '?':
            g_string_append_c (o, '.');
            break;
        case '.':
        case '^':
        case '$':
        case '+':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '|':
        case '\\':
            g_string_append_c (o, '\\');
            g_string_append_c (o, *p);
            break;
        default:
            g_string_append_c (o, *p);
        }
    return g_string_free (o, FALSE);
}

/* JSON for {field: <op expr>} (caller g_free). */
static char *
wiz_rule_json (const wiz_rule_t *r)
{
    char *key = wiz_json_quote (r->field != NULL ? r->field : "");
    char *expr;
    char *out;

    switch (r->op)
    {
    case WIZ_OP_NE:
    case WIZ_OP_GT:
    case WIZ_OP_GTE:
    case WIZ_OP_LT:
    case WIZ_OP_LTE:
    {
        const char *opk = r->op == WIZ_OP_NE ? "$ne"
            : r->op == WIZ_OP_GT             ? "$gt"
            : r->op == WIZ_OP_GTE            ? "$gte"
            : r->op == WIZ_OP_LT             ? "$lt"
                                             : "$lte";
        char *s = wiz_scalar_json (r->value);
        expr = g_strdup_printf ("{ \"%s\": %s }", opk, s);
        g_free (s);
        break;
    }
    case WIZ_OP_IN:
    case WIZ_OP_NIN:
    {
        const char *opk = r->op == WIZ_OP_NIN ? "$nin" : "$in";
        char *value = g_strstrip (g_strdup (r->value != NULL ? r->value : ""));

        if (wiz_is_json_array_text (value))
            expr = g_strdup_printf ("{ \"%s\": %s }", opk, value);
        else
        {
            char **parts = g_strsplit (value, ",", -1);
            GString *arr = g_string_new ("[");
            guint i;

            for (i = 0; parts[i] != NULL; i++)
            {
                char *sc = wiz_scalar_json (parts[i]);
                if (i != 0)
                    g_string_append (arr, ", ");
                g_string_append (arr, sc);
                g_free (sc);
            }
            g_string_append_c (arr, ']');
            g_strfreev (parts);
            expr = g_strdup_printf ("{ \"%s\": %s }", opk, arr->str);
            g_string_free (arr, TRUE);
        }
        g_free (value);
        break;
    }
    case WIZ_OP_REGEX:
    {
        char *p = wiz_json_quote (r->value != NULL ? r->value : "");
        if (r->options != NULL)
        {
            char *o = wiz_json_quote (r->options);

            expr = g_strdup_printf ("{ \"$regex\": %s, \"$options\": %s }", p, o);
            g_free (o);
        }
        else
            expr = g_strdup_printf ("{ \"$regex\": %s }", p);
        g_free (p);
        break;
    }
    case WIZ_OP_LIKE:
    {
        char *rx = wiz_glob_to_regex (r->value);
        char *p = wiz_json_quote (rx);
        expr = g_strdup_printf ("{ \"$regex\": %s, \"$options\": \"i\" }", p);
        g_free (rx);
        g_free (p);
        break;
    }
    case WIZ_OP_EXISTS:
    {
        gboolean b =
            !(r->value != NULL
              && (g_ascii_strcasecmp (r->value, "false") == 0 || strcmp (r->value, "0") == 0));
        expr = g_strdup_printf ("{ \"$exists\": %s }", b ? "true" : "false");
        break;
    }
    case WIZ_OP_EQ:
    default:
        expr = wiz_scalar_json (r->value);
        break;
    }

    out = g_strdup_printf ("{ %s: %s }", key, expr);
    g_free (key);
    g_free (expr);
    return out;
}

/* Combine non-empty rules into one JSON document (caller g_free), or NULL. */
static char *
wiz_generate (void)
{
    GPtrArray *members;
    GString *run;
    guint run_count = 0;
    guint i;
    char *out;

    if (s_rules == NULL)
        return NULL;

    members = g_ptr_array_new_with_free_func (g_free);
    run = g_string_new (NULL);

    for (i = 0; i < s_rules->len; i++)
    {
        const wiz_rule_t *rule = &g_array_index (s_rules, wiz_rule_t, i);
        char *rj;
        gboolean last_valid;
        guint j;

        if (rule->field == NULL || rule->field[0] == '\0')
            continue; /* skip blank rows */

        rj = wiz_rule_json (rule);
        if (run_count != 0)
            g_string_append (run, ", ");
        g_string_append (run, rj);
        g_free (rj);
        run_count++;

        /* End the run if this is the last non-empty rule, or it joins with OR. */
        last_valid = TRUE;
        for (j = i + 1; j < s_rules->len; j++)
        {
            const wiz_rule_t *n = &g_array_index (s_rules, wiz_rule_t, j);
            if (n->field != NULL && n->field[0] != '\0')
            {
                last_valid = FALSE;
                break;
            }
        }
        if (last_valid || rule->logic == WIZ_OR || rule->logic == WIZ_END)
        {
            char *member = run_count == 1 ? g_strdup (run->str)
                                          : g_strdup_printf ("{ \"$and\": [%s] }", run->str);
            g_ptr_array_add (members, member);
            g_string_truncate (run, 0);
            run_count = 0;
        }
    }
    g_string_free (run, TRUE);

    if (members->len == 0)
        out = NULL;
    else if (members->len == 1)
        out = g_strdup ((const char *) g_ptr_array_index (members, 0));
    else
    {
        GString *o = g_string_new ("{ \"$or\": [");
        for (i = 0; i < members->len; i++)
        {
            if (i != 0)
                g_string_append (o, ", ");
            g_string_append (o, (const char *) g_ptr_array_index (members, i));
        }
        g_string_append (o, "] }");
        out = g_string_free (o, FALSE);
    }
    g_ptr_array_free (members, TRUE);
    return out;
}

/* --------------------------------------------------------------------------------------------- */
/* Reverse import: parse a filter back into rules (best-effort, wizard subset)                   */
/* --------------------------------------------------------------------------------------------- */

/* Value text as the value input expects it, or NULL if the type is not a
   single representable wizard value. */
static char *
wiz_value_text (bson_iter_t *it)
{
    switch (bson_iter_type (it))
    {
    case BSON_TYPE_UTF8:
    {
        uint32_t l = 0;
        const char *s = bson_iter_utf8 (it, &l);
        char *copy = g_strndup (s, l);
        gboolean typed = wiz_has_edge_space (copy) || wiz_is_number (copy)
            || strcmp (copy, "true") == 0 || strcmp (copy, "false") == 0
            || strcmp (copy, "null") == 0 || g_str_has_prefix (copy, "ObjectId(")
            || g_str_has_prefix (copy, "ISODate(");

        if (typed)
        {
            char *quoted = wiz_json_quote (copy);

            g_free (copy);
            return quoted;
        }
        return copy;
    }
    case BSON_TYPE_INT32:
        return g_strdup_printf ("%d", bson_iter_int32 (it));
    case BSON_TYPE_INT64:
        return g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) bson_iter_int64 (it));
    case BSON_TYPE_DOUBLE:
    {
        char b[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr (b, sizeof (b), bson_iter_double (it));
        return g_strdup (b);
    }
    case BSON_TYPE_BOOL:
        return g_strdup (bson_iter_bool (it) ? "true" : "false");
    case BSON_TYPE_NULL:
        return g_strdup ("null");
    case BSON_TYPE_OID:
    {
        char s[25];
        bson_oid_to_string (bson_iter_oid (it), s);
        return g_strdup_printf ("ObjectId(\"%s\")", s);
    }
    case BSON_TYPE_DATE_TIME:
    {
        gint64 ms = bson_iter_date_time (it);
        GDateTime *dt = g_date_time_new_from_unix_utc (ms / 1000);
        char *iso;
        char *out;

        if (dt == NULL)
            return NULL;
        iso = g_date_time_format (dt, "%Y-%m-%dT%H:%M:%SZ");
        g_date_time_unref (dt);
        out = g_strdup_printf ("ISODate(\"%s\")", iso != NULL ? iso : "");
        g_free (iso);
        return out;
    }
    default:
        return NULL;
    }
}

/* Map a Mongo comparison/set/exists operator key to a wizard op. FALSE if the
   key is not one the builder models (e.g. $elemMatch, $regex handled apart). */
static gboolean
wiz_op_from_key (const char *k, wiz_op_t *op)
{
    if (strcmp (k, "$ne") == 0)
        *op = WIZ_OP_NE;
    else if (strcmp (k, "$gt") == 0)
        *op = WIZ_OP_GT;
    else if (strcmp (k, "$gte") == 0)
        *op = WIZ_OP_GTE;
    else if (strcmp (k, "$lt") == 0)
        *op = WIZ_OP_LT;
    else if (strcmp (k, "$lte") == 0)
        *op = WIZ_OP_LTE;
    else if (strcmp (k, "$in") == 0)
        *op = WIZ_OP_IN;
    else if (strcmp (k, "$nin") == 0)
        *op = WIZ_OP_NIN;
    else if (strcmp (k, "$exists") == 0)
        *op = WIZ_OP_EXISTS;
    else
        return FALSE;
    return TRUE;
}

/* Value text for operator @op read from @v (caller g_free), or NULL if @v is
   not representable. */
static char *
wiz_value_for_op (wiz_op_t op, bson_iter_t *v)
{
    if (op == WIZ_OP_IN || op == WIZ_OP_NIN)
    {
        bson_iter_t arr;
        GString *acc;

        if (!BSON_ITER_HOLDS_ARRAY (v) || !bson_iter_recurse (v, &arr))
            return NULL;
        acc = g_string_new (NULL);
        while (bson_iter_next (&arr))
        {
            char *t = wiz_bson_value_json (&arr);

            if (t == NULL)
            {
                g_string_free (acc, TRUE);
                return NULL;
            }
            if (acc->len != 0)
                g_string_append (acc, ", ");
            g_string_append (acc, t);
            g_free (t);
        }
        g_string_prepend_c (acc, '[');
        g_string_append_c (acc, ']');
        return g_string_free (acc, FALSE);
    }
    if (op == WIZ_OP_EXISTS)
        return g_strdup (bson_iter_as_bool (v) ? "true" : "false");
    return wiz_value_text (v);
}

/* Append rule(s) for {field: <it>}; FALSE if the shape is outside the subset.
   A value document with several operators ({$gte,$lte}, ...) becomes several
   AND-joined rules on the same field, which is how date/number ranges are
   modelled. */
static gboolean
wiz_append_rule_from (const char *field, bson_iter_t *it, wiz_logic_t logic)
{
    bson_iter_t sub;
    const char *regex = NULL;
    const char *options = NULL;
    gboolean has_options = FALSE;
    gboolean only_regex_opts = TRUE;
    int nkeys = 0;

    if (!BSON_ITER_HOLDS_DOCUMENT (it))
    {
        wiz_rule_t r = { NULL, WIZ_OP_EQ, NULL, NULL, logic };

        r.value = wiz_value_text (it);
        if (r.value == NULL)
            return FALSE;
        r.field = g_strdup (field);
        g_array_append_val (s_rules, r);
        return TRUE;
    }

    /* Detect a $regex (+optional $options) value, which maps to one rule. */
    if (!bson_iter_recurse (it, &sub))
        return FALSE;
    while (bson_iter_next (&sub))
    {
        const char *k = bson_iter_key (&sub);

        nkeys++;
        if (strcmp (k, "$regex") == 0 && BSON_ITER_HOLDS_UTF8 (&sub))
            regex = bson_iter_utf8 (&sub, NULL);
        else if (strcmp (k, "$options") == 0 && BSON_ITER_HOLDS_UTF8 (&sub))
        {
            options = bson_iter_utf8 (&sub, NULL);
            has_options = TRUE;
        }
        else
            only_regex_opts = FALSE;
    }

    if (regex != NULL && only_regex_opts)
    {
        wiz_rule_t r = { g_strdup (field), WIZ_OP_REGEX, g_strdup (regex),
                         has_options ? g_strdup (options) : NULL, logic };
        g_array_append_val (s_rules, r);
        return TRUE;
    }
    if (nkeys == 0)
        return FALSE;

    /* One rule per operator key (same field, AND-joined). */
    if (!bson_iter_recurse (it, &sub))
        return FALSE;
    while (bson_iter_next (&sub))
    {
        wiz_rule_t r = { NULL, WIZ_OP_EQ, NULL, NULL, WIZ_AND };

        if (!wiz_op_from_key (bson_iter_key (&sub), &r.op))
            return FALSE;
        r.value = wiz_value_for_op (r.op, &sub);
        if (r.value == NULL)
            return FALSE;
        r.field = g_strdup (field);
        g_array_append_val (s_rules, r);
    }
    return TRUE;
}

/* Parse a plain rule doc (field:value pairs), AND-joined. */
static gboolean
wiz_parse_simple (bson_iter_t *child)
{
    gboolean any = FALSE;

    while (bson_iter_next (child))
    {
        const char *field = bson_iter_key (child);

        if (field[0] == '$') /* operator at rule-doc top -> outside subset */
            return FALSE;
        if (!wiz_append_rule_from (field, child, WIZ_AND))
            return FALSE;
        any = TRUE;
    }
    return any;
}

/* Parse an array of rule docs ($and body), AND-joined. */
static gboolean
wiz_parse_and_array (bson_iter_t *arr)
{
    gboolean any = FALSE;

    while (bson_iter_next (arr))
    {
        bson_iter_t elem;

        if (!BSON_ITER_HOLDS_DOCUMENT (arr) || !bson_iter_recurse (arr, &elem))
            return FALSE;
        if (!wiz_parse_simple (&elem))
            return FALSE;
        any = TRUE;
    }
    return any;
}

/* Parse one $or member (a {$and:[...]} group or a plain rule doc). */
static gboolean
wiz_parse_member (bson_iter_t *member)
{
    bson_iter_t scan, child;
    const char *k1 = NULL;
    int nkeys = 0;

    if (!BSON_ITER_HOLDS_DOCUMENT (member) || !bson_iter_recurse (member, &scan))
        return FALSE;
    while (bson_iter_next (&scan))
    {
        if (nkeys == 0)
            k1 = bson_iter_key (&scan);
        nkeys++;
    }

    if (nkeys == 1 && strcmp (k1, "$and") == 0)
    {
        bson_iter_t andit, arr;

        if (!bson_iter_recurse (member, &andit) || !bson_iter_next (&andit)
            || !BSON_ITER_HOLDS_ARRAY (&andit) || !bson_iter_recurse (&andit, &arr))
            return FALSE;
        return wiz_parse_and_array (&arr);
    }

    if (!bson_iter_recurse (member, &child))
        return FALSE;
    return wiz_parse_simple (&child);
}

/* Best-effort import of @json into s_rules. Returns the number of rules added,
   or 0 if the filter is empty, unparsable, or outside the wizard subset (in
   which case s_rules is left untouched). */
static int
wiz_import_filter (const char *json)
{
    bson_t *doc;
    bson_iter_t scan;
    const char *k1 = NULL;
    int nkeys = 0;
    guint start = s_rules->len;
    gboolean ok = FALSE;

    if (json == NULL || json[0] == '\0')
        return 0;
    doc = bson_new_from_json ((const uint8_t *) json, -1, NULL);
    if (doc == NULL)
        return 0;

    if (bson_iter_init (&scan, doc))
        while (bson_iter_next (&scan))
        {
            if (nkeys == 0)
                k1 = bson_iter_key (&scan);
            nkeys++;
        }

    if (nkeys == 1 && k1 != NULL && strcmp (k1, "$or") == 0)
    {
        bson_iter_t orit, arr;
        int n = 0;
        int i = 0;

        if (bson_iter_init_find (&orit, doc, "$or") && BSON_ITER_HOLDS_ARRAY (&orit)
            && bson_iter_recurse (&orit, &arr))
            while (bson_iter_next (&arr))
                n++;

        if (n > 0 && bson_iter_init_find (&orit, doc, "$or") && bson_iter_recurse (&orit, &arr))
        {
            ok = TRUE;
            while (ok && bson_iter_next (&arr))
            {
                guint before = s_rules->len;

                if (!wiz_parse_member (&arr))
                    ok = FALSE;
                else if (s_rules->len > before)
                {
                    wiz_rule_t *last = &g_array_index (s_rules, wiz_rule_t, s_rules->len - 1);
                    last->logic = (i < n - 1) ? WIZ_OR : WIZ_END;
                }
                i++;
            }
        }
    }
    else if (nkeys == 1 && k1 != NULL && strcmp (k1, "$and") == 0)
    {
        bson_iter_t andit, arr;

        if (bson_iter_init_find (&andit, doc, "$and") && BSON_ITER_HOLDS_ARRAY (&andit)
            && bson_iter_recurse (&andit, &arr))
            ok = wiz_parse_and_array (&arr);
    }
    else if (nkeys > 0)
    {
        bson_iter_t child;

        if (bson_iter_init (&child, doc))
            ok = wiz_parse_simple (&child);
    }

    bson_destroy (doc);

    if (ok && s_rules->len > start)
    {
        wiz_rule_t *last = &g_array_index (s_rules, wiz_rule_t, s_rules->len - 1);
        if (last->logic == WIZ_AND) /* not already set to OR/END by the $or path */
            last->logic = WIZ_END;
    }

    if (!ok)
    {
        while (s_rules->len > start)
            g_array_remove_index (s_rules, s_rules->len - 1);
        return 0;
    }
    return (int) (s_rules->len - start);
}

/* Lenient import of a whole document's top-level scalar fields as == rules
   (non-scalar fields are skipped, not a failure). Returns the count added. */
static int
wiz_import_doc_fields (const char *json)
{
    bson_t *doc;
    bson_iter_t it;
    guint start = s_rules->len;

    if (json == NULL || json[0] == '\0')
        return 0;
    doc = bson_new_from_json ((const uint8_t *) json, -1, NULL);
    if (doc == NULL)
        return 0;

    if (bson_iter_init (&it, doc))
        while (bson_iter_next (&it))
        {
            const char *field = bson_iter_key (&it);
            char *value;
            wiz_rule_t r;

            if (field[0] == '$')
                continue;
            value = wiz_value_text (&it);
            if (value == NULL)
                continue; /* nested doc/array - skip */
            r.field = g_strdup (field);
            r.op = WIZ_OP_EQ;
            r.value = value;
            r.options = NULL;
            r.logic = WIZ_AND;
            g_array_append_val (s_rules, r);
        }
    bson_destroy (doc);

    if (s_rules->len > start)
    {
        wiz_rule_t *last = &g_array_index (s_rules, wiz_rule_t, s_rules->len - 1);
        last->logic = WIZ_END;
    }
    return (int) (s_rules->len - start);
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

    if (s_top > 0)
    {
        widget_gotoyx (w, WIZ_ROW0, WIZ_X_MARK);
        tty_print_string ("^");
    }
    if (s_top + WIZ_MAXVIS < (int) s_rules->len)
    {
        widget_gotoyx (w, WIZ_ROW0 + WIZ_MAXVIS - 1, WIZ_X_MARK);
        tty_print_string ("v");
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
                        WPOS_KEEP_DEFAULT, TRUE, dialog_colors, wiz_dlg_cb, NULL,
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
