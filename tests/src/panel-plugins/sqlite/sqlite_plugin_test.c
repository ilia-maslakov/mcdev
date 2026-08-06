/*
   SQLite panel plugin tests.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.
 */

#define TEST_SUITE_NAME "/src/panel-plugins/sqlite"

#include "tests/mctest.h"

#include <string.h>
#include <unistd.h>

#include <sqlite3.h>

#include "lib/panel-plugin.h"
#include "src/filemanager/dir.h"
#include "src/panel-plugins/sqlite/sqlite_json.h"
#include "src/viewer/mcviewer.h"

const mc_panel_plugin_t *mc_panel_plugin_register (void);

/* The plugin's F3 handler calls the real viewer in MC.  These model tests do
   not open a terminal UI, so they provide that one boundary as a no-op. */
gboolean
mcview_viewer (const char *command, const vfs_path_t *file_vpath, int start_line,
               off_t search_start, off_t search_end)
{
    (void) command;
    (void) file_vpath;
    (void) start_line;
    (void) search_start;
    (void) search_end;
    return TRUE;
}

static void
sqlite_test_list_clear (dir_list *list)
{
    int i;

    for (i = 0; i < list->len; i++)
    {
        g_string_free (list->list[i].fname, TRUE);
        g_free (list->list[i].name_sort_key);
        g_free (list->list[i].extension_sort_key);
    }
    g_free (list->list);
    memset (list, 0, sizeof (*list));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_test_list_has (const dir_list *list, const char *name)
{
    int i;

    for (i = 0; i < list->len; i++)
        if (strcmp (list->list[i].fname->str, name) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
sqlite_test_create_database (void)
{
    GError *error = NULL;
    char *path = NULL;
    sqlite3 *db = NULL;
    int fd;
    int rc;

    fd = g_file_open_tmp ("mc-sqlite-test-XXXXXX", &path, &error);
    ck_assert_int_ne (fd, -1);
    close (fd);

    rc = sqlite3_open (path, &db);
    ck_assert_int_eq (rc, SQLITE_OK);
    rc = sqlite3_exec (db,
                       "CREATE TABLE contacts (id INTEGER PRIMARY KEY, name TEXT, photo BLOB, "
                       "content TEXT);"
                       "INSERT INTO contacts (name, photo, content) VALUES "
                       "('Ada', X'00ff', '{\"IndexText\":{\"LineStart\":0}}');"
                       "INSERT INTO contacts (name, photo, content) VALUES ('Grace', NULL, "
                       "'plain text');",
                       NULL, NULL, NULL);
    ck_assert_int_eq (rc, SQLITE_OK);
    sqlite3_close (db);
    g_clear_error (&error);

    return path;
}

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_browse_database_and_view_row)
{
    const mc_panel_plugin_t *plugin;
    void *data;
    dir_list list = { 0 };
    char *db_path;
    char *local_path = NULL;
    char *content = NULL;
    char *location;
    void *restored;

    db_path = sqlite_test_create_database ();
    plugin = mc_panel_plugin_register ();
    data = plugin->open (NULL, db_path);
    mctest_assert_not_null (data);

    ck_assert_int_eq (plugin->get_items (data, &list), MC_PPR_OK);
    mctest_assert_true (sqlite_test_list_has (&list, "contacts"));
    sqlite_test_list_clear (&list);

    ck_assert_int_eq (plugin->chdir (data, "contacts"), MC_PPR_OK);
    ck_assert_int_eq (plugin->get_items (data, &list), MC_PPR_OK);
    mctest_assert_true (sqlite_test_list_has (&list, "schema.sql"));
    mctest_assert_true (sqlite_test_list_has (&list, "rows-000000000001-000000000002"));
    sqlite_test_list_clear (&list);

    ck_assert_int_eq (plugin->chdir (data, "rows-000000000001-000000000002"), MC_PPR_OK);
    ck_assert_int_eq (plugin->get_items (data, &list), MC_PPR_OK);
    mctest_assert_true (sqlite_test_list_has (&list, "row-000000000001.json"));
    sqlite_test_list_clear (&list);

    ck_assert_int_eq (plugin->chdir (data, ".."), MC_PPR_OK);
    mctest_assert_str_eq (plugin->get_focus_name (data), "rows-000000000001-000000000002");
    ck_assert_int_eq (plugin->chdir (data, ".."), MC_PPR_OK);
    mctest_assert_str_eq (plugin->get_focus_name (data), "contacts");

    ck_assert_int_eq (plugin->chdir (data, "contacts"), MC_PPR_OK);
    ck_assert_int_eq (plugin->chdir (data, "rows-000000000001-000000000002"), MC_PPR_OK);
    ck_assert_int_eq (plugin->get_local_copy (data, "row-000000000001.json", &local_path),
                      MC_PPR_OK);
    mctest_assert_true (g_file_get_contents (local_path, &content, NULL, NULL));
    ck_assert_ptr_ne (strstr (content, "\"name\": \"Ada\""), NULL);
    ck_assert_ptr_ne (strstr (content, "\"$blob\": \"00ff\""), NULL);
    ck_assert_ptr_ne (strstr (content, "\\\"IndexText\\\""), NULL);

    location = plugin->get_location (data);
    mctest_assert_not_null (location);
    restored = plugin->open (NULL, location);
    mctest_assert_not_null (restored);
    plugin->close (restored);

    unlink (local_path);
    g_free (local_path);
    g_free (content);
    g_free (location);
    plugin->close (data);
    unlink (db_path);
    g_free (db_path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_pretty_embedded_json)
{
    const char input[] = "{\"IndexText\":{\"LineStart\":0},\"IndexContent\":\"\\n\"}";
    const char expected[] = "{\n"
                            "    \"IndexText\": {\n"
                            "      \"LineStart\": 0\n"
                            "    },\n"
                            "    \"IndexContent\": \"\\n\"\n"
                            "  }";
    char *pretty;

    pretty = sqlite_json_pretty (input, strlen (input), 2);
    mctest_assert_not_null (pretty);
    ck_assert_str_eq (pretty, expected);
    g_free (pretty);

    mctest_assert_null (sqlite_json_pretty ("{\"missing\": ]", strlen ("{\"missing\": ]"), 2));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_non_database_is_declined)
{
    const mc_panel_plugin_t *plugin;
    GError *error = NULL;
    char *path = NULL;
    int fd;

    fd = g_file_open_tmp ("mc-sqlite-test-XXXXXX", &path, &error);
    ck_assert_int_ne (fd, -1);
    close (fd);

    plugin = mc_panel_plugin_register ();
    mctest_assert_null (plugin->open (NULL, path));

    unlink (path);
    g_free (path);
    g_clear_error (&error);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_closing_database_focuses_its_file)
{
    const mc_panel_plugin_t *plugin;
    mc_panel_host_t host = { 0 };
    void *data;
    char *db_path;
    char *base;

    db_path = sqlite_test_create_database ();
    base = g_path_get_basename (db_path);
    plugin = mc_panel_plugin_register ();
    data = plugin->open (&host, db_path);
    mctest_assert_not_null (data);

    ck_assert_int_eq (plugin->chdir (data, ".."), MC_PPR_CLOSE);
    mctest_assert_str_eq (host.focus_after, base);

    plugin->close (data);
    g_free (host.focus_after);
    g_free (base);
    unlink (db_path);
    g_free (db_path);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");
    tcase_add_test (tc_core, test_browse_database_and_view_row);
    tcase_add_test (tc_core, test_pretty_embedded_json);
    tcase_add_test (tc_core, test_non_database_is_declined);
    tcase_add_test (tc_core, test_closing_database_focuses_its_file);

    return mctest_run_all (tc_core);
}
