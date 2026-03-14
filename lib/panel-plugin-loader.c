/*
   Dynamic panel plugin loader.

   Copyright (C) 2025
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

/** \file panel-plugin-loader.c
 *  \brief Source: dynamic panel plugin loader
 *
 *  Scans MC_PANEL_PLUGINS_DIR for shared objects exporting
 *  mc_panel_plugin_register(), loads them, and registers the returned
 *  mc_panel_plugin_t descriptor via mc_panel_plugin_add().
 */

#include <config.h>

#include <stdio.h>

#include "lib/global.h"
#include "lib/editor-plugin.h"
#include "lib/panel-plugin.h"

#ifdef HAVE_GMODULE

#include <gmodule.h>

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

static GPtrArray *panel_plugin_modules = NULL;
static gboolean editor_plugins_loaded = FALSE;
static GPtrArray *editor_plugin_modules = NULL;

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
module_filename_has_native_suffix (const gchar *filename)
{
    if (filename == NULL)
        return FALSE;

    return g_str_has_suffix (filename, ".so") || g_str_has_suffix (filename, ".dylib")
        || g_str_has_suffix (filename, ".bundle") || g_str_has_suffix (filename, ".dll");
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
mc_panel_plugins_load_from_dir (const gchar *plugins_dir)
{
    GDir *dir;
    const gchar *filename;

    dir = g_dir_open (plugins_dir, 0, NULL);
    if (dir == NULL)
        return;

    while ((filename = g_dir_read_name (dir)) != NULL)
    {
        GModule *module;
        mc_panel_plugin_register_fn register_fn;
        const mc_panel_plugin_t *plugin;
        gchar *path;

        if (!module_filename_has_native_suffix (filename))
            continue;

        path = g_build_filename (plugins_dir, filename, (char *) NULL);
        module = g_module_open (path, G_MODULE_BIND_LAZY);
        if (module == NULL)
        {
            fprintf (stderr, "Panel plugin %s: %s\n", filename, g_module_error ());
            g_free (path);
            continue;
        }

        if (!g_module_symbol (module, MC_PANEL_PLUGIN_ENTRY, (gpointer *) &register_fn))
        {
            fprintf (stderr, "Panel plugin %s: symbol %s not found\n", filename,
                     MC_PANEL_PLUGIN_ENTRY);
            g_module_close (module);
            g_free (path);
            continue;
        }

        plugin = register_fn ();
        if (plugin == NULL || !mc_panel_plugin_add (plugin))
        {
            /* duplicate name from user dir overriding system dir is expected, stay silent */
            g_module_close (module);
            g_free (path);
            continue;
        }

        g_module_make_resident (module); /* prevent unload - plugin descriptor lives in .so */
        g_ptr_array_add (panel_plugin_modules, module);
        g_free (path);
    }

    g_dir_close (dir);
}

/* --------------------------------------------------------------------------------------------- */

void
mc_panel_plugins_load (void)
{
    gchar *system_dir;
    gchar *user_dir;

    panel_plugin_modules = g_ptr_array_new ();

    /* load from system plugin directory */
    system_dir = g_build_filename (MC_PANEL_PLUGINS_DIR, (char *) NULL);
    mc_panel_plugins_load_from_dir (system_dir);
    g_free (system_dir);

    /* load from user plugin directory (~/.local/lib/mc/panel-plugins) */
    user_dir =
        g_build_filename (g_get_home_dir (), ".local", "lib", "mc", "panel-plugins", (char *) NULL);
    mc_panel_plugins_load_from_dir (user_dir);
    g_free (user_dir);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_editor_plugins_load_from_dir (const gchar *plugins_dir)
{
    GDir *dir;
    const gchar *filename;

    dir = g_dir_open (plugins_dir, 0, NULL);
    if (dir == NULL)
        return;

    while ((filename = g_dir_read_name (dir)) != NULL)
    {
        GModule *module;
        mc_editor_plugin_register_fn register_fn;
        const mc_editor_plugin_t *plugin;
        gchar *path;

        if (!module_filename_has_native_suffix (filename))
            continue;

        path = g_build_filename (plugins_dir, filename, (char *) NULL);
        module = g_module_open (path, G_MODULE_BIND_LAZY);
        if (module == NULL)
        {
            fprintf (stderr, "Editor plugin %s: %s\n", filename, g_module_error ());
            g_free (path);
            continue;
        }

        if (!g_module_symbol (module, MC_EDITOR_PLUGIN_ENTRY, (gpointer *) &register_fn))
        {
            fprintf (stderr, "Editor plugin %s: symbol %s not found\n", filename,
                     MC_EDITOR_PLUGIN_ENTRY);
            g_module_close (module);
            g_free (path);
            continue;
        }

        plugin = register_fn ();
        if (plugin == NULL || !mc_editor_plugin_add (plugin))
        {
            g_module_close (module);
            g_free (path);
            continue;
        }

        g_module_make_resident (module);
        g_ptr_array_add (editor_plugin_modules, module);
        g_free (path);
    }

    g_dir_close (dir);
}

/* --------------------------------------------------------------------------------------------- */

void
mc_editor_plugins_load (void)
{
    gchar *system_dir;
    gchar *user_dir;

    if (editor_plugins_loaded)
        return;

    editor_plugins_loaded = TRUE;
    editor_plugin_modules = g_ptr_array_new ();

    /* load from system plugin directory */
    system_dir = g_build_filename (MC_EDITOR_PLUGINS_DIR, (char *) NULL);
    mc_editor_plugins_load_from_dir (system_dir);
    g_free (system_dir);

    /* load from user plugin directory (~/.local/lib/mc/editor-plugins) */
    user_dir = g_build_filename (g_get_home_dir (), ".local", "lib", "mc", "editor-plugins",
                                 (char *) NULL);
    mc_editor_plugins_load_from_dir (user_dir);
    g_free (user_dir);
}

/* --------------------------------------------------------------------------------------------- */

#else /* !HAVE_GMODULE */

void
mc_panel_plugins_load (void)
{
    // GModule not available - dynamic panel plugins disabled
}

/* --------------------------------------------------------------------------------------------- */

void
mc_editor_plugins_load (void)
{
    /* GModule not available - dynamic editor plugins disabled */
}

/* --------------------------------------------------------------------------------------------- */

#endif /* HAVE_GMODULE */
