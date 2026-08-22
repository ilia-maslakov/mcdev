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
    GHashTable *disabled_package_ids;
    char *user_scripts_dir;
    char *user_modules_dir;
    gboolean mcedit_enabled;
    gboolean stopping;
    gboolean warning_shown;
    mc_runtime_subscription_t macro_subscription;
};

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
    gboolean closed;
    guint callback_depth;
    mc_runtime_event_id_t active_event;
};

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
    char *description;
    int priority;
    int action_ref;
    guint errors;
    gboolean disabled;
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
mc_lua_origin_name (mc_lua_package_origin_t origin)
{
    return origin == MC_LUA_PACKAGE_USER ? "user" : "system";
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

static void
mc_lua_runtime_warn_once (mc_lua_runtime_t *runtime, const char *package_id, const char *event_name)
{
    char *message;

    if (runtime == NULL || runtime->warning_shown)
        return;

    runtime->warning_shown = TRUE;
    message = g_strdup_printf ("Lua script %s: %s failed", package_id != NULL ? package_id : "?",
                               event_name != NULL ? event_name : "callback");
    if (runtime->host != NULL && runtime->host->struct_size >= MC_LUA_HOST_API_UI_SIZE
        && (runtime->host->capability_flags & MC_RUNTIME_HOST_CAP_UI) != 0
        && runtime->host->ui_status != NULL && runtime->host->ui_status (runtime->context, message))
    {
        g_free (message);
        return;
    }

    fprintf (stderr, "Lua scripts: %s\n", message);
    g_free (message);
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
        else if (strcmp (method, "cursor") == 0)
            lua_pushcfunction (lua, mc_lua_editor_cursor);
        else if (strcmp (method, "set_cursor") == 0)
            lua_pushcfunction (lua, mc_lua_editor_set_cursor);
        else if (strcmp (method, "is_readonly") == 0)
            lua_pushcfunction (lua, mc_lua_editor_is_readonly);
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
mc_lua_report_error (mc_lua_package_t *package, const char *event_name)
{
    lua_State *lua = package->lua;
    const char *message = lua_tostring (lua, -1);
    const char *traceback;

    luaL_traceback (lua, lua, message != NULL ? message : "Lua error", 1);
    traceback = lua_tostring (lua, -1);
    mc_lua_log (package, "error", traceback != NULL ? traceback : "Lua error");
    mc_lua_runtime_warn_once (package->runtime, package->id, event_name);
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
    g_free (macro->description);
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

    package->closed = TRUE;
    mc_lua_package_unsubscribe_all (package);

    if (package->macros != NULL)
        g_ptr_array_set_size (package->macros, 0);

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
    if (runtime->disabled_package_ids != NULL)
        g_hash_table_destroy (runtime->disabled_package_ids);
    g_free (runtime->user_scripts_dir);
    g_free (runtime->user_modules_dir);
    g_free (runtime);
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
    const char *key;
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
    key = luaL_checkstring (lua, -1);
    normalized_key = mc_lua_key_name_normalize (key);
    lua_pop (lua, 1);
    if (normalized_key == NULL)
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
    macro->description = g_strdup (description);
    macro->priority = (int) priority;
    macro->action_ref = LUA_NOREF;
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
mc_lua_not_ready (lua_State *lua)
{
    lua_pushnil (lua);
    lua_pushliteral (lua, "not_ready");
    return 2;
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
    g_free (filename);

    if (mc_lua_file_is_trusted_under (package->root, package_module))
        module_path = package_module;
    else if (mc_lua_file_is_trusted_under (package->origin == MC_LUA_PACKAGE_USER
                                               ? package->runtime->user_modules_dir
                                               : mc_lua_system_modules_dir (),
                                           shared_module))
        module_path = shared_module;

    if (module_path == NULL)
    {
        g_free (package_module);
        g_free (shared_module);
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
    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_lua_install_api (mc_lua_package_t *package)
{
    lua_State *lua = package->lua;
    const char *const levels[] = { "debug", "info", "warn", "error" };
    guint i;

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

    lua_createtable (lua, 0, 2);
    lua_pushcfunction (lua, mc_lua_ui_status);
    lua_setfield (lua, -2, "status");
    lua_pushcfunction (lua, mc_lua_ui_message);
    lua_setfield (lua, -2, "message");
    lua_setfield (lua, -2, "ui");

    lua_createtable (lua, 0, 2);
    lua_pushcfunction (lua, mc_lua_panel_active);
    lua_setfield (lua, -2, "active");
    lua_pushcfunction (lua, mc_lua_panel_passive);
    lua_setfield (lua, -2, "passive");
    lua_setfield (lua, -2, "panel");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_editor_current);
    lua_setfield (lua, -2, "current");
    lua_setfield (lua, -2, "editor");

    lua_createtable (lua, 0, 1);
    lua_pushcfunction (lua, mc_lua_viewer_current);
    lua_setfield (lua, -2, "current");
    lua_setfield (lua, -2, "viewer");

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
        mc_lua_report_error (package, mc_lua_event_name (snapshot->event_id));
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
mc_lua_macro_event_callback (gpointer runtime_context, const mc_runtime_event_snapshot_t *snapshot,
                             gpointer user_data)
{
    mc_lua_runtime_t *runtime = (mc_lua_runtime_t *) user_data;
    mc_lua_macro_t *macro;
    mc_lua_package_t *package;
    lua_State *lua;
    mc_runtime_event_id_t previous_event;
    gboolean pass = FALSE;

    if (runtime == NULL || runtime != mc_lua_runtime_current || runtime->context != runtime_context
        || runtime->stopping)
        return MC_RUNTIME_EVENT_PASS;

    macro = mc_lua_find_editor_macro (runtime, snapshot);
    if (macro == NULL)
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
        mc_lua_report_error (package, macro->id);
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
        mc_lua_report_error (package, "startup");
        mc_lua_package_destroy (package);
        return FALSE;
    }

    g_ptr_array_add (runtime->packages, package);
    fprintf (stderr, "Lua script %s (%s) loaded\n", package->id,
             mc_lua_origin_name (package->origin));
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
    gboolean mcedit_enabled = TRUE;
    char *user_scripts_dir = NULL;

    runtime->user_scripts_dir =
        g_build_filename (g_get_user_config_dir (), "mc", "lua", "scripts", (char *) NULL);
    runtime->user_modules_dir =
        g_build_filename (g_get_user_config_dir (), "mc", "lua", "lib", (char *) NULL);
    runtime->mcedit_enabled = TRUE;

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

    if (g_key_file_has_key (ini, "Lua", "mcedit_enabled", NULL))
    {
        mcedit_enabled = g_key_file_get_boolean (ini, "Lua", "mcedit_enabled", &error);
        if (error != NULL)
        {
            g_clear_error (&error);
            mcedit_enabled = TRUE;
        }
    }
    else if (g_key_file_has_key (ini, "Lua", "editor_enabled", NULL))
    {
        /* Keep configurations written by the previous development layout working. */
        mcedit_enabled = g_key_file_get_boolean (ini, "Lua", "editor_enabled", &error);
        if (error != NULL)
        {
            g_clear_error (&error);
            mcedit_enabled = TRUE;
        }
    }
    runtime->mcedit_enabled = mcedit_enabled;

    user_scripts_dir = g_key_file_get_string (ini, "Lua", "user_scripts_dir", NULL);
    if (user_scripts_dir != NULL && user_scripts_dir[0] != '\0')
    {
        if (g_path_is_absolute (user_scripts_dir))
        {
            g_free (runtime->user_scripts_dir);
            runtime->user_scripts_dir = g_steal_pointer (&user_scripts_dir);
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
        info->disabled = g_hash_table_contains (runtime->disabled_package_ids, candidate->id)
            || (g_strcmp0 (info->workspace, "mcedit") == 0 && !runtime->mcedit_enabled);
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
    };

    return &descriptor;
}

/* --------------------------------------------------------------------------------------------- */
