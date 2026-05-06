#ifndef MC__VIEWER_TERMINAL_BUFFER_H
#define MC__VIEWER_TERMINAL_BUFFER_H

#include "lib/global.h"

#include "ansi.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct
{
    int fg;
    int bg;
    gboolean bold;
    gboolean italic;
    gboolean underline;
    gboolean blink;
    gboolean reverse;
} mcview_cell_attr_t;

typedef struct
{
    gunichar ch; /* 0 = empty / erased */
    mcview_cell_attr_t attr;
} mcview_vterm_cell_t;

typedef struct mcview_terminal_buffer_struct mcview_terminal_buffer_t;

/*** declarations of public functions ************************************************************/

mcview_terminal_buffer_t *mcview_terminal_buffer_new (void);
void mcview_terminal_buffer_free (mcview_terminal_buffer_t *buf);

void mcview_terminal_buffer_clear (mcview_terminal_buffer_t *buf);

void mcview_terminal_buffer_put_char (mcview_terminal_buffer_t *buf, int row, int col, gunichar ch,
                                      const mcview_ansi_state_t *ansi);

const mcview_vterm_cell_t *mcview_terminal_buffer_get (const mcview_terminal_buffer_t *buf, int row,
                                                       int col);

void mcview_terminal_buffer_fill_range (mcview_terminal_buffer_t *buf, int row, int col_from,
                                        int col_to, gunichar ch, const mcview_ansi_state_t *ansi);

void mcview_terminal_buffer_scroll_up (mcview_terminal_buffer_t *buf, int top, int bottom, int cols,
                                       const mcview_ansi_state_t *ansi);

void mcview_terminal_buffer_scroll_down (mcview_terminal_buffer_t *buf, int top, int bottom,
                                         int cols, const mcview_ansi_state_t *ansi);

void mcview_terminal_buffer_erase_eol (mcview_terminal_buffer_t *buf, int row, int col,
                                       int term_cols, const mcview_ansi_state_t *ansi);

void mcview_terminal_buffer_erase_bol (mcview_terminal_buffer_t *buf, int row, int col,
                                       int term_cols, const mcview_ansi_state_t *ansi);

void mcview_terminal_buffer_erase_line (mcview_terminal_buffer_t *buf, int row, int term_cols,
                                        const mcview_ansi_state_t *ansi);

void mcview_terminal_buffer_delete_chars (mcview_terminal_buffer_t *buf, int row, int col,
                                          int count, int term_cols,
                                          const mcview_ansi_state_t *ansi);

int mcview_terminal_buffer_max_row (const mcview_terminal_buffer_t *buf);

mcview_terminal_buffer_t *mcview_terminal_buffer_copy (const mcview_terminal_buffer_t *src);

/*** inline functions ****************************************************************************/

#endif /* MC__VIEWER_TERMINAL_BUFFER_H */
