/*
   Lua runtime extension for Midnight Commander.

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

/** \file src/lua/mc-lua.c
 *  \brief Source: optional Lua implementation of the runtime extension ABI
 */

#include <config.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gmodule.h>
#include <glib/gstdio.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "lib/extension-runtime.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#ifndef MC_LUA_SYSTEM_SCRIPTS_DIR
#define MC_LUA_SYSTEM_SCRIPTS_DIR "/usr/share/mc/lua/scripts"
#endif

#ifndef MC_LUA_SYSTEM_MODULES_DIR
#define MC_LUA_SYSTEM_MODULES_DIR "/usr/share/mc/lua/lib"
#endif

#define MC_LUA_API_VERSION      1
#define MC_LUA_ID_MAX_LENGTH    64
#define MC_LUA_MANIFEST_FILE    "lua.ini"
#define MC_LUA_MANIFEST_GROUP   "Lua"
#define MC_LUA_REGISTRY_PACKAGE "mc.lua.package"
#define MC_LUA_REGISTRY_MODULES "mc.lua.modules"
#define MC_LUA_HANDLE_METATABLE "mc.runtime.handle"
#define MC_LUA_HOST_API_UI_SIZE                                                                    \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, ui_message)                                        \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->ui_message))
#define MC_LUA_HOST_API_LOG_SIZE                                                                   \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, log)                                               \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->log))
#define MC_LUA_HOST_API_OBJECTS_SIZE                                                               \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, file_list_free)                                    \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->file_list_free))
#define MC_LUA_HOST_API_EDITOR_SELECTED_TEXT_SIZE                                                  \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_selected_text)                              \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_selected_text))
#define MC_LUA_HOST_API_RUNTIME_ERROR_SIZE                                                         \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, runtime_error)                                     \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->runtime_error))
#define MC_LUA_HOST_API_DIALOG_SIZE                                                                \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, dialog_result_free)                                \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->dialog_result_free))
#define MC_LUA_HOST_API_EDITOR_INFO_SIZE                                                           \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_info_free)                                  \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_info_free))
#define MC_LUA_HOST_API_EDITOR_SELECTION_SIZE                                                      \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_selection_free)                             \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_selection_free))
#define MC_LUA_HOST_API_EDITOR_REPLACE_SELECTION_SIZE                                              \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_replace_selection)                          \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_replace_selection))
#define MC_LUA_HOST_API_EDITOR_REPLACE_SIZE                                                        \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_replace)                                    \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_replace))
#define MC_LUA_HOST_API_PROCESS_SIZE                                                               \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, process_result_free)                               \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->process_result_free))
#define MC_LUA_HOST_API_INDICATORS_SIZE                                                            \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, ui_indicators_clear_owner)                         \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->ui_indicators_clear_owner))
#define MC_LUA_HOST_API_EDITOR_TAB_WIDTH_SIZE                                                      \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_tab_width)                                  \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_tab_width))
#define MC_LUA_HOST_API_EDITOR_TEXT_SIZE                                                           \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_text)                                       \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_text))
#define MC_LUA_HOST_API_EDITOR_EDIT_SIZE                                                           \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_edit)                                       \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_edit))
#define MC_LUA_HOST_API_EDITOR_REPLACE_SELECTION_V2_SIZE                                           \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, editor_replace_selection_v2)                       \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->editor_replace_selection_v2))
#define MC_LUA_HOST_API_UI_TEXT_WIDTH_SIZE                                                         \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, ui_text_width)                                     \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->ui_text_width))
#define MC_LUA_HOST_API_PANEL_PROVIDER_SIZE                                                        \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, panel_provider_unregister)                         \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->panel_provider_unregister))
#define MC_LUA_HOST_API_VIEWER_SOURCE_SIZE                                                         \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, viewer_controller_open)                            \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->viewer_controller_open))
#define MC_LUA_HOST_API_OPEN_DIFF_SIZE                                                             \
    (G_STRUCT_OFFSET (mc_runtime_host_api_v1_t, ui_open_diff)                                      \
     + sizeof (((mc_runtime_host_api_v1_t *) NULL)->ui_open_diff))
#define MC_LUA_DIALOG_MAX_CONTROLS  32
#define MC_LUA_DIALOG_MAX_OPTIONS   64
#define MC_LUA_DIALOG_MAX_DEPTH     8
#define MC_LUA_VIEWER_DEFINITION_MT "mc.viewer_source.definition"
#define MC_LUA_VIEWER_CONTROLLER_MT "mc.viewer_source.controller"

/*** file scope type declarations ****************************************************************/

typedef enum
{
    MC_LUA_PACKAGE_SYSTEM,
    MC_LUA_PACKAGE_USER
} mc_lua_package_origin_t;

typedef enum
{
    MC_LUA_PROVIDES_NONE = 0,
    MC_LUA_PROVIDES_EVENTS = (1 << 0),
    MC_LUA_PROVIDES_MACROS = (1 << 1)
} mc_lua_provides_t;

typedef struct
{
    const char *name;
    const char *directory;
} mc_lua_workspace_t;

typedef struct mc_lua_runtime mc_lua_runtime_t;
typedef struct mc_lua_package mc_lua_package_t;
typedef struct mc_lua_subscription mc_lua_subscription_t;
typedef struct mc_lua_macro mc_lua_macro_t;
typedef struct mc_lua_package_candidate mc_lua_package_candidate_t;
typedef struct mc_lua_package_info mc_lua_package_info_t;
typedef struct mc_lua_panel_provider mc_lua_panel_provider_t;
typedef struct mc_lua_viewer_definition mc_lua_viewer_definition_t;
typedef struct mc_lua_viewer_controller mc_lua_viewer_controller_t;

typedef struct
{
    mc_runtime_handle_t handle;
} mc_lua_handle_t;

struct mc_lua_runtime
{
    const mc_runtime_host_api_v1_t *host;
    mc_runtime_plugin_context_t *context;
    GPtrArray *packages;
    GPtrArray *catalog;
    GPtrArray *macros;
    GHashTable *panel_providers;
    guint64 next_panel_provider_id;
    GHashTable *viewer_controllers;
    guint64 next_viewer_controller_id;
    GHashTable *disabled_package_ids;
    char *user_scripts_dir;
    char *user_modules_dir;
    char *legacy_user_scripts_dir;
    char *legacy_user_modules_dir;
    gboolean user_scripts_dir_overridden;
    gboolean stopping;
    mc_runtime_subscription_t macro_subscription;
};

struct mc_lua_panel_provider
{
    mc_lua_package_t *package;
    guint64 runtime_id;
    guint64 next_instance_id;
    char *id;
    char *title;
    char *prefix;
    char *help_file;
    char *help_node;
    int open_ref;
    int close_ref;
    int list_ref;
    int navigate_ref;
    int enter_ref;
    int reload_ref;
    int invoke_action_ref;
    int view_ref;
    int open_read_ref;
    int new_connection_ref;
    int edit_connection_ref;
    int copy_connection_ref;
    int rename_connection_ref;
    int delete_connection_ref;
    int connections_ref;
    mc_runtime_panel_action_t *actions;
    guint actions_count;
    GHashTable *instances;
    mc_runtime_handle_t registration;
};

static void mc_lua_cache_connection (mc_lua_panel_provider_t *provider, int source);
static gboolean mc_lua_parse_viewer_source (lua_State *lua, int table,
                                            mc_runtime_viewer_source_t *source);
static void mc_lua_viewer_source_clear (mc_runtime_viewer_source_t *source);

struct mc_lua_package
{
    mc_lua_runtime_t *runtime;
    char *id;
    char *workspace;
    char *root;
    char *entry_path;
    mc_lua_package_origin_t origin;
    guint provides;
    lua_State *lua;
    GHashTable *subscriptions;
    GPtrArray *macros;
    GPtrArray *panel_providers;
    GPtrArray *viewer_definitions;
    gboolean closed;
    guint callback_depth;
    mc_runtime_event_id_t active_event;
};

struct mc_lua_viewer_definition
{
    mc_lua_package_t *package;
    char *id;
    char *help_file;
    char *help_node;
    int open_ref;
    int initial_params_ref;
    int prepare_ref;
    int options_ref;
    int close_ref;
};

struct mc_lua_viewer_controller
{
    mc_lua_viewer_definition_t *definition;
    guint64 runtime_id;
    int definition_ref;
    int session_ref;
    int live_ref;
    int pending_ref;
    gboolean owned;
    gboolean closed;
};

typedef struct
{
    mc_runtime_dialog_t dialog;
    guint control_count;
} mc_lua_dialog_spec_t;

struct mc_lua_subscription
{
    mc_lua_package_t *package;
    guint64 token;
    guint64 *token_key;
    int callback_ref;
};

struct mc_lua_macro
{
    mc_lua_package_t *package;
    char *id;
    char *area;
    char *key;
    char *display_key;
    char *description;
    char *menu_path;
    char *menu_label;
    int menu_position;
    int priority;
    int action_ref;
    guint errors;
    gboolean disabled;
    gboolean listed;
};

struct mc_lua_package_candidate
{
    char *id;
    char *name;
    char *workspace;
    char *root;
    char *entry;
    mc_lua_package_origin_t origin;
    guint provides;
};

struct mc_lua_package_info
{
    char *id;
    char *name;
    char *workspace;
    char *root;
    mc_lua_package_origin_t origin;
    guint provides;
    gboolean disabled;
};

/*** forward declarations (file scope functions) *************************************************/

static void mc_lua_subscription_destroy (gpointer data);
static void mc_lua_macro_destroy (gpointer data);
static void mc_lua_panel_provider_destroy (gpointer data);
static void mc_lua_viewer_definition_destroy (gpointer data);
static void mc_lua_package_destroy (mc_lua_package_t *package);
static mc_runtime_event_result_t mc_lua_event_callback (gpointer runtime_context,
                                                        const mc_runtime_event_snapshot_t *snapshot,
                                                        gpointer user_data);
static mc_runtime_event_result_t
mc_lua_macro_event_callback (gpointer runtime_context, const mc_runtime_event_snapshot_t *snapshot,
                             gpointer user_data);
static int mc_lua_handle_index (lua_State *lua);
static int mc_lua_panel_active (lua_State *lua);
static int mc_lua_panel_passive (lua_State *lua);
static int mc_lua_editor_current (lua_State *lua);
static int mc_lua_viewer_current (lua_State *lua);
G_MODULE_EXPORT const mc_runtime_plugin_descriptor_v1_t *mc_runtime_plugin_register_v1 (void);

/*** file scope variables ************************************************************************/

static mc_lua_runtime_t *mc_lua_runtime_current = NULL;
static char mc_lua_module_loading_sentinel;
static const mc_lua_workspace_t mc_lua_workspaces[] = {
    { "mc", "mc" },           { "mcedit", "editor" }, { "mcview", "viewer" },
    { "mcterm", "terminal" }, { "mcdiff", "diff" },
};

/*** file scope functions ************************************************************************/

static const char *
mc_lua_system_scripts_dir (void)
{
#ifdef HAVE_TESTS
    const char *directory = g_getenv ("MC_LUA_TEST_SYSTEM_SCRIPTS_DIR");

    if (directory != NULL && directory[0] != '\0')
        return directory;
#endif

    return MC_LUA_SYSTEM_SCRIPTS_DIR;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mc_lua_system_modules_dir (void)
{
#ifdef HAVE_TESTS
    const char *directory = g_getenv ("MC_LUA_TEST_SYSTEM_MODULES_DIR");

    if (directory != NULL && directory[0] != '\0')
        return directory;
#endif

    return MC_LUA_SYSTEM_MODULES_DIR;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mc_lua_catalog_origin_name (mc_lua_package_origin_t origin)
{
    return origin == MC_LUA_PACKAGE_USER ? "user" : "global";
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_host_has_capability (const mc_lua_package_t *package, guint64 capability, gsize minimum_size)
{
    return package != NULL && package->runtime != NULL && package->runtime->host != NULL
        && package->runtime->host->struct_size >= minimum_size
        && (package->runtime->host->capability_flags & capability) != 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_log (const mc_lua_package_t *package, const char *level, const char *message)
{
    const char *id = package != NULL && package->id != NULL ? package->id : "?";

    if (mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_LOG, MC_LUA_HOST_API_LOG_SIZE)
        && package->runtime->host->log != NULL)
    {
        char *source = g_strdup_printf ("lua/%s", id);

        package->runtime->host->log (package->runtime->context, source, level, message);
        g_free (source);
        return;
    }

    fprintf (stderr, "lua/%s %s: %s\n", package != NULL && package->id != NULL ? package->id : "?",
             level, message != NULL ? message : "unknown error");
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_id_is_valid (const char *id)
{
    gsize length;
    gsize i;

    if (id == NULL)
        return FALSE;

    length = strlen (id);
    if (length == 0 || length > MC_LUA_ID_MAX_LENGTH)
        return FALSE;

    for (i = 0; i < length; i++)
        if (!g_ascii_isalnum (id[i]) && id[i] != '_' && id[i] != '.' && id[i] != '-')
            return FALSE;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static const mc_lua_workspace_t *
mc_lua_workspace_find (const char *workspace)
{
    guint i;

    if (workspace == NULL)
        return NULL;

    for (i = 0; i < G_N_ELEMENTS (mc_lua_workspaces); i++)
        if (g_strcmp0 (workspace, mc_lua_workspaces[i].name) == 0)
            return &mc_lua_workspaces[i];

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_provides_parse (const char *value, guint *provides)
{
    gchar **items;
    gchar **item;
    guint parsed = MC_LUA_PROVIDES_NONE;

    if (provides == NULL)
        return FALSE;

    *provides = MC_LUA_PROVIDES_NONE;
    if (value == NULL)
        return TRUE;
    if (value[0] == '\0')
        return FALSE;

    items = g_strsplit_set (value, ",; \t\r\n", -1);
    for (item = items; item != NULL && *item != NULL; item++)
    {
        char *name = g_strstrip (*item);

        if (name[0] == '\0')
            continue;
        if (g_ascii_strcasecmp (name, "events") == 0)
            parsed |= MC_LUA_PROVIDES_EVENTS;
        else if (g_ascii_strcasecmp (name, "macros") == 0)
            parsed |= MC_LUA_PROVIDES_MACROS;
        else
        {
            g_strfreev (items);
            return FALSE;
        }
    }
    g_strfreev (items);

    if (parsed == MC_LUA_PROVIDES_NONE)
        return FALSE;

    *provides = parsed;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mc_lua_key_name_normalize (const char *key)
{
    char *normalized;
    gsize i;

    if (key == NULL)
        return NULL;

    normalized = g_ascii_strdown (key, -1);
    g_strstrip (normalized);
    if (normalized[0] == '\0' || strlen (normalized) > MC_LUA_ID_MAX_LENGTH)
    {
        g_free (normalized);
        return NULL;
    }

    for (i = 0; normalized[i] != '\0'; i++)
        if (!g_ascii_isprint (normalized[i]) || g_ascii_isspace (normalized[i]))
        {
            g_free (normalized);
            return NULL;
        }

    return normalized;
}

/* --------------------------------------------------------------------------------------------- */

/* Lua code runs with MC's full privileges.  Reject paths that another account
   can replace, including symbolic links and writable intermediate directories. */
static gboolean
mc_lua_path_is_trusted (const char *path, gboolean expect_directory)
{
    struct stat st;

    if (path == NULL || g_lstat (path, &st) != 0)
        return FALSE;

    if (expect_directory ? !S_ISDIR (st.st_mode) : !S_ISREG (st.st_mode))
        return FALSE;

    return (st.st_uid == 0 || st.st_uid == geteuid ()) && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_file_is_trusted_under (const char *root, const char *path)
{
    char *canonical_root;
    char *canonical_path;
    char *directory;
    gboolean trusted = FALSE;
    gsize root_length;

    if (root == NULL || path == NULL)
        return FALSE;

    canonical_root = g_canonicalize_filename (root, NULL);
    canonical_path = g_canonicalize_filename (path, NULL);
    root_length = strlen (canonical_root);

    if (!g_str_has_prefix (canonical_path, canonical_root)
        || (root_length > 1 && canonical_path[root_length] != G_DIR_SEPARATOR)
        || !mc_lua_path_is_trusted (canonical_root, TRUE))
        goto done;

    directory = g_path_get_dirname (canonical_path);
    while (TRUE)
    {
        char *parent;

        if (!mc_lua_path_is_trusted (directory, TRUE))
            break;
        if (g_strcmp0 (directory, canonical_root) == 0)
        {
            trusted = mc_lua_path_is_trusted (canonical_path, FALSE);
            break;
        }

        parent = g_path_get_dirname (directory);
        if (g_strcmp0 (parent, directory) == 0)
        {
            g_free (parent);
            break;
        }
        g_free (directory);
        directory = parent;
    }
    g_free (directory);

done:
    g_free (canonical_path);
    g_free (canonical_root);
    return trusted;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_relative_lua_file_is_valid (const char *filename)
{
    const char *part;
    const char *next;

    if (filename == NULL || filename[0] == '\0' || g_path_is_absolute (filename)
        || !g_str_has_suffix (filename, ".lua") || strchr (filename, '\\') != NULL)
        return FALSE;

    part = filename;
    while (part != NULL)
    {
        next = strchr (part, G_DIR_SEPARATOR);
        if (next == part || (next == NULL && strcmp (part, ".") == 0)
            || (next == NULL && strcmp (part, "..") == 0)
            || (next != NULL && next - part == 1 && part[0] == '.')
            || (next != NULL && next - part == 2 && part[0] == '.' && part[1] == '.'))
            return FALSE;

        part = next != NULL ? next + 1 : NULL;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_module_name_is_valid (const char *name)
{
    gboolean beginning = TRUE;
    const char *cursor;

    if (name == NULL || name[0] == '\0')
        return FALSE;

    for (cursor = name; *cursor != '\0'; cursor++)
    {
        if (*cursor == '.')
        {
            if (beginning)
                return FALSE;
            beginning = TRUE;
        }
        else if (beginning)
        {
            if (!g_ascii_isalpha (*cursor) && *cursor != '_')
                return FALSE;
            beginning = FALSE;
        }
        else if (!g_ascii_isalnum (*cursor) && *cursor != '_')
            return FALSE;
    }

    return !beginning;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_id_t
mc_lua_event_id_from_name (const char *event_name)
{
    static const char *const event_names[MC_RUNTIME_EVENT_COUNT] = {
        NULL,
        MCEVENT_RUNTIME_STARTUP,
        MCEVENT_RUNTIME_SHUTDOWN,
        MCEVENT_RUNTIME_PANEL_CHDIR,
        MCEVENT_RUNTIME_PANEL_SELECTION_CHANGED,
        MCEVENT_RUNTIME_PANEL_FILE_OPEN,
        MCEVENT_RUNTIME_EDITOR_OPEN,
        MCEVENT_RUNTIME_EDITOR_SAVE,
        MCEVENT_RUNTIME_EDITOR_KEY,
        MCEVENT_RUNTIME_VIEWER_OPEN,
    };
    mc_runtime_event_id_t event_id;

    if (event_name == NULL)
        return MC_RUNTIME_EVENT_INVALID;

    for (event_id = MC_RUNTIME_EVENT_STARTUP; event_id < MC_RUNTIME_EVENT_COUNT; event_id++)
        if (strcmp (event_name, event_names[event_id]) == 0)
            return event_id;

    return MC_RUNTIME_EVENT_INVALID;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mc_lua_event_name (mc_runtime_event_id_t event_id)
{
    static const char *const event_names[MC_RUNTIME_EVENT_COUNT] = {
        NULL,
        MCEVENT_RUNTIME_STARTUP,
        MCEVENT_RUNTIME_SHUTDOWN,
        MCEVENT_RUNTIME_PANEL_CHDIR,
        MCEVENT_RUNTIME_PANEL_SELECTION_CHANGED,
        MCEVENT_RUNTIME_PANEL_FILE_OPEN,
        MCEVENT_RUNTIME_EDITOR_OPEN,
        MCEVENT_RUNTIME_EDITOR_SAVE,
        MCEVENT_RUNTIME_EDITOR_KEY,
        MCEVENT_RUNTIME_VIEWER_OPEN,
    };

    if (event_id <= MC_RUNTIME_EVENT_INVALID || event_id >= MC_RUNTIME_EVENT_COUNT)
        return NULL;

    return event_names[event_id];
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_handle_is_valid (const mc_runtime_handle_t *handle)
{
    return handle != NULL && handle->kind > MC_RUNTIME_HANDLE_INVALID
        && handle->kind <= MC_RUNTIME_HANDLE_VIEWER && handle->id != 0 && handle->generation != 0;
}

/* --------------------------------------------------------------------------------------------- */

static mc_lua_package_t *
mc_lua_package_from_state (lua_State *lua)
{
    mc_lua_package_t *package;

    lua_getfield (lua, LUA_REGISTRYINDEX, MC_LUA_REGISTRY_PACKAGE);
    package = (mc_lua_package_t *) lua_touserdata (lua, -1);
    lua_pop (lua, 1);

    return package;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_return_error (lua_State *lua, const char *message)
{
    lua_pushnil (lua);
    lua_pushstring (lua, message != NULL ? message : "failed");
    return 2;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_package_has_active_context (const mc_lua_package_t *package)
{
    return package != NULL && package->callback_depth != 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_require_active_context (lua_State *lua, const mc_lua_package_t *package)
{
    if (mc_lua_package_has_active_context (package))
        return TRUE;

    (void) mc_lua_return_error (lua, "no active MC context");
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_require_object_capability (lua_State *lua, const mc_lua_package_t *package,
                                  guint64 capability)
{
    if (mc_lua_host_has_capability (package, capability, MC_LUA_HOST_API_OBJECTS_SIZE))
        return TRUE;

    (void) mc_lua_return_error (lua, "not_ready");
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_mutation_is_allowed (lua_State *lua, const mc_lua_package_t *package)
{
    if (package != NULL && package->active_event == MC_RUNTIME_EVENT_PANEL_FILE_OPEN)
    {
        (void) mc_lua_return_error (lua, "forbidden_in_phase");
        return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_get_handle (lua_State *lua, int index, mc_runtime_handle_kind_t expected_kind,
                   mc_runtime_handle_t *handle)
{
    const mc_lua_handle_t *lua_handle =
        (const mc_lua_handle_t *) luaL_testudata (lua, index, MC_LUA_HANDLE_METATABLE);

    if (lua_handle == NULL || lua_handle->handle.kind != expected_kind)
    {
        (void) mc_lua_return_error (lua, "invalid_handle");
        return FALSE;
    }

    if (handle != NULL)
        *handle = lua_handle->handle;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_check_string_without_nul (lua_State *lua, int index, const char **value)
{
    size_t length;
    const char *string = luaL_checklstring (lua, index, &length);

    if (memchr (string, '\0', length) != NULL)
    {
        (void) mc_lua_return_error (lua, "invalid_argument");
        return FALSE;
    }

    if (value != NULL)
        *value = string;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_host_string (lua_State *lua, const mc_lua_package_t *package,
                         mc_runtime_string_t *string)
{
    if (string != NULL && string->data != NULL)
        lua_pushlstring (lua, string->data, string->length);
    else
        lua_pushliteral (lua, "");

    if (package != NULL && package->runtime != NULL && package->runtime->host != NULL
        && package->runtime->host->string_free != NULL)
        package->runtime->host->string_free (package->runtime->context, string);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_set_string_field (lua_State *lua, const char *name, const char *value)
{
    if (value != NULL)
        lua_pushlstring (lua, value, strlen (value));
    else
        lua_pushnil (lua);
    lua_setfield (lua, -2, name);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_set_boolean_field (lua_State *lua, const char *name, gboolean value)
{
    lua_pushboolean (lua, value);
    lua_setfield (lua, -2, name);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_set_integer_field (lua_State *lua, const char *name, gint64 value)
{
    lua_pushinteger (lua, (lua_Integer) value);
    lua_setfield (lua, -2, name);
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_handle_tostring (lua_State *lua)
{
    const mc_lua_handle_t *handle = (const mc_lua_handle_t *) lua_touserdata (lua, 1);
    const char *kind = "invalid";

    if (handle != NULL)
    {
        switch (handle->handle.kind)
        {
        case MC_RUNTIME_HANDLE_PANEL:
            kind = "panel";
            break;
        case MC_RUNTIME_HANDLE_EDITOR:
            kind = "editor";
            break;
        case MC_RUNTIME_HANDLE_VIEWER:
            kind = "viewer";
            break;
        case MC_RUNTIME_HANDLE_INVALID:
        default:
            break;
        }
    }

    lua_pushfstring (lua, "mc.%s handle", kind);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_handle (lua_State *lua, const mc_runtime_handle_t *handle)
{
    mc_lua_handle_t *lua_handle;

    if (!mc_lua_handle_is_valid (handle))
    {
        lua_pushnil (lua);
        return;
    }

    lua_handle = (mc_lua_handle_t *) lua_newuserdata (lua, sizeof (*lua_handle));
    lua_handle->handle = *handle;

    if (luaL_newmetatable (lua, MC_LUA_HANDLE_METATABLE))
    {
        lua_pushcfunction (lua, mc_lua_handle_tostring);
        lua_setfield (lua, -2, "__tostring");
        lua_pushcfunction (lua, mc_lua_handle_index);
        lua_setfield (lua, -2, "__index");
    }
    lua_setmetatable (lua, -2);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_file (lua_State *lua, const mc_runtime_file_snapshot_t *file)
{
    if (file == NULL)
    {
        lua_pushnil (lua);
        return;
    }

    lua_createtable (lua, 0, 6);
    mc_lua_set_string_field (lua, "name", file->name);
    mc_lua_set_string_field (lua, "path", file->path);
    mc_lua_set_boolean_field (lua, "is_dir", file->is_dir);
    mc_lua_set_integer_field (lua, "size", (gint64) file->size);
    mc_lua_set_integer_field (lua, "mtime", file->mtime);
    mc_lua_set_boolean_field (lua, "marked", file->marked);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_selected_files (lua_State *lua, const GPtrArray *selected)
{
    guint i;

    lua_createtable (lua, selected != NULL ? (int) selected->len : 0, 0);
    if (selected == NULL)
        return;

    for (i = 0; i < selected->len; i++)
    {
        mc_lua_push_file (
            lua,
            (const mc_runtime_file_snapshot_t *) g_ptr_array_index ((GPtrArray *) selected, i));
        lua_rawseti (lua, -2, (lua_Integer) i + 1);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_event (lua_State *lua, const mc_runtime_event_snapshot_t *snapshot)
{
    lua_createtable (lua, 0, 7);
    mc_lua_set_string_field (lua, "name", mc_lua_event_name (snapshot->event_id));

    switch (snapshot->event_id)
    {
    case MC_RUNTIME_EVENT_STARTUP:
        mc_lua_set_string_field (lua, "run_mode", snapshot->data.startup.run_mode);
        mc_lua_set_string_field (lua, "config_dir", snapshot->data.startup.config_dir);
        mc_lua_set_string_field (lua, "data_dir", snapshot->data.startup.data_dir);
        break;

    case MC_RUNTIME_EVENT_SHUTDOWN:
        mc_lua_set_string_field (lua, "reason", snapshot->data.shutdown.reason);
        break;

    case MC_RUNTIME_EVENT_PANEL_CHDIR:
        mc_lua_push_handle (lua, &snapshot->data.panel_chdir.panel);
        lua_setfield (lua, -2, "panel");
        mc_lua_set_string_field (lua, "old_path", snapshot->data.panel_chdir.old_path);
        mc_lua_set_string_field (lua, "new_path", snapshot->data.panel_chdir.new_path);
        mc_lua_set_string_field (lua, "cause", snapshot->data.panel_chdir.cause);
        break;

    case MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED:
        mc_lua_push_handle (lua, &snapshot->data.panel_selection_changed.panel);
        lua_setfield (lua, -2, "panel");
        mc_lua_push_file (lua, snapshot->data.panel_selection_changed.current);
        lua_setfield (lua, -2, "current");
        mc_lua_push_selected_files (lua, snapshot->data.panel_selection_changed.selected);
        lua_setfield (lua, -2, "selected");
        mc_lua_set_integer_field (lua, "selected_count",
                                  snapshot->data.panel_selection_changed.selected_count);
        mc_lua_set_boolean_field (lua, "selected_truncated",
                                  snapshot->data.panel_selection_changed.selected_truncated);
        break;

    case MC_RUNTIME_EVENT_PANEL_FILE_OPEN:
        mc_lua_push_handle (lua, &snapshot->data.panel_file_open.panel);
        lua_setfield (lua, -2, "panel");
        mc_lua_set_string_field (lua, "path", snapshot->data.panel_file_open.path);
        mc_lua_set_string_field (lua, "open_mode", snapshot->data.panel_file_open.open_mode);
        mc_lua_set_boolean_field (lua, "is_dir", snapshot->data.panel_file_open.is_dir);
        break;

    case MC_RUNTIME_EVENT_EDITOR_OPEN:
        mc_lua_push_handle (lua, &snapshot->data.editor_open.editor);
        lua_setfield (lua, -2, "editor");
        mc_lua_set_string_field (lua, "path", snapshot->data.editor_open.path);
        mc_lua_set_boolean_field (lua, "readonly", snapshot->data.editor_open.readonly);
        mc_lua_set_integer_field (lua, "line", snapshot->data.editor_open.line);
        mc_lua_set_integer_field (lua, "column", snapshot->data.editor_open.column);
        break;

    case MC_RUNTIME_EVENT_EDITOR_SAVE:
        mc_lua_push_handle (lua, &snapshot->data.editor_save.editor);
        lua_setfield (lua, -2, "editor");
        mc_lua_set_string_field (lua, "path", snapshot->data.editor_save.path);
        mc_lua_set_string_field (lua, "previous_path", snapshot->data.editor_save.previous_path);
        mc_lua_set_boolean_field (lua, "save_as", snapshot->data.editor_save.save_as);
        break;

    case MC_RUNTIME_EVENT_EDITOR_KEY:
        mc_lua_push_handle (lua, &snapshot->data.editor_key.editor);
        lua_setfield (lua, -2, "editor");
        lua_createtable (lua, 0, 6);
        mc_lua_set_string_field (lua, "name", snapshot->data.editor_key.key.name);
        mc_lua_set_integer_field (lua, "code", snapshot->data.editor_key.key.code);
        mc_lua_set_string_field (lua, "text", snapshot->data.editor_key.key.text);
        lua_createtable (lua, 0, 3);
        mc_lua_set_boolean_field (lua, "shift", snapshot->data.editor_key.key.shift);
        mc_lua_set_boolean_field (lua, "ctrl", snapshot->data.editor_key.key.ctrl);
        mc_lua_set_boolean_field (lua, "alt", snapshot->data.editor_key.key.alt);
        lua_setfield (lua, -2, "modifiers");
        lua_setfield (lua, -2, "key");
        break;

    case MC_RUNTIME_EVENT_VIEWER_OPEN:
        mc_lua_push_handle (lua, &snapshot->data.viewer_open.viewer);
        lua_setfield (lua, -2, "viewer");
        mc_lua_set_string_field (lua, "path", snapshot->data.viewer_open.path);
        mc_lua_set_string_field (lua, "source_kind", snapshot->data.viewer_open.source_kind);
        mc_lua_set_integer_field (lua, "start_line", snapshot->data.viewer_open.start_line);
        break;

    case MC_RUNTIME_EVENT_INVALID:
    case MC_RUNTIME_EVENT_COUNT:
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_panel_cwd (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_string_t path = { NULL, 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_PANEL)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_PANEL, &handle))
        return 2;

    if (!package->runtime->host->panel_cwd (package->runtime->context, &handle, &path, &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_host_string (lua, package, &path);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_panel_current (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_file_snapshot_t *file = NULL;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_PANEL)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_PANEL, &handle))
        return 2;

    if (!package->runtime->host->panel_current (package->runtime->context, &handle, &file, &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_file (lua, file);
    package->runtime->host->file_snapshot_free (package->runtime->context, file);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_panel_selected (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_file_list_t files = { NULL, 0, 0, FALSE };
    const char *error = NULL;
    guint i;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_PANEL)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_PANEL, &handle))
        return 2;

    if (!package->runtime->host->panel_selected (package->runtime->context, &handle, &files,
                                                 &error))
        return mc_lua_return_error (lua, error);

    lua_createtable (lua, (int) files.len, 0);
    for (i = 0; i < files.len; i++)
    {
        mc_lua_push_file (lua, files.items[i]);
        lua_rawseti (lua, -2, (lua_Integer) i + 1);
    }
    package->runtime->host->file_list_free (package->runtime->context, &files);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_panel_refresh (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_PANEL)
        || !mc_lua_mutation_is_allowed (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_PANEL, &handle))
        return 2;

    if (!package->runtime->host->panel_refresh (package->runtime->context, &handle, &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_panel_chdir (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    const char *path;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_PANEL)
        || !mc_lua_mutation_is_allowed (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_PANEL, &handle)
        || !mc_lua_check_string_without_nul (lua, 2, &path))
        return 2;

    if (!package->runtime->host->panel_chdir (package->runtime->context, &handle, path, &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_path (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_string_t path = { NULL, 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (!package->runtime->host->editor_path (package->runtime->context, &handle, &path, &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_host_string (lua, package, &path);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_info (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_info_t info = { 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_INFO_SIZE)
        || package->runtime->host->editor_info == NULL
        || package->runtime->host->editor_info_free == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_info (package->runtime->context, &handle, &info, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

    lua_createtable (lua, 0, 7);
    if (info.has_path)
    {
        lua_pushlstring (lua, info.path, info.path_length);
        lua_setfield (lua, -2, "path");
    }
    lua_pushlstring (lua, info.name, info.name_length);
    lua_setfield (lua, -2, "name");
    mc_lua_set_boolean_field (lua, "modified", info.modified);
    mc_lua_set_boolean_field (lua, "readonly", info.readonly);
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) info.revision);
    mc_lua_set_integer_field (lua, "byte_length", (lua_Integer) info.byte_length);
    mc_lua_set_integer_field (lua, "line_count", (lua_Integer) info.line_count);
    package->runtime->host->editor_info_free (package->runtime->context, &info);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_push_editor_position (lua_State *lua, const mc_runtime_editor_position_t *position)
{
    lua_createtable (lua, 0, 3);
    mc_lua_set_integer_field (lua, "offset", (lua_Integer) position->offset);
    mc_lua_set_integer_field (lua, "line", (lua_Integer) position->line);
    mc_lua_set_integer_field (lua, "column", (lua_Integer) position->column);
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_selection (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_selection_t selection = { 0 };
    const char *error = NULL;
    const char *kind;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_SELECTION_SIZE)
        || package->runtime->host->editor_selection == NULL
        || package->runtime->host->editor_selection_free == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_selection (package->runtime->context, &handle, &selection,
                                                   &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

    switch (selection.kind)
    {
    case MC_RUNTIME_EDITOR_SELECTION_NONE:
        kind = "none";
        break;
    case MC_RUNTIME_EDITOR_SELECTION_LINEAR:
        kind = "linear";
        break;
    case MC_RUNTIME_EDITOR_SELECTION_COLUMN:
        kind = "column";
        break;
    default:
        package->runtime->host->editor_selection_free (package->runtime->context, &selection);
        return mc_lua_return_error (lua, "invalid_result");
    }

    lua_createtable (lua, 0, 7);
    lua_pushstring (lua, kind);
    lua_setfield (lua, -2, "kind");
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) selection.revision);
    mc_lua_push_editor_position (lua, &selection.anchor);
    lua_setfield (lua, -2, "anchor");
    mc_lua_push_editor_position (lua, &selection.cursor);
    lua_setfield (lua, -2, "cursor");
    lua_createtable (lua, (int) selection.ranges_count, 0);
    for (guint i = 0; i < selection.ranges_count; i++)
    {
        lua_createtable (lua, 0, 2);
        mc_lua_set_integer_field (lua, "from", (lua_Integer) selection.ranges[i].from);
        mc_lua_set_integer_field (lua, "to", (lua_Integer) selection.ranges[i].to);
        lua_rawseti (lua, -2, (lua_Integer) i + 1);
    }
    lua_setfield (lua, -2, "ranges");
    if (selection.has_text)
    {
        lua_pushlstring (lua, selection.text, selection.text_length);
        lua_setfield (lua, -2, "text");
    }
    mc_lua_set_boolean_field (lua, "text_truncated", selection.text_truncated);
    package->runtime->host->editor_selection_free (package->runtime->context, &selection);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_replace_selection (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_edit_result_t result = { 0 };
    mc_runtime_editor_selection_t selection = { 0 };
    const char *error = NULL;
    const char *replacement;
    size_t replacement_length;

    if (!mc_lua_require_active_context (lua, package) || !mc_lua_mutation_is_allowed (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    replacement = luaL_checklstring (lua, 2, &replacement_length);
    if (mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                    MC_LUA_HOST_API_EDITOR_REPLACE_SELECTION_V2_SIZE)
        && package->runtime->host->editor_replace_selection_v2 != NULL
        && package->runtime->host->editor_selection != NULL
        && package->runtime->host->editor_selection_free != NULL)
    {
        if (!package->runtime->host->editor_selection (package->runtime->context, &handle,
                                                       &selection, &error))
            return mc_lua_return_error (lua, error != NULL ? error : "failed");
        if (!package->runtime->host->editor_replace_selection_v2 (
                package->runtime->context, &handle, selection.revision, replacement,
                replacement_length, &result, &error))
        {
            package->runtime->host->editor_selection_free (package->runtime->context, &selection);
            return mc_lua_return_error (lua, error != NULL ? error : "failed");
        }
        package->runtime->host->editor_selection_free (package->runtime->context, &selection);
        goto success;
    }
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_REPLACE_SELECTION_SIZE)
        || package->runtime->host->editor_replace_selection == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_replace_selection (
            package->runtime->context, &handle, replacement, replacement_length, &result, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

success:
    lua_createtable (lua, 0, 2);
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) result.revision);
    mc_lua_push_editor_position (lua, &result.cursor);
    lua_setfield (lua, -2, "cursor");
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean mc_lua_table_uint64 (lua_State *lua, int index, const char *field, guint64 *value,
                                     gboolean required);

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_replace (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_edit_result_t result = { 0 };
    lua_Integer from;
    lua_Integer to;
    const char *replacement;
    size_t replacement_length;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package) || !mc_lua_mutation_is_allowed (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (lua_istable (lua, 2))
    {
        mc_runtime_editor_change_t change = { 0 };
        mc_runtime_editor_edit_t edit_spec = { 0 };

        (void) mc_lua_table_uint64 (lua, 2, "from", &change.from, TRUE);
        (void) mc_lua_table_uint64 (lua, 2, "to", &change.to, TRUE);
        (void) mc_lua_table_uint64 (lua, 2, "revision", &edit_spec.revision, TRUE);
        replacement = luaL_checklstring (lua, 3, &replacement_length);
        change.text = replacement;
        change.text_length = replacement_length;
        edit_spec.changes = &change;
        edit_spec.changes_count = 1;
        if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                         MC_LUA_HOST_API_EDITOR_EDIT_SIZE)
            || package->runtime->host->editor_edit == NULL)
            return mc_lua_return_error (lua, "not_ready");
        if (!package->runtime->host->editor_edit (package->runtime->context, &handle, &edit_spec,
                                                  &result, &error))
            return mc_lua_return_error (lua, error != NULL ? error : "failed");
        goto success;
    }
    from = luaL_checkinteger (lua, 2);
    to = luaL_checkinteger (lua, 3);
    replacement = luaL_checklstring (lua, 4, &replacement_length);
    if (from < 0 || to < from)
        return mc_lua_return_error (lua, "invalid_range");
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_REPLACE_SIZE)
        || package->runtime->host->editor_replace == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_replace (package->runtime->context, &handle, (guint64) from,
                                                 (guint64) to, replacement, replacement_length,
                                                 &result, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

success:
    lua_createtable (lua, 0, 2);
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) result.revision);
    mc_lua_push_editor_position (lua, &result.cursor);
    lua_setfield (lua, -2, "cursor");
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_table_uint64 (lua_State *lua, int index, const char *field, guint64 *value,
                     gboolean required)
{
    lua_Integer integer;

    lua_getfield (lua, index, field);
    if (lua_isnil (lua, -1) && !required)
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    integer = luaL_checkinteger (lua, -1);
    lua_pop (lua, 1);
    if (integer < 0)
        luaL_error (lua, "%s must be non-negative", field);
    *value = (guint64) integer;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_text (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_range_t range = { 0 };
    mc_runtime_string_t text = { 0 };
    guint64 revision = 0;
    gboolean has_range = !lua_isnoneornil (lua, 2);
    gboolean has_revision = FALSE;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_TEXT_SIZE)
        || package->runtime->host->editor_text == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (has_range)
    {
        luaL_checktype (lua, 2, LUA_TTABLE);
        (void) mc_lua_table_uint64 (lua, 2, "from", &range.from, TRUE);
        (void) mc_lua_table_uint64 (lua, 2, "to", &range.to, TRUE);
        has_revision = mc_lua_table_uint64 (lua, 2, "revision", &revision, FALSE);
    }
    if (!package->runtime->host->editor_text (package->runtime->context, &handle,
                                              has_range ? &range : NULL, has_revision, revision,
                                              &text, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");
    mc_lua_push_host_string (lua, package, &text);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_edit (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_editor_edit_t edit_spec = { 0 };
    mc_runtime_editor_edit_result_t result = { 0 };
    mc_runtime_editor_change_t *changes;
    const char *error = NULL;
    int changes_table_index;
    size_t changes_count;

    if (!mc_lua_require_active_context (lua, package) || !mc_lua_mutation_is_allowed (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    luaL_checktype (lua, 2, LUA_TTABLE);
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_EDIT_SIZE)
        || package->runtime->host->editor_edit == NULL)
        return mc_lua_return_error (lua, "not_ready");

    (void) mc_lua_table_uint64 (lua, 2, "revision", &edit_spec.revision, TRUE);
    lua_getfield (lua, 2, "changes");
    luaL_checktype (lua, -1, LUA_TTABLE);
    changes_table_index = lua_gettop (lua);
    changes_count = lua_rawlen (lua, -1);
    if (changes_count == 0 || changes_count > 1024)
    {
        lua_pop (lua, 1);
        return mc_lua_return_error (lua, "invalid_edit");
    }
    /* Keep the temporary native array owned by Lua.  luaL_check* below may
     * raise a longjmp on malformed input, in which case a heap allocation
     * made with g_new0() would leak. */
    changes = (mc_runtime_editor_change_t *) lua_newuserdata (
        lua, sizeof (mc_runtime_editor_change_t) * changes_count);
    memset (changes, 0, sizeof (mc_runtime_editor_change_t) * changes_count);
    for (size_t i = 0; i < changes_count; i++)
    {
        size_t length;

        lua_rawgeti (lua, changes_table_index, (lua_Integer) i + 1);
        luaL_checktype (lua, -1, LUA_TTABLE);
        (void) mc_lua_table_uint64 (lua, lua_gettop (lua), "from", &changes[i].from, TRUE);
        (void) mc_lua_table_uint64 (lua, lua_gettop (lua), "to", &changes[i].to, TRUE);
        lua_getfield (lua, -1, "text");
        changes[i].text = luaL_checklstring (lua, -1, &length);
        changes[i].text_length = length;
        lua_pop (lua, 2);
    }
    edit_spec.changes = changes;
    edit_spec.changes_count = (guint) changes_count;
    lua_getfield (lua, 2, "cursor");
    if (!lua_isnil (lua, -1))
    {
        luaL_checktype (lua, -1, LUA_TTABLE);
        edit_spec.has_cursor = TRUE;
        (void) mc_lua_table_uint64 (lua, lua_gettop (lua), "offset", &edit_spec.cursor.offset,
                                    TRUE);
    }
    lua_pop (lua, 1);

    if (!package->runtime->host->editor_edit (package->runtime->context, &handle, &edit_spec,
                                              &result, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");
    lua_createtable (lua, 0, 2);
    mc_lua_set_integer_field (lua, "revision", (lua_Integer) result.revision);
    mc_lua_push_editor_position (lua, &result.cursor);
    lua_setfield (lua, -2, "cursor");
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_cursor (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    guint64 line;
    guint64 column;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (!package->runtime->host->editor_cursor (package->runtime->context, &handle, &line, &column,
                                                &error))
        return mc_lua_return_error (lua, error);

    lua_pushinteger (lua, (lua_Integer) line);
    lua_pushinteger (lua, (lua_Integer) column);
    return 2;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_set_cursor (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    lua_Integer line;
    lua_Integer column;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_mutation_is_allowed (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    line = luaL_checkinteger (lua, 2);
    column = luaL_checkinteger (lua, 3);
    if (line <= 0 || column <= 0)
        return mc_lua_return_error (lua, "invalid_argument");

    if (!package->runtime->host->editor_set_cursor (package->runtime->context, &handle,
                                                    (guint64) line, (guint64) column, &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_is_readonly (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    gboolean readonly;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (!package->runtime->host->editor_is_readonly (package->runtime->context, &handle, &readonly,
                                                     &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, readonly);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_tab_width (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    guint tab_width;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_EDITOR,
                                     MC_LUA_HOST_API_EDITOR_TAB_WIDTH_SIZE)
        || package->runtime->host->editor_tab_width == NULL)
        return mc_lua_return_error (lua, "not_ready");
    if (!package->runtime->host->editor_tab_width (package->runtime->context, &handle, &tab_width,
                                                   &error))
        return mc_lua_return_error (lua, error != NULL ? error : "failed");

    lua_pushinteger (lua, (lua_Integer) tab_width);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_get_text (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    lua_Integer from;
    lua_Integer to;
    mc_runtime_string_t text = { NULL, 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    from = luaL_checkinteger (lua, 2);
    to = luaL_checkinteger (lua, 3);
    if (!package->runtime->host->editor_get_text (package->runtime->context, &handle, from, to,
                                                  &text, &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_host_string (lua, package, &text);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_selected_text (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_string_t text = { NULL, 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (package->runtime->host->struct_size < MC_LUA_HOST_API_EDITOR_SELECTED_TEXT_SIZE
        || package->runtime->host->editor_selected_text == NULL)
        return mc_lua_return_error (lua, "not_supported");
    if (!package->runtime->host->editor_selected_text (package->runtime->context, &handle, &text,
                                                       &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_host_string (lua, package, &text);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_insert (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    const char *text;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_mutation_is_allowed (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle)
        || !mc_lua_check_string_without_nul (lua, 2, &text))
        return 2;

    if (!package->runtime->host->editor_insert (package->runtime->context, &handle, text, &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_save (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR)
        || !mc_lua_mutation_is_allowed (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_EDITOR, &handle))
        return 2;

    if (!package->runtime->host->editor_save (package->runtime->context, &handle, &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_viewer_path (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_string_t path = { NULL, 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_VIEWER)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_VIEWER, &handle))
        return 2;

    if (!package->runtime->host->viewer_path (package->runtime->context, &handle, &path, &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_host_string (lua, package, &path);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_viewer_position (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    gint64 offset;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_VIEWER)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_VIEWER, &handle))
        return 2;

    if (!package->runtime->host->viewer_position (package->runtime->context, &handle, &offset,
                                                  &error))
        return mc_lua_return_error (lua, error);

    lua_pushinteger (lua, (lua_Integer) offset);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_viewer_goto (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    lua_Integer offset;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_VIEWER)
        || !mc_lua_mutation_is_allowed (lua, package)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_VIEWER, &handle))
        return 2;

    offset = luaL_checkinteger (lua, 2);
    if (offset < 0)
        return mc_lua_return_error (lua, "invalid_argument");
    if (!package->runtime->host->viewer_goto (package->runtime->context, &handle, offset, &error))
        return mc_lua_return_error (lua, error);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_viewer_mode (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;
    mc_runtime_string_t mode = { NULL, 0 };
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_VIEWER)
        || !mc_lua_get_handle (lua, 1, MC_RUNTIME_HANDLE_VIEWER, &handle))
        return 2;

    if (!package->runtime->host->viewer_mode (package->runtime->context, &handle, &mode, &error))
        return mc_lua_return_error (lua, error);

    mc_lua_push_host_string (lua, package, &mode);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_handle_index (lua_State *lua)
{
    const mc_lua_handle_t *handle =
        (const mc_lua_handle_t *) luaL_testudata (lua, 1, MC_LUA_HANDLE_METATABLE);
    const char *method;

    if (handle == NULL || !lua_isstring (lua, 2))
    {
        lua_pushnil (lua);
        return 1;
    }

    method = lua_tostring (lua, 2);
    switch (handle->handle.kind)
    {
    case MC_RUNTIME_HANDLE_PANEL:
        if (strcmp (method, "cwd") == 0)
            lua_pushcfunction (lua, mc_lua_panel_cwd);
        else if (strcmp (method, "current") == 0)
            lua_pushcfunction (lua, mc_lua_panel_current);
        else if (strcmp (method, "selected") == 0)
            lua_pushcfunction (lua, mc_lua_panel_selected);
        else if (strcmp (method, "refresh") == 0)
            lua_pushcfunction (lua, mc_lua_panel_refresh);
        else if (strcmp (method, "chdir") == 0)
            lua_pushcfunction (lua, mc_lua_panel_chdir);
        else
            lua_pushnil (lua);
        break;

    case MC_RUNTIME_HANDLE_EDITOR:
        if (strcmp (method, "path") == 0)
            lua_pushcfunction (lua, mc_lua_editor_path);
        else if (strcmp (method, "info") == 0)
            lua_pushcfunction (lua, mc_lua_editor_info);
        else if (strcmp (method, "selection") == 0)
            lua_pushcfunction (lua, mc_lua_editor_selection);
        else if (strcmp (method, "replace_selection") == 0)
            lua_pushcfunction (lua, mc_lua_editor_replace_selection);
        else if (strcmp (method, "replace") == 0)
            lua_pushcfunction (lua, mc_lua_editor_replace);
        else if (strcmp (method, "text") == 0)
            lua_pushcfunction (lua, mc_lua_editor_text);
        else if (strcmp (method, "edit") == 0)
            lua_pushcfunction (lua, mc_lua_editor_edit);
        else if (strcmp (method, "cursor") == 0)
            lua_pushcfunction (lua, mc_lua_editor_cursor);
        else if (strcmp (method, "set_cursor") == 0)
            lua_pushcfunction (lua, mc_lua_editor_set_cursor);
        else if (strcmp (method, "is_readonly") == 0)
            lua_pushcfunction (lua, mc_lua_editor_is_readonly);
        else if (strcmp (method, "tab_width") == 0)
            lua_pushcfunction (lua, mc_lua_editor_tab_width);
        else if (strcmp (method, "get_text") == 0)
            lua_pushcfunction (lua, mc_lua_editor_get_text);
        else if (strcmp (method, "selected_text") == 0)
            lua_pushcfunction (lua, mc_lua_editor_selected_text);
        else if (strcmp (method, "insert") == 0)
            lua_pushcfunction (lua, mc_lua_editor_insert);
        else if (strcmp (method, "save") == 0)
            lua_pushcfunction (lua, mc_lua_editor_save);
        else
            lua_pushnil (lua);
        break;

    case MC_RUNTIME_HANDLE_VIEWER:
        if (strcmp (method, "path") == 0)
            lua_pushcfunction (lua, mc_lua_viewer_path);
        else if (strcmp (method, "position") == 0)
            lua_pushcfunction (lua, mc_lua_viewer_position);
        else if (strcmp (method, "goto") == 0)
            lua_pushcfunction (lua, mc_lua_viewer_goto);
        else if (strcmp (method, "mode") == 0)
            lua_pushcfunction (lua, mc_lua_viewer_mode);
        else
            lua_pushnil (lua);
        break;

    case MC_RUNTIME_HANDLE_INVALID:
    default:
        lua_pushnil (lua);
        break;
    }

    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_panel_active (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_PANEL))
        return 2;

    handle = package->runtime->host->panel_active (package->runtime->context);
    if (!mc_lua_handle_is_valid (&handle))
        return mc_lua_return_error (lua, "not_ready");
    mc_lua_push_handle (lua, &handle);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_panel_passive (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_PANEL))
        return 2;

    handle = package->runtime->host->panel_passive (package->runtime->context);
    if (!mc_lua_handle_is_valid (&handle))
        return mc_lua_return_error (lua, "not_ready");
    mc_lua_push_handle (lua, &handle);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_editor_current (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_EDITOR))
        return 2;

    handle = package->runtime->host->editor_current (package->runtime->context);
    if (!mc_lua_handle_is_valid (&handle))
        return mc_lua_return_error (lua, "not_ready");
    mc_lua_push_handle (lua, &handle);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_viewer_current (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_runtime_handle_t handle;

    if (!mc_lua_require_active_context (lua, package)
        || !mc_lua_require_object_capability (lua, package, MC_RUNTIME_HOST_CAP_VIEWER))
        return 2;

    handle = package->runtime->host->viewer_current (package->runtime->context);
    if (!mc_lua_handle_is_valid (&handle))
        return mc_lua_return_error (lua, "not_ready");
    mc_lua_push_handle (lua, &handle);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_report_error (mc_lua_package_t *package, mc_runtime_error_phase_t phase, const char *summary)
{
    lua_State *lua = package->lua;
    const char *message = lua_tostring (lua, -1);
    const char *traceback;

    luaL_traceback (lua, lua, message != NULL ? message : "Lua error", 1);
    traceback = lua_tostring (lua, -1);
    if (package->runtime->host->struct_size >= MC_LUA_HOST_API_RUNTIME_ERROR_SIZE
        && package->runtime->host->runtime_error != NULL)
        package->runtime->host->runtime_error (package->runtime->context, "lua", package->id, phase,
                                               summary,
                                               traceback != NULL ? traceback : "Lua error");
    else
        mc_lua_log (package, "error", traceback != NULL ? traceback : "Lua error");
    lua_pop (lua, 2);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_subscription_destroy (gpointer data)
{
    mc_lua_subscription_t *subscription = (mc_lua_subscription_t *) data;
    mc_lua_package_t *package;

    if (subscription == NULL)
        return;

    package = subscription->package;
    if (package != NULL && package->subscriptions != NULL && subscription->token != 0)
        (void) g_hash_table_remove (package->subscriptions, &subscription->token);

    if (package != NULL && package->lua != NULL && subscription->callback_ref != LUA_NOREF)
        luaL_unref (package->lua, LUA_REGISTRYINDEX, subscription->callback_ref);

    g_free (subscription->token_key);
    g_free (subscription);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_macro_destroy (gpointer data)
{
    mc_lua_macro_t *macro = (mc_lua_macro_t *) data;
    mc_lua_package_t *package;

    if (macro == NULL)
        return;

    package = macro->package;
    if (package != NULL && package->runtime != NULL && package->runtime->macros != NULL)
        (void) g_ptr_array_remove_fast (package->runtime->macros, macro);

    if (package != NULL && package->lua != NULL && macro->action_ref != LUA_NOREF)
        luaL_unref (package->lua, LUA_REGISTRYINDEX, macro->action_ref);

    g_free (macro->id);
    g_free (macro->area);
    g_free (macro->key);
    g_free (macro->display_key);
    g_free (macro->description);
    g_free (macro->menu_path);
    g_free (macro->menu_label);
    g_free (macro);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_unsubscribe_all (mc_lua_package_t *package)
{
    GList *values;
    GList *link;

    if (package == NULL || package->subscriptions == NULL || package->runtime == NULL
        || package->runtime->host == NULL)
        return;

    values = g_hash_table_get_values (package->subscriptions);
    for (link = values; link != NULL; link = g_list_next (link))
    {
        const mc_lua_subscription_t *subscription = (const mc_lua_subscription_t *) link->data;

        if (!package->runtime->host->unsubscribe (package->runtime->context, subscription->token))
            mc_lua_log (package, "warning", "could not remove a Lua event subscription");
    }
    g_list_free (values);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_close (mc_lua_package_t *package)
{
    if (package == NULL || package->closed)
        return;

    mc_lua_package_unsubscribe_all (package);

    if (package->runtime != NULL && package->runtime->host != NULL
        && package->runtime->host->struct_size >= MC_LUA_HOST_API_INDICATORS_SIZE
        && package->runtime->host->ui_indicators_clear_owner != NULL)
        package->runtime->host->ui_indicators_clear_owner (package->runtime->context, package->id);

    if (package->macros != NULL)
        g_ptr_array_set_size (package->macros, 0);
    if (package->panel_providers != NULL)
        g_ptr_array_set_size (package->panel_providers, 0);

    package->closed = TRUE;
    if (package->lua != NULL)
    {
        lua_close (package->lua);
        package->lua = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_destroy (mc_lua_package_t *package)
{
    if (package == NULL)
        return;

    mc_lua_package_close (package);
    if (package->subscriptions != NULL)
        g_hash_table_destroy (package->subscriptions);
    if (package->macros != NULL)
        g_ptr_array_free (package->macros, TRUE);
    if (package->panel_providers != NULL)
        g_ptr_array_free (package->panel_providers, TRUE);
    if (package->viewer_definitions != NULL)
        g_ptr_array_free (package->viewer_definitions, TRUE);
    g_free (package->id);
    g_free (package->workspace);
    g_free (package->root);
    g_free (package->entry_path);
    g_free (package);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_destroy (gpointer data)
{
    mc_lua_runtime_t *runtime = (mc_lua_runtime_t *) data;

    if (runtime == NULL)
        return;

    if (mc_lua_runtime_current == runtime)
        mc_lua_runtime_current = NULL;
    if (runtime->packages != NULL)
        g_ptr_array_free (runtime->packages, TRUE);
    if (runtime->catalog != NULL)
        g_ptr_array_free (runtime->catalog, TRUE);
    if (runtime->macros != NULL)
        g_ptr_array_free (runtime->macros, TRUE);
    if (runtime->panel_providers != NULL)
        g_hash_table_destroy (runtime->panel_providers);
    if (runtime->viewer_controllers != NULL)
        g_hash_table_destroy (runtime->viewer_controllers);
    if (runtime->disabled_package_ids != NULL)
        g_hash_table_destroy (runtime->disabled_package_ids);
    g_free (runtime->user_scripts_dir);
    g_free (runtime->user_modules_dir);
    g_free (runtime->legacy_user_scripts_dir);
    g_free (runtime->legacy_user_modules_dir);
    g_free (runtime);
}

/* --------------------------------------------------------------------------------------------- */

static char *
mc_lua_dup_table_string (lua_State *lua, int table, const char *field)
{
    char *value = NULL;

    lua_getfield (lua, table, field);
    if (lua_isstring (lua, -1))
        value = g_strdup (lua_tostring (lua, -1));
    lua_pop (lua, 1);
    return value;
}

static guint64
mc_lua_panel_table_uint64 (lua_State *lua, int table, const char *field, guint64 fallback)
{
    guint64 value = fallback;

    lua_getfield (lua, table, field);
    if (lua_isinteger (lua, -1) && lua_tointeger (lua, -1) >= 0)
        value = (guint64) lua_tointeger (lua, -1);
    lua_pop (lua, 1);
    return value;
}

static gboolean
mc_lua_table_boolean (lua_State *lua, int table, const char *field, gboolean fallback)
{
    gboolean value = fallback;

    lua_getfield (lua, table, field);
    if (lua_isboolean (lua, -1))
        value = lua_toboolean (lua, -1) != 0;
    lua_pop (lua, 1);
    return value;
}

static const char **
mc_lua_parse_string_array (lua_State *lua, int table, const char *field, guint *count)
{
    const char **values = NULL;
    guint i, length;

    *count = 0;
    lua_getfield (lua, table, field);
    if (!lua_istable (lua, -1))
    {
        lua_pop (lua, 1);
        return NULL;
    }
    length = (guint) lua_rawlen (lua, -1);
    values = g_new0 (const char *, length);
    for (i = 0; i < length; i++)
    {
        lua_rawgeti (lua, -1, (lua_Integer) i + 1);
        if (lua_isstring (lua, -1))
            values[i] = g_strdup (lua_tostring (lua, -1));
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);
    *count = length;
    return values;
}

static void
mc_lua_panel_response_free (mc_runtime_plugin_context_t *context,
                            mc_runtime_panel_provider_response_t *response)
{
    mc_runtime_panel_entry_t *entries;
    mc_runtime_panel_column_t *columns;
    guint i, j;

    (void) context;
    if (response == NULL)
        return;
    entries = (mc_runtime_panel_entry_t *) response->view.entries;
    for (i = 0; i < response->view.entries_count; i++)
    {
        mc_runtime_panel_column_value_t *values =
            (mc_runtime_panel_column_value_t *) entries[i].columns;
        g_free ((char *) entries[i].id);
        g_free ((char *) entries[i].name);
        g_free ((char *) entries[i].role);
        g_free ((char *) entries[i].link_target);
        g_free ((char *) entries[i].help_node);
        for (j = 0; j < entries[i].columns_count; j++)
        {
            g_free ((char *) values[j].id);
            g_free ((char *) values[j].value);
        }
        g_free (values);
        for (j = 0; j < entries[i].actions_count; j++)
            g_free ((char *) entries[i].actions[j]);
        g_free ((char **) entries[i].actions);
    }
    g_free (entries);
    columns = (mc_runtime_panel_column_t *) response->view.columns;
    for (i = 0; i < response->view.columns_count; i++)
    {
        g_free ((char *) columns[i].id);
        g_free ((char *) columns[i].title);
    }
    g_free (columns);
    for (i = 0; i < response->view.actions_count; i++)
        g_free ((char *) response->view.actions[i]);
    g_free ((char **) response->view.actions);
    g_free ((char *) response->view.location);
    g_free ((char *) response->view.title);
    g_free ((char *) response->view.footer);
    g_free ((char *) response->view.focus_id);
    g_free ((char *) response->view.help_node);
    g_free ((char *) response->view.default_format);
    g_free ((char *) response->view.default_sort_id);
    g_free ((char *) response->location);
    g_free ((char *) response->focus_id);
    g_free ((char *) response->status);
    g_free ((char *) response->local_path);
    if (response->read_source != NULL)
    {
        mc_lua_viewer_source_clear ((mc_runtime_viewer_source_t *) response->read_source);
        g_free ((mc_runtime_viewer_source_t *) response->read_source);
    }
    memset (response, 0, sizeof (*response));
}

static mc_runtime_panel_entry_kind_t
mc_lua_panel_entry_kind (const char *kind)
{
    if (g_strcmp0 (kind, "directory") == 0)
        return MC_RUNTIME_PANEL_ENTRY_DIRECTORY;
    if (g_strcmp0 (kind, "symlink") == 0)
        return MC_RUNTIME_PANEL_ENTRY_SYMLINK;
    if (g_strcmp0 (kind, "special") == 0)
        return MC_RUNTIME_PANEL_ENTRY_SPECIAL;
    return MC_RUNTIME_PANEL_ENTRY_FILE;
}

static void
mc_lua_parse_panel_columns (lua_State *lua, int table, mc_runtime_panel_view_t *view)
{
    mc_runtime_panel_column_t *columns;
    guint i, length;

    lua_getfield (lua, table, "columns");
    if (!lua_istable (lua, -1))
    {
        lua_pop (lua, 1);
        return;
    }
    length = MIN ((guint) lua_rawlen (lua, -1), 32U);
    columns = g_new0 (mc_runtime_panel_column_t, length);
    for (i = 0; i < length; i++)
    {
        char *align;

        lua_rawgeti (lua, -1, (lua_Integer) i + 1);
        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 1);
            continue;
        }
        columns[i].id = mc_lua_dup_table_string (lua, -1, "id");
        columns[i].title = mc_lua_dup_table_string (lua, -1, "title");
        columns[i].min_width = (guint) mc_lua_panel_table_uint64 (lua, -1, "min_width", 1);
        columns[i].expands = mc_lua_table_boolean (lua, -1, "expands", FALSE);
        columns[i].user_format = mc_lua_table_boolean (lua, -1, "user_format", TRUE);
        align = mc_lua_dup_table_string (lua, -1, "align");
        columns[i].align = g_strcmp0 (align, "right") == 0 ? MC_RUNTIME_PANEL_ALIGN_RIGHT
            : g_strcmp0 (align, "center") == 0             ? MC_RUNTIME_PANEL_ALIGN_CENTER
                                                           : MC_RUNTIME_PANEL_ALIGN_LEFT;
        g_free (align);
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);
    view->columns = columns;
    view->columns_count = length;
}

static void
mc_lua_parse_panel_column_values (lua_State *lua, int table, mc_runtime_panel_entry_t *entry)
{
    mc_runtime_panel_column_value_t *values;
    guint length = 0, index = 0;

    lua_getfield (lua, table, "columns");
    if (!lua_istable (lua, -1))
    {
        lua_pop (lua, 1);
        return;
    }
    lua_pushnil (lua);
    while (lua_next (lua, -2) != 0)
    {
        if (lua_isstring (lua, -2) && (lua_isstring (lua, -1) || lua_isnumber (lua, -1)))
            length++;
        lua_pop (lua, 1);
    }
    values = g_new0 (mc_runtime_panel_column_value_t, length);
    lua_pushnil (lua);
    while (index < length && lua_next (lua, -2) != 0)
    {
        if (lua_isstring (lua, -2) && (lua_isstring (lua, -1) || lua_isnumber (lua, -1)))
        {
            values[index].id = g_strdup (lua_tostring (lua, -2));
            values[index].value = g_strdup (lua_tostring (lua, -1));
            index++;
        }
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);
    entry->columns = values;
    entry->columns_count = index;
}

static gboolean
mc_lua_parse_panel_view (lua_State *lua, int table, mc_runtime_panel_view_t *view)
{
    guint i, length;
    mc_runtime_panel_entry_t *entries;

    memset (view, 0, sizeof (*view));
    view->revision = mc_lua_panel_table_uint64 (lua, table, "revision", 0);
    view->location = mc_lua_dup_table_string (lua, table, "location");
    view->title = mc_lua_dup_table_string (lua, table, "title");
    view->footer = mc_lua_dup_table_string (lua, table, "footer");
    view->focus_id = mc_lua_dup_table_string (lua, table, "focus_id");
    view->help_node = mc_lua_dup_table_string (lua, table, "help_node");
    view->default_format = mc_lua_dup_table_string (lua, table, "default_format");
    view->default_sort_id = mc_lua_dup_table_string (lua, table, "default_sort_id");
    view->default_sort_reverse = mc_lua_table_boolean (lua, table, "default_sort_reverse", FALSE);
    view->actions = mc_lua_parse_string_array (lua, table, "actions", &view->actions_count);
    mc_lua_parse_panel_columns (lua, table, view);

    lua_getfield (lua, table, "entries");
    if (!lua_istable (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    length = (guint) lua_rawlen (lua, -1);
    entries = g_new0 (mc_runtime_panel_entry_t, length);
    view->entries = entries;
    view->entries_count = length;
    for (i = 0; i < length; i++)
    {
        char *kind;

        lua_rawgeti (lua, -1, (lua_Integer) i + 1);
        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 2);
            return FALSE;
        }
        entries[i].id = mc_lua_dup_table_string (lua, -1, "id");
        entries[i].name = mc_lua_dup_table_string (lua, -1, "name");
        entries[i].role = mc_lua_dup_table_string (lua, -1, "role");
        kind = mc_lua_dup_table_string (lua, -1, "kind");
        entries[i].kind = mc_lua_panel_entry_kind (kind);
        g_free (kind);
        entries[i].mode = (guint) mc_lua_panel_table_uint64 (lua, -1, "mode", 0);
        entries[i].size = mc_lua_panel_table_uint64 (lua, -1, "size", 0);
        entries[i].mtime = (gint64) mc_lua_panel_table_uint64 (lua, -1, "mtime", 0);
        entries[i].atime = (gint64) mc_lua_panel_table_uint64 (lua, -1, "atime", 0);
        entries[i].ctime = (gint64) mc_lua_panel_table_uint64 (lua, -1, "ctime", 0);
        entries[i].uid = mc_lua_panel_table_uint64 (lua, -1, "uid", 0);
        entries[i].gid = mc_lua_panel_table_uint64 (lua, -1, "gid", 0);
        entries[i].link_target = mc_lua_dup_table_string (lua, -1, "link_target");
        entries[i].help_node = mc_lua_dup_table_string (lua, -1, "help_node");
        mc_lua_parse_panel_column_values (lua, -1, &entries[i]);
        entries[i].actions =
            mc_lua_parse_string_array (lua, -1, "actions", &entries[i].actions_count);
        lua_pop (lua, 1);
        if (entries[i].id == NULL || entries[i].name == NULL)
        {
            lua_pop (lua, 1);
            return FALSE;
        }
    }
    lua_pop (lua, 1);
    return view->revision != 0;
}

static int *
mc_lua_panel_instance_ref (mc_lua_panel_provider_t *provider, guint64 instance_id)
{
    return (int *) g_hash_table_lookup (provider->instances, &instance_id);
}

static gboolean
mc_lua_panel_call (mc_lua_panel_provider_t *provider, int callback_ref, int instance_ref,
                   const mc_runtime_panel_provider_request_t *request, int results)
{
    lua_State *lua = provider->package->lua;
    int arguments = 1;

    if (callback_ref == LUA_NOREF || lua == NULL)
        return FALSE;
    lua_rawgeti (lua, LUA_REGISTRYINDEX, callback_ref);
    if (instance_ref != LUA_NOREF)
        lua_rawgeti (lua, LUA_REGISTRYINDEX, instance_ref);
    else
    {
        lua_pushnil (lua); /* Reserved host object; v1 methods are added append-only. */
        lua_pushstring (lua, request != NULL && request->path != NULL ? request->path : "");
        arguments = 2;
    }
    if (instance_ref != LUA_NOREF && callback_ref == provider->navigate_ref)
    {
        lua_createtable (lua, 0, 4);
        if (request->entry_id != NULL)
        {
            lua_pushliteral (lua, "entry");
            lua_setfield (lua, -2, "kind");
            lua_pushstring (lua, request->entry_id);
            lua_setfield (lua, -2, "entry_id");
        }
        else if (request->path != NULL)
        {
            lua_pushliteral (lua, "history");
            lua_setfield (lua, -2, "kind");
            lua_pushstring (lua, request->path);
            lua_setfield (lua, -2, "location");
        }
        else
        {
            lua_pushliteral (lua, "parent");
            lua_setfield (lua, -2, "kind");
        }
        lua_pushinteger (lua, (lua_Integer) request->revision);
        lua_setfield (lua, -2, "revision");
        arguments = 2;
    }
    provider->package->callback_depth++;
    if (lua_pcall (lua, arguments, results, 0) != LUA_OK)
    {
        mc_lua_report_error (provider->package, MC_RUNTIME_ERROR_PHASE_EVENT,
                             "Lua panel provider callback failed");
        provider->package->callback_depth--;
        return FALSE;
    }
    provider->package->callback_depth--;
    return TRUE;
}

static gboolean
mc_lua_panel_provider_dispatch (mc_runtime_plugin_context_t *context, guint64 runtime_provider_id,
                                mc_runtime_panel_provider_operation_t operation,
                                const mc_runtime_panel_provider_request_t *request,
                                mc_runtime_panel_provider_response_t *response, const char **error)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    mc_lua_panel_provider_t *provider;
    int *instance_ref;
    lua_State *lua;

    (void) context;
    if (runtime == NULL || runtime->panel_providers == NULL)
        return FALSE;
    provider = g_hash_table_lookup (runtime->panel_providers, &runtime_provider_id);
    if (provider == NULL || provider->package->closed || provider->package->lua == NULL)
        return FALSE;
    lua = provider->package->lua;

    if (operation == MC_RUNTIME_PANEL_PROVIDER_OPEN)
    {
        guint64 *key;
        int *value;

        if (!mc_lua_panel_call (provider, provider->open_ref, LUA_NOREF, request, 2))
        {
            if (!lua_isnone (lua, -1))
                lua_pop (lua, 1);
            if (error != NULL)
                *error = "open_failed";
            return FALSE;
        }
        if (lua_isnil (lua, -2))
        {
            response->status = lua_isstring (lua, -1) ? g_strdup (lua_tostring (lua, -1))
                                                      : g_strdup ("Cannot open provider");
            lua_pop (lua, 2);
            return FALSE;
        }
        lua_pop (lua, 1); /* optional error result */
        key = g_new (guint64, 1);
        *key = ++provider->next_instance_id;
        value = g_new (int, 1);
        *value = luaL_ref (lua, LUA_REGISTRYINDEX);
        g_hash_table_insert (provider->instances, key, value);
        response->instance_id = *key;
        return TRUE;
    }

    instance_ref = mc_lua_panel_instance_ref (provider, request->instance_id);
    if (instance_ref == NULL)
    {
        if (error != NULL)
            *error = "stale_instance";
        return FALSE;
    }
    if (operation == MC_RUNTIME_PANEL_PROVIDER_CLOSE)
    {
        if (provider->close_ref != LUA_NOREF)
            (void) mc_lua_panel_call (provider, provider->close_ref, *instance_ref, request, 0);
        luaL_unref (lua, LUA_REGISTRYINDEX, *instance_ref);
        g_hash_table_remove (provider->instances, &request->instance_id);
        return TRUE;
    }
    if (operation == MC_RUNTIME_PANEL_PROVIDER_LIST)
    {
        gboolean valid;

        if (!mc_lua_panel_call (provider, provider->list_ref, *instance_ref, request, 1)
            || !lua_istable (lua, -1))
        {
            if (!lua_isnone (lua, -1))
                lua_pop (lua, 1);
            if (error != NULL)
                *error = "list_failed";
            return FALSE;
        }
        valid = mc_lua_parse_panel_view (lua, -1, &response->view);
        lua_pop (lua, 1);
        if (!valid)
        {
            mc_lua_panel_response_free (context, response);
            if (error != NULL)
                *error = "invalid_view";
        }
        return valid;
    }
    if (operation == MC_RUNTIME_PANEL_PROVIDER_INVOKE_ACTION
        && provider->invoke_action_ref != LUA_NOREF)
    {
        guint i;

        lua_rawgeti (lua, LUA_REGISTRYINDEX, provider->invoke_action_ref);
        lua_rawgeti (lua, LUA_REGISTRYINDEX, *instance_ref);
        lua_pushstring (lua, request->action_id != NULL ? request->action_id : "");
        lua_createtable (lua, (int) request->selected_count, 1);
        for (i = 0; i < request->selected_count; i++)
        {
            lua_pushstring (lua, request->selected_ids[i]);
            lua_rawseti (lua, -2, (lua_Integer) i + 1);
        }
        lua_pushinteger (lua, (lua_Integer) request->revision);
        lua_setfield (lua, -2, "revision");
        provider->package->callback_depth++;
        if (lua_pcall (lua, 3, 1, 0) != LUA_OK)
        {
            mc_lua_report_error (provider->package, MC_RUNTIME_ERROR_PHASE_EVENT,
                                 "Lua panel action callback failed");
            provider->package->callback_depth--;
            return FALSE;
        }
        provider->package->callback_depth--;
        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 1);
            return FALSE;
        }
        response->refresh = mc_lua_table_boolean (lua, -1, "refresh", FALSE);
        response->close = mc_lua_table_boolean (lua, -1, "close", FALSE);
        response->location = mc_lua_dup_table_string (lua, -1, "location");
        response->focus_id = mc_lua_dup_table_string (lua, -1, "focus");
        response->status = mc_lua_dup_table_string (lua, -1, "status");
        response->handled = TRUE;
        lua_pop (lua, 1);
        return TRUE;
    }
    if (operation == MC_RUNTIME_PANEL_PROVIDER_VIEW && provider->view_ref != LUA_NOREF)
    {
        lua_rawgeti (lua, LUA_REGISTRYINDEX, provider->view_ref);
        lua_rawgeti (lua, LUA_REGISTRYINDEX, *instance_ref);
        lua_pushstring (lua, request->entry_id != NULL ? request->entry_id : "");
        lua_createtable (lua, 0, 3);
        lua_pushboolean (lua, request->path != NULL && strcmp (request->path, "quick") == 0);
        lua_setfield (lua, -2, "quick");
        lua_pushboolean (lua, request->path != NULL && strcmp (request->path, "plain") == 0);
        lua_setfield (lua, -2, "plain");
        lua_pushstring (lua, request->path != NULL ? request->path : "view");
        lua_setfield (lua, -2, "mode");
        provider->package->callback_depth++;
        if (lua_pcall (lua, 3, 1, 0) != LUA_OK)
        {
            mc_lua_report_error (provider->package, MC_RUNTIME_ERROR_PHASE_EVENT,
                                 "Lua panel view callback failed");
            provider->package->callback_depth--;
            return FALSE;
        }
        provider->package->callback_depth--;
        response->handled = lua_toboolean (lua, -1) != 0;
        lua_pop (lua, 1);
        return TRUE;
    }
    if (operation == MC_RUNTIME_PANEL_PROVIDER_OPEN_READ && provider->open_read_ref != LUA_NOREF)
    {
        mc_runtime_viewer_source_t *source;

        lua_rawgeti (lua, LUA_REGISTRYINDEX, provider->open_read_ref);
        lua_rawgeti (lua, LUA_REGISTRYINDEX, *instance_ref);
        lua_pushstring (lua, request->entry_id != NULL ? request->entry_id : "");
        provider->package->callback_depth++;
        if (lua_pcall (lua, 2, 1, 0) != LUA_OK)
        {
            mc_lua_report_error (provider->package, MC_RUNTIME_ERROR_PHASE_EVENT,
                                 "Lua panel open_read callback failed");
            provider->package->callback_depth--;
            return FALSE;
        }
        provider->package->callback_depth--;
        source = g_new0 (mc_runtime_viewer_source_t, 1);
        if (!lua_istable (lua, -1) || !mc_lua_parse_viewer_source (lua, -1, source))
        {
            lua_pop (lua, 1);
            mc_lua_viewer_source_clear (source);
            g_free (source);
            return FALSE;
        }
        lua_pop (lua, 1);
        response->read_source = source;
        response->handled = TRUE;
        return TRUE;
    }
    if (operation == MC_RUNTIME_PANEL_PROVIDER_NEW_CONNECTION
        && provider->new_connection_ref != LUA_NOREF)
    {
        lua_rawgeti (lua, LUA_REGISTRYINDEX, provider->new_connection_ref);
        lua_pushnil (lua); /* Reserved host object. */
        provider->package->callback_depth++;
        if (lua_pcall (lua, 1, 1, 0) != LUA_OK)
        {
            mc_lua_report_error (provider->package, MC_RUNTIME_ERROR_PHASE_EVENT,
                                 "Lua panel new connection callback failed");
            provider->package->callback_depth--;
            return FALSE;
        }
        provider->package->callback_depth--;
        if (lua_isnil (lua, -1))
        {
            lua_pop (lua, 1);
            return FALSE;
        }
        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 1);
            return FALSE;
        }
        mc_lua_cache_connection (provider, -1);
        response->refresh = TRUE;
        response->focus_id = mc_lua_dup_table_string (lua, -1, "title");
        response->status = g_strdup ("Connection created");
        response->handled = TRUE;
        lua_pop (lua, 1);
        return TRUE;
    }
    if (operation == MC_RUNTIME_PANEL_PROVIDER_EDIT_CONNECTION
        || operation == MC_RUNTIME_PANEL_PROVIDER_COPY_CONNECTION
        || operation == MC_RUNTIME_PANEL_PROVIDER_RENAME_CONNECTION
        || operation == MC_RUNTIME_PANEL_PROVIDER_DELETE_CONNECTION)
    {
        int callback_ref = operation == MC_RUNTIME_PANEL_PROVIDER_EDIT_CONNECTION
            ? provider->edit_connection_ref
            : operation == MC_RUNTIME_PANEL_PROVIDER_COPY_CONNECTION ? provider->copy_connection_ref
            : operation == MC_RUNTIME_PANEL_PROVIDER_RENAME_CONNECTION
            ? provider->rename_connection_ref
            : provider->delete_connection_ref;

        if (callback_ref == LUA_NOREF)
            return FALSE;
        lua_rawgeti (lua, LUA_REGISTRYINDEX, callback_ref);
        lua_pushnil (lua); /* Reserved host object. */
        lua_rawgeti (lua, LUA_REGISTRYINDEX, provider->connections_ref);
        lua_getfield (lua, -1, request->connection_id != NULL ? request->connection_id : "");
        lua_remove (lua, -2);
        if (operation == MC_RUNTIME_PANEL_PROVIDER_DELETE_CONNECTION)
        {
            gboolean deleted = lua_toboolean (lua, -1) != 0;

            lua_pop (lua, 1);
            if (!deleted)
                return FALSE;
            lua_rawgeti (lua, LUA_REGISTRYINDEX, provider->connections_ref);
            lua_pushstring (lua, request->connection_id);
            lua_pushnil (lua);
            lua_settable (lua, -3);
            lua_pop (lua, 1);
            response->refresh = TRUE;
            response->status = g_strdup ("Connection deleted");
            response->handled = TRUE;
            return TRUE;
        }
        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 3);
            return FALSE;
        }
        provider->package->callback_depth++;
        if (lua_pcall (lua, 2, 1, 0) != LUA_OK)
        {
            mc_lua_report_error (provider->package, MC_RUNTIME_ERROR_PHASE_EVENT,
                                 "Lua panel edit connection callback failed");
            provider->package->callback_depth--;
            return FALSE;
        }
        provider->package->callback_depth--;
        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 1);
            return FALSE;
        }
        if (operation != MC_RUNTIME_PANEL_PROVIDER_COPY_CONNECTION)
        {
            lua_rawgeti (lua, LUA_REGISTRYINDEX, provider->connections_ref);
            lua_pushstring (lua, request->connection_id);
            lua_pushnil (lua);
            lua_settable (lua, -3);
            lua_pop (lua, 1);
        }
        mc_lua_cache_connection (provider, -1);
        response->refresh = TRUE;
        response->focus_id = mc_lua_dup_table_string (lua, -1, "title");
        response->status = g_strdup (
            operation == MC_RUNTIME_PANEL_PROVIDER_COPY_CONNECTION         ? "Connection copied"
                : operation == MC_RUNTIME_PANEL_PROVIDER_RENAME_CONNECTION ? "Connection renamed"
                                                                           : "Connection updated");
        response->handled = TRUE;
        lua_pop (lua, 1);
        return TRUE;
    }
    if (operation == MC_RUNTIME_PANEL_PROVIDER_RELOAD && provider->reload_ref != LUA_NOREF)
    {
        if (!mc_lua_panel_call (provider, provider->reload_ref, *instance_ref, request, 1))
            return FALSE;
    }
    else if ((operation == MC_RUNTIME_PANEL_PROVIDER_NAVIGATE_ENTRY
              || operation == MC_RUNTIME_PANEL_PROVIDER_NAVIGATE_PARENT
              || operation == MC_RUNTIME_PANEL_PROVIDER_NAVIGATE_HISTORY)
             && provider->navigate_ref != LUA_NOREF)
    {
        if (!mc_lua_panel_call (provider, provider->navigate_ref, *instance_ref, request, 1))
            return FALSE;
    }
    else
        return FALSE;
    if (!lua_istable (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    response->refresh = mc_lua_table_boolean (lua, -1, "refresh", FALSE);
    response->close = mc_lua_table_boolean (lua, -1, "close", FALSE);
    response->location = mc_lua_dup_table_string (lua, -1, "location");
    response->focus_id = mc_lua_dup_table_string (lua, -1, "focus");
    response->status = mc_lua_dup_table_string (lua, -1, "status");
    lua_pop (lua, 1);
    response->handled = TRUE;
    return response->handled;
}

static int
mc_lua_panel_callback_ref (lua_State *lua, int table, const char *field, gboolean required)
{
    int ref = LUA_NOREF;

    lua_getfield (lua, table, field);
    if (lua_isfunction (lua, -1))
        ref = luaL_ref (lua, LUA_REGISTRYINDEX);
    else
    {
        lua_pop (lua, 1);
        if (required)
            luaL_error (lua, "panel provider field '%s' must be a function", field);
    }
    return ref;
}

static void
mc_lua_panel_action_clear (mc_runtime_panel_action_t *action)
{
    g_free ((char *) action->id);
    g_free ((char *) action->title);
    g_free ((char *) action->key);
    g_free ((char *) action->menu_path);
    g_free ((char *) action->menu_label);
    g_free ((char *) action->help_node);
    g_free ((char *) action->open_path);
}

static gboolean
mc_lua_push_connection_copy (lua_State *lua, int source)
{
    static const char *const string_fields[] = { "id", "title", "location", "description" };
    guint i;

    source = lua_absindex (lua, source);
    lua_getfield (lua, source, "id");
    lua_getfield (lua, source, "title");
    lua_getfield (lua, source, "location");
    if (!lua_isstring (lua, -3) || lua_rawlen (lua, -3) == 0 || !lua_isstring (lua, -2)
        || lua_rawlen (lua, -2) == 0 || !lua_isstring (lua, -1))
    {
        lua_pop (lua, 3);
        return FALSE;
    }
    lua_pop (lua, 3);
    lua_createtable (lua, 0, 5);
    for (i = 0; i < G_N_ELEMENTS (string_fields); i++)
    {
        lua_getfield (lua, source, string_fields[i]);
        if (!lua_isnil (lua, -1))
            lua_setfield (lua, -2, string_fields[i]);
        else
            lua_pop (lua, 1);
    }
    lua_getfield (lua, source, "favorite");
    if (!lua_isnil (lua, -1))
        lua_setfield (lua, -2, "favorite");
    else
        lua_pop (lua, 1);
    return TRUE;
}

static void
mc_lua_cache_connection (mc_lua_panel_provider_t *provider, int source)
{
    lua_State *lua = provider->package->lua;
    const char *id;

    source = lua_absindex (lua, source);
    lua_getfield (lua, source, "id");
    id = lua_tostring (lua, -1);
    if (id != NULL && id[0] != '\0')
    {
        lua_rawgeti (lua, LUA_REGISTRYINDEX, provider->connections_ref);
        lua_pushstring (lua, id);
        if (mc_lua_push_connection_copy (lua, source))
            lua_settable (lua, -3);
        else
            lua_pop (lua, 1);
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);
}

static gboolean
mc_lua_panel_parse_actions (lua_State *lua, int spec, mc_lua_panel_provider_t *provider)
{
    GArray *actions = g_array_new (FALSE, TRUE, sizeof (mc_runtime_panel_action_t));
    guint i, count;

    lua_getfield (lua, spec, "actions");
    count = lua_istable (lua, -1) ? (guint) lua_rawlen (lua, -1) : 0;
    for (i = 0; i < count; i++)
    {
        mc_runtime_panel_action_t action = { 0 };
        char *targets;

        lua_rawgeti (lua, -1, (lua_Integer) i + 1);
        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 1);
            continue;
        }
        action.id = mc_lua_dup_table_string (lua, -1, "id");
        action.title = mc_lua_dup_table_string (lua, -1, "title");
        action.key = mc_lua_dup_table_string (lua, -1, "key");
        action.help_node = mc_lua_dup_table_string (lua, -1, "help_node");
        targets = mc_lua_dup_table_string (lua, -1, "targets");
        action.targets = g_strcmp0 (targets, "selection") == 0 ? MC_RUNTIME_PANEL_ACTION_SELECTION
            : g_strcmp0 (targets, "view") == 0                 ? MC_RUNTIME_PANEL_ACTION_VIEW
                                                               : MC_RUNTIME_PANEL_ACTION_CURRENT;
        g_free (targets);
        lua_getfield (lua, -1, "menu");
        if (lua_istable (lua, -1))
        {
            action.menu_path = mc_lua_dup_table_string (lua, -1, "path");
            action.menu_label = mc_lua_dup_table_string (lua, -1, "label");
            action.menu_position = (gint) mc_lua_panel_table_uint64 (lua, -1, "position", 0);
        }
        lua_pop (lua, 1);
        if (action.id != NULL && action.title != NULL)
            g_array_append_val (actions, action);
        else
            mc_lua_panel_action_clear (&action);
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);

    /* Connections are materialized once at registration and become ordinary
     * open actions.  Their location remains typed provider data. */
    lua_getfield (lua, spec, "connections");
    if (lua_isfunction (lua, -1))
    {
        if (lua_pcall (lua, 0, 1, 0) != LUA_OK)
        {
            mc_lua_report_error (provider->package, MC_RUNTIME_ERROR_PHASE_STARTUP,
                                 "Lua panel connections callback failed");
            g_array_free (actions, TRUE);
            return FALSE;
        }
        if (lua_istable (lua, -1))
        {
            count = (guint) lua_rawlen (lua, -1);
            for (i = 0; i < count; i++)
            {
                mc_runtime_panel_action_t action = { 0 };
                char *id, *location;

                lua_rawgeti (lua, -1, (lua_Integer) i + 1);
                if (!lua_istable (lua, -1))
                {
                    lua_pop (lua, 1);
                    continue;
                }
                id = mc_lua_dup_table_string (lua, -1, "id");
                location = mc_lua_dup_table_string (lua, -1, "location");
                action.title = mc_lua_dup_table_string (lua, -1, "title");
                if (id != NULL && location != NULL && action.title != NULL)
                {
                    mc_lua_cache_connection (provider, -1);
                    action.id = g_strdup_printf ("connection.%s", id);
                    action.open_path = g_strconcat (provider->prefix, location, NULL);
                    action.targets = MC_RUNTIME_PANEL_ACTION_VIEW;
                    g_array_append_val (actions, action);
                }
                else
                    mc_lua_panel_action_clear (&action);
                g_free (id);
                g_free (location);
                lua_pop (lua, 1);
            }
        }
        lua_pop (lua, 1);
    }
    else
        lua_pop (lua, 1);

    provider->actions_count = actions->len;
    provider->actions = (mc_runtime_panel_action_t *) g_array_free (actions, FALSE);
    return TRUE;
}

static int
mc_lua_panel_provider_register (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_lua_panel_provider_t *provider;
    mc_runtime_panel_provider_t descriptor = { 0 };
    mc_runtime_panel_help_t help = { 0 };
    guint64 *key;
    const char *error = NULL;

    luaL_checktype (lua, 1, LUA_TTABLE);
    if (package == NULL || package->runtime->stopping || g_strcmp0 (package->workspace, "mc") != 0)
        return luaL_error (lua, "panel providers are available only to mc packages");
    if (package->runtime->host->struct_size < MC_LUA_HOST_API_PANEL_PROVIDER_SIZE
        || package->runtime->host->panel_provider_register == NULL)
        return luaL_error (lua, "host does not support panel providers");

    provider = g_new0 (mc_lua_panel_provider_t, 1);
    provider->package = package;
    provider->open_ref = LUA_NOREF;
    provider->close_ref = LUA_NOREF;
    provider->list_ref = LUA_NOREF;
    provider->navigate_ref = LUA_NOREF;
    provider->enter_ref = LUA_NOREF;
    provider->reload_ref = LUA_NOREF;
    provider->invoke_action_ref = LUA_NOREF;
    provider->view_ref = LUA_NOREF;
    provider->open_read_ref = LUA_NOREF;
    provider->new_connection_ref = LUA_NOREF;
    provider->edit_connection_ref = LUA_NOREF;
    provider->copy_connection_ref = LUA_NOREF;
    provider->rename_connection_ref = LUA_NOREF;
    provider->delete_connection_ref = LUA_NOREF;
    lua_newtable (lua);
    provider->connections_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
    provider->id = mc_lua_dup_table_string (lua, 1, "id");
    provider->title = mc_lua_dup_table_string (lua, 1, "title");
    provider->prefix = mc_lua_dup_table_string (lua, 1, "prefix");
    lua_getfield (lua, 1, "help");
    if (lua_istable (lua, -1))
    {
        provider->help_file = mc_lua_dup_table_string (lua, -1, "file");
        provider->help_node = mc_lua_dup_table_string (lua, -1, "node");
    }
    lua_pop (lua, 1);
    if (provider->id == NULL || provider->title == NULL || provider->prefix == NULL)
    {
        mc_lua_panel_provider_destroy (provider);
        return luaL_error (lua, "panel provider requires id, title and prefix");
    }
    lua_getfield (lua, 1, "open");
    lua_getfield (lua, 1, "list");
    if (!lua_isfunction (lua, -2) || !lua_isfunction (lua, -1))
    {
        lua_pop (lua, 2);
        mc_lua_panel_provider_destroy (provider);
        return luaL_error (lua, "panel provider requires open and list functions");
    }
    lua_pop (lua, 2);
    provider->open_ref = mc_lua_panel_callback_ref (lua, 1, "open", FALSE);
    provider->close_ref = mc_lua_panel_callback_ref (lua, 1, "close", FALSE);
    provider->list_ref = mc_lua_panel_callback_ref (lua, 1, "list", FALSE);
    provider->navigate_ref = mc_lua_panel_callback_ref (lua, 1, "navigate", FALSE);
    provider->enter_ref = mc_lua_panel_callback_ref (lua, 1, "enter", FALSE);
    provider->reload_ref = mc_lua_panel_callback_ref (lua, 1, "reload", FALSE);
    provider->invoke_action_ref = mc_lua_panel_callback_ref (lua, 1, "invoke_action", FALSE);
    provider->view_ref = mc_lua_panel_callback_ref (lua, 1, "view", FALSE);
    provider->open_read_ref = mc_lua_panel_callback_ref (lua, 1, "open_read", FALSE);
    provider->new_connection_ref = mc_lua_panel_callback_ref (lua, 1, "new_connection", FALSE);
    provider->edit_connection_ref = mc_lua_panel_callback_ref (lua, 1, "edit_connection", FALSE);
    provider->copy_connection_ref = mc_lua_panel_callback_ref (lua, 1, "copy_connection", FALSE);
    provider->rename_connection_ref =
        mc_lua_panel_callback_ref (lua, 1, "rename_connection", FALSE);
    provider->delete_connection_ref =
        mc_lua_panel_callback_ref (lua, 1, "delete_connection", FALSE);
    if (!mc_lua_panel_parse_actions (lua, 1, provider))
    {
        mc_lua_panel_provider_destroy (provider);
        return luaL_error (lua, "invalid panel connections");
    }
    provider->instances = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, g_free);
    provider->runtime_id = ++package->runtime->next_panel_provider_id;
    descriptor.struct_size = sizeof (descriptor);
    descriptor.api_version = 1;
    descriptor.runtime_provider_id = provider->runtime_id;
    descriptor.id = provider->id;
    descriptor.title = provider->title;
    descriptor.prefix = provider->prefix;
    if (provider->help_file != NULL || provider->help_node != NULL)
    {
        help.file = provider->help_file;
        help.node = provider->help_node;
        descriptor.help = &help;
    }
    descriptor.dispatch = mc_lua_panel_provider_dispatch;
    descriptor.response_free = mc_lua_panel_response_free;
    descriptor.supports_new_connection = provider->new_connection_ref != LUA_NOREF;
    descriptor.supports_edit_connection = provider->edit_connection_ref != LUA_NOREF;
    descriptor.supports_copy_connection = provider->copy_connection_ref != LUA_NOREF;
    descriptor.supports_rename_connection = provider->rename_connection_ref != LUA_NOREF;
    descriptor.supports_delete_connection = provider->delete_connection_ref != LUA_NOREF;
    descriptor.supports_open_read = provider->open_read_ref != LUA_NOREF;
    descriptor.actions = provider->actions;
    descriptor.actions_count = provider->actions_count;
    if (!package->runtime->host->panel_provider_register (package->runtime->context, &descriptor,
                                                          &provider->registration, &error))
    {
        mc_lua_panel_provider_destroy (provider);
        lua_pushnil (lua);
        lua_pushstring (lua, error != NULL ? error : "registration_failed");
        return 2;
    }
    key = g_new (guint64, 1);
    *key = provider->runtime_id;
    g_hash_table_insert (package->runtime->panel_providers, key, provider);
    g_ptr_array_add (package->panel_providers, provider);
    lua_pushboolean (lua, TRUE);
    return 1;
}

static void
mc_lua_panel_provider_destroy (gpointer data)
{
    mc_lua_panel_provider_t *provider = data;
    lua_State *lua;
    GHashTableIter iter;
    gpointer value;

    if (provider == NULL)
        return;
    lua = provider->package != NULL ? provider->package->lua : NULL;
    if (provider->package != NULL && provider->package->runtime != NULL)
    {
        mc_lua_runtime_t *runtime = provider->package->runtime;
        const char *error = NULL;

        if (provider->registration.id != 0 && runtime->host != NULL
            && runtime->host->panel_provider_unregister != NULL)
            (void) runtime->host->panel_provider_unregister (runtime->context,
                                                             &provider->registration, &error);
        if (runtime->panel_providers != NULL)
            g_hash_table_remove (runtime->panel_providers, &provider->runtime_id);
    }
    if (lua != NULL && provider->instances != NULL)
    {
        g_hash_table_iter_init (&iter, provider->instances);
        while (g_hash_table_iter_next (&iter, NULL, &value))
            luaL_unref (lua, LUA_REGISTRYINDEX, *(int *) value);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->open_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->close_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->list_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->navigate_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->enter_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->reload_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->invoke_action_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->view_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->open_read_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->new_connection_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->edit_connection_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->copy_connection_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->rename_connection_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->delete_connection_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, provider->connections_ref);
    }
    if (provider->instances != NULL)
        g_hash_table_destroy (provider->instances);
    g_free (provider->id);
    g_free (provider->title);
    g_free (provider->prefix);
    for (guint i = 0; i < provider->actions_count; i++)
        mc_lua_panel_action_clear (&provider->actions[i]);
    g_free (provider->actions);
    g_free (provider->help_file);
    g_free (provider->help_node);
    g_free (provider);
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_source_tag (lua_State *lua, const char *tag)
{
    luaL_checktype (lua, 1, LUA_TTABLE);
    lua_pushstring (lua, tag);
    lua_setfield (lua, 1, "_mc_source");
    lua_settop (lua, 1);
    return 1;
}

static int
mc_lua_source_bytes (lua_State *lua)
{
    size_t length;
    const char *bytes = luaL_checklstring (lua, 1, &length);

    lua_createtable (lua, 0, 2);
    lua_pushliteral (lua, "bytes");
    lua_setfield (lua, -2, "_mc_source");
    lua_pushlstring (lua, bytes, length);
    lua_setfield (lua, -2, "data");
    return 1;
}

static int
mc_lua_source_file (lua_State *lua)
{
    return mc_lua_source_tag (lua, "file");
}
static int
mc_lua_source_process (lua_State *lua)
{
    return mc_lua_source_tag (lua, "process");
}
static int
mc_lua_source_pipeline (lua_State *lua)
{
    return mc_lua_source_tag (lua, "pipeline");
}

static void
mc_lua_viewer_source_clear (mc_runtime_viewer_source_t *source)
{
    guint i;

    if (source == NULL)
        return;
    g_free ((char *) source->bytes);
    g_free ((char *) source->path);
    for (i = 0; i < source->process.argc; i++)
        g_free ((char *) source->process.argv[i]);
    g_free ((char **) source->process.argv);
    g_free ((char *) source->process.cwd);
    for (i = 0; i < source->stages_count; i++)
        mc_lua_viewer_source_clear ((mc_runtime_viewer_source_t *) &source->stages[i]);
    g_free ((mc_runtime_viewer_source_t *) source->stages);
    memset (source, 0, sizeof (*source));
}

static gboolean
mc_lua_parse_viewer_process (lua_State *lua, int table, mc_runtime_viewer_process_t *process)
{
    const char **argv;
    guint i, count;

    lua_getfield (lua, table, "argv");
    if (!lua_istable (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    count = (guint) lua_rawlen (lua, -1);
    if (count == 0 || count > 256)
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    argv = g_new0 (const char *, count);
    for (i = 0; i < count; i++)
    {
        lua_rawgeti (lua, -1, (lua_Integer) i + 1);
        if (!lua_isstring (lua, -1))
        {
            lua_pop (lua, 2);
            process->argv = argv;
            process->argc = i;
            return FALSE;
        }
        argv[i] = g_strdup (lua_tostring (lua, -1));
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);
    process->argv = argv;
    process->argc = count;
    process->cwd = mc_lua_dup_table_string (lua, table, "cwd");
    return TRUE;
}

static gboolean
mc_lua_parse_viewer_source (lua_State *lua, int table, mc_runtime_viewer_source_t *source)
{
    char *kind;

    memset (source, 0, sizeof (*source));
    source->struct_size = sizeof (*source);
    kind = mc_lua_dup_table_string (lua, table, "_mc_source");
    if (g_strcmp0 (kind, "bytes") == 0)
    {
        size_t length;

        source->kind = MC_RUNTIME_VIEWER_SOURCE_BYTES;
        lua_getfield (lua, table, "data");
        if (!lua_isstring (lua, -1))
        {
            lua_pop (lua, 1);
            g_free (kind);
            return FALSE;
        }
        source->bytes = g_memdup2 (lua_tolstring (lua, -1, &length), length);
        source->bytes_length = length;
        lua_pop (lua, 1);
    }
    else if (g_strcmp0 (kind, "file") == 0)
    {
        source->kind = MC_RUNTIME_VIEWER_SOURCE_FILE;
        source->path = mc_lua_dup_table_string (lua, table, "path");
        source->unlink_on_close = mc_lua_table_boolean (lua, table, "unlink_on_close", FALSE);
        if (source->path == NULL)
        {
            g_free (kind);
            return FALSE;
        }
    }
    else if (g_strcmp0 (kind, "process") == 0)
    {
        source->kind = MC_RUNTIME_VIEWER_SOURCE_PROCESS;
        if (!mc_lua_parse_viewer_process (lua, table, &source->process))
        {
            g_free (kind);
            return FALSE;
        }
    }
    else if (g_strcmp0 (kind, "pipeline") == 0)
    {
        mc_runtime_viewer_source_t *stages;
        guint i, count = (guint) lua_rawlen (lua, table);

        if (count < 2 || count > 16)
        {
            g_free (kind);
            return FALSE;
        }
        source->kind = MC_RUNTIME_VIEWER_SOURCE_PIPELINE;
        stages = g_new0 (mc_runtime_viewer_source_t, count);
        source->stages = stages;
        source->stages_count = count;
        for (i = 0; i < count; i++)
        {
            lua_rawgeti (lua, table, (lua_Integer) i + 1);
            if (!lua_istable (lua, -1) || !mc_lua_parse_viewer_source (lua, -1, &stages[i])
                || stages[i].kind != MC_RUNTIME_VIEWER_SOURCE_PROCESS)
            {
                lua_pop (lua, 1);
                g_free (kind);
                return FALSE;
            }
            lua_pop (lua, 1);
        }
    }
    else
    {
        g_free (kind);
        return FALSE;
    }
    g_free (kind);
    return TRUE;
}

static gboolean
mc_lua_parse_viewer_spec (lua_State *lua, int table, mc_runtime_viewer_spec_t *spec)
{
    mc_runtime_viewer_source_t *source;
    char *scroll;

    memset (spec, 0, sizeof (*spec));
    spec->struct_size = sizeof (*spec);
    lua_getfield (lua, table, "source");
    if (!lua_istable (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    source = g_new0 (mc_runtime_viewer_source_t, 1);
    if (!mc_lua_parse_viewer_source (lua, -1, source))
    {
        lua_pop (lua, 1);
        mc_lua_viewer_source_clear (source);
        g_free (source);
        return FALSE;
    }
    lua_pop (lua, 1);
    spec->source = source;
    spec->title = mc_lua_dup_table_string (lua, table, "title");
    spec->help_node = mc_lua_dup_table_string (lua, table, "help_node");
    scroll = mc_lua_dup_table_string (lua, table, "auto_scroll");
    spec->auto_scroll_bottom = g_strcmp0 (scroll, "bottom") == 0;
    g_free (scroll);
    return TRUE;
}

static void
mc_lua_viewer_spec_free (mc_runtime_plugin_context_t *context, mc_runtime_viewer_spec_t *spec)
{
    (void) context;
    if (spec == NULL)
        return;
    if (spec->source != NULL)
    {
        mc_lua_viewer_source_clear ((mc_runtime_viewer_source_t *) spec->source);
        g_free ((mc_runtime_viewer_source_t *) spec->source);
    }
    g_free ((char *) spec->title);
    g_free ((char *) spec->help_node);
    memset (spec, 0, sizeof (*spec));
}

static mc_lua_viewer_controller_t *
mc_lua_viewer_controller_check (lua_State *lua, int index)
{
    return (mc_lua_viewer_controller_t *) luaL_checkudata (lua, index, MC_LUA_VIEWER_CONTROLLER_MT);
}

static gboolean
mc_lua_viewer_call_prepare (mc_lua_viewer_controller_t *controller, int params_ref,
                            mc_runtime_viewer_spec_t *spec)
{
    mc_lua_package_t *package = controller->definition->package;
    lua_State *lua = package->lua;
    gboolean valid;

    lua_rawgeti (lua, LUA_REGISTRYINDEX, controller->definition->prepare_ref);
    lua_rawgeti (lua, LUA_REGISTRYINDEX, controller->session_ref);
    lua_rawgeti (lua, LUA_REGISTRYINDEX, params_ref);
    package->callback_depth++;
    if (lua_pcall (lua, 2, 1, 0) != LUA_OK)
    {
        mc_lua_report_error (package, MC_RUNTIME_ERROR_PHASE_EVENT,
                             "Lua viewer prepare callback failed");
        package->callback_depth--;
        return FALSE;
    }
    package->callback_depth--;
    valid = lua_istable (lua, -1) && mc_lua_parse_viewer_spec (lua, -1, spec);
    lua_pop (lua, 1);
    return valid;
}

static void
mc_lua_viewer_controller_close (mc_lua_viewer_controller_t *controller)
{
    mc_lua_package_t *package;
    lua_State *lua;

    if (controller == NULL || controller->closed)
        return;
    controller->closed = TRUE;
    package = controller->definition->package;
    lua = package->lua;
    if (lua != NULL && controller->definition->close_ref != LUA_NOREF)
    {
        lua_rawgeti (lua, LUA_REGISTRYINDEX, controller->definition->close_ref);
        lua_rawgeti (lua, LUA_REGISTRYINDEX, controller->session_ref);
        package->callback_depth++;
        if (lua_pcall (lua, 1, 0, 0) != LUA_OK)
            mc_lua_report_error (package, MC_RUNTIME_ERROR_PHASE_EVENT,
                                 "Lua viewer close callback failed");
        package->callback_depth--;
    }
    if (package->runtime->viewer_controllers != NULL)
        g_hash_table_remove (package->runtime->viewer_controllers, &controller->runtime_id);
    if (lua != NULL)
    {
        luaL_unref (lua, LUA_REGISTRYINDEX, controller->session_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, controller->live_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, controller->pending_ref);
        controller->session_ref = controller->live_ref = controller->pending_ref = LUA_NOREF;
    }
}

static gboolean
mc_lua_viewer_dispatch (mc_runtime_plugin_context_t *context, guint64 controller_id,
                        mc_runtime_viewer_controller_operation_t operation, int key,
                        mc_runtime_viewer_spec_t *spec, gboolean *handled, const char **error)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    mc_lua_viewer_controller_t *controller;
    mc_lua_package_t *package;
    lua_State *lua;

    (void) context;
    (void) key;
    if (runtime == NULL || runtime->viewer_controllers == NULL)
        return FALSE;
    controller = g_hash_table_lookup (runtime->viewer_controllers, &controller_id);
    if (controller == NULL || controller->closed)
        return FALSE;
    package = controller->definition->package;
    lua = package->lua;
    *handled = FALSE;
    switch (operation)
    {
    case MC_RUNTIME_VIEWER_CONTROLLER_OPTIONS:
        if (controller->definition->options_ref == LUA_NOREF)
            return TRUE;
        lua_rawgeti (lua, LUA_REGISTRYINDEX, controller->definition->options_ref);
        lua_rawgeti (lua, LUA_REGISTRYINDEX, controller->session_ref);
        lua_rawgeti (lua, LUA_REGISTRYINDEX, controller->live_ref);
        package->callback_depth++;
        if (lua_pcall (lua, 2, 1, 0) != LUA_OK)
        {
            mc_lua_report_error (package, MC_RUNTIME_ERROR_PHASE_EVENT,
                                 "Lua viewer options callback failed");
            package->callback_depth--;
            return FALSE;
        }
        package->callback_depth--;
        if (lua_isnil (lua, -1))
        {
            lua_pop (lua, 1);
            return TRUE;
        }
        luaL_unref (lua, LUA_REGISTRYINDEX, controller->pending_ref);
        controller->pending_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
        *handled = TRUE;
        return TRUE;
    case MC_RUNTIME_VIEWER_CONTROLLER_PREPARE:
        if (controller->pending_ref == LUA_NOREF)
            return FALSE;
        return mc_lua_viewer_call_prepare (controller, controller->pending_ref, spec);
    case MC_RUNTIME_VIEWER_CONTROLLER_COMMIT:
        luaL_unref (lua, LUA_REGISTRYINDEX, controller->live_ref);
        controller->live_ref = controller->pending_ref;
        controller->pending_ref = LUA_NOREF;
        return TRUE;
    case MC_RUNTIME_VIEWER_CONTROLLER_ROLLBACK:
        luaL_unref (lua, LUA_REGISTRYINDEX, controller->pending_ref);
        controller->pending_ref = LUA_NOREF;
        return TRUE;
    case MC_RUNTIME_VIEWER_CONTROLLER_CLOSE:
        mc_lua_viewer_controller_close (controller);
        return TRUE;
    case MC_RUNTIME_VIEWER_CONTROLLER_KEY:
        return TRUE;
    default:
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
}

static int
mc_lua_viewer_controller_gc (lua_State *lua)
{
    mc_lua_viewer_controller_t *controller = mc_lua_viewer_controller_check (lua, 1);

    mc_lua_viewer_controller_close (controller);
    if (controller->definition_ref != LUA_NOREF)
        luaL_unref (lua, LUA_REGISTRYINDEX, controller->definition_ref);
    controller->definition_ref = LUA_NOREF;
    return 0;
}

static int
mc_lua_viewer_definition_create (lua_State *lua)
{
    mc_lua_viewer_definition_t **holder = luaL_checkudata (lua, 1, MC_LUA_VIEWER_DEFINITION_MT);
    mc_lua_viewer_definition_t *definition = *holder;
    mc_lua_package_t *package = definition->package;
    mc_lua_viewer_controller_t *controller;
    guint64 *key;
    int initial_ref;
    int arguments = lua_gettop (lua);

    lua_rawgeti (lua, LUA_REGISTRYINDEX, definition->open_ref);
    lua_pushvalue (lua, 2);
    if (lua_pcall (lua, 1, 1, 0) != LUA_OK)
        return lua_error (lua);
    if (lua_isnil (lua, -1))
        return luaL_error (lua, "viewer controller open returned nil");

    initial_ref = luaL_ref (lua, LUA_REGISTRYINDEX); /* temporary session reference */
    controller = lua_newuserdata (lua, sizeof (*controller));
    memset (controller, 0, sizeof (*controller));
    controller->definition = definition;
    controller->definition_ref = LUA_NOREF;
    controller->session_ref = initial_ref;
    controller->live_ref = LUA_NOREF;
    controller->pending_ref = LUA_NOREF;
    luaL_getmetatable (lua, MC_LUA_VIEWER_CONTROLLER_MT);
    lua_setmetatable (lua, -2);
    lua_pushvalue (lua, 1);
    controller->definition_ref = luaL_ref (lua, LUA_REGISTRYINDEX);

    if (arguments >= 3)
        lua_pushvalue (lua, 3);
    else
        lua_newtable (lua);
    initial_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
    if (definition->initial_params_ref != LUA_NOREF)
    {
        lua_rawgeti (lua, LUA_REGISTRYINDEX, definition->initial_params_ref);
        lua_rawgeti (lua, LUA_REGISTRYINDEX, controller->session_ref);
        lua_rawgeti (lua, LUA_REGISTRYINDEX, initial_ref);
        if (lua_pcall (lua, 2, 1, 0) != LUA_OK)
            return lua_error (lua);
        controller->live_ref = luaL_ref (lua, LUA_REGISTRYINDEX);
        luaL_unref (lua, LUA_REGISTRYINDEX, initial_ref);
    }
    else
        controller->live_ref = initial_ref;
    controller->runtime_id = ++package->runtime->next_viewer_controller_id;
    key = g_new (guint64, 1);
    *key = controller->runtime_id;
    g_hash_table_insert (package->runtime->viewer_controllers, key, controller);
    return 1;
}

static int
mc_lua_viewer_source_define (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_lua_viewer_definition_t *definition;
    mc_lua_viewer_definition_t **holder;

    luaL_checktype (lua, 1, LUA_TTABLE);
    definition = g_new0 (mc_lua_viewer_definition_t, 1);
    definition->package = package;
    definition->id = mc_lua_dup_table_string (lua, 1, "id");
    lua_getfield (lua, 1, "help");
    if (lua_istable (lua, -1))
    {
        definition->help_file = mc_lua_dup_table_string (lua, -1, "file");
        definition->help_node = mc_lua_dup_table_string (lua, -1, "node");
    }
    lua_pop (lua, 1);
    definition->open_ref = definition->initial_params_ref = definition->prepare_ref =
        definition->options_ref = definition->close_ref = LUA_NOREF;
    if (definition->id == NULL)
    {
        mc_lua_viewer_definition_destroy (definition);
        return luaL_error (lua, "viewer source definition requires id");
    }
    definition->open_ref = mc_lua_panel_callback_ref (lua, 1, "open", FALSE);
    definition->initial_params_ref = mc_lua_panel_callback_ref (lua, 1, "initial_params", FALSE);
    definition->prepare_ref = mc_lua_panel_callback_ref (lua, 1, "prepare", FALSE);
    definition->options_ref = mc_lua_panel_callback_ref (lua, 1, "options", FALSE);
    definition->close_ref = mc_lua_panel_callback_ref (lua, 1, "close", FALSE);
    if (definition->open_ref == LUA_NOREF || definition->prepare_ref == LUA_NOREF
        || definition->close_ref == LUA_NOREF)
    {
        mc_lua_viewer_definition_destroy (definition);
        return luaL_error (lua, "viewer definition requires open, prepare and close functions");
    }
    g_ptr_array_add (package->viewer_definitions, definition);
    holder = lua_newuserdata (lua, sizeof (*holder));
    *holder = definition;
    luaL_getmetatable (lua, MC_LUA_VIEWER_DEFINITION_MT);
    lua_setmetatable (lua, -2);
    return 1;
}

static void
mc_lua_viewer_definition_destroy (gpointer data)
{
    mc_lua_viewer_definition_t *definition = data;
    lua_State *lua;

    if (definition == NULL)
        return;
    lua = definition->package != NULL ? definition->package->lua : NULL;
    if (lua != NULL)
    {
        luaL_unref (lua, LUA_REGISTRYINDEX, definition->open_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, definition->initial_params_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, definition->prepare_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, definition->options_ref);
        luaL_unref (lua, LUA_REGISTRYINDEX, definition->close_ref);
    }
    g_free (definition->id);
    g_free (definition->help_file);
    g_free (definition->help_node);
    g_free (definition);
}

static int
mc_lua_ui_open_viewer (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_lua_viewer_controller_t *controller;
    mc_runtime_viewer_controller_t descriptor = { 0 };
    mc_runtime_viewer_spec_t initial = { 0 };
    const char *error = NULL;
    gboolean ok;

    luaL_checktype (lua, 1, LUA_TTABLE);
    lua_getfield (lua, 1, "controller");
    controller = mc_lua_viewer_controller_check (lua, -1);
    lua_pop (lua, 1);
    if (controller->owned || controller->closed)
        return luaL_error (lua, controller->owned ? "owned" : "closed");
    if (package->runtime->host->struct_size < MC_LUA_HOST_API_VIEWER_SOURCE_SIZE
        || package->runtime->host->viewer_controller_open == NULL)
        return luaL_error (lua, "viewer source host is unavailable");
    if (!mc_lua_viewer_call_prepare (controller, controller->live_ref, &initial))
        return luaL_error (lua, "invalid_source");
    descriptor.struct_size = sizeof (descriptor);
    descriptor.api_version = 1;
    descriptor.controller_id = controller->runtime_id;
    descriptor.initial_spec = &initial;
    descriptor.dispatch = mc_lua_viewer_dispatch;
    descriptor.spec_free = mc_lua_viewer_spec_free;
    descriptor.help_file = controller->definition->help_file;
    descriptor.help_node = controller->definition->help_node;
    controller->owned = TRUE;
    ok = package->runtime->host->viewer_controller_open (package->runtime->context, &descriptor,
                                                         &error);
    mc_lua_viewer_spec_free (package->runtime->context, &initial);
    if (!ok)
    {
        controller->owned = FALSE;
        lua_pushnil (lua);
        lua_pushstring (lua, error != NULL ? error : "open_failed");
        return 2;
    }
    lua_pushboolean (lua, TRUE);
    return 1;
}

static int
mc_lua_ui_open_diff (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *left;
    const char *right;
    const char *left_label;
    const char *right_label;
    const char *error = NULL;
    size_t left_length;
    size_t right_length;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    if (package->runtime->host->struct_size < MC_LUA_HOST_API_OPEN_DIFF_SIZE
        || package->runtime->host->ui_open_diff == NULL)
        return mc_lua_return_error (lua, "not_supported");
    luaL_checktype (lua, 1, LUA_TTABLE);
    lua_getfield (lua, 1, "left");
    left = luaL_checklstring (lua, -1, &left_length);
    lua_getfield (lua, 1, "right");
    right = luaL_checklstring (lua, -1, &right_length);
    lua_getfield (lua, 1, "left_label");
    left_label = luaL_optstring (lua, -1, "before");
    lua_getfield (lua, 1, "right_label");
    right_label = luaL_optstring (lua, -1, "after");
    if (!package->runtime->host->ui_open_diff (package->runtime->context, left, left_length, right,
                                               right_length, left_label, right_label, &error))
    {
        lua_pop (lua, 4);
        return mc_lua_return_error (lua, error);
    }
    lua_pop (lua, 4);
    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_on (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *event_name;
    mc_runtime_event_id_t event_id;
    lua_Integer priority = 0;
    mc_lua_subscription_t *subscription;
    mc_runtime_subscription_t token;
    GError *mcerror = NULL;

    if (package == NULL || package->runtime == NULL || package->runtime->stopping)
        return luaL_error (lua, "Lua runtime is stopping");

    event_name = luaL_checkstring (lua, 1);
    event_id = mc_lua_event_id_from_name (event_name);
    if (event_id == MC_RUNTIME_EVENT_INVALID)
    {
        lua_pushnil (lua);
        lua_pushfstring (lua, "unknown event '%s'", event_name);
        return 2;
    }

    luaL_checktype (lua, 2, LUA_TFUNCTION);
    if (!lua_isnoneornil (lua, 3))
    {
        luaL_checktype (lua, 3, LUA_TTABLE);
        lua_getfield (lua, 3, "priority");
        if (!lua_isnil (lua, -1))
        {
            if (!lua_isinteger (lua, -1))
                return luaL_error (lua, "options.priority must be an integer");
            priority = lua_tointeger (lua, -1);
        }
        lua_pop (lua, 1);
    }

    if (priority < -100 || priority > 100)
    {
        lua_pushnil (lua);
        lua_pushliteral (lua, "options.priority must be between -100 and 100");
        return 2;
    }

    subscription = g_new0 (mc_lua_subscription_t, 1);
    subscription->package = package;
    subscription->callback_ref = LUA_NOREF;
    lua_pushvalue (lua, 2);
    subscription->callback_ref = luaL_ref (lua, LUA_REGISTRYINDEX);

    token = package->runtime->host->subscribe (package->runtime->context, event_id, (int) priority,
                                               mc_lua_event_callback, subscription,
                                               mc_lua_subscription_destroy, &mcerror);
    if (token == 0)
    {
        const char *message = mcerror != NULL ? mcerror->message : "could not register callback";

        luaL_unref (lua, LUA_REGISTRYINDEX, subscription->callback_ref);
        g_free (subscription);
        lua_pushnil (lua);
        lua_pushstring (lua, message);
        g_clear_error (&mcerror);
        return 2;
    }

    subscription->token = token;
    subscription->token_key = g_new (guint64, 1);
    *subscription->token_key = token;
    g_hash_table_insert (package->subscriptions, subscription->token_key, subscription);

    lua_pushinteger (lua, (lua_Integer) token);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_off (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    lua_Integer lua_token;
    guint64 token;
    mc_lua_subscription_t *subscription;
    gboolean removed;

    if (package == NULL || package->runtime == NULL)
    {
        lua_pushboolean (lua, FALSE);
        return 1;
    }

    lua_token = luaL_checkinteger (lua, 1);
    if (lua_token <= 0)
    {
        lua_pushboolean (lua, FALSE);
        return 1;
    }

    token = (guint64) lua_token;
    subscription = (mc_lua_subscription_t *) g_hash_table_lookup (package->subscriptions, &token);
    if (subscription == NULL)
    {
        lua_pushboolean (lua, FALSE);
        return 1;
    }

    removed = package->runtime->host->unsubscribe (package->runtime->context, token);
    lua_pushboolean (lua, removed);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_macro (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *id;
    const char *area;
    const char *key = NULL;
    const char *description;
    char *normalized_key;
    lua_Integer priority = 50;
    guint i;
    mc_lua_macro_t *macro;

    if (package == NULL || package->runtime == NULL || package->runtime->stopping)
        return luaL_error (lua, "Lua runtime is stopping");
    if ((package->provides & MC_LUA_PROVIDES_MACROS) == 0)
        return mc_lua_return_error (lua, "manifest_does_not_declare_macros");

    luaL_checktype (lua, 1, LUA_TTABLE);

    lua_getfield (lua, 1, "id");
    id = luaL_checkstring (lua, -1);
    if (!mc_lua_id_is_valid (id))
    {
        lua_pop (lua, 1);
        return mc_lua_return_error (lua, "invalid_macro_id");
    }
    lua_pop (lua, 1);

    lua_getfield (lua, 1, "area");
    area = luaL_checkstring (lua, -1);
    if (g_ascii_strcasecmp (area, "editor") != 0)
    {
        lua_pop (lua, 1);
        return mc_lua_return_error (lua, "unsupported_macro_area");
    }
    lua_pop (lua, 1);

    if (g_strcmp0 (package->workspace, "mcedit") != 0)
        return mc_lua_return_error (lua, "macro_area_not_allowed_by_workspace");

    lua_getfield (lua, 1, "key");
    if (lua_isnil (lua, -1))
        normalized_key = NULL;
    else
    {
        key = luaL_checkstring (lua, -1);
        normalized_key = mc_lua_key_name_normalize (key);
    }
    lua_pop (lua, 1);
    if (key != NULL && normalized_key == NULL)
        return mc_lua_return_error (lua, "invalid_macro_key");

    lua_getfield (lua, 1, "description");
    description = luaL_checkstring (lua, -1);
    if (description[0] == '\0')
    {
        lua_pop (lua, 1);
        g_free (normalized_key);
        return mc_lua_return_error (lua, "invalid_macro_description");
    }
    lua_pop (lua, 1);

    lua_getfield (lua, 1, "priority");
    if (!lua_isnil (lua, -1))
    {
        if (!lua_isinteger (lua, -1))
        {
            lua_pop (lua, 1);
            g_free (normalized_key);
            return luaL_error (lua, "macro.priority must be an integer");
        }
        priority = lua_tointeger (lua, -1);
    }
    lua_pop (lua, 1);
    if (priority < 0 || priority > 100)
    {
        g_free (normalized_key);
        return mc_lua_return_error (lua, "macro.priority must be between 0 and 100");
    }

    for (i = 0; package->macros != NULL && i < package->macros->len; i++)
    {
        const mc_lua_macro_t *existing =
            (const mc_lua_macro_t *) g_ptr_array_index (package->macros, i);

        if (g_strcmp0 (existing->id, id) == 0)
        {
            g_free (normalized_key);
            return mc_lua_return_error (lua, "duplicate_macro_id");
        }
    }

    lua_getfield (lua, 1, "action");
    if (!lua_isfunction (lua, -1))
    {
        lua_pop (lua, 1);
        g_free (normalized_key);
        return luaL_error (lua, "macro.action must be a function");
    }

    macro = g_new0 (mc_lua_macro_t, 1);
    macro->package = package;
    macro->id = g_strdup (id);
    macro->area = g_strdup ("editor");
    macro->key = normalized_key;
    macro->display_key = g_strdup (key);
    macro->description = g_strdup (description);
    macro->priority = (int) priority;
    macro->menu_position = 1000;
    macro->action_ref = LUA_NOREF;
    macro->listed = TRUE;
    lua_getfield (lua, 1, "listed");
    if (!lua_isnil (lua, -1))
    {
        if (!lua_isboolean (lua, -1))
        {
            lua_pop (lua, 1);
            mc_lua_macro_destroy (macro);
            return luaL_error (lua, "macro.listed must be a boolean");
        }
        macro->listed = lua_toboolean (lua, -1);
    }
    lua_pop (lua, 1);

    lua_getfield (lua, 1, "menu");
    if (!lua_isnil (lua, -1))
    {
        const char *menu_path;
        const char *menu_label;
        lua_Integer menu_position = 1000;

        if (!lua_istable (lua, -1))
        {
            lua_pop (lua, 1);
            mc_lua_macro_destroy (macro);
            return luaL_error (lua, "macro.menu must be a table");
        }

        lua_getfield (lua, -1, "path");
        if (!lua_isstring (lua, -1))
        {
            lua_pop (lua, 2);
            mc_lua_macro_destroy (macro);
            return luaL_error (lua, "macro.menu.path must be a string");
        }
        menu_path = lua_tostring (lua, -1);
        if (menu_path[0] == '\0' || strchr (menu_path, '/') != NULL
            || strchr (menu_path, '\\') != NULL)
        {
            lua_pop (lua, 2);
            mc_lua_macro_destroy (macro);
            return mc_lua_return_error (lua, "invalid_macro_menu_path");
        }
        macro->menu_path = g_strdup (menu_path);
        lua_pop (lua, 1);

        lua_getfield (lua, -1, "label");
        if (lua_isnil (lua, -1))
            menu_label = description;
        else
        {
            if (!lua_isstring (lua, -1))
            {
                lua_pop (lua, 2);
                mc_lua_macro_destroy (macro);
                return luaL_error (lua, "macro.menu.label must be a string");
            }
            menu_label = lua_tostring (lua, -1);
        }
        if (menu_label[0] == '\0')
        {
            lua_pop (lua, 2);
            mc_lua_macro_destroy (macro);
            return mc_lua_return_error (lua, "invalid_macro_menu_label");
        }
        macro->menu_label = g_strdup (menu_label);
        lua_pop (lua, 1);

        lua_getfield (lua, -1, "position");
        if (!lua_isnil (lua, -1))
        {
            if (!lua_isinteger (lua, -1))
            {
                lua_pop (lua, 2);
                mc_lua_macro_destroy (macro);
                return luaL_error (lua, "macro.menu.position must be an integer");
            }
            menu_position = lua_tointeger (lua, -1);
        }
        lua_pop (lua, 1);
        if (menu_position < -100000 || menu_position > 100000)
        {
            lua_pop (lua, 1);
            mc_lua_macro_destroy (macro);
            return mc_lua_return_error (lua, "macro.menu.position_out_of_range");
        }
        macro->menu_position = (int) menu_position;
    }
    lua_pop (lua, 1);
    macro->action_ref = luaL_ref (lua, LUA_REGISTRYINDEX);

    g_ptr_array_add (package->macros, macro);
    g_ptr_array_add (package->runtime->macros, macro);
    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_log_message (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *level = lua_tostring (lua, lua_upvalueindex (1));
    const char *message = luaL_checkstring (lua, 1);

    mc_lua_log (package, level != NULL ? level : "info", message);
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_process_run (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *command;
    const char *error = NULL;
    lua_Integer max_output = 8 * 1024 * 1024;
    mc_runtime_process_result_t result = { 0 };

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    if (!mc_lua_mutation_is_allowed (lua, package))
        return 2;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_PROCESS,
                                     MC_LUA_HOST_API_PROCESS_SIZE)
        || package->runtime->host->process_run_shell == NULL
        || package->runtime->host->process_result_free == NULL)
        return mc_lua_return_error (lua, "not_supported");

    luaL_checktype (lua, 1, LUA_TTABLE);
    lua_getfield (lua, 1, "command");
    command = luaL_checkstring (lua, -1);
    if (command[0] == '\0')
    {
        lua_pop (lua, 1);
        return mc_lua_return_error (lua, "invalid_command");
    }
    lua_pop (lua, 1);

    lua_getfield (lua, 1, "max_output");
    if (!lua_isnil (lua, -1))
    {
        if (!lua_isinteger (lua, -1))
        {
            lua_pop (lua, 1);
            return luaL_error (lua, "process.max_output must be an integer");
        }
        max_output = lua_tointeger (lua, -1);
    }
    lua_pop (lua, 1);
    if (max_output < 1 || max_output > 64 * 1024 * 1024)
        return mc_lua_return_error (lua, "max_output_out_of_range");

    if (!package->runtime->host->process_run_shell (package->runtime->context, command,
                                                    (gsize) max_output, &result, &error))
        return mc_lua_return_error (lua, error);

    lua_createtable (lua, 0, 7);
    lua_pushlstring (lua, result.out.data != NULL ? result.out.data : "", result.out.length);
    lua_setfield (lua, -2, "stdout");
    lua_pushlstring (lua, result.err.data != NULL ? result.err.data : "", result.err.length);
    lua_setfield (lua, -2, "stderr");
    if (result.exit_code >= 0)
        lua_pushinteger (lua, result.exit_code);
    else
        lua_pushnil (lua);
    lua_setfield (lua, -2, "exit_code");
    if (result.term_signal > 0)
        lua_pushinteger (lua, result.term_signal);
    else
        lua_pushnil (lua);
    lua_setfield (lua, -2, "signal");
    lua_pushboolean (lua, result.out_truncated);
    lua_setfield (lua, -2, "stdout_truncated");
    lua_pushboolean (lua, result.err_truncated);
    lua_setfield (lua, -2, "stderr_truncated");
    package->runtime->host->process_result_free (package->runtime->context, &result);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_not_ready (lua_State *lua)
{
    lua_pushnil (lua);
    lua_pushliteral (lua, "not_ready");
    return 2;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_dialog_controls_free (mc_runtime_dialog_control_t *controls, guint count)
{
    guint i;

    for (i = 0; controls != NULL && i < count; i++)
    {
        g_free ((char *) controls[i].id);
        g_free ((char *) controls[i].text);
        g_free ((char *) controls[i].label);
        g_free ((char *) controls[i].value);
        if (controls[i].options != NULL)
        {
            guint j;
            for (j = 0; j < controls[i].options_count; j++)
            {
                g_free ((char *) controls[i].options[j].id);
                g_free ((char *) controls[i].options[j].label);
            }
            g_free ((mc_runtime_dialog_option_t *) controls[i].options);
        }
        mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) controls[i].controls,
                                     controls[i].controls_count);
    }
    g_free (controls);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_string (lua_State *lua, int table, const char *name, gboolean required, gsize maximum,
                      char **value)
{
    const char *text;

    lua_getfield (lua, table, name);
    if (lua_isnil (lua, -1) && !required)
    {
        lua_pop (lua, 1);
        return TRUE;
    }
    if (!lua_isstring (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    text = lua_tostring (lua, -1);
    if (text == NULL || strlen (text) > maximum || !g_utf8_validate (text, -1, NULL))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    *value = g_strdup (text);
    lua_pop (lua, 1);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_uint (lua_State *lua, int table, const char *name, guint *value, gboolean *present)
{
    lua_getfield (lua, table, name);
    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        return TRUE;
    }
    if (!lua_isinteger (lua, -1) || lua_tointeger (lua, -1) <= 0
        || (guint64) lua_tointeger (lua, -1) > G_MAXUINT)
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    *value = (guint) lua_tointeger (lua, -1);
    *present = TRUE;
    lua_pop (lua, 1);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_boolean (lua_State *lua, int table, const char *name, gboolean *value)
{
    lua_getfield (lua, table, name);
    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        return TRUE;
    }
    if (!lua_isboolean (lua, -1))
    {
        lua_pop (lua, 1);
        return FALSE;
    }
    *value = lua_toboolean (lua, -1);
    lua_pop (lua, 1);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_completion_name_is_valid (const char *name)
{
    static const char *const names[] = { "files", "hosts", "commands", "variables",
                                         "users", "cd",    "shell",    NULL };
    int i;

    for (i = 0; names[i] != NULL; i++)
        if (strcmp (name, names[i]) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_input_completion (lua_State *lua, int table, mc_runtime_dialog_control_t *control)
{
    mc_runtime_dialog_option_t *options = NULL;
    guint count, i;
    gboolean valid = TRUE;

    table = lua_absindex (lua, table);
    lua_getfield (lua, table, "completion");
    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        return TRUE;
    }
    if (!lua_istable (lua, -1) || (count = (guint) lua_rawlen (lua, -1)) == 0 || count > 7)
    {
        lua_pop (lua, 1);
        return FALSE;
    }

    options = g_new0 (mc_runtime_dialog_option_t, count);
    for (i = 0; i < count && valid; i++)
    {
        const char *name;
        guint previous;

        lua_rawgeti (lua, -1, (lua_Integer) i + 1);
        name = lua_isstring (lua, -1) ? lua_tostring (lua, -1) : NULL;
        valid = name != NULL && mc_lua_dialog_completion_name_is_valid (name);
        for (previous = 0; valid && previous < i; previous++)
            if (strcmp (options[previous].id, name) == 0)
                valid = FALSE;
        if (valid)
            options[i].id = g_strdup (name);
        lua_pop (lua, 1);
    }
    lua_pop (lua, 1);
    if (!valid)
    {
        for (i = 0; i < count; i++)
            g_free ((char *) options[i].id);
        g_free (options);
        return FALSE;
    }

    control->options = options;
    control->options_count = count;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_parse_controls (lua_State *lua, int index, guint depth, mc_lua_dialog_spec_t *spec,
                              GHashTable *ids, mc_runtime_dialog_control_t **result, guint *count)
{
    guint i, length;
    mc_runtime_dialog_control_t *controls;

    if (!lua_istable (lua, index) || depth > MC_LUA_DIALOG_MAX_DEPTH
        || (length = (guint) lua_rawlen (lua, index)) == 0
        || spec->control_count + length > MC_LUA_DIALOG_MAX_CONTROLS)
        return FALSE;
    controls = g_new0 (mc_runtime_dialog_control_t, length);
    spec->control_count += length;
    for (i = 0; i < length; i++)
    {
        mc_runtime_dialog_control_t *control = &controls[i];
        const char *type;

        lua_rawgeti (lua, index, (lua_Integer) i + 1);
        if (!lua_istable (lua, -1))
            goto fail;
        lua_getfield (lua, -1, "type");
        type = lua_tostring (lua, -1);
        lua_pop (lua, 1);
        if (type == NULL)
            goto fail;
        if (strcmp (type, "label") == 0)
            control->type = MC_RUNTIME_DIALOG_LABEL;
        else if (strcmp (type, "input") == 0)
            control->type = MC_RUNTIME_DIALOG_INPUT;
        else if (strcmp (type, "checkbox") == 0)
            control->type = MC_RUNTIME_DIALOG_CHECKBOX;
        else if (strcmp (type, "select") == 0)
            control->type = MC_RUNTIME_DIALOG_SELECT;
        else if (strcmp (type, "separator") == 0)
            control->type = MC_RUNTIME_DIALOG_SEPARATOR;
        else if (strcmp (type, "hbox") == 0)
            control->type = MC_RUNTIME_DIALOG_HBOX;
        else if (strcmp (type, "vbox") == 0)
            control->type = MC_RUNTIME_DIALOG_VBOX;
        else if (strcmp (type, "spacer") == 0)
            control->type = MC_RUNTIME_DIALOG_SPACER;
        else if (strcmp (type, "button") == 0)
            control->type = MC_RUNTIME_DIALOG_BUTTON;
        else
            goto fail;
        if (!mc_lua_dialog_uint (lua, -1, "x", &control->x, &control->has_x)
            || !mc_lua_dialog_uint (lua, -1, "y", &control->y, &control->has_y)
            || !mc_lua_dialog_uint (lua, -1, "width", &control->width, &control->has_width)
            || !mc_lua_dialog_uint (lua, -1, "height", &control->height, &control->has_height)
            || !mc_lua_dialog_boolean (lua, -1, "expand_x", &control->expand_x)
            || !mc_lua_dialog_boolean (lua, -1, "expand_y", &control->expand_y))
            goto fail;
        if (control->type == MC_RUNTIME_DIALOG_LABEL
            || control->type == MC_RUNTIME_DIALOG_SEPARATOR)
        {
            if (!mc_lua_dialog_string (
                    lua, -1, control->type == MC_RUNTIME_DIALOG_LABEL ? "text" : "label",
                    control->type == MC_RUNTIME_DIALOG_LABEL, 16384, (char **) &control->text))
                goto fail;
        }
        else if (control->type == MC_RUNTIME_DIALOG_HBOX || control->type == MC_RUNTIME_DIALOG_VBOX)
        {
            lua_getfield (lua, -1, "controls");
            if (!mc_lua_dialog_parse_controls (lua, lua_gettop (lua), depth + 1, spec, ids,
                                               (mc_runtime_dialog_control_t **) &control->controls,
                                               &control->controls_count))
                goto fail;
            lua_pop (lua, 1);
        }
        else if (control->type != MC_RUNTIME_DIALOG_SPACER)
        {
            if (!mc_lua_dialog_string (lua, -1, "id", TRUE, MC_LUA_ID_MAX_LENGTH,
                                       (char **) &control->id)
                || !mc_lua_id_is_valid (control->id) || g_hash_table_contains (ids, control->id))
                goto fail;
            g_hash_table_add (ids, (gpointer) control->id);
            if (control->type == MC_RUNTIME_DIALOG_BUTTON
                || control->type == MC_RUNTIME_DIALOG_CHECKBOX
                || control->type == MC_RUNTIME_DIALOG_SELECT)
                if (!mc_lua_dialog_string (lua, -1, "label", TRUE, 4096, (char **) &control->label))
                    goto fail;
            if (control->type == MC_RUNTIME_DIALOG_INPUT
                || control->type == MC_RUNTIME_DIALOG_SELECT)
                if (!mc_lua_dialog_string (lua, -1, "value", FALSE, 4096,
                                           (char **) &control->value))
                    goto fail;
            if (control->type == MC_RUNTIME_DIALOG_INPUT)
            {
                if (!mc_lua_dialog_string (lua, -1, "history", FALSE, MC_LUA_ID_MAX_LENGTH,
                                           (char **) &control->text)
                    || (control->text != NULL && control->text[0] != '\0'
                        && !mc_lua_id_is_valid (control->text))
                    || !mc_lua_dialog_input_completion (lua, -1, control)
                    || !mc_lua_dialog_boolean (lua, -1, "complete_on_tab", &control->checked)
                    || (control->checked && control->options_count == 0))
                    goto fail;
            }
            if (control->type == MC_RUNTIME_DIALOG_CHECKBOX
                && !mc_lua_dialog_boolean (lua, -1, "value", &control->checked))
                goto fail;
            if (control->type == MC_RUNTIME_DIALOG_SELECT)
            {
                guint option_count, option_index;
                mc_runtime_dialog_option_t *options;

                lua_getfield (lua, -1, "options");
                option_count = lua_istable (lua, -1) ? (guint) lua_rawlen (lua, -1) : 0;
                if (option_count == 0 || option_count > MC_LUA_DIALOG_MAX_OPTIONS)
                    goto fail;
                options = g_new0 (mc_runtime_dialog_option_t, option_count);
                for (option_index = 0; option_index < option_count; option_index++)
                {
                    guint previous;
                    lua_rawgeti (lua, -1, (lua_Integer) option_index + 1);
                    if (!lua_istable (lua, -1)
                        || !mc_lua_dialog_string (lua, -1, "id", TRUE, MC_LUA_ID_MAX_LENGTH,
                                                  (char **) &options[option_index].id)
                        || !mc_lua_id_is_valid (options[option_index].id)
                        || !mc_lua_dialog_string (lua, -1, "label", TRUE, 4096,
                                                  (char **) &options[option_index].label))
                    {
                        lua_pop (lua, 1);
                        g_free (options);
                        goto fail;
                    }
                    for (previous = 0; previous < option_index; previous++)
                        if (strcmp (options[previous].id, options[option_index].id) == 0)
                        {
                            lua_pop (lua, 1);
                            g_free (options);
                            goto fail;
                        }
                    lua_pop (lua, 1);
                }
                lua_pop (lua, 1);
                control->options = options;
                control->options_count = option_count;
                if (control->value != NULL)
                {
                    gboolean found = FALSE;
                    for (option_index = 0; option_index < option_count; option_index++)
                        if (strcmp (control->value, options[option_index].id) == 0)
                            found = TRUE;
                    if (!found)
                        goto fail;
                }
            }
            if (control->type == MC_RUNTIME_DIALOG_BUTTON
                && (!mc_lua_dialog_boolean (lua, -1, "default", &control->default_button)
                    || !mc_lua_dialog_boolean (lua, -1, "cancel", &control->cancel_button)))
                goto fail;
        }
        lua_pop (lua, 1);
    }
    *result = controls;
    *count = length;
    return TRUE;
fail:
    lua_pop (lua, 1);
    spec->control_count -= length;
    mc_lua_dialog_controls_free (controls, length);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_dialog_check_controls (const mc_runtime_dialog_control_t *controls, guint count,
                              guint *buttons, guint *defaults, guint *cancels)
{
    guint i;

    for (i = 0; i < count; i++)
    {
        const mc_runtime_dialog_control_t *control = &controls[i];
        if (control->type == MC_RUNTIME_DIALOG_BUTTON)
        {
            (*buttons)++;
            if (control->default_button)
                (*defaults)++;
            if (control->cancel_button)
                (*cancels)++;
        }
        if ((control->type == MC_RUNTIME_DIALOG_HBOX || control->type == MC_RUNTIME_DIALOG_VBOX)
            && !mc_lua_dialog_check_controls (control->controls, control->controls_count, buttons,
                                              defaults, cancels))
            return FALSE;
    }
    return *defaults <= 1 && *cancels <= 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_ui_dialog (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    mc_lua_dialog_spec_t spec = { { NULL, 0, 0, FALSE, FALSE, NULL, 0 }, 0 };
    GHashTable *ids;
    mc_runtime_dialog_result_t result = { NULL, NULL, 0 };
    const char *error = NULL;
    guint buttons = 0, defaults = 0, cancels = 0, i;

    if (!mc_lua_require_active_context (lua, package) || !mc_lua_mutation_is_allowed (lua, package))
        return 2;
    if (package->active_event == MC_RUNTIME_EVENT_STARTUP
        || package->active_event == MC_RUNTIME_EVENT_SHUTDOWN)
        return mc_lua_return_error (lua, "forbidden_in_phase");
    if (!lua_istable (lua, 1))
        return mc_lua_return_error (lua, "invalid_dialog");
    if (!mc_lua_dialog_string (lua, 1, "title", TRUE, 256, (char **) &spec.dialog.title)
        || !mc_lua_dialog_uint (lua, 1, "width", &spec.dialog.width, &spec.dialog.has_width)
        || !mc_lua_dialog_uint (lua, 1, "height", &spec.dialog.height, &spec.dialog.has_height))
        goto invalid;
    ids = g_hash_table_new (g_str_hash, g_str_equal);
    lua_getfield (lua, 1, "controls");
    if (!mc_lua_dialog_parse_controls (lua, lua_gettop (lua), 1, &spec, ids,
                                       (mc_runtime_dialog_control_t **) &spec.dialog.controls,
                                       &spec.dialog.controls_count))
    {
        lua_pop (lua, 1);
        g_hash_table_destroy (ids);
        goto invalid;
    }
    lua_pop (lua, 1);
    g_hash_table_destroy (ids);
    if (!mc_lua_dialog_check_controls (spec.dialog.controls, spec.dialog.controls_count, &buttons,
                                       &defaults, &cancels)
        || buttons == 0)
        goto invalid;
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI, MC_LUA_HOST_API_DIALOG_SIZE)
        || package->runtime->host->ui_dialog == NULL
        || package->runtime->host->dialog_result_free == NULL)
        goto not_ready;
    if (!package->runtime->host->ui_dialog (package->runtime->context, &spec.dialog, &result,
                                            &error))
    {
        mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) spec.dialog.controls,
                                     spec.dialog.controls_count);
        g_free ((char *) spec.dialog.title);
        return mc_lua_return_error (lua, error != NULL ? error : "not_ready");
    }
    lua_newtable (lua);
    lua_pushstring (lua, result.button_id != NULL ? result.button_id : "");
    lua_setfield (lua, -2, "button");
    lua_newtable (lua);
    for (i = 0; i < result.values_count; i++)
    {
        if (result.values[i].is_boolean)
            lua_pushboolean (lua, result.values[i].checked);
        else
            lua_pushstring (lua, result.values[i].value != NULL ? result.values[i].value : "");
        lua_setfield (lua, -2, result.values[i].id);
    }
    lua_setfield (lua, -2, "values");
    package->runtime->host->dialog_result_free (package->runtime->context, &result);
    mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) spec.dialog.controls,
                                 spec.dialog.controls_count);
    g_free ((char *) spec.dialog.title);
    return 1;
not_ready:
    mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) spec.dialog.controls,
                                 spec.dialog.controls_count);
    g_free ((char *) spec.dialog.title);
    return mc_lua_not_ready (lua);
invalid:
    mc_lua_log (package, "error", "invalid Lua dialog specification");
    mc_lua_dialog_controls_free ((mc_runtime_dialog_control_t *) spec.dialog.controls,
                                 spec.dialog.controls_count);
    g_free ((char *) spec.dialog.title);
    return mc_lua_return_error (lua, "invalid_dialog");
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_ui_indicator (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *id;
    const char *area = "editor";
    const char *text;
    lua_Integer priority = 0;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package) || !mc_lua_mutation_is_allowed (lua, package))
        return 2;
    luaL_checktype (lua, 1, LUA_TTABLE);

    lua_getfield (lua, 1, "id");
    id = luaL_checkstring (lua, -1);
    lua_pop (lua, 1);
    lua_getfield (lua, 1, "area");
    if (!lua_isnil (lua, -1))
        area = luaL_checkstring (lua, -1);
    lua_pop (lua, 1);
    lua_getfield (lua, 1, "text");
    text = luaL_checkstring (lua, -1);
    lua_pop (lua, 1);
    lua_getfield (lua, 1, "priority");
    if (!lua_isnil (lua, -1))
    {
        if (!lua_isinteger (lua, -1))
            return luaL_error (lua, "indicator.priority must be an integer");
        priority = lua_tointeger (lua, -1);
    }
    lua_pop (lua, 1);

    if (!mc_lua_id_is_valid (id) || strcmp (area, "editor") != 0 || text[0] == '\0'
        || strlen (text) > 128 || !g_utf8_validate (text, -1, NULL) || priority < -100000
        || priority > 100000)
        return mc_lua_return_error (lua, "invalid_indicator");

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI,
                                     MC_LUA_HOST_API_INDICATORS_SIZE)
        || package->runtime->host->ui_indicator_set == NULL
        || !package->runtime->host->ui_indicator_set (package->runtime->context, package->id, area,
                                                      id, text, (gint) priority, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "not_ready");

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_ui_indicator_clear (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *id;
    const char *area;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package) || !mc_lua_mutation_is_allowed (lua, package))
        return 2;
    id = luaL_checkstring (lua, 1);
    area = luaL_optstring (lua, 2, "editor");

    if (!mc_lua_id_is_valid (id) || strcmp (area, "editor") != 0)
        return mc_lua_return_error (lua, "invalid_indicator");

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI,
                                     MC_LUA_HOST_API_INDICATORS_SIZE)
        || package->runtime->host->ui_indicator_clear == NULL
        || !package->runtime->host->ui_indicator_clear (package->runtime->context, package->id,
                                                        area, id, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "not_ready");

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_ui_status (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *text;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    text = luaL_checkstring (lua, 1);

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI, MC_LUA_HOST_API_UI_SIZE)
        || package->runtime->host->ui_status == NULL
        || !package->runtime->host->ui_status (package->runtime->context, text))
        return mc_lua_not_ready (lua);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_ui_message (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *title;
    const char *text;

    if (!mc_lua_require_active_context (lua, package) || !mc_lua_mutation_is_allowed (lua, package))
        return 2;
    title = luaL_checkstring (lua, 1);
    text = luaL_checkstring (lua, 2);

    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI, MC_LUA_HOST_API_UI_SIZE)
        || package->runtime->host->ui_message == NULL
        || !package->runtime->host->ui_message (package->runtime->context, title, text))
        return mc_lua_not_ready (lua);

    lua_pushboolean (lua, TRUE);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_ui_text_width (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *text;
    size_t text_length;
    guint width;
    const char *error = NULL;

    if (!mc_lua_require_active_context (lua, package))
        return 2;
    text = luaL_checklstring (lua, 1, &text_length);
    if (!mc_lua_host_has_capability (package, MC_RUNTIME_HOST_CAP_UI,
                                     MC_LUA_HOST_API_UI_TEXT_WIDTH_SIZE)
        || package->runtime->host->ui_text_width == NULL
        || !package->runtime->host->ui_text_width (package->runtime->context, text, text_length,
                                                   &width, &error))
        return mc_lua_return_error (lua, error != NULL ? error : "not_ready");
    lua_pushinteger (lua, (lua_Integer) width);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mc_lua_module_filename (const char *module_name)
{
    char *filename;
    char *cursor;
    char *result;

    filename = g_strdup (module_name);
    for (cursor = filename; *cursor != '\0'; cursor++)
        if (*cursor == '.')
            *cursor = G_DIR_SEPARATOR;

    result = g_strconcat (filename, ".lua", (char *) NULL);
    g_free (filename);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

static int
mc_lua_require (lua_State *lua)
{
    mc_lua_package_t *package = mc_lua_package_from_state (lua);
    const char *module_name = luaL_checkstring (lua, 1);
    char *filename;
    char *package_module;
    char *shared_module;
    char *legacy_shared_module = NULL;
    const char *module_path = NULL;
    char *message;

    if (package == NULL || package->runtime == NULL)
        return luaL_error (lua, "no Lua script context");

    if (!mc_lua_module_name_is_valid (module_name))
        return luaL_error (lua, "invalid Lua module name '%s'", module_name);

    lua_getfield (lua, LUA_REGISTRYINDEX, MC_LUA_REGISTRY_MODULES);
    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        lua_newtable (lua);
        lua_pushvalue (lua, -1);
        lua_setfield (lua, LUA_REGISTRYINDEX, MC_LUA_REGISTRY_MODULES);
    }

    lua_getfield (lua, -1, module_name);
    if (!lua_isnil (lua, -1))
    {
        if (lua_islightuserdata (lua, -1)
            && lua_touserdata (lua, -1) == &mc_lua_module_loading_sentinel)
        {
            lua_pop (lua, 2);
            return luaL_error (lua, "circular require for module '%s'", module_name);
        }
        lua_remove (lua, -2);
        return 1;
    }
    lua_pop (lua, 1);

    filename = mc_lua_module_filename (module_name);
    package_module = g_build_filename (package->root, "lib", filename, (char *) NULL);
    shared_module = g_build_filename (package->origin == MC_LUA_PACKAGE_USER
                                          ? package->runtime->user_modules_dir
                                          : mc_lua_system_modules_dir (),
                                      filename, (char *) NULL);
    if (package->origin == MC_LUA_PACKAGE_USER)
        legacy_shared_module =
            g_build_filename (package->runtime->legacy_user_modules_dir, filename, (char *) NULL);
    g_free (filename);

    if (mc_lua_file_is_trusted_under (package->root, package_module))
        module_path = package_module;
    else if (mc_lua_file_is_trusted_under (package->origin == MC_LUA_PACKAGE_USER
                                               ? package->runtime->user_modules_dir
                                               : mc_lua_system_modules_dir (),
                                           shared_module))
        module_path = shared_module;
    else if (legacy_shared_module != NULL
             && mc_lua_file_is_trusted_under (package->runtime->legacy_user_modules_dir,
                                              legacy_shared_module))
        module_path = legacy_shared_module;

    if (module_path == NULL)
    {
        g_free (package_module);
        g_free (shared_module);
        g_free (legacy_shared_module);
        lua_pop (lua, 1);
        return luaL_error (lua, "module '%s' not found", module_name);
    }

    lua_pushlightuserdata (lua, &mc_lua_module_loading_sentinel);
    lua_setfield (lua, -2, module_name);

    if (luaL_loadfile (lua, module_path) != LUA_OK || lua_pcall (lua, 0, 1, 0) != LUA_OK)
    {
        message = g_strdup (lua_tostring (lua, -1));
        lua_pop (lua, 1);
        lua_pushnil (lua);
        lua_setfield (lua, -2, module_name);
        lua_pop (lua, 1);
        g_free (package_module);
        g_free (shared_module);
        g_free (legacy_shared_module);
        return luaL_error (lua, "could not load module '%s': %s", module_name,
                           message != NULL ? message : "Lua error");
    }

    if (lua_isnil (lua, -1))
    {
        lua_pop (lua, 1);
        lua_pushboolean (lua, TRUE);
    }

    lua_pushvalue (lua, -1);
    lua_setfield (lua, -3, module_name);
    lua_remove (lua, -2);
    g_free (package_module);
    g_free (shared_module);
    g_free (legacy_shared_module);
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_install_api (mc_lua_package_t *package)
{
    lua_State *lua = package->lua;
    const char *const levels[] = { "debug", "info", "warn", "error" };
    guint i;

    if (luaL_newmetatable (lua, MC_LUA_VIEWER_DEFINITION_MT))
    {
        lua_newtable (lua);
        lua_pushcfunction (lua, mc_lua_viewer_definition_create);
        lua_setfield (lua, -2, "create");
        lua_setfield (lua, -2, "__index");
    }
    lua_pop (lua, 1);
    if (luaL_newmetatable (lua, MC_LUA_VIEWER_CONTROLLER_MT))
    {
        lua_pushcfunction (lua, mc_lua_viewer_controller_gc);
        lua_setfield (lua, -2, "__gc");
    }
    lua_pop (lua, 1);

    lua_pushlightuserdata (lua, package);
    lua_setfield (lua, LUA_REGISTRYINDEX, MC_LUA_REGISTRY_PACKAGE);

    lua_getglobal (lua, "package");
    if (lua_istable (lua, -1))
    {
        lua_pushliteral (lua, "");
        lua_setfield (lua, -2, "cpath");
        lua_pushnil (lua);
        lua_setfield (lua, -2, "loadlib");
        lua_newtable (lua);
        lua_setfield (lua, -2, "searchers");
    }
    lua_pop (lua, 1);

    lua_pushcfunction (lua, mc_lua_require);
    lua_setglobal (lua, "require");

    lua_createtable (lua, 0, 7);
    lua_pushcfunction (lua, mc_lua_on);
    lua_setfield (lua, -2, "on");
    lua_pushcfunction (lua, mc_lua_off);
    lua_setfield (lua, -2, "off");
    lua_pushcfunction (lua, mc_lua_macro);
    lua_setfield (lua, -2, "macro");
    lua_pushinteger (lua, 0);
    lua_setfield (lua, -2, "PASS");
    lua_pushinteger (lua, 1);
    lua_setfield (lua, -2, "CONSUME");

    lua_createtable (lua, 0, G_N_ELEMENTS (levels));
    for (i = 0; i < G_N_ELEMENTS (levels); i++)
    {
        lua_pushstring (lua, levels[i]);
        lua_pushcclosure (lua, mc_lua_log_message, 1);
        lua_setfield (lua, -2, levels[i]);
    }
    lua_setfield (lua, -2, "log");

    lua_createtable (lua, 0, 7);
    lua_pushcfunction (lua, mc_lua_ui_status);
    lua_setfield (lua, -2, "status");
    lua_pushcfunction (lua, mc_lua_ui_message);
    lua_setfield (lua, -2, "message");
    lua_pushcfunction (lua, mc_lua_ui_dialog);
    lua_setfield (lua, -2, "dialog");
    lua_pushcfunction (lua, mc_lua_ui_indicator);
    lua_setfield (lua, -2, "indicator");
    lua_pushcfunction (lua, mc_lua_ui_indicator_clear);
    lua_setfield (lua, -2, "indicator_clear");
    lua_pushcfunction (lua, mc_lua_ui_text_width);
    lua_setfield (lua, -2, "text_width");
    lua_pushcfunction (lua, mc_lua_ui_open_viewer);
    lua_setfield (lua, -2, "open_viewer");
    lua_pushcfunction (lua, mc_lua_ui_open_diff);
    lua_setfield (lua, -2, "open_diff");
    lua_setfield (lua, -2, "ui");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_process_run);
    lua_setfield (lua, -2, "run");
    lua_setfield (lua, -2, "process");

    lua_createtable (lua, 0, 2);
    lua_pushcfunction (lua, mc_lua_panel_active);
    lua_setfield (lua, -2, "active");
    lua_pushcfunction (lua, mc_lua_panel_passive);
    lua_setfield (lua, -2, "passive");
    lua_setfield (lua, -2, "panel");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_panel_provider_register);
    lua_setfield (lua, -2, "register");
    lua_setfield (lua, -2, "panel_provider");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_editor_current);
    lua_setfield (lua, -2, "current");
    lua_setfield (lua, -2, "editor");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_viewer_current);
    lua_setfield (lua, -2, "current");
    lua_setfield (lua, -2, "viewer");

    lua_createtable (lua, 0, 4);
    lua_pushcfunction (lua, mc_lua_source_bytes);
    lua_setfield (lua, -2, "bytes");
    lua_pushcfunction (lua, mc_lua_source_file);
    lua_setfield (lua, -2, "file");
    lua_pushcfunction (lua, mc_lua_source_process);
    lua_setfield (lua, -2, "process");
    lua_pushcfunction (lua, mc_lua_source_pipeline);
    lua_setfield (lua, -2, "pipeline");
    lua_setfield (lua, -2, "source");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_viewer_source_define);
    lua_setfield (lua, -2, "define");
    lua_setfield (lua, -2, "viewer_source");

    lua_setglobal (lua, "mc");
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
mc_lua_event_callback (gpointer runtime_context, const mc_runtime_event_snapshot_t *snapshot,
                       gpointer user_data)
{
    mc_lua_subscription_t *subscription = (mc_lua_subscription_t *) user_data;
    mc_lua_package_t *package;
    lua_State *lua;
    gboolean consume = FALSE;
    mc_runtime_event_id_t previous_event;

    (void) runtime_context;

    if (subscription == NULL || snapshot == NULL || subscription->package == NULL)
        return MC_RUNTIME_EVENT_PASS;

    package = subscription->package;
    lua = package->lua;
    if (package->closed || lua == NULL)
        return MC_RUNTIME_EVENT_PASS;

    lua_rawgeti (lua, LUA_REGISTRYINDEX, subscription->callback_ref);
    if (!lua_isfunction (lua, -1))
    {
        lua_pop (lua, 1);
        mc_lua_log (package, "error", "event callback is no longer a function");
        return MC_RUNTIME_EVENT_ERROR;
    }

    previous_event = package->active_event;
    package->callback_depth++;
    package->active_event = snapshot->event_id;
    mc_lua_push_event (lua, snapshot);
    if (lua_pcall (lua, 1, 1, 0) != LUA_OK)
    {
        mc_lua_report_error (
            package,
            snapshot->event_id == MC_RUNTIME_EVENT_STARTUP ? MC_RUNTIME_ERROR_PHASE_STARTUP
                                                           : MC_RUNTIME_ERROR_PHASE_EVENT,
            snapshot->event_id == MC_RUNTIME_EVENT_STARTUP ? "Lua startup callback failed"
                                                           : "Lua event callback failed");
        package->active_event = previous_event;
        package->callback_depth--;
        return MC_RUNTIME_EVENT_ERROR;
    }

    if (snapshot->event_id == MC_RUNTIME_EVENT_EDITOR_KEY)
    {
        consume = lua_isboolean (lua, -1)
            ? lua_toboolean (lua, -1)
            : (lua_isinteger (lua, -1) && lua_tointeger (lua, -1) == 1);
    }
    lua_pop (lua, 1);
    package->active_event = previous_event;
    package->callback_depth--;

    return consume ? MC_RUNTIME_EVENT_CONSUME : MC_RUNTIME_EVENT_PASS;
}

/* --------------------------------------------------------------------------------------------- */

static mc_lua_macro_t *
mc_lua_find_editor_macro (mc_lua_runtime_t *runtime, const mc_runtime_event_snapshot_t *snapshot)
{
    char *key;
    mc_lua_macro_t *selected = NULL;
    guint i;

    if (runtime == NULL || runtime->macros == NULL || snapshot == NULL
        || snapshot->event_id != MC_RUNTIME_EVENT_EDITOR_KEY)
        return NULL;

    key = mc_lua_key_name_normalize (snapshot->data.editor_key.key.name);
    if (key == NULL)
        return NULL;

    for (i = 0; i < runtime->macros->len; i++)
    {
        mc_lua_macro_t *macro = (mc_lua_macro_t *) g_ptr_array_index (runtime->macros, i);

        if (macro == NULL || macro->disabled || macro->package == NULL || macro->package->closed
            || g_strcmp0 (macro->area, "editor") != 0 || g_strcmp0 (macro->key, key) != 0)
            continue;

        if (selected == NULL || macro->priority > selected->priority)
            selected = macro;
    }

    g_free (key);
    return selected;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
mc_lua_invoke_macro (mc_lua_macro_t *macro, const mc_runtime_event_snapshot_t *snapshot)
{
    mc_lua_package_t *package;
    lua_State *lua;
    mc_runtime_event_id_t previous_event;
    gboolean pass = FALSE;

    if (macro == NULL || macro->disabled || macro->package == NULL || macro->package->closed
        || snapshot == NULL)
        return MC_RUNTIME_EVENT_PASS;

    package = macro->package;
    lua = package->lua;
    if (lua == NULL)
        return MC_RUNTIME_EVENT_PASS;

    lua_rawgeti (lua, LUA_REGISTRYINDEX, macro->action_ref);
    if (!lua_isfunction (lua, -1))
    {
        lua_pop (lua, 1);
        macro->disabled = TRUE;
        mc_lua_log (package, "error", "macro action is no longer a function");
        return MC_RUNTIME_EVENT_CONSUME;
    }

    previous_event = package->active_event;
    package->callback_depth++;
    package->active_event = snapshot->event_id;
    mc_lua_push_event (lua, snapshot);
    if (lua_pcall (lua, 1, 1, 0) != LUA_OK)
    {
        macro->errors++;
        mc_lua_report_error (package, MC_RUNTIME_ERROR_PHASE_MACRO, "Lua macro callback failed");
        if (macro->errors >= 3)
        {
            macro->disabled = TRUE;
            mc_lua_log (package, "warning", "macro disabled after three errors");
        }
        package->active_event = previous_event;
        package->callback_depth--;
        return MC_RUNTIME_EVENT_CONSUME;
    }

    pass = (lua_isboolean (lua, -1) && !lua_toboolean (lua, -1))
        || (lua_isinteger (lua, -1) && lua_tointeger (lua, -1) == 0);
    lua_pop (lua, 1);
    package->active_event = previous_event;
    package->callback_depth--;

    return pass ? MC_RUNTIME_EVENT_PASS : MC_RUNTIME_EVENT_CONSUME;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_event_result_t
mc_lua_macro_event_callback (gpointer runtime_context, const mc_runtime_event_snapshot_t *snapshot,
                             gpointer user_data)
{
    mc_lua_runtime_t *runtime = (mc_lua_runtime_t *) user_data;

    if (runtime == NULL || runtime != mc_lua_runtime_current || runtime->context != runtime_context
        || runtime->stopping)
        return MC_RUNTIME_EVENT_PASS;

    return mc_lua_invoke_macro (mc_lua_find_editor_macro (runtime, snapshot), snapshot);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_candidate_destroy (mc_lua_package_candidate_t *candidate)
{
    if (candidate == NULL)
        return;

    g_free (candidate->id);
    g_free (candidate->name);
    g_free (candidate->workspace);
    g_free (candidate->root);
    g_free (candidate->entry);
    g_free (candidate);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_package_info_destroy (mc_lua_package_info_t *info)
{
    if (info == NULL)
        return;

    g_free (info->id);
    g_free (info->name);
    g_free (info->workspace);
    g_free (info->root);
    g_free (info);
}

/* --------------------------------------------------------------------------------------------- */

static mc_lua_package_candidate_t *
mc_lua_package_candidate_new (const char *parent, const char *directory_name,
                              mc_lua_package_origin_t origin, const char *workspace)
{
    char *root;
    char *ini_path;
    char *entry_path;
    GKeyFile *ini = NULL;
    GError *error = NULL;
    char *id = NULL;
    char *name = NULL;
    char *entry = NULL;
    char *provides = NULL;
    guint provides_flags = MC_LUA_PROVIDES_NONE;
    gint api_version;
    mc_lua_package_candidate_t *candidate = NULL;

    if (!mc_lua_id_is_valid (directory_name) || mc_lua_workspace_find (workspace) == NULL)
        return NULL;

    root = g_build_filename (parent, directory_name, (char *) NULL);
    if (!mc_lua_path_is_trusted (root, TRUE))
        goto done;

    ini_path = g_build_filename (root, MC_LUA_MANIFEST_FILE, (char *) NULL);
    if (!mc_lua_file_is_trusted_under (root, ini_path))
        goto done_with_ini;
    ini = g_key_file_new ();
    if (!g_key_file_load_from_file (ini, ini_path, G_KEY_FILE_NONE, &error))
        goto done_with_ini;

    id = g_key_file_get_string (ini, MC_LUA_MANIFEST_GROUP, "id", &error);
    if (error != NULL || !mc_lua_id_is_valid (id) || strcmp (id, directory_name) != 0)
        goto done_with_id;

    api_version = g_key_file_get_integer (ini, MC_LUA_MANIFEST_GROUP, "api_version", &error);
    if (error != NULL || api_version != MC_LUA_API_VERSION)
        goto done_with_id;

    entry = g_key_file_get_string (ini, MC_LUA_MANIFEST_GROUP, "entry", &error);
    if (error != NULL || !mc_lua_relative_lua_file_is_valid (entry))
        goto done_with_entry;

    entry_path = g_build_filename (root, entry, (char *) NULL);
    if (!mc_lua_file_is_trusted_under (root, entry_path))
    {
        g_free (entry_path);
        goto done_with_entry;
    }
    g_free (entry_path);

    provides = g_key_file_get_string (ini, MC_LUA_MANIFEST_GROUP, "provides", NULL);
    if (!mc_lua_provides_parse (provides, &provides_flags))
        goto done_with_provides;

    candidate = g_new0 (mc_lua_package_candidate_t, 1);
    candidate->id = g_steal_pointer (&id);
    name = g_key_file_get_string (ini, MC_LUA_MANIFEST_GROUP, "name", NULL);
    candidate->name =
        name != NULL && name[0] != '\0' ? g_steal_pointer (&name) : g_strdup (candidate->id);
    candidate->workspace = g_strdup (workspace);
    candidate->root = g_steal_pointer (&root);
    candidate->entry = g_steal_pointer (&entry);
    candidate->origin = origin;
    candidate->provides = provides_flags;

done_with_provides:
    g_free (provides);
done_with_entry:
    g_free (entry);
done_with_id:
    g_free (id);
    g_free (name);
done_with_ini:
    g_clear_error (&error);
    if (ini != NULL)
        g_key_file_free (ini);
    g_free (ini_path);
done:
    g_free (root);
    return candidate;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_discover_workspace_directory (const char *directory, mc_lua_package_origin_t origin,
                                     const char *workspace, GHashTable *candidates)
{
    GDir *dir;
    const char *entry;

    if (directory == NULL)
        return;

    /* A workspace script directory is optional.  Its absence is not a trust error. */
    if (!g_file_test (directory, G_FILE_TEST_IS_DIR))
        return;

    if (!mc_lua_path_is_trusted (directory, TRUE))
    {
        fprintf (stderr, "Lua scripts: ignoring insecure directory %s\n", directory);
        return;
    }

    dir = g_dir_open (directory, 0, NULL);
    if (dir == NULL)
        return;

    while ((entry = g_dir_read_name (dir)) != NULL)
    {
        mc_lua_package_candidate_t *candidate =
            mc_lua_package_candidate_new (directory, entry, origin, workspace);

        if (candidate != NULL)
            g_hash_table_replace (candidates, g_strdup (candidate->id), candidate);
    }

    g_dir_close (dir);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_discover_script_root (const char *directory, mc_lua_package_origin_t origin,
                             GHashTable *candidates)
{
    guint i;

    if (directory == NULL)
        return;

    for (i = 0; i < G_N_ELEMENTS (mc_lua_workspaces); i++)
    {
        char *workspace_directory =
            g_build_filename (directory, mc_lua_workspaces[i].directory, (char *) NULL);

        mc_lua_discover_workspace_directory (workspace_directory, origin, mc_lua_workspaces[i].name,
                                             candidates);
        g_free (workspace_directory);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gint
mc_lua_package_candidate_compare (gconstpointer first, gconstpointer second)
{
    const mc_lua_package_candidate_t *first_candidate = (const mc_lua_package_candidate_t *) first;
    const mc_lua_package_candidate_t *second_candidate =
        (const mc_lua_package_candidate_t *) second;

    return g_strcmp0 (first_candidate->id, second_candidate->id);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_package_load (mc_lua_runtime_t *runtime, const mc_lua_package_candidate_t *candidate)
{
    mc_lua_package_t *package;
    int status;

    package = g_new0 (mc_lua_package_t, 1);
    package->runtime = runtime;
    package->id = g_strdup (candidate->id);
    package->workspace = g_strdup (candidate->workspace);
    package->root = g_strdup (candidate->root);
    package->entry_path = g_build_filename (candidate->root, candidate->entry, (char *) NULL);
    package->origin = candidate->origin;
    package->provides = candidate->provides;
    package->subscriptions = g_hash_table_new (g_int64_hash, g_int64_equal);
    package->macros = g_ptr_array_new_with_free_func (mc_lua_macro_destroy);
    package->panel_providers = g_ptr_array_new_with_free_func (mc_lua_panel_provider_destroy);
    package->viewer_definitions = g_ptr_array_new_with_free_func (mc_lua_viewer_definition_destroy);
    package->lua = luaL_newstate ();

    if (package->lua == NULL)
    {
        mc_lua_log (package, "error", "could not create a Lua state");
        mc_lua_package_destroy (package);
        return FALSE;
    }

    luaL_openlibs (package->lua);
    mc_lua_install_api (package);

    status = luaL_loadfile (package->lua, package->entry_path);
    if (status == LUA_OK)
        status = lua_pcall (package->lua, 0, 0, 0);
    if (status != LUA_OK)
    {
        mc_lua_report_error (package, MC_RUNTIME_ERROR_PHASE_STARTUP, "Lua package startup failed");
        mc_lua_package_destroy (package);
        return FALSE;
    }

    g_ptr_array_add (runtime->packages, package);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_load_disabled_package_ids (mc_lua_runtime_t *runtime)
{
    char *path;
    GKeyFile *ini;
    gchar **keys;
    gsize key_count = 0;
    gsize i;

    if (runtime == NULL || runtime->disabled_package_ids == NULL)
        return;

    path = g_build_filename (g_get_user_config_dir (), "mc", "plugins.ini", (char *) NULL);
    ini = g_key_file_new ();
    if (!g_key_file_load_from_file (ini, path, G_KEY_FILE_NONE, NULL))
    {
        g_key_file_free (ini);
        g_free (path);
        return;
    }

    keys = g_key_file_get_keys (ini, "DisabledPlugins", &key_count, NULL);
    for (i = 0; keys != NULL && i < key_count; i++)
    {
        const char *id;

        if (!g_str_has_prefix (keys[i], "lua/"))
            continue;
        id = keys[i] + strlen ("lua/");
        if (mc_lua_id_is_valid (id))
            g_hash_table_add (runtime->disabled_package_ids, g_strdup (id));
    }

    g_strfreev (keys);
    g_key_file_free (ini);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_config_enabled (mc_lua_runtime_t *runtime)
{
    char *ini_path;
    GKeyFile *ini;
    GError *error = NULL;
    gboolean enabled = TRUE;
    char *user_scripts_dir = NULL;

    runtime->user_scripts_dir =
        g_build_filename (g_get_user_data_dir (), "mc", "lua", "scripts", (char *) NULL);
    runtime->user_modules_dir =
        g_build_filename (g_get_user_data_dir (), "mc", "lua", "lib", (char *) NULL);
    runtime->legacy_user_scripts_dir =
        g_build_filename (g_get_user_config_dir (), "mc", "lua", "scripts", (char *) NULL);
    runtime->legacy_user_modules_dir =
        g_build_filename (g_get_user_config_dir (), "mc", "lua", "lib", (char *) NULL);
    if (g_strcmp0 (g_getenv ("MC_NO_LUA"), "1") == 0)
        return FALSE;

    ini_path = g_build_filename (g_get_user_config_dir (), "mc", "ini", (char *) NULL);
    ini = g_key_file_new ();
    if (!g_key_file_load_from_file (ini, ini_path, G_KEY_FILE_NONE, &error))
    {
        g_clear_error (&error);
        g_key_file_free (ini);
        g_free (ini_path);
        return TRUE;
    }

    if (g_key_file_has_key (ini, "Lua", "enabled", NULL))
    {
        enabled = g_key_file_get_boolean (ini, "Lua", "enabled", &error);
        if (error != NULL)
        {
            g_clear_error (&error);
            enabled = TRUE;
        }
    }

    user_scripts_dir = g_key_file_get_string (ini, "Lua", "user_scripts_dir", NULL);
    if (user_scripts_dir != NULL && user_scripts_dir[0] != '\0')
    {
        if (g_path_is_absolute (user_scripts_dir))
        {
            g_free (runtime->user_scripts_dir);
            runtime->user_scripts_dir = g_steal_pointer (&user_scripts_dir);
            runtime->user_scripts_dir_overridden = TRUE;
        }
        else
            fprintf (stderr, "Lua scripts: ignoring relative user_scripts_dir\n");
    }

    g_free (user_scripts_dir);
    g_key_file_free (ini);
    g_free (ini_path);
    return enabled;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_load_packages (mc_lua_runtime_t *runtime)
{
    GHashTable *candidates;
    GList *values;
    GList *link;

    candidates = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify) mc_lua_package_candidate_destroy);
    mc_lua_discover_script_root (mc_lua_system_scripts_dir (), MC_LUA_PACKAGE_SYSTEM, candidates);
    if (!runtime->user_scripts_dir_overridden)
        mc_lua_discover_script_root (runtime->legacy_user_scripts_dir, MC_LUA_PACKAGE_USER,
                                     candidates);
    mc_lua_discover_script_root (runtime->user_scripts_dir, MC_LUA_PACKAGE_USER, candidates);

    values = g_hash_table_get_values (candidates);
    values = g_list_sort (values, mc_lua_package_candidate_compare);
    for (link = values; link != NULL; link = g_list_next (link))
    {
        const mc_lua_package_candidate_t *candidate =
            (const mc_lua_package_candidate_t *) link->data;
        mc_lua_package_info_t *info = g_new0 (mc_lua_package_info_t, 1);

        info->id = g_strdup (candidate->id);
        info->name = g_strdup (candidate->name != NULL ? candidate->name : candidate->id);
        info->workspace = g_strdup (candidate->workspace != NULL ? candidate->workspace : "mc");
        info->root = g_strdup (candidate->root);
        info->origin = candidate->origin;
        info->provides = candidate->provides;
        info->disabled = g_hash_table_contains (runtime->disabled_package_ids, candidate->id);
        g_ptr_array_add (runtime->catalog, info);

        if (!info->disabled)
            (void) mc_lua_package_load (runtime, candidate);
    }

    g_list_free (values);
    g_hash_table_destroy (candidates);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_enumerate_packages (mc_runtime_plugin_context_t *context,
                                   mc_runtime_package_callback_t callback, gpointer user_data)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || callback == NULL
        || runtime->catalog == NULL)
        return;

    for (i = 0; i < runtime->catalog->len; i++)
    {
        const mc_lua_package_info_t *info =
            (const mc_lua_package_info_t *) g_ptr_array_index (runtime->catalog, i);

        callback (info->id, info->name, !info->disabled, user_data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_enumerate_package_details (mc_runtime_plugin_context_t *context,
                                          mc_runtime_package_details_callback_t callback,
                                          gpointer user_data)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || callback == NULL
        || runtime->catalog == NULL)
        return;

    for (i = 0; i < runtime->catalog->len; i++)
    {
        const mc_lua_package_info_t *info =
            (const mc_lua_package_info_t *) g_ptr_array_index (runtime->catalog, i);

        callback (info->id, info->name, info->workspace, mc_lua_catalog_origin_name (info->origin),
                  info->root, !info->disabled, user_data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_enumerate_actions (mc_runtime_plugin_context_t *context, const char *workspace,
                                  mc_runtime_action_callback_t callback, gpointer user_data)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || runtime->stopping || callback == NULL
        || g_strcmp0 (workspace, "mcedit") != 0)
        return;

    for (i = 0; i < runtime->macros->len; i++)
    {
        const mc_lua_macro_t *macro =
            (const mc_lua_macro_t *) g_ptr_array_index (runtime->macros, i);
        char *id;

        if (macro == NULL || macro->disabled || !macro->listed || macro->package == NULL
            || macro->package->closed || g_strcmp0 (macro->area, "editor") != 0)
            continue;

        id = g_strdup_printf ("%s:%s", macro->package->id, macro->id);
        callback (id, macro->description, macro->display_key, user_data);
        g_free (id);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_enumerate_menu_actions (mc_runtime_plugin_context_t *context, const char *workspace,
                                       mc_runtime_menu_action_callback_t callback,
                                       gpointer user_data)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || runtime->stopping || callback == NULL
        || g_strcmp0 (workspace, "mcedit") != 0)
        return;

    for (i = 0; i < runtime->macros->len; i++)
    {
        const mc_lua_macro_t *macro =
            (const mc_lua_macro_t *) g_ptr_array_index (runtime->macros, i);
        char *id;

        if (macro == NULL || macro->disabled || macro->menu_path == NULL
            || macro->menu_label == NULL || macro->package == NULL || macro->package->closed
            || g_strcmp0 (macro->area, "editor") != 0)
            continue;

        id = g_strdup_printf ("%s:%s", macro->package->id, macro->id);
        callback (id, macro->menu_path, macro->menu_label, macro->display_key, macro->menu_position,
                  user_data);
        g_free (id);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_runtime_invoke_action (mc_runtime_plugin_context_t *context, const char *workspace,
                              const char *action_id, const char **error)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    mc_lua_macro_t *selected = NULL;
    mc_runtime_event_snapshot_t snapshot = { 0 };
    guint previous_errors;
    guint i;

    if (error != NULL)
        *error = NULL;
    if (runtime == NULL || runtime->context != context || runtime->stopping
        || g_strcmp0 (workspace, "mcedit") != 0 || action_id == NULL)
    {
        if (error != NULL)
            *error = "invalid_context";
        return FALSE;
    }

    for (i = 0; i < runtime->macros->len; i++)
    {
        mc_lua_macro_t *macro = (mc_lua_macro_t *) g_ptr_array_index (runtime->macros, i);
        char *id;
        gboolean matches;

        if (macro == NULL || macro->package == NULL)
            continue;
        id = g_strdup_printf ("%s:%s", macro->package->id, macro->id);
        matches = g_strcmp0 (id, action_id) == 0;
        g_free (id);
        if (matches)
        {
            selected = macro;
            break;
        }
    }
    if (selected == NULL || selected->disabled || selected->package->closed)
    {
        if (error != NULL)
            *error = "action_not_found";
        return FALSE;
    }

    snapshot.event_id = MC_RUNTIME_EVENT_EDITOR_KEY;
    snapshot.data.editor_key.editor = runtime->host->editor_current (context);
    if (snapshot.data.editor_key.editor.kind != MC_RUNTIME_HANDLE_EDITOR
        || snapshot.data.editor_key.editor.id == 0
        || snapshot.data.editor_key.editor.generation == 0)
    {
        if (error != NULL)
            *error = "no_active_editor";
        return FALSE;
    }
    snapshot.data.editor_key.key.name = selected->display_key;
    previous_errors = selected->errors;
    (void) mc_lua_invoke_macro (selected, &snapshot);
    if (selected->errors != previous_errors)
    {
        if (error != NULL)
            *error = "callback_failed";
        return FALSE;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_lua_runtime_init (const mc_runtime_host_api_v1_t *host, mc_runtime_plugin_context_t *context,
                     GError **error)
{
    mc_lua_runtime_t *runtime;
    GError *subscribe_error = NULL;

    if (mc_lua_runtime_current != NULL)
    {
        g_set_error (error, g_quark_from_static_string ("mc-lua"), 0,
                     "The Lua runtime is already initialized");
        return FALSE;
    }

    runtime = g_new0 (mc_lua_runtime_t, 1);
    runtime->host = host;
    runtime->context = context;
    runtime->packages = g_ptr_array_new_with_free_func ((GDestroyNotify) mc_lua_package_destroy);
    runtime->catalog =
        g_ptr_array_new_with_free_func ((GDestroyNotify) mc_lua_package_info_destroy);
    runtime->macros = g_ptr_array_new ();
    runtime->panel_providers = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
    runtime->viewer_controllers = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
    runtime->disabled_package_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    runtime->macro_subscription =
        host->subscribe (context, MC_RUNTIME_EVENT_EDITOR_KEY, 50, mc_lua_macro_event_callback,
                         runtime, NULL, &subscribe_error);
    if (runtime->macro_subscription == 0)
    {
        if (subscribe_error != NULL)
        {
            if (error != NULL)
                g_propagate_error (error, subscribe_error);
            else
                g_error_free (subscribe_error);
        }
        else
            g_set_error (error, g_quark_from_static_string ("mc-lua"), 0,
                         "Could not subscribe Lua macro dispatcher");
        g_ptr_array_free (runtime->packages, TRUE);
        g_ptr_array_free (runtime->catalog, TRUE);
        g_ptr_array_free (runtime->macros, TRUE);
        g_hash_table_destroy (runtime->panel_providers);
        g_hash_table_destroy (runtime->viewer_controllers);
        g_hash_table_destroy (runtime->disabled_package_ids);
        g_free (runtime);
        return FALSE;
    }

    mc_lua_runtime_current = runtime;
    host->context_set_data (context, runtime, mc_lua_runtime_destroy);

    mc_lua_load_disabled_package_ids (runtime);
    if (mc_lua_config_enabled (runtime))
        mc_lua_runtime_load_packages (runtime);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_runtime_shutdown (mc_runtime_plugin_context_t *context)
{
    mc_lua_runtime_t *runtime = mc_lua_runtime_current;
    guint i;

    if (runtime == NULL || runtime->context != context || runtime->stopping)
        return;

    runtime->stopping = TRUE;
    runtime->host->unsubscribe_all (context);

    for (i = 0; i < runtime->packages->len; i++)
        mc_lua_package_close ((mc_lua_package_t *) g_ptr_array_index (runtime->packages, i));
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

G_MODULE_EXPORT const mc_runtime_plugin_descriptor_v1_t *
mc_runtime_plugin_register_v1 (void)
{
    static const mc_runtime_plugin_descriptor_v1_t descriptor = {
        .abi_version = MC_RUNTIME_PLUGIN_ABI_VERSION,
        .struct_size = sizeof (mc_runtime_plugin_descriptor_v1_t),
        .capability_flags = 0,
        .runtime_name = "lua",
        .required_host_capabilities = MC_RUNTIME_HOST_CAP_EVENTS | MC_RUNTIME_HOST_CAP_CONTEXT_DATA,
        .init = mc_lua_runtime_init,
        .shutdown = mc_lua_runtime_shutdown,
        .enumerate_packages = mc_lua_runtime_enumerate_packages,
        .enumerate_package_details = mc_lua_runtime_enumerate_package_details,
        .enumerate_actions = mc_lua_runtime_enumerate_actions,
        .invoke_action = mc_lua_runtime_invoke_action,
        .enumerate_menu_actions = mc_lua_runtime_enumerate_menu_actions,
        .display_name = "Lua engine",
    };

    return &descriptor;
}

/* --------------------------------------------------------------------------------------------- */
