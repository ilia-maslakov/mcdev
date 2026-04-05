/*
   tests for docker panel path restore helpers

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026.

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

#define TEST_SUITE_NAME "/src/panel-plugins/docker/paths"

#include "tests/mctest.h"

#include <string.h>

#include "src/filemanager/panel.h"
#include "src/panel-plugins/docker/docker-internal.h"

static gboolean stub_resolve_current_result;
static int stub_resolve_current_called;
static int stub_reload_details_called;
static int stub_reload_files_called;
static int stub_reload_mounts_called;
static int stub_host_message_called;
static const char *stub_host_message_text;

/* Source-under-test. */
#include "src/panel-plugins/docker/docker.c"

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_ui_view_container_summary (const docker_container_summary_t *summary)
{
    (void) summary;
    return TRUE;
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
docker_ui_viewer_command (const char *cmd)
{
    (void) cmd;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_ui_show_create_container_dialog (char **image, char **name, char **command, gboolean *detach)
{
    (void) image;
    (void) name;
    (void) command;
    (void) detach;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_is_ungrouped_project (const char *project)
{
    return (project == NULL || *project == '\0' || strcmp (project, docker_ungrouped_project) == 0);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_reload_projects (docker_data_t *data, char **err_text)
{
    (void) err_text;
    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_reload_items (docker_data_t *data, char **err_text)
{
    (void) err_text;
    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_resolve_current (docker_data_t *data, char **err_text)
{
    (void) err_text;
    stub_resolve_current_called++;

    if (stub_resolve_current_result)
    {
        if (data->current_container_id == NULL)
            data->current_container_id = g_strdup ("cid-1");
        return TRUE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_reload_details (docker_data_t *data)
{
    stub_reload_details_called++;
    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_containers_enter (docker_data_t *data, const char *name)
{
    (void) data;
    (void) name;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_view_summary (docker_data_t *data, const char *fname)
{
    (void) data;
    (void) fname;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_containers_view_logs (docker_data_t *data, const char *fname)
{
    (void) data;
    (void) fname;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_containers_get_local_copy (docker_data_t *data, const char *fname, char **local_path)
{
    (void) data;
    (void) fname;
    (void) local_path;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_containers_delete_items (docker_data_t *data, const char **names, int count)
{
    (void) data;
    (void) names;
    (void) count;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_containers_create_item (docker_data_t *data)
{
    (void) data;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

const mc_panel_column_t *
docker_containers_get_columns (size_t *count)
{
    if (count != NULL)
        *count = 0;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_containers_get_column_value (docker_data_t *data, const char *fname, const char *column_id)
{
    (void) data;
    (void) fname;
    (void) column_id;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_containers_get_default_format (void)
{
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_container_files_reload (docker_data_t *data, char **err_text)
{
    (void) err_text;
    stub_reload_files_called++;
    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_container_mounts_reload (docker_data_t *data, char **err_text)
{
    (void) err_text;
    stub_reload_mounts_called++;
    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_container_files_chdir (docker_data_t *data, const char *path)
{
    (void) data;
    (void) path;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_container_files_enter_mounts (docker_data_t *data, const char *name)
{
    (void) data;
    (void) name;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_container_files_get_local_copy (docker_data_t *data, const char *fname, char **local_path)
{
    (void) data;
    (void) fname;
    (void) local_path;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_container_files_get_default_format (docker_data_t *data)
{
    (void) data;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_images_reload (docker_data_t *data, char **err_text)
{
    (void) err_text;
    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_images_delete_items (docker_data_t *data, const char **names, int count)
{
    (void) data;
    (void) names;
    (void) count;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_volumes_reload (docker_data_t *data, char **err_text)
{
    (void) err_text;
    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_volumes_view_summary (docker_data_t *data, const char *fname)
{
    (void) data;
    (void) fname;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_volumes_delete_items (docker_data_t *data, const char **names, int count)
{
    (void) data;
    (void) names;
    (void) count;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

const mc_panel_column_t *
docker_volumes_get_columns (size_t *count)
{
    if (count != NULL)
        *count = 0;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_volumes_get_column_value (docker_data_t *data, const char *fname, const char *column_id)
{
    (void) data;
    (void) fname;
    (void) column_id;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
docker_volumes_get_default_format (void)
{
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_networks_reload (docker_data_t *data, char **err_text)
{
    (void) err_text;
    data->items = g_ptr_array_new_with_free_func (docker_item_free);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_pp_result_t
docker_networks_delete_items (docker_data_t *data, const char **names, int count)
{
    (void) data;
    (void) names;
    (void) count;
    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */

static void
mock_host_message (mc_panel_host_t *host, int flags, const char *title, const char *text)
{
    (void) host;
    (void) flags;
    (void) title;
    stub_host_message_called++;
    stub_host_message_text = text;
}

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    stub_resolve_current_result = TRUE;
    stub_resolve_current_called = 0;
    stub_reload_details_called = 0;
    stub_reload_files_called = 0;
    stub_reload_mounts_called = 0;
    stub_host_message_called = 0;
    stub_host_message_text = NULL;
}

/* @After */
static void
teardown (void)
{
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_open_path_restores_files_view_state)
{
    docker_data_t *data;

    data = (docker_data_t *) docker_open (NULL, "docker:/containers/proj/app/files/etc");

    ck_assert_ptr_nonnull (data);
    ck_assert_int_eq (data->view, DOCKER_VIEW_CONTAINER_FILES);
    ck_assert_str_eq (data->current_project, "proj");
    ck_assert_str_eq (data->current_container_name, "app");
    ck_assert_str_eq (data->files_cwd, "/etc");
    ck_assert_str_eq (docker_get_path (data), "/containers/proj/app/files/etc");

    docker_close (data);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_open_path_restores_mounts_view_state)
{
    docker_data_t *data;

    data = (docker_data_t *) docker_open (NULL, "docker:/containers/proj/app/mounts");

    ck_assert_ptr_nonnull (data);
    ck_assert_int_eq (data->view, DOCKER_VIEW_CONTAINER_MOUNTS);
    ck_assert_str_eq (data->current_project, "proj");
    ck_assert_str_eq (data->current_container_name, "app");
    ck_assert_str_eq (docker_get_path (data), "/containers/proj/app/mounts");

    docker_close (data);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_open_path_keeps_files_root_without_trailing_slash)
{
    docker_data_t *data;

    data = (docker_data_t *) docker_open (NULL, "docker:/containers/proj/app/files/");

    ck_assert_ptr_nonnull (data);
    ck_assert_int_eq (data->view, DOCKER_VIEW_CONTAINER_FILES);
    ck_assert_str_eq (data->files_cwd, "/");
    ck_assert_str_eq (docker_get_path (data), "/containers/proj/app/files");

    docker_close (data);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_open_path_accepts_redundant_slashes_and_missing_leading_slash)
{
    docker_data_t *data;

    data = (docker_data_t *) docker_open (NULL, "docker:////containers/proj/app/mounts/");

    ck_assert_ptr_nonnull (data);
    ck_assert_int_eq (data->view, DOCKER_VIEW_CONTAINER_MOUNTS);
    ck_assert_str_eq (data->current_project, "proj");
    ck_assert_str_eq (data->current_container_name, "app");
    ck_assert_str_eq (docker_get_path (data), "/containers/proj/app/mounts");

    docker_close (data);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_reload_items_resolves_container_before_details)
{
    docker_data_t *data;

    data = (docker_data_t *) docker_open (NULL, "docker:/containers/proj/app");

    ck_assert_ptr_nonnull (data);
    ck_assert (reload_items (data));
    ck_assert_int_eq (stub_resolve_current_called, 1);
    ck_assert_int_eq (stub_reload_details_called, 1);
    ck_assert_str_eq (data->current_container_id, "cid-1");

    docker_close (data);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_open_path_rejects_invalid_plugin_path)
{
    docker_data_t *data;
    mc_panel_host_t host = { 0 };

    host.message = mock_host_message;

    data = (docker_data_t *) docker_open (&host, "docker:/broken/path");

    ck_assert_ptr_null (data);
    ck_assert_int_eq (stub_host_message_called, 1);
    ck_assert_str_eq (stub_host_message_text, "Cannot reopen Docker panel path.");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_open_root_path_without_list)
{
    docker_data_t *data;

    data = (docker_data_t *) docker_open (NULL, "docker:/");

    ck_assert_ptr_nonnull (data);
    ck_assert_int_eq (data->view, DOCKER_VIEW_ROOT);
    docker_close (data);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");
    tcase_add_checked_fixture (tc_core, setup, teardown);

    tcase_add_test (tc_core, test_open_path_restores_files_view_state);
    tcase_add_test (tc_core, test_open_path_restores_mounts_view_state);
    tcase_add_test (tc_core, test_open_path_keeps_files_root_without_trailing_slash);
    tcase_add_test (tc_core, test_open_path_accepts_redundant_slashes_and_missing_leading_slash);
    tcase_add_test (tc_core, test_reload_items_resolves_container_before_details);
    tcase_add_test (tc_core, test_open_path_rejects_invalid_plugin_path);
    tcase_add_test (tc_core, test_open_root_path_without_list);

    return mctest_run_all (tc_core);
}

void
panel_directory_history_add_path (WPanel *panel, const char *path)
{
    (void) panel;
    (void) path;
}
