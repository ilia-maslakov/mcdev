/*
   YAML provider for mctree: a recursive descent parser for an explicit
   YAML subset.

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

   Supported subset (also stated in doc/man/mc.1.in):

     document       = nested-block at any indent
     block(I)       = mapping(I) | sequence(I)
     mapping(I)     = one or more "key: value" entries at indent I
     sequence(I)    = one or more "- value" items at indent I; an item may
                      start an inline mapping ("- key: value") whose other
                      entries continue at the column of the item content,
                      i.e. where an "&anchor" prefix sits, if present
     value          = plain scalar | quoted scalar | *alias | "|" or ">"
                      block scalar | empty (nested block on deeper lines;
                      a mapping value may also be a sequence at the SAME
                      indent as its key)
     "&name" before a value anchors it; "*name" copies the anchored value.
     A cyclic alias, and expansion past the parse-wide node budget
     (config max_alias_nodes), is shown as a *name reference instead.
     Text that merely starts with '*' but contains spaces is a plain
     scalar, not an alias.

   Not supported: tags and flow collections ({} and []) are not parsed
   and remain part of the plain scalar text; multi-line plain scalars
   and escape processing inside quoted scalars are not supported.
   "---" and "..." lines are ignored, so multiple documents are merged
   into one tree.  A tab character in indentation is a parse error.

   Layers, mirroring the JSON provider:
     - a line scanner classifying every raw line once (indent, content
       with comments stripped, ignorable flag) behind a cursor;
     - shared value handling used by mapping entries and sequence items;
     - block grammar rules recursing per nesting level (depth-capped).
   Every parse function returns TRUE on success and FALSE with the
   GError set exactly once at the failing line.
 */

#include <config.h>

#include <string.h>

#include "src/mctree/mctree-providers.h"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    int indent;
    char *content;        // comment-stripped, trimmed; owned
    gboolean ignored;     // blank, comment-only, "---" or "..."
    gboolean tab_indent;  // indentation contains a tab: a parse error
} mctree_yaml_line_t;

typedef struct
{
    char **raw_lines;  // unprocessed lines, for block scalar collection
    mctree_yaml_line_t *lines;
    gsize line_count;
    gsize pos;  // cursor into lines[] / raw_lines[]
    gsize depth;
    gsize max_depth;
    gsize alias_budget;        // remaining nodes alias expansion may create
    GHashTable *anchors;       // name -> value node in the model
    GHashTable *subtree_sizes; /* mctree_node_t* -> gsize, memoized: without it
                                  every over-budget alias re-walks the whole
                                  anchored subtree, O(aliases * subtree) CPU */
    GError **error;
} mctree_yaml_parser_t;

/*** forward declarations (file scope functions) *************************************************/

static gboolean mctree_yaml_parse_mapping (mctree_yaml_parser_t *parser, mctree_model_t *model,
                                           mctree_node_t *container, int indent, char *first_text,
                                           gsize first_line);
static gboolean mctree_yaml_parse_nested (mctree_yaml_parser_t *parser, mctree_model_t *model,
                                          mctree_node_t *parent, int entry_indent,
                                          gboolean allow_sequence_at_entry_indent,
                                          mctree_node_t **container_out);

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */
/* Line scanner. */

static int
mctree_yaml_indent (const char *line)
{
    int indent = 0;

    while (line[indent] == ' ')
        indent++;

    return indent;
}

/* --------------------------------------------------------------------------------------------- */

/* Left-to-right scan state over one line.  A quote starts a quoted scalar
 * only at a position where a scalar can begin (line start, after ": ",
 * after "- ", after an "&anchor" token); a quote inside a plain scalar
 * ("don't", "foo'bar") is literal text. */

typedef struct
{
    gboolean in_single;
    gboolean in_double;
    gboolean at_scalar_start;
    gboolean in_anchor;
    gboolean skip_next;  // second half of a doubled '' inside a single-quoted scalar
} mctree_yaml_scan_t;

static void
mctree_yaml_scan_init (mctree_yaml_scan_t *scan)
{
    memset (scan, 0, sizeof (*scan));
    scan->at_scalar_start = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Feed text[i] to the scanner.  Returns TRUE while inside a quoted scalar,
 * including its delimiters. */

static gboolean
mctree_yaml_scan_char (mctree_yaml_scan_t *scan, const char *text, gsize i)
{
    const char c = text[i];
    const char next = c != '\0' ? text[i + 1] : '\0';

    if (scan->skip_next)
    {
        scan->skip_next = FALSE;
        return TRUE;
    }

    if (scan->in_single)
    {
        if (c == '\'')
        {
            if (next == '\'')
                scan->skip_next = TRUE;  // '' is an escaped quote: stay inside
            else
                scan->in_single = FALSE;
        }
        return TRUE;
    }
    if (scan->in_double)
    {
        if (c == '"')
            scan->in_double = FALSE;
        return TRUE;
    }

    if (g_ascii_isspace (c))
    {
        scan->in_anchor = FALSE;  // whitespace ends the token, keeps scalar-start
        return FALSE;
    }

    if (scan->in_anchor)
        return FALSE;

    if (scan->at_scalar_start)
    {
        if (c == '\'' || c == '"')
        {
            if (c == '\'')
                scan->in_single = TRUE;
            else
                scan->in_double = TRUE;
            scan->at_scalar_start = FALSE;
            return TRUE;
        }
        if (c == '&')
        {
            scan->in_anchor = TRUE;  // "&name" precedes the scalar it anchors
            return FALSE;
        }
        if (c == '-' && (next == '\0' || g_ascii_isspace (next)))
            return FALSE;  // "- " is the item marker, the scalar starts after it
    }

    if (c == ':' && (next == '\0' || g_ascii_isspace (next)))
    {
        scan->at_scalar_start = TRUE;  // a value scalar may start after ": "
        return FALSE;
    }

    scan->at_scalar_start = FALSE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_yaml_strip_comment (const char *text)
{
    mctree_yaml_scan_t scan;
    gsize i;

    mctree_yaml_scan_init (&scan);

    for (i = 0; text[i] != '\0'; i++)
    {
        if (!scan.in_single && !scan.in_double && text[i] == '#'
            && (i == 0 || g_ascii_isspace (text[i - 1])))
            return g_strndup (text, i);
        (void) mctree_yaml_scan_char (&scan, text, i);
    }

    return g_strdup (text);
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_yaml_scan_lines (mctree_yaml_parser_t *parser)
{
    gsize i;

    parser->lines = g_new0 (mctree_yaml_line_t, parser->line_count);

    for (i = 0; i < parser->line_count; i++)
    {
        mctree_yaml_line_t *line = &parser->lines[i];
        const char *raw = parser->raw_lines[i];
        const gsize ws = strspn (raw, " \t");
        char *clean;

        clean = mctree_yaml_strip_comment (raw);
        line->indent = mctree_yaml_indent (clean);
        line->content = g_strstrip (clean);
        line->ignored = line->content[0] == '\0' || strcmp (line->content, "---") == 0
            || strcmp (line->content, "...") == 0;
        line->tab_indent = raw[ws] != '\0' && memchr (raw, '\t', ws) != NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_yaml_skip_ignored (mctree_yaml_parser_t *parser)
{
    while (parser->pos < parser->line_count && parser->lines[parser->pos].ignored)
        parser->pos++;
}

/* --------------------------------------------------------------------------------------------- */

/* The meaningful line under the cursor, or NULL at end of input. */

static const mctree_yaml_line_t *
mctree_yaml_cur (mctree_yaml_parser_t *parser)
{
    mctree_yaml_skip_ignored (parser);
    return parser->pos < parser->line_count ? &parser->lines[parser->pos] : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_fail (mctree_yaml_parser_t *parser, gsize line_index, const char *message)
{
    g_set_error (parser->error, G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
                 _ ("YAML parse error at line %" G_GSIZE_FORMAT ": %s"), line_index + 1, message);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/* Scalar helpers. */

/* A sequence item is "-" followed by whitespace or end of line; a plain
 * key may legitimately start with '-' ("-foo: bar"). */

static gboolean
mctree_yaml_is_sequence_item (const char *content)
{
    return content[0] == '-' && (content[1] == '\0' || g_ascii_isspace (content[1]));
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_yaml_unquote (const char *value)
{
    gsize len;

    len = strlen (value);
    if (len >= 2
        && ((value[0] == '"' && value[len - 1] == '"')
            || (value[0] == '\'' && value[len - 1] == '\'')))
        return g_strndup (value + 1, len - 2);

    return g_strdup (value);
}

/* --------------------------------------------------------------------------------------------- */

/* If text begins with an "&name" anchor, return the text after it and store
 * the anchor name; otherwise return text unchanged.  Never mutates text, so
 * column arithmetic on the containing line stays valid. */

static const char *
mctree_yaml_skip_anchor (const char *text, char **anchor_out)
{
    const char *p = text;
    const char *start;

    *anchor_out = NULL;

    if (*p != '&')
        return text;

    start = ++p;
    while (*p != '\0' && !g_ascii_isspace (*p))
        p++;

    if (p == start)
        return text;

    *anchor_out = g_strndup (start, (gsize) (p - start));
    while (g_ascii_isspace (*p))
        p++;

    return p;
}

/* --------------------------------------------------------------------------------------------- */

/* An unquoted ':' followed by whitespace or end of line separates a mapping
 * key from its value; a ':' inside a scalar (URLs, quoted text) does not. */

static char *
mctree_yaml_find_key_separator (const char *text)
{
    mctree_yaml_scan_t scan;
    gsize i;

    mctree_yaml_scan_init (&scan);

    for (i = 0; text[i] != '\0'; i++)
    {
        if (!scan.in_single && !scan.in_double && text[i] == ':'
            && (text[i + 1] == '\0' || g_ascii_isspace (text[i + 1])))
            return (char *) text + i;
        (void) mctree_yaml_scan_char (&scan, text, i);
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_split_key_value (char *text, char **key, char **value)
{
    char *p;

    p = mctree_yaml_find_key_separator (text);
    if (p == NULL)
        return FALSE;

    *p = '\0';
    *key = g_strstrip (text);
    *value = g_strstrip (p + 1);
    return **key != '\0';
}

/* --------------------------------------------------------------------------------------------- */
/* Block scalar collection ("|" literal and ">" folded).  Works on the raw
 * lines: comments and blank lines inside a block are content. */

static char *
mctree_yaml_chomp_line (const char *line)
{
    return g_strchomp (g_strdup (line));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_block_line_ends (mctree_yaml_parser_t *parser, gsize i, int parent_indent,
                             int *indent_out, char **text_out)
{
    char *clean = mctree_yaml_chomp_line (parser->raw_lines[i]);
    int indent = mctree_yaml_indent (clean);
    gboolean blank = clean[mctree_yaml_indent (clean)] == '\0';

    // a trailing blank line is not part of the block
    if (blank && i == parser->line_count - 1)
    {
        g_free (clean);
        return TRUE;
    }

    if (!blank && indent <= parent_indent)
    {
        g_free (clean);
        return TRUE;
    }

    *indent_out = blank ? -1 : indent;
    *text_out = clean;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_yaml_collect_block (mctree_yaml_parser_t *parser, int parent_indent, gboolean folded)
{
    GString *block;
    int block_indent = -1;
    gsize i;

    // first pass: the block content indent is the minimum indent of its lines
    for (i = parser->pos; i < parser->line_count; i++)
    {
        int indent;
        char *text;

        if (mctree_yaml_block_line_ends (parser, i, parent_indent, &indent, &text))
            break;
        if (indent >= 0 && (block_indent < 0 || indent < block_indent))
            block_indent = indent;
        g_free (text);
    }

    if (block_indent < 0)
        block_indent = parent_indent + 1;

    block = g_string_new ("");

    for (i = parser->pos; i < parser->line_count; i++)
    {
        int indent;
        char *text;
        const char *content;

        if (mctree_yaml_block_line_ends (parser, i, parent_indent, &indent, &text))
            break;

        content = text + MIN ((int) strlen (text), block_indent);
        if (!folded)
        {
            g_string_append (block, content);
            g_string_append_c (block, '\n');
        }
        else if (indent < 0)
            g_string_append_c (block, '\n');
        else
        {
            if (block->len > 0 && block->str[block->len - 1] != '\n')
                g_string_append_c (block, ' ');
            g_string_append (block, g_strstrip ((char *) content));
        }

        g_free (text);
    }

    parser->pos = i;
    return g_string_free (block, FALSE);
}

/* --------------------------------------------------------------------------------------------- */
/* Anchors and aliases. */

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

/* Exact node count of a subtree, memoized per node.
 * mctree_node_descendant_count() is a display count that skips transparent
 * containers, but the clone copies every node, so the budget must too.
 * Memoization is safe only for finished subtrees: the caller must not
 * measure a node that is still being built (the cyclic-alias case). */

static gsize
mctree_yaml_subtree_size (mctree_yaml_parser_t *parser, const mctree_node_t *node)
{
    gpointer cached;
    gsize count = 1;
    guint i;

    cached = g_hash_table_lookup (parser->subtree_sizes, node);
    if (cached != NULL)
        return GPOINTER_TO_SIZE (cached);

    for (i = 0; node->children != NULL && i < node->children->len; i++)
        count += mctree_yaml_subtree_size (parser, g_ptr_array_index (node->children, i));

    g_hash_table_insert (parser->subtree_sizes, (gpointer) node, GSIZE_TO_POINTER (count));
    return count;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_node_is_ancestor (const mctree_node_t *node, const mctree_node_t *candidate)
{
    for (; node != NULL; node = node->parent)
        if (node == candidate)
            return TRUE;

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_yaml_register_anchor (mctree_yaml_parser_t *parser, char *anchor, mctree_node_t *node)
{
    if (anchor == NULL)
        return;
    if (node != NULL)
        g_hash_table_replace (parser->anchors, anchor, node);
    else
        g_free (anchor);
}

/* --------------------------------------------------------------------------------------------- */

/* Copy the anchored value under a new node of node_type/key.  An unknown
 * anchor, a cyclic one (the anchored node is still being built and the
 * insertion point lies inside it, so cloning would never terminate) and an
 * expansion past the parse-wide node budget (chained aliases multiply the
 * tree: the "billion laughs" pattern) are all shown as a *name reference
 * instead of a copy. */

static mctree_node_t *
mctree_yaml_add_alias (mctree_yaml_parser_t *parser, mctree_model_t *model, mctree_node_t *parent,
                       mctree_node_type_t node_type, const char *key, const char *alias_name)
{
    mctree_node_t *source;
    mctree_node_t *node;

    source = (mctree_node_t *) g_hash_table_lookup (parser->anchors, alias_name);

    /* The cycle check must come before the size walk: a cyclic source is
       still being built, and measuring it would memoize a stale size. */
    if (source == NULL || mctree_yaml_node_is_ancestor (parent, source)
        || mctree_yaml_subtree_size (parser, source) > parser->alias_budget)
    {
        char *value = g_strdup_printf ("*%s", alias_name);

        node = mctree_model_add_node (model, parent, node_type, key, value);
        g_free (value);
        return node;
    }

    if (mctree_node_child_count (source) > 0)
    {
        parser->alias_budget -= mctree_yaml_subtree_size (parser, source);
        node = mctree_model_add_node (model, parent, node_type, key, NULL);
        mctree_yaml_clone_node (model, node, source);
        return node;
    }

    return mctree_model_add_node (model, parent, node_type, key, source->value);
}

/* --------------------------------------------------------------------------------------------- */
/* Grammar rules. */

/* A meaningful line is consumed by the grammar: reject broken indentation
 * here so it cannot silently change the tree shape. */

static gboolean
mctree_yaml_check_line (mctree_yaml_parser_t *parser, const mctree_yaml_line_t *line)
{
    if (!line->tab_indent)
        return TRUE;

    return mctree_yaml_fail (parser, parser->pos, _ ("tab character in indentation"));
}

/* --------------------------------------------------------------------------------------------- */

/* Create a node of node_type/key under parent holding the value described
 * by text (with its "&name" prefix, if any, already parsed into anchor by
 * the caller, which passes ownership).  Consumes following lines for
 * nested blocks and block scalars.  entry_indent is the indent of the
 * line the value appeared on. */

static gboolean
mctree_yaml_parse_value (mctree_yaml_parser_t *parser, mctree_model_t *model, mctree_node_t *parent,
                         mctree_node_type_t node_type, const char *key, const char *text,
                         char *anchor, int entry_indent, gboolean allow_sequence_at_entry_indent)
{
    mctree_node_t *node;

    if (text[0] == '\0')
    {
        mctree_node_t *container = NULL;

        node = mctree_model_add_node (model, parent, node_type, key, NULL);
        if (!mctree_yaml_parse_nested (parser, model, node, entry_indent,
                                       allow_sequence_at_entry_indent, &container))
        {
            g_free (anchor);
            return FALSE;
        }
        mctree_yaml_register_anchor (parser, anchor, container != NULL ? container : node);
        return TRUE;
    }

    if (strcmp (text, "|") == 0 || strcmp (text, ">") == 0)
    {
        char *block;

        /* The entry line is already consumed: the cursor is on the first
           potential block line. */
        block = mctree_yaml_collect_block (parser, entry_indent, text[0] == '>');
        node = mctree_model_add_node (model, parent, node_type, key, block);
        g_free (block);
        mctree_yaml_register_anchor (parser, anchor, node);
        return TRUE;
    }

    if (text[0] == '*' && text[1] != '\0' && strchr (text, ' ') == NULL)
    {
        /* "b: &b *a": the anchor of an aliased value must resolve too, so
           register it on the node the alias produced. */
        node = mctree_yaml_add_alias (parser, model, parent, node_type, key, text + 1);
        mctree_yaml_register_anchor (parser, anchor, node);
        return TRUE;
    }

    {
        char *scalar = mctree_yaml_unquote (text);

        node = mctree_model_add_node (model, parent, node_type, key, scalar);
        g_free (scalar);
        mctree_yaml_register_anchor (parser, anchor, node);
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_yaml_parse_sequence (mctree_yaml_parser_t *parser, mctree_model_t *model,
                            mctree_node_t *container, int indent)
{
    const mctree_yaml_line_t *cur;

    while ((cur = mctree_yaml_cur (parser)) != NULL && cur->indent == indent
           && mctree_yaml_is_sequence_item (cur->content))
    {
        char *content = cur->content;
        const gsize line = parser->pos;
        const char *rest;
        char *anchor = NULL;
        char *item_key;
        int content_col;
        gboolean ok = TRUE;

        if (!mctree_yaml_check_line (parser, cur))
            return FALSE;

        parser->pos++;

        rest = content + 1;
        while (g_ascii_isspace (*rest))
            rest++;
        content_col = indent + (int) (rest - content);
        rest = mctree_yaml_skip_anchor (rest, &anchor);

        item_key = g_strdup_printf ("[%u]", mctree_node_child_count (container));

        if (rest[0] != '\0' && mctree_yaml_find_key_separator (rest) != NULL)
        {
            /* "- key: value": the item is a mapping whose first entry is on
               this line; its other entries continue at the column of the
               item content (where a "&anchor" prefix sits, if present). */
            mctree_node_t *item;
            mctree_node_t *object;

            item = mctree_model_add_node (model, container, MCTREE_NODE_ITEM, item_key, NULL);
            object = mctree_model_add_node (model, item, MCTREE_NODE_OBJECT, NULL, NULL);
            mctree_yaml_register_anchor (parser, anchor, object);
            anchor = NULL;

            if (++parser->depth > parser->max_depth)
                ok = mctree_yaml_fail (parser, line, _ ("nesting is too deep"));
            else
                ok = mctree_yaml_parse_mapping (parser, model, object, content_col, (char *) rest,
                                                line);
            parser->depth--;
        }
        else
        {
            ok = mctree_yaml_parse_value (parser, model, container, MCTREE_NODE_ITEM, item_key,
                                          rest, anchor, indent, FALSE);
            anchor = NULL;  // ownership passed to parse_value
        }

        g_free (item_key);
        if (!ok)
            return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Parse mapping entries at the given indent.  When the first entry comes
 * from a "- key: value" line it is passed as first_text (already consumed
 * from the cursor). */

static gboolean
mctree_yaml_parse_mapping (mctree_yaml_parser_t *parser, mctree_model_t *model,
                           mctree_node_t *container, int indent, char *first_text, gsize first_line)
{
    char *text = first_text;
    gsize line = first_line;

    while (TRUE)
    {
        char *key;
        char *value;
        char *anchor = NULL;
        const char *value_text;

        if (text == NULL)
        {
            const mctree_yaml_line_t *cur = mctree_yaml_cur (parser);

            if (cur == NULL || cur->indent != indent || mctree_yaml_is_sequence_item (cur->content))
                return TRUE;

            if (!mctree_yaml_check_line (parser, cur))
                return FALSE;

            text = cur->content;
            line = parser->pos;
            parser->pos++;
        }

        if (!mctree_yaml_split_key_value (text, &key, &value))
            return mctree_yaml_fail (parser, line, _ ("expected key: value"));

        value_text = mctree_yaml_skip_anchor (value, &anchor);
        if (!mctree_yaml_parse_value (parser, model, container, MCTREE_NODE_FIELD, key, value_text,
                                      anchor, indent, TRUE))
            return FALSE;

        text = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Parse the block nested under an entry at entry_indent: lines indented
 * deeper, or (for mapping values) a sequence at the entry indent itself.
 * Returns the created container in *container_out, NULL if there was no
 * nested content. */

static gboolean
mctree_yaml_parse_nested (mctree_yaml_parser_t *parser, mctree_model_t *model,
                          mctree_node_t *parent, int entry_indent,
                          gboolean allow_sequence_at_entry_indent, mctree_node_t **container_out)
{
    const mctree_yaml_line_t *cur;
    mctree_node_t *container;
    gboolean sequence;
    gboolean ok;

    *container_out = NULL;

    cur = mctree_yaml_cur (parser);
    if (cur == NULL)
        return TRUE;

    sequence = mctree_yaml_is_sequence_item (cur->content);

    if (cur->indent <= entry_indent
        && !(sequence && allow_sequence_at_entry_indent && cur->indent == entry_indent))
        return TRUE;  // nothing nested under this entry

    /* No tab check here: this line is only inspected, and the mapping or
       sequence loop below is the one that consumes and validates it. */

    if (++parser->depth > parser->max_depth)
    {
        parser->depth--;
        return mctree_yaml_fail (parser, parser->pos, _ ("nesting is too deep"));
    }

    container = mctree_model_add_node (
        model, parent, sequence ? MCTREE_NODE_ARRAY : MCTREE_NODE_OBJECT, NULL, NULL);

    ok = sequence ? mctree_yaml_parse_sequence (parser, model, container, cur->indent)
                  : mctree_yaml_parse_mapping (parser, model, container, cur->indent, NULL, 0);

    parser->depth--;
    *container_out = container;
    return ok;
}

/* --------------------------------------------------------------------------------------------- */
/* Provider entry points. */

static mctree_model_t *
mctree_yaml_parse (const unsigned char *data, gsize len, const mctree_resolver_config_t *config,
                   GError **error)
{
    mctree_yaml_parser_t parser;
    mctree_model_t *model;
    mctree_node_t *root;
    mctree_node_t *container;
    char *contents;
    gboolean ok;
    gsize i;

    const char *body;

    contents = g_strndup ((const char *) data, len);
    // skip a leading UTF-8 BOM so it does not glue itself to the first key
    body = g_str_has_prefix (contents, "\xef\xbb\xbf") ? contents + 3 : contents;
    memset (&parser, 0, sizeof (parser));
    parser.raw_lines = g_strsplit (body, "\n", -1);
    parser.line_count = g_strv_length (parser.raw_lines);
    parser.anchors = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    parser.subtree_sizes = g_hash_table_new (g_direct_hash, g_direct_equal);
    parser.max_depth = config->max_depth;
    parser.alias_budget = config->max_alias_nodes;
    parser.error = error;
    mctree_yaml_scan_lines (&parser);

    model = mctree_model_new (config->scalar_preview_limit);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "YAML", NULL);

    ok = mctree_yaml_parse_nested (&parser, model, root, -1, TRUE, &container);
    if (ok && mctree_yaml_cur (&parser) != NULL)
        ok = mctree_yaml_fail (&parser, parser.pos, _ ("unexpected indentation"));

    if (!ok)
    {
        mctree_model_free (model);
        model = NULL;
    }

    for (i = 0; i < parser.line_count; i++)
        g_free (parser.lines[i].content);
    g_free (parser.lines);
    g_hash_table_destroy (parser.anchors);
    g_hash_table_destroy (parser.subtree_sizes);
    g_strfreev (parser.raw_lines);
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
    .name = "yaml",
    .state = MCTREE_PROVIDER_ENABLED,
    .probe = mctree_yaml_probe,
    .parse = mctree_yaml_parse,
};
