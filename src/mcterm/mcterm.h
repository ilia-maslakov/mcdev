#ifndef MC__MCTERM_H
#define MC__MCTERM_H

#include "lib/global.h"
#include "lib/widget.h"
#include "lib/tty/tty.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct WMcTerm WMcTerm;

/*** declarations of public functions ************************************************************/

#ifdef ENABLE_MCTERM

WMcTerm *mcterm_new (const WRect *r, const char *start_dir);
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

#else /* !ENABLE_MCTERM */

static inline WMcTerm *
mcterm_new (const WRect *r, const char *start_dir)
{
    (void) r;
    (void) start_dir;
    return NULL;
}
static inline void
mcterm_free (WMcTerm *t)
{
    (void) t;
}
static inline gboolean
mcterm_is_alive (const WMcTerm *t)
{
    (void) t;
    return FALSE;
}
static inline gboolean
mcterm_in_alt_screen (const WMcTerm *t)
{
    (void) t;
    return FALSE;
}
static inline Widget *
mcterm_widget (WMcTerm *t)
{
    (void) t;
    return NULL;
}
static inline gboolean
mcterm_send_line (WMcTerm *t, const char *line)
{
    (void) t;
    (void) line;
    return FALSE;
}
static inline gboolean
mcterm_send_internal_line (WMcTerm *t, const char *line)
{
    (void) t;
    (void) line;
    return FALSE;
}
static inline gboolean
mcterm_shell_at_prompt (const WMcTerm *t)
{
    (void) t;
    return FALSE;
}
static inline const char *
mcterm_osc7_raw (const WMcTerm *t)
{
    (void) t;
    return NULL;
}
static inline void
mcterm_set_prompt_callback (WMcTerm *t, void (*cb) (void *), void *data)
{
    (void) t;
    (void) cb;
    (void) data;
}
static inline void
mcterm_set_after_redraw_callback (WMcTerm *t, void (*cb) (void *), void *data)
{
    (void) t;
    (void) cb;
    (void) data;
}
static inline gboolean
mcterm_osc7_capable (const WMcTerm *t)
{
    (void) t;
    return FALSE;
}
static inline int
mcterm_cursor_col (const WMcTerm *t)
{
    (void) t;
    return -1;
}
static inline void
mcterm_draw_prompt_row (const WMcTerm *t, int screen_y)
{
    (void) t;
    (void) screen_y;
}
static inline gboolean
mcterm_send_tab_complete (WMcTerm *t, const char *text)
{
    (void) t;
    (void) text;
    return FALSE;
}

#endif /* ENABLE_MCTERM */

/*** inline functions ****************************************************************************/

#endif /* MC__MCTERM_H */
