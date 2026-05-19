/*
   Panelize plugin - preset selection dialog.

   Copyright (C) 1995-2026
   Free Software Foundation, Inc.

   Written by:
   Janne Kukonlehto, 1995
   Jakub Jelinek, 1995
   Andrew Borodin <aborodin@vmail.ru> 2011-2023
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
#include "lib/strutil.h"
#include "lib/tty/tty.h"
#include "lib/widget.h"

#include "src/history.h"  // MC_HISTORY_FM_PANELIZE_CMD

#include "panelize_dlg.h"

/*** file scope macro definitions ****************************************************************/

#define UX       3
#define UY       2

#define B_ADD    (B_USER)
#define B_REMOVE (B_USER + 1)
#define B_EDIT   (B_USER + 2)

/*** file scope type declarations ****************************************************************/

typedef struct
{
    GPtrArray *presets;
    WListbox *listbox;
    WInput *input;
    int last_listitem;
} panelize_dlg_state_t;

/*** file scope variables ************************************************************************/

static panelize_dlg_state_t state;

/*** file scope functions ************************************************************************/

static void
sync_input_to_listbox (void)
{
    int idx = state.listbox->current;

    if (idx == state.last_listitem || idx < 0)
        return;

    state.last_listitem = idx;
    if ((guint) idx < state.presets->len)
    {
        panelize_preset_t *p = g_ptr_array_index (state.presets, (guint) idx);
        input_assign_text (state.input, p->command);
        state.input->point = 0;
        input_update (state.input, TRUE);
    }
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
panelize_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_INIT:
        group_default_callback (w, NULL, MSG_INIT, 0, NULL);
        MC_FALLTHROUGH;
    case MSG_NOTIFY:
        sync_input_to_listbox ();
        return MSG_HANDLED;
    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/

panelize_dlg_result_t
panelize_dlg_run (GPtrArray *presets, int initial_index, int *out_index, char **out_command)
{
    static const struct
    {
        int ret_cmd;
        button_flags_t flags;
        const char *text;
    } buttons[] = {
        { B_ENTER, DEFPUSH_BUTTON, N_ ("Pane&lize") }, { B_EDIT, NORMAL_BUTTON, N_ ("&Edit") },
        { B_ADD, NORMAL_BUTTON, N_ ("&Add new") },     { B_REMOVE, NORMAL_BUTTON, N_ ("&Remove") },
        { B_CANCEL, NORMAL_BUTTON, N_ ("&Cancel") },
    };

    WDialog *dlg;
    WGroup *g;
    int cols, blen, x, y;
    size_t i;
    int ret;
    guint p_idx;
    panelize_dlg_result_t result;

    if (out_index != NULL)
        *out_index = -1;
    if (out_command != NULL)
        *out_command = NULL;

    /* compute layout */
    blen = G_N_ELEMENTS (buttons) - 1;
    for (i = 0; i < G_N_ELEMENTS (buttons); i++)
    {
        blen += str_term_width1 (_ (buttons[i].text)) + 3 + 1;
        if (buttons[i].flags == DEFPUSH_BUTTON)
            blen += 2;
    }

    cols = COLS - 6;
    cols = MAX (cols, blen + 4);

    dlg = dlg_create (TRUE, 0, 0, 20, cols, WPOS_CENTER, FALSE, dialog_colors,
                      panelize_dlg_callback, NULL, "[External panelize]", _ ("External panelize"));
    g = GROUP (dlg);

    state.presets = presets;
    state.last_listitem = -1;

    y = UY;
    group_add_widget (g, groupbox_new (y++, UX, 12, cols - UX * 2, ""));

    state.listbox = listbox_new (y, UX + 1, 10, cols - UX * 2 - 2, FALSE, NULL);
    for (p_idx = 0; p_idx < presets->len; p_idx++)
    {
        panelize_preset_t *p = g_ptr_array_index (presets, p_idx);
        listbox_add_item (state.listbox, LISTBOX_APPEND_AT_END, 0, p->label, p, FALSE);
    }
    if (initial_index >= 0 && (guint) initial_index < presets->len)
        listbox_set_current (state.listbox, initial_index);
    group_add_widget (g, state.listbox);

    y += WIDGET (state.listbox)->rect.lines + 1;
    group_add_widget (g, label_new (y++, UX, _ ("Command")));
    state.input =
        input_new (y++, UX, input_colors, cols - UX * 2, "", MC_HISTORY_FM_PANELIZE_CMD,
                   INPUT_COMPLETE_FILENAMES | INPUT_COMPLETE_HOSTNAMES | INPUT_COMPLETE_COMMANDS
                       | INPUT_COMPLETE_VARIABLES | INPUT_COMPLETE_USERNAMES | INPUT_COMPLETE_CD
                       | INPUT_COMPLETE_SHELL_ESC);
    group_add_widget (g, state.input);

    group_add_widget (g, hline_new (y++, -1, -1));

    x = (cols - blen) / 2;
    for (i = 0; i < G_N_ELEMENTS (buttons); i++)
    {
        WButton *b =
            button_new (y, x, buttons[i].ret_cmd, buttons[i].flags, _ (buttons[i].text), NULL);
        group_add_widget (g, b);
        x += button_get_width (b) + 1;
    }

    widget_select (WIDGET (state.listbox));

    ret = dlg_run (dlg);

    if (out_index != NULL)
        *out_index = state.listbox->current;
    if (ret == B_ENTER && out_command != NULL && !input_is_empty (state.input))
        *out_command = input_get_text (state.input);

    widget_destroy (WIDGET (dlg));

    switch (ret)
    {
    case B_ENTER:
        result = PANELIZE_DLG_PANELIZE;
        break;
    case B_ADD:
        result = PANELIZE_DLG_ADD;
        break;
    case B_REMOVE:
        result = PANELIZE_DLG_REMOVE;
        break;
    case B_EDIT:
        result = PANELIZE_DLG_EDIT;
        break;
    case B_CANCEL:
    default:
        result = PANELIZE_DLG_CANCEL;
        break;
    }

    state.presets = NULL;
    state.listbox = NULL;
    state.input = NULL;
    return result;
}

/* --------------------------------------------------------------------------------------------- */
