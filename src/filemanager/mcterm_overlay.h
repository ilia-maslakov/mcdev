/** \file mcterm_overlay.h
 *  \brief Header: file manager mcterm overlay controller
 */

#ifndef MC__MCTERM_OVERLAY_H
#define MC__MCTERM_OVERLAY_H

#include "lib/widget.h"

/*** typedefs(not structures) and defined constants **********************************************/

typedef cb_ret_t (*mcterm_overlay_command_cb_t) (long command, void *data);
typedef cb_ret_t (*mcterm_overlay_enter_cb_t) (void *data);

typedef enum
{
    MCTERM_OVERLAY_CMDLINE_NOT_APPLICABLE,
    MCTERM_OVERLAY_CMDLINE_HANDLED,
    MCTERM_OVERLAY_CMDLINE_SENT
} mcterm_overlay_cmdline_result_t;

/*** declarations of public functions ************************************************************/

gboolean mcterm_overlay_active (void);
void mcterm_overlay_toggle (void);
void mcterm_overlay_destroy (void);

void mcterm_overlay_draw_visible_panels (void);
void mcterm_overlay_after_filemanager_draw (void);
void mcterm_overlay_resize (const WRect *r);

gboolean mcterm_overlay_complete_or_cycle_focus (void);
cb_ret_t mcterm_overlay_send_enter_if_cmdline_empty (void);
gboolean mcterm_overlay_show_panel_if_hidden (int idx);
gboolean mcterm_overlay_toggle_panel_command (gboolean right_panel_command);

mcterm_overlay_cmdline_result_t mcterm_overlay_run_cmdline (const char *cmd, gboolean is_cd,
                                                            gboolean is_exit);
gboolean mcterm_overlay_panel_exec (const char *cmd);

cb_ret_t mcterm_overlay_handle_key (Widget *w, int parm,
                                    mcterm_overlay_command_cb_t execute_command,
                                    mcterm_overlay_enter_cb_t execute_cmdline_enter, void *data);

/*** inline functions ****************************************************************************/

#endif
