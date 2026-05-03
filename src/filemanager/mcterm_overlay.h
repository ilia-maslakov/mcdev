/** \file mcterm_overlay.h
 *  \brief Header: mcterm overlay helpers for filemanager panels
 */

#ifndef MC__MCTERM_OVERLAY_H
#define MC__MCTERM_OVERLAY_H

#include "lib/global.h"
#include "lib/widget.h"

#include "src/mcterm/mcterm.h"

/*** declarations of public functions ************************************************************/

void mcterm_overlay_install (WMcTerm *term);
void mcterm_overlay_redraw_visible_panels (WMcTerm *term);
void mcterm_overlay_cycle_focus (WMcTerm *term);
gboolean mcterm_overlay_panel_has_focus (void);
void mcterm_overlay_show_other_panel_after_open (WMcTerm *term, gboolean right_command);
void mcterm_overlay_toggle_panel (WMcTerm *term, int idx);

#endif /* MC__MCTERM_OVERLAY_H */
