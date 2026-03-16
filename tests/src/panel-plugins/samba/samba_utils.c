/*
   src/panel-plugins/samba - tests for Samba utility functions

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

#define TEST_SUITE_NAME "/src/panel-plugins/samba"

#include "tests/mctest.h"

#include "lib/panel-plugin.h"
#include "lib/util.h"

typedef struct
{
    char *help_filename;
} samba_data_t;

/* --------------------------------------------------------------------------------------------- */
/* Copied utility functions under test                                                           */
/* --------------------------------------------------------------------------------------------- */

static char *
smb_url_up (const char *url)
{
    const char *after_scheme;
    const char *last_slash;

    after_scheme = url + 6; /* past "smb://" */

    if (*after_scheme == '\0')
        return NULL;

    last_slash = strrchr (after_scheme, '/');
    if (last_slash == NULL)
        return NULL; /* at smb://SERVER level -- no parent */

    return g_strndup (url, (gsize) (last_slash - url));
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
samba_get_help_info (void *plugin_data, const char **filename, const char **node)
{
    samba_data_t *data = (samba_data_t *) plugin_data;

    if (filename != NULL && data != NULL && data->help_filename != NULL
        && exist_file (data->help_filename))
        *filename = data->help_filename;
    else if (filename != NULL)
        *filename = NULL;
    if (node != NULL)
        *node = "[Samba Plugin]";

    if (filename != NULL && *filename == NULL)
        return MC_PPR_NOT_SUPPORTED;

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/* @DataSource("test_smb_url_up_ds") */
static const struct test_smb_url_up_ds
{
    const char *input;
    const char *expected;
} test_smb_url_up_ds[] = {
    { "smb://", NULL },
    { "smb://SERVER", NULL },
    { "smb://SERVER/", "smb://SERVER" },
    { "smb://SERVER/SHARE", "smb://SERVER" },
    { "smb://SERVER/SHARE/", "smb://SERVER/SHARE" },
    { "smb://SERVER/SHARE/path", "smb://SERVER/SHARE" },
    { "smb://SERVER/SHARE/path/nested", "smb://SERVER/SHARE/path" },
};

/* --------------------------------------------------------------------------------------------- */

/* @Test(dataSource = "test_smb_url_up_ds") */
START_PARAMETRIZED_TEST (test_smb_url_up, test_smb_url_up_ds)
{
    char *actual;

    actual = smb_url_up (data->input);

    if (data->expected == NULL)
    {
        mctest_assert_null (actual);
    }
    else
    {
        mctest_assert_str_eq (actual, data->expected);
    }

    g_free (actual);
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_samba_get_help_info_existing_file)
{
    samba_data_t plugin_data;
    char template_path[] = "/tmp/mc-samba-help-XXXXXX";
    int fd;
    const char *filename = NULL;
    const char *node = NULL;
    mc_pp_result_t result;

    fd = g_mkstemp (template_path);
    ck_assert_int_ne (fd, -1);
    close (fd);

    plugin_data.help_filename = template_path;

    result = samba_get_help_info (&plugin_data, &filename, &node);

    ck_assert_int_eq (result, MC_PPR_OK);
    mctest_assert_str_eq (filename, template_path);
    mctest_assert_str_eq (node, "[Samba Plugin]");

    unlink (template_path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_samba_get_help_info_missing_file)
{
    samba_data_t plugin_data;
    const char *filename = "sentinel";
    const char *node = NULL;
    mc_pp_result_t result;

    plugin_data.help_filename = (char *) "/tmp/mc-samba-help-does-not-exist";

    result = samba_get_help_info (&plugin_data, &filename, &node);

    ck_assert_int_eq (result, MC_PPR_NOT_SUPPORTED);
    mctest_assert_null (filename);
    mctest_assert_str_eq (node, "[Samba Plugin]");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test */
START_TEST (test_samba_get_help_info_null_data)
{
    const char *filename = "sentinel";
    const char *node = NULL;
    mc_pp_result_t result;

    result = samba_get_help_info (NULL, &filename, &node);

    ck_assert_int_eq (result, MC_PPR_NOT_SUPPORTED);
    mctest_assert_null (filename);
    mctest_assert_str_eq (node, "[Samba Plugin]");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    mctest_add_parameterized_test (tc_core, test_smb_url_up, test_smb_url_up_ds);
    tcase_add_test (tc_core, test_samba_get_help_info_existing_file);
    tcase_add_test (tc_core, test_samba_get_help_info_missing_file);
    tcase_add_test (tc_core, test_samba_get_help_info_null_data);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
