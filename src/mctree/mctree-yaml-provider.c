/*
   YAML provider for mctree.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.
 */

#include <config.h>

#include <string.h>

#include "src/mctree/mctree-providers.h"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    int indent;
    mctree_node_t *node;
    gboolean pending_field;
    char *pending_anchor;
} mctree_yaml_stack_entry_t;

typedef struct
{
    char **lines;
    gsize line_count;
    GHashTable *anchors; /* name -> mctree_node_t* */
    GError **error;
} mctree_yaml_parser_t;

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
mctree_yaml_stack_entry_free (mctree_yaml_stack_entry_t *entry)
{
    if (entry == NULL)
        return;

    g_free (entry->pending_anchor);
    g_free (entry);
}

/* --------------------------------------------------------------------------------------------- */

static int
mctree_yaml_indent (const char *line)
{
    int indent = 0;

    while (line[indent] == ' ')
        indent++;

    return indent;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_yaml_strip_comment (const char *text)
{
    gboolean in_single = FALSE;
    gboolean in_double = FALSE;
    gsize i;

    for (i = 0; text[i] != '\0'; i++)
    {
        if (text[i] == '\'' && !in_double)
            in_single = !in_single;
        else if (text[i] == '"' && !in_single)
            in_double = !in_double;
        else if (text[i] == '#' && !in_single && !in_double
                 && (i == 0 || g_ascii_isspace (text[i - 1])))
            return g_strndup (text, i);
    }

    return g_strdup (text);
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_yaml_clean_line (const char *line)
{
    char *without_comment;
    char *clean;

    without_comment = mctree_yaml_strip_comment (line);
    clean = g_strchomp (without_comment);
    return clean;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_yaml_chomp_line (const char *line)
{
    return g_strchomp (g_strdup (line));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_is_ignored_line (const char *trimmed)
{
    return trimmed[0] == '\0' || strcmp (trimmed, "---") == 0 || strcmp (trimmed, "...") == 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_fail (mctree_yaml_parser_t *parser, gsize line_index, const char *message)
{
    g_set_error (parser->error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _ ("YAML parse error at line %lu: %s"), (unsigned long) (line_index + 1), message);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_yaml_unquote (const char *value)
{
    gsize len;

    if (value == NULL)
        return NULL;

    len = strlen (value);
    if (len >= 2
        && ((value[0] == '"' && value[len - 1] == '"')
            || (value[0] == '\'' && value[len - 1] == '\'')))
        return g_strndup (value + 1, len - 2);

    return g_strdup (value);
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_yaml_take_anchor (char **value)
{
    char *p;
    char *start;
    char *anchor;

    if (value == NULL || *value == NULL)
        return NULL;

    p = g_strstrip (*value);
    if (*p != '&')
        return NULL;

    start = ++p;
    while (*p != '\0' && !g_ascii_isspace (*p))
        p++;

    anchor = g_strndup (start, (gsize) (p - start));
    while (g_ascii_isspace (*p))
        p++;

    memmove (*value, p, strlen (p) + 1);
    return anchor;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_split_key_value (char *text, char **key, char **value)
{
    gboolean in_single = FALSE;
    gboolean in_double = FALSE;
    char *p;

    for (p = text; *p != '\0'; p++)
    {
        if (*p == '\'' && !in_double)
            in_single = !in_single;
        else if (*p == '"' && !in_single)
            in_double = !in_double;
        else if (*p == ':' && !in_single && !in_double && (p[1] == '\0' || g_ascii_isspace (p[1])))
        {
            *p = '\0';
            *key = g_strstrip (text);
            *value = g_strstrip (p + 1);
            return **key != '\0';
        }
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static mctree_yaml_stack_entry_t *
mctree_yaml_stack_top (GPtrArray *stack)
{
    return stack->len == 0
        ? NULL
        : (mctree_yaml_stack_entry_t *) g_ptr_array_index (stack, stack->len - 1);
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_yaml_stack_push (GPtrArray *stack, int indent, mctree_node_t *node, gboolean pending_field,
                        char *pending_anchor)
{
    mctree_yaml_stack_entry_t *entry;

    entry = g_new0 (mctree_yaml_stack_entry_t, 1);
    entry->indent = indent;
    entry->node = node;
    entry->pending_field = pending_field;
    entry->pending_anchor = pending_anchor;
    g_ptr_array_add (stack, entry);
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_yaml_stack_pop_to_parent (GPtrArray *stack, int indent)
{
    while (stack->len > 1)
    {
        mctree_yaml_stack_entry_t *entry = mctree_yaml_stack_top (stack);

        if (entry->indent < indent)
            break;

        g_ptr_array_remove_index (stack, stack->len - 1);
    }
}

/* --------------------------------------------------------------------------------------------- */

static mctree_node_t *mctree_yaml_clone_node (mctree_model_t *model, mctree_node_t *parent,
                                              const mctree_node_t *source);

static void
mctree_yaml_clone_children (mctree_model_t *model, mctree_node_t *target,
                            const mctree_node_t *source)
{
    guint i;

    for (i = 0; source->children != NULL && i < source->children->len; i++)
        mctree_yaml_clone_node (model, target, g_ptr_array_index (source->children, i));
}

/* --------------------------------------------------------------------------------------------- */

static mctree_node_t *
mctree_yaml_clone_node (mctree_model_t *model, mctree_node_t *parent, const mctree_node_t *source)
{
    mctree_node_t *copy;

    copy = mctree_model_add_node (model, parent, source->type, source->key, source->value);
    copy->expanded = source->expanded;
    mctree_yaml_clone_children (model, copy, source);

    return copy;
}

/* --------------------------------------------------------------------------------------------- */

static mctree_node_t *
mctree_yaml_ensure_container (mctree_yaml_parser_t *parser, mctree_model_t *model, GPtrArray *stack,
                              gboolean sequence)
{
    mctree_yaml_stack_entry_t *entry = mctree_yaml_stack_top (stack);

    if (entry == NULL)
        return NULL;

    if (entry->pending_field)
    {
        mctree_node_t *container;

        container = mctree_model_add_node (
            model, entry->node, sequence ? MCTREE_NODE_ARRAY : MCTREE_NODE_OBJECT, NULL, NULL);
        if (entry->pending_anchor != NULL)
        {
            g_hash_table_replace (parser->anchors, entry->pending_anchor, container);
            entry->pending_anchor = NULL;
        }

        entry->node = container;
        entry->pending_field = FALSE;
    }

    return entry->node;
}

/* --------------------------------------------------------------------------------------------- */

static guint
mctree_yaml_next_item_index (const mctree_node_t *array)
{
    return mctree_node_child_count (array);
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_yaml_collect_block (mctree_yaml_parser_t *parser, gsize *line_index, int parent_indent,
                           gboolean folded)
{
    GString *block;
    int block_indent = -1;
    gsize i;

    for (i = *line_index + 1; i < parser->line_count; i++)
    {
        char *clean = mctree_yaml_chomp_line (parser->lines[i]);
        int indent = mctree_yaml_indent (clean);
        char *trimmed = g_strstrip (g_strdup (clean));

        if (i == parser->line_count - 1 && trimmed[0] == '\0')
        {
            g_free (trimmed);
            g_free (clean);
            break;
        }

        if (trimmed[0] != '\0' && indent <= parent_indent)
        {
            g_free (trimmed);
            g_free (clean);
            break;
        }

        if (trimmed[0] != '\0' && indent > parent_indent
            && (block_indent < 0 || indent < block_indent))
            block_indent = indent;

        g_free (trimmed);
        g_free (clean);
    }

    if (block_indent < 0)
        block_indent = parent_indent + 1;

    block = g_string_new ("");

    for (i = *line_index + 1; i < parser->line_count; i++)
    {
        char *clean = mctree_yaml_chomp_line (parser->lines[i]);
        int indent = mctree_yaml_indent (clean);
        char *trimmed = g_strstrip (g_strdup (clean));
        const char *text;

        if (i == parser->line_count - 1 && trimmed[0] == '\0')
        {
            g_free (trimmed);
            g_free (clean);
            break;
        }

        if (trimmed[0] != '\0' && indent <= parent_indent)
        {
            g_free (trimmed);
            g_free (clean);
            break;
        }

        text = clean + MIN ((int) strlen (clean), block_indent);
        if (folded)
        {
            if (trimmed[0] == '\0')
                g_string_append_c (block, '\n');
            else
            {
                if (block->len > 0 && block->str[block->len - 1] != '\n')
                    g_string_append_c (block, ' ');
                g_string_append (block, g_strstrip ((char *) text));
            }
        }
        else
        {
            g_string_append (block, text);
            g_string_append_c (block, '\n');
        }

        g_free (trimmed);
        g_free (clean);
    }

    *line_index = i - 1;
    return g_string_free (block, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean mctree_yaml_parse_mapping_text (mctree_yaml_parser_t *parser, mctree_model_t *model,
                                                GPtrArray *stack, mctree_node_t *parent, char *text,
                                                int indent, gsize *line_index);

static gboolean
mctree_yaml_add_alias (mctree_yaml_parser_t *parser, mctree_model_t *model, mctree_node_t *parent,
                       const char *key, const char *alias_name)
{
    mctree_node_t *source;
    mctree_node_t *field;

    source = (mctree_node_t *) g_hash_table_lookup (parser->anchors, alias_name);
    if (source == NULL)
    {
        char *value = g_strdup_printf ("*%s", alias_name);

        mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, value);
        g_free (value);
        return TRUE;
    }

    field = mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, NULL);
    mctree_yaml_clone_node (model, field, source);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_parse_mapping_text (mctree_yaml_parser_t *parser, mctree_model_t *model,
                                GPtrArray *stack, mctree_node_t *parent, char *text, int indent,
                                gsize *line_index)
{
    char *key;
    char *value;
    char *anchor = NULL;

    if (!mctree_yaml_split_key_value (text, &key, &value))
        return mctree_yaml_fail (parser, *line_index, _ ("expected key: value"));

    anchor = mctree_yaml_take_anchor (&value);

    if (value[0] == '\0')
    {
        mctree_node_t *field;

        field = mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, NULL);
        mctree_yaml_stack_push (stack, indent, field, TRUE, anchor);
        return TRUE;
    }

    if (strcmp (value, "|") == 0 || strcmp (value, ">") == 0)
    {
        char *block;
        mctree_node_t *field;

        block = mctree_yaml_collect_block (parser, line_index, indent, strcmp (value, ">") == 0);
        field = mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, block);
        if (anchor != NULL)
            g_hash_table_replace (parser->anchors, anchor, field);
        g_free (block);
        return TRUE;
    }

    if (value[0] == '*')
    {
        gboolean ok = mctree_yaml_add_alias (parser, model, parent, key, value + 1);

        g_free (anchor);
        return ok;
    }

    {
        char *scalar = mctree_yaml_unquote (value);
        mctree_node_t *field;

        field = mctree_model_add_node (model, parent, MCTREE_NODE_FIELD, key, scalar);
        if (anchor != NULL)
            g_hash_table_replace (parser->anchors, anchor, field);
        g_free (scalar);
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_parse_sequence_text (mctree_yaml_parser_t *parser, mctree_model_t *model,
                                 GPtrArray *stack, mctree_node_t *array, char *text, int indent,
                                 gsize *line_index)
{
    char *item_key;
    mctree_node_t *item;

    item_key = g_strdup_printf ("[%u]", mctree_yaml_next_item_index (array));

    if (text[0] == '\0')
    {
        item = mctree_model_add_node (model, array, MCTREE_NODE_ITEM, item_key, NULL);
        mctree_yaml_stack_push (stack, indent, item, TRUE, NULL);
        g_free (item_key);
        return TRUE;
    }

    if (strchr (text, ':') != NULL)
    {
        mctree_node_t *object;

        item = mctree_model_add_node (model, array, MCTREE_NODE_ITEM, item_key, NULL);
        object = mctree_model_add_node (model, item, MCTREE_NODE_OBJECT, NULL, NULL);
        mctree_yaml_stack_push (stack, indent, object, FALSE, NULL);
        g_free (item_key);

        return mctree_yaml_parse_mapping_text (parser, model, stack, object, text, indent + 2,
                                               line_index);
    }

    {
        char *scalar = mctree_yaml_unquote (text);

        mctree_model_add_node (model, array, MCTREE_NODE_ITEM, item_key, scalar);
        g_free (scalar);
    }

    g_free (item_key);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_parse_lines (mctree_yaml_parser_t *parser, mctree_model_t *model, mctree_node_t *root)
{
    GPtrArray *stack;
    mctree_node_t *root_object;
    gsize i;

    root_object = mctree_model_add_node (model, root, MCTREE_NODE_OBJECT, NULL, NULL);
    stack = g_ptr_array_new_with_free_func ((GDestroyNotify) mctree_yaml_stack_entry_free);
    mctree_yaml_stack_push (stack, -1, root_object, FALSE, NULL);

    for (i = 0; i < parser->line_count; i++)
    {
        char *clean;
        char *trimmed;
        int indent;
        gboolean ok;

        clean = mctree_yaml_clean_line (parser->lines[i]);
        indent = mctree_yaml_indent (clean);
        trimmed = g_strstrip (clean);

        if (mctree_yaml_is_ignored_line (trimmed))
        {
            g_free (clean);
            continue;
        }

        mctree_yaml_stack_pop_to_parent (stack, indent);

        if (g_str_has_prefix (trimmed, "-"))
        {
            mctree_node_t *array;
            char *rest;

            if (trimmed[1] != '\0' && !g_ascii_isspace (trimmed[1]))
            {
                g_free (clean);
                g_ptr_array_free (stack, TRUE);
                return mctree_yaml_fail (parser, i, _ ("expected space after '-'"));
            }

            array = mctree_yaml_ensure_container (parser, model, stack, TRUE);
            rest = g_strstrip (trimmed + 1);
            ok = mctree_yaml_parse_sequence_text (parser, model, stack, array, rest, indent, &i);
        }
        else
        {
            mctree_node_t *parent;

            parent = mctree_yaml_ensure_container (parser, model, stack, FALSE);
            ok = mctree_yaml_parse_mapping_text (parser, model, stack, parent, trimmed, indent, &i);
        }

        g_free (clean);
        if (!ok)
        {
            g_ptr_array_free (stack, TRUE);
            return FALSE;
        }
    }

    g_ptr_array_free (stack, TRUE);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static mctree_model_t *
mctree_yaml_parse (const unsigned char *data, gsize len, const mctree_resolver_config_t *config,
                   GError **error)
{
    mctree_yaml_parser_t parser;
    mctree_model_t *model;
    mctree_node_t *root;
    char *contents;

    contents = g_strndup ((const char *) data, len);
    parser.lines = g_strsplit (contents, "\n", -1);
    parser.line_count = g_strv_length (parser.lines);
    parser.anchors = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    parser.error = error;

    model = mctree_model_new (config->scalar_preview_limit);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "YAML", NULL);

    if (!mctree_yaml_parse_lines (&parser, model, root))
    {
        mctree_model_free (model);
        model = NULL;
    }

    g_hash_table_destroy (parser.anchors);
    g_strfreev (parser.lines);
    g_free (contents);
    return model;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_probe (const unsigned char *data, gsize len)
{
    mctree_resolver_config_t config;
    mctree_model_t *model;
    GError *error = NULL;

    mctree_resolver_config_init (&config);
    model = mctree_yaml_parse (data, len, &config, &error);
    g_clear_error (&error);
    if (model == NULL)
        return FALSE;

    mctree_model_free (model);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/*** public variables ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mctree_provider_t mctree_yaml_provider = {
    .content_type = MCTREE_CONTENT_YAML,
    .name = "builtin-yaml",
    .state = MCTREE_PROVIDER_ENABLED,
    .enabled = TRUE,
    .probe = mctree_yaml_probe,
    .parse = mctree_yaml_parse,
};
