/*
   XML provider for mctree: a non-validating well-formedness parser.

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

   The parser is organized in three layers, mirroring the JSON provider:
     - a micro-lexer over the input buffer (peek / consume / skip_ws);
     - token parsers producing C strings (name, attribute value, text), which
       set the error exactly once at the failure position;
     - grammar rules (element, content, document) attaching nodes to the model.
   Every parse function returns TRUE on success and FALSE with the GError
   set; the caller never sets an error on behalf of a callee.

   Scope is what the tree view needs: elements, attributes, text, CDATA,
   comments, processing instructions and character/predefined entities.
   There is no DTD processing, so an undefined named entity is kept verbatim
   rather than rejected - a viewer should still show the document.
 */

#include <config.h>

#include <string.h>

#include "src/mctree/mctree-providers.h"

/*** file scope macro definitions ****************************************************************/

#define MCTREE_XML_ROOT_LABEL "XML"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    const char *data;
    gsize len;
    gsize pos;
    gsize depth;
    gsize max_depth;
    GError **error;
} mctree_xml_parser_t;

/*** forward declarations (file scope functions) *************************************************/

static gboolean mctree_xml_parse_element (mctree_xml_parser_t *parser, mctree_model_t *model,
                                          mctree_node_t *parent);

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/* Micro-lexer. */

static gboolean
mctree_xml_at_end (const mctree_xml_parser_t *parser)
{
    return parser->pos >= parser->len;
}

/* --------------------------------------------------------------------------------------------- */

static char
mctree_xml_peek (const mctree_xml_parser_t *parser)
{
    return mctree_xml_at_end (parser) ? '\0' : parser->data[parser->pos];
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_looking_at (const mctree_xml_parser_t *parser, const char *literal)
{
    const gsize n = strlen (literal);

    return parser->len - parser->pos >= n && memcmp (parser->data + parser->pos, literal, n) == 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_consume (mctree_xml_parser_t *parser, const char *literal)
{
    if (!mctree_xml_looking_at (parser, literal))
        return FALSE;

    parser->pos += strlen (literal);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_is_space (char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_xml_skip_ws (mctree_xml_parser_t *parser)
{
    while (!mctree_xml_at_end (parser) && mctree_xml_is_space (parser->data[parser->pos]))
        parser->pos++;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_fail (mctree_xml_parser_t *parser, const char *message)
{
    g_set_error (parser->error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _ ("XML parse error at byte %" G_GSIZE_FORMAT ": %s"), parser->pos, message);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/* Token parsers. */

/* Name characters per XML 1.0; every byte >= 0x80 is accepted so that UTF-8
 * names pass through without decoding each codepoint. */

static gboolean
mctree_xml_is_name_start (char c)
{
    return g_ascii_isalpha (c) || c == '_' || c == ':' || (unsigned char) c >= 0x80;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_is_name_char (char c)
{
    return mctree_xml_is_name_start (c) || g_ascii_isdigit (c) || c == '-' || c == '.';
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_parse_name (mctree_xml_parser_t *parser, char **result)
{
    const gsize start = parser->pos;

    *result = NULL;

    if (!mctree_xml_is_name_start (mctree_xml_peek (parser)))
        return mctree_xml_fail (parser, _ ("expected a name"));

    while (!mctree_xml_at_end (parser) && mctree_xml_is_name_char (parser->data[parser->pos]))
        parser->pos++;

    *result = g_strndup (parser->data + start, parser->pos - start);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Append a character reference (&#12; or &#x1f600;).  The caller has already
 * consumed "&#"; on any malformation the whole reference is kept verbatim so
 * that odd-but-readable documents still render. */

static void
mctree_xml_append_char_ref (mctree_xml_parser_t *parser, GString *str)
{
    const gsize start = parser->pos;
    gunichar code = 0;
    gboolean hex;
    gboolean digits = FALSE;
    char utf8[6];

    hex = mctree_xml_consume (parser, "x") || mctree_xml_consume (parser, "X");

    while (!mctree_xml_at_end (parser))
    {
        const char c = parser->data[parser->pos];
        int digit;

        digit = hex ? g_ascii_xdigit_value (c) : (g_ascii_isdigit (c) ? c - '0' : -1);
        if (digit < 0)
            break;

        // clamp instead of overflowing; the value is rejected below anyway
        if (code <= 0x10ffff)
            code = code * (hex ? 16 : 10) + (gunichar) digit;
        digits = TRUE;
        parser->pos++;
    }

    if (digits && code != 0 && g_unichar_validate (code) && mctree_xml_consume (parser, ";"))
    {
        g_string_append_len (str, utf8, g_unichar_to_utf8 (code, utf8));
        return;
    }

    parser->pos = start;
    g_string_append (str, "&#");
}

/* --------------------------------------------------------------------------------------------- */

/* Expand one reference starting at '&'.  Only the five predefined entities and
 * character references are known; anything else stays literal (no DTD here). */

static void
mctree_xml_append_reference (mctree_xml_parser_t *parser, GString *str)
{
    static const struct
    {
        const char *name;
        char value;
    } predefined[] = {
        { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' }, { "quot;", '"' }, { "apos;", '\'' },
    };
    gsize i;

    parser->pos++;  // '&'

    if (mctree_xml_consume (parser, "#"))
    {
        mctree_xml_append_char_ref (parser, str);
        return;
    }

    for (i = 0; i < G_N_ELEMENTS (predefined); i++)
        if (mctree_xml_consume (parser, predefined[i].name))
        {
            g_string_append_c (str, predefined[i].value);
            return;
        }

    g_string_append_c (str, '&');
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_parse_attr_value (mctree_xml_parser_t *parser, char **result)
{
    GString *str;
    char quote;

    *result = NULL;

    quote = mctree_xml_peek (parser);
    if (quote != '"' && quote != '\'')
        return mctree_xml_fail (parser, _ ("expected a quoted attribute value"));
    parser->pos++;

    str = g_string_new ("");

    while (!mctree_xml_at_end (parser))
    {
        const char c = parser->data[parser->pos];

        if (c == quote)
        {
            parser->pos++;
            *result = g_string_free (str, FALSE);
            return TRUE;
        }

        if (c == '<')
        {
            g_string_free (str, TRUE);
            return mctree_xml_fail (parser, _ ("'<' is not allowed in an attribute value"));
        }

        if (c == '&')
            mctree_xml_append_reference (parser, str);
        else
        {
            // attribute-value normalization: literal newlines and tabs become spaces
            g_string_append_c (str, mctree_xml_is_space (c) ? ' ' : c);
            parser->pos++;
        }
    }

    g_string_free (str, TRUE);
    return mctree_xml_fail (parser, _ ("unterminated attribute value"));
}

/* --------------------------------------------------------------------------------------------- */
/* Misc constructs that carry no tree content. */

static gboolean
mctree_xml_skip_comment (mctree_xml_parser_t *parser)
{
    const char *end;

    // "<!--" already consumed
    end = g_strstr_len (parser->data + parser->pos, (gssize) (parser->len - parser->pos), "-->");
    if (end == NULL)
        return mctree_xml_fail (parser, _ ("unterminated comment"));

    parser->pos = (gsize) (end - parser->data) + 3;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_skip_pi (mctree_xml_parser_t *parser)
{
    const char *end;

    // "<?" already consumed
    end = g_strstr_len (parser->data + parser->pos, (gssize) (parser->len - parser->pos), "?>");
    if (end == NULL)
        return mctree_xml_fail (parser, _ ("unterminated processing instruction"));

    parser->pos = (gsize) (end - parser->data) + 2;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Skip a DOCTYPE declaration, including an internal subset in brackets. */

static gboolean
mctree_xml_skip_doctype (mctree_xml_parser_t *parser)
{
    // "<!DOCTYPE" already consumed
    while (!mctree_xml_at_end (parser))
    {
        const char c = parser->data[parser->pos];

        if (c == '>')
        {
            parser->pos++;
            return TRUE;
        }

        if (c == '[')
        {
            const char *end;

            end = g_strstr_len (parser->data + parser->pos, (gssize) (parser->len - parser->pos),
                                "]");
            if (end == NULL)
                return mctree_xml_fail (parser, _ ("unterminated DOCTYPE subset"));
            parser->pos = (gsize) (end - parser->data) + 1;
            continue;
        }

        parser->pos++;
    }

    return mctree_xml_fail (parser, _ ("unterminated DOCTYPE"));
}

/* --------------------------------------------------------------------------------------------- */
/* Grammar rules. */

/* Emit a TEXT node unless the collected run is blank, and reset the buffer.
 * Blank runs are indentation between elements and would bury the tree. */

static void
mctree_xml_flush_text (mctree_model_t *model, mctree_node_t *parent, GString *text)
{
    char *stripped;

    if (text->len == 0)
        return;

    stripped = g_strstrip (g_strdup (text->str));
    if (stripped[0] != '\0')
        mctree_model_add_node (model, parent, MCTREE_NODE_TEXT, NULL, stripped);
    g_free (stripped);

    g_string_truncate (text, 0);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_parse_cdata (mctree_xml_parser_t *parser, mctree_model_t *model, mctree_node_t *parent)
{
    const char *end;
    const gsize start = parser->pos;
    char *stripped;

    // "<![CDATA[" already consumed
    end = g_strstr_len (parser->data + parser->pos, (gssize) (parser->len - parser->pos), "]]>");
    if (end == NULL)
        return mctree_xml_fail (parser, _ ("unterminated CDATA section"));

    stripped = g_strndup (parser->data + start, (gsize) (end - parser->data) - start);
    stripped = g_strstrip (stripped);
    if (stripped[0] != '\0')
        mctree_model_add_node (model, parent, MCTREE_NODE_TEXT, NULL, stripped);
    g_free (stripped);

    parser->pos = (gsize) (end - parser->data) + 3;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_parse_attributes (mctree_xml_parser_t *parser, mctree_model_t *model,
                             mctree_node_t *element)
{
    while (TRUE)
    {
        char *name;
        char *value;

        mctree_xml_skip_ws (parser);

        if (mctree_xml_peek (parser) == '>' || mctree_xml_looking_at (parser, "/>"))
            return TRUE;

        if (!mctree_xml_parse_name (parser, &name))
            return FALSE;

        mctree_xml_skip_ws (parser);
        if (!mctree_xml_consume (parser, "="))
        {
            g_free (name);
            return mctree_xml_fail (parser, _ ("expected '=' after attribute name"));
        }

        mctree_xml_skip_ws (parser);
        if (!mctree_xml_parse_attr_value (parser, &value))
        {
            g_free (name);
            return FALSE;
        }

        mctree_model_add_node (model, element, MCTREE_NODE_ATTRIBUTE, name, value);
        g_free (value);
        g_free (name);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Parse element content up to and including the matching end tag. */

static gboolean
mctree_xml_parse_content (mctree_xml_parser_t *parser, mctree_model_t *model,
                          mctree_node_t *element, const char *name)
{
    GString *text;
    gboolean ok = TRUE;

    text = g_string_new ("");

    while (ok)
    {
        if (mctree_xml_at_end (parser))
        {
            ok = mctree_xml_fail (parser, _ ("unterminated element"));
            break;
        }

        if (parser->data[parser->pos] != '<')
        {
            if (parser->data[parser->pos] == '&')
                mctree_xml_append_reference (parser, text);
            else
                g_string_append_c (text, parser->data[parser->pos++]);
            continue;
        }

        if (mctree_xml_consume (parser, "</"))
        {
            char *close;

            mctree_xml_flush_text (model, element, text);

            if (!mctree_xml_parse_name (parser, &close))
            {
                ok = FALSE;
                break;
            }

            if (strcmp (close, name) != 0)
            {
                g_free (close);
                ok = mctree_xml_fail (parser, _ ("mismatched end tag"));
                break;
            }
            g_free (close);

            mctree_xml_skip_ws (parser);
            if (!mctree_xml_consume (parser, ">"))
                ok = mctree_xml_fail (parser, _ ("expected '>' to close an end tag"));
            break;
        }

        mctree_xml_flush_text (model, element, text);

        if (mctree_xml_consume (parser, "<![CDATA["))
            ok = mctree_xml_parse_cdata (parser, model, element);
        else if (mctree_xml_consume (parser, "<!--"))
            ok = mctree_xml_skip_comment (parser);
        else if (mctree_xml_consume (parser, "<?"))
            ok = mctree_xml_skip_pi (parser);
        else
            ok = mctree_xml_parse_element (parser, model, element);
    }

    g_string_free (text, TRUE);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_parse_element (mctree_xml_parser_t *parser, mctree_model_t *model, mctree_node_t *parent)
{
    char *name;
    mctree_node_t *element;
    gboolean ok;

    if (!mctree_xml_consume (parser, "<"))
        return mctree_xml_fail (parser, _ ("expected '<'"));

    if (++parser->depth > parser->max_depth)
    {
        parser->depth--;
        return mctree_xml_fail (parser, _ ("nesting is too deep"));
    }

    if (!mctree_xml_parse_name (parser, &name))
    {
        parser->depth--;
        return FALSE;
    }

    element = mctree_model_add_node (model, parent, MCTREE_NODE_ELEMENT, name, NULL);

    ok = mctree_xml_parse_attributes (parser, model, element);

    if (ok)
    {
        if (mctree_xml_consume (parser, "/>"))
            ;
        else if (!mctree_xml_consume (parser, ">"))
            ok = mctree_xml_fail (parser, _ ("expected '>' or '/>'"));
        else
            ok = mctree_xml_parse_content (parser, model, element, name);
    }

    g_free (name);
    parser->depth--;
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

/* Prolog and trailing miscellany: whitespace, comments and PIs, plus a single
 * DOCTYPE before the root element. */

static gboolean
mctree_xml_skip_misc (mctree_xml_parser_t *parser, gboolean allow_doctype)
{
    while (TRUE)
    {
        mctree_xml_skip_ws (parser);

        if (mctree_xml_consume (parser, "<!--"))
        {
            if (!mctree_xml_skip_comment (parser))
                return FALSE;
        }
        else if (mctree_xml_consume (parser, "<?"))
        {
            if (!mctree_xml_skip_pi (parser))
                return FALSE;
        }
        else if (allow_doctype && mctree_xml_consume (parser, "<!DOCTYPE"))
        {
            if (!mctree_xml_skip_doctype (parser))
                return FALSE;
        }
        else
            return TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */
/* Provider entry points. */

/* libxml2 transcoded declared encodings; keep that working for the common
 * non-UTF-8 files by converting up front.  Returns NULL when the input is
 * already usable as-is. */

static char *
mctree_xml_to_utf8 (const unsigned char *data, gsize len, gsize *out_len)
{
    static const char *const charsets[] = { "UTF-16", "ISO-8859-1", NULL };
    char *declared = NULL;
    char *converted;
    gsize written;
    int i;

    if (g_utf8_validate ((const char *) data, (gssize) len, NULL))
        return NULL;

    // pick the encoding out of the XML declaration when it is ASCII-readable
    {
        const char *head = (const char *) data;
        const gsize scan = MIN (len, (gsize) 200);
        const char *enc;

        enc = g_strstr_len (head, (gssize) scan, "encoding=");
        if (enc != NULL)
        {
            const char quote = enc[9];
            const char *start = enc + 10;
            const char *end;

            if (quote == '"' || quote == '\'')
            {
                end = memchr (start, quote, (gsize) (head + scan - start));
                if (end != NULL)
                    declared = g_strndup (start, (gsize) (end - start));
            }
        }
    }

    if (declared != NULL)
    {
        converted =
            g_convert ((const char *) data, (gssize) len, "UTF-8", declared, NULL, &written, NULL);
        g_free (declared);
        if (converted != NULL)
        {
            *out_len = written;
            return converted;
        }
    }

    for (i = 0; charsets[i] != NULL; i++)
    {
        converted = g_convert ((const char *) data, (gssize) len, "UTF-8", charsets[i], NULL,
                               &written, NULL);
        if (converted != NULL)
        {
            *out_len = written;
            return converted;
        }
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mctree_model_t *
mctree_xml_parse (const unsigned char *data, gsize len, const mctree_resolver_config_t *config,
                  GError **error)
{
    mctree_xml_parser_t parser = {
        .data = (const char *) data,
        .len = len,
        .max_depth = config->max_depth,
        .error = error,
    };
    mctree_model_t *model;
    mctree_node_t *root;
    char *converted = NULL;

    if (len >= 3 && memcmp (data, "\xef\xbb\xbf", 3) == 0)
        parser.pos = 3;

    converted = mctree_xml_to_utf8 (data + parser.pos, len - parser.pos, &parser.len);
    if (converted != NULL)
    {
        parser.data = converted;
        parser.pos = 0;
    }
    else if (!g_utf8_validate (parser.data + parser.pos, (gssize) (parser.len - parser.pos), NULL))
    {
        g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_BAD_UTF8,
                     _ ("XML input is not valid UTF-8"));
        return NULL;
    }

    model = mctree_model_new (config->scalar_preview_limit);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, MCTREE_XML_ROOT_LABEL, NULL);

    if (!mctree_xml_skip_misc (&parser, TRUE) || !mctree_xml_parse_element (&parser, model, root)
        || !mctree_xml_skip_misc (&parser, FALSE))
    {
        mctree_model_free (model);
        g_free (converted);
        return NULL;
    }

    if (!mctree_xml_at_end (&parser))
    {
        mctree_xml_fail (&parser, _ ("trailing data after the root element"));
        mctree_model_free (model);
        g_free (converted);
        return NULL;
    }

    g_free (converted);
    return model;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_probe (const unsigned char *data, gsize len)
{
    mctree_resolver_config_t config;
    mctree_model_t *model;
    GError *error = NULL;

    mctree_resolver_config_init (&config);
    model = mctree_xml_parse (data, len, &config, &error);
    g_clear_error (&error);
    if (model == NULL)
        return FALSE;

    mctree_model_free (model);
    return TRUE;
}

/*** public variables ****************************************************************************/

const mctree_provider_t mctree_xml_provider = {
    .content_type = MCTREE_CONTENT_XML,
    .name = "xml",
    .state = MCTREE_PROVIDER_ENABLED,
    .probe = mctree_xml_probe,
    .parse = mctree_xml_parse,
};
