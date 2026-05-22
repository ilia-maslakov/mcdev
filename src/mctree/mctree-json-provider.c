/*
   JSON provider for mctree.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.
 */

#include <config.h>

#include <string.h>

#ifdef HAVE_MCTREE_JSON_GLIB
#include <json-glib/json-glib.h>
#endif

#include "src/mctree/mctree-providers.h"

#ifdef HAVE_MCTREE_JSON_GLIB
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_probe (const unsigned char *data, gsize len)
{
    JsonParser *parser;
    GError *error = NULL;
    gboolean ok;

    parser = json_parser_new ();
    ok = json_parser_load_from_data (parser, (const char *) data, len, &error);
    g_clear_error (&error);
    g_object_unref (parser);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static void mctree_json_add_node (mctree_model_t *model, mctree_node_t *parent, const char *key,
                                  JsonNode *node);

static void
mctree_json_add_object (mctree_model_t *model, mctree_node_t *parent, JsonObject *object)
{
    GList *members, *iter;

    members = json_object_get_members (object);
    for (iter = members; iter != NULL; iter = g_list_next (iter))
    {
        const char *member = (const char *) iter->data;

        mctree_json_add_node (model, parent, member, json_object_get_member (object, member));
    }
    g_list_free (members);
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_json_add_array (mctree_model_t *model, mctree_node_t *parent, JsonArray *array)
{
    guint i, len;

    len = json_array_get_length (array);
    for (i = 0; i < len; i++)
    {
        char *key;

        key = g_strdup_printf ("[%u]", i);
        mctree_json_add_node (model, parent, key, json_array_get_element (array, i));
        g_free (key);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_json_add_node (mctree_model_t *model, mctree_node_t *parent, const char *key, JsonNode *node)
{
    JsonNodeType node_type;

    node_type = json_node_get_node_type (node);
    if (node_type == JSON_NODE_OBJECT)
    {
        mctree_node_t *field;
        mctree_node_t *object_node;

        field = key != NULL ? mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, NULL)
                            : parent;
        object_node = mctree_model_add_node (model, field, MCTREE_NODE_OBJECT, NULL, NULL);
        mctree_json_add_object (model, object_node, json_node_get_object (node));
    }
    else if (node_type == JSON_NODE_ARRAY)
    {
        mctree_node_t *field;
        mctree_node_t *array_node;

        field = key != NULL ? mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, NULL)
                            : parent;
        array_node = mctree_model_add_node (model, field, MCTREE_NODE_ARRAY, NULL, NULL);
        mctree_json_add_array (model, array_node, json_node_get_array (node));
    }
    else
    {
        char *value;

        /* Render string scalars as their raw text (json_to_string would wrap
           them in quotes), matching the built-in parser's output. */
        if (json_node_get_node_type (node) == JSON_NODE_VALUE
            && json_node_get_value_type (node) == G_TYPE_STRING)
            value = g_strdup (json_node_get_string (node));
        else
            value = json_to_string (node, FALSE);
        if (key != NULL && g_str_has_prefix (key, "["))
            mctree_model_add_node (model, parent, MCTREE_NODE_ITEM, key, value);
        else if (key != NULL)
            mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, value);
        else
            mctree_model_add_node (model, parent, MCTREE_NODE_SCALAR, NULL, value);
        g_free (value);
    }
}

/* --------------------------------------------------------------------------------------------- */

static mctree_model_t *
mctree_json_parse (const unsigned char *data, gsize len, const mctree_resolver_config_t *config,
                   GError **error)
{
    JsonParser *parser;
    JsonNode *root_node;
    mctree_model_t *model;
    mctree_node_t *root;

    parser = json_parser_new ();
    if (!json_parser_load_from_data (parser, (const char *) data, len, error))
    {
        g_object_unref (parser);
        return NULL;
    }

    model = mctree_model_new (config->scalar_preview_limit);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "JSON", NULL);
    root_node = json_parser_get_root (parser);
    mctree_json_add_node (model, root, NULL, root_node);

    g_object_unref (parser);
    return model;
}
#else
/*** file scope type declarations ****************************************************************/

typedef struct
{
    const char *data;
    gsize len;
    gsize pos;
    GError **error;
} mctree_json_parser_t;

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
mctree_json_skip_ws (mctree_json_parser_t *parser)
{
    while (parser->pos < parser->len)
    {
        char c = parser->data[parser->pos];

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
                 _ ("JSON parse error at byte %lu: %s"), (unsigned long) parser->pos, message);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean mctree_json_parse_value (mctree_json_parser_t *parser, mctree_model_t *model,
                                         mctree_node_t *parent, const char *key);

/* --------------------------------------------------------------------------------------------- */

static int
mctree_json_hex_value (char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_read_hex4 (mctree_json_parser_t *parser, gunichar *codepoint)
{
    gunichar value = 0;
    int i;

    if (parser->pos + 4 > parser->len)
        return FALSE;

    for (i = 0; i < 4; i++)
    {
        int digit = mctree_json_hex_value (parser->data[parser->pos + i]);

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
    int len;

    if (!mctree_json_read_hex4 (parser, &codepoint))
    {
        mctree_json_fail (parser, _ ("invalid unicode escape"));
        return FALSE;
    }

    if (codepoint >= 0xd800 && codepoint <= 0xdbff)
    {
        gunichar low;

        if (parser->pos + 6 > parser->len || parser->data[parser->pos] != '\\'
            || parser->data[parser->pos + 1] != 'u')
        {
            mctree_json_fail (parser, _ ("unfinished unicode surrogate pair"));
            return FALSE;
        }

        parser->pos += 2;
        if (!mctree_json_read_hex4 (parser, &low) || low < 0xdc00 || low > 0xdfff)
        {
            mctree_json_fail (parser, _ ("invalid unicode surrogate pair"));
            return FALSE;
        }

        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
    }
    else if (codepoint >= 0xdc00 && codepoint <= 0xdfff)
    {
        mctree_json_fail (parser, _ ("invalid unicode surrogate pair"));
        return FALSE;
    }

    if (codepoint == 0)
    {
        mctree_json_fail (parser, _ ("null character in string"));
        return FALSE;
    }

    if (!g_unichar_validate (codepoint))
    {
        mctree_json_fail (parser, _ ("invalid unicode codepoint"));
        return FALSE;
    }

    len = g_unichar_to_utf8 (codepoint, utf8);
    g_string_append_len (str, utf8, len);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_json_parse_string_value (mctree_json_parser_t *parser)
{
    GString *str;

    if (parser->pos >= parser->len || parser->data[parser->pos] != '"')
    {
        mctree_json_fail (parser, _ ("expected string"));
        return NULL;
    }

    parser->pos++;
    str = g_string_new ("");

    while (parser->pos < parser->len)
    {
        char c;

        c = parser->data[parser->pos++];
        if (c == '"')
            return g_string_free (str, FALSE);

        if ((unsigned char) c < 0x20)
        {
            g_string_free (str, TRUE);
            mctree_json_fail (parser, _ ("control character in string"));
            return NULL;
        }

        if (c != '\\')
        {
            g_string_append_c (str, c);
            continue;
        }

        if (parser->pos >= parser->len)
        {
            g_string_free (str, TRUE);
            mctree_json_fail (parser, _ ("unfinished escape sequence"));
            return NULL;
        }

        c = parser->data[parser->pos++];
        switch (c)
        {
        case '"':
        case '\\':
        case '/':
            g_string_append_c (str, c);
            break;
        case 'b':
            g_string_append_c (str, '\b');
            break;
        case 'f':
            g_string_append_c (str, '\f');
            break;
        case 'n':
            g_string_append_c (str, '\n');
            break;
        case 'r':
            g_string_append_c (str, '\r');
            break;
        case 't':
            g_string_append_c (str, '\t');
            break;
        case 'u':
            if (!mctree_json_append_unicode_escape (parser, str))
            {
                g_string_free (str, TRUE);
                return NULL;
            }
            break;
        default:
            g_string_free (str, TRUE);
            mctree_json_fail (parser, _ ("invalid escape sequence"));
            return NULL;
        }
    }

    g_string_free (str, TRUE);
    mctree_json_fail (parser, _ ("unterminated string"));
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_json_parse_number_value (mctree_json_parser_t *parser)
{
    gsize start = parser->pos;

    if (parser->data[parser->pos] == '-')
        parser->pos++;

    if (parser->pos >= parser->len)
        return NULL;

    if (parser->data[parser->pos] == '0')
        parser->pos++;
    else if (g_ascii_isdigit (parser->data[parser->pos]))
        while (parser->pos < parser->len && g_ascii_isdigit (parser->data[parser->pos]))
            parser->pos++;
    else
        return NULL;

    if (parser->pos < parser->len && parser->data[parser->pos] == '.')
    {
        parser->pos++;
        if (parser->pos >= parser->len || !g_ascii_isdigit (parser->data[parser->pos]))
            return NULL;
        while (parser->pos < parser->len && g_ascii_isdigit (parser->data[parser->pos]))
            parser->pos++;
    }

    if (parser->pos < parser->len
        && (parser->data[parser->pos] == 'e' || parser->data[parser->pos] == 'E'))
    {
        parser->pos++;
        if (parser->pos < parser->len
            && (parser->data[parser->pos] == '+' || parser->data[parser->pos] == '-'))
            parser->pos++;
        if (parser->pos >= parser->len || !g_ascii_isdigit (parser->data[parser->pos]))
            return NULL;
        while (parser->pos < parser->len && g_ascii_isdigit (parser->data[parser->pos]))
            parser->pos++;
    }

    return g_strndup (parser->data + start, parser->pos - start);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_parse_object (mctree_json_parser_t *parser, mctree_model_t *model,
                          mctree_node_t *object_node)
{
    mctree_json_skip_ws (parser);
    if (parser->pos < parser->len && parser->data[parser->pos] == '}')
    {
        parser->pos++;
        return TRUE;
    }

    while (parser->pos < parser->len)
    {
        char *key;

        mctree_json_skip_ws (parser);
        key = mctree_json_parse_string_value (parser);
        if (key == NULL)
            return FALSE;

        mctree_json_skip_ws (parser);
        if (parser->pos >= parser->len || parser->data[parser->pos] != ':')
        {
            g_free (key);
            return mctree_json_fail (parser, _ ("expected ':'"));
        }
        parser->pos++;

        if (!mctree_json_parse_value (parser, model, object_node, key))
        {
            g_free (key);
            return FALSE;
        }
        g_free (key);

        mctree_json_skip_ws (parser);
        if (parser->pos < parser->len && parser->data[parser->pos] == ',')
        {
            parser->pos++;
            continue;
        }
        if (parser->pos < parser->len && parser->data[parser->pos] == '}')
        {
            parser->pos++;
            return TRUE;
        }
        return mctree_json_fail (parser, _ ("expected ',' or '}'"));
    }

    return mctree_json_fail (parser, _ ("unterminated object"));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_parse_array (mctree_json_parser_t *parser, mctree_model_t *model,
                         mctree_node_t *array_node)
{
    guint index = 0;

    mctree_json_skip_ws (parser);
    if (parser->pos < parser->len && parser->data[parser->pos] == ']')
    {
        parser->pos++;
        return TRUE;
    }

    while (parser->pos < parser->len)
    {
        char *key;

        key = g_strdup_printf ("[%u]", index++);
        if (!mctree_json_parse_value (parser, model, array_node, key))
        {
            g_free (key);
            return FALSE;
        }
        g_free (key);

        mctree_json_skip_ws (parser);
        if (parser->pos < parser->len && parser->data[parser->pos] == ',')
        {
            parser->pos++;
            continue;
        }
        if (parser->pos < parser->len && parser->data[parser->pos] == ']')
        {
            parser->pos++;
            return TRUE;
        }
        return mctree_json_fail (parser, _ ("expected ',' or ']'"));
    }

    return mctree_json_fail (parser, _ ("unterminated array"));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_add_scalar (mctree_model_t *model, mctree_node_t *parent, const char *key,
                        const char *value)
{
    if (key != NULL && g_str_has_prefix (key, "["))
        mctree_model_add_node (model, parent, MCTREE_NODE_ITEM, key, value);
    else if (key != NULL)
        mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, value);
    else
        mctree_model_add_node (model, parent, MCTREE_NODE_SCALAR, NULL, value);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_json_parse_value (mctree_json_parser_t *parser, mctree_model_t *model, mctree_node_t *parent,
                         const char *key)
{
    mctree_node_t *container_parent;
    mctree_node_t *container;
    char *value;

    mctree_json_skip_ws (parser);
    if (parser->pos >= parser->len)
        return mctree_json_fail (parser, _ ("expected value"));

    switch (parser->data[parser->pos])
    {
    case '{':
        parser->pos++;
        container_parent = key != NULL
            ? mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, NULL)
            : parent;
        container = mctree_model_add_node (model, container_parent, MCTREE_NODE_OBJECT, NULL, NULL);
        return mctree_json_parse_object (parser, model, container);
    case '[':
        parser->pos++;
        container_parent = key != NULL
            ? mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, NULL)
            : parent;
        container = mctree_model_add_node (model, container_parent, MCTREE_NODE_ARRAY, NULL, NULL);
        return mctree_json_parse_array (parser, model, container);
    case '"':
        value = mctree_json_parse_string_value (parser);
        if (value == NULL)
            return FALSE;
        mctree_json_add_scalar (model, parent, key, value);
        g_free (value);
        return TRUE;
    case 't':
        if (parser->pos + 4 <= parser->len && strncmp (parser->data + parser->pos, "true", 4) == 0)
        {
            parser->pos += 4;
            return mctree_json_add_scalar (model, parent, key, "true");
        }
        break;
    case 'f':
        if (parser->pos + 5 <= parser->len && strncmp (parser->data + parser->pos, "false", 5) == 0)
        {
            parser->pos += 5;
            return mctree_json_add_scalar (model, parent, key, "false");
        }
        break;
    case 'n':
        if (parser->pos + 4 <= parser->len && strncmp (parser->data + parser->pos, "null", 4) == 0)
        {
            parser->pos += 4;
            return mctree_json_add_scalar (model, parent, key, "null");
        }
        break;
    default:
        if (parser->data[parser->pos] == '-' || g_ascii_isdigit (parser->data[parser->pos]))
        {
            value = mctree_json_parse_number_value (parser);
            if (value == NULL)
                return mctree_json_fail (parser, _ ("invalid number"));
            mctree_json_add_scalar (model, parent, key, value);
            g_free (value);
            return TRUE;
        }
        break;
    }

    return mctree_json_fail (parser, _ ("invalid value"));
}

/* --------------------------------------------------------------------------------------------- */

static mctree_model_t *
mctree_json_parse (const unsigned char *data, gsize len, const mctree_resolver_config_t *config,
                   GError **error)
{
    mctree_json_parser_t parser;
    mctree_model_t *model;
    mctree_node_t *root;

    parser.data = (const char *) data;
    parser.len = len;
    parser.pos = 0;
    parser.error = error;

    model = mctree_model_new (config->scalar_preview_limit);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "JSON", NULL);

    if (!mctree_json_parse_value (&parser, model, root, NULL))
    {
        mctree_model_free (model);
        return NULL;
    }

    mctree_json_skip_ws (&parser);
    if (parser.pos != parser.len)
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
#endif

/*** public variables ****************************************************************************/

const mctree_provider_t mctree_json_provider = {
    .content_type = MCTREE_CONTENT_JSON,
    .name = "json-glib",
#ifdef HAVE_MCTREE_JSON_GLIB
    .state = MCTREE_PROVIDER_ENABLED,
    .enabled = TRUE,
    .probe = mctree_json_probe,
    .parse = mctree_json_parse,
#else
    .state = MCTREE_PROVIDER_ENABLED,
    .enabled = TRUE,
    .probe = mctree_json_probe,
    .parse = mctree_json_parse,
#endif
};
