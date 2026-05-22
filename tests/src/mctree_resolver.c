/*
   Tests for mctree resolver.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.
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
    const mctree_provider_t *const *providers;
    const mctree_provider_t *xml_provider;
    const mctree_provider_t *yaml_provider;
    gsize count = 0;

    providers = mctree_provider_list (&count);

    ck_assert_uint_eq (count, 3);
    mctest_assert_not_null (providers);

    xml_provider = mctree_provider_for_type (MCTREE_CONTENT_XML);
    yaml_provider = mctree_provider_for_type (MCTREE_CONTENT_YAML);

    mctest_assert_not_null (xml_provider);
    mctest_assert_not_null (yaml_provider);
    ck_assert_str_eq (mctree_provider_state_name (yaml_provider->state), "enabled");
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
    tcase_add_test (tc_core, test_json_file_resolves_with_builtin_provider);
    tcase_add_test (tc_core, test_yaml_file_resolves_with_builtin_provider);
    tcase_add_test (tc_core, test_too_large_file_is_rejected_before_probe);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
