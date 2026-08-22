/*
   Manage Plugins dialog: enable/disable runtime extensions and editor/panel plugins.

   Copyright (C) 2025-2026
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

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/tty/tty.h"
#include "lib/keybind.h"  // CK_Enter
#include "lib/mcconfig.h"
#include "lib/widget.h"
#include "lib/widget/table.h"
#include "lib/editor-plugin.h"
#include "lib/extension-runtime.h"
#include "lib/panel-plugin.h"
#include "lib/plugin-prefs.h"

#include "src/editor-plugins/builtin-plugins.h"  // editor_plugins_register_all
#include "src/filemanager/cmd.h"  // edit_file_at_line()
#include "src/filemanager/magic.h"

#include "manage_plugins.h"

/*** file scope macro definitions ****************************************************************/

#define MP_LIST_MAX_H           16
#define MP_LUA_CONFIG_GROUP     "Lua"
#define MP_LUA_MCEDIT_WORKSPACE "mcedit"
#define MP_LUA_MANIFEST_FILE    "lua.ini"
#define MP_LUA_MANIFEST_GROUP   "Lua"
#define MP_LUA_EDIT_SCRIPT      (B_USER + 1)
#define MP_LUA_RUN_SCRIPT       (B_USER + 2)

/*** file scope type declarations ****************************************************************/

typedef gboolean (*mp_enabled_getter_t) (void);
typedef void (*mp_enabled_setter_t) (gboolean enabled);
typedef gboolean (*mp_settings_callback_t) (void);

typedef struct
{
    const char *kind;                      /* plugin type (display only) */
    mc_plugin_kind_t kind_id;              /* prefs namespace */
    const char *name;                      /* plugin id */
    const char *desc;                      /* display name */
    const mc_panel_plugin_t *panel_plugin; /* panel plugin for settings, else NULL */
    mp_enabled_getter_t get_enabled;       /* NULL: use plugin-prefs */
    mp_enabled_setter_t set_enabled;       /* NULL: use plugin-prefs */
    mp_settings_callback_t settings;       /* runtime-owned settings */
} mp_row_t;

/* Passed to the dialog callback so Enter/F4 can reach the selected row. */
typedef struct
{
    WTable *tbl;
    const GArray *rows;
} mp_ctx_t;

typedef struct
{
    char *id;
    char *name;
    char *provides;
    char *origin;
    char *directory;
} mp_lua_editor_script_t;

typedef struct
{
    WTable *table;
    const GPtrArray *scripts;
} mp_lua_scripts_ctx_t;

static void mp_lua_editor_actions_dialog (const char *package_id);

/*** file scope functions ************************************************************************/

/* Return TRUE if a row for the given plugin (kind, name) already exists in @rows. */
static gboolean
mp_row_exists (const GArray *rows, mc_plugin_kind_t kind_id, const char *name)
{
    guint i;

    for (i = 0; i < rows->len; i++)
    {
        const mp_row_t *r = &g_array_index (rows, mp_row_t, i);
        if (r->kind_id == kind_id && g_strcmp0 (r->name, name) == 0)
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

#ifdef ENABLE_LUA_PLUGIN

static gboolean
mp_lua_core_enabled (void)
{
    return mc_global.main_config == NULL
        || mc_config_get_bool (mc_global.main_config, MP_LUA_CONFIG_GROUP, "enabled", TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
mp_lua_set_config_enabled (const char *key, gboolean enabled)
{
    if (mc_global.main_config == NULL)
        return;

    mc_config_set_bool (mc_global.main_config, MP_LUA_CONFIG_GROUP, key, enabled);
    (void) mc_config_save_file (mc_global.main_config, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
mp_lua_set_core_enabled (gboolean enabled)
{
    mp_lua_set_config_enabled ("enabled", enabled);
}

/* --------------------------------------------------------------------------------------------- */

static void
mp_lua_editor_script_destroy (mp_lua_editor_script_t *script)
{
    if (script == NULL)
        return;

    g_free (script->id);
    g_free (script->name);
    g_free (script->provides);
    g_free (script->origin);
    g_free (script->directory);
    g_free (script);
}

/* --------------------------------------------------------------------------------------------- */

static char *
mp_lua_editor_script_provides (const char *directory)
{
    GKeyFile *ini;
    char *path;
    char *provides;

    if (directory == NULL || directory[0] == '\0')
        return g_strdup (_ ("unspecified"));

    path = g_build_filename (directory, MP_LUA_MANIFEST_FILE, (char *) NULL);
    ini = g_key_file_new ();
    if (!g_key_file_load_from_file (ini, path, G_KEY_FILE_NONE, NULL))
    {
        g_key_file_free (ini);
        g_free (path);
        return g_strdup (_ ("unspecified"));
    }

    provides = g_key_file_get_string (ini, MP_LUA_MANIFEST_GROUP, "provides", NULL);
    g_key_file_free (ini);
    g_free (path);

    if (provides == NULL || provides[0] == '\0')
    {
        g_free (provides);
        return g_strdup (_ ("unspecified"));
    }

    return provides;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mp_lua_editor_script_entry (const char *directory)
{
    GKeyFile *ini;
    char *manifest_path;
    char *entry = NULL;
    char *joined = NULL;
    char *root = NULL;
    char *path = NULL;
    gsize root_length;

    if (directory == NULL || directory[0] == '\0')
        return NULL;

    manifest_path = g_build_filename (directory, MP_LUA_MANIFEST_FILE, (char *) NULL);
    ini = g_key_file_new ();
    if (g_key_file_load_from_file (ini, manifest_path, G_KEY_FILE_NONE, NULL))
        entry = g_key_file_get_string (ini, MP_LUA_MANIFEST_GROUP, "entry", NULL);
    g_key_file_free (ini);
    g_free (manifest_path);

    if (entry == NULL || entry[0] == '\0' || g_path_is_absolute (entry))
        goto done;

    joined = g_build_filename (directory, entry, (char *) NULL);
    root = g_canonicalize_filename (directory, NULL);
    path = g_canonicalize_filename (joined, NULL);
    root_length = strlen (root);
    if (!g_str_has_prefix (path, root)
        || (path[root_length] != '\0' && path[root_length] != G_DIR_SEPARATOR)
        || !g_file_test (path, G_FILE_TEST_IS_REGULAR))
        g_clear_pointer (&path, g_free);

done:
    g_free (root);
    g_free (joined);
    g_free (entry);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

static void
mp_collect_lua_editor_script (const char *runtime_name, const char *id, const char *display_name,
                              const char *workspace, const char *origin, const char *directory,
                              gboolean enabled, gpointer user_data)
{
    GPtrArray *scripts = (GPtrArray *) user_data;
    mp_lua_editor_script_t *script;

    (void) enabled;

    if (scripts == NULL || g_strcmp0 (runtime_name, "lua") != 0
        || g_strcmp0 (workspace, MP_LUA_MCEDIT_WORKSPACE) != 0 || id == NULL || id[0] == '\0'
        || directory == NULL || directory[0] == '\0')
        return;

    script = g_new0 (mp_lua_editor_script_t, 1);
    script->id = g_strdup (id);
    script->name = g_strdup (display_name != NULL && display_name[0] != '\0' ? display_name : id);
    script->provides = mp_lua_editor_script_provides (directory);
    script->origin = g_strdup (origin != NULL && origin[0] != '\0' ? origin : "unknown");
    script->directory = g_strdup (directory);
    g_ptr_array_add (scripts, script);
}

/* --------------------------------------------------------------------------------------------- */

static int
mp_lua_editor_scripts_get_nrows (const void *data)
{
    const GPtrArray *scripts = (const GPtrArray *) data;

    return scripts != NULL ? (int) scripts->len : 0;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mp_lua_editor_scripts_get_text (const void *data, int row, int col)
{
    const GPtrArray *scripts = (const GPtrArray *) data;
    const mp_lua_editor_script_t *script;

    if (scripts == NULL || row < 0 || row >= (int) scripts->len)
        return "";

    script = (const mp_lua_editor_script_t *) g_ptr_array_index (scripts, (guint) row);
    switch (col)
    {
    case 1:
        return script->origin != NULL ? script->origin : "";
    case 2:
        return script->id != NULL ? script->id : "";
    case 3:
        return script->provides != NULL ? script->provides : "";
    case 4:
        return script->name != NULL ? script->name : "";
    default:
        return "";
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mp_lua_editor_scripts_get_checked (const void *data, int row, int col)
{
    const GPtrArray *scripts = (const GPtrArray *) data;
    const mp_lua_editor_script_t *script;

    (void) col;

    if (scripts == NULL || row < 0 || row >= (int) scripts->len)
        return FALSE;

    script = (const mp_lua_editor_script_t *) g_ptr_array_index (scripts, (guint) row);
    return !mc_plugin_prefs_is_disabled (MC_PLUGIN_KIND_LUA, script->id);
}

/* --------------------------------------------------------------------------------------------- */

static void
mp_lua_editor_scripts_set_checked (void *data, int row, int col, gboolean val)
{
    GPtrArray *scripts = (GPtrArray *) data;
    const mp_lua_editor_script_t *script;

    (void) col;

    if (scripts == NULL || row < 0 || row >= (int) scripts->len)
        return;

    script = (const mp_lua_editor_script_t *) g_ptr_array_index (scripts, (guint) row);
    mc_plugin_prefs_set_disabled (MC_PLUGIN_KIND_LUA, script->id, !val);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
mp_lua_editor_scripts_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm,
                                    void *data)
{
    (void) sender;
    (void) data;

    if (msg == MSG_UNHANDLED_KEY && parm == KEY_F (4))
    {
        DIALOG (w)->ret_value = MP_LUA_EDIT_SCRIPT;
        dlg_close (DIALOG (w));
        return MSG_HANDLED;
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

static int
mp_lua_editor_script_run (WButton *button, int action)
{
    const WDialog *dialog = DIALOG (WIDGET (button)->owner);
    const mp_lua_scripts_ctx_t *ctx = (const mp_lua_scripts_ctx_t *) dialog->data.p;
    int row;

    (void) action;
    if (ctx == NULL || ctx->table == NULL || ctx->scripts == NULL)
        return 0;

    row = table_get_current (ctx->table);
    if (row >= 0 && row < (int) ctx->scripts->len)
    {
        const mp_lua_editor_script_t *script =
            (const mp_lua_editor_script_t *) g_ptr_array_index (ctx->scripts, (guint) row);

        mp_lua_editor_actions_dialog (script->id);
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mp_lua_editor_scripts_dialog (void)
{
    GPtrArray *scripts;
    WDialog *dlg;
    WTable *tbl;
    int n_rows, table_h, dlg_h, dlg_w, table_w;
    int check_w, origin_w, id_w, provides_w, desc_w;
    table_column_def_t col_defs[5];
    table_datasource_t ds;
    mp_lua_scripts_ctx_t ctx;
    char *entry_path = NULL;

    scripts = g_ptr_array_new_with_free_func ((GDestroyNotify) mp_lua_editor_script_destroy);
    mc_runtime_plugins_enumerate_package_details (mp_collect_lua_editor_script, scripts);

    if (scripts->len == 0)
    {
        message (D_NORMAL, _ ("Lua editor scripts"), "%s",
                 mc_runtime_plugins_are_loaded ()
                     ? _ ("No global or user Lua editor scripts were found.")
                     : _ ("Lua runtime is disabled. Enable Lua engine and restart MC."));
        g_ptr_array_free (scripts, TRUE);
        return FALSE;
    }

    n_rows = (int) scripts->len;
    table_h = MAX (3, MIN (n_rows, MP_LIST_MAX_H));
    dlg_h = table_h + 4;
    dlg_w = MIN (COLS - 4, 78);
    table_w = dlg_w - 1;
    check_w = 4;
    origin_w = 9;
    id_w = 16;
    provides_w = 14;
    desc_w = table_w - check_w - origin_w - id_w - provides_w - 6;
    if (desc_w < 10)
        desc_w = 10;

    col_defs[0].width = check_w;
    col_defs[0].align = J_CENTER;
    col_defs[0].type = TABLE_COL_CHECK;
    col_defs[1].width = origin_w;
    col_defs[1].align = J_LEFT;
    col_defs[1].type = TABLE_COL_TEXT;
    col_defs[2].width = id_w;
    col_defs[2].align = J_LEFT;
    col_defs[2].type = TABLE_COL_TEXT;
    col_defs[3].width = provides_w;
    col_defs[3].align = J_LEFT;
    col_defs[3].type = TABLE_COL_TEXT;
    col_defs[4].width = desc_w;
    col_defs[4].align = J_LEFT;
    col_defs[4].type = TABLE_COL_TEXT;

    dlg = dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w,
                      WPOS_KEEP_DEFAULT, TRUE, dialog_colors, mp_lua_editor_scripts_dlg_callback,
                      NULL, "[Lua editor scripts]", _ ("Lua editor scripts"));
    tbl = table_new (1, 1, table_h, table_w, 5, col_defs);
    tbl->scrollbar = TRUE;
    tbl->scrollbar_on_frame = TRUE;
    ctx.table = tbl;
    ctx.scripts = scripts;
    dlg->data.p = &ctx;

    ds.get_nrows = mp_lua_editor_scripts_get_nrows;
    ds.get_text = mp_lua_editor_scripts_get_text;
    ds.get_checked = mp_lua_editor_scripts_get_checked;
    ds.set_checked = mp_lua_editor_scripts_set_checked;
    ds.data = scripts;
    ds.cycle_choice = NULL;
    table_set_datasource (tbl, ds);

    group_add_widget (GROUP (dlg), tbl);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (GROUP (dlg),
                      button_new (dlg_h - 2, (dlg_w - 20) / 2, MP_LUA_RUN_SCRIPT, DEFPUSH_BUTTON,
                                  _ ("&Run"), mp_lua_editor_script_run));
    group_add_widget (GROUP (dlg),
                      button_new (dlg_h - 2, (dlg_w - 20) / 2 + 10, B_CANCEL, NORMAL_BUTTON,
                                  _ ("&Close"), NULL));
    widget_select (WIDGET (tbl));

    if (dlg_run (dlg) == MP_LUA_EDIT_SCRIPT)
    {
        int row = table_get_current (tbl);

        if (row >= 0 && row < (int) scripts->len)
        {
            const mp_lua_editor_script_t *script =
                (const mp_lua_editor_script_t *) g_ptr_array_index (scripts, (guint) row);
            entry_path = mp_lua_editor_script_entry (script->directory);
        }
    }

    widget_destroy (WIDGET (dlg));
    g_ptr_array_free (scripts, TRUE);

    if (entry_path != NULL)
    {
        vfs_path_t *entry_vpath = vfs_path_from_str (entry_path);

        edit_file_at_line (entry_vpath, TRUE, 0);
        vfs_path_free (entry_vpath, TRUE);
        g_free (entry_path);
        return TRUE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
manage_lua_editor_scripts_dialog (void)
{
    return mp_lua_editor_scripts_dialog ();
}

/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    char *runtime_name;
    char *id;
    char *label;
    char *shortcut;
} mp_lua_action_t;

typedef struct
{
    GPtrArray *actions;
    const char *package_id;
} mp_lua_action_collection_t;

static void
mp_lua_action_destroy (mp_lua_action_t *action)
{
    if (action == NULL)
        return;
    g_free (action->runtime_name);
    g_free (action->id);
    g_free (action->label);
    g_free (action->shortcut);
    g_free (action);
}

static void
mp_collect_lua_editor_action (const char *runtime_name, const char *id, const char *label,
                              const char *shortcut, gpointer user_data)
{
    mp_lua_action_collection_t *collection = (mp_lua_action_collection_t *) user_data;
    mp_lua_action_t *action;
    gsize package_id_length;

    if (collection == NULL || collection->actions == NULL || collection->package_id == NULL
        || g_strcmp0 (runtime_name, "lua") != 0 || id == NULL || label == NULL)
        return;
    package_id_length = strlen (collection->package_id);
    if (!g_str_has_prefix (id, collection->package_id) || id[package_id_length] != ':')
        return;

    action = g_new0 (mp_lua_action_t, 1);
    action->runtime_name = g_strdup (runtime_name);
    action->id = g_strdup (id);
    action->label = g_strdup (label);
    action->shortcut = g_strdup (shortcut);
    g_ptr_array_add (collection->actions, action);
}

static void
mp_lua_editor_action_invoke (const mp_lua_action_t *action)
{
    const char *error = NULL;

    if (!mc_runtime_plugins_invoke_action (action->runtime_name, MP_LUA_MCEDIT_WORKSPACE,
                                           action->id, &error))
        message (D_ERROR, _ ("Lua action failed"), "%s",
                 error != NULL ? error : _ ("Unknown error"));
}

static void
mp_lua_editor_actions_dialog (const char *package_id)
{
    GPtrArray *actions =
        g_ptr_array_new_with_free_func ((GDestroyNotify) mp_lua_action_destroy);
    mp_lua_action_collection_t collection = { actions, package_id };
    Listbox *listbox;
    int selected;
    guint i;

    mc_runtime_plugins_enumerate_actions (MP_LUA_MCEDIT_WORKSPACE,
                                          mp_collect_lua_editor_action, &collection);
    if (actions->len == 0)
    {
        message (D_NORMAL, _ ("Lua"), "%s",
                 _ ("The selected script has no runnable actions."));
        g_ptr_array_free (actions, TRUE);
        return;
    }

    if (actions->len == 1)
    {
        mp_lua_editor_action_invoke (
            (const mp_lua_action_t *) g_ptr_array_index (actions, 0));
        g_ptr_array_free (actions, TRUE);
        return;
    }

    listbox = listbox_window_new (MIN ((int) actions->len, MP_LIST_MAX_H),
                                  MIN (COLS - 6, 64), _ ("Run Lua action"), NULL);
    for (i = 0; i < actions->len; i++)
    {
        const mp_lua_action_t *action =
            (const mp_lua_action_t *) g_ptr_array_index (actions, i);
        char *line = action->shortcut != NULL && action->shortcut[0] != '\0'
            ? g_strdup_printf ("%s  [%s]", action->label, action->shortcut)
            : g_strdup (action->label);

        listbox_add_item (listbox->list, LISTBOX_APPEND_AT_END, 0, line, NULL, FALSE);
        g_free (line);
    }

    selected = listbox_run (listbox);
    if (selected >= 0 && selected < (int) actions->len)
        mp_lua_editor_action_invoke (
            (const mp_lua_action_t *) g_ptr_array_index (actions, (guint) selected));

    g_ptr_array_free (actions, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

#endif /* ENABLE_LUA_PLUGIN */

static GArray *
mp_collect_rows (GPtrArray *strpool)
{
    GArray *rows;
    const GSList *l;

    /* Trigger builtin editor plugin registration so this dialog shows them even
     * when the user has not opened an editor yet in this session.  Disabled
     * plugins are filtered out by mc_editor_plugin_add() and are restored below
     * from plugins.ini. */
#ifdef USE_INTERNAL_EDIT
    editor_plugins_register_all ();
#endif

    rows = g_array_new (FALSE, FALSE, sizeof (mp_row_t));

#ifdef ENABLE_LUA_PLUGIN
    {
        mp_row_t r = {
            .kind = "runtime",
            .kind_id = MC_PLUGIN_KIND_LUA,
            .name = "lua",
            .desc = "Lua engine",
            .panel_plugin = NULL,
            .get_enabled = mp_lua_core_enabled,
            .set_enabled = mp_lua_set_core_enabled,
            .settings = mp_lua_editor_scripts_dialog,
        };

        g_array_append_val (rows, r);
    }
#endif

    for (l = mc_editor_plugin_list (); l != NULL; l = g_slist_next (l))
    {
        const mc_editor_plugin_t *p = (const mc_editor_plugin_t *) l->data;
        mp_row_t r;

        if (p->name == NULL)
            continue;

        r.kind = "mcedit";
        r.kind_id = MC_PLUGIN_KIND_EDITOR;
        r.name = p->name;
        r.desc = p->display_name != NULL ? p->display_name : p->name;
        r.panel_plugin = NULL;
        r.get_enabled = NULL;
        r.set_enabled = NULL;
        r.settings = NULL;
        g_array_append_val (rows, r);
    }

    for (l = mc_panel_plugin_list (); l != NULL; l = g_slist_next (l))
    {
        const mc_panel_plugin_t *p = (const mc_panel_plugin_t *) l->data;
        mp_row_t r;

        if (p->name == NULL)
            continue;

        r.kind = "panel";
        r.kind_id = MC_PLUGIN_KIND_PANEL;
        r.name = p->name;
        r.desc = p->display_name != NULL ? p->display_name : p->name;
        r.panel_plugin = p;
        r.get_enabled = NULL;
        r.set_enabled = NULL;
        r.settings = NULL;
        g_array_append_val (rows, r);
    }

    /* Disabled plugins are not in the runtime registries; pull their names from
     * plugins.ini so the user can re-enable them.  Their display metadata is
     * unavailable until the plugin is registered again. */
    {
        static const struct
        {
            mc_plugin_kind_t kind_id;
            const char *kind;
        } kinds[] = {
            { MC_PLUGIN_KIND_EDITOR, "mcedit" },
            { MC_PLUGIN_KIND_PANEL, "panel" },
        };
        gsize k;

        for (k = 0; k < G_N_ELEMENTS (kinds); k++)
        {
            gchar **disabled = mc_plugin_prefs_list_disabled (kinds[k].kind_id);
            gchar **it;

            if (disabled == NULL)
                continue;

            for (it = disabled; *it != NULL; it++)
            {
                mp_row_t r;
                char *owned;

                if (mp_row_exists (rows, kinds[k].kind_id, *it))
                    continue;

                owned = g_strdup (*it);
                g_ptr_array_add (strpool, owned);

                r.kind = kinds[k].kind;
                r.kind_id = kinds[k].kind_id;
                r.name = owned;
                r.desc = _ ("(disabled, not loaded)");
                r.panel_plugin = NULL;
                r.get_enabled = NULL;
                r.set_enabled = NULL;
                r.settings = NULL;
                g_array_append_val (rows, r);
            }
            g_strfreev (disabled);
        }
    }

    return rows;
}

/* --------------------------------------------------------------------------------------------- */

static int
mp_get_nrows (const void *data)
{
    const GArray *rows = (const GArray *) data;
    return rows != NULL ? (int) rows->len : 0;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mp_get_text (const void *data, int row, int col)
{
    const GArray *rows = (const GArray *) data;
    const mp_row_t *r;

    if (rows == NULL || row < 0 || row >= (int) rows->len)
        return "";

    r = &g_array_index (rows, mp_row_t, (guint) row);

    switch (col)
    {
    case 1:
        return r->kind != NULL ? r->kind : "";
    case 2:
        return r->name != NULL ? r->name : "";
    case 3:
        return r->desc != NULL ? r->desc : "";
    default:
        return "";
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mp_get_checked (const void *data, int row, int col)
{
    const GArray *rows = (const GArray *) data;
    const mp_row_t *r;

    (void) col;

    if (rows == NULL || row < 0 || row >= (int) rows->len)
        return FALSE;

    r = &g_array_index (rows, mp_row_t, (guint) row);
    if (r->get_enabled != NULL)
        return r->get_enabled ();
    return !mc_plugin_prefs_is_disabled (r->kind_id, r->name);
}

/* --------------------------------------------------------------------------------------------- */

static void
mp_set_checked (void *data, int row, int col, gboolean val)
{
    GArray *rows = (GArray *) data;
    const mp_row_t *r;

    (void) col;

    if (rows == NULL || row < 0 || row >= (int) rows->len)
        return;

    r = &g_array_index (rows, mp_row_t, (guint) row);
    if (r->set_enabled != NULL)
        r->set_enabled (val);
    else
        mc_plugin_prefs_set_disabled (r->kind_id, r->name, !val);
}

/* --------------------------------------------------------------------------------------------- */

/* Open the settings dialog of the plugin under the table cursor, if it has one. */
static gboolean
mp_invoke_settings (const mp_ctx_t *ctx)
{
    int row;
    const mp_row_t *r;

    if (ctx == NULL || ctx->rows == NULL)
        return FALSE;

    row = table_get_current (ctx->tbl);
    if (row < 0 || row >= (int) ctx->rows->len)
        return FALSE;

    r = &g_array_index (ctx->rows, mp_row_t, (guint) row);
    if (r->settings != NULL)
        return r->settings ();

    if (r->panel_plugin == NULL || r->panel_plugin->configure == NULL)
    {
        message (D_NORMAL, _ ("Manage Plugins"), "%s", _ ("This plugin has no settings."));
        return FALSE;
    }

    r->panel_plugin->configure ();

    /* A panel plugin may have edited its user-owned magic.ini groups. */
    mc_magic_flush ();
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
mp_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    const mp_ctx_t *ctx = (const mp_ctx_t *) DIALOG (w)->data.p;

    switch (msg)
    {
    case MSG_NOTIFY:
        if (ctx != NULL && sender == WIDGET (ctx->tbl) && parm == CK_Enter)
        {
            if (mp_invoke_settings (ctx))
                dlg_close (DIALOG (w));
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    case MSG_UNHANDLED_KEY:
        if (ctx != NULL && parm == KEY_F (4))
        {
            if (mp_invoke_settings (ctx))
                dlg_close (DIALOG (w));
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
manage_plugins_dialog (void)
{
    GArray *rows;
    GPtrArray *strpool; /* owns names restored from plugins.ini */
    WDialog *dlg;
    WTable *tbl;
    int n_rows, table_h, dlg_h, dlg_w, table_w;
    int check_w, kind_w, name_w, desc_w;
    table_column_def_t col_defs[4];
    table_datasource_t ds;
    mp_ctx_t ctx;

    strpool = g_ptr_array_new_with_free_func (g_free);
    rows = mp_collect_rows (strpool);
    n_rows = (int) rows->len;

    if (n_rows == 0)
    {
        g_array_free (rows, TRUE);
        g_ptr_array_free (strpool, TRUE);
        message (D_NORMAL, _ ("Manage Plugins"), "%s", _ ("No plugins loaded."));
        return;
    }

    table_h = MIN (n_rows, MP_LIST_MAX_H);
    table_h = MAX (table_h, 3);

    /* border(1) + table(N) + hline(1) + button(1) + border(1) */
    dlg_h = table_h + 4;
    dlg_w = MIN (COLS - 4, 78);

    /* columns: check(4) | kind(8) | name(14) | description(rest) */
    /* the last column of the table lands on the frame, that is where the scrollbar goes */
    table_w = dlg_w - 1;
    check_w = 4;
    kind_w = 8;
    name_w = 14;
    desc_w = table_w - check_w - kind_w - name_w - 5; /* 5 = 1 margin + 3 seps + 1 scrollbar */
    if (desc_w < 10)
        desc_w = 10;

    col_defs[0].width = check_w;
    col_defs[0].align = J_CENTER;
    col_defs[0].type = TABLE_COL_CHECK;
    col_defs[1].width = kind_w;
    col_defs[1].align = J_LEFT;
    col_defs[1].type = TABLE_COL_TEXT;
    col_defs[2].width = name_w;
    col_defs[2].align = J_LEFT;
    col_defs[2].type = TABLE_COL_TEXT;
    col_defs[3].width = desc_w;
    col_defs[3].align = J_LEFT;
    col_defs[3].type = TABLE_COL_TEXT;

    dlg = dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w,
                      WPOS_KEEP_DEFAULT, TRUE, dialog_colors, mp_dlg_callback, NULL,
                      "[Manage Plugins]", _ ("Manage Plugins"));

    tbl = table_new (1, 1, table_h, table_w, 4, col_defs);
    tbl->scrollbar = TRUE;
    tbl->scrollbar_on_frame = TRUE;

    ctx.tbl = tbl;
    ctx.rows = rows;
    dlg->data.p = &ctx;

    ds.get_nrows = mp_get_nrows;
    ds.get_text = mp_get_text;
    ds.get_checked = mp_get_checked;
    ds.set_checked = mp_set_checked;
    ds.data = rows;
    ds.cycle_choice = NULL;
    table_set_datasource (tbl, ds);

    group_add_widget (GROUP (dlg), tbl);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, (dlg_w - 10) / 2, B_CANCEL, NORMAL_BUTTON, _ ("&Close"), NULL));

    /* without this the button takes the focus and the table ignores the keys */
    widget_select (WIDGET (tbl));

    dlg_run (dlg);
    widget_destroy (WIDGET (dlg));

    g_array_free (rows, TRUE);
    g_ptr_array_free (strpool, TRUE);
}
