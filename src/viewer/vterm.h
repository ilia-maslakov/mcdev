#ifndef MC__VIEWER_VTERM_H
#define MC__VIEWER_VTERM_H

#include "lib/global.h"

#include "ansi.h"
#include "terminal_buffer.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define MCVIEW_VTERM_MAX_PARAMS 16

/*** enums ***************************************************************************************/

typedef enum
{
    VTERM_CHAR,
    VTERM_SGR,
    VTERM_CR,
    VTERM_LF,
    VTERM_CURSOR_ABS,
    VTERM_CURSOR_FWD,
    VTERM_CURSOR_BACK,
    VTERM_CURSOR_UP,
    VTERM_CURSOR_DOWN,
    VTERM_ERASE_EOL,
    VTERM_ERASE_BOL,
    VTERM_ERASE_LINE,
    VTERM_ERASE_SCREEN,
    VTERM_ALT_SCREEN_EXIT,
    VTERM_CONSUMED,
} vterm_result_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct
{
    vterm_result_t type;
    int param1;
    int param2;
    gunichar ch;
    mcview_ansi_state_t ansi;
} vterm_event_t;

typedef struct mcview_vterm_struct mcview_vterm_t;

/*** declarations of public functions ************************************************************/

mcview_vterm_t *mcview_vterm_new (void);
void mcview_vterm_free (mcview_vterm_t *vt);

vterm_event_t mcview_vterm_feed (mcview_vterm_t *vt, unsigned char byte);

void mcview_vterm_apply_event (mcview_vterm_t *vt, const vterm_event_t *ev);

int mcview_vterm_cursor_row (const mcview_vterm_t *vt);
int mcview_vterm_cursor_col (const mcview_vterm_t *vt);
mcview_terminal_buffer_t *mcview_vterm_buf (mcview_vterm_t *vt);
off_t mcview_vterm_replay_offset (const mcview_vterm_t *vt);
void mcview_vterm_set_replay_offset (mcview_vterm_t *vt, off_t offset);

#define MCVIEW_VTERM_FOLLOW_END (-1)

int mcview_vterm_dpy_top_row (const mcview_vterm_t *vt);
void mcview_vterm_set_dpy_top_row (mcview_vterm_t *vt, int row);
int mcview_vterm_resolve_top_row (const mcview_vterm_t *vt, int data_lines);
void mcview_vterm_reset (mcview_vterm_t *vt);

/*** inline functions ****************************************************************************/

#endif /* MC__VIEWER_VTERM_H */
