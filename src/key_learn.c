/*
   Learn terminal keys -- new dialog with buttons and modifier checkboxes.

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

#include <stdlib.h>
#include <string.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/strutil.h"
#include "lib/terminal.h"
#include "lib/tty/tty.h"
#include "lib/tty/color.h"
#include "lib/tty/key.h"
#include "lib/tty/mouse.h"
#include "lib/skin.h"
#include "lib/util.h"
#include "lib/widget.h"

#include "setup.h"
#include "key_learn.h"

#ifdef USE_INTERNAL_EDIT
#include "editor/edit.h"
#endif

/*** file scope macro definitions ****************************************************************/

#define LK_DLG_WIDTH  76
#define LK_DLG_HEIGHT 17

#define LK_GRID_COLS  4
#define LK_CELL_WIDTH 18
#define LK_BTN_WIDTH  7 /* [xxxxx] */
#define LK_SEQ_WIDTH  8

#define LK_KEY_BASE   (B_USER + 200)
#define LK_EDIT_TERM  (B_USER + 199)

/*** file scope type declarations ****************************************************************/

typedef enum
{
    LK_SECTION_FKEYS = 0,
    LK_SECTION_NAV,
    LK_SECTION_SPECIAL,
    LK_SECTION_COUNT
} lk_section_t;

typedef struct
{
    const char *display;
    const char *key_name;
    lk_section_t section;
} lk_key_def_t;

typedef struct
{
    Widget *button;
    Widget *label;
    char *sequence;
    gboolean ok;
    gboolean cleared;    /* marked for deletion on save */
    gboolean learned;    /* TRUE if learned in this session (unsaved) */
    int learn_modifiers; /* modifiers active when learned */
    int clear_modifiers; /* modifiers active when cleared */
} lk_slot_t;

/*** forward declarations (file scope functions) *************************************************/

static int lk_button_callback (WButton *button, int action);

/*** file scope variables ************************************************************************/

static WDialog *lk_dlg = NULL;
static unsigned long lk_mod_ctrl_id = 0;
static unsigned long lk_mod_alt_id = 0;
static unsigned long lk_mod_shift_id = 0;
static lk_slot_t *lk_slots = NULL;
static gboolean lk_changed = FALSE;

/* clang-format off */
static const lk_key_def_t lk_keys[] = {
    /* Function keys */
    { "F1",    "f1",    LK_SECTION_FKEYS },
    { "F2",    "f2",    LK_SECTION_FKEYS },
    { "F3",    "f3",    LK_SECTION_FKEYS },
    { "F4",    "f4",    LK_SECTION_FKEYS },
    { "F5",    "f5",    LK_SECTION_FKEYS },
    { "F6",    "f6",    LK_SECTION_FKEYS },
    { "F7",    "f7",    LK_SECTION_FKEYS },
    { "F8",    "f8",    LK_SECTION_FKEYS },
    { "F9",    "f9",    LK_SECTION_FKEYS },
    { "F10",   "f10",   LK_SECTION_FKEYS },
    { "F11",   "f11",   LK_SECTION_FKEYS },
    { "F12",   "f12",   LK_SECTION_FKEYS },
    /* Arrows / Navigation */
    { "Left",  "left",     LK_SECTION_NAV },
    { "Right", "right",    LK_SECTION_NAV },
    { "Up",    "up",       LK_SECTION_NAV },
    { "Down",  "down",     LK_SECTION_NAV },
    { "Home",  "home",     LK_SECTION_NAV },
    { "End",   "end",      LK_SECTION_NAV },
    { "PgUp",  "pgup",     LK_SECTION_NAV },
    { "PgDn",  "pgdn",     LK_SECTION_NAV },
    { "Ins",   "insert",   LK_SECTION_NAV },
    { "Del",   "delete",   LK_SECTION_NAV },
    /* Special */
    { "Tab",   "tab",      LK_SECTION_SPECIAL },
    { "BkSpc", "backspace",LK_SECTION_SPECIAL },
};
/* clang-format on */

static const int lk_key_count = G_N_ELEMENTS (lk_keys);

static const char *lk_section_names[LK_SECTION_COUNT] = {
    N_ ("Function keys"),
    N_ ("Arrows / Navigation"),
    N_ ("Special"),
};

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static int
lk_get_modifiers (void)
{
    int modifiers = 0;
    Widget *w;

    if (lk_dlg == NULL)
        return 0;

    w = widget_find_by_id (WIDGET (lk_dlg), lk_mod_ctrl_id);
    if (w != NULL && CHECK (w)->state)
        modifiers |= KEY_M_CTRL;

    w = widget_find_by_id (WIDGET (lk_dlg), lk_mod_alt_id);
    if (w != NULL && CHECK (w)->state)
        modifiers |= KEY_M_ALT;

    w = widget_find_by_id (WIDGET (lk_dlg), lk_mod_shift_id);
    if (w != NULL && CHECK (w)->state)
        modifiers |= KEY_M_SHIFT;

    return modifiers;
}

/* --------------------------------------------------------------------------------------------- */

static char *
lk_build_key_name (const char *base, int modifiers)
{
    return tty_build_key_name (base, modifiers);
}

/* --------------------------------------------------------------------------------------------- */

static void
lk_update_slot (int idx)
{
    int modifiers;
    char *full_name;
    char *seq = NULL;

    modifiers = lk_get_modifiers ();

    /* preserve unsaved learned slot if modifiers match */
    if (lk_slots[idx].learned && lk_slots[idx].learn_modifiers == modifiers)
        goto done;

    /* if switching away from learned slot's modifiers, hide it (but don't free) */
    if (lk_slots[idx].learned && lk_slots[idx].learn_modifiers != modifiers)
    {
        /* show "--" but keep sequence/learned state for save */
        if (lk_slots[idx].label != NULL)
            label_set_text (LABEL (lk_slots[idx].label), "--");
        return;
    }

    /* preserve unsaved cleared slot if modifiers match */
    if (lk_slots[idx].cleared && lk_slots[idx].clear_modifiers == modifiers)
        goto done;

    /* if switching away from cleared slot's modifiers, hide it (but don't free) */
    if (lk_slots[idx].cleared && lk_slots[idx].clear_modifiers != modifiers)
    {
        if (lk_slots[idx].label != NULL)
            label_set_text (LABEL (lk_slots[idx].label), "--");
        return;
    }

    full_name = lk_build_key_name (lk_keys[idx].key_name, modifiers);

    g_free (lk_slots[idx].sequence);
    lk_slots[idx].sequence = NULL;
    lk_slots[idx].ok = FALSE;
    lk_slots[idx].cleared = FALSE;

    /* read directly from term keys file */
    {
        char *term_file;
        mc_config_t *term_cfg;

        term_file = mc_term_keys_path ();
        term_cfg = mc_config_init (term_file, TRUE);

        seq = mc_config_get_string_raw (term_cfg, "keys", full_name, NULL);

        mc_config_deinit (term_cfg);
        g_free (term_file);
    }

    if (seq != NULL && seq[0] != '\0')
    {
        lk_slots[idx].ok = TRUE;
        lk_slots[idx].sequence = seq;
    }
    else
    {
        g_free (seq);

        /* fallback: try trie lookup */
        int keycode;

        keycode = tty_keyname_to_keycode (full_name, NULL);
        if (keycode > 0)
        {
            seq = tty_key_lookup_sequence (keycode);
            if (seq != NULL)
            {
                lk_slots[idx].ok = TRUE;
                lk_slots[idx].sequence = seq;
            }
        }
    }

    g_free (full_name);

done:
    if (lk_slots[idx].label != NULL)
    {
        char lbl[LK_SEQ_WIDTH + 1];

        if (lk_slots[idx].sequence != NULL)
            g_snprintf (lbl, sizeof (lbl), "%.8s", lk_slots[idx].sequence);
        else
            g_strlcpy (lbl, "--", sizeof (lbl));

        label_set_text (LABEL (lk_slots[idx].label), lbl);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
lk_update_all (void)
{
    int i;

    for (i = 0; i < lk_key_count; i++)
        lk_update_slot (i);
}

/* --------------------------------------------------------------------------------------------- */

static int
lk_button_callback (WButton *button, int action)
{
    int idx;
    char *seq;
    int modifiers;
    int keycode;
    char *full_display;
    char *full_name;

    (void) button;

    idx = action - LK_KEY_BASE;
    if (idx < 0 || idx >= lk_key_count)
        return 0;

    modifiers = lk_get_modifiers ();
    full_display = lk_build_key_name (lk_keys[idx].display, modifiers);

    {
        WDialog *d;
        gboolean seq_ok = FALSE;

        d = create_message (D_NORMAL, _ ("Learn key"), _ ("Press %s and wait..."), full_display);
        mc_refresh ();

        disable_mouse ();
        seq = learn_key ();
        enable_mouse ();

        dlg_run_done (d);
        widget_destroy (WIDGET (d));

        if (seq == NULL || strcmp (seq, "\\e") == 0 || strcmp (seq, "\\e\\e") == 0)
        {
            g_free (seq);
            g_free (full_display);
            return 0;
        }

        full_name = lk_build_key_name (lk_keys[idx].key_name, modifiers);
        keycode = tty_keyname_to_keycode (full_name, NULL);
        g_free (full_name);

        if (keycode > 0)
        {
            char *esc_seq;
            int existing;

            esc_seq = convert_controls (seq);

            /* check if this sequence is already mapped to another key */
            existing = tty_match_seq_to_keycode (esc_seq, (int) strlen (esc_seq));
            if (existing > 0 && existing != keycode)
            {
                char *exist_name;

                exist_name = tty_keycode_to_keyname (existing);
                if (exist_name != NULL)
                {
                    char *msg;

                    msg = g_strdup_printf (_ ("Sequence %s is already assigned to %s.\n"
                                              "Reassign to %s?"),
                                           seq, exist_name, full_display);
                    if (query_dialog (_ ("Conflict"), msg, D_NORMAL, 2, _ ("&Yes"), _ ("&No")) != 0)
                    {
                        g_free (msg);
                        g_free (exist_name);
                        g_free (esc_seq);
                        g_free (seq);
                        g_free (full_display);
                        return 0;
                    }
                    g_free (msg);

                    /* mark old slot for deletion so lk_save removes it from file.
                       Extract modifiers from the existing keycode to find the right slot,
                       since the conflict may come from a different modifier set. */
                    {
                        int oi;
                        int old_mod = existing & KEY_M_MASK;

                        for (oi = 0; oi < lk_key_count; oi++)
                        {
                            char *slot_name;

                            slot_name = lk_build_key_name (lk_keys[oi].key_name, old_mod);
                            if (tty_keyname_to_keycode (slot_name, NULL) == existing)
                            {
                                g_free (lk_slots[oi].sequence);
                                lk_slots[oi].sequence = NULL;
                                lk_slots[oi].ok = FALSE;
                                lk_slots[oi].cleared = TRUE;
                                lk_slots[oi].clear_modifiers = old_mod;
                                lk_changed = TRUE;

                                if (lk_slots[oi].label != NULL)
                                    label_set_text (LABEL (lk_slots[oi].label), "--");

                                g_free (slot_name);
                                break;
                            }
                            g_free (slot_name);
                        }
                    }

                    g_free (exist_name);
                }
            }

            seq_ok = define_sequence (keycode, esc_seq, MCKEY_NOACTION);
            g_free (esc_seq);

            if (seq_ok)
            {
                char lbl[LK_SEQ_WIDTH + 1];

                g_free (lk_slots[idx].sequence);
                lk_slots[idx].sequence = g_strdup (seq);
                lk_slots[idx].ok = TRUE;
                lk_slots[idx].learned = TRUE;
                lk_slots[idx].learn_modifiers = modifiers;
                lk_changed = TRUE;

                g_snprintf (lbl, sizeof (lbl), "%.8s", seq);
                label_set_text (LABEL (lk_slots[idx].label), lbl);
            }
        }

        if (!seq_ok)
            message (D_NORMAL, _ ("Warning"), _ ("Cannot accept this key.\nYou pressed: %s"), seq);

        g_free (seq);
    }

    g_free (full_display);

    return 0; /* do not close dialog */
}

/* --------------------------------------------------------------------------------------------- */

static void
lk_save (void)
{
    int i;
    char *term_file;
    mc_config_t *term_cfg;
    gboolean changed = FALSE;

    term_file = mc_term_keys_path ();

    term_cfg = mc_config_init (term_file, FALSE);

    for (i = 0; i < lk_key_count; i++)
    {
        /* use the modifiers under which the slot was learned/cleared,
           not the currently active checkboxes */
        int slot_mod;

        if (lk_slots[i].learned)
            slot_mod = lk_slots[i].learn_modifiers;
        else if (lk_slots[i].cleared)
            slot_mod = lk_slots[i].clear_modifiers;
        else
            slot_mod = lk_get_modifiers ();

        if (lk_slots[i].cleared)
        {
            char *full_name;

            full_name = lk_build_key_name (lk_keys[i].key_name, slot_mod);
            mc_config_del_key (term_cfg, "keys", full_name);
            g_free (full_name);
            changed = TRUE;
        }
        else if (lk_slots[i].learned && lk_slots[i].sequence != NULL && lk_slots[i].ok)
        {
            char *esc_str;
            char *full_name;

            esc_str = str_escape (lk_slots[i].sequence, -1, "\\", TRUE);
            full_name = lk_build_key_name (lk_keys[i].key_name, slot_mod);
            mc_config_set_string_raw_value (term_cfg, "keys", full_name, esc_str);
            g_free (full_name);
            g_free (esc_str);
            changed = TRUE;
        }
    }

    if (changed)
        mc_config_save_file (term_cfg, NULL);

    mc_config_deinit (term_cfg);
    g_free (term_file);

    /* clear learned state after save */
    for (i = 0; i < lk_key_count; i++)
    {
        lk_slots[i].learned = FALSE;
        lk_slots[i].cleared = FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
lk_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_KEY:
        if (parm == KEY_DC)
        {
            /* find focused key button and clear its learned sequence */
            Widget *current = WIDGET (GROUP (w)->current->data);
            int i;

            for (i = 0; i < lk_key_count; i++)
            {
                if (lk_slots != NULL && lk_slots[i].button == current)
                {
                    /* mark for deletion on save */
                    g_free (lk_slots[i].sequence);
                    lk_slots[i].sequence = NULL;
                    lk_slots[i].ok = FALSE;
                    lk_slots[i].cleared = TRUE;
                    lk_slots[i].clear_modifiers = lk_get_modifiers ();
                    lk_changed = TRUE;

                    label_set_text (LABEL (lk_slots[i].label), "--");
                    return MSG_HANDLED;
                }
            }
        }
        return MSG_NOT_HANDLED;

    case MSG_NOTIFY:
        /* checkbox toggled -- refresh all slots */
        lk_update_all ();
        return MSG_HANDLED;

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
key_learn (void)
{
    WGroup *g;
    WCheck *chk_ctrl, *chk_alt, *chk_shift;
    WHLine *hl;
    int y, i;
    int result;
    int section;
    gboolean save_old_esc_mode = old_esc_mode;
    gboolean save_alt_plus_minus = mc_global.tty.alternate_plus_minus;
    char title[80];

    old_esc_mode = FALSE;
    mc_global.tty.alternate_plus_minus = TRUE;

    g_snprintf (title, sizeof (title), _ ("Learn terminal [%s]"),
                getenv ("TERM") != NULL ? getenv ("TERM") : "unknown");

    lk_dlg = dlg_create (TRUE, 0, 0, LK_DLG_HEIGHT, LK_DLG_WIDTH, WPOS_CENTER, FALSE, dialog_colors,
                         lk_dlg_callback, NULL, "[Learn keys]", title);
    g = GROUP (lk_dlg);

    y = 2;

    /* modifier checkboxes */
    chk_ctrl = check_new (y, 3, FALSE, _ ("&Ctrl"));
    chk_alt = check_new (y, 16, FALSE, _ ("&Alt"));
    chk_shift = check_new (y, 28, FALSE, _ ("&Shift"));
    lk_mod_ctrl_id = WIDGET (chk_ctrl)->id;
    lk_mod_alt_id = WIDGET (chk_alt)->id;
    lk_mod_shift_id = WIDGET (chk_shift)->id;
    group_add_widget (g, chk_ctrl);
    group_add_widget (g, chk_alt);
    group_add_widget (g, chk_shift);
    y++;

    /* allocate slots */
    lk_slots = g_new0 (lk_slot_t, lk_key_count);
    lk_changed = FALSE;

    /* build key buttons and labels by section */
    {
        int col = 0;
        int prev_section = -1;

        for (i = 0; i < lk_key_count; i++)
        {
            char btn_text[LK_BTN_WIDTH + 1];
            int bx;

            section = (int) lk_keys[i].section;

            /* section header hline */
            if (section != prev_section)
            {
                char *padded;

                if (col != 0)
                {
                    y++;
                    col = 0;
                }

                padded = g_strdup_printf (" %s ", _ (lk_section_names[section]));
                hl = hline_new (y, -1, -1);
                hline_set_text (hl, padded);
                g_free (padded);
                group_add_widget (g, hl);
                y++;
                prev_section = section;
            }

            bx = 3 + col * LK_CELL_WIDTH;

            /* button with padded name */
            g_snprintf (btn_text, sizeof (btn_text), "%-5s", lk_keys[i].display);
            lk_slots[i].button = WIDGET (
                button_new (y, bx, LK_KEY_BASE + i, NARROW_BUTTON, btn_text, lk_button_callback));
            group_add_widget (g, lk_slots[i].button);

            /* label for sequence */
            lk_slots[i].label = WIDGET (label_new (y, bx + LK_BTN_WIDTH + 1, "--"));
            group_add_widget (g, lk_slots[i].label);

            col++;
            if (col >= LK_GRID_COLS)
            {
                col = 0;
                y++;
            }
        }

        if (col != 0)
            y++;
    }

    /* hline before buttons */
    group_add_widget (g, hline_new (y, -1, -1));
    y++;

    /* buttons */
    {
        int save_w = str_term_width1 (_ ("&Save")) + 4;
        int edit_w = str_term_width1 (_ ("Edit &term file")) + 4;
        int close_w = str_term_width1 (_ ("&Close")) + 4;
        int gap = 2;
        int total_w = save_w + gap + edit_w + gap + close_w;
        int bx = (LK_DLG_WIDTH - total_w) / 2;

        group_add_widget (g, button_new (y, bx, B_ENTER, NORMAL_BUTTON, _ ("&Save"), NULL));
        bx += save_w + gap;
        group_add_widget (
            g, button_new (y, bx, LK_EDIT_TERM, NORMAL_BUTTON, _ ("Edit &term file"), NULL));
        bx += edit_w + gap;
        group_add_widget (g, button_new (y, bx, B_CANCEL, NORMAL_BUTTON, _ ("&Close"), NULL));
    }

    /* probe initial state */
    lk_update_all ();

    result = dlg_run (lk_dlg);

    if (result == B_ENTER && lk_changed)
        lk_save ();

    /* cleanup */
    for (i = 0; i < lk_key_count; i++)
        g_free (lk_slots[i].sequence);
    g_free (lk_slots);
    lk_slots = NULL;

    widget_destroy (WIDGET (lk_dlg));
    lk_dlg = NULL;

    if (result == LK_EDIT_TERM)
    {
#ifdef USE_INTERNAL_EDIT
        char *term_path;
        edit_arg_t *arg;

        term_path = mc_term_keys_path ();
        arg = edit_arg_new (term_path, 0);
        edit_file (arg);
        edit_arg_free (arg);
        g_free (term_path);
#endif
    }

    old_esc_mode = save_old_esc_mode;
    mc_global.tty.alternate_plus_minus = save_alt_plus_minus;

    repaint_screen ();
}

/* --------------------------------------------------------------------------------------------- */
