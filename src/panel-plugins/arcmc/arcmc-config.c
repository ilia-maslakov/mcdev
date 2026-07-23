/*
   Archive browser panel plugin -config persistence.

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

#include <config.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/tty/key.h"       // KEY_F()
#include "lib/plugin-prefs.h"  // mc_plugin_prefs_parse_hotkey()

#include "arcmc-types.h"
#include "arcmc-config.h"

/*** file scope variables ************************************************************************/

/* Config key names for builtin formats (must match builtin_formats[] order in dialog-settings.c) */
static const char *const builtin_keys[ARCMC_BUILTIN_COUNT] = {
    "zip",     "7z",     "tar.gz",   "tar.bz2", "tar.xz", "tar", "cpio",
    "tar.zst", "tar.lz", "tar.lzma", "iso",     "xar",    "cab",
};

#define ARCMC_CONFIG_FILE       "arcmc.ini"
#define ARCMC_SECTION_BUILTIN   "arcmc-builtin"
#define ARCMC_SECTION_EXT       "arcmc-ext"
#define ARCMC_SECTION_EXT_PARAM "arcmc-ext-params-"
#define ARCMC_SECTION_KEYS      "arcmc"

/* Config key + default for the "Create archive" hotkey. Names are resolved
   with tty_keyname_to_keycode() just like mc keymaps ("ctrl-a", "alt-x",
   "shift-f1", ...); "none" disables the global hotkey. */
#define ARCMC_KEY_CREATE         "hotkey_create"
#define ARCMC_KEY_CREATE_DEFAULT "shift-f1"

/*** file scope functions ************************************************************************/

/* Load a string config key and replace the field in the archiver struct.
   The old value (a .rodata literal) is not freed - intentional, it's program-lifetime. */
static void
load_ext_param_str (mc_config_t *cfg, const char *section, const char *key, const char **field)
{
    char *val;

    val = mc_config_get_string (cfg, section, key, NULL);
    if (val == NULL)
        return;

    if (val[0] == '\0')
    {
        g_free (val);
        *field = NULL;
        return;
    }

    *field = val;
}

/* Save a string config key if the field is non-NULL. */
static void
save_ext_param_str (mc_config_t *cfg, const char *section, const char *key, const char *value)
{
    if (value != NULL)
        mc_config_set_string (cfg, section, key, value);
    else
        mc_config_set_string (cfg, section, key, "");
}

/*** global variables ****************************************************************************/

gboolean arcmc_builtin_enabled[ARCMC_BUILTIN_COUNT];
gboolean *arcmc_ext_enabled = NULL;

int arcmc_hotkey_create = 0;
const char *arcmc_hotkey_create_label = NULL;

/* Raw config text for the hotkey (e.g. "shift-f1", "none"), kept so that
   arcmc_config_save() writes it back verbatim instead of a lossy keyname
   derived from the normalized key code. */
static char *arcmc_hotkey_create_text = NULL;

/*** public functions ****************************************************************************/

void
arcmc_config_load (void)
{
    mc_config_t *cfg;
    char *cfg_path;
    size_t i;

    /* default: all enabled */
    for (i = 0; i < ARCMC_BUILTIN_COUNT; i++)
        arcmc_builtin_enabled[i] = TRUE;

    g_free (arcmc_ext_enabled);
    arcmc_ext_enabled = g_new (gboolean, ext_archivers_count);
    for (i = 0; i < ext_archivers_count; i++)
        arcmc_ext_enabled[i] = TRUE;

    cfg_path = g_build_filename (mc_config_get_path (), ARCMC_CONFIG_FILE, (char *) NULL);
    cfg = mc_config_init (cfg_path, TRUE);
    g_free (cfg_path);

    {
        char *value;
        char *label = NULL;

        if (cfg != NULL)
            value = mc_config_get_string (cfg, ARCMC_SECTION_KEYS, ARCMC_KEY_CREATE,
                                          ARCMC_KEY_CREATE_DEFAULT);
        else
            value = g_strdup (ARCMC_KEY_CREATE_DEFAULT);

        g_free (arcmc_hotkey_create_text);
        arcmc_hotkey_create_text = g_strdup (value);
        arcmc_hotkey_create =
            mc_plugin_prefs_parse_hotkey (value, ARCMC_KEY_CREATE_DEFAULT, KEY_F (11), &label);
        g_free ((char *) arcmc_hotkey_create_label);
        arcmc_hotkey_create_label = label;
        g_free (value);
    }

    if (cfg == NULL)
        return;

    for (i = 0; i < ARCMC_BUILTIN_COUNT; i++)
        arcmc_builtin_enabled[i] =
            mc_config_get_bool (cfg, ARCMC_SECTION_BUILTIN, builtin_keys[i], TRUE);

    for (i = 0; i < ext_archivers_count; i++)
        arcmc_ext_enabled[i] =
            mc_config_get_bool (cfg, ARCMC_SECTION_EXT, ext_archivers[i].name, TRUE);

    /* load per-archiver custom parameters */
    for (i = 0; i < ext_archivers_count; i++)
    {
        char section[64];
        arcmc_ext_archiver_t *a = &ext_archivers[i];

        g_snprintf (section, sizeof (section), "%s%s", ARCMC_SECTION_EXT_PARAM, a->name);

        if (!mc_config_has_group (cfg, section))
            continue;

        load_ext_param_str (cfg, section, "pack_bin", &a->pack_bin);
        load_ext_param_str (cfg, section, "pack_args", &a->pack_args);
        load_ext_param_str (cfg, section, "unpack_bin", &a->unpack_bin);
        load_ext_param_str (cfg, section, "unpack_args", &a->unpack_args);
        load_ext_param_str (cfg, section, "test_bin", &a->test_bin);
        load_ext_param_str (cfg, section, "test_args", &a->test_args);
        load_ext_param_str (cfg, section, "extfs_helper", &a->extfs_helper);
        load_ext_param_str (cfg, section, "list_file_arg", &a->list_file_arg);
    }

    mc_config_deinit (cfg);
}

/* --------------------------------------------------------------------------------------------- */

void
arcmc_config_save (void)
{
    mc_config_t *cfg;
    char *cfg_path;
    size_t i;

    cfg_path = g_build_filename (mc_config_get_path (), ARCMC_CONFIG_FILE, (char *) NULL);
    cfg = mc_config_init (cfg_path, FALSE);

    if (cfg == NULL)
    {
        g_free (cfg_path);
        return;
    }

    mc_config_set_string (cfg, ARCMC_SECTION_KEYS, ARCMC_KEY_CREATE,
                          arcmc_hotkey_create_text != NULL ? arcmc_hotkey_create_text
                                                           : ARCMC_KEY_CREATE_DEFAULT);

    for (i = 0; i < ARCMC_BUILTIN_COUNT; i++)
        mc_config_set_bool (cfg, ARCMC_SECTION_BUILTIN, builtin_keys[i], arcmc_builtin_enabled[i]);

    for (i = 0; i < ext_archivers_count; i++)
        mc_config_set_bool (cfg, ARCMC_SECTION_EXT, ext_archivers[i].name, arcmc_ext_enabled[i]);

    /* save per-archiver custom parameters */
    for (i = 0; i < ext_archivers_count; i++)
    {
        char section[64];
        const arcmc_ext_archiver_t *a = &ext_archivers[i];

        g_snprintf (section, sizeof (section), "%s%s", ARCMC_SECTION_EXT_PARAM, a->name);

        save_ext_param_str (cfg, section, "pack_bin", a->pack_bin);
        save_ext_param_str (cfg, section, "pack_args", a->pack_args);
        save_ext_param_str (cfg, section, "unpack_bin", a->unpack_bin);
        save_ext_param_str (cfg, section, "unpack_args", a->unpack_args);
        save_ext_param_str (cfg, section, "test_bin", a->test_bin);
        save_ext_param_str (cfg, section, "test_args", a->test_args);
        save_ext_param_str (cfg, section, "extfs_helper", a->extfs_helper);
        save_ext_param_str (cfg, section, "list_file_arg", a->list_file_arg);
    }

    mc_config_save_file (cfg, NULL);
    mc_config_deinit (cfg);
    g_free (cfg_path);
}

/* --------------------------------------------------------------------------------------------- */
