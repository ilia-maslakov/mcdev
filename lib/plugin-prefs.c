/*
   Per-user enable/disable state for editor and panel plugins.

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

#include "lib/global.h"
#include "lib/mcconfig.h"

#include "plugin-prefs.h"

/*** file scope macro definitions ****************************************************************/

#define PLUGINS_PREFS_FILE "plugins.ini"
#define DISABLED_GROUP     "DisabledPlugins"

/*** file scope variables ************************************************************************/

static mc_config_t *prefs_config = NULL;

/*** file scope functions ************************************************************************/

static char *
prefs_path (void)
{
    const char *base;

    base = mc_config_get_path ();
    if (base == NULL)
        return NULL;
    return g_build_filename (base, PLUGINS_PREFS_FILE, (char *) NULL);
}

static mc_config_t *
prefs_get (void)
{
    /* Plugins may be registered before VFS/config runtime is initialised
       (e.g. in unit tests that don't set up VFS).  mc_config_init()
       depends on that runtime, and mc_global.main_config is set in
       setup.c right after vfs_init(). */
    if (mc_global.main_config == NULL)
        return NULL;

    if (prefs_config == NULL)
    {
        char *path = prefs_path ();
        if (path == NULL)
            return NULL;
        prefs_config = mc_config_init (path, FALSE);
        g_free (path);
    }
    return prefs_config;
}

static const char *
kind_prefix (mc_plugin_kind_t kind)
{
    switch (kind)
    {
    case MC_PLUGIN_KIND_EDITOR:
        return "editor/";
    case MC_PLUGIN_KIND_PANEL:
        return "panel/";
    default:
        return "?/";
    }
}

static char *
kind_key (mc_plugin_kind_t kind, const char *plugin_name)
{
    return g_strconcat (kind_prefix (kind), plugin_name, (char *) NULL);
}

/*** public functions ****************************************************************************/

gboolean
mc_plugin_prefs_is_disabled (mc_plugin_kind_t kind, const char *plugin_name)
{
    mc_config_t *cfg;
    char *key;
    gboolean result;

    if (plugin_name == NULL || *plugin_name == '\0')
        return FALSE;

    cfg = prefs_get ();
    if (cfg == NULL)
        return FALSE;

    key = kind_key (kind, plugin_name);
    result = mc_config_has_param (cfg, DISABLED_GROUP, key);
    g_free (key);
    return result;
}

gchar **
mc_plugin_prefs_list_disabled (mc_plugin_kind_t kind)
{
    mc_config_t *cfg;
    gchar **all;
    GPtrArray *out;
    const char *prefix;
    gsize prefix_len;
    gsize i, len = 0;

    cfg = prefs_get ();
    if (cfg == NULL)
        return NULL;

    all = mc_config_get_keys (cfg, DISABLED_GROUP, &len);
    prefix = kind_prefix (kind);
    prefix_len = strlen (prefix);

    out = g_ptr_array_new ();
    for (i = 0; i < len; i++)
        if (strncmp (all[i], prefix, prefix_len) == 0)
            g_ptr_array_add (out, g_strdup (all[i] + prefix_len));
    g_ptr_array_add (out, NULL);
    g_strfreev (all);

    return (gchar **) g_ptr_array_free (out, FALSE);
}

void
mc_plugin_prefs_set_disabled (mc_plugin_kind_t kind, const char *plugin_name, gboolean disabled)
{
    mc_config_t *cfg;
    char *path, *key;

    if (plugin_name == NULL || *plugin_name == '\0')
        return;

    cfg = prefs_get ();
    if (cfg == NULL)
        return;

    key = kind_key (kind, plugin_name);
    if (disabled)
        mc_config_set_string (cfg, DISABLED_GROUP, key, "true");
    else
        mc_config_del_key (cfg, DISABLED_GROUP, key);
    g_free (key);

    path = prefs_path ();
    if (path != NULL)
    {
        mc_config_save_to_file (cfg, path, NULL);
        g_free (path);
    }
}
