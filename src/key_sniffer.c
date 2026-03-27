/*
   Key sniffer dialog -- shows raw escape sequences and keycode mapping.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026.

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
#include "lib/keybind.h"
#include "lib/terminal.h" /* convert_controls() */
#include "lib/tty/tty.h"
#include "lib/tty/key.h"
#include "lib/tty/mouse.h"
#include "lib/widget.h"

#include "keymap.h"
#include "key_sniffer.h"

/*** file scope macro definitions ****************************************************************/

#define KS_DLG_WIDTH   50
#define KS_DLG_HEIGHT  11

#define KS_BTN_CAPTURE (B_USER + 1)

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static WLabel *ks_lbl_raw;
static WLabel *ks_lbl_keycode;
static WLabel *ks_lbl_name;
static WLabel *ks_lbl_action;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
ks_update_display (const char *seq)
{
    char *raw;
    int keycode;
    char *key_name;
    const char *action_name;
    char kc_display[32];

    /* convert to raw bytes */
    raw = convert_controls (seq);

    /* display raw sequence + hex dump */
    {
        GString *disp;
        int i;

        disp = g_string_sized_new (64);
        g_string_append (disp, seq);
        g_string_append (disp, "  [");
        for (i = 0; i < (int) strlen (raw); i++)
        {
            if (i > 0)
                g_string_append_c (disp, ' ');
            g_string_append_printf (disp, "%02x", (unsigned char) raw[i]);
        }
        g_string_append_c (disp, ']');
        label_set_text (ks_lbl_raw, disp->str);
        g_string_free (disp, TRUE);
    }

    /* match trie */
    keycode = tty_match_seq_to_keycode (raw, (int) strlen (raw));

    /* single byte fallback */
    if (keycode == 0 && strlen (raw) == 1)
    {
        unsigned char c = (unsigned char) raw[0];

        keycode = (c < 32) ? (KEY_M_CTRL | (c + 'a' - 1)) : (int) c;
    }
    /* ESC + single char: Alt */
    if (keycode == 0 && strlen (raw) == 2 && raw[0] == '\x1b')
        keycode = KEY_M_ALT | (int) (unsigned char) raw[1];
    g_free (raw);

    /* display keycode */
    if (keycode > 0)
        g_snprintf (kc_display, sizeof (kc_display), "0x%x (%d)", (unsigned) keycode, keycode);
    else
        g_strlcpy (kc_display, _ ("(not recognized)"), sizeof (kc_display));
    label_set_text (ks_lbl_keycode, kc_display);

    /* display symbolic name */
    key_name = (keycode > 0) ? tty_keycode_to_keyname (keycode) : NULL;
    label_set_text (ks_lbl_name, (key_name != NULL) ? key_name : "");

    /* display action from current keymaps with section prefix */
    {
        static const struct
        {
            const char *name;
            const global_keymap_t **map;
        } ks_maps[] = {
            { "filemanager", &filemanager_map },
            { "panel", &panel_map },
            { "dialog", &dialog_map },
            { "input", &input_map },
            { "menu", &menu_map },
            { "tree", &tree_map },
        };

        char action_buf[64];
        gboolean found = FALSE;

        if (keycode > 0)
        {
            size_t mi;

            for (mi = 0; mi < G_N_ELEMENTS (ks_maps); mi++)
            {
                long cmd;

                if (*ks_maps[mi].map == NULL)
                    continue;

                cmd = keybind_lookup_keymap_command (*ks_maps[mi].map, (long) keycode);
                if (cmd != CK_IgnoreKey)
                {
                    action_name = keybind_lookup_actionname (cmd);
                    if (action_name != NULL)
                    {
                        g_snprintf (action_buf, sizeof (action_buf), "%s/%s", ks_maps[mi].name,
                                    action_name);
                        label_set_text (ks_lbl_action, action_buf);
                        found = TRUE;
                        break;
                    }
                }
            }
        }

        if (!found)
            label_set_text (ks_lbl_action, _ ("(none)"));
    }

    g_free (key_name);
}

/* --------------------------------------------------------------------------------------------- */

static int
ks_capture_btn (WButton *button, int action)
{
    WDialog *d;
    char *seq;

    (void) button;
    (void) action;

    d = create_message (D_NORMAL, _ ("Key sniffer"), "%s",
                        _ ("Press any key...\n\n"
                           "Wait until this message disappears."));
    mc_refresh ();

    disable_mouse ();
    seq = learn_key ();
    enable_mouse ();

    dlg_run_done (d);
    widget_destroy (WIDGET (d));

    if (seq != NULL)
    {
        ks_update_display (seq);
        g_free (seq);
    }

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
key_sniffer (void)
{
    WDialog *dlg;
    WGroup *g;
    WButton *btn;

    dlg = dlg_create (TRUE, 0, 0, KS_DLG_HEIGHT, KS_DLG_WIDTH, WPOS_CENTER, FALSE, dialog_colors,
                      NULL, NULL, "[Key Sniffer]", _ ("Key sniffer"));
    g = GROUP (dlg);

    group_add_widget (g, label_new (2, 2, _ ("Shortcut:")));
    ks_lbl_name = label_new (2, 12, "");
    group_add_widget (g, ks_lbl_name);

    group_add_widget (g, label_new (3, 2, _ ("Action:")));
    ks_lbl_action = label_new (3, 12, "");
    group_add_widget (g, ks_lbl_action);

    group_add_widget (g, label_new (4, 2, _ ("Raw:")));
    ks_lbl_raw = label_new (4, 12, "");
    group_add_widget (g, ks_lbl_raw);

    group_add_widget (g, label_new (5, 2, _ ("Keycode:")));
    ks_lbl_keycode = label_new (5, 12, "");
    group_add_widget (g, ks_lbl_keycode);

    group_add_widget (g, hline_new (7, -1, -1));

    btn = button_new (8, 6, KS_BTN_CAPTURE, NORMAL_BUTTON, _ ("&Capture key"), ks_capture_btn);
    group_add_widget (g, btn);
    group_add_widget (g, button_new (8, 30, B_CANCEL, NORMAL_BUTTON, _ ("&Close"), NULL));

    dlg_run (dlg);
    widget_destroy (WIDGET (dlg));

    repaint_screen ();
}

/* --------------------------------------------------------------------------------------------- */
