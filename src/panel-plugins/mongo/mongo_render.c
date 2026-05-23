/*
   MongoDB panel plugin -- BSON value rendering.

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
 */

#include <config.h>

#include <inttypes.h>
#include <string.h>

#include <bson/bson.h>

#include "lib/global.h"

#include "mongo_render.h"

/*** file scope macros ***************************************************************************/

#define BASE64_KEY     "\"base64\""
#define BASE64_KEY_LEN 8
#define ELLIPSIS       "..."
#define ELLIPSIS_LEN   3

/*** public functions ****************************************************************************/

void
mongo_render_value (const bson_value_t *v, char *buf, gsize buflen)
{
    if (buf == NULL || buflen == 0)
        return;
    if (v == NULL)
    {
        g_strlcpy (buf, "?", buflen);
        return;
    }

    switch (v->value_type)
    {
    case BSON_TYPE_OID:
    {
        char hex[25];
        bson_oid_to_string (&v->value.v_oid, hex);
        g_strlcpy (buf, hex, buflen);
        return;
    }
    case BSON_TYPE_UTF8:
    {
        const char *s = v->value.v_utf8.str;
        if (s == NULL)
            g_strlcpy (buf, "\"\"", buflen);
        else
        {
            int n = g_snprintf (buf, buflen, "\"%s\"", s);
            /* On truncation the closing quote is lost; restore it so the cell
               still reads as a quoted (clipped) string. */
            if (n < 0 || (gsize) n >= buflen)
                if (buflen >= 2)
                    buf[buflen - 2] = '"';
        }
        return;
    }
    case BSON_TYPE_INT32:
        g_snprintf (buf, buflen, "%" PRId32, v->value.v_int32);
        return;
    case BSON_TYPE_INT64:
        g_snprintf (buf, buflen, "%" PRId64, v->value.v_int64);
        return;
    case BSON_TYPE_DOUBLE:
        g_snprintf (buf, buflen, "%.6g", v->value.v_double);
        return;
    case BSON_TYPE_BOOL:
        g_strlcpy (buf, v->value.v_bool ? "true" : "false", buflen);
        return;
    case BSON_TYPE_NULL:
        g_strlcpy (buf, "null", buflen);
        return;
    case BSON_TYPE_DATE_TIME:
        g_snprintf (buf, buflen, "Date(%" PRId64 ")", v->value.v_datetime);
        return;
    case BSON_TYPE_DOCUMENT:
        g_strlcpy (buf, "<doc>", buflen);
        return;
    case BSON_TYPE_ARRAY:
        g_strlcpy (buf, "<arr>", buflen);
        return;
    case BSON_TYPE_BINARY:
        g_snprintf (buf, buflen, "<bin %u B>", v->value.v_binary.data_len);
        return;
    default:
        g_strlcpy (buf, "<?>", buflen);
        return;
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_render_type (const bson_value_t *v, char *buf, gsize buflen)
{
    if (buf == NULL || buflen == 0)
        return;
    if (v == NULL)
    {
        g_strlcpy (buf, "?", buflen);
        return;
    }
    switch (v->value_type)
    {
    case BSON_TYPE_OID:
        g_strlcpy (buf, "oid", buflen);
        return;
    case BSON_TYPE_UTF8:
        g_strlcpy (buf, "str", buflen);
        return;
    case BSON_TYPE_INT32:
        g_strlcpy (buf, "int", buflen);
        return;
    case BSON_TYPE_INT64:
        g_strlcpy (buf, "i64", buflen);
        return;
    case BSON_TYPE_DOUBLE:
        g_strlcpy (buf, "dbl", buflen);
        return;
    case BSON_TYPE_BOOL:
        g_strlcpy (buf, "bool", buflen);
        return;
    case BSON_TYPE_NULL:
        g_strlcpy (buf, "null", buflen);
        return;
    case BSON_TYPE_DATE_TIME:
        g_strlcpy (buf, "date", buflen);
        return;
    case BSON_TYPE_DOCUMENT:
        g_strlcpy (buf, "doc", buflen);
        return;
    case BSON_TYPE_ARRAY:
        g_strlcpy (buf, "arr", buflen);
        return;
    case BSON_TYPE_BINARY:
        g_strlcpy (buf, "bin", buflen);
        return;
    default:
        g_strlcpy (buf, "?", buflen);
        return;
    }
}

/* --------------------------------------------------------------------------------------------- */

char *
mongo_render_truncate_base64 (const char *json, gsize keep)
{
    GString *out;
    const char *p;

    if (json == NULL)
        return NULL;
    if (keep < ELLIPSIS_LEN + 1)
        keep = ELLIPSIS_LEN + 1;

    out = g_string_sized_new (strlen (json));
    p = json;

    while (*p != '\0')
    {
        if (strncmp (p, BASE64_KEY, BASE64_KEY_LEN) == 0)
        {
            const char *colon_scan;
            const char *start;
            gsize len;

            /* Accept both "base64": and "base64" : renderings. */
            colon_scan = p + BASE64_KEY_LEN;
            while (*colon_scan == ' ' || *colon_scan == '\t' || *colon_scan == '\n'
                   || *colon_scan == '\r')
                colon_scan++;
            if (*colon_scan != ':')
            {
                g_string_append_c (out, *p);
                p++;
                continue;
            }
            colon_scan++;
            while (*colon_scan == ' ' || *colon_scan == '\t' || *colon_scan == '\n'
                   || *colon_scan == '\r')
                colon_scan++;
            if (*colon_scan != '"')
            {
                g_string_append_c (out, *p);
                p++;
                continue;
            }

            g_string_append_len (out, p, (gssize) (colon_scan - p + 1));
            p = colon_scan + 1;

            start = p;
            while (*p != '\0' && *p != '"')
                p++;
            len = (gsize) (p - start);

            if (len > keep)
            {
                g_string_append_len (out, start, (gssize) (keep - ELLIPSIS_LEN));
                g_string_append_len (out, ELLIPSIS, ELLIPSIS_LEN);
            }
            else
                g_string_append_len (out, start, (gssize) len);

            if (*p == '"')
            {
                g_string_append_c (out, *p);
                p++;
            }
        }
        else
        {
            g_string_append_c (out, *p);
            p++;
        }
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static void
append_indent (GString *out, int depth, int step)
{
    int n = depth * step;
    int i;
    if (n <= 0)
        return;
    for (i = 0; i < n; i++)
        g_string_append_c (out, ' ');
}

char *
mongo_render_pretty_json (const char *json, int indent)
{
    GString *out;
    const char *p;
    int depth = 0;
    gboolean in_string = FALSE;
    gboolean escape = FALSE;

    if (json == NULL)
        return NULL;
    if (indent < 0)
        indent = 0;
    out = g_string_sized_new (strlen (json) * 2 + 16);

    for (p = json; *p != '\0'; p++)
    {
        char c = *p;

        if (in_string)
        {
            g_string_append_c (out, c);
            if (escape)
                escape = FALSE;
            else if (c == '\\')
                escape = TRUE;
            else if (c == '"')
                in_string = FALSE;
            continue;
        }

        switch (c)
        {
        case '"':
            in_string = TRUE;
            g_string_append_c (out, c);
            break;
        case '{':
        case '[':
            g_string_append_c (out, c);
            {
                const char *q = p + 1;
                while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
                    q++;
                if (*q == '}' || *q == ']')
                    break;
            }
            depth++;
            g_string_append_c (out, '\n');
            append_indent (out, depth, indent);
            break;
        case '}':
        case ']':
            if (depth > 0 && out->len > 0
                && (out->str[out->len - 1] != '{' && out->str[out->len - 1] != '['))
            {
                depth--;
                g_string_append_c (out, '\n');
                append_indent (out, depth, indent);
            }
            g_string_append_c (out, c);
            break;
        case ',':
            g_string_append_c (out, c);
            g_string_append_c (out, '\n');
            append_indent (out, depth, indent);
            break;
        case ':':
            g_string_append_c (out, c);
            g_string_append_c (out, ' ');
            break;
        case ' ':
        case '\t':
        case '\n':
        case '\r':
            break;
        default:
            g_string_append_c (out, c);
        }
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
