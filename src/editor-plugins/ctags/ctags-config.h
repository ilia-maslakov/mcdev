/** \file ctags-config.h
 *  \brief Header: ctags plugin configuration
 */

#ifndef MC__CTAGS_CONFIG_H
#define MC__CTAGS_CONFIG_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct
{
    char *ctags_cmd;         /* ctags executable, default "ctags" */
    char *ctags_args;        /* extra args for repository indexing */
    gboolean case_sensitive; /* symbol search case sensitivity */
    gboolean auto_discover;  /* auto-discover tags file in parent dirs */
} ctags_config_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

void ctags_config_init (ctags_config_t *cfg);
void ctags_config_free (ctags_config_t *cfg);
void ctags_config_load (ctags_config_t *cfg);
void ctags_config_save (const ctags_config_t *cfg);

/* Show configuration dialog.  Returns TRUE if user accepted changes. */
gboolean ctags_config_dialog (ctags_config_t *cfg);

/*** inline functions ****************************************************************************/

#endif /* MC__CTAGS_CONFIG_H */
