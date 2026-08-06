/*
   JSON pretty-printer used by the SQLite panel plugin.

   Copyright (C) 2026
   Free Software Foundation, Inc.

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

#include "sqlite_json.h"

/*** file scope macro definitions ****************************************************************/

#define SQLITE_JSON_MAX_DEPTH 64

/*** file scope type declarations ****************************************************************/

typedef struct
{
    const char *cur;
    const char *end;
    guint depth;
} sqlite_json_parser_t;

/*** forward declarations (file scope functions) *************************************************/

static gboolean sqlite_json_parse_value (sqlite_json_parser_t *parser, GString *out, int indent);

/*** file scope functions ************************************************************************/

static void
sqlite_json_append_indent (GString *out, int indent)
{
    while (indent-- > 0)
        g_string_append_c (out, ' ');
}

/* --------------------------------------------------------------------------------------------- */

static void
sqlite_json_skip_ws (sqlite_json_parser_t *parser)
{
    while (parser->cur < parser->end
           && (*parser->cur == ' ' || *parser->cur == '\t' || *parser->cur == '\r'
               || *parser->cur == '\n'))
        parser->cur++;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_json_consume (sqlite_json_parser_t *parser, char expected)
{
    if (parser->cur >= parser->end || *parser->cur != expected)
        return FALSE;
    parser->cur++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_json_is_hex (char c)
{
    return g_ascii_isxdigit ((guchar) c);
}

/* --------------------------------------------------------------------------------------------- */

/* Validate one JSON string and append its original representation unchanged. */
static gboolean
sqlite_json_append_string (sqlite_json_parser_t *parser, GString *out)
{
    const char *start;

    if (parser->cur >= parser->end || *parser->cur != '"')
        return FALSE;

    start = parser->cur++;
    while (parser->cur < parser->end)
    {
        unsigned char c = (unsigned char) *parser->cur++;

        if (c == '"')
        {
            g_string_append_len (out, start, (gssize) (parser->cur - start));
            return TRUE;
        }
        if (c < 0x20)
            return FALSE;
        if (c != '\\')
            continue;

        if (parser->cur >= parser->end)
            return FALSE;
        c = (unsigned char) *parser->cur++;
        if (c == 'u')
        {
            int i;

            for (i = 0; i < 4; i++)
            {
                if (parser->cur >= parser->end || !sqlite_json_is_hex (*parser->cur))
                    return FALSE;
                parser->cur++;
            }
        }
        else if (c != '"' && c != '\\' && c != '/' && c != 'b' && c != 'f' && c != 'n' && c != 'r'
                 && c != 't')
            return FALSE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_json_parse_number (sqlite_json_parser_t *parser, GString *out)
{
    const char *start = parser->cur;

    if (parser->cur < parser->end && *parser->cur == '-')
        parser->cur++;
    if (parser->cur >= parser->end)
        return FALSE;

    if (*parser->cur == '0')
        parser->cur++;
    else if (g_ascii_isdigit ((guchar) *parser->cur))
    {
        do
            parser->cur++;
        while (parser->cur < parser->end && g_ascii_isdigit ((guchar) *parser->cur));
    }
    else
        return FALSE;

    if (parser->cur < parser->end && *parser->cur == '.')
    {
        parser->cur++;
        if (parser->cur >= parser->end || !g_ascii_isdigit ((guchar) *parser->cur))
            return FALSE;
        do
            parser->cur++;
        while (parser->cur < parser->end && g_ascii_isdigit ((guchar) *parser->cur));
    }

    if (parser->cur < parser->end && (*parser->cur == 'e' || *parser->cur == 'E'))
    {
        parser->cur++;
        if (parser->cur < parser->end && (*parser->cur == '+' || *parser->cur == '-'))
            parser->cur++;
        if (parser->cur >= parser->end || !g_ascii_isdigit ((guchar) *parser->cur))
            return FALSE;
        do
            parser->cur++;
        while (parser->cur < parser->end && g_ascii_isdigit ((guchar) *parser->cur));
    }

    g_string_append_len (out, start, (gssize) (parser->cur - start));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_json_parse_literal (sqlite_json_parser_t *parser, GString *out, const char *literal)
{
    gsize length = strlen (literal);

    if ((gsize) (parser->end - parser->cur) < length || memcmp (parser->cur, literal, length) != 0)
        return FALSE;

    g_string_append_len (out, literal, (gssize) length);
    parser->cur += length;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_json_parse_object (sqlite_json_parser_t *parser, GString *out, int indent)
{
    if (!sqlite_json_consume (parser, '{'))
        return FALSE;
    sqlite_json_skip_ws (parser);
    if (sqlite_json_consume (parser, '}'))
    {
        g_string_append (out, "{}");
        return TRUE;
    }

    g_string_append (out, "{\n");
    while (TRUE)
    {
        sqlite_json_append_indent (out, indent + 2);
        if (!sqlite_json_append_string (parser, out))
            return FALSE;
        sqlite_json_skip_ws (parser);
        if (!sqlite_json_consume (parser, ':'))
            return FALSE;
        g_string_append (out, ": ");
        if (!sqlite_json_parse_value (parser, out, indent + 2))
            return FALSE;
        sqlite_json_skip_ws (parser);

        if (sqlite_json_consume (parser, '}'))
        {
            g_string_append_c (out, '\n');
            sqlite_json_append_indent (out, indent);
            g_string_append_c (out, '}');
            return TRUE;
        }
        if (!sqlite_json_consume (parser, ','))
            return FALSE;
        sqlite_json_skip_ws (parser);
        g_string_append (out, ",\n");
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_json_parse_array (sqlite_json_parser_t *parser, GString *out, int indent)
{
    if (!sqlite_json_consume (parser, '['))
        return FALSE;
    sqlite_json_skip_ws (parser);
    if (sqlite_json_consume (parser, ']'))
    {
        g_string_append (out, "[]");
        return TRUE;
    }

    g_string_append (out, "[\n");
    while (TRUE)
    {
        sqlite_json_append_indent (out, indent + 2);
        if (!sqlite_json_parse_value (parser, out, indent + 2))
            return FALSE;
        sqlite_json_skip_ws (parser);

        if (sqlite_json_consume (parser, ']'))
        {
            g_string_append_c (out, '\n');
            sqlite_json_append_indent (out, indent);
            g_string_append_c (out, ']');
            return TRUE;
        }
        if (!sqlite_json_consume (parser, ','))
            return FALSE;
        sqlite_json_skip_ws (parser);
        g_string_append (out, ",\n");
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_json_parse_value (sqlite_json_parser_t *parser, GString *out, int indent)
{
    gboolean ok;

    sqlite_json_skip_ws (parser);
    if (parser->cur >= parser->end)
        return FALSE;

    switch (*parser->cur)
    {
    case '{':
        if (++parser->depth > SQLITE_JSON_MAX_DEPTH)
            return FALSE;
        ok = sqlite_json_parse_object (parser, out, indent);
        parser->depth--;
        return ok;
    case '[':
        if (++parser->depth > SQLITE_JSON_MAX_DEPTH)
            return FALSE;
        ok = sqlite_json_parse_array (parser, out, indent);
        parser->depth--;
        return ok;
    case '"':
        return sqlite_json_append_string (parser, out);
    case 't':
        return sqlite_json_parse_literal (parser, out, "true");
    case 'f':
        return sqlite_json_parse_literal (parser, out, "false");
    case 'n':
        return sqlite_json_parse_literal (parser, out, "null");
    default:
        return sqlite_json_parse_number (parser, out);
    }
}

/* --------------------------------------------------------------------------------------------- */

char *
sqlite_json_pretty (const char *json, gsize length, int indent)
{
    sqlite_json_parser_t parser;
    GString *out;

    if (json == NULL || !g_utf8_validate (json, (gssize) length, NULL))
        return NULL;

    parser.cur = json;
    parser.end = json + length;
    parser.depth = 0;
    out = g_string_sized_new (length + 32);

    if (!sqlite_json_parse_value (&parser, out, MAX (indent, 0)))
        goto fail;
    sqlite_json_skip_ws (&parser);
    if (parser.cur != parser.end)
        goto fail;

    return g_string_free (out, FALSE);

fail:
    g_string_free (out, TRUE);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
