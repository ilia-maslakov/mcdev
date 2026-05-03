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
    VTERM_ERASE_TO_EOS, /* ESC[J  or ESC[0J: cursor to end of screen */
    VTERM_ERASE_TO_BOS, /* ESC[1J: start of screen to cursor */
    VTERM_ALT_SCREEN_ENTER,
    VTERM_ALT_SCREEN_EXIT,
    VTERM_SET_SCROLL_REGION, /* param1=top_row (0-based), param2=bottom_row (0-based) */
    VTERM_CURSOR_ROW_ABS,    /* param1=target_row (0-based), col unchanged */
    VTERM_ERASE_CHARS,       /* param1=count; erase from cursor_col, cursor stays */
    VTERM_DCH,               /* param1=count; delete chars at cursor, shift left, fill right */
    VTERM_RI,                /* ESC M: reverse index -- scroll region down, cursor up */
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
gboolean mcview_vterm_in_alt_screen (const mcview_vterm_t *vt);
gboolean mcview_vterm_app_cursor_keys (const mcview_vterm_t *vt);
mcview_terminal_buffer_t *mcview_vterm_buf (mcview_vterm_t *vt);
off_t mcview_vterm_replay_offset (const mcview_vterm_t *vt);
void mcview_vterm_set_replay_offset (mcview_vterm_t *vt, off_t offset);

#define MCVIEW_VTERM_FOLLOW_END (-1)

int mcview_vterm_dpy_top_row (const mcview_vterm_t *vt);
void mcview_vterm_set_dpy_top_row (mcview_vterm_t *vt, int row);
int mcview_vterm_resolve_top_row (const mcview_vterm_t *vt, int data_lines);
void mcview_vterm_reset (mcview_vterm_t *vt);
const char *mcview_vterm_osc7_raw (const mcview_vterm_t *vt);
guint mcview_vterm_osc7_generation (const mcview_vterm_t *vt);

/* Update terminal size; returns TRUE on change. */
gboolean mcview_vterm_set_size (mcview_vterm_t *vt, int rows, int cols);

void mcview_vterm_restore_sync_snapshot (mcview_vterm_t *vt, mcview_terminal_buffer_t *snap_buf,
                                         int snap_cursor_row);

/* Render a terminal buffer region to the TUI screen. */
void mcview_render_terminal_canvas (const mcview_terminal_buffer_t *buf, int top_row, int screen_y,
                                    int screen_x, int rows, int cols);


/*** inline functions ****************************************************************************/

#endif /* MC__VIEWER_VTERM_H */
