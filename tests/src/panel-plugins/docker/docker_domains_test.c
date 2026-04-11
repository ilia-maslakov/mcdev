/*
   tests for docker domain modules -- images, volumes, networks

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

#define TEST_SUITE_NAME "/src/panel-plugins/docker/domains"

#include "tests/mctest.h"

#include <stdarg.h>
#include <string.h>

#include "src/panel-plugins/docker/docker-internal.h"

static const char *stub_images_output;
static const char *stub_networks_output;
static const char *stub_volumes_output;
static const char *stub_ps_output;
static const char *stub_mounts_output;
static GPtrArray *stub_commands;
static const char docker_images_cmd[] =
    "docker images --format '{{.ID}}\\t{{.Repository}}:{{.Tag}}\\t{{.Size}}'";
static const char docker_networks_cmd[] =
    "docker network ls --format '{{.ID}}\\t{{.Name}}\\t{{.Driver}}\\t{{.Scope}}'";
static const char docker_volumes_cmd[] =
    "docker volume ls --format '{{.Name}}\\t{{.Driver}}\\t{{.Scope}}\\t{{.Status}}'";
static const char docker_ps_cmd[] = "docker ps -aq";
static const char docker_mounts_inspect_prefix[] =
    "docker inspect --format '{{.Name}} {{range .Mounts}}{{.Type}}:{{.Name}} {{end}}'";
static const char docker_rmi_prefix[] = "docker rmi ";
static const char docker_network_rm_prefix[] = "docker network rm ";
static const char docker_volume_rm_prefix[] = "docker volume rm ";

/* Source-under-test: include .c files directly to exercise internal parsers. */
#include "src/panel-plugins/docker/images.c"
#include "src/panel-plugins/docker/networks.c"
#include "src/panel-plugins/docker/volumes.c"

/* --------------------------------------------------------------------------------------------- */

void
docker_item_free (gpointer p)
{
    docker_item_t *item = (docker_item_t *) p;

    if (item == NULL)
        return;

    g_free (item->name);
    g_free (item->id);
    g_free (item->link_target);
    g_free (item->status);
    g_free (item->image);
    g_free (item->ports);
    g_free (item->driver);
    g_free (item->scope);
    g_free (item->created);
    g_free (item);
}

/* --------------------------------------------------------------------------------------------- */

const docker_item_t *
find_item_by_name (const docker_data_t *data, const char *name)
{
    guint i;

    if (data == NULL || data->items == NULL || name == NULL)
        return NULL;

    for (i = 0; i < data->items->len; i++)
    {
        const docker_item_t *item = (const docker_item_t *) g_ptr_array_index (data->items, i);
        const char *lookup_name = name;

        if (data->view == DOCKER_VIEW_VOLUMES && (name[0] == '*' || name[0] == ' ')
            && name[1] != '\0')
            lookup_name = name + 1;

        if (strcmp (item->name, lookup_name) == 0)
            return item;
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
run_cmd (const char *cmd, char **output, char **err_text)
{
    if (output != NULL)
        *output = NULL;
    if (err_text != NULL)
        *err_text = NULL;

    g_ptr_array_add (stub_commands, g_strdup (cmd));

    if (strcmp (cmd, docker_images_cmd) == 0)
    {
        if (output != NULL)
            *output = g_strdup (stub_images_output);
        return TRUE;
    }

    if (strcmp (cmd, docker_networks_cmd) == 0)
    {
        if (output != NULL)
            *output = g_strdup (stub_networks_output);
        return TRUE;
    }

    if (strcmp (cmd, docker_volumes_cmd) == 0)
    {
        if (output != NULL)
            *output = g_strdup (stub_volumes_output);
        return TRUE;
    }

    if (strcmp (cmd, docker_ps_cmd) == 0)
    {
        if (output != NULL)
            *output = g_strdup (stub_ps_output);
        return TRUE;
    }

    if (g_str_has_prefix (cmd, docker_mounts_inspect_prefix))
    {
        if (output != NULL)
            *output = g_strdup (stub_mounts_output);
        return TRUE;
    }

    if (g_str_has_prefix (cmd, docker_rmi_prefix)
        || g_str_has_prefix (cmd, docker_network_rm_prefix)
        || g_str_has_prefix (cmd, docker_volume_rm_prefix))
    {
        if (output != NULL)
            *output = g_strdup ("");
        return TRUE;
    }

    if (err_text != NULL)
        *err_text = g_strdup ("unexpected command");
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

void
message (int flags, const char *title, const char *text, ...)
{
    (void) flags;
    (void) title;
    (void) text;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_conn_run (const docker_connection_t *conn, const char *docker_args, char **output,
                 char **err_text)
{
    char *cmd;
    gboolean ok;

    (void) conn;
    cmd = g_strdup_printf ("docker %s", docker_args);
    ok = run_cmd (cmd, output, err_text);
    g_free (cmd);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_conn_capture_inspect (const docker_connection_t *conn, const char *obj_id,
                             const char *format)
{
    char *quoted_id;
    char *quoted_format;
    char *docker_args;
    char *output = NULL;

    (void) conn;
    quoted_id = g_shell_quote (obj_id);
    quoted_format = g_shell_quote (format);
    docker_args = g_strdup_printf ("inspect --format %s %s", quoted_format, quoted_id);
    docker_conn_run (NULL, docker_args, &output, NULL);
    g_free (docker_args);
    g_free (quoted_id);
    g_free (quoted_format);
    return output;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_ui_view_volume_summary (const docker_volume_summary_t *summary)
{
    (void) summary;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
write_temp_content (const char *prefix, const char *content, char **local_path)
{
    (void) prefix;
    (void) content;
    (void) local_path;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    stub_images_output = NULL;
    stub_networks_output = NULL;
    stub_volumes_output = NULL;
    stub_ps_output = NULL;
    stub_mounts_output = NULL;
    stub_commands = g_ptr_array_new_with_free_func (g_free);
}

/* @After */
static void
teardown (void)
{
    g_ptr_array_free (stub_commands, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
free_data_items (docker_data_t *data)
{
    if (data->items != NULL)
    {
        g_ptr_array_free (data->items, TRUE);
        data->items = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_images_reload_parses_sizes)
{
    docker_data_t data = { 0 };
    docker_item_t *first;
    docker_item_t *second;

    stub_images_output = "sha256:111\trepo:tag\t12.3MB\nsha256:222\tbusybox:latest\t4KiB\n";

    ck_assert (docker_images_reload (&data, NULL));
    ck_assert_ptr_nonnull (data.items);
    ck_assert_uint_eq (data.items->len, 2);

    first = (docker_item_t *) g_ptr_array_index (data.items, 0);
    second = (docker_item_t *) g_ptr_array_index (data.items, 1);

    ck_assert_str_eq (first->id, "sha256:111");
    ck_assert_str_eq (first->name, "repo:tag");
    ck_assert_int_eq ((int) first->size, 12300000);

    ck_assert_str_eq (second->name, "busybox:latest");
    ck_assert_int_eq ((int) second->size, 4096);

    free_data_items (&data);
}
END_TEST

START_TEST (test_networks_reload_formats_driver_and_scope)
{
    docker_data_t data = { 0 };
    docker_item_t *item;

    stub_networks_output = "nid123\tbridge\tbridge\tlocal\n";

    ck_assert (docker_networks_reload (&data, NULL));
    ck_assert_ptr_nonnull (data.items);
    ck_assert_uint_eq (data.items->len, 1);

    item = (docker_item_t *) g_ptr_array_index (data.items, 0);
    ck_assert_str_eq (item->id, "nid123");
    ck_assert_str_eq (item->name, "bridge (bridge/local)");

    free_data_items (&data);
}
END_TEST

START_TEST (test_volumes_reload_marks_in_use_and_exposes_columns)
{
    docker_data_t data = { 0 };
    docker_item_t *used;
    docker_item_t *unused;

    data.view = DOCKER_VIEW_VOLUMES;
    stub_volumes_output = "vol1\tlocal\tlocal\t\n"
                          "vol2\tlocal\tlocal\t\n";
    stub_ps_output = "cid1\ncid2\n";
    stub_mounts_output = "/ctr1 volume:vol1 \n"
                         "/ctr2 bind: \n";

    ck_assert (docker_volumes_reload (&data, NULL));
    ck_assert_ptr_nonnull (data.items);
    ck_assert_uint_eq (data.items->len, 2);

    used = (docker_item_t *) g_ptr_array_index (data.items, 0);
    unused = (docker_item_t *) g_ptr_array_index (data.items, 1);

    ck_assert_str_eq (used->name, "vol1");
    ck_assert_str_eq (used->status, "in use");
    ck_assert_str_eq (docker_volumes_get_column_value (&data, "vol1", "state"), "U");
    ck_assert_str_eq (docker_volumes_get_column_value (&data, "vol1", "scope"), "local");

    ck_assert_str_eq (unused->name, "vol2");
    ck_assert_str_eq (unused->status, "unused");
    ck_assert_str_eq (docker_volumes_get_column_value (&data, "vol2", "state"), "N");
    ck_assert_str_eq (docker_volumes_get_default_format (), "name | state:1 | scope:5");

    free_data_items (&data);
}
END_TEST

START_TEST (test_images_delete_uses_item_id)
{
    docker_data_t data = { 0 };
    docker_item_t *item;
    const char *names[] = { "repo:tag" };
    const char *cmd;

    data.items = g_ptr_array_new_with_free_func (docker_item_free);
    item = g_new0 (docker_item_t, 1);
    item->id = g_strdup ("sha256:abc");
    item->name = g_strdup ("repo:tag");
    g_ptr_array_add (data.items, item);

    ck_assert_int_eq (docker_images_delete_items (&data, names, 1), MC_PPR_OK);
    ck_assert_uint_eq (stub_commands->len, 1);

    cmd = (const char *) g_ptr_array_index (stub_commands, 0);
    ck_assert_ptr_nonnull (strstr (cmd, "docker rmi "));
    ck_assert_ptr_nonnull (strstr (cmd, "sha256:abc"));

    free_data_items (&data);
}
END_TEST

START_TEST (test_networks_delete_uses_item_id)
{
    docker_data_t data = { 0 };
    docker_item_t *item;
    const char *names[] = { "bridge (bridge/local)" };
    const char *cmd;

    data.items = g_ptr_array_new_with_free_func (docker_item_free);
    item = g_new0 (docker_item_t, 1);
    item->id = g_strdup ("net123");
    item->name = g_strdup ("bridge (bridge/local)");
    g_ptr_array_add (data.items, item);

    ck_assert_int_eq (docker_networks_delete_items (&data, names, 1), MC_PPR_OK);
    ck_assert_uint_eq (stub_commands->len, 1);

    cmd = (const char *) g_ptr_array_index (stub_commands, 0);
    ck_assert_ptr_nonnull (strstr (cmd, "docker network rm "));
    ck_assert_ptr_nonnull (strstr (cmd, "net123"));

    free_data_items (&data);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc;

    tc = tcase_create ("docker-domains");
    tcase_set_timeout (tc, 10);

    tcase_add_checked_fixture (tc, setup, teardown);

    tcase_add_test (tc, test_images_reload_parses_sizes);
    tcase_add_test (tc, test_networks_reload_formats_driver_and_scope);
    tcase_add_test (tc, test_volumes_reload_marks_in_use_and_exposes_columns);
    tcase_add_test (tc, test_images_delete_uses_item_id);
    tcase_add_test (tc, test_networks_delete_uses_item_id);

    return mctest_run_all (tc);
}

/* --------------------------------------------------------------------------------------------- */
