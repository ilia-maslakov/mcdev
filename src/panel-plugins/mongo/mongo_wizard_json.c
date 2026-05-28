/*
   MongoDB panel plugin -- filter-builder wizard, JSON generation.

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

   The rendering side of the wizard: rules in, JSON out. Values use Extended
   JSON ($oid/$date) so bson_new_from_json accepts them. No dialog or widget
   state; the only shared input is the s_rules list (read in wiz_generate).
 */

#include <config.h>

#include <string.h>

#include <bson/bson.h>

#include "lib/global.h"

#include "mongo_wizard_priv.h"

/*** file scope functions ************************************************************************/

char *
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

/* --------------------------------------------------------------------------------------------- */

gboolean
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

/* --------------------------------------------------------------------------------------------- */

gboolean
wiz_has_edge_space (const char *s)
{
    gsize len;

    if (s == NULL || s[0] == '\0')
        return FALSE;
    len = strlen (s);
    return g_ascii_isspace (s[0]) || g_ascii_isspace (s[len - 1]);
}

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

char *
wiz_picker_scalar_json (const char *raw)
{
    if (wiz_has_edge_space (raw))
        return wiz_json_quote (raw);
    return wiz_scalar_json (raw);
}

/* --------------------------------------------------------------------------------------------- */

char *
wiz_picker_value_text (const char *raw)
{
    if (wiz_has_edge_space (raw))
        return wiz_json_quote (raw);
    return g_strdup (raw != NULL ? raw : "");
}

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

char *
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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Combine non-empty rules into one JSON document (caller g_free), or NULL. */
char *
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
