/*
   Internal file viewer for the Midnight Commander
   VT100 terminal sequence parser for ANSI terminal replay mode.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <string.h>

#include "lib/global.h"

#include "ansi.h"
#include "terminal_buffer.h"
#include "vterm.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define VTERM_ESC_CHAR               0x1Bu

#define MCVIEW_VTERM_MAX_CANVAS_ROWS 2000

#define MCVIEW_VTERM_MAX_CANVAS_COLS 4096

/*** file scope type declarations ****************************************************************/

struct mcview_vterm_struct
{
    mcview_ansi_state_t ansi;

    gboolean saw_esc;
    gboolean in_csi;
    gboolean csi_private;
    gboolean in_osc;
    gboolean in_osc_esc;
    gboolean in_esc_char;

    int params[MCVIEW_VTERM_MAX_PARAMS];
    int param_count;
    int current_param;
    gboolean has_current;

    unsigned char utf8_buf[4];
    int utf8_len;
    int utf8_expected;

    int cursor_row;
    int cursor_col;

    int term_rows;
    int term_cols;
    int scroll_top;
    int scroll_bottom;

    off_t replay_offset;

    mcview_terminal_buffer_t *buf;
    mcview_terminal_buffer_t *snapshot_buf; /* main-screen content saved on ALT_SCREEN_ENTER */
    int snapshot_cursor_row;
    int snapshot_cursor_col;
    mcview_terminal_buffer_t *alt_frame_buf; /* last complete frame inside alt-screen */

    /* Cursor-home with no new chars since last snapshot is exit cleanup. */
    gboolean new_chars_since_snapshot;

    /* TRUE while inside ESC[?1049h / ESC[?1049l alt-screen bracket.
     * Snapshot is taken on entry and must not be overwritten by TUI redraws. */
    gboolean in_alt_screen;

    int dpy_top_row;
};

/*** file scope variables ************************************************************************/

/*** forward declarations (file scope functions) *************************************************/

static vterm_event_t vterm_dispatch_csi (mcview_vterm_t *vt, unsigned char final_byte);
static vterm_event_t vterm_handle_utf8 (mcview_vterm_t *vt, unsigned char byte);
static void vterm_finalize_param (mcview_vterm_t *vt);
static vterm_event_t vterm_make (mcview_vterm_t *vt, vterm_result_t type);

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static vterm_event_t
vterm_make (mcview_vterm_t *vt, vterm_result_t type)
{
    vterm_event_t ev;

    memset (&ev, 0, sizeof (ev));
    ev.type = type;
    ev.ansi = vt->ansi;
    return ev;
}

/* --------------------------------------------------------------------------------------------- */

static void
vterm_finalize_param (mcview_vterm_t *vt)
{
    if (vt->param_count < MCVIEW_VTERM_MAX_PARAMS)
    {
        vt->params[vt->param_count] = vt->current_param;
        vt->param_count++;
    }
    vt->current_param = 0;
    vt->has_current = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static vterm_event_t
vterm_dispatch_csi (mcview_vterm_t *vt, unsigned char final_byte)
{
    int p0, p1;

    /* DEC private sequences (prefixed with ?): handle known ones, consume the rest. */
    if (vt->csi_private)
    {
        if (vt->param_count > 0 && vt->params[0] == 1049)
        {
            /* ESC[?1049h = enter alt-screen, ESC[?1049l = exit alt-screen */
            if (final_byte == 'h')
                return vterm_make (vt, VTERM_ALT_SCREEN_ENTER);
            if (final_byte == 'l')
                return vterm_make (vt, VTERM_ALT_SCREEN_EXIT);
        }
        return vterm_make (vt, VTERM_CONSUMED);
    }

    p0 = (vt->param_count > 0) ? vt->params[0] : 0;
    p1 = (vt->param_count > 1) ? vt->params[1] : 0;

    switch (final_byte)
    {
    case 'm':
        /* SGR: ansi.c already applied it. */
        return vterm_make (vt, VTERM_SGR);

    case 'H': /* cursor position (1-based row;col) */
    case 'f': /* same */
    {
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_ABS);
        ev.param1 = (p0 > 0 ? p0 : 1) - 1; /* 1-based -> 0-based */
        ev.param2 = (p1 > 0 ? p1 : 1) - 1;
        return ev;
    }

    case 'A': /* cursor up */
    {
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_UP);
        ev.param1 = (p0 > 0) ? p0 : 1;
        return ev;
    }

    case 'B': /* cursor down */
    {
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_DOWN);
        ev.param1 = (p0 > 0) ? p0 : 1;
        return ev;
    }

    case 'C': /* cursor forward */
    {
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_FWD);
        ev.param1 = (p0 > 0) ? p0 : 1;
        return ev;
    }

    case 'D': /* cursor back N */
    {
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_BACK);
        ev.param1 = (p0 > 0) ? p0 : 1;
        return ev;
    }

    case 'E': /* cursor next line */
    {
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_ABS);
        ev.param1 = vt->cursor_row + ((p0 > 0) ? p0 : 1);
        ev.param2 = 0;
        return ev;
    }

    case 'F': /* cursor previous line */
    {
        int n = (p0 > 0) ? p0 : 1;
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_ABS);
        ev.param1 = (vt->cursor_row >= n) ? vt->cursor_row - n : 0;
        ev.param2 = 0;
        return ev;
    }

    case 'G': /* cursor horizontal absolute (1-based col) */
    {
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_ABS);
        ev.param1 = vt->cursor_row;
        ev.param2 = (p0 > 0 ? p0 : 1) - 1;
        return ev;
    }

    case 'K': /* erase in line */
        switch (p0)
        {
        case 0:
            return vterm_make (vt, VTERM_ERASE_EOL);
        case 1:
            return vterm_make (vt, VTERM_ERASE_BOL);
        case 2:
            return vterm_make (vt, VTERM_ERASE_LINE);
        default:
            return vterm_make (vt, VTERM_CONSUMED);
        }

    case 'J': /* erase in display */
        if (p0 == 2)
            return vterm_make (vt, VTERM_ERASE_SCREEN);
        if (p0 == 1)
            return vterm_make (vt, VTERM_ERASE_TO_BOS);
        return vterm_make (vt, VTERM_ERASE_TO_EOS);

    case 'r': /* DECSTBM -- set scroll region (1-based top;bottom) */
    {
        int top = (p0 > 0 ? p0 : 1) - 1;
        int bot = (p1 > 0 ? p1 : vt->term_rows) - 1;
        vterm_event_t ev;

        if (top < 0)
            top = 0;
        if (bot >= vt->term_rows)
            bot = vt->term_rows - 1;
        if (top >= bot) /* invalid region: consume without effect */
            return vterm_make (vt, VTERM_CONSUMED);
        ev = vterm_make (vt, VTERM_SET_SCROLL_REGION);
        ev.param1 = top;
        ev.param2 = bot;
        return ev;
    }

    case 'd': /* VPA -- vertical position absolute (1-based row) */
    {
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_ROW_ABS);
        ev.param1 = (p0 > 0 ? p0 : 1) - 1;
        return ev;
    }

    case 'X': /* ECH -- erase characters */
    {
        vterm_event_t ev = vterm_make (vt, VTERM_ERASE_CHARS);
        ev.param1 = (p0 > 0) ? p0 : 1;
        return ev;
    }

    default:
        return vterm_make (vt, VTERM_CONSUMED);
    }
}

/* --------------------------------------------------------------------------------------------- */

/** Determine the expected length of a UTF-8 sequence from its lead byte. */
static int
utf8_seq_len (unsigned char b)
{
    if (b < 0x80)
        return 1;
    if ((b & 0xE0) == 0xC0)
        return 2;
    if ((b & 0xF0) == 0xE0)
        return 3;
    if ((b & 0xF8) == 0xF0)
        return 4;
    return -1; /* continuation byte or invalid */
}

/* --------------------------------------------------------------------------------------------- */

static vterm_event_t
vterm_handle_utf8 (mcview_vterm_t *vt, unsigned char byte)
{
    /* Continuation byte while accumulating a multi-byte sequence. */
    if (vt->utf8_expected > 0)
    {
        if ((byte & 0xC0) != 0x80)
        {
            /* Invalid continuation -- abandon sequence, treat byte as lead. */
            vt->utf8_len = 0;
            vt->utf8_expected = 0;
        }
        else
        {
            vt->utf8_buf[vt->utf8_len++] = byte;
            if (vt->utf8_len == vt->utf8_expected)
            {
                /* Sequence complete: decode codepoint. */
                gunichar ch;
                vterm_event_t ev = vterm_make (vt, VTERM_CHAR);

                switch (vt->utf8_expected)
                {
                case 2:
                    ch = ((gunichar) (vt->utf8_buf[0] & 0x1F) << 6) | (vt->utf8_buf[1] & 0x3F);
                    break;
                case 3:
                    ch = ((gunichar) (vt->utf8_buf[0] & 0x0F) << 12)
                        | ((gunichar) (vt->utf8_buf[1] & 0x3F) << 6) | (vt->utf8_buf[2] & 0x3F);
                    break;
                case 4:
                    ch = ((gunichar) (vt->utf8_buf[0] & 0x07) << 18)
                        | ((gunichar) (vt->utf8_buf[1] & 0x3F) << 12)
                        | ((gunichar) (vt->utf8_buf[2] & 0x3F) << 6) | (vt->utf8_buf[3] & 0x3F);
                    break;
                default:
                    ch = '?';
                    break;
                }

                vt->utf8_len = 0;
                vt->utf8_expected = 0;
                ev.ch = ch;
                return ev;
            }
            /* Sequence not yet complete. */
            return vterm_make (vt, VTERM_CONSUMED);
        }
    }

    /* Lead byte of a multi-byte sequence. */
    {
        int expected = utf8_seq_len (byte);

        if (expected == 1)
        {
            /* ASCII or C1 control -- treat as single character. */
            vterm_event_t ev = vterm_make (vt, VTERM_CHAR);
            ev.ch = (gunichar) byte;
            return ev;
        }

        if (expected > 1)
        {
            vt->utf8_buf[0] = byte;
            vt->utf8_len = 1;
            vt->utf8_expected = expected;
            return vterm_make (vt, VTERM_CONSUMED);
        }

        /* Invalid byte (e.g. 0x80-0xBF without prior lead): consume. */
        return vterm_make (vt, VTERM_CONSUMED);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

mcview_vterm_t *
mcview_vterm_new (void)
{
    mcview_vterm_t *vt;

    vt = g_new0 (mcview_vterm_t, 1);
    mcview_ansi_state_init (&vt->ansi);
    vt->buf = mcview_terminal_buffer_new ();
    vt->dpy_top_row = MCVIEW_VTERM_FOLLOW_END;
    vt->term_rows = 24;
    vt->term_cols = 80;
    vt->scroll_top = 0;
    vt->scroll_bottom = 23;
    return vt;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_vterm_free (mcview_vterm_t *vt)
{
    if (vt == NULL)
        return;
    mcview_terminal_buffer_free (vt->buf);
    mcview_terminal_buffer_free (vt->snapshot_buf);
    mcview_terminal_buffer_free (vt->alt_frame_buf);
    g_free (vt);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_vterm_reset (mcview_vterm_t *vt)
{
    mcview_ansi_state_init (&vt->ansi);
    vt->saw_esc = FALSE;
    vt->in_csi = FALSE;
    vt->csi_private = FALSE;
    vt->in_osc = FALSE;
    vt->in_osc_esc = FALSE;
    vt->in_esc_char = FALSE;
    vt->param_count = 0;
    vt->current_param = 0;
    vt->has_current = FALSE;
    vt->utf8_len = 0;
    vt->utf8_expected = 0;
    vt->cursor_row = 0;
    vt->cursor_col = 0;
    vt->scroll_top = 0;
    vt->scroll_bottom = vt->term_rows - 1;
    vt->replay_offset = 0;
    vt->dpy_top_row = MCVIEW_VTERM_FOLLOW_END;
    mcview_terminal_buffer_free (vt->snapshot_buf);
    vt->snapshot_buf = NULL;
    mcview_terminal_buffer_free (vt->alt_frame_buf);
    vt->alt_frame_buf = NULL;
    vt->new_chars_since_snapshot = FALSE;
    vt->in_alt_screen = FALSE;
    mcview_terminal_buffer_clear (vt->buf);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcview_vterm_set_size (mcview_vterm_t *vt, int rows, int cols)
{
    if (rows < 1)
        rows = 1;
    if (cols < 1)
        cols = 1;

    if (rows == vt->term_rows && cols == vt->term_cols)
        return FALSE;

    vt->term_rows = rows;
    vt->term_cols = cols;

    if (vt->scroll_bottom >= vt->term_rows)
        vt->scroll_bottom = vt->term_rows - 1;
    if (vt->scroll_top >= vt->scroll_bottom)
        vt->scroll_top = 0;
    if (vt->cursor_row >= vt->term_rows)
        vt->cursor_row = vt->term_rows - 1;
    if (vt->cursor_col >= vt->term_cols)
        vt->cursor_col = vt->term_cols - 1;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

vterm_event_t
mcview_vterm_feed (mcview_vterm_t *vt, unsigned char byte)
{
    /* OSC: consume bytes until BEL or ST (ESC \). */
    if (vt->in_osc)
    {
        if (vt->in_osc_esc)
        {
            vt->in_osc_esc = FALSE;
            if (byte == '\\')
                vt->in_osc = FALSE;
            /* Any other char after ESC in OSC: stay in OSC. */
        }
        else if (byte == 0x07u)
        {
            vt->in_osc = FALSE; /* BEL terminates OSC */
        }
        else if (byte == ESC_CHAR)
        {
            vt->in_osc_esc = TRUE;
        }
        /* Do not feed OSC content to ansi.c -- it would misinterpret it. */
        return vterm_make (vt, VTERM_CONSUMED);
    }

    /* ESC ( B or similar: consume one more byte. */
    if (vt->in_esc_char)
    {
        vt->in_esc_char = FALSE;
        /* Feed to ansi.c to keep its state consistent. */
        mcview_ansi_parse_char (&vt->ansi, (int) byte);
        return vterm_make (vt, VTERM_CONSUMED);
    }

    /* Feed every non-OSC byte to ansi.c for SGR state maintenance. */
    mcview_ansi_parse_char (&vt->ansi, (int) byte);

    /* ---- vterm's own CSI/ESC state machine ---- */

    /* Byte after ESC determines the sequence type. */
    if (vt->saw_esc && byte != ESC_CHAR)
    {
        vt->saw_esc = FALSE;

        if (byte == '[')
        {
            /* CSI: start accumulating. */
            vt->in_csi = TRUE;
            vt->csi_private = FALSE;
            vt->param_count = 0;
            vt->current_param = 0;
            vt->has_current = FALSE;
        }
        else if (byte == ']')
        {
            vt->in_osc = TRUE;
            vt->in_osc_esc = FALSE;
        }
        else if (byte == '(' || byte == ')' || byte == '*' || byte == '+')
        {
            vt->in_esc_char = TRUE; /* charset designations: consume next byte */
        }
        /* ESC = / ESC > / ESC M / ESC c / etc.: just consumed. */
        return vterm_make (vt, VTERM_CONSUMED);
    }

    if (byte == ESC_CHAR)
    {
        vt->saw_esc = TRUE;
        vt->in_csi = FALSE; /* ESC resets any in-progress CSI */
        vt->utf8_len = 0;
        vt->utf8_expected = 0;
        return vterm_make (vt, VTERM_CONSUMED);
    }

    /* Inside CSI: accumulate parameters or dispatch on final byte. */
    if (vt->in_csi)
    {
        if (byte >= '0' && byte <= '9')
        {
            if (vt->current_param <= 65535)
                vt->current_param = vt->current_param * 10 + (byte - '0');
            vt->has_current = TRUE;
            return vterm_make (vt, VTERM_CONSUMED);
        }

        if (byte == ';' || byte == ':')
        {
            vterm_finalize_param (vt);
            return vterm_make (vt, VTERM_CONSUMED);
        }

        if (byte == '?')
        {
            vt->csi_private = TRUE;
            return vterm_make (vt, VTERM_CONSUMED);
        }

        if (byte >= 0x40u && byte <= 0x7Eu)
        {
            /* Final byte: finalize last param and dispatch. */
            vterm_finalize_param (vt);
            vt->in_csi = FALSE;
            return vterm_dispatch_csi (vt, byte);
        }

        /* Intermediate bytes (0x20-0x3F, excluding '?' handled above): consume. */
        return vterm_make (vt, VTERM_CONSUMED);
    }

    /* Regular character -- handle CR, LF, BS, and UTF-8. */
    if (byte == '\r')
        return vterm_make (vt, VTERM_CR);
    if (byte == '\n')
        return vterm_make (vt, VTERM_LF);
    if (byte == '\b') /* BS (0x08): move cursor left */
        return vterm_make (vt, VTERM_CURSOR_BACK);
    if (byte == '\t')
    {
        vterm_event_t ev = vterm_make (vt, VTERM_CURSOR_FWD);
        ev.param1 = 8 - (vt->cursor_col % 8);
        return ev;
    }
    /* Other C0 controls (0x01-0x07, 0x0B-0x0C, 0x0E-0x1F): silently consume. */
    if (byte < 0x20)
        return vterm_make (vt, VTERM_CONSUMED);
    return vterm_handle_utf8 (vt, byte);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_vterm_apply_event (mcview_vterm_t *vt, const vterm_event_t *ev)
{
    switch (ev->type)
    {
    case VTERM_CHAR:
        if (vt->cursor_col < MCVIEW_VTERM_MAX_CANVAS_COLS)
            mcview_terminal_buffer_put_char (vt->buf, vt->cursor_row, vt->cursor_col, ev->ch,
                                             &ev->ansi);
        vt->cursor_col++;
        vt->new_chars_since_snapshot = TRUE;
        break;

    case VTERM_CR:
        vt->cursor_col = 0;
        break;

    case VTERM_LF:
        vt->cursor_col = 0;
        if (vt->cursor_row >= vt->scroll_top && vt->cursor_row < vt->scroll_bottom)
            vt->cursor_row++;
        else if (vt->cursor_row == vt->scroll_bottom)
            mcview_terminal_buffer_scroll_up (vt->buf, vt->scroll_top, vt->scroll_bottom,
                                              vt->term_cols, &ev->ansi);
        else if (vt->cursor_row < vt->term_rows - 1)
            vt->cursor_row++;
        break;

    case VTERM_CURSOR_ABS:
    {
        int new_row = (ev->param1 >= 0) ? ev->param1 : 0;
        int new_col = (ev->param2 >= 0) ? ev->param2 : 0;

        if (new_row == 0 && vt->new_chars_since_snapshot
            && mcview_terminal_buffer_max_row (vt->buf) >= 0
            && mcview_terminal_buffer_max_row (vt->buf) < MCVIEW_VTERM_MAX_CANVAS_ROWS)
        {
            if (vt->in_alt_screen)
            {
                /* Inside alt-screen: save to alt_frame_buf so the last complete
                 * TUI frame is available for viewer display on exit. */
                mcview_terminal_buffer_free (vt->alt_frame_buf);
                vt->alt_frame_buf = mcview_terminal_buffer_copy (vt->buf);
            }
            else
            {
                /* Outside alt-screen: update main-screen snapshot. */
                mcview_terminal_buffer_free (vt->snapshot_buf);
                vt->snapshot_buf = mcview_terminal_buffer_copy (vt->buf);
                vt->snapshot_cursor_row = vt->cursor_row;
                vt->snapshot_cursor_col = vt->cursor_col;
            }
            vt->new_chars_since_snapshot = FALSE;
        }

        vt->cursor_row = (new_row < vt->term_rows) ? new_row : vt->term_rows - 1;
        vt->cursor_col = (new_col < vt->term_cols) ? new_col : vt->term_cols - 1;
    }
    break;

    case VTERM_CURSOR_FWD:
        vt->cursor_col += (ev->param1 > 0) ? ev->param1 : 1;
        if (vt->cursor_col >= vt->term_cols)
            vt->cursor_col = vt->term_cols - 1;
        break;

    case VTERM_CURSOR_BACK:
    {
        int n = (ev->param1 > 0) ? ev->param1 : 1;
        vt->cursor_col -= n;
        if (vt->cursor_col < 0)
            vt->cursor_col = 0;
    }
    break;

    case VTERM_CURSOR_UP:
        vt->cursor_row -= ev->param1;
        if (vt->cursor_row < 0)
            vt->cursor_row = 0;
        break;

    case VTERM_CURSOR_DOWN:
        vt->cursor_row += ev->param1;
        if (vt->cursor_row >= vt->term_rows)
            vt->cursor_row = vt->term_rows - 1;
        break;

    case VTERM_ERASE_EOL:
        mcview_terminal_buffer_erase_eol (vt->buf, vt->cursor_row, vt->cursor_col, vt->term_cols,
                                          &ev->ansi);
        break;

    case VTERM_ERASE_BOL:
        mcview_terminal_buffer_erase_bol (vt->buf, vt->cursor_row, vt->cursor_col, vt->term_cols,
                                          &ev->ansi);
        break;

    case VTERM_ERASE_LINE:
        mcview_terminal_buffer_erase_line (vt->buf, vt->cursor_row, vt->term_cols, &ev->ansi);
        break;

    case VTERM_ERASE_SCREEN:
        if (vt->new_chars_since_snapshot && mcview_terminal_buffer_max_row (vt->buf) >= 0
            && mcview_terminal_buffer_max_row (vt->buf) < MCVIEW_VTERM_MAX_CANVAS_ROWS)
        {
            if (vt->in_alt_screen)
            {
                mcview_terminal_buffer_free (vt->alt_frame_buf);
                vt->alt_frame_buf = mcview_terminal_buffer_copy (vt->buf);
            }
            else
            {
                mcview_terminal_buffer_free (vt->snapshot_buf);
                vt->snapshot_buf = mcview_terminal_buffer_copy (vt->buf);
                vt->snapshot_cursor_row = vt->cursor_row;
                vt->snapshot_cursor_col = vt->cursor_col;
            }
        }
        vt->new_chars_since_snapshot = FALSE;
        mcview_terminal_buffer_clear (vt->buf);
        vt->cursor_row = 0;
        vt->cursor_col = 0;
        break;

    case VTERM_ERASE_TO_EOS:
    {
        int r;
        /* Erase from cursor to end of current line, then clear all rows below. */
        mcview_terminal_buffer_erase_eol (vt->buf, vt->cursor_row, vt->cursor_col, vt->term_cols,
                                          &ev->ansi);
        for (r = vt->cursor_row + 1; r <= mcview_terminal_buffer_max_row (vt->buf); r++)
            mcview_terminal_buffer_erase_line (vt->buf, r, vt->term_cols, &ev->ansi);
        break;
    }

    case VTERM_ERASE_TO_BOS:
    {
        int r;
        /* Clear all rows above cursor, then erase from start of current line to cursor. */
        for (r = 0; r < vt->cursor_row; r++)
            mcview_terminal_buffer_erase_line (vt->buf, r, vt->term_cols, &ev->ansi);
        mcview_terminal_buffer_erase_bol (vt->buf, vt->cursor_row, vt->cursor_col, vt->term_cols,
                                          &ev->ansi);
        break;
    }

    case VTERM_ALT_SCREEN_ENTER:
        /* Snapshot the main screen so it can be restored on exit (mcterm use case).
         * alt_frame_buf will track the last complete TUI frame inside this bracket. */
        if (mcview_terminal_buffer_max_row (vt->buf) < MCVIEW_VTERM_MAX_CANVAS_ROWS)
        {
            mcview_terminal_buffer_free (vt->snapshot_buf);
            vt->snapshot_buf = mcview_terminal_buffer_copy (vt->buf);
            vt->snapshot_cursor_row = vt->cursor_row;
            vt->snapshot_cursor_col = vt->cursor_col;
        }
        mcview_terminal_buffer_free (vt->alt_frame_buf);
        vt->alt_frame_buf = NULL;
        vt->in_alt_screen = TRUE;
        vt->new_chars_since_snapshot = FALSE;
        break;

    case VTERM_ALT_SCREEN_EXIT:
        if (vt->snapshot_buf != NULL)
        {
            if (mcview_terminal_buffer_max_row (vt->snapshot_buf) > 0)
            {
                /* Main screen had content spanning multiple rows: restore it
                 * (shell->app->shell flow).  Single-row noise (debug output,
                 * init sequences) does not qualify -- use alt_frame_buf instead. */
                mcview_terminal_buffer_free (vt->buf);
                mcview_terminal_buffer_free (vt->alt_frame_buf);
                vt->alt_frame_buf = NULL;
                vt->buf = vt->snapshot_buf;
                vt->snapshot_buf = NULL;
                vt->cursor_row = vt->snapshot_cursor_row;
                vt->cursor_col = vt->snapshot_cursor_col;
            }
            else if (vt->alt_frame_buf != NULL)
            {
                /* Main screen was empty but we captured a frame inside alt-screen
                 * (viewer mode: log starts with app).  Show that last frame. */
                mcview_terminal_buffer_free (vt->buf);
                mcview_terminal_buffer_free (vt->snapshot_buf);
                vt->snapshot_buf = NULL;
                vt->buf = vt->alt_frame_buf;
                vt->alt_frame_buf = NULL;
            }
            else if (vt->new_chars_since_snapshot)
            {
                /* No frame snapshot yet (app never did cursor-home) but chars were
                 * written: keep current buf (htop.log-style: exit erases only last row). */
                mcview_terminal_buffer_free (vt->snapshot_buf);
                vt->snapshot_buf = NULL;
            }
            else
            {
                /* Empty alt-screen bracket (init noise): clear to avoid artifacts. */
                mcview_terminal_buffer_free (vt->snapshot_buf);
                vt->snapshot_buf = NULL;
                mcview_terminal_buffer_clear (vt->buf);
                vt->cursor_row = 0;
                vt->cursor_col = 0;
            }
        }
        else
        {
            vt->cursor_row = 0;
            vt->cursor_col = 0;
        }
        vt->in_alt_screen = FALSE;
        vt->new_chars_since_snapshot = FALSE;
        break;

    case VTERM_SET_SCROLL_REGION:
        vt->scroll_top = ev->param1;
        vt->scroll_bottom = ev->param2;
        vt->cursor_row = 0;
        vt->cursor_col = 0;
        break;

    case VTERM_CURSOR_ROW_ABS:
    {
        int row = ev->param1;
        if (row < 0)
            row = 0;
        if (row >= vt->term_rows)
            row = vt->term_rows - 1;
        vt->cursor_row = row;
    }
    break;

    case VTERM_ERASE_CHARS:
    {
        int count = (ev->param1 > 0) ? ev->param1 : 1;
        int col_to = vt->cursor_col + count - 1;
        if (col_to >= vt->term_cols)
            col_to = vt->term_cols - 1;
        mcview_terminal_buffer_fill_range (vt->buf, vt->cursor_row, vt->cursor_col, col_to, ' ',
                                           &ev->ansi);
    }
    break;

    case VTERM_SGR:
    case VTERM_CONSUMED:
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

int
mcview_vterm_cursor_row (const mcview_vterm_t *vt)
{
    return vt->cursor_row;
}

/* --------------------------------------------------------------------------------------------- */

int
mcview_vterm_cursor_col (const mcview_vterm_t *vt)
{
    return vt->cursor_col;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mcview_vterm_in_alt_screen (const mcview_vterm_t *vt)
{
    return vt->in_alt_screen;
}

/* --------------------------------------------------------------------------------------------- */

mcview_terminal_buffer_t *
mcview_vterm_buf (mcview_vterm_t *vt)
{
    return vt->buf;
}

/* --------------------------------------------------------------------------------------------- */

off_t
mcview_vterm_replay_offset (const mcview_vterm_t *vt)
{
    return vt->replay_offset;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_vterm_set_replay_offset (mcview_vterm_t *vt, off_t offset)
{
    vt->replay_offset = offset;
}

/* --------------------------------------------------------------------------------------------- */

int
mcview_vterm_dpy_top_row (const mcview_vterm_t *vt)
{
    return vt->dpy_top_row;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_vterm_set_dpy_top_row (mcview_vterm_t *vt, int row)
{
    /* MCVIEW_VTERM_FOLLOW_END (-1) is a valid sentinel; clamp anything below it. */
    vt->dpy_top_row = (row < MCVIEW_VTERM_FOLLOW_END) ? MCVIEW_VTERM_FOLLOW_END : row;
}

/* --------------------------------------------------------------------------------------------- */

int
mcview_vterm_resolve_top_row (const mcview_vterm_t *vt, int data_lines)
{
    int top = vt->dpy_top_row;
    int max;

    if (top >= 0)
        return top;

    /* FOLLOW_END: pin the viewport to the bottom of the canvas. */
    max = mcview_terminal_buffer_max_row (vt->buf);
    if (max < 0)
        return 0;
    top = max - data_lines + 1;
    return (top > 0) ? top : 0;
}

/* --------------------------------------------------------------------------------------------- */
