/** \file panelize_dlg.h
 *  \brief Header: Panelize preset selection dialog.
 */

#ifndef MC__PANELIZE_DLG_H
#define MC__PANELIZE_DLG_H

#include "lib/global.h"

#include "panelize_config.h"

typedef enum
{
    PANELIZE_DLG_CANCEL,
    PANELIZE_DLG_PANELIZE,
    PANELIZE_DLG_ADD,
    PANELIZE_DLG_REMOVE,
    PANELIZE_DLG_EDIT,
} panelize_dlg_result_t;

/* Show the preset dialog.
   @presets        : current preset list (read-only here; caller mutates on
                     ADD/REMOVE/EDIT result and re-invokes)
   @initial_index  : preset to highlight initially (-1 for none)
   @out_index      : selected listbox row, -1 if input was used freely
   @out_command    : strdup'd command from input field (caller frees);
                     populated on PANELIZE result, NULL otherwise.
*/
panelize_dlg_result_t panelize_dlg_run (GPtrArray *presets, int initial_index, int *out_index,
                                        char **out_command);

#endif
