/*
   MongoDB panel plugin -- filter-builder wizard, reverse import / parser.

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

   A best-effort recursive-descent parser over the small filter subset the
   wizard can model: plain {field: value} docs, $and arrays, $or members, and
   per-field operator documents. Parsed shapes are appended to s_rules. No
   dialog or widget state; testable on plain BSON/JSON input.
 */

#include <config.h>

#include <string.h>

#include <bson/bson.h>

#include "lib/global.h"

#include "mongo_wizard_priv.h"

/*** file scope functions ************************************************************************/

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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */

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

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Best-effort import of @json into s_rules. Returns the number of rules added,
   or 0 if the filter is empty, unparsable, or outside the wizard subset (in
   which case s_rules is left untouched). */
int
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

/* --------------------------------------------------------------------------------------------- */

/* Lenient import of a whole document's top-level scalar fields as == rules
   (non-scalar fields are skipped, not a failure). Returns the count added. */
int
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
