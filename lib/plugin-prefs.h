/** \file plugin-prefs.h
 *  \brief Header: shared preference helpers for editor and panel plugins.
 *
 * Enable/disable state lives in ~/.config/mc/plugins.ini under section
 * [DisabledPlugins], one key per disabled plugin in the form "<kind>/<name>"
 * (value is ignored, presence == disabled).  Kind disambiguates editor and
 * panel plugins so they can share names without colliding.  Changes are
 * persisted immediately on each set call.  Also provides a common hotkey
 * config parser so plugins resolve key bindings identically.
 */

#ifndef MC__PLUGIN_PREFS_H
#define MC__PLUGIN_PREFS_H

#include "lib/global.h"

typedef enum
{
    MC_PLUGIN_KIND_EDITOR,
    MC_PLUGIN_KIND_PANEL,
} mc_plugin_kind_t;

gboolean mc_plugin_prefs_is_disabled (mc_plugin_kind_t kind, const char *plugin_name);
void mc_plugin_prefs_set_disabled (mc_plugin_kind_t kind, const char *plugin_name,
                                   gboolean disabled);

/* Return a NULL-terminated array of plugin names disabled under the given
 * kind.  Caller must free with g_strfreev(). */
gchar **mc_plugin_prefs_list_disabled (mc_plugin_kind_t kind);

/* Resolve a plugin hotkey config value to a key code, shared so all plugins
 * behave identically.  "none" disables (returns 0); an empty or unrecognized
 * value falls back to fallback_text, then fallback_key.  The result is passed
 * through tty_normalize_keycode() so "shift-f1" matches the KEY_F(11) that
 * real key events carry.  When label != NULL, *label receives a freshly
 * allocated display string (e.g. "Shift-F1"), or NULL; caller frees it. */
int mc_plugin_prefs_parse_hotkey (const char *value, const char *fallback_text, int fallback_key,
                                  char **label);

/* Read one stripped, non-empty string from [group]/key in the ini file at
 * @path.  Returns NULL if the file is absent or the value is unset/empty.
 * Caller frees. */
char *mc_plugin_prefs_read_config_string (const char *path, const char *group, const char *key);

/* Resolve a plugin hotkey from config file @basename (looked up first in the
 * user config dir, then the system config dir), section @group, entry @key,
 * via mc_plugin_prefs_parse_hotkey().  @label as in that function. */
int mc_plugin_prefs_load_hotkey (const char *basename, const char *group, const char *key,
                                 const char *fallback_text, int fallback_key, char **label);

#endif /* MC__PLUGIN_PREFS_H */
