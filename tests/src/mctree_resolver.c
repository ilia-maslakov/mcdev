/*
   Tests for mctree resolver.

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
 */

#define TEST_SUITE_NAME "/src/mctree-resolver"

#include "tests/mctest.h"

#include <unistd.h>

#include "src/mctree/mctree-resolver.h"

/* --------------------------------------------------------------------------------------------- */

static char *
write_temp_file (const char *pattern, const char *contents)
{
    char *path = NULL;
    int fd;

    fd = g_file_open_tmp (pattern, &path, NULL);
    ck_assert_int_ne (fd, -1);
    ck_assert_int_eq (write (fd, contents, strlen (contents)), (ssize_t) strlen (contents));
    close (fd);

    return path;
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_provider_registry_reports_optional_states)
{
    static const mctree_content_type_t types[] = { MCTREE_CONTENT_JSON, MCTREE_CONTENT_XML,
                                                   MCTREE_CONTENT_HTML, MCTREE_CONTENT_YAML };
    gsize i;

    for (i = 0; i < G_N_ELEMENTS (types); i++)
    {
        const mctree_provider_t *provider;

        provider = mctree_provider_for_type (types[i]);
        mctest_assert_not_null (provider);
        ck_assert_int_eq (provider->content_type, types[i]);
    }

    // the built-in providers are unconditionally available
    ck_assert_int_eq (mctree_provider_for_type (MCTREE_CONTENT_JSON)->state,
                      MCTREE_PROVIDER_ENABLED);
    ck_assert_int_eq (mctree_provider_for_type (MCTREE_CONTENT_YAML)->state,
                      MCTREE_PROVIDER_ENABLED);
    mctest_assert_null (mctree_provider_for_type (MCTREE_CONTENT_UNKNOWN));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_xml_file_resolves_to_model_when_provider_is_present)
{
    const mctree_provider_t *xml_provider;
    mctree_resolver_config_t config;
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GArray *rows;
    GError *error = NULL;
    char *path;

    xml_provider = mctree_provider_for_type (MCTREE_CONTENT_XML);
    if (xml_provider->state != MCTREE_PROVIDER_ENABLED)
        return;

    path = write_temp_file ("mctree-XXXXXX.xml", "<root id=\"42\"><child>text</child></root>");

    mctree_resolver_config_init (&config);
    model = mctree_resolve_file (path, &config, &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);
    ck_assert_ptr_eq (result.provider, xml_provider);
    ck_assert_int_eq (result.content_type, MCTREE_CONTENT_XML);

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_ge (rows->len, 3);
    ck_assert_str_eq (g_array_index (rows, mctree_visible_row_t, 0).node->key, "root");

    g_array_free (rows, TRUE);
    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
    unlink (path);
    g_free (path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_html_file_resolves_via_recovering_parser)
{
    const mctree_provider_t *html_provider;
    mctree_resolver_config_t config;
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GArray *rows;
    GError *error = NULL;
    char *path;

    html_provider = mctree_provider_for_type (MCTREE_CONTENT_HTML);
    mctest_assert_not_null (html_provider);
    if (html_provider->state != MCTREE_PROVIDER_ENABLED)
        return;

    // tag soup: unclosed <li> and <br>, unquoted attribute
    path = write_temp_file ("mctree-XXXXXX.html",
                            "<!DOCTYPE html>\n<html><body class=main><ul><li>one<li>two<br></ul>"
                            "</body></html>");

    mctree_resolver_config_init (&config);
    model = mctree_resolve_file (path, &config, &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);
    ck_assert_ptr_eq (result.provider, html_provider);
    ck_assert_int_eq (result.content_type, MCTREE_CONTENT_HTML);

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_ge (rows->len, 2);
    ck_assert_str_eq (g_array_index (rows, mctree_visible_row_t, 0).node->key, "html");

    g_array_free (rows, TRUE);
    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
    unlink (path);
    g_free (path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_plain_text_is_not_claimed_by_html_probe)
{
    const mctree_provider_t *html_provider;

    html_provider = mctree_provider_for_type (MCTREE_CONTENT_HTML);
    mctest_assert_not_null (html_provider);
    if (html_provider->state != MCTREE_PROVIDER_ENABLED)
        return;

    // the recovering parser would accept this; the sniff must reject it
    mctest_assert_false (
        html_provider->probe ((const unsigned char *) "just some notes\nsecond line\n", 28));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_json_file_resolves_with_builtin_provider)
{
    const mctree_provider_t *json_provider;
    mctree_resolver_config_t config;
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GArray *rows;
    GError *error = NULL;
    char *path;

    json_provider = mctree_provider_for_type (MCTREE_CONTENT_JSON);
    ck_assert_int_eq (json_provider->state, MCTREE_PROVIDER_ENABLED);

    path = write_temp_file ("mctree-XXXXXX.json",
                            "{\"name\":\"demo\",\"title\":\"D\\u002eW\\u002e Griffith\","
                            "\"items\":[1,true,null]}");

    mctree_resolver_config_init (&config);
    model = mctree_resolve_file (path, &config, &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);
    ck_assert_ptr_eq (result.provider, json_provider);
    ck_assert_int_eq (result.content_type, MCTREE_CONTENT_JSON);

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_ge (rows->len, 2);
    {
        gboolean found_title = FALSE;
        gboolean found_items = FALSE;
        guint i;

        for (i = 0; i < rows->len; i++)
        {
            mctree_node_t *node = g_array_index (rows, mctree_visible_row_t, i).node;

            if (node->key != NULL && strcmp (node->key, "title") == 0)
            {
                found_title = TRUE;
                ck_assert_str_eq (node->value, "D.W. Griffith");
            }
            else if (node->key != NULL && strcmp (node->key, "items") == 0)
            {
                found_items = TRUE;
                ck_assert_int_eq (node->type, MCTREE_NODE_FIELD);
            }
        }

        mctest_assert_true (found_title);
        mctest_assert_true (found_items);
    }

    g_array_free (rows, TRUE);
    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
    unlink (path);
    g_free (path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Helper: parse content from a temp file, return the model (NULL on failure). */
static mctree_model_t *
resolve_content (const char *name_template, const char *content, mctree_resolver_result_t *result,
                 GError **error)
{
    mctree_resolver_config_t config;
    mctree_model_t *model;
    char *path;

    path = write_temp_file (name_template, content);
    mctree_resolver_config_init (&config);
    model = mctree_resolve_file (path, &config, result, error);
    unlink (path);
    g_free (path);

    return model;
}

/* --------------------------------------------------------------------------------------------- */

/* Depth-first search for a node with the given key. */
static const mctree_node_t *
find_node_by_key (const mctree_node_t *node, const char *key)
{
    guint i;

    if (node->key != NULL && strcmp (node->key, key) == 0)
        return node;

    for (i = 0; node->children != NULL && i < node->children->len; i++)
    {
        const mctree_node_t *found;

        found = find_node_by_key (g_ptr_array_index (node->children, i), key);
        if (found != NULL)
            return found;
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Grammar-derived conformance tables for the built-in YAML subset parser:
 * one entry per rule of the subset documented in the provider header. */

static const char *const yaml_valid_inputs[] = {
    // mappings
    "a: 1\n",
    "a:\n  b:\n    c: deep\n",
    "a: 1\n\n# comment\n---\nb: 2\n",
    // sequences
    "- a\n- b\n",
    "items:\n  - x\n  - y\n",
    "items:\n- x\n- y\n",  // sequence at the same indent as its key
    "- name: x\n  age: 2\n- name: y\n",
    "-\n  a: 1\n",
    // scalars
    "url: http://example.test:8080/\n",
    "quoted: \"a: b\"\n",
    "s: |\n  line1\n  line2\n",
    "s: >\n  folded\n  text\n",
    "- |\n  block in a sequence\n",
    // anchors and aliases
    "a: &x 5\nb: *x\n",
    "- &a first\n- *a\n",
    "base: &b\n  k: v\ncopy: *b\n",
    "a: &a one\nb: &b *a\nc: *b\n",      // anchor on an alias
    "- &person name: Ada\n  age: 36\n",  // anchored inline mapping in a sequence
    // a plain key may start with '-'
    "-foo: bar\n",
    // flow collections and tags are kept as plain scalar text
    "a: [1, 2]\ne: {}\n",
    // quotes inside plain scalars are literal text
    "title: don't\n",
    "foo'bar: baz\n",
    "say: it \"was\" fine\n",
    // a leading UTF-8 BOM is ignored
    "\xef\xbb\xbf"
    "a: 1\n",
    NULL,
};

static const char *const yaml_invalid_inputs[] = {
    "plain text\n",    // a bare scalar is not a document
    "-x\n",            // neither an item nor "key: value"
    "a: 1\nplain\n",   // scalar line inside a mapping
    "  a: 1\nb: 2\n",  // dedent below the block start
    "a: 1\n- x\n",     // sequence item inside a mapping block
    "\ta: 1\n",        // tab in indentation
    "a:\n\t- x\n",     // tab in indentation of a nested block
    NULL,
};

START_TEST (test_yaml_grammar_conformance)
{
    const mctree_provider_t *provider;
    mctree_resolver_config_t config;
    gsize i;

    provider = mctree_provider_for_type (MCTREE_CONTENT_YAML);
    mctree_resolver_config_init (&config);

    for (i = 0; yaml_valid_inputs[i] != NULL; i++)
    {
        GError *error = NULL;
        mctree_model_t *model;

        model = provider->parse ((const unsigned char *) yaml_valid_inputs[i],
                                 strlen (yaml_valid_inputs[i]), &config, &error);
        if (model == NULL)
            ck_abort_msg ("valid YAML rejected: %s (%s)", yaml_valid_inputs[i],
                          error != NULL ? error->message : "no error");
        mctree_model_free (model);
        g_clear_error (&error);
    }

    for (i = 0; yaml_invalid_inputs[i] != NULL; i++)
    {
        GError *error = NULL;
        mctree_model_t *model;

        model = provider->parse ((const unsigned char *) yaml_invalid_inputs[i],
                                 strlen (yaml_invalid_inputs[i]), &config, &error);
        if (model != NULL)
            ck_abort_msg ("invalid YAML accepted: %s", yaml_invalid_inputs[i]);
        if (error == NULL)
            ck_abort_msg ("invalid YAML rejected without diagnostic: %s", yaml_invalid_inputs[i]);
        g_clear_error (&error);
    }
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_sequence_at_key_indent_attaches_under_the_key)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *field;
    const mctree_node_t *item;

    model = resolve_content ("mctree-XXXXXX.yaml", "a:\n- x\n- y\nb: 1\n", &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    field = find_node_by_key (model->root, "a");
    mctest_assert_not_null (field);
    // the sequence must live under "a", not next to it
    item = find_node_by_key (field, "[1]");
    mctest_assert_not_null (item);
    ck_assert_str_eq (item->value, "y");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_sequence_items_support_anchors_and_blocks)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *item;

    model = resolve_content ("mctree-XXXXXX.yaml",
                             "- &a first\n"
                             "- *a\n"
                             "- |\n"
                             "  block text\n",
                             &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    item = find_node_by_key (model->root, "[1]");
    mctest_assert_not_null (item);
    ck_assert_str_eq (item->value, "first");  // alias copied the anchored scalar

    item = find_node_by_key (model->root, "[2]");
    mctest_assert_not_null (item);
    ck_assert_str_eq (item->value, "block text\n");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_anchor_on_alias_resolves_through_the_chain)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *node;

    // scalar chain: c must resolve to the value a was anchored to
    model = resolve_content ("mctree-XXXXXX.yaml", "a: &a one\nb: &b *a\nc: *b\n", &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    node = find_node_by_key (model->root, "c");
    mctest_assert_not_null (node);
    mctest_assert_not_null (node->value);
    ck_assert_str_eq (node->value, "one");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);

    // container chain: the copy of a copy still carries the children
    model = resolve_content ("mctree-XXXXXX.yaml", "base: &a\n  k: v\nmid: &b *a\ncopy: *b\n",
                             &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    node = find_node_by_key (model->root, "copy");
    mctest_assert_not_null (node);
    node = find_node_by_key (node, "k");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "v");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_anchored_inline_mapping_continues_at_content_column)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *item;
    const mctree_node_t *node;

    model = resolve_content ("mctree-XXXXXX.yaml",
                             "people:\n"
                             "- &person name: Ada\n"
                             "  age: 36\n"
                             "twin: *person\n",
                             &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    // both entries belong to the same item mapping
    item = find_node_by_key (model->root, "[0]");
    mctest_assert_not_null (item);
    mctest_assert_not_null (find_node_by_key (item, "name"));
    node = find_node_by_key (item, "age");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "36");

    // the anchor covers the whole mapping
    node = find_node_by_key (model->root, "twin");
    mctest_assert_not_null (node);
    mctest_assert_not_null (find_node_by_key (node, "age"));

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_flow_collections_are_kept_as_scalar_text)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *node;

    model = resolve_content ("mctree-XXXXXX.yaml", "a: [1, 2]\ne: {}\n", &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    node = find_node_by_key (model->root, "a");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "[1, 2]");

    node = find_node_by_key (model->root, "e");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "{}");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_alias_expansion_is_bounded)
{
    mctree_resolver_result_t result = { 0 };
    mctree_resolver_config_t config;
    const mctree_provider_t *provider;
    mctree_model_t *model;
    GError *error = NULL;
    GString *content;
    gsize i;

    /* The "billion laughs" pattern: each level holds two aliases of the
     * previous anchor, so full expansion would be exponential. */
    content = g_string_new ("a0: &b0\n  k: v\n");
    for (i = 1; i <= 14; i++)
        g_string_append_printf (content,
                                "a%" G_GSIZE_FORMAT ": &b%" G_GSIZE_FORMAT "\n"
                                "  x: *b%" G_GSIZE_FORMAT "\n"
                                "  y: *b%" G_GSIZE_FORMAT "\n",
                                i, i, i - 1, i - 1);

    provider = mctree_provider_for_type (MCTREE_CONTENT_YAML);
    mctree_resolver_config_init (&config);

    model = provider->parse ((const unsigned char *) content->str, content->len, &config, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);
    // the budget caps the tree: full expansion would be ~160k nodes
    ck_assert_uint_le (model->nodes->len, config.max_alias_nodes + 200);

    mctree_model_free (model);
    g_string_free (content, TRUE);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_over_budget_alias_repeats_stay_references)
{
    mctree_resolver_config_t config;
    const mctree_provider_t *provider;
    mctree_model_t *model;
    GError *error = NULL;
    GString *content;
    const mctree_node_t *node;
    gsize i;
    const gsize big = 10500;     // over the default budget on its own
    const gsize aliases = 3000;  // each refusal must be O(1), not a re-walk

    content = g_string_new ("big: &big\n");
    for (i = 0; i < big; i++)
        g_string_append_printf (content, "  k%" G_GSIZE_FORMAT ": v\n", i);
    for (i = 0; i < aliases; i++)
        g_string_append_printf (content, "r%" G_GSIZE_FORMAT ": *big\n", i);
    // refusing over-budget aliases must not consume the budget
    g_string_append (content, "s: &s tiny\nt: *s\n");

    provider = mctree_provider_for_type (MCTREE_CONTENT_YAML);
    mctree_resolver_config_init (&config);

    model = provider->parse ((const unsigned char *) content->str, content->len, &config, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    node = find_node_by_key (model->root, "r0");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "*big");

    node = find_node_by_key (model->root, "t");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "tiny");

    // no alias was expanded: the tree is the document itself plus references
    ck_assert_uint_le (model->nodes->len, big + 2 * aliases + 100);

    mctree_model_free (model);
    g_string_free (content, TRUE);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_quotes_inside_plain_scalars_are_literal)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *node;

    model = resolve_content ("mctree-XXXXXX.yaml",
                             "title: don't # comment\n"
                             "foo'bar: baz\n"
                             "quoted: \"x # y\"\n",
                             &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    // the apostrophe does not open a quoted scalar: the comment is stripped
    node = find_node_by_key (model->root, "title");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "don't");

    // a quote inside a key does not break separator detection
    node = find_node_by_key (model->root, "foo'bar");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "baz");

    // a real quoted scalar still protects '#' from comment stripping
    node = find_node_by_key (model->root, "quoted");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "x # y");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_doubled_quote_keeps_the_comment_marker_inside)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *node;

    /* '' inside a single-quoted scalar is an escaped quote: the '#' after it
     * is still inside the scalar and must not be cut as a comment. */
    model = resolve_content ("mctree-XXXXXX.yaml",
                             "q: 'a'' #b'\n"
                             "w: 'don''t stop'\n",
                             &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    node = find_node_by_key (model->root, "q");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "a'' #b");

    node = find_node_by_key (model->root, "w");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "don''t stop");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_bom_is_skipped_by_both_parsers)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *node;

    model = resolve_content ("mctree-XXXXXX.json",
                             "\xef\xbb\xbf"
                             "{\"key\":\"v\"}",
                             &result, &error);
    mctest_assert_null (error);
    mctest_assert_not_null (model);
    mctest_assert_not_null (find_node_by_key (model->root, "key"));
    mctree_model_free (model);
    mctree_resolver_result_clear (&result);

    model = resolve_content ("mctree-XXXXXX.yaml",
                             "\xef\xbb\xbf"
                             "key: v\n",
                             &result, &error);
    mctest_assert_null (error);
    mctest_assert_not_null (model);
    // the BOM must not glue itself to the first key
    node = find_node_by_key (model->root, "key");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "v");
    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_tab_in_indentation_is_diagnosed)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;

    model = resolve_content ("mctree-XXXXXX.yaml", "a:\n\tb: 1\n", &result, &error);

    mctest_assert_null (model);
    mctest_assert_not_null (error);
    mctest_assert_not_null (strstr (error->message, "tab"));

    g_clear_error (&error);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_key_starting_with_dash_is_a_mapping_key)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *node;

    model = resolve_content ("mctree-XXXXXX.yaml", "-foo: bar\n", &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    node = find_node_by_key (model->root, "-foo");
    mctest_assert_not_null (node);
    ck_assert_str_eq (node->value, "bar");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_cyclic_alias_does_not_crash)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *self_node;

    model = resolve_content ("mctree-XXXXXX.yaml",
                             "a: &a\n"
                             "  self: *a\n",
                             &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    // the cyclic alias must be rendered as a reference, not cloned
    self_node = find_node_by_key (model->root, "self");
    mctest_assert_not_null (self_node);
    mctest_assert_not_null (self_node->value);
    ck_assert_str_eq (self_node->value, "*a");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_sequence_scalar_with_colon_is_not_a_mapping)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *item;

    model = resolve_content ("mctree-XXXXXX.yaml",
                             "items:\n"
                             "  - \"http://example.test:8080/\"\n"
                             "  - http://example.test:9090/\n",
                             &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    item = find_node_by_key (model->root, "[0]");
    mctest_assert_not_null (item);
    mctest_assert_not_null (item->value);
    ck_assert_str_eq (item->value, "http://example.test:8080/");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* Grammar-derived conformance tables for the built-in JSON parser: one entry
 * per RFC 8259 rule, positive and negative, instead of ad-hoc examples. */

static const char *const json_valid_inputs[] = {
    // structural
    "{}",
    "[]",
    "[0,1,2]",
    "{\"a\":[{\"b\":null}]}",
    " \t\r\n {\"a\":1} \n",
    // bare top-level scalars
    "true",
    "false",
    "null",
    "42",
    "\"text\"",
    // numbers
    "0",
    "-0",
    "123",
    "0.5",
    "-1.5e+10",
    "1E2",
    "2e-3",
    // strings
    "\"\\\" \\\\ \\/ \\b \\f \\n \\r \\t\"",
    "\"\\u0041\"",
    "\"\\ud83d\\ude00\"",  // surrogate pair
    "\"a\\u0000b\"",       // NUL kept in escaped form
    // a leading UTF-8 BOM is ignored
    "\xef\xbb\xbf"
    "{\"a\":1}",
    NULL,
};

static const char *const json_invalid_inputs[] = {
    // structural
    "",
    "{",
    "[",
    "]",
    "{]",
    "[1 2]",
    "[1,]",
    "{\"a\":1,}",
    "{\"a\" 1}",
    "{1:2}",
    "\"a\" \"b\"",  // trailing data
    // numbers
    "01",
    "1.",
    ".5",
    "+1",
    "1e",
    "--1",
    // literals
    "tru",
    "nul",
    "True",
    // strings
    "\"abc",
    "\"\\q\"",
    "\"\\u12\"",
    "\"\\ud800\"",   // lone high surrogate
    "\"a\x01, b\"",  // raw control character
    "\"\xff\"",      // invalid UTF-8
    NULL,
};

START_TEST (test_json_grammar_conformance)
{
    const mctree_provider_t *provider;
    mctree_resolver_config_t config;
    gsize i;

    provider = mctree_provider_for_type (MCTREE_CONTENT_JSON);
    mctree_resolver_config_init (&config);

    for (i = 0; json_valid_inputs[i] != NULL; i++)
    {
        GError *error = NULL;
        mctree_model_t *model;

        model = provider->parse ((const unsigned char *) json_valid_inputs[i],
                                 strlen (json_valid_inputs[i]), &config, &error);
        if (model == NULL)
            ck_abort_msg ("valid JSON rejected: %s (%s)", json_valid_inputs[i],
                          error != NULL ? error->message : "no error");
        mctree_model_free (model);
        g_clear_error (&error);
    }

    for (i = 0; json_invalid_inputs[i] != NULL; i++)
    {
        GError *error = NULL;
        mctree_model_t *model;

        model = provider->parse ((const unsigned char *) json_invalid_inputs[i],
                                 strlen (json_invalid_inputs[i]), &config, &error);
        if (model != NULL)
            ck_abort_msg ("invalid JSON accepted: %s", json_invalid_inputs[i]);
        if (error == NULL)
            ck_abort_msg ("invalid JSON rejected without diagnostic: %s", json_invalid_inputs[i]);
        g_clear_error (&error);
    }
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_json_deep_nesting_is_rejected_with_diagnostic)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    GString *content;
    gsize i;
    const gsize depth = 300000;

    content = g_string_new (NULL);
    for (i = 0; i < depth; i++)
        g_string_append_c (content, '[');
    g_string_append_c (content, '0');
    for (i = 0; i < depth; i++)
        g_string_append_c (content, ']');

    model = resolve_content ("mctree-XXXXXX.json", content->str, &result, &error);

    mctest_assert_null (model);
    mctest_assert_not_null (error);
    mctest_assert_not_null (strstr (error->message, "nesting"));

    g_clear_error (&error);
    g_string_free (content, TRUE);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_json_moderate_nesting_parses)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    GString *content;
    gsize i;
    const gsize depth = 100;

    content = g_string_new (NULL);
    for (i = 0; i < depth; i++)
        g_string_append_c (content, '[');
    g_string_append_c (content, '7');
    for (i = 0; i < depth; i++)
        g_string_append_c (content, ']');

    model = resolve_content ("mctree-XXXXXX.json", content->str, &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    mctree_model_free (model);
    g_string_free (content, TRUE);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_deep_nesting_is_rejected_with_diagnostic)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    GString *content;
    gsize i;
    const gsize depth = 300;

    content = g_string_new (NULL);
    for (i = 0; i < depth; i++)
    {
        gsize j;

        for (j = 0; j < i * 2; j++)
            g_string_append_c (content, ' ');
        g_string_append (content, "a:\n");
    }

    model = resolve_content ("mctree-XXXXXX.yaml", content->str, &result, &error);

    mctest_assert_null (model);
    mctest_assert_not_null (error);
    mctest_assert_not_null (strstr (error->message, "nesting"));

    g_clear_error (&error);
    g_string_free (content, TRUE);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_json_nul_escape_is_kept_in_escaped_form)
{
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    const mctree_node_t *value_node;

    model = resolve_content ("mctree-XXXXXX.json", "{\"value\":\"a\\u0000b\"}", &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);

    value_node = find_node_by_key (model->root, "value");
    mctest_assert_not_null (value_node);
    mctest_assert_not_null (value_node->value);
    ck_assert_str_eq (value_node->value, "a\\u0000b");

    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_yaml_file_resolves_with_builtin_provider)
{
    const mctree_provider_t *yaml_provider;
    mctree_resolver_config_t config;
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GArray *rows;
    GError *error = NULL;
    char *path;

    yaml_provider = mctree_provider_for_type (MCTREE_CONTENT_YAML);
    ck_assert_int_eq (yaml_provider->state, MCTREE_PROVIDER_ENABLED);

    path = write_temp_file ("mctree-XXXXXX.yaml",
                            "---\n"
                            "receipt: Oz-Ware Purchase Invoice\n"
                            "customer:\n"
                            "    first_name: Dorothy\n"
                            "items:\n"
                            "    - part_no: A4786\n"
                            "      quantity: 4\n"
                            "bill-to: &id001\n"
                            "    street: |\n"
                            "            123 Tornado Alley\n"
                            "            Suite 16\n"
                            "    city: East Centerville\n"
                            "ship-to: *id001\n"
                            "specialDelivery: >\n"
                            "    Follow the Yellow Brick\n"
                            "    Road to the Emerald City.\n"
                            "    Pay no attention to the\n"
                            "    man behind the curtain.\n"
                            "note: |\n"
                            "    keep # this text\n");

    mctree_resolver_config_init (&config);
    model = mctree_resolve_file (path, &config, &result, &error);

    mctest_assert_null (error);
    mctest_assert_not_null (model);
    mctest_assert_not_null (result.provider);
    ck_assert_int_eq (result.content_type, MCTREE_CONTENT_YAML);
    ck_assert_ptr_eq (result.provider, yaml_provider);

    rows = mctree_model_build_visible_rows (model);
    ck_assert_uint_ge (rows->len, 6);
    {
        gboolean found_special_delivery = FALSE;
        gboolean found_note = FALSE;
        guint i;

        for (i = 0; i < rows->len; i++)
        {
            mctree_node_t *node = g_array_index (rows, mctree_visible_row_t, i).node;

            ck_assert_str_ne (mctree_node_type_name (node->type), "object");
            if (node->key != NULL && strcmp (node->key, "specialDelivery") == 0)
            {
                found_special_delivery = TRUE;
                mctest_assert_not_null (node->value);
                mctest_assert_true (g_str_has_prefix (node->value, "Follow the Yellow Brick"));
            }
            else if (node->key != NULL && strcmp (node->key, "note") == 0)
            {
                found_note = TRUE;
                mctest_assert_not_null (node->value);
                ck_assert_str_eq (node->value, "keep # this text\n");
            }
        }

        mctest_assert_true (found_special_delivery);
        mctest_assert_true (found_note);
    }

    g_array_free (rows, TRUE);
    mctree_model_free (model);
    mctree_resolver_result_clear (&result);
    unlink (path);
    g_free (path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_too_large_file_is_rejected_before_probe)
{
    mctree_resolver_config_t config;
    mctree_resolver_result_t result = { 0 };
    mctree_model_t *model;
    GError *error = NULL;
    char *path;

    path = write_temp_file ("mctree-XXXXXX.xml", "<root/>");

    mctree_resolver_config_init (&config);
    config.max_parse_size = 1;
    model = mctree_resolve_file (path, &config, &result, &error);

    mctest_assert_null (model);
    mctest_assert_null (error);
    mctest_assert_true (result.too_large);

    unlink (path);
    g_free (path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    tcase_add_test (tc_core, test_provider_registry_reports_optional_states);
    tcase_add_test (tc_core, test_xml_file_resolves_to_model_when_provider_is_present);
    tcase_add_test (tc_core, test_html_file_resolves_via_recovering_parser);
    tcase_add_test (tc_core, test_plain_text_is_not_claimed_by_html_probe);
    tcase_add_test (tc_core, test_json_file_resolves_with_builtin_provider);
    tcase_add_test (tc_core, test_yaml_file_resolves_with_builtin_provider);
    tcase_add_test (tc_core, test_yaml_grammar_conformance);
    tcase_add_test (tc_core, test_yaml_sequence_at_key_indent_attaches_under_the_key);
    tcase_add_test (tc_core, test_yaml_sequence_items_support_anchors_and_blocks);
    tcase_add_test (tc_core, test_yaml_anchor_on_alias_resolves_through_the_chain);
    tcase_add_test (tc_core, test_yaml_anchored_inline_mapping_continues_at_content_column);
    tcase_add_test (tc_core, test_yaml_flow_collections_are_kept_as_scalar_text);
    tcase_add_test (tc_core, test_yaml_alias_expansion_is_bounded);
    tcase_add_test (tc_core, test_yaml_over_budget_alias_repeats_stay_references);
    tcase_add_test (tc_core, test_yaml_quotes_inside_plain_scalars_are_literal);
    tcase_add_test (tc_core, test_yaml_doubled_quote_keeps_the_comment_marker_inside);
    tcase_add_test (tc_core, test_bom_is_skipped_by_both_parsers);
    tcase_add_test (tc_core, test_yaml_tab_in_indentation_is_diagnosed);
    tcase_add_test (tc_core, test_yaml_key_starting_with_dash_is_a_mapping_key);
    tcase_add_test (tc_core, test_yaml_cyclic_alias_does_not_crash);
    tcase_add_test (tc_core, test_yaml_sequence_scalar_with_colon_is_not_a_mapping);
    tcase_add_test (tc_core, test_json_nul_escape_is_kept_in_escaped_form);
    tcase_add_test (tc_core, test_json_grammar_conformance);
    tcase_add_test (tc_core, test_json_deep_nesting_is_rejected_with_diagnostic);
    tcase_add_test (tc_core, test_json_moderate_nesting_parses);
    tcase_add_test (tc_core, test_yaml_deep_nesting_is_rejected_with_diagnostic);
    tcase_add_test (tc_core, test_too_large_file_is_rejected_before_probe);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
