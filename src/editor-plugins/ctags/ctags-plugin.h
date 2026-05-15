/** \file ctags-plugin.h
 *  \brief Header: ctags editor plugin entry point
 */

#ifndef MC__CTAGS_PLUGIN_H
#define MC__CTAGS_PLUGIN_H

#include "lib/editor-plugin.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Returns the plugin descriptor for registration. */
const mc_editor_plugin_t *ctags_get_plugin (void);

/*** inline functions ****************************************************************************/

#endif /* MC__CTAGS_PLUGIN_H */
