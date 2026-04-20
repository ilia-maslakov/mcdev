/*
   Internal file viewer for the Midnight Commander
   Virtual screen buffer for ANSI terminal replay mode.

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

#include "terminal_buffer.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

struct mcview_terminal_buffer_struct
{
    GHashTable *rows;
    int max_row;
};

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static void
cell_attr_from_ansi (mcview_cell_attr_t *attr, const mcview_ansi_state_t *ansi)
{
    attr->fg = ansi->fg;
    attr->bg = ansi->bg;
    attr->bold = ansi->bold;
    attr->italic = ansi->italic;
    attr->underline = ansi->underline;
    attr->blink = ansi->blink;
    attr->reverse = ansi->reverse;
}

/* --------------------------------------------------------------------------------------------- */

static GArray *
get_or_create_row (mcview_terminal_buffer_t *buf, int row)
{
    gpointer key = GINT_TO_POINTER (row);
    GArray *arr;

    arr = (GArray *) g_hash_table_lookup (buf->rows, key);
    if (arr == NULL)
    {
        arr = g_array_new (FALSE, TRUE, sizeof (mcview_vterm_cell_t));
        g_hash_table_insert (buf->rows, key, arr);
    }
    return arr;
}

/* --------------------------------------------------------------------------------------------- */

static void
ensure_col (GArray *arr, int col)
{
    if ((int) arr->len <= col)
    {
        mcview_vterm_cell_t empty;
        memset (&empty, 0, sizeof (empty));
        while ((int) arr->len <= col)
            g_array_append_val (arr, empty);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
free_row_array (gpointer data)
{
    g_array_free ((GArray *) data, TRUE);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

mcview_terminal_buffer_t *
mcview_terminal_buffer_new (void)
{
    mcview_terminal_buffer_t *buf;

    buf = g_new (mcview_terminal_buffer_t, 1);
    buf->rows = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, free_row_array);
    buf->max_row = -1;
    return buf;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_terminal_buffer_free (mcview_terminal_buffer_t *buf)
{
    if (buf == NULL)
        return;
    g_hash_table_destroy (buf->rows);
    g_free (buf);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_terminal_buffer_clear (mcview_terminal_buffer_t *buf)
{
    g_hash_table_remove_all (buf->rows);
    buf->max_row = -1;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_terminal_buffer_put_char (mcview_terminal_buffer_t *buf, int row, int col, gunichar ch,
                                 const mcview_ansi_state_t *ansi)
{
    GArray *arr;
    mcview_vterm_cell_t *cell;

    if (row < 0 || col < 0)
        return;

    arr = get_or_create_row (buf, row);
    ensure_col (arr, col);

    cell = &g_array_index (arr, mcview_vterm_cell_t, col);
    cell->ch = ch;
    cell_attr_from_ansi (&cell->attr, ansi);

    if (row > buf->max_row)
        buf->max_row = row;
}

/* --------------------------------------------------------------------------------------------- */

const mcview_vterm_cell_t *
mcview_terminal_buffer_get (const mcview_terminal_buffer_t *buf, int row, int col)
{
    GArray *arr;

    arr = (GArray *) g_hash_table_lookup (buf->rows, GINT_TO_POINTER (row));
    if (arr == NULL || col < 0 || (int) arr->len <= col)
        return NULL;
    return &g_array_index (arr, mcview_vterm_cell_t, col);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_terminal_buffer_fill_range (mcview_terminal_buffer_t *buf, int row, int col_from, int col_to,
                                   gunichar ch, const mcview_ansi_state_t *ansi)
{
    GArray *arr;
    mcview_cell_attr_t attr;
    int i;

    if (row < 0 || col_from > col_to || col_to < 0)
        return;

    cell_attr_from_ansi (&attr, ansi);
    arr = get_or_create_row (buf, row);
    ensure_col (arr, col_to);

    for (i = col_from; i <= col_to; i++)
    {
        mcview_vterm_cell_t *cell = &g_array_index (arr, mcview_vterm_cell_t, i);
        cell->ch = ch;
        cell->attr = attr;
    }

    if (row > buf->max_row)
        buf->max_row = row;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_terminal_buffer_scroll_up (mcview_terminal_buffer_t *buf, int top, int bottom, int cols,
                                  const mcview_ansi_state_t *ansi)
{
    int row;

    if (top >= bottom || cols <= 0)
        return;

    /* Shift rows top..bottom-1: each row gets the content of the row below it. */
    for (row = top; row < bottom; row++)
    {
        gpointer key_dst = GINT_TO_POINTER (row);
        gpointer key_src = GINT_TO_POINTER (row + 1);
        GArray *src_arr;

        g_hash_table_remove (buf->rows, key_dst);

        src_arr = (GArray *) g_hash_table_lookup (buf->rows, key_src);
        if (src_arr != NULL)
        {
            GArray *dst_arr = g_array_new (FALSE, TRUE, sizeof (mcview_vterm_cell_t));
            if (src_arr->len > 0)
                g_array_append_vals (dst_arr, src_arr->data, src_arr->len);
            g_hash_table_insert (buf->rows, key_dst, dst_arr);
        }
    }

    /* Clear the vacated bottom row. */
    g_hash_table_remove (buf->rows, GINT_TO_POINTER (bottom));
    mcview_terminal_buffer_fill_range (buf, bottom, 0, cols - 1, ' ', ansi);

    if (buf->max_row < bottom)
        buf->max_row = bottom;
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_terminal_buffer_erase_eol (mcview_terminal_buffer_t *buf, int row, int col, int term_cols,
                                  const mcview_ansi_state_t *ansi)
{
    if (col >= term_cols || term_cols <= 0)
        return;
    mcview_terminal_buffer_fill_range (buf, row, col, term_cols - 1, ' ', ansi);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_terminal_buffer_erase_bol (mcview_terminal_buffer_t *buf, int row, int col, int term_cols,
                                  const mcview_ansi_state_t *ansi)
{
    if (col < 0)
        return;
    if (col >= term_cols)
        col = term_cols - 1;
    mcview_terminal_buffer_fill_range (buf, row, 0, col, ' ', ansi);
}

/* --------------------------------------------------------------------------------------------- */

void
mcview_terminal_buffer_erase_line (mcview_terminal_buffer_t *buf, int row, int term_cols,
                                   const mcview_ansi_state_t *ansi)
{
    if (term_cols <= 0)
        return;
    mcview_terminal_buffer_fill_range (buf, row, 0, term_cols - 1, ' ', ansi);
}

/* --------------------------------------------------------------------------------------------- */

int
mcview_terminal_buffer_max_row (const mcview_terminal_buffer_t *buf)
{
    return buf->max_row;
}

/* --------------------------------------------------------------------------------------------- */

mcview_terminal_buffer_t *
mcview_terminal_buffer_copy (const mcview_terminal_buffer_t *src)
{
    mcview_terminal_buffer_t *dst;
    GHashTableIter iter;
    gpointer key, value;

    dst = mcview_terminal_buffer_new ();
    dst->max_row = src->max_row;

    g_hash_table_iter_init (&iter, (GHashTable *) src->rows);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        GArray *src_arr = (GArray *) value;
        GArray *dst_arr = g_array_new (FALSE, TRUE, sizeof (mcview_vterm_cell_t));
        if (src_arr->len > 0)
            g_array_append_vals (dst_arr, src_arr->data, src_arr->len);
        g_hash_table_insert (dst->rows, key, dst_arr);
    }

    return dst;
}

/* --------------------------------------------------------------------------------------------- */
