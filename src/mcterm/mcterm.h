#ifndef MC__MCTERM_H
#define MC__MCTERM_H

#include "lib/global.h"
#include "lib/widget.h"
#include "lib/tty/tty.h"  // KEY_* declarations

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct WMcTerm WMcTerm;

/*** declarations of public functions ************************************************************/

WMcTerm *mcterm_new (const WRect *r);
void mcterm_free (WMcTerm *t);
gboolean mcterm_is_alive (const WMcTerm *t);
gboolean mcterm_in_alt_screen (const WMcTerm *t);
Widget *mcterm_widget (WMcTerm *t);
gboolean mcterm_send_line (WMcTerm *t, const char *line);
gboolean mcterm_send_internal_line (WMcTerm *t, const char *line);
gboolean mcterm_shell_at_prompt (const WMcTerm *t);
const char *mcterm_osc7_raw (const WMcTerm *t);
void mcterm_set_prompt_callback (WMcTerm *t, void (*cb) (void *), void *data);
void mcterm_set_after_redraw_callback (WMcTerm *t, void (*cb) (void *), void *data);
gboolean mcterm_osc7_capable (const WMcTerm *t);
int mcterm_cursor_col (const WMcTerm *t);
void mcterm_draw_prompt_row (const WMcTerm *t, int screen_y);
gboolean mcterm_send_tab_complete (WMcTerm *t, const char *text);
/*** inline functions ****************************************************************************/

#endif /* MC__MCTERM_H */
