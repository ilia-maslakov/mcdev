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
#include "lib/strutil.h"

/*** global variables ****************************************************************************/

static GError *error = NULL;
static char *test_root = NULL;
static char *config_dir = NULL;
static char *data_dir = NULL;
static char *system_scripts_dir = NULL;
static char *user_scripts_dir = NULL;
static char *legacy_user_scripts_dir = NULL;
static char *system_mc_scripts_dir = NULL;
static char *user_mc_scripts_dir = NULL;
static char *system_editor_scripts_dir = NULL;
static char *user_editor_scripts_dir = NULL;
static char *output_path = NULL;
static char *ui_status_text = NULL;
static char *ui_message_title = NULL;
static char *ui_message_text = NULL;
static guint ui_dialog_count = 0;
static char *runtime_error_runtime = NULL;
static char *runtime_error_package = NULL;
static char *runtime_error_summary = NULL;
static char *runtime_error_details = NULL;
static mc_runtime_error_phase_t runtime_error_phase;
static guint runtime_error_count = 0;
static char *object_panel_chdir_path = NULL;
static char *object_editor_insert_text = NULL;
static char *object_editor_replacement = NULL;
static char *object_editor_range_replacement = NULL;
static char *process_shell_command = NULL;
static guint64 object_editor_range_from = 0;
static guint64 object_editor_range_to = 0;
static guint object_panel_refreshes = 0;
static guint64 object_editor_line = 0;
static guint64 object_editor_column = 0;
static guint64 object_editor_typed_revision = 0;
static guint object_editor_typed_changes = 0;
static guint64 object_editor_selection_revision = 0;
static gint64 object_viewer_offset = 0;
static guint enumerated_lua_packages = 0;
static guint enumerated_lua_runtimes = 0;
static gboolean enumerated_disabled_beta = FALSE;
static guint enumerated_lua_editor_packages = 0;
static guint enumerated_disabled_lua_editor_packages = 0;
static gboolean enumerated_lua_editor_global = FALSE;
static gboolean enumerated_lua_editor_user = FALSE;
static guint enumerated_lua_actions = 0;
static gboolean enumerated_lua_consume_action = FALSE;
static guint enumerated_lua_menu_actions = 0;
static gboolean enumerated_lua_drawing_action = FALSE;
static mc_runtime_panel_provider_t registered_panel_provider;
static mc_runtime_panel_help_t registered_panel_help;
static gboolean panel_provider_registered = FALSE;
static guint viewer_controller_open_count = 0;
static guint viewer_controller_close_count = 0;

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

static gboolean
test_panel_provider_register (mc_runtime_plugin_context_t *context,
                              const mc_runtime_panel_provider_t *provider,
                              mc_runtime_handle_t *registration, const char **provider_error)
{
    (void) context;
    (void) provider_error;
    registered_panel_provider = *provider;
    if (provider->help != NULL)
    {
        registered_panel_help = *provider->help;
        registered_panel_provider.help = &registered_panel_help;
    }
    panel_provider_registered = TRUE;
    *registration = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_PANEL_PROVIDER, 91, 1 };
    return TRUE;
}

static gboolean
test_panel_provider_unregister (const mc_runtime_handle_t *registration,
                                const char **provider_error)
{
    (void) provider_error;
    ck_assert_int_eq (registration->kind, MC_RUNTIME_HANDLE_PANEL_PROVIDER);
    panel_provider_registered = FALSE;
    return TRUE;
}

static void
create_panel_provider_script (void)
{
    char *root = g_build_filename (user_mc_scripts_dir, "panel-provider", (char *) NULL);
    char *ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    char *entry_path = g_build_filename (root, "init.lua", (char *) NULL);

    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    write_file (ini_path,
                "[Lua]\nid=panel-provider\napi_version=1\nname=Panel provider\nentry=init.lua\n");
    write_file (
        entry_path,
        "assert(mc.panel_provider.register {\n"
        " id='test-panel', title='Test panel', prefix='test-panel:',\n"
        " help={file='help/test.hlp',node='provider'},\n"
        " open=function(host,path) assert(host==nil); if path=='reject' then"
        " return nil,'Not a repository' end; return {path=path, revision=1} end,\n"
        " close=function(instance) instance.closed=true end,\n"
        " list=function(instance) return {revision=instance.revision, location=instance.path,"
        " title='Lua panel', help_node='view', entries={{id='dir:one',name='one',"
        "kind='directory',help_node='entry'}}} end,\n"
        " actions={{id='refresh',title='Refresh',targets='selection',"
        " menu={path='Command',label='Refresh Lua panel'}}},\n"
        " connections=function()return {{id='saved',title='Saved connection',"
        " location='/saved',description='snapshot',favorite=true}}end,\n"
        " new_connection=function(host)assert(host==nil);return {id='new',title='New connection',"
        " location='/new'}end,\n"
        " edit_connection=function(host,c)assert(host==nil);assert(c.id=='saved');"
        " assert(c.description=='snapshot' and c.favorite);return {id='saved',"
        " title='Edited connection',location='/edited'}end,\n"
        " copy_connection=function(host,c)assert(host==nil and c.id=='saved');"
        " return {id='copy',title='Copied connection',location='/copy'}end,\n"
        " rename_connection=function(host,c)assert(host==nil and c.id=='saved');"
        " return {id='saved',title='Renamed connection',location='/saved'}end,\n"
        " delete_connection=function(host,c)assert(host==nil and c.id=='saved');return true end,\n"
        " invoke_action=function(instance,id,selection) assert(id=='refresh');"
        " assert(selection[1]=='dir:one'); return {refresh=true,status='refreshed'} end,\n"
        " view=function(instance,id,request) assert(id=='dir:one');"
        " assert(request.plain and request.mode=='plain' and not request.quick);return true end,\n"
        " open_read=function(instance,id) assert(id=='dir:one');"
        " return mc.source.process{argv={'printf','archive'},cwd='/tmp'} end,\n"
        " navigate=function(instance,request) instance.path='/one'; instance.revision=2;"
        " return {refresh=true,location=instance.path,focus='dir:one'} end,\n"
        "})\n");
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
}

static gboolean
test_viewer_controller_open (mc_runtime_plugin_context_t *context,
                             const mc_runtime_viewer_controller_t *controller,
                             const char **viewer_error)
{
    mc_runtime_viewer_spec_t draft = { .struct_size = sizeof (draft) };
    gboolean handled = FALSE;

    (void) viewer_error;
    ck_assert_ptr_nonnull (controller->initial_spec);
    ck_assert_int_eq (controller->initial_spec->source->kind, MC_RUNTIME_VIEWER_SOURCE_BYTES);
    ck_assert_str_eq (controller->initial_spec->title, "revision 1");
    ck_assert_str_eq (controller->help_node, "controller-help");
    viewer_controller_open_count++;
    mctest_assert_true (controller->dispatch (context, controller->controller_id,
                                              MC_RUNTIME_VIEWER_CONTROLLER_OPTIONS, 0, &draft,
                                              &handled, viewer_error));
    mctest_assert_true (handled);
    mctest_assert_true (controller->dispatch (context, controller->controller_id,
                                              MC_RUNTIME_VIEWER_CONTROLLER_PREPARE, 0, &draft,
                                              &handled, viewer_error));
    ck_assert_str_eq (draft.title, "revision 2");
    controller->spec_free (context, &draft);
    mctest_assert_true (controller->dispatch (context, controller->controller_id,
                                              MC_RUNTIME_VIEWER_CONTROLLER_COMMIT, 0, &draft,
                                              &handled, viewer_error));
    mctest_assert_true (controller->dispatch (context, controller->controller_id,
                                              MC_RUNTIME_VIEWER_CONTROLLER_CLOSE, 0, &draft,
                                              &handled, viewer_error));
    viewer_controller_close_count++;
    return TRUE;
}

static void
create_viewer_controller_script (void)
{
    char *root = g_build_filename (user_mc_scripts_dir, "viewer-controller", (char *) NULL);
    char *ini_path = g_build_filename (root, "lua.ini", (char *) NULL);
    char *entry_path = g_build_filename (root, "init.lua", (char *) NULL);

    ck_assert_int_eq (g_mkdir_with_parents (root, 0700), 0);
    write_file (
        ini_path,
        "[Lua]\nid=viewer-controller\napi_version=1\nname=Viewer controller\nentry=init.lua\n");
    write_file (entry_path,
                "local d=assert(mc.viewer_source.define {id='test',"
                "help={node='controller-help'},"
                "open=function(identity)return {name=identity.name}end,"
                "prepare=function(session,p)return {source=mc.source.bytes(p.text),"
                "title='revision '..p.revision}end,"
                "options=function(session,p)return {text='two',revision=2}end,"
                "close=function(session)session.closed=true end})\n"
                "local c=assert(d:create({name='demo'},{text='one',revision=1}))\n"
                "assert(mc.ui.open_viewer {controller=c})\n");
    g_free (entry_path);
    g_free (ini_path);
    g_free (root);
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
    write_file (
        entry_path,
        "if os.getenv(\"MC_LUA_TEST_UI\") ~= \"1\" then return end\n"
        "mc.on(\"startup\", function()\n"
        "    assert(mc.ui.status(\"Lua status\"))\n"
        "    assert(mc.ui.message(\"Lua title\", \"Lua message\"))\n"
        "    assert(mc.ui.indicator { id = \"ui-test\", area = \"editor\", text = \"[UI]\", "
        "priority = 25 })\n"
        "end)\n"
        "mc.on(\"editor.key\", function()\n"
        "    local result = assert(mc.ui.dialog {\n"
        "        title = \"Base64 tools\",\n"
        "        controls = {\n"
        "            { type = \"label\", text = \"Selected text\" },\n"
        "            { id = \"operation\", type = \"select\", label = \"Operation\", value = "
        "\"decode\", options = {\n"
        "                { id = \"decode\", label = \"Decode\" },\n"
        "                { id = \"encode\", label = \"Encode\" },\n"
        "            } },\n"
        "            { id = \"line_width\", type = \"input\", value = \"0\",\n"
        "              history = \"lua-test-input\",\n"
        "              complete_on_tab = true,\n"
        "              completion = { \"commands\", \"files\", \"shell\" } },\n"
        "            { id = \"omit_padding\", type = \"checkbox\", label = \"Omit padding\", value "
        "= false },\n"
        "            { type = \"hbox\", controls = {\n"
        "                { id = \"run\", type = \"button\", label = \"&Run\", default = true },\n"
        "                { id = \"close\", type = \"button\", label = \"&Close\", cancel = true "
        "},\n"
        "            } },\n"
        "        },\n"
        "    })\n"
        "    assert(result.button == \"run\")\n"
        "    assert(result.values.operation == \"encode\")\n"
        "    assert(result.values.line_width == \"76\")\n"
        "    assert(result.values.omit_padding == true)\n"
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
    write_file (
        entry_path,
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
        "    local info = editor:info()\n"
        "    assert(info.path == \"/editor\" and info.name == \"editor\")\n"
        "    assert(info.modified and not info.readonly and info.revision == 7)\n"
        "    assert(info.byte_length == 4 and info.line_count == 1)\n"
        "    local selection = editor:selection()\n"
        "    assert(selection.kind == \"column\" and selection.revision == 7)\n"
        "    assert(selection.anchor.offset == 1 and selection.anchor.line == 1)\n"
        "    assert(selection.cursor.offset == 8 and selection.cursor.column == 4)\n"
        "    assert(#selection.ranges == 2)\n"
        "    assert(selection.ranges[1].from == 1 and selection.ranges[1].to == 3)\n"
        "    assert(selection.ranges[2].from == 6 and selection.ranges[2].to == 8)\n"
        "    assert(selection.text == \"bc\\ngh\" and not selection.text_truncated)\n"
        "    local edit_result = assert(editor:replace_selection(\"BC\\nGH\"))\n"
        "    assert(edit_result.revision == 8 and edit_result.cursor.offset == 5)\n"
        "    local range_result = assert(editor:replace(1, 3, \"xy\"))\n"
        "    assert(range_result.revision == 9 and range_result.cursor.offset == 3)\n"
        "    local line, column = editor:cursor()\n"
        "    assert(line == 2 and column == 3 and not editor:is_readonly())\n"
        "    assert(editor:tab_width() == 8)\n"
        "    assert(editor:get_text(1, 4) == \"text\")\n"
        "    assert(editor:text { from = 1, to = 3, revision = 7 } == \"yp\")\n"
        "    local typed = assert(editor:replace({ from = 1, to = 3, revision = 7 }, \"zz\"))\n"
        "    assert(typed.revision == 10)\n"
        "    local transaction = assert(editor:edit { revision = 10, changes = {\n"
        "        { from = 0, to = 1, text = \"A\" },\n"
        "        { from = 3, to = 4, text = \"Z\" },\n"
        "    }, cursor = { offset = 2 } })\n"
        "    assert(transaction.revision == 11 and transaction.cursor.offset == 2)\n"
        "    assert(mc.ui.text_width(\"漢\") == 2)\n"
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

    script = g_strdup_printf (
        "if os.getenv(\"MC_LUA_TEST_MACRO\") ~= \"1\" then return end\n"
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
        "})\n"
        "assert(mc.macro {\n"
        "    id = \"menu-only\",\n"
        "    area = \"editor\",\n"
        "    description = \"Menu only\",\n"
        "    menu = { path = \"Drawing\", label = \"Draw test line\", "
        "position = 25 },\n"
        "    action = function()\n"
        "        local result = assert(mc.process.run { command = \"printf test\" })\n"
        "        assert(result.stdout == \"process output\" and "
        "result.stderr == \"warning\" and result.exit_code == 7)\n"
        "        record(\"menu-only\") return mc.CONSUME\n"
        "    end,\n"
        "})\n"
        "assert(mc.macro {\n"
        "    id = \"hidden-f9\",\n"
        "    area = \"editor\",\n"
        "    key = \"F9\",\n"
        "    description = \"Hidden F9\",\n"
        "    listed = false,\n"
        "    action = function() return mc.CONSUME end,\n"
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

static gboolean
test_ui_dialog (const mc_runtime_dialog_t *dialog, mc_runtime_dialog_result_t *result,
                const char **dialog_error)
{
    const mc_runtime_dialog_control_t *buttons;

    (void) dialog_error;
    ck_assert_ptr_nonnull (dialog);
    ck_assert_str_eq (dialog->title, "Base64 tools");
    ck_assert_int_eq ((int) dialog->controls_count, 5);
    ck_assert_int_eq ((int) dialog->controls[0].type, (int) MC_RUNTIME_DIALOG_LABEL);
    ck_assert_str_eq (dialog->controls[0].text, "Selected text");
    ck_assert_int_eq ((int) dialog->controls[1].type, (int) MC_RUNTIME_DIALOG_SELECT);
    ck_assert_str_eq (dialog->controls[1].value, "decode");
    ck_assert_int_eq ((int) dialog->controls[1].options_count, 2);
    ck_assert_int_eq ((int) dialog->controls[2].type, (int) MC_RUNTIME_DIALOG_INPUT);
    ck_assert_str_eq (dialog->controls[2].value, "0");
    ck_assert_str_eq (dialog->controls[2].text, "lua-test-input");
    ck_assert (dialog->controls[2].checked);
    ck_assert_int_eq ((int) dialog->controls[2].options_count, 3);
    ck_assert_str_eq (dialog->controls[2].options[0].id, "commands");
    ck_assert_str_eq (dialog->controls[2].options[1].id, "files");
    ck_assert_str_eq (dialog->controls[2].options[2].id, "shell");
    ck_assert_int_eq ((int) dialog->controls[3].type, (int) MC_RUNTIME_DIALOG_CHECKBOX);
    ck_assert (!dialog->controls[3].checked);
    ck_assert_int_eq ((int) dialog->controls[4].type, (int) MC_RUNTIME_DIALOG_HBOX);
    buttons = dialog->controls[4].controls;
    ck_assert_int_eq ((int) dialog->controls[4].controls_count, 2);
    ck_assert_str_eq (buttons[0].id, "run");
    ck_assert (buttons[0].default_button);
    ck_assert_str_eq (buttons[1].id, "close");
    ck_assert (buttons[1].cancel_button);

    result->button_id = g_strdup ("run");
    result->values_count = 3;
    result->values = g_new0 (mc_runtime_dialog_value_t, result->values_count);
    result->values[0].id = g_strdup ("operation");
    result->values[0].value = g_strdup ("encode");
    result->values[1].id = g_strdup ("line_width");
    result->values[1].value = g_strdup ("76");
    result->values[2].id = g_strdup ("omit_padding");
    result->values[2].is_boolean = TRUE;
    result->values[2].checked = TRUE;
    ui_dialog_count++;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_dialog_result_free (mc_runtime_dialog_result_t *result)
{
    guint i;

    g_free (result->button_id);
    for (i = 0; i < result->values_count; i++)
    {
        g_free ((char *) result->values[i].id);
        g_free ((char *) result->values[i].value);
    }
    g_free (result->values);
    memset (result, 0, sizeof (*result));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_process_run_shell (const char *command, gsize max_output, mc_runtime_process_result_t *result,
                        const char **out_error)
{
    (void) max_output;
    if (out_error != NULL)
        *out_error = NULL;
    g_free (process_shell_command);
    process_shell_command = g_strdup (command);
    memset (result, 0, sizeof (*result));
    result->out.data = g_strdup ("process output");
    result->out.length = strlen (result->out.data);
    result->err.data = g_strdup ("warning");
    result->err.length = strlen (result->err.data);
    result->exit_code = 7;
    return TRUE;
}

static void
test_process_result_free (mc_runtime_process_result_t *result)
{
    g_free (result->out.data);
    g_free (result->err.data);
    memset (result, 0, sizeof (*result));
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
test_runtime_error (const char *runtime_name, const char *package_id,
                    mc_runtime_error_phase_t phase, const char *summary, const char *details)
{
    g_free (runtime_error_runtime);
    g_free (runtime_error_package);
    g_free (runtime_error_summary);
    g_free (runtime_error_details);
    runtime_error_runtime = g_strdup (runtime_name);
    runtime_error_package = g_strdup (package_id);
    runtime_error_phase = phase;
    runtime_error_summary = g_strdup (summary);
    runtime_error_details = g_strdup (details);
    runtime_error_count++;
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
test_enumerate_lua_runtime (const char *runtime_name, const char *display_name, guint abi_version,
                            guint64 capability_flags, guint64 required_host_capabilities,
                            gpointer user_data)
{
    (void) capability_flags;
    (void) required_host_capabilities;
    (void) user_data;

    ck_assert_str_eq (runtime_name, "lua");
    ck_assert_str_eq (display_name, "Lua engine");
    ck_assert_int_eq ((int) abi_version, MC_RUNTIME_PLUGIN_ABI_VERSION);
    enumerated_lua_runtimes++;
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

static void
test_enumerate_lua_action (const char *runtime_name, const char *id, const char *label,
                           const char *shortcut, gpointer user_data)
{
    (void) user_data;

    ck_assert_str_eq (runtime_name, "lua");
    ck_assert_ptr_nonnull (id);
    ck_assert_ptr_nonnull (label);
    enumerated_lua_actions++;
    if (g_strcmp0 (id, "macro-test:consume-f11") == 0)
    {
        ck_assert_str_eq (label, "Consume F11");
        ck_assert_str_eq (shortcut, "F11");
        enumerated_lua_consume_action = TRUE;
    }
    else if (g_strcmp0 (id, "macro-test:menu-only") == 0)
    {
        ck_assert_str_eq (label, "Menu only");
        ck_assert_ptr_null (shortcut);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
test_enumerate_lua_menu_action (const char *runtime_name, const char *id, const char *menu_path,
                                const char *label, const char *shortcut, gint position,
                                gpointer user_data)
{
    (void) user_data;

    ck_assert_str_eq (runtime_name, "lua");
    enumerated_lua_menu_actions++;
    if (g_strcmp0 (id, "macro-test:menu-only") == 0)
    {
        ck_assert_str_eq (menu_path, "Drawing");
        ck_assert_str_eq (label, "Draw test line");
        ck_assert_ptr_null (shortcut);
        ck_assert_int_eq (position, 25);
        enumerated_lua_drawing_action = TRUE;
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
test_object_editor_info (const mc_runtime_handle_t *editor, mc_runtime_editor_info_t *info,
                         const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    memset (info, 0, sizeof (*info));
    info->path = g_strdup ("/editor");
    info->path_length = strlen (info->path);
    info->name = g_strdup ("editor");
    info->name_length = strlen (info->name);
    info->has_path = TRUE;
    info->modified = TRUE;
    info->readonly = FALSE;
    info->revision = 7;
    info->byte_length = 4;
    info->line_count = 1;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_object_editor_info_free (mc_runtime_editor_info_t *info)
{
    g_free (info->path);
    g_free (info->name);
    memset (info, 0, sizeof (*info));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_selection (const mc_runtime_handle_t *editor,
                              mc_runtime_editor_selection_t *selection, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    memset (selection, 0, sizeof (*selection));
    selection->kind = MC_RUNTIME_EDITOR_SELECTION_COLUMN;
    selection->revision = 7;
    selection->anchor = (mc_runtime_editor_position_t) { 1, 1, 2 };
    selection->cursor = (mc_runtime_editor_position_t) { 8, 2, 4 };
    selection->ranges_count = 2;
    selection->ranges = g_new (mc_runtime_editor_range_t, 2);
    selection->ranges[0] = (mc_runtime_editor_range_t) { 1, 3 };
    selection->ranges[1] = (mc_runtime_editor_range_t) { 6, 8 };
    selection->text = g_strdup ("bc\ngh");
    selection->text_length = 5;
    selection->has_text = TRUE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
test_object_editor_selection_free (mc_runtime_editor_selection_t *selection)
{
    g_free (selection->ranges);
    g_free (selection->text);
    memset (selection, 0, sizeof (*selection));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_replace_selection (const mc_runtime_handle_t *editor, const char *text,
                                      gsize text_length, mc_runtime_editor_edit_result_t *result,
                                      const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    g_free (object_editor_replacement);
    object_editor_replacement = g_strndup (text, text_length);
    result->revision = 8;
    result->cursor = (mc_runtime_editor_position_t) { 5, 1, 6 };
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_replace (const mc_runtime_handle_t *editor, guint64 from, guint64 to,
                            const char *text, gsize text_length,
                            mc_runtime_editor_edit_result_t *result, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    object_editor_range_from = from;
    object_editor_range_to = to;
    g_free (object_editor_range_replacement);
    object_editor_range_replacement = g_strndup (text, text_length);
    result->revision = 9;
    result->cursor = (mc_runtime_editor_position_t) { 3, 1, 4 };
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
test_object_editor_text (const mc_runtime_handle_t *editor, const mc_runtime_editor_range_t *range,
                         gboolean has_revision, guint64 revision, mc_runtime_string_t *text,
                         const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");
    ck_assert (range != NULL && range->from == 1 && range->to == 3);
    ck_assert (has_revision && revision == 7);
    text->data = g_strdup ("yp");
    text->length = 2;
    return TRUE;
}

static gboolean
test_object_editor_edit (const mc_runtime_handle_t *editor,
                         const mc_runtime_editor_edit_t *edit_spec,
                         mc_runtime_editor_edit_result_t *result, const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");
    object_editor_typed_revision = edit_spec->revision;
    object_editor_typed_changes = edit_spec->changes_count;
    result->revision = edit_spec->changes_count == 1 ? 10 : 11;
    result->cursor =
        (mc_runtime_editor_position_t) { edit_spec->has_cursor ? edit_spec->cursor.offset : 3, 1,
                                         3 };
    return TRUE;
}

static gboolean
test_object_editor_replace_selection_v2 (const mc_runtime_handle_t *editor, guint64 revision,
                                         const char *text, gsize text_length,
                                         mc_runtime_editor_edit_result_t *result,
                                         const char **object_error)
{
    object_editor_selection_revision = revision;
    return test_object_editor_replace_selection (editor, text, text_length, result, object_error);
}

static gboolean
test_ui_text_width (const char *text, gsize text_length, guint *width, const char **object_error)
{
    (void) object_error;
    ck_assert_int_eq ((int) text_length, 3);
    ck_assert_int_eq (memcmp (text, "漢", 3), 0);
    *width = 2;
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
test_object_editor_tab_width (const mc_runtime_handle_t *editor, guint *tab_width,
                              const char **object_error)
{
    if (!test_object_handle_is (editor, MC_RUNTIME_HANDLE_EDITOR, 2))
        return test_object_fail (object_error, "closed");

    *tab_width = 8;
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
        .runtime_error = test_runtime_error,
        .ui_dialog = test_ui_dialog,
        .dialog_result_free = test_dialog_result_free,
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
        .editor_info = test_object_editor_info,
        .editor_info_free = test_object_editor_info_free,
        .editor_selection = test_object_editor_selection,
        .editor_selection_free = test_object_editor_selection_free,
        .editor_replace_selection = test_object_editor_replace_selection,
        .editor_replace = test_object_editor_replace,
        .process_run_shell = test_process_run_shell,
        .process_result_free = test_process_result_free,
        .editor_tab_width = test_object_editor_tab_width,
        .editor_text = test_object_editor_text,
        .editor_edit = test_object_editor_edit,
        .editor_replace_selection_v2 = test_object_editor_replace_selection_v2,
        .ui_text_width = test_ui_text_width,
        .panel_provider_register = test_panel_provider_register,
        .panel_provider_unregister = test_panel_provider_unregister,
        .viewer_controller_open = test_viewer_controller_open,
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
    g_clear_pointer (&runtime_error_runtime, g_free);
    g_clear_pointer (&runtime_error_package, g_free);
    g_clear_pointer (&runtime_error_summary, g_free);
    g_clear_pointer (&runtime_error_details, g_free);
    runtime_error_phase = MC_RUNTIME_ERROR_PHASE_STARTUP;
    runtime_error_count = 0;
    ui_dialog_count = 0;
    g_clear_pointer (&object_panel_chdir_path, g_free);
    g_clear_pointer (&object_editor_insert_text, g_free);
    g_clear_pointer (&object_editor_replacement, g_free);
    g_clear_pointer (&object_editor_range_replacement, g_free);
    g_clear_pointer (&process_shell_command, g_free);
    object_editor_range_from = 0;
    object_editor_range_to = 0;
    object_panel_refreshes = 0;
    object_editor_line = 0;
    object_editor_column = 0;
    object_editor_typed_revision = 0;
    object_editor_typed_changes = 0;
    object_editor_selection_revision = 0;
    object_viewer_offset = 0;
    enumerated_lua_packages = 0;
    enumerated_lua_runtimes = 0;
    enumerated_disabled_beta = FALSE;
    enumerated_lua_editor_packages = 0;
    enumerated_disabled_lua_editor_packages = 0;
    enumerated_lua_editor_global = FALSE;
    enumerated_lua_editor_user = FALSE;
    enumerated_lua_actions = 0;
    enumerated_lua_consume_action = FALSE;
    enumerated_lua_menu_actions = 0;
    enumerated_lua_drawing_action = FALSE;
    memset (&registered_panel_provider, 0, sizeof (registered_panel_provider));
    panel_provider_registered = FALSE;
    viewer_controller_open_count = 0;
    viewer_controller_close_count = 0;
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
    remove_tree (legacy_user_scripts_dir);
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
    char *indicators;

    mc_runtime_plugins_shutdown ();
    indicators = mc_runtime_ui_indicators_compose ("editor", 80);
    ck_assert_str_eq (indicators, "");
    g_free (indicators);
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

START_TEST (test_lua_runtime_panel_provider_open_error)
{
    mc_runtime_panel_provider_request_t request = { 0 };
    mc_runtime_panel_provider_response_t response = { 0 };
    const char *provider_error = NULL;

    create_panel_provider_script ();
    ck_assert_msg (mc_runtime_plugins_load (&error), "Failed to load runtime: %s",
                   error != NULL ? error->message : "unknown error");
    request.struct_size = sizeof (request);
    request.operation_version = 1;
    request.path = "reject";
    mctest_assert_false (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id, MC_RUNTIME_PANEL_PROVIDER_OPEN,
        &request, &response, &provider_error));
    ck_assert_str_eq (response.status, "Not a repository");
    registered_panel_provider.response_free (NULL, &response);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_panel_provider_dispatch)
{
    mc_runtime_panel_provider_request_t request = { 0 };
    mc_runtime_panel_provider_response_t response = { 0 };
    guint64 instance_id;
    const char *provider_error = NULL;

    create_panel_provider_script ();
    ck_assert_msg (mc_runtime_plugins_load (&error), "Failed to load runtime: %s",
                   error != NULL ? error->message : "unknown error");
    mctest_assert_true (panel_provider_registered);
    ck_assert_str_eq (registered_panel_provider.id, "test-panel");
    ck_assert_uint_eq (registered_panel_provider.actions_count, 2);
    ck_assert_str_eq (registered_panel_provider.help->node, "provider");
    ck_assert_str_eq (registered_panel_provider.actions[1].open_path, "test-panel:/saved");
    mctest_assert_true (registered_panel_provider.supports_new_connection);
    mctest_assert_true (registered_panel_provider.supports_edit_connection);
    mctest_assert_true (registered_panel_provider.supports_copy_connection);
    mctest_assert_true (registered_panel_provider.supports_rename_connection);
    mctest_assert_true (registered_panel_provider.supports_delete_connection);
    mctest_assert_true (registered_panel_provider.supports_open_read);

    request.struct_size = sizeof (request);
    request.operation_version = 1;
    request.path = "/";
    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id, MC_RUNTIME_PANEL_PROVIDER_OPEN,
        &request, &response, &provider_error));
    ck_assert_uint_ne (response.instance_id, 0);
    instance_id = response.instance_id;
    registered_panel_provider.response_free (NULL, &response);

    request.instance_id = instance_id;
    request.path = NULL;
    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id, MC_RUNTIME_PANEL_PROVIDER_LIST,
        &request, &response, &provider_error));
    ck_assert_uint_eq (response.view.revision, 1);
    ck_assert_uint_eq (response.view.entries_count, 1);
    ck_assert_str_eq (response.view.entries[0].id, "dir:one");
    registered_panel_provider.response_free (NULL, &response);

    request.entry_id = "dir:one";
    request.path = "plain";
    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id, MC_RUNTIME_PANEL_PROVIDER_VIEW,
        &request, &response, &provider_error));
    mctest_assert_true (response.handled);
    registered_panel_provider.response_free (NULL, &response);

    request.revision = 1;
    request.entry_id = "dir:one";
    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id, MC_RUNTIME_PANEL_PROVIDER_OPEN_READ,
        &request, &response, &provider_error));
    ck_assert_ptr_nonnull (response.read_source);
    ck_assert_int_eq (response.read_source->kind, MC_RUNTIME_VIEWER_SOURCE_PROCESS);
    ck_assert_uint_eq (response.read_source->process.argc, 2);
    ck_assert_str_eq (response.read_source->process.argv[0], "printf");
    ck_assert_str_eq (response.read_source->process.argv[1], "archive");
    ck_assert_str_eq (response.read_source->process.cwd, "/tmp");
    registered_panel_provider.response_free (NULL, &response);

    request.revision = 1;
    request.entry_id = "dir:one";
    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id,
        MC_RUNTIME_PANEL_PROVIDER_NAVIGATE_ENTRY, &request, &response, &provider_error));
    mctest_assert_true (response.refresh);
    ck_assert_str_eq (response.location, "/one");
    registered_panel_provider.response_free (NULL, &response);

    {
        const char *selected[] = { "dir:one" };

        request.action_id = "refresh";
        request.selected_ids = selected;
        request.selected_count = 1;
        mctest_assert_true (registered_panel_provider.dispatch (
            NULL, registered_panel_provider.runtime_provider_id,
            MC_RUNTIME_PANEL_PROVIDER_INVOKE_ACTION, &request, &response, &provider_error));
        mctest_assert_true (response.refresh);
        ck_assert_str_eq (response.status, "refreshed");
        registered_panel_provider.response_free (NULL, &response);
        request.action_id = NULL;
        request.selected_ids = NULL;
        request.selected_count = 0;
    }

    request.entry_id = NULL;
    request.connection_id = "saved";
    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id,
        MC_RUNTIME_PANEL_PROVIDER_EDIT_CONNECTION, &request, &response, &provider_error));
    ck_assert_str_eq (response.focus_id, "Edited connection");
    registered_panel_provider.response_free (NULL, &response);

    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id,
        MC_RUNTIME_PANEL_PROVIDER_COPY_CONNECTION, &request, &response, &provider_error));
    ck_assert_str_eq (response.focus_id, "Copied connection");
    registered_panel_provider.response_free (NULL, &response);

    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id,
        MC_RUNTIME_PANEL_PROVIDER_RENAME_CONNECTION, &request, &response, &provider_error));
    ck_assert_str_eq (response.focus_id, "Renamed connection");
    registered_panel_provider.response_free (NULL, &response);

    /* The immutable snapshot cache is replaced transactionally, then removed. */
    request.connection_id = "saved";
    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id,
        MC_RUNTIME_PANEL_PROVIDER_DELETE_CONNECTION, &request, &response, &provider_error));
    mctest_assert_true (response.refresh);
    registered_panel_provider.response_free (NULL, &response);

    request.connection_id = NULL;
    mctest_assert_true (registered_panel_provider.dispatch (
        NULL, registered_panel_provider.runtime_provider_id, MC_RUNTIME_PANEL_PROVIDER_CLOSE,
        &request, &response, &provider_error));
    registered_panel_provider.response_free (NULL, &response);
}
END_TEST

START_TEST (test_lua_runtime_viewer_source_controller)
{
    create_viewer_controller_script ();
    ck_assert_msg (mc_runtime_plugins_load (&error), "Failed to load runtime: %s",
                   error != NULL ? error->message : "unknown error");
    ck_assert_uint_eq (viewer_controller_open_count, 1);
    ck_assert_uint_eq (viewer_controller_close_count, 1);
}
END_TEST

/* --------------------------------------------------------------------------------------------- */

START_TEST (test_lua_runtime_discovers_legacy_config_scripts)
{
    mc_runtime_event_snapshot_t *snapshot;
    char *legacy_mc_scripts_dir;
    char *contents = NULL;

    legacy_mc_scripts_dir = g_build_filename (legacy_user_scripts_dir, "mc", (char *) NULL);
    create_script (legacy_mc_scripts_dir, "legacy", "legacy", FALSE);
    g_free (legacy_mc_scripts_dir);

    mctest_assert_true (mc_runtime_plugins_load (&error));
    snapshot = startup_snapshot_new ();
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);

    mctest_assert_true (g_file_get_contents (output_path, &contents, NULL, &error));
    g_clear_error (&error);
    ck_assert_str_eq (contents, "user-alpha:full\nuser-beta:full\nlegacy:full\n");
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
    {
        char *indicators = mc_runtime_ui_indicators_compose ("editor", 80);

        ck_assert_str_eq (indicators, "[UI]");
        g_free (indicators);
    }

    snapshot = mc_runtime_event_snapshot_new (MC_RUNTIME_EVENT_EDITOR_KEY);
    snapshot->data.editor_key.editor = (mc_runtime_handle_t) { MC_RUNTIME_HANDLE_EDITOR, 2, 1 };
    snapshot->data.editor_key.key.name = g_strdup ("F11");
    snapshot->data.editor_key.key.code = 11;
    mctest_assert_true (mc_runtime_event_publish (snapshot, &error));
    mc_runtime_event_snapshot_free (snapshot);
    ck_assert_int_eq ((int) ui_dialog_count, 1);
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

    ck_assert_int_eq ((int) runtime_error_count, 1);
    ck_assert_str_eq (runtime_error_runtime, "lua");
    ck_assert_str_eq (runtime_error_package, "error-test");
    ck_assert_int_eq ((int) runtime_error_phase, (int) MC_RUNTIME_ERROR_PHASE_STARTUP);
    ck_assert_str_eq (runtime_error_summary, "Lua startup callback failed");
    ck_assert_msg (strstr (runtime_error_details, "expected test error") != NULL,
                   "Lua traceback did not include the original error");
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
    ck_assert_str_eq (object_editor_replacement, "BC\nGH");
    ck_assert_uint_eq (object_editor_range_from, 1);
    ck_assert_uint_eq (object_editor_range_to, 3);
    ck_assert_str_eq (object_editor_range_replacement, "xy");
    ck_assert_uint_eq (object_editor_selection_revision, 7);
    ck_assert_uint_eq (object_editor_typed_revision, 10);
    ck_assert_uint_eq (object_editor_typed_changes, 2);
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

    mc_runtime_plugins_enumerate_actions ("mcedit", test_enumerate_lua_action, NULL);
    ck_assert_int_eq ((int) enumerated_lua_actions, 3);
    mctest_assert_true (enumerated_lua_consume_action);
    mc_runtime_plugins_enumerate_menu_actions ("mcedit", test_enumerate_lua_menu_action, NULL);
    ck_assert_int_eq ((int) enumerated_lua_menu_actions, 1);
    mctest_assert_true (enumerated_lua_drawing_action);
    {
        const char *action_error = NULL;

        mctest_assert_true (mc_runtime_plugins_invoke_action (
            "lua", "mcedit", "macro-test:pass-f10", &action_error));
        ck_assert_ptr_null (action_error);
        mctest_assert_true (mc_runtime_plugins_invoke_action (
            "lua", "mcedit", "macro-test:menu-only", &action_error));
        ck_assert_ptr_null (action_error);
        ck_assert_str_eq (process_shell_command, "printf test");
    }

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
    ck_assert_str_eq (contents, "macro-pass\nmenu-only\nmacro-consume\nmacro-pass\n");
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
    mc_runtime_plugins_enumerate_runtimes (test_enumerate_lua_runtime, NULL);
    mc_runtime_plugins_enumerate_package_details (test_enumerate_lua_package_details, NULL);

    ck_assert_int_eq ((int) enumerated_lua_runtimes, 1);
    ck_assert_int_eq ((int) enumerated_lua_editor_packages, 2);
    ck_assert (enumerated_lua_editor_global);
    ck_assert (enumerated_lua_editor_user);
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

    str_init_strings ("UTF-8");
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
    data_dir = g_build_filename (test_root, "data", (char *) NULL);
    user_scripts_dir = g_build_filename (data_dir, "mc", "lua", "scripts", (char *) NULL);
    legacy_user_scripts_dir = g_build_filename (config_dir, "mc", "lua", "scripts", (char *) NULL);
    system_mc_scripts_dir = g_build_filename (system_scripts_dir, "mc", (char *) NULL);
    user_mc_scripts_dir = g_build_filename (user_scripts_dir, "mc", (char *) NULL);
    system_editor_scripts_dir = g_build_filename (system_scripts_dir, "editor", (char *) NULL);
    user_editor_scripts_dir = g_build_filename (user_scripts_dir, "editor", (char *) NULL);
    output_path = g_build_filename (test_root, "events.log", (char *) NULL);
    (void) g_mkdir_with_parents (config_dir, 0700);
    {
        char *mc_config_dir = g_build_filename (config_dir, "mc", (char *) NULL);

        (void) g_mkdir_with_parents (mc_config_dir, 0700);
        g_free (mc_config_dir);
    }
    g_setenv ("XDG_CONFIG_HOME", config_dir, TRUE);
    g_setenv ("XDG_DATA_HOME", data_dir, TRUE);
    g_setenv ("MC_LUA_TEST_SYSTEM_SCRIPTS_DIR", system_scripts_dir, TRUE);

    tc_core = tcase_create ("Core");
    tcase_add_checked_fixture (tc_core, setup, teardown);
    tcase_add_test (tc_core, test_lua_runtime_loads_user_override_and_callbacks);
    tcase_add_test (tc_core, test_lua_runtime_discovers_legacy_config_scripts);
    tcase_add_test (tc_core, test_lua_runtime_requires_known_workspace_directory);
    tcase_add_test (tc_core, test_lua_runtime_uses_optional_ui_host_services);
    tcase_add_test (tc_core, test_lua_runtime_isolates_callback_errors);
    tcase_add_test (tc_core, test_lua_runtime_exposes_object_api_through_opaque_handles);
    tcase_add_test (tc_core, test_lua_runtime_registers_editor_macros);
    tcase_add_test (tc_core, test_lua_runtime_converts_all_domain_event_snapshots);
    tcase_add_test (tc_core, test_lua_runtime_honors_per_package_disable);
    tcase_add_test (tc_core, test_lua_runtime_enumerates_editor_package_details);
    tcase_add_test (tc_core, test_lua_runtime_rejects_insecure_package_paths);
    tcase_add_test (tc_core, test_lua_runtime_honors_disable_environment);
    tcase_add_test (tc_core, test_lua_runtime_panel_provider_dispatch);
    tcase_add_test (tc_core, test_lua_runtime_panel_provider_open_error);
    tcase_add_test (tc_core, test_lua_runtime_viewer_source_controller);

    result = mctest_run_all (tc_core);

    g_unsetenv ("MC_NO_LUA");
    g_unsetenv ("MC_LUA_TEST_EVENT_SHAPES");
    g_unsetenv ("MC_LUA_TEST_UI");
    g_unsetenv ("MC_LUA_TEST_ERROR");
    g_unsetenv ("MC_LUA_TEST_OBJECTS");
    g_unsetenv ("MC_LUA_TEST_MACRO");
    g_unsetenv ("MC_LUA_TEST_SYSTEM_SCRIPTS_DIR");
    g_unsetenv ("XDG_DATA_HOME");
    remove_tree (test_root);
    g_free (object_editor_insert_text);
    g_free (object_editor_replacement);
    g_free (process_shell_command);
    g_free (object_editor_range_replacement);
    g_free (object_panel_chdir_path);
    g_free (ui_message_text);
    g_free (ui_message_title);
    g_free (ui_status_text);
    g_free (runtime_error_details);
    g_free (runtime_error_summary);
    g_free (runtime_error_package);
    g_free (runtime_error_runtime);
    g_free (output_path);
    g_free (user_editor_scripts_dir);
    g_free (system_editor_scripts_dir);
    g_free (user_mc_scripts_dir);
    g_free (system_mc_scripts_dir);
    g_free (user_scripts_dir);
    g_free (legacy_user_scripts_dir);
    g_free (system_scripts_dir);
    g_free (config_dir);
    g_free (data_dir);
    g_free (test_root);
    str_uninit_strings ();
    return result;
}

/* --------------------------------------------------------------------------------------------- */
