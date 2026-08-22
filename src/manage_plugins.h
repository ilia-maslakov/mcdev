/** \file manage_plugins.h
 *  \brief Header: Manage Plugins dialog (enable/disable individual plugins).
 */

#ifndef MC__MANAGE_PLUGINS_H
#define MC__MANAGE_PLUGINS_H

void manage_plugins_dialog (void);

#ifdef ENABLE_LUA_PLUGIN
gboolean manage_lua_editor_scripts_dialog (void);
#endif

#endif /* MC__MANAGE_PLUGINS_H */
