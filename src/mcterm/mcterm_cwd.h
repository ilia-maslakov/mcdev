#ifndef MC__MCTERM_CWD_H
#define MC__MCTERM_CWD_H

#include "mcterm.h"

/*** declarations of public functions ************************************************************/

char *mcterm_osc7_uri_to_path (const char *osc7_raw);
/* Return the shell's current cwd from OSC 7, or NULL if cwd matches panel_cwd.
 * Caller must g_free() the returned string. */
char *mcterm_cwd_on_exit (WMcTerm *t, const char *panel_cwd);


/*** inline functions ****************************************************************************/

#endif /* MC__MCTERM_CWD_H */
