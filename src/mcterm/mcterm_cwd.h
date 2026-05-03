#ifndef MC__MCTERM_CWD_H
#define MC__MCTERM_CWD_H

#include "mcterm.h"

/*** declarations of public functions ************************************************************/

#ifdef ENABLE_MCTERM

char *mcterm_osc7_uri_to_path (const char *osc7_raw);
char *mcterm_cwd_on_exit (WMcTerm *t, const char *panel_cwd);

#else /* !ENABLE_MCTERM */

static inline char *
mcterm_cwd_on_exit (WMcTerm *t, const char *panel_cwd)
{
    (void) t;
    (void) panel_cwd;
    return NULL;
}

#endif /* ENABLE_MCTERM */

/*** inline functions ****************************************************************************/

#endif /* MC__MCTERM_CWD_H */
