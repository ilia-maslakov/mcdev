/*
   JSON provider for mctree: a strict RFC 8259 recursive descent parser.

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

   The parser is organized in three layers:
     - a micro-lexer over the input buffer (peek / consume / skip_ws);
     - token parsers producing C strings (string, number, literal), which
       set the error exactly once at the failure position;
     - grammar rules (value, object, array) attaching nodes to the model.
   Every parse function returns TRUE on success and FALSE with the GError
   set; the caller never sets an error on behalf of a callee.
 */

#include <config.h>

#include <string.h>

#include "src/mctree/mctree-providers.h"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    const char *data;
    gsize len;
    gsize pos;
    gsize depth;
    gsize max_depth;
    GError **error;
} mctree_json_parser_t;

/*** forward declarations (file scope functions) *************************************************/

static gboolean mctree_json_parse_value (mctree_json_parser_t *parser, mctree_model_t *model,
                                         mctree_node_t *parent, const char *key,
                                         mctree_node_type_t scalar_type);

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/* Micro-lexer. */

static gboolean
mctree_json_at_end (const mctree_json_parser_t *parser)
{
    return parser->pos >= parser->len;
}

/* --------------------------------------------------------------------------------------------- */

static char
mctree_json_peek (const mctree_json_parser_t *parser)
{
    return mctree_json_at_end (parser) ? '\0' : parser->data[parser->pos];
}

/* --------------------------------------------------------------------------------------------- */

/* Advance over the expected character; FALSE if something else is next.
 * The at_end test is redundant (peek returns NUL there) and kept to make
 * the contract explicit: never advance past the end of input. */

static gboolean
mctree_json_consume (mctree_json_parser_t *parser, char expected)
{
    if (mctree_json_peek (parser) != expected || mctree_json_at_end (parser))
        return FALSE;

    parser->pos++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_json_skip_ws (mctree_json_parser_t *parser)
{
    while (!mctree_json_at_end (parser))
    {
        const char c = parser->data[parser->pos];

        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        parser->pos++;
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_fail (mctree_json_parser_t *parser, const char *message)
{
    g_set_error (parser->error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _ ("JSON parse error at byte %" G_GSIZE_FORMAT ": %s"), parser->pos, message);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/* Token parsers. */

static gboolean
mctree_json_read_hex4 (mctree_json_parser_t *parser, gunichar *codepoint)
{
    gunichar value = 0;
    int i;

    if (parser->pos + 4 > parser->len)
        return FALSE;

    for (i = 0; i < 4; i++)
    {
        const int digit = g_ascii_xdigit_value (parser->data[parser->pos + i]);

        if (digit < 0)
            return FALSE;

        value = (value << 4) | (gunichar) digit;
    }

    parser->pos += 4;
    *codepoint = value;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_append_unicode_escape (mctree_json_parser_t *parser, GString *str)
{
    gunichar codepoint;
    char utf8[6];

    if (!mctree_json_read_hex4 (parser, &codepoint))
        return mctree_json_fail (parser, _ ("invalid unicode escape"));

    if (codepoint >= 0xd800 && codepoint <= 0xdbff)
    {
        gunichar low;

        if (parser->pos + 6 > parser->len || parser->data[parser->pos] != '\\'
            || parser->data[parser->pos + 1] != 'u')
            return mctree_json_fail (parser, _ ("unfinished unicode surrogate pair"));

        parser->pos += 2;
        if (!mctree_json_read_hex4 (parser, &low) || low < 0xdc00 || low > 0xdfff)
            return mctree_json_fail (parser, _ ("invalid unicode surrogate pair"));

        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
    }
    else if (codepoint >= 0xdc00 && codepoint <= 0xdfff)
        return mctree_json_fail (parser, _ ("invalid unicode surrogate pair"));

    if (codepoint == 0)
    {
        /* Model values are C strings and cannot carry a real NUL:
           keep the escaped form visible instead of rejecting the file. */
        g_string_append (str, "\\u0000");
        return TRUE;
    }

    if (!g_unichar_validate (codepoint))
        return mctree_json_fail (parser, _ ("invalid unicode codepoint"));

    g_string_append_len (str, utf8, g_unichar_to_utf8 (codepoint, utf8));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_append_escape (mctree_json_parser_t *parser, GString *str)
{
    char c;

    if (mctree_json_at_end (parser))
        return mctree_json_fail (parser, _ ("unfinished escape sequence"));

    c = parser->data[parser->pos++];
    switch (c)
    {
    case '"':
    case '\\':
    case '/':
        g_string_append_c (str, c);
        return TRUE;
    case 'b':
        g_string_append_c (str, '\b');
        return TRUE;
    case 'f':
        g_string_append_c (str, '\f');
        return TRUE;
    case 'n':
        g_string_append_c (str, '\n');
        return TRUE;
    case 'r':
        g_string_append_c (str, '\r');
        return TRUE;
    case 't':
        g_string_append_c (str, '\t');
        return TRUE;
    case 'u':
        return mctree_json_append_unicode_escape (parser, str);
    default:
        return mctree_json_fail (parser, _ ("invalid escape sequence"));
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_parse_string (mctree_json_parser_t *parser, char **result)
{
    GString *str;

    *result = NULL;

    if (!mctree_json_consume (parser, '"'))
        return mctree_json_fail (parser, _ ("expected string"));

    str = g_string_new ("");

    while (!mctree_json_at_end (parser))
    {
        const char c = parser->data[parser->pos++];

        if (c == '"')
        {
            if (!g_utf8_validate (str->str, (gssize) str->len, NULL))
            {
                g_string_free (str, TRUE);
                return mctree_json_fail (parser, _ ("string is not valid UTF-8"));
            }

            *result = g_string_free (str, FALSE);
            return TRUE;
        }

        if ((unsigned char) c < 0x20)
        {
            g_string_free (str, TRUE);
            return mctree_json_fail (parser, _ ("control character in string"));
        }

        if (c != '\\')
            g_string_append_c (str, c);
        else if (!mctree_json_append_escape (parser, str))
        {
            g_string_free (str, TRUE);
            return FALSE;
        }
    }

    g_string_free (str, TRUE);
    return mctree_json_fail (parser, _ ("unterminated string"));
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_json_skip_digits (mctree_json_parser_t *parser)
{
    while (!mctree_json_at_end (parser) && g_ascii_isdigit (parser->data[parser->pos]))
        parser->pos++;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_parse_number (mctree_json_parser_t *parser, char **result)
{
    const gsize start = parser->pos;

    *result = NULL;

    (void) mctree_json_consume (parser, '-');

    // integer part: a lone 0, or a nonzero digit followed by any digits
    if (mctree_json_consume (parser, '0'))
        ;
    else if (g_ascii_isdigit (mctree_json_peek (parser)))
        mctree_json_skip_digits (parser);
    else
        goto malformed;

    if (mctree_json_consume (parser, '.'))
    {
        if (!g_ascii_isdigit (mctree_json_peek (parser)))
            goto malformed;
        mctree_json_skip_digits (parser);
    }

    if (mctree_json_peek (parser) == 'e' || mctree_json_peek (parser) == 'E')
    {
        parser->pos++;
        if (!mctree_json_consume (parser, '+'))
            (void) mctree_json_consume (parser, '-');
        if (!g_ascii_isdigit (mctree_json_peek (parser)))
            goto malformed;
        mctree_json_skip_digits (parser);
    }

    *result = g_strndup (parser->data + start, parser->pos - start);
    return TRUE;

malformed:
    parser->pos = start;
    return mctree_json_fail (parser, _ ("invalid number"));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_parse_literal (mctree_json_parser_t *parser, const char *literal)
{
    const gsize n = strlen (literal);

    if (parser->len - parser->pos < n || strncmp (parser->data + parser->pos, literal, n) != 0)
        return FALSE;

    parser->pos += n;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/* Grammar rules. */

static gboolean
mctree_json_parse_object (mctree_json_parser_t *parser, mctree_model_t *model,
                          mctree_node_t *object_node)
{
    mctree_json_skip_ws (parser);
    if (mctree_json_consume (parser, '}'))
        return TRUE;

    do
    {
        char *key;
        gboolean ok;

        mctree_json_skip_ws (parser);
        if (!mctree_json_parse_string (parser, &key))
            return FALSE;

        mctree_json_skip_ws (parser);
        if (!mctree_json_consume (parser, ':'))
        {
            g_free (key);
            return mctree_json_fail (parser, _ ("expected ':'"));
        }

        ok = mctree_json_parse_value (parser, model, object_node, key, MCTREE_NODE_FIELD);
        g_free (key);
        if (!ok)
            return FALSE;

        mctree_json_skip_ws (parser);
    }
    while (mctree_json_consume (parser, ','));

    if (mctree_json_consume (parser, '}'))
        return TRUE;

    return mctree_json_fail (parser,
                             mctree_json_at_end (parser) ? _ ("unterminated object")
                                                         : _ ("expected ',' or '}'"));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_parse_array (mctree_json_parser_t *parser, mctree_model_t *model,
                         mctree_node_t *array_node)
{
    guint index = 0;

    mctree_json_skip_ws (parser);
    if (mctree_json_consume (parser, ']'))
        return TRUE;

    do
    {
        char *key;
        gboolean ok;

        key = g_strdup_printf ("[%u]", index++);
        ok = mctree_json_parse_value (parser, model, array_node, key, MCTREE_NODE_ITEM);
        g_free (key);
        if (!ok)
            return FALSE;

        mctree_json_skip_ws (parser);
    }
    while (mctree_json_consume (parser, ','));

    if (mctree_json_consume (parser, ']'))
        return TRUE;

    return mctree_json_fail (
        parser, mctree_json_at_end (parser) ? _ ("unterminated array") : _ ("expected ',' or ']'"));
}

/* --------------------------------------------------------------------------------------------- */

/* Attach an object or array to the tree and parse its contents.  A keyed
 * container gets a FIELD wrapper labeled with the key ("[n]" for array
 * items), so containers look uniform in the tree regardless of context. */

static gboolean
mctree_json_parse_container (mctree_json_parser_t *parser, mctree_model_t *model,
                             mctree_node_t *parent, const char *key, gboolean is_object)
{
    mctree_node_t *wrapper;
    mctree_node_t *container;
    gboolean ok;

    if (++parser->depth > parser->max_depth)
    {
        parser->depth--;
        return mctree_json_fail (parser, _ ("nesting is too deep"));
    }

    wrapper =
        key != NULL ? mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, NULL) : parent;
    container = mctree_model_add_node (
        model, wrapper, is_object ? MCTREE_NODE_OBJECT : MCTREE_NODE_ARRAY, NULL, NULL);

    ok = is_object ? mctree_json_parse_object (parser, model, container)
                   : mctree_json_parse_array (parser, model, container);

    parser->depth--;
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

/* Parse one value and attach it under parent.  Scalars become nodes of
 * scalar_type (FIELD for object members, ITEM for array items, SCALAR for
 * a bare top-level value). */

static gboolean
mctree_json_parse_value (mctree_json_parser_t *parser, mctree_model_t *model, mctree_node_t *parent,
                         const char *key, mctree_node_type_t scalar_type)
{
    static const char *const literals[] = { "true", "false", "null", NULL };
    char *value = NULL;
    char c;
    int i;

    mctree_json_skip_ws (parser);
    if (mctree_json_at_end (parser))
        return mctree_json_fail (parser, _ ("expected value"));

    c = mctree_json_peek (parser);

    if (c == '{' || c == '[')
    {
        parser->pos++;
        return mctree_json_parse_container (parser, model, parent, key, c == '{');
    }

    if (c == '"')
    {
        if (!mctree_json_parse_string (parser, &value))
            return FALSE;
    }
    else if (c == '-' || g_ascii_isdigit (c))
    {
        if (!mctree_json_parse_number (parser, &value))
            return FALSE;
    }
    else
    {
        for (i = 0; literals[i] != NULL; i++)
            if (mctree_json_parse_literal (parser, literals[i]))
            {
                value = g_strdup (literals[i]);
                break;
            }

        if (value == NULL)
            return mctree_json_fail (parser, _ ("invalid value"));
    }

    mctree_model_add_node (model, parent, scalar_type, key, value);
    g_free (value);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/* Provider entry points. */

static mctree_model_t *
mctree_json_parse (const unsigned char *data, gsize len, const mctree_resolver_config_t *config,
                   GError **error)
{
    mctree_json_parser_t parser = {
        .data = (const char *) data,
        .len = len,
        .max_depth = config->max_depth,
        .error = error,
    };
    mctree_model_t *model;
    mctree_node_t *root;

    // RFC 8259 allows ignoring a leading UTF-8 BOM, and Windows editors add it
    if (len >= 3 && memcmp (data, "\xef\xbb\xbf", 3) == 0)
        parser.pos = 3;

    model = mctree_model_new (config->scalar_preview_limit);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "JSON", NULL);

    if (!mctree_json_parse_value (&parser, model, root, NULL, MCTREE_NODE_SCALAR))
    {
        mctree_model_free (model);
        return NULL;
    }

    mctree_json_skip_ws (&parser);
    if (!mctree_json_at_end (&parser))
    {
        mctree_json_fail (&parser, _ ("trailing data"));
        mctree_model_free (model);
        return NULL;
    }

    return model;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_probe (const unsigned char *data, gsize len)
{
    mctree_resolver_config_t config;
    mctree_model_t *model;
    GError *error = NULL;

    mctree_resolver_config_init (&config);
    model = mctree_json_parse (data, len, &config, &error);
    g_clear_error (&error);
    if (model == NULL)
        return FALSE;

    mctree_model_free (model);
    return TRUE;
}

/*** public variables ****************************************************************************/

const mctree_provider_t mctree_json_provider = {
    .content_type = MCTREE_CONTENT_JSON,
    .name = "json",
    .state = MCTREE_PROVIDER_ENABLED,
    .probe = mctree_json_probe,
    .parse = mctree_json_parse,
};
