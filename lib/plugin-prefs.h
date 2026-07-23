/** \file plugin-prefs.h
 *  \brief Header: per-user enable/disable state for editor and panel plugins.
 *
 * State lives in ~/.config/mc/plugins.ini under section [DisabledPlugins], one
 * key per disabled plugin in the form "<kind>/<name>" (value is ignored,
 * presence == disabled).  Kind disambiguates editor and panel plugins so they
 * can share names without colliding.  Changes are persisted immediately on
 * each set call.
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

/* Resolve a plugin hotkey config value to a key code, shared by all panel
 * plugins so they behave identically:
 *   - NULL/empty          -> fallback_text is tried, else fallback_key
 *   - "none"              -> 0 (disabled)
 *   - a known key name    -> tty_normalize_keycode (tty_keyname_to_keycode ())
 *                            so "shift-f1" folds to KEY_F(11) like real events
 *   - an unknown key name -> fallback_text is tried, else fallback_key
 * When label != NULL, *label receives a freshly allocated mc-native display
 * string (e.g. "Shift-F1", "Ctrl-a") on success, or NULL.  Caller frees it. */
int mc_plugin_prefs_parse_hotkey (const char *value, const char *fallback_text, int fallback_key,
                                  char **label);

#endif /* MC__PLUGIN_PREFS_H */
