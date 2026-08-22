/*
   lib - tests for the Lua runtime extension

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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define TEST_SUITE_NAME "/lib"

#include "tests/mctest.h"

#include <stdlib.h>
#include <string.h>

#include <glib/gstdio.h>

#include "lib/event.h"
#include "lib/extension-runtime.h"
#include "lib/runtime-events.h"

/*** global variables ****************************************************************************/

static GError *error = NULL;
static char *test_root = NULL;
static char *config_dir = NULL;
static char *system_scripts_dir = NULL;
static char *user_scripts_dir = NULL;
static char *system_mc_scripts_dir = NULL;
static char *user_mc_scripts_dir = NULL;
static char *system_editor_scripts_dir = NULL;
static char *user_editor_scripts_dir = NULL;
static char *output_path = NULL;
static char *ui_status_text = NULL;
static char *ui_message_title = NULL;
static char *ui_message_text = NULL;
static char *object_panel_chdir_path = NULL;
static char *object_editor_insert_text = NULL;
static guint object_panel_refreshes = 0;
static guint64 object_editor_line = 0;
static guint64 object_editor_column = 0;
static gint64 object_viewer_offset = 0;
static guint enumerated_lua_packages = 0;
static gboolean enumerated_disabled_beta = FALSE;
static guint enumerated_lua_editor_packages = 0;
static guint enumerated_disabled_lua_editor_packages = 0;
static gboolean enumerated_lua_editor_global = FALSE;
static gboolean enumerated_lua_editor_user = FALSE;

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

static void
remove_tree (const char *path)
{
    GDir *directory;
    const char *entry;

    directory = g_dir_open (path, 0, NULL);
    if (directory == NULL)
    {
        (void) g_remove (path);
        return;
    }

    while ((entry = g_dir_read_name (directory)) != NULL)
    {
        char *child = g_build_filename (path, entry, (char *) NULL);

        if (g_file_test (child, G_FILE_TEST_IS_DIR))
            remove_tree (child);
        else
            (void) g_remove (child);
        g_free (child);
    }

    g_dir_close (directory);
    (void) g_rmdir (path);
}

/* --------------------------------------------------------------------------------------------- */

static void
write_file (const char *path, const char *contents)
{
    mctest_assert_true (g_file_set_contents (path, contents, -1, &error));
    g_clear_error (&error);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_script (const char *parent, const char *id, const char *label, gboolean unregister)
{
    char *root;
    char *library_dir;
    char *ini_path;
    char *entry_path;
    char *module_path;
    char *ini;
    char *script;
    char *module;

    root = g_build_filename (parent, id, (char *) NULL);
    library_dir = g_build_filename (root, "lib", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (library_dir, 0700), 0);

    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    module_path = g_build_filename (library_dir, "format.lua", (char *) NULL);
    ini = g_strdup_printf ("[Lua]\nid=%s\napi_version=1\nname=%s\nentry=init.lua\n", id, id);

    if (unregister)
        script = g_strdup_printf ("local token = mc.on(\"startup\", function (ev)\n"
                                  "    local stream = assert(io.open(\"%s\", \"a\"))\n"
                                  "    stream:write(\"%s:\" .. ev.run_mode .. \"\\n\")\n"
                                  "    stream:close()\n"
                                  "end)\n"
                                  "assert(mc.off(token))\n",
                                  output_path, label);
    else
        script = g_strdup_printf ("local label = require(\"format\")\n"
                                  "mc.on(\"startup\", function (ev)\n"
                                  "    local stream = assert(io.open(\"%s\", \"a\"))\n"
                                  "    stream:write(label .. \":\" .. ev.run_mode .. \"\\n\")\n"
                                  "    stream:close()\n"
                                  "end)\n",
                                  output_path);

    write_file (ini_path, ini);
    write_file (entry_path, script);
    module = g_strdup_printf ("return \"%s\"\n", label);
    write_file (module_path, module);

    g_free (module);
    g_free (script);
    g_free (ini);
    g_free (module_path);
    g_free (entry_path);
    g_free (ini_path);
    g_free (library_dir);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_editor_script (const char *parent, const char *id)
{
    char *root;
    char *ini_path;
    char *entry_path;
    char *ini;

    root = g_build_filename (parent, id, (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    ini = g_strdup_printf ("[Lua]\nid=%s\napi_version=1\nname=%s\nentry=init.lua\n", id, id);
    write_file (ini_path, ini);
    write_file (entry_path, "mc.on(\"editor.save\", function (ev) end)\n");

    g_free (ini);
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_test_packages (void)
{
    (void) g_remove (output_path);

    create_script (system_mc_scripts_dir, "alpha", "system-alpha", FALSE);
    create_script (user_mc_scripts_dir, "alpha", "user-alpha", FALSE);
    create_script (user_mc_scripts_dir, "beta", "user-beta", FALSE);
    create_script (user_mc_scripts_dir, "off", "off", TRUE);
    create_editor_script (system_editor_scripts_dir, "editor-global");
    create_editor_script (user_editor_scripts_dir, "editor-user");
}

/* --------------------------------------------------------------------------------------------- */

static void
create_event_shape_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;
    char *script;

    root = g_build_filename (user_mc_scripts_dir, "event-shapes", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=event-shapes\n"
                "api_version=1\n"
                "name=Event snapshot checks\n"
                "entry=init.lua\n");

    script = g_strdup_printf (
        "if os.getenv(\"MC_LUA_TEST_EVENT_SHAPES\") ~= \"1\" then return end\n"
        "local function record(value)\n"
        "    local stream = assert(io.open(\"%s\", \"a\"))\n"
        "    stream:write(value .. \"\\n\")\n"
        "    stream:close()\n"
        "end\n"
        "mc.on(\"shutdown\", function(ev)\n"
        "    assert(ev.reason == \"quit\")\n"
        "    record(\"shutdown\")\n"
        "end)\n"
        "mc.on(\"panel.chdir\", function(ev)\n"
        "    assert(type(ev.panel) == \"userdata\")\n"
        "    assert(ev.old_path == \"/old\" and ev.new_path == \"/new\" and ev.cause == \"user\")\n"
        "    record(\"chdir\")\n"
        "end)\n"
        "mc.on(\"panel.selection_changed\", function(ev)\n"
        "    assert(type(ev.panel) == \"userdata\" and ev.current.name == \"chosen\")\n"
        "    assert(ev.selected_count == 1 and not ev.selected_truncated and #ev.selected == 1)\n"
        "    assert(ev.selected[1].path == \"/new/marked\" and ev.selected[1].marked)\n"
        "    record(\"selection\")\n"
        "end)\n"
        "mc.on(\"panel.file_open\", function(ev)\n"
        "    assert(type(ev.panel) == \"userdata\" and ev.path == \"/new/file\")\n"
        "    assert(ev.open_mode == \"view\" and not ev.is_dir)\n"
        "    record(\"open\")\n"
        "end)\n"
        "mc.on(\"editor.open\", function(ev)\n"
        "    assert(type(ev.editor) == \"userdata\" and ev.path == \"/new/edit\")\n"
        "    assert(ev.readonly and ev.line == 4 and ev.column == 9)\n"
        "    record(\"editor-open\")\n"
        "end)\n"
        "mc.on(\"editor.save\", function(ev)\n"
        "    assert(type(ev.editor) == \"userdata\" and ev.path == \"/new/edit\")\n"
        "    assert(ev.previous_path == \"/old/edit\" and ev.save_as)\n"
        "    record(\"editor-save\")\n"
        "end)\n"
        "mc.on(\"editor.key\", function(ev)\n"
        "    assert(type(ev.editor) == \"userdata\" and ev.key.name == \"Ctrl-S\")\n"
        "    assert(ev.key.code == 19 and ev.key.text == nil and ev.key.modifiers.ctrl)\n"
        "    record(\"editor-key\")\n"
        "    return mc.CONSUME\n"
        "end)\n"
        "mc.on(\"viewer.open\", function(ev)\n"
        "    assert(type(ev.viewer) == \"userdata\" and ev.path == \"/new/view\")\n"
        "    assert(ev.source_kind == \"file\" and ev.start_line == 7)\n"
        "    record(\"viewer-open\")\n"
        "end)\n",
        output_path);
    write_file (entry_path, script);

    g_free (script);
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_ui_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;

    root = g_build_filename (user_mc_scripts_dir, "ui-test", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=ui-test\n"
                "api_version=1\n"
                "name=UI host service check\n"
                "entry=init.lua\n");
    write_file (entry_path,
                "if os.getenv(\"MC_LUA_TEST_UI\") ~= \"1\" then return end\n"
                "mc.on(\"startup\", function()\n"
                "    assert(mc.ui.status(\"Lua status\"))\n"
                "    assert(mc.ui.message(\"Lua title\", \"Lua message\"))\n"
                "end)\n");

    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_error_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;

    root = g_build_filename (user_mc_scripts_dir, "error-test", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=error-test\n"
                "api_version=1\n"
                "name=Error boundary check\n"
                "entry=init.lua\n");
    write_file (entry_path,
                "if os.getenv(\"MC_LUA_TEST_ERROR\") ~= \"1\" then return end\n"
                "mc.on(\"startup\", function() error(\"expected test error\") end)\n");

    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_object_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;

    root = g_build_filename (user_mc_scripts_dir, "object-test", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=object-test\n"
                "api_version=1\n"
                "name=Object API checks\n"
                "entry=init.lua\n");
    write_file (entry_path,
                "if os.getenv(\"MC_LUA_TEST_OBJECTS\") ~= \"1\" then return end\n"
                "local unavailable, unavailable_error = mc.panel.active()\n"
                "assert(unavailable == nil and unavailable_error == \"no active MC context\")\n"
                "mc.on(\"startup\", function()\n"
                "    local panel = assert(mc.panel.active())\n"
                "    assert(panel:cwd() == \"/panel\")\n"
                "    assert(panel:current().name == \"current\")\n"
                "    assert(#panel:selected() == 1 and panel:selected()[1].name == \"marked\")\n"
                "    assert(panel:refresh())\n"
                "    assert(panel:chdir(\"/other\"))\n"
                "    local editor = assert(mc.editor.current())\n"
                "    assert(editor:path() == \"/editor\")\n"
                "    local line, column = editor:cursor()\n"
                "    assert(line == 2 and column == 3 and not editor:is_readonly())\n"
                "    assert(editor:get_text(1, 4) == \"text\")\n"
                "    assert(editor:selected_text() == \"U2V0\")\n"
                "    assert(editor:set_cursor(3, 4))\n"
                "    assert(editor:insert(\"!\"))\n"
                "    assert(editor:save())\n"
                "    local viewer = assert(mc.viewer.current())\n"
                "    assert(viewer:path() == \"/viewer\")\n"
                "    assert(viewer:position() == 12 and viewer:mode() == \"text\")\n"
                "    assert(viewer[\"goto\"](viewer, 17))\n"
                "end)\n"
                "mc.on(\"panel.chdir\", function(ev)\n"
                "    local path, closed_error = ev.panel:cwd()\n"
                "    assert(path == nil and closed_error == \"closed\")\n"
                "end)\n"
                "mc.on(\"panel.file_open\", function(ev)\n"
                "    local ok, phase_error = ev.panel:refresh()\n"
                "    assert(ok == nil and phase_error == \"forbidden_in_phase\")\n"
                "    assert(mc.ui.status(\"phase status\"))\n"
                "end)\n");

    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static void
create_macro_script (void)
{
    char *root;
    char *ini_path;
    char *entry_path;
    char *script;

    root = g_build_filename (user_editor_scripts_dir, "macro-test", (char *) NULL);
    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    entry_path = g_build_filename (root, "init.lua", (char *) NULL);
    write_file (ini_path,
                "[Lua]\n"
                "id=macro-test\n"
                "api_version=1\n"
                "name=Macro API checks\n"
                "entry=init.lua\n"
                "provides=macros\n");

    script =
        g_strdup_printf ("if os.getenv(\"MC_LUA_TEST_MACRO\") ~= \"1\" then return end\n"
                         "local function record(value)\n"
                         "    local stream = assert(io.open(\"%s\", \"a\"))\n"
                         "    stream:write(value .. \"\\n\")\n"
                         "    stream:close()\n"
                         "end\n"
                         "assert(mc.macro {\n"
                         "    id = \"consume-f11\",\n"
                         "    area = \"editor\",\n"
                         "    key = \"F11\",\n"
                         "    description = \"Consume F11\",\n"
                         "    action = function(ev)\n"
                         "        assert(ev.name == \"editor.key\" and ev.key.name == \"F11\")\n"
                         "        assert(type(ev.editor) == \"userdata\" and "
                         "ev.editor:selected_text() == \"U2V0\")\n"
                         "        record(\"macro-consume\")\n"
                         "        return mc.CONSUME\n"
                         "    end,\n"
                         "})\n"
                         "assert(mc.macro {\n"
                         "    id = \"pass-f10\",\n"
                         "    area = \"editor\",\n"
                         "    key = \"F10\",\n"
                         "    description = \"Pass F10\",\n"
                         "    action = function()\n"
                         "        record(\"macro-pass\")\n"
                         "        return mc.PASS\n"
                         "    end,\n"
                         "})\n",
                         output_path);
    write_file (entry_path, script);

    g_free (script);
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_ui_status (const char *text)
{
    g_free (ui_status_text);
    ui_status_text = g_strdup (text);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_ui_message (const char *title, const char *text)
{
    g_free (ui_message_title);
    g_free (ui_message_text);
    ui_message_title = g_strdup (title);
    ui_message_text = g_strdup (text);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_runtime_log (const char *source, const char *level, const char *message)
{
    (void) source;
    (void) level;
    (void) message;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_enumerate_lua_package (const char *runtime_name, const char *id, const char *display_name,
                            gboolean enabled, gpointer user_data)
{
    (void) display_name;
    (void) user_data;

    if (g_strcmp0 (runtime_name, "lua") != 0)
        return;

    enumerated_lua_packages++;
    if (g_strcmp0 (id, "beta") == 0 && !enabled)
        enumerated_disabled_beta = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_enumerate_lua_package_details (const char *runtime_name, const char *id,
                                    const char *display_name, const char *workspace,
                                    const char *origin, const char *directory, gboolean enabled,
                                    gpointer user_data)
{
    (void) display_name;
    (void) user_data;

    if (g_strcmp0 (runtime_name, "lua") != 0 || g_strcmp0 (workspace, "mcedit") != 0)
        return;

    enumerated_lua_editor_packages++;
    if (!enabled)
        enumerated_disabled_lua_editor_packages++;
    if (g_strcmp0 (id, "editor-global") == 0)
    {
        ck_assert_str_eq (origin, "global");
        ck_assert (g_str_has_suffix (directory, "/editor-global"));
        enumerated_lua_editor_global = TRUE;
    }
    else if (g_strcmp0 (id, "editor-user") == 0)
    {
        ck_assert_str_eq (origin, "user");
        ck_assert (g_str_has_suffix (directory, "/editor-user"));
        enumerated_lua_editor_user = TRUE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_fail (const char **object_error, const char *message)
{
    if (object_error != NULL)
        *object_error = message;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_handle_is (const mc_runtime_handle_t *handle, mc_runtime_handle_kind_t kind, guint64 id)
{
    return handle != NULL && handle->kind == kind && handle->id == id && handle->generation == 1;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_file_snapshot_t *
test_object_file_new (const char *name, const char *path, gboolean marked)
{
    mc_runtime_file_snapshot_t *file = mc_runtime_file_snapshot_new ();

    file->name = g_strdup (name);
    file->path = g_strdup (path);
    file->is_dir = FALSE;
    file->size = 42;
    file->mtime = 123;
    file->marked = marked;
    return file;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
test_object_panel_active (void)
{
    return (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_PANEL, 1, 1 };
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
test_object_panel_passive (void)
{
    return (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_PANEL, 4, 1 };
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_panel_cwd (const mc_runtime_handle_t *panel, mc_runtime_string_t *path,
                       const char **object_error)
{
    if (!test_object_handle_is (panel, MC_RUNTIME_HANDLE_PANEL, 1))
        return test_object_fail (object_error, "closed");

    path->data = g_strdup ("/panel");
    path->length = strlen (path->data);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_panel_current (const mc_runtime_handle_t *panel, mc_runtime_file_snapshot_t **file,
                           const char **object_error)
{
    if (!test_object_handle_is (panel, MC_RUNTIME_HANDLE_PANEL, 1))
        return test_object_fail (object_error, "closed");

    *file = test_object_file_new ("current", "/panel/current", FALSE);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_panel_selected (const mc_runtime_handle_t *panel, mc_runtime_file_list_t *files,
                            const char **object_error)
{
    if (!test_object_handle_is (panel, MC_RUNTIME_HANDLE_PANEL, 1))
        return test_object_fail (object_error, "closed");

    files->items = g_new0 (mc_runtime_file_snapshot_t *, 1);
    files->items[0] = test_object_file_new ("marked", "/panel/marked", TRUE);
    files->len = 1;
    files->total_count = 1;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_panel_refresh (const mc_runtime_handle_t *panel, const char **object_error)
{
    if (!test_object_handle_is (panel, MC_RUNTIME_HANDLE_PANEL, 1))
        return test_object_fail (object_error, "closed");

    object_panel_refreshes++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_panel_chdir (const mc_runtime_handle_t *panel, const char *path,
                         const char **object_error)
{
    if (!test_object_handle_is (panel, MC_RUNTIME_HANDLE_PANEL, 1))
        return test_object_fail (object_error, "closed");

    g_free (object_panel_chdir_path);
    object_panel_chdir_path = g_strdup (path);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
test_object_editor_current (void)
{
    return (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_path (const mc_runtime_handle_t *editor, mc_runtime_string_t *path,
                         const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    path->data = g_strdup ("/editor");
    path->length = strlen (path->data);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_cursor (const mc_runtime_handle_t *editor, guint64 *line, guint64 *column,
                           const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    *line = 2;
    *column = 3;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_set_cursor (const mc_runtime_handle_t *editor, guint64 line, guint64 column,
                               const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    object_editor_line = line;
    object_editor_column = column;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_is_readonly (const mc_runtime_handle_t *editor, gboolean *readonly,
                                const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    *readonly = FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_get_text (const mc_runtime_handle_t *editor, gint64 from, gint64 to,
                             mc_runtime_string_t *text, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");
    if (from != 1 || to != 4)
        return test_object_fail (object_error, "invalid_range");

    text->data = g_strdup ("text");
    text->length = 4;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_selected_text (const mc_runtime_handle_t *editor, mc_runtime_string_t *text,
                                  const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    text->data = g_strdup ("U2V0");
    text->length = 4;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_insert (const mc_runtime_handle_t *editor, const char *text,
                           const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    g_free (object_editor_insert_text);
    object_editor_insert_text = g_strdup (text);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_save (const mc_runtime_handle_t *editor, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
test_object_viewer_current (void)
{
    return (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_VIEWER, 3, 1 };
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_viewer_path (const mc_runtime_handle_t *viewer, mc_runtime_string_t *path,
                         const char **object_error)
{
    if (!test_object_handle_is (viewer, MC_RUNTIME_HANDLE_VIEWER, 3))
        return test_object_fail (object_error, "closed");

    path->data = g_strdup ("/viewer");
    path->length = strlen (path->data);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_viewer_position (const mc_runtime_handle_t *viewer, gint64 *offset,
                             const char **object_error)
{
    if (!test_object_handle_is (viewer, MC_RUNTIME_HANDLE_VIEWER, 3))
        return test_object_fail (object_error, "closed");

    *offset = 12;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_viewer_goto (const mc_runtime_handle_t *viewer, gint64 offset,
                         const char **object_error)
{
    if (!test_object_handle_is (viewer, MC_RUNTIME_HANDLE_VIEWER, 3))
        return test_object_fail (object_error, "closed");

    object_viewer_offset = offset;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_viewer_mode (const mc_runtime_handle_t *viewer, mc_runtime_string_t *mode,
                         const char **object_error)
{
    if (!test_object_handle_is (viewer, MC_RUNTIME_HANDLE_VIEWER, 3))
        return test_object_fail (object_error, "closed");

    mode->data = g_strdup ("text");
    mode->length = 4;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_object_string_free (mc_runtime_string_t *string)
{
    if (string == NULL)
        return;

    g_free (string->data);
    string->data = NULL;
    string->length = 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_object_file_snapshot_free (mc_runtime_file_snapshot_t *file)
{
    mc_runtime_file_snapshot_free (file);
}

/* --------------------------------------------------------------------------------------------- */

static void
test_object_file_list_free (mc_runtime_file_list_t *files)
{
    guint i;

    if (files == NULL)
        return;

    for (i = 0; i < files->len; i++)
        mc_runtime_file_snapshot_free (files->items[i]);
    g_free (files->items);
    memset (files, 0, sizeof (*files));
}

/* --------------------------------------------------------------------------------------------- */

/* @Before */
static void
setup (void)
{
    static const mc_runtime_host_services_v1_t services = {
        .abi_version = MC_RUNTIME_PLUGIN_ABI_VERSION,
        .struct_size = sizeof (mc_runtime_host_services_v1_t),
        .ui_status = test_ui_status,
        .ui_message = test_ui_message,
        .log = test_runtime_log,
        .panel_active = test_object_panel_active,
        .panel_passive = test_object_panel_passive,
        .panel_cwd = test_object_panel_cwd,
        .panel_current = test_object_panel_current,
        .panel_selected = test_object_panel_selected,
        .panel_refresh = test_object_panel_refresh,
        .panel_chdir = test_object_panel_chdir,
        .editor_current = test_object_editor_current,
        .editor_path = test_object_editor_path,
        .editor_cursor = test_object_editor_cursor,
        .editor_set_cursor = test_object_editor_set_cursor,
        .editor_is_readonly = test_object_editor_is_readonly,
        .editor_get_text = test_object_editor_get_text,
        .editor_insert = test_object_editor_insert,
        .editor_save = test_object_editor_save,
        .viewer_current = test_object_viewer_current,
        .viewer_path = test_object_viewer_path,
        .viewer_position = test_object_viewer_position,
        .viewer_goto = test_object_viewer_goto,
        .viewer_mode = test_object_viewer_mode,
        .string_free = test_object_string_free,
        .file_snapshot_free = test_object_file_snapshot_free,
        .file_list_free = test_object_file_list_free,
        .editor_selected_text = test_object_editor_selected_text,
    };

    error = NULL;
    g_unsetenv ("MC_NO_LUA");
    g_unsetenv ("MC_LUA_TEST_EVENT_SHAPES");
    g_unsetenv ("MC_LUA_TEST_UI");
    g_unsetenv ("MC_LUA_TEST_ERROR");
    g_unsetenv ("MC_LUA_TEST_OBJECTS");
    g_unsetenv ("MC_LUA_TEST_MACRO");
    g_clear_pointer (&ui_status_text, g_free);
    g_clear_pointer (&ui_message_title, g_free);
    g_clear_pointer (&ui_message_text, g_free);
    g_clear_pointer (&object_panel_chdir_path, g_free);
    g_clear_pointer (&object_editor_insert_text, g_free);
    object_panel_refreshes = 0;
    object_editor_line = 0;
    object_editor_column = 0;
    object_viewer_offset = 0;
    enumerated_lua_packages = 0;
    enumerated_disabled_beta = FALSE;
    enumerated_lua_editor_packages = 0;
    enumerated_disabled_lua_editor_packages = 0;
    enumerated_lua_editor_global = FALSE;
    enumerated_lua_editor_user = FALSE;
    {
        char *prefs_path = g_build_filename (config_dir, "mc", "plugins.ini", (char *) NULL);
        char *ini_path = g_build_filename (config_dir, "mc", "ini", (char *) NULL);

        (void) g_remove (prefs_path);
        (void) g_remove (ini_path);
        g_free (ini_path);
        g_free (prefs_path);
    }
    remove_tree (system_scripts_dir);
    remove_tree (user_scripts_dir);
    create_test_packages ();
    create_event_shape_script ();
    create_ui_script ();
    create_error_script ();
    create_object_script ();

    ck_assert_msg (mc_event_init (&error), "Failed to initialize event transport: %s",
                   error != NULL ? error->message : "unknown error");
    ck_assert_msg (mc_runtime_events_init (&error), "Failed to initialize runtime events: %s",
                   error != NULL ? error->message : "unknown error");
    mc_runtime_plugins_set_host_services (&services);
    mc_runtime_plugins_set_directory_for_tests (TEST_LUA_RUNTIME_DIR);
}

/* --------------------------------------------------------------------------------------------- */

/* @After */
static void
teardown (void)
{
    mc_runtime_plugins_shutdown ();
    mc_runtime_events_deinit ();
    g_clear_error (&error);
    ck_assert_msg (mc_event_deinit (&error), "Failed to deinitialize event transport: %s",
                   error != NULL ? error->message : "unknown error");
    g_clear_error (&error);
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_snapshot_t *
startup_snapshot_new (void)
{
    mc_runtime_event_snapshot_t *snapshot;

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_STARTUP);
    snapshot->data.startup.run_mode = g_strdup ("full");
    snapshot->data.startup.config_dir = g_strdup (config_dir);
    snapshot->data.startup.data_dir = g_strdup ("/usr/share/mc");

    return snapshot;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_file_snapshot_t *
file_snapshot_new (const char *name, const char *path, gboolean marked)
{
    mc_runtime_file_snapshot_t *file;

    file = mc_runtime_file_snapshot_new ();
    file->name = g_strdup (name);
    file->path = g_strdup (path);
    file->is_dir = FALSE;
    file->size = 42;
    file->mtime = 123;
    file->marked = marked;

    return file;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_loads_user_override_and_callbacks)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *contents = NULL;

    mctest_assert_true (mc_runtime_plugins_load (&error));
    ck_assert_int_eq ((int) mc_runtime_plugins_count (), 1);

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "user-alpha:full\nuser-beta:full\n");
    g_free (contents);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_requires_known_workspace_directory)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *unknown_workspace_dir;
    char *contents = NULL;

    create_script (system_scripts_dir, "outside-workspace", "outside-workspace", FALSE);
    unknown_workspace_dir = g_build_filename (system_scripts_dir, "unknown", (char *) NULL);
    create_script (unknown_workspace_dir, "unknown-workspace", "unknown-workspace", FALSE);
    g_free (unknown_workspace_dir);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "user-alpha:full\nuser-beta:full\n");
    g_free (contents);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_uses_optional_ui_host_services)
{
    mc_runtime_event_snapshot_t *snapshot;

    g_setenv ("MC_LUA_TEST_UI", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    ck_assert_str_eq (ui_status_text, "Lua status");
    ck_assert_str_eq (ui_message_title, "Lua title");
    ck_assert_str_eq (ui_message_text, "Lua message");
    g_unsetenv ("MC_LUA_TEST_UI");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_isolates_callback_errors)
{
    mc_runtime_event_snapshot_t *snapshot;

    g_setenv ("MC_LUA_TEST_ERROR", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    ck_assert_str_eq (ui_status_text, "Lua script error-test: startup failed");
    g_unsetenv ("MC_LUA_TEST_ERROR");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_exposes_object_api_through_opaque_handles)
{
    mc_runtime_event_snapshot_t *snapshot;

    g_setenv ("MC_LUA_TEST_OBJECTS", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    ck_assert_int_eq ((int) object_panel_refreshes, 1);
    ck_assert_str_eq (object_panel_chdir_path, "/other");
    ck_assert_int_eq ((int) object_editor_line, 3);
    ck_assert_int_eq ((int) object_editor_column, 4);
    ck_assert_str_eq (object_editor_insert_text, "!");
    ck_assert_int_eq ((int) object_viewer_offset, 17);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_PANEL_CHDIR);
    snapshot->data.panel_chdir.panel = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_PANEL, 99, 1 };
    snapshot->data.panel_chdir.old_path = g_strdup ("/old");
    snapshot->data.panel_chdir.new_path = g_strdup ("/new");
    snapshot->data.panel_chdir.cause = g_strdup ("user");
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_PANEL_FILE_OPEN);
    snapshot->data.panel_file_open.panel = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_PANEL, 1, 1 };
    snapshot->data.panel_file_open.path = g_strdup ("/panel/file");
    snapshot->data.panel_file_open.open_mode = g_strdup ("view");
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);
    ck_assert_str_eq (ui_status_text, "phase status");

    g_unsetenv ("MC_LUA_TEST_OBJECTS");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_registers_editor_macros)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *contents = NULL;

    create_macro_script ();
    g_setenv ("MC_LUA_TEST_MACRO", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_key.key.name = g_strdup ("F11");
    snapshot->data.editor_key.key.code = 11;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_true (snapshot->consumed);
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_key.key.name = g_strdup ("f10");
    snapshot->data.editor_key.key.code = 10;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_false (snapshot->consumed);
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "macro-consume\nmacro-pass\n");
    g_free (contents);
    g_unsetenv ("MC_LUA_TEST_MACRO");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_converts_all_domain_event_snapshots)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *contents = NULL;

    g_setenv ("MC_LUA_TEST_EVENT_SHAPES", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));
    (void) g_remove (output_path);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_PANEL_CHDIR);
    snapshot->data.panel_chdir.panel = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_PANEL, 1, 1 };
    snapshot->data.panel_chdir.old_path = g_strdup ("/old");
    snapshot->data.panel_chdir.new_path = g_strdup ("/new");
    snapshot->data.panel_chdir.cause = g_strdup ("user");
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED);
    snapshot->data.panel_selection_changed.panel =
        (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_PANEL, 1, 1 };
    snapshot->data.panel_selection_changed.current =
        file_snapshot_new ("chosen", "/new/chosen", FALSE);
    g_ptr_array_add (snapshot->data.panel_selection_changed.selected,
                     file_snapshot_new ("marked", "/new/marked", TRUE));
    snapshot->data.panel_selection_changed.selected_count = 1;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_PANEL_FILE_OPEN);
    snapshot->data.panel_file_open.panel = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_PANEL, 1, 1 };
    snapshot->data.panel_file_open.path = g_strdup ("/new/file");
    snapshot->data.panel_file_open.open_mode = g_strdup ("view");
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_OPEN);
    snapshot->data.editor_open.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_open.path = g_strdup ("/new/edit");
    snapshot->data.editor_open.readonly = TRUE;
    snapshot->data.editor_open.line = 4;
    snapshot->data.editor_open.column = 9;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_SAVE);
    snapshot->data.editor_save.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_save.path = g_strdup ("/new/edit");
    snapshot->data.editor_save.previous_path = g_strdup ("/old/edit");
    snapshot->data.editor_save.save_as = TRUE;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_key.key.name = g_strdup ("Ctrl-S");
    snapshot->data.editor_key.key.code = 19;
    snapshot->data.editor_key.key.ctrl = TRUE;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mctest_assert_true (snapshot->consumed);
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_VIEWER_OPEN);
    snapshot->data.viewer_open.viewer = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_VIEWER, 3, 1 };
    snapshot->data.viewer_open.path = g_strdup ("/new/view");
    snapshot->data.viewer_open.source_kind = g_strdup ("file");
    snapshot->data.viewer_open.start_line = 7;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_SHUTDOWN);
    snapshot->data.shutdown.reason = g_strdup ("quit");
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents,
                      "chdir\nselection\nopen\neditor-open\neditor-save\neditor-key\n"
                      "viewer-open\nshutdown\n");
    g_free (contents);
    g_unsetenv ("MC_LUA_TEST_EVENT_SHAPES");
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_honors_per_package_disable)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *prefs_path;
    char *contents = NULL;

    prefs_path = g_build_filename (config_dir, "mc", "plugins.ini", (char *) NULL);
    write_file (prefs_path, "[DisabledPlugins]\nlua/beta=true\n");
    g_free (prefs_path);

    mctest_assert_true (mc_runtime_plugins_load (&error));
    mc_runtime_plugins_enumerate_packages (test_enumerate_lua_package, NULL);
    ck_assert_int_gt ((int) enumerated_lua_packages, 0);
    ck_assert (enumerated_disabled_beta);

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "user-alpha:full\n");
    g_free (contents);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_enumerates_editor_package_details)
{
    mctest_assert_true (mc_runtime_plugins_load (&error));
    mc_runtime_plugins_enumerate_package_details (test_enumerate_lua_package_details, NULL);

    ck_assert_int_eq ((int) enumerated_lua_editor_packages, 2);
    ck_assert (enumerated_lua_editor_global);
    ck_assert (enumerated_lua_editor_user);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_honors_mcedit_workspace_disable)
{
    char *ini_path;

    ini_path = g_build_filename (config_dir, "mc", "ini", (char *) NULL);
    write_file (ini_path, "[Lua]\nmcedit_enabled=false\n");
    g_free (ini_path);

    mctest_assert_true (mc_runtime_plugins_load (&error));
    mc_runtime_plugins_enumerate_package_details (test_enumerate_lua_package_details, NULL);

    ck_assert_int_eq ((int) enumerated_lua_editor_packages, 2);
    ck_assert_int_eq ((int) enumerated_disabled_lua_editor_packages, 2);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_rejects_insecure_package_paths)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *root;
    char *contents = NULL;

    create_script (user_mc_scripts_dir, "unsafe", "unsafe", FALSE);
    root = g_build_filename (user_mc_scripts_dir, "unsafe", (char *) NULL);
    ck_assert_int_eq (g_chmod (root, 0777), 0);
    g_free (root);

    mctest_assert_true (mc_runtime_plugins_load (&error));
    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "user-alpha:full\nuser-beta:full\n");
    g_free (contents);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_honors_disable_environment)
{
    mc_runtime_event_snapshot_t *snapshot;

    g_setenv ("MC_NO_LUA", "1", TRUE);
    mctest_assert_true (mc_runtime_plugins_load (&error));
    ck_assert_int_eq ((int) mc_runtime_plugins_count (), 0);

    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_false (g_file_test (output_path, G_FILE_TEST_EXISTS));
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

int
main (void)
{
    TCase *tc_core;
    int result;

    test_root = g_dir_make_tmp ("mc-lua-runtime.XXXXXX", &error);
    if (test_root == NULL)
    {
        fprintf (stderr, "Could not create test directory: %s\n",
                 error != NULL ? error->message : "unknown error");
        g_clear_error (&error);
        return EXIT_FAILURE;
    }

    config_dir = g_build_filename (test_root, "config", (char *) NULL);
    system_scripts_dir = g_build_filename (test_root, "system-scripts", (char *) NULL);
    user_scripts_dir = g_build_filename (config_dir, "mc", "lua", "scripts", (char *) NULL);
    system_mc_scripts_dir = g_build_filename (system_scripts_dir, "mc", (char *) NULL);
    user_mc_scripts_dir = g_build_filename (user_scripts_dir, "mc", (char *) NULL);
    system_editor_scripts_dir = g_build_filename (system_scripts_dir, "editor", (char *) NULL);
    user_editor_scripts_dir = g_build_filename (user_scripts_dir, "editor", (char *) NULL);
    output_path = g_build_filename (test_root, "events.log", (char *) NULL);
    g_setenv ("XDG_CONFIG_HOME", config_dir, TRUE);
    g_setenv ("MC_LUA_TEST_SYSTEM_SCRIPTS_DIR", system_scripts_dir, TRUE);

    tc_core = tcase_create ("Core");
    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_lua_runtime_loads_user_override_and_callbacks);
    tcase_add_test (tc_core, test_lua_runtime_requires_known_workspace_directory);
    tcase_add_test (tc_core, test_lua_runtime_uses_optional_ui_host_services);
    tcase_add_test (tc_core, test_lua_runtime_isolates_callback_errors);
    tcase_add_test (tc_core, test_lua_runtime_exposes_object_api_through_opaque_handles);
    tcase_add_test (tc_core, test_lua_runtime_registers_editor_macros);
    tcase_add_test (tc_core, test_lua_runtime_converts_all_domain_event_snapshots);
    tcase_add_test (tc_core, test_lua_runtime_honors_per_package_disable);
    tcase_add_test (tc_core, test_lua_runtime_enumerates_editor_package_details);
    tcase_add_test (tc_core, test_lua_runtime_honors_mcedit_workspace_disable);
    tcase_add_test (tc_core, test_lua_runtime_rejects_insecure_package_paths);
    tcase_add_test (tc_core, test_lua_runtime_honors_disable_environment);

    result = mctest_run_all (tc_core);

    g_unsetenv ("MC_NO_LUA");
    g_unsetenv ("MC_LUA_TEST_EVENT_SHAPES");
    g_unsetenv ("MC_LUA_TEST_UI");
    g_unsetenv ("MC_LUA_TEST_ERROR");
    g_unsetenv ("MC_LUA_TEST_OBJECTS");
    g_unsetenv ("MC_LUA_TEST_MACRO");
    g_unsetenv ("MC_LUA_TEST_SYSTEM_SCRIPTS_DIR");
    remove_tree (test_root);
    g_free (object_editor_insert_text);
    g_free (object_panel_chdir_path);
    g_free (ui_message_text);
    g_free (ui_message_title);
    g_free (ui_status_text);
    g_free (output_path);
    g_free (user_editor_scripts_dir);
    g_free (system_editor_scripts_dir);
    g_free (user_mc_scripts_dir);
    g_free (system_mc_scripts_dir);
    g_free (user_scripts_dir);
    g_free (system_scripts_dir);
    g_free (config_dir);
    g_free (test_root);
    return result;
}

/* --------------------------------------------------------------------------------------------- */
