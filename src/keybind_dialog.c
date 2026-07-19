/*
   Key bindings configuration dialog.

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
#include "lib/fileloc.h"
#include "lib/keybind.h"
#include "lib/mcconfig.h"
#include "lib/terminal.h"
#include "lib/tty/tty.h"
#include "lib/tty/key.h"
#include "lib/tty/mouse.h"
#include "lib/widget.h"
#include "lib/widget/table.h"

#ifdef USE_INTERNAL_EDIT
#include "src/editor/edit.h"
#endif
#include "setup.h"
#include "keymap.h"
#include "keybind_dialog.h"
#include "key_learn.h"

/*** file scope macro definitions ****************************************************************/

#define KBD_TABLE_COLS  3

#define KBD_COL_ACTION  16
#define KBD_COL_KEY     20

#define KBD_KEY_COL     1

#define KBD_TAB_BASE    (B_USER + 100)
#define KBD_TAB_BACK    (B_USER + 90)
#define KBD_EDIT_FILE   (B_USER + 91)
#define KBD_EDIT_TERM   (B_USER + 92)
#define KBD_CAPTURE     (B_USER + 93)
#define KBD_CAPTURE_ADD (B_USER + 94)

/*** file scope type declarations ****************************************************************/

typedef struct
{
    const char *label;
    const char *section;
} keybind_section_t;

typedef struct
{
    const char *label;
    int nsections;
    const keybind_section_t *sections;
} keybind_group_t;

typedef struct
{
    const char *section; /* keymap section name */
    long command;
    char *key_name;
} keybind_change_t;

/* action entry in the dictionary */
typedef struct
{
    char *action_name;
    char *desc;
    long command;
    GPtrArray *keys; /* char* key names */
    gboolean overridden;
    gboolean dirty; /* modified since load */
} kbd_action_t;

/* mapping: table row -> action index + key index */
typedef struct
{
    int action_idx;
    int key_idx; /* -1 = primary row (action + first key), >=0 = sub-row */
} kbd_row_map_t;

/*** forward declarations (file scope functions) *************************************************/

static const global_keymap_t *keybind_find_map (const char *section);

/*** file scope variables ************************************************************************/

static WTable *kbd_table = NULL;
static GPtrArray *kbd_changes = NULL;
static int kbd_dlg_width = 0;
static int kbd_capture_row = -1;

/* dictionary: sorted array of kbd_action_t */
static GArray *kbd_dict = NULL;
static const char *kbd_dict_section = NULL; /* section that dict was built for */
/* row map: table row -> (action_idx, key_idx) */
static GArray *kbd_row_map = NULL;

/* current navigation state */
static int kbd_group_idx = 0;    /* top-level group */
static int kbd_section_idx = -1; /* -1 = show group tabs, >=0 = inside subtab */
static const char *kbd_current_section = NULL;

/* --- Section definitions --- */

static const keybind_section_t filemanager_sections[] = {
    { N_ ("&Main"), KEYMAP_SECTION_FILEMANAGER },
    { N_ ("&Ctrl-X + ..."), KEYMAP_SECTION_FILEMANAGER_EXT },
};

static const keybind_section_t panel_sections[] = {
    { N_ ("&Panel"), KEYMAP_SECTION_PANEL },
};

static const keybind_section_t editor_sections[] = {
    { N_ ("&Main"), KEYMAP_SECTION_EDITOR },
#ifdef USE_INTERNAL_EDIT
    { N_ ("&Ctrl-X + ..."), KEYMAP_SECTION_EDITOR_EXT },
#endif
};

static const keybind_section_t viewer_sections[] = {
    { N_ ("&Normal"), KEYMAP_SECTION_VIEWER },
    { N_ ("&Hex"), KEYMAP_SECTION_VIEWER_HEX },
    { N_ ("&Structured"), KEYMAP_SECTION_VIEWER_STRUCT },
};

#ifdef USE_DIFF_VIEW
static const keybind_section_t diff_sections[] = {
    { N_ ("&Diff"), KEYMAP_SECTION_DIFFVIEWER },
};
#endif

static const keybind_section_t widget_sections[] = {
    { N_ ("&Dialog"), KEYMAP_SECTION_DIALOG }, { N_ ("&Menu"), KEYMAP_SECTION_MENU },
    { N_ ("&Input"), KEYMAP_SECTION_INPUT },   { N_ ("&Listbox"), KEYMAP_SECTION_LISTBOX },
    { N_ ("&Radio"), KEYMAP_SECTION_RADIO },   { N_ ("&Tree"), KEYMAP_SECTION_TREE },
    { N_ ("&Help"), KEYMAP_SECTION_HELP },
#ifdef ENABLE_EXT2FS_ATTR
    { N_ ("c&Hattr"), KEYMAP_SECTION_CHATTR },
#endif
};

static const keybind_group_t keybind_groups[] = {
    { N_ ("&Panel"), G_N_ELEMENTS (panel_sections), panel_sections },
    { N_ ("&Filemanager"), G_N_ELEMENTS (filemanager_sections), filemanager_sections },
    { N_ ("&Editor"), G_N_ELEMENTS (editor_sections), editor_sections },
    { N_ ("&Viewer"), G_N_ELEMENTS (viewer_sections), viewer_sections },
#ifdef USE_DIFF_VIEW
    { N_ ("&Diff"), G_N_ELEMENTS (diff_sections), diff_sections },
#endif
    { N_ ("&Widgets"), G_N_ELEMENTS (widget_sections), widget_sections },
};

static const int keybind_group_count = G_N_ELEMENTS (keybind_groups);

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
keybind_change_free (gpointer p)
{
    keybind_change_t *c = (keybind_change_t *) p;
    g_free (c->key_name);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

static void
kbd_action_clear (kbd_action_t *a)
{
    g_free (a->action_name);
    g_free (a->desc);
    if (a->keys != NULL)
        g_ptr_array_free (a->keys, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static int
kbd_action_compare (const void *a, const void *b)
{
    const kbd_action_t *aa = (const kbd_action_t *) a;
    const kbd_action_t *bb = (const kbd_action_t *) b;

    return g_ascii_strcasecmp (aa->action_name, bb->action_name);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Build the dictionary from keymap for current section.
 * Groups entries by command, collects all keys per command.
 */
static void
kbd_dict_build (void)
{
    const global_keymap_t *keymap;
    size_t i;
    mc_config_t *user_cfg = NULL;
    char *user_keymap_path;

    if (kbd_dict != NULL)
    {
        for (i = 0; i < kbd_dict->len; i++)
            kbd_action_clear (&g_array_index (kbd_dict, kbd_action_t, i));
        g_array_set_size (kbd_dict, 0);
    }
    else
        kbd_dict = g_array_new (FALSE, FALSE, sizeof (kbd_action_t));

    keymap = keybind_find_map (kbd_current_section);
    if (keymap == NULL)
        return;

    /* detect overrides */
    user_keymap_path = mc_config_get_full_path (GLOBAL_KEYMAP_FILE);
    if (g_file_test (user_keymap_path, G_FILE_TEST_EXISTS))
        user_cfg = mc_config_init (user_keymap_path, TRUE);
    g_free (user_keymap_path);

    for (i = 0; keymap[i].key != 0; i++)
    {
        const char *action_name;
        const char *caption;
        guint gi;
        gboolean found = FALSE;

        action_name = keybind_lookup_actionname (keymap[i].command);
        if (action_name == NULL)
            continue;

        caption = (keymap[i].caption[0] != '\0') ? keymap[i].caption : NULL;

        /* find existing entry */
        for (gi = 0; gi < kbd_dict->len; gi++)
        {
            kbd_action_t *a = &g_array_index (kbd_dict, kbd_action_t, gi);

            if (a->command == keymap[i].command)
            {
                if (caption != NULL)
                {
                    /* skip duplicate keys (case-insensitive) */
                    guint ki;
                    gboolean dup = FALSE;

                    for (ki = 0; ki < a->keys->len; ki++)
                    {
                        if (g_ascii_strcasecmp ((const char *) g_ptr_array_index (a->keys, ki),
                                                caption)
                            == 0)
                        {
                            dup = TRUE;
                            break;
                        }
                    }
                    if (!dup)
                        g_ptr_array_add (a->keys, g_strdup (caption));
                }
                found = TRUE;
                break;
            }
        }

        if (!found)
        {
            kbd_action_t ne;
            const char *desc;

            desc = keybind_lookup_actiondesc (keymap[i].command);

            ne.action_name = g_strdup (action_name);
            ne.desc = g_strdup ((desc != NULL) ? _ (desc) : action_name);
            ne.command = keymap[i].command;
            ne.keys = g_ptr_array_new_with_free_func (g_free);
            ne.overridden = FALSE;
            ne.dirty = FALSE;

            if (user_cfg != NULL && kbd_current_section != NULL)
                ne.overridden = mc_config_has_param (user_cfg, kbd_current_section, action_name);

            if (caption != NULL)
                g_ptr_array_add (ne.keys, g_strdup (caption));

            g_array_append_val (kbd_dict, ne);
        }
    }

    /* sort */
    qsort (kbd_dict->data, kbd_dict->len, sizeof (kbd_action_t), kbd_action_compare);

    kbd_dict_section = kbd_current_section;

    if (user_cfg != NULL)
        mc_config_deinit (user_cfg);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Build row map from dictionary.
 */
static void
kbd_dict_fill_table (void)
{
    guint i;

    if (kbd_row_map != NULL)
        g_array_set_size (kbd_row_map, 0);
    else
        kbd_row_map = g_array_new (FALSE, FALSE, sizeof (kbd_row_map_t));

    if (kbd_dict == NULL)
        return;

    for (i = 0; i < kbd_dict->len; i++)
    {
        kbd_action_t *a = &g_array_index (kbd_dict, kbd_action_t, i);
        kbd_row_map_t rm;
        guint ki;

        /* primary row */
        rm.action_idx = (int) i;
        rm.key_idx = 0;
        g_array_append_val (kbd_row_map, rm);

        /* sub-rows for additional keys */
        for (ki = 1; ki < a->keys->len; ki++)
        {
            rm.action_idx = (int) i;
            rm.key_idx = (int) ki;
            g_array_append_val (kbd_row_map, rm);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
kbd_table_get_nrows (const void *data)
{
    (void) data;

    if (kbd_row_map == NULL)
        return 0;

    return (int) kbd_row_map->len;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
kbd_table_get_text (const void *data, int row, int col)
{
    static char buf[128];
    kbd_row_map_t *map;
    kbd_action_t *a;

    (void) data;

    if (kbd_row_map == NULL || row < 0 || row >= (int) kbd_row_map->len)
        return "";

    map = &g_array_index (kbd_row_map, kbd_row_map_t, (guint) row);

    if (kbd_dict == NULL || map->action_idx < 0 || map->action_idx >= (int) kbd_dict->len)
        return "";

    a = &g_array_index (kbd_dict, kbd_action_t, (guint) map->action_idx);

    switch (col)
    {
    case 0: /* action name */
        if (map->key_idx > 0)
            return "";
        if (a->overridden || a->dirty)
        {
            g_snprintf (buf, sizeof (buf), "*%s", a->action_name);
            return buf;
        }
        return a->action_name;

    case 1: /* key */
        if (map->key_idx == 0)
        {
            if (a->keys->len > 0)
                return (const char *) g_ptr_array_index (a->keys, 0);
            return "";
        }
        else
        {
            const char *extra_key;
            char conn_buf[8];
            int frm_char;
            int len;

            if (map->key_idx < 0 || map->key_idx >= (int) a->keys->len)
                return "";

            extra_key = (const char *) g_ptr_array_index (a->keys, (guint) map->key_idx);
            frm_char = ((guint) map->key_idx == a->keys->len - 1)
                ? mc_tty_frm[MC_TTY_FRM_LEFTBOTTOM]
                : mc_tty_frm[MC_TTY_FRM_LEFTMIDDLE];
            conn_buf[0] = ' ';
            len = g_unichar_to_utf8 ((gunichar) frm_char, conn_buf + 1);
            conn_buf[1 + len] = '\0';
            g_snprintf (buf, sizeof (buf), "%s %s", conn_buf, extra_key);
            return buf;
        }

    case 2: /* description */
        if (map->key_idx > 0)
            return "";
        return a->desc;

    default:
        return "";
    }
}

/* --------------------------------------------------------------------------------------------- */

static const global_keymap_t *
keybind_find_map (const char *section)
{
    if (section == NULL)
        return NULL;

    if (strcmp (section, KEYMAP_SECTION_FILEMANAGER) == 0)
        return filemanager_map;
    if (strcmp (section, KEYMAP_SECTION_FILEMANAGER_EXT) == 0)
        return filemanager_x_map;
    if (strcmp (section, KEYMAP_SECTION_PANEL) == 0)
        return panel_map;
    if (strcmp (section, KEYMAP_SECTION_DIALOG) == 0)
        return (const global_keymap_t *) dialog_keymap->data;
    if (strcmp (section, KEYMAP_SECTION_MENU) == 0)
        return (const global_keymap_t *) menu_keymap->data;
    if (strcmp (section, KEYMAP_SECTION_INPUT) == 0)
        return (const global_keymap_t *) input_keymap->data;
    if (strcmp (section, KEYMAP_SECTION_LISTBOX) == 0)
        return (const global_keymap_t *) listbox_keymap->data;
    if (strcmp (section, KEYMAP_SECTION_RADIO) == 0)
        return (const global_keymap_t *) radio_keymap->data;
    if (strcmp (section, KEYMAP_SECTION_TREE) == 0)
        return tree_map;
    if (strcmp (section, KEYMAP_SECTION_HELP) == 0)
        return help_map;
#ifdef ENABLE_EXT2FS_ATTR
    if (strcmp (section, KEYMAP_SECTION_CHATTR) == 0)
        return chattr_map;
#endif
#ifdef USE_INTERNAL_EDIT
    if (strcmp (section, KEYMAP_SECTION_EDITOR) == 0)
        return editor_map;
    if (strcmp (section, KEYMAP_SECTION_EDITOR_EXT) == 0)
        return editor_x_map;
#endif
    if (strcmp (section, KEYMAP_SECTION_VIEWER) == 0)
        return viewer_map;
    if (strcmp (section, KEYMAP_SECTION_VIEWER_HEX) == 0)
        return viewer_hex_map;
    if (strcmp (section, KEYMAP_SECTION_VIEWER_STRUCT) == 0)
        return viewer_struct_map;
#ifdef USE_DIFF_VIEW
    if (strcmp (section, KEYMAP_SECTION_DIFFVIEWER) == 0)
        return diff_map;
#endif
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static long
keybind_get_command_for_row (int row)
{
    kbd_row_map_t *rm;

    if (kbd_row_map == NULL || row < 0 || row >= (int) kbd_row_map->len)
        return CK_IgnoreKey;

    rm = &g_array_index (kbd_row_map, kbd_row_map_t, (guint) row);
    if (rm->action_idx < 0 || rm->action_idx >= (int) kbd_dict->len)
        return CK_IgnoreKey;

    return g_array_index (kbd_dict, kbd_action_t, (guint) rm->action_idx).command;
}

/* --------------------------------------------------------------------------------------------- */

static int
keybind_get_key_idx_for_row (int row)
{
    if (kbd_row_map == NULL || row < 0 || row >= (int) kbd_row_map->len)
        return -1;

    return g_array_index (kbd_row_map, kbd_row_map_t, (guint) row).key_idx;
}

/* --------------------------------------------------------------------------------------------- */

static int
keybind_get_action_idx_for_row (int row)
{
    if (kbd_row_map == NULL || row < 0 || row >= (int) kbd_row_map->len)
        return -1;

    return g_array_index (kbd_row_map, kbd_row_map_t, (guint) row).action_idx;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

static void
keybind_record_change (long command, const char *key_name, int key_idx)
{
    guint i;
    gboolean found = FALSE;

    for (i = 0; i < kbd_changes->len; i++)
    {
        keybind_change_t *c = (keybind_change_t *) g_ptr_array_index (kbd_changes, i);
        if (c->section == kbd_current_section && c->command == command)
        {
            g_free (c->key_name);
            c->key_name = (key_name != NULL) ? g_strdup (key_name) : NULL;
            found = TRUE;
            break;
        }
    }

    if (!found)
    {
        keybind_change_t *c = g_new0 (keybind_change_t, 1);
        c->section = kbd_current_section;
        c->command = command;
        c->key_name = (key_name != NULL) ? g_strdup (key_name) : NULL;
        g_ptr_array_add (kbd_changes, c);
    }

    /* mark the corresponding kbd_dict entry as dirty and update its key */
    if (kbd_dict != NULL && key_name != NULL)
    {
        guint di;

        for (di = 0; di < kbd_dict->len; di++)
        {
            kbd_action_t *a = &g_array_index (kbd_dict, kbd_action_t, di);

            if (a->command == command)
            {
                a->dirty = TRUE;

                if (key_idx >= 0 && key_idx < (int) a->keys->len)
                {
                    g_free (g_ptr_array_index (a->keys, (guint) key_idx));
                    a->keys->pdata[key_idx] = g_strdup (key_name);
                }
                else
                    g_ptr_array_add (a->keys, g_strdup (key_name));
                break;
            }
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
keybind_table_fill (void)
{
    /* rebuild dictionary only if section changed or first time */
    if (kbd_dict == NULL || kbd_dict->len == 0 || kbd_dict_section != kbd_current_section)
        kbd_dict_build ();
    kbd_dict_fill_table ();
}

/* --------------------------------------------------------------------------------------------- */

static int
ks_key_pick_btn (WButton *button, int action)
{
    static const char *ks_keys[] = {
        "F1",   "F2",  "F3",  "F4",  "F5",   "F6",   "F7", "F8",   "F9",   "F10",   "F11", "F12",
        "Home", "End", "Ins", "Del", "PgUp", "PgDn", "Up", "Down", "Left", "Right", "Tab", "BkSpc",
    };
    static const int n_keys = G_N_ELEMENTS (ks_keys);

    Widget *w = WIDGET (button);
    WDialog *pick_dlg;
    WListbox *pick_list;
    int result;
    int fe;

    (void) action;

    pick_dlg = dlg_create (TRUE, w->rect.y - 1, w->rect.x, 14, 14, WPOS_KEEP_DEFAULT, TRUE,
                           dialog_colors, NULL, NULL, "[Learn keys]", _ ("Key"));
    pick_list = listbox_new (1, 1, 12, 12, FALSE, NULL);

    {
        int i;

        for (i = 0; i < n_keys; i++)
            listbox_add_item (pick_list, LISTBOX_APPEND_AT_END, 0, ks_keys[i], NULL, FALSE);
    }

    fe = listbox_search_text (pick_list, button_get_text (button));
    if (fe >= 0)
        listbox_set_current (pick_list, fe);

    group_add_widget (GROUP (pick_dlg), pick_list);
    result = dlg_run (pick_dlg);

    if (result != B_CANCEL)
    {
        char *sel_text = NULL;

        listbox_get_current (pick_list, &sel_text, NULL);
        if (sel_text != NULL)
            button_set_text (button, sel_text);
    }

    widget_destroy (WIDGET (pick_dlg));
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* Inline learn dialog: user picks modifiers + key, sequence is registered and binding assigned */
static void
keybind_inline_learn (const char *raw_seq, int row)
{
    static const char *learnable_names[] = {
        "f1",   "f2",   "f3",  "f4",   "f5",   "f6",    "f7",     "f8",
        "f9",   "f10",  "f11", "f12",  "home", "end",   "insert", "delete",
        "pgup", "pgdn", "up",  "down", "left", "right", "tab",    "backspace",
    };
    static const char *learnable_display[] = {
        "F1",   "F2",  "F3",  "F4",  "F5",   "F6",   "F7", "F8",   "F9",   "F10",   "F11", "F12",
        "Home", "End", "Ins", "Del", "PgUp", "PgDn", "Up", "Down", "Left", "Right", "Tab", "BkSpc",
    };
    static const int n_learnable = G_N_ELEMENTS (learnable_names);

    WDialog *ldlg;
    WGroup *lg;
    WCheck *chk_ctrl, *chk_alt, *chk_shift;
    WButton *key_btn;
    int ldlg_w = 44;
    int y;
    int ret;
    int ki;
    char seq_label[64];

    g_snprintf (seq_label, sizeof (seq_label), _ ("Unknown sequence: %s"), raw_seq);

    /* y tracks current row; final dialog height = y + 2 (for bottom frame + button row) */
    y = 2;

    ldlg = dlg_create (TRUE, 0, 0, 11, ldlg_w, WPOS_CENTER, FALSE, dialog_colors, NULL, NULL,
                       "[Learn keys]", _ ("Assign key"));
    lg = GROUP (ldlg);

    group_add_widget (lg, label_new (y++, 3, seq_label));
    group_add_widget (lg, hline_new (y++, -1, -1));

    chk_ctrl = check_new (y, 3, FALSE, _ ("&Ctrl"));
    chk_alt = check_new (y, 16, FALSE, _ ("&Alt"));
    chk_shift = check_new (y++, 28, FALSE, _ ("&Shift"));
    group_add_widget (lg, chk_ctrl);
    group_add_widget (lg, chk_alt);
    group_add_widget (lg, chk_shift);

    y++;
    group_add_widget (lg, label_new (y, 3, _ ("Key:")));
    key_btn =
        button_new (y++, 9, B_USER + 50, NORMAL_BUTTON, learnable_display[0], ks_key_pick_btn);
    group_add_widget (lg, key_btn);

    group_add_widget (lg, hline_new (y++, -1, -1));
    {
        int ok_w = str_term_width1 (_ ("&OK")) + 5;
        int cancel_w = str_term_width1 (_ ("&Cancel")) + 3;
        int b12 = ok_w + cancel_w + 1;
        int bx = (ldlg_w - b12) / 2;

        group_add_widget (lg, button_new (y, bx, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
        group_add_widget (
            lg, button_new (y, bx + ok_w + 1, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));
    }

    ret = dlg_run (ldlg);

    {
        const char *selected_key;
        int sel_idx = -1;
        int modifiers = 0;

        selected_key = button_get_text (key_btn);

        for (ki = 0; ki < n_learnable; ki++)
        {
            if (g_ascii_strcasecmp (selected_key, learnable_display[ki]) == 0)
            {
                sel_idx = ki;
                break;
            }
        }

        if (chk_ctrl->state)
            modifiers |= KEY_M_CTRL;
        if (chk_alt->state)
            modifiers |= KEY_M_ALT;
        if (chk_shift->state)
            modifiers |= KEY_M_SHIFT;

        widget_destroy (WIDGET (ldlg));

        if (ret != B_ENTER)
            return;

        if (sel_idx >= 0 && sel_idx < n_learnable)
        {
            char *full_name_learn;
            int keycode_learn;

            full_name_learn = tty_build_key_name (learnable_names[sel_idx], modifiers);
            keycode_learn = tty_keyname_to_keycode (full_name_learn, NULL);

            if (keycode_learn > 0)
            {
                long command;

                /* remember learned sequence for saving later */
                {
                    keybind_change_t *lc = g_new0 (keybind_change_t, 1);

                    lc->section = NULL;
                    lc->command = keycode_learn;
                    lc->key_name = g_strdup (raw_seq);
                    g_ptr_array_add (kbd_changes, lc);
                }

                command = keybind_get_command_for_row (row);
                if (command != CK_IgnoreKey)
                {
                    int kix = keybind_get_key_idx_for_row (row);
                    keybind_record_change (command, full_name_learn, kix);
                }
            }

            g_free (full_name_learn);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
keybind_clear_key (void)
{
    int row;
    int action_idx, key_idx;
    kbd_action_t *a;

    if (kbd_table == NULL || kbd_dict == NULL)
        return;

    row = table_get_current (kbd_table);
    action_idx = keybind_get_action_idx_for_row (row);
    key_idx = keybind_get_key_idx_for_row (row);

    if (action_idx < 0 || action_idx >= (int) kbd_dict->len)
        return;

    a = &g_array_index (kbd_dict, kbd_action_t, (guint) action_idx);
    a->dirty = TRUE;

    if (key_idx >= 0 && key_idx < (int) a->keys->len)
    {
        g_ptr_array_remove_index (a->keys, (guint) key_idx);
        kbd_dict_fill_table ();
        table_set_current (kbd_table, row);
    }

    widget_draw (WIDGET (kbd_table));
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
keybind_save_changes (void)
{
    char *fname;
    mc_config_t *cfg;
    guint i;
    gboolean has_dirty = FALSE;

    if (kbd_dict == NULL)
        return TRUE;

    /* check if anything changed */
    for (i = 0; i < kbd_dict->len; i++)
    {
        if (g_array_index (kbd_dict, kbd_action_t, i).dirty)
        {
            has_dirty = TRUE;
            break;
        }
    }

    if (!has_dirty)
        return TRUE;

    fname = mc_config_get_full_path (GLOBAL_KEYMAP_FILE);
    cfg = mc_config_init (fname, FALSE);

    for (i = 0; i < kbd_dict->len; i++)
    {
        kbd_action_t *a = &g_array_index (kbd_dict, kbd_action_t, i);

        if (!a->dirty)
            continue;

        if (kbd_current_section == NULL)
            continue;

        if (a->keys->len > 0)
        {
            GString *val;
            guint ki;

            val = g_string_new ("");
            for (ki = 0; ki < a->keys->len; ki++)
            {
                if (ki > 0)
                    g_string_append (val, "; ");
                g_string_append (val, (const char *) g_ptr_array_index (a->keys, ki));
            }
            mc_config_set_string (cfg, kbd_current_section, a->action_name, val->str);
            g_string_free (val, TRUE);
        }
        else
            mc_config_del_key (cfg, kbd_current_section, a->action_name);

        a->dirty = FALSE;
        a->overridden = TRUE;
    }

    mc_config_save_file (cfg, NULL);
    mc_config_deinit (cfg);
    g_free (fname);

    /* save learned terminal sequences to term keys file */
    {
        char *term_file;
        mc_config_t *term_cfg = NULL;
        gboolean term_changed = FALSE;

        term_file = mc_term_keys_path ();
        term_cfg = mc_config_init (term_file, FALSE);

        for (i = 0; i < kbd_changes->len; i++)
        {
            keybind_change_t *lc = (keybind_change_t *) g_ptr_array_index (kbd_changes, i);

            if (lc->section != NULL)
                continue; /* not a learned seq */

            if (lc->key_name != NULL && lc->command > 0)
            {
                char *key_label;

                key_label = tty_keycode_to_keyname (lc->command);
                if (key_label != NULL)
                {
                    char *esc_str;

                    esc_str = str_escape (lc->key_name, -1, "\\", TRUE);
                    mc_config_set_string_raw_value (term_cfg, "keys", key_label, esc_str);
                    g_free (esc_str);
                    term_changed = TRUE;
                }
                g_free (key_label);
            }
        }

        if (term_changed)
            mc_config_save_file (term_cfg, NULL);
        mc_config_deinit (term_cfg);
        g_free (term_file);
    }

    /* reload keymap arrays (safe -- does not call load_key_defs/define_sequence) */
    keymap_save_old_maps ();
    keymap_free ();
    keymap_load (TRUE);
    keymap_refresh_widgets ();

    /* force dictionary rebuild on next table fill */
    kbd_dict_section = NULL;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
keybind_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_KEY:
        if (kbd_table != NULL && widget_get_state (WIDGET (kbd_table), WST_FOCUSED))
        {
            if (parm == '\n' || parm == KEY_ENTER || parm == KEY_F (5))
            {
                /* inline capture: show prompt over current dialog */
                gboolean append_mode = (parm == KEY_F (5));
                WDialog *prompt;
                char *seq;
                int capture_row;

                capture_row = table_get_current (kbd_table);

                prompt = create_message (D_NORMAL, _ ("Set shortcut"), "%s",
                                         _ ("Press new shortcut..."));
                mc_refresh ();

                seq = learn_key ();

                dlg_run_done (prompt);
                widget_destroy (WIDGET (prompt));

                if (seq != NULL && strcmp (seq, "\\e") != 0 && strcmp (seq, "\\e\\e") != 0
                    && strcmp (seq, "^m") != 0 && strcmp (seq, "^i") != 0)
                {
                    char *raw;
                    int keycode;

                    raw = convert_controls (seq);
                    keycode = tty_match_seq_to_keycode (raw, (int) strlen (raw));

                    if (keycode == 0 && strlen (raw) == 1)
                    {
                        unsigned char c = (unsigned char) raw[0];

                        keycode = (c < 32) ? (KEY_M_CTRL | (c + 'a' - 1)) : (int) c;
                    }
                    if (keycode == 0 && strlen (raw) == 2 && raw[0] == '\x1b')
                        keycode = KEY_M_ALT | (int) (unsigned char) raw[1];

                    g_free (raw);

                    if (keycode > 0)
                    {
                        char *key_name;
                        int action_idx;

                        key_name = tty_keycode_to_keyname (keycode);
                        action_idx = keybind_get_action_idx_for_row (capture_row);

                        if (action_idx >= 0 && key_name != NULL)
                        {
                            kbd_action_t *a =
                                &g_array_index (kbd_dict, kbd_action_t, (guint) action_idx);

                            /* warn if this key is already bound to another action */
                            {
                                guint ci;
                                gboolean conflict = FALSE;

                                for (ci = 0; ci < kbd_dict->len && !conflict; ci++)
                                {
                                    kbd_action_t *other =
                                        &g_array_index (kbd_dict, kbd_action_t, ci);
                                    guint ki;

                                    if (other->command == a->command)
                                        continue;

                                    for (ki = 0; ki < other->keys->len; ki++)
                                    {
                                        int other_kc;

                                        other_kc = tty_keyname_to_keycode (
                                            (const char *) g_ptr_array_index (other->keys, ki),
                                            NULL);
                                        if (other_kc > 0 && other_kc == keycode)
                                        {
                                            char *warn_msg;
                                            int answer;

                                            warn_msg =
                                                g_strdup_printf (_ ("%s is already bound to %s.\n"
                                                                    "Reassign anyway?"),
                                                                 key_name, other->desc);
                                            answer =
                                                query_dialog (_ ("Warning"), warn_msg, D_NORMAL, 2,
                                                              _ ("&Yes"), _ ("&No"));
                                            g_free (warn_msg);

                                            if (answer != 0)
                                            {
                                                g_free (key_name);
                                                /* seq is freed at capture_done */
                                                goto capture_done;
                                            }

                                            /* remove from the other action */
                                            g_ptr_array_remove_index (other->keys, ki);
                                            other->dirty = TRUE;
                                            conflict = TRUE;
                                            break;
                                        }
                                    }
                                }
                            }

                            a->dirty = TRUE;

                            if (append_mode)
                            {
                                g_ptr_array_add (a->keys, g_strdup (key_name));
                            }
                            else
                            {
                                int key_idx = keybind_get_key_idx_for_row (capture_row);

                                if (key_idx >= 0 && key_idx < (int) a->keys->len)
                                {
                                    g_free (g_ptr_array_index (a->keys, (guint) key_idx));
                                    a->keys->pdata[key_idx] = g_strdup (key_name);
                                }
                                else
                                    g_ptr_array_add (a->keys, g_strdup (key_name));
                            }

                            kbd_dict_fill_table ();
                            table_set_current (kbd_table, capture_row);
                        }
                        g_free (key_name);
                    }
                    else
                    {
                        keybind_inline_learn (seq, capture_row);
                        kbd_dict_fill_table ();
                        table_set_current (kbd_table, capture_row);
                    }
                }

            capture_done:
                g_free (seq);
                widget_draw (WIDGET (kbd_table));
                return MSG_HANDLED;
            }

            if (parm == KEY_DC || parm == KEY_F (8))
            {
                keybind_clear_key ();
                return MSG_HANDLED;
            }
        }
        return MSG_NOT_HANDLED;

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

static int
keybind_show_page (void)
{
    WDialog *dlg;
    WGroup *g;
    int dlg_height;
    int table_height;
    int y, x, i;
    int desc_width;
    int ret;

    table_column_def_t col_defs[KBD_TABLE_COLS];

    kbd_dlg_width = COLS - 4;
    if (kbd_dlg_width < 80)
        kbd_dlg_width = 80;
    if (kbd_dlg_width > 140)
        kbd_dlg_width = 140;

    desc_width = kbd_dlg_width - KBD_COL_ACTION - KBD_COL_KEY - 8;
    if (desc_width < 20)
        desc_width = 20;

    col_defs[0].width = KBD_COL_ACTION;
    col_defs[0].align = J_LEFT;
    col_defs[0].type = TABLE_COL_TEXT;
    col_defs[1].width = KBD_COL_KEY;
    col_defs[1].align = J_LEFT;
    col_defs[1].type = TABLE_COL_TEXT;
    col_defs[2].width = desc_width;
    col_defs[2].align = J_LEFT;
    col_defs[2].type = TABLE_COL_TEXT;

    dlg_height = LINES - 4;
    if (dlg_height < 16)
        dlg_height = 16;

    table_height = dlg_height - 8;

    dlg = dlg_create (TRUE, 0, 0, dlg_height, kbd_dlg_width, WPOS_CENTER, FALSE, dialog_colors,
                      keybind_dlg_callback, NULL, "[Key Bindings]", _ ("Key bindings"));
    g = GROUP (dlg);

    y = 2;
    x = 2;

    if (kbd_section_idx < 0)
    {
        /* top-level: show group tabs */
        for (i = 0; i < keybind_group_count; i++)
        {
            int btn_action = KBD_TAB_BASE + i;
            const char *lbl = _ (keybind_groups[i].label);
            WButton *btn = button_new (y, x, btn_action, NORMAL_BUTTON, lbl, NULL);

            group_add_widget (g, btn);
            x += str_term_width1 (lbl) + 4;
        }
    }
    else
    {
        /* sub-level: [Back] + section tabs */
        const keybind_group_t *grp = &keybind_groups[kbd_group_idx];
        WButton *back_btn = button_new (y, x, KBD_TAB_BACK, NORMAL_BUTTON, _ ("&Back"), NULL);

        group_add_widget (g, back_btn);
        x += str_term_width1 (_ ("&Back")) + 4;

        for (i = 0; i < grp->nsections; i++)
        {
            int btn_action = KBD_TAB_BASE + i;
            const char *lbl = _ (grp->sections[i].label);
            WButton *btn = button_new (y, x, btn_action, NORMAL_BUTTON, lbl, NULL);

            group_add_widget (g, btn);
            x += str_term_width1 (lbl) + 4;
        }
    }

    /* hline separator */
    group_add_widget (g, hline_new (y + 1, -1, -1));
    y += 2;

    /* table */
    kbd_table = table_new (y, 2, table_height, kbd_dlg_width - 5, KBD_TABLE_COLS, col_defs);
    kbd_table->scrollbar = TRUE;
    {
        table_datasource_t ds = { kbd_table_get_nrows, kbd_table_get_text, NULL, NULL, NULL };

        table_set_datasource (kbd_table, ds);
    }
    group_add_widget (g, kbd_table);
    y += table_height;

    /* hline before buttons */
    group_add_widget (g, hline_new (y, -1, -1));
    y++;

    /* buttons */
    {
        int save_w = str_term_width1 (_ ("&Save")) + 4;
        int edit_w = str_term_width1 (_ ("Edit &keymap file")) + 4;
        int term_w = str_term_width1 (_ ("Edit &term file")) + 4;
        int close_w = str_term_width1 (_ ("&Close")) + 4;
        int gap = 2;
        int total_w = save_w + gap + edit_w + gap + term_w + gap + close_w;
        int bx = (kbd_dlg_width - total_w) / 2;

        group_add_widget (g, button_new (y, bx, B_ENTER, NORMAL_BUTTON, _ ("&Save"), NULL));
        bx += save_w + gap;
        group_add_widget (
            g, button_new (y, bx, KBD_EDIT_FILE, NORMAL_BUTTON, _ ("Edit &keymap file"), NULL));
        bx += edit_w + gap;
        group_add_widget (
            g, button_new (y, bx, KBD_EDIT_TERM, NORMAL_BUTTON, _ ("Edit &term file"), NULL));
        bx += term_w + gap;
        group_add_widget (g, button_new (y, bx, B_CANCEL, NORMAL_BUTTON, _ ("&Close"), NULL));
    }

    keybind_table_fill ();

    /* restore position after capture */
    if (kbd_capture_row >= 0)
        table_set_current (kbd_table, kbd_capture_row);

    /* focus on table */
    widget_select (WIDGET (kbd_table));

    ret = dlg_run (dlg);
    widget_destroy (WIDGET (dlg));
    kbd_table = NULL;
    return ret;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
keybind_dialog (void)
{
    kbd_changes = g_ptr_array_new_with_free_func (keybind_change_free);
    kbd_group_idx = 0;
    kbd_section_idx = -1;
    kbd_current_section = keybind_groups[0].sections[0].section;

    while (TRUE)
    {
        int ret;

        ret = keybind_show_page ();

        if (ret == B_ENTER)
        {
            keybind_save_changes ();
            continue;
        }

        if (ret == B_CANCEL)
            break;

        /* KBD_CAPTURE no longer used -- capture is inline in MSG_KEY */

        if (ret == KBD_EDIT_FILE)
        {
#ifdef USE_INTERNAL_EDIT
            /* Edit mc.keymap in internal editor */
            char *keymap_path;
            edit_arg_t *arg;

            keymap_path = mc_config_get_full_path (GLOBAL_KEYMAP_FILE);
            arg = edit_arg_new (keymap_path, 0);
            edit_file (arg);
            edit_arg_free (arg);
            g_free (keymap_path);
#endif
            /* changes take effect on next mc restart */
            continue;
        }

        if (ret == KBD_EDIT_TERM)
        {
#ifdef USE_INTERNAL_EDIT
            /* Edit terminal keys file in internal editor */
            char *term_path;
            edit_arg_t *arg;

            term_path = mc_term_keys_path ();
            arg = edit_arg_new (term_path, 0);
            edit_file (arg);
            edit_arg_free (arg);
            g_free (term_path);
#endif

            continue;
        }

        if (ret == KBD_TAB_BACK)
        {
            /* return to top-level, show first group content */
            kbd_group_idx = 0;
            kbd_section_idx = -1;
            kbd_current_section = keybind_groups[0].sections[0].section;
            continue;
        }

        if (ret >= KBD_TAB_BASE)
        {
            int idx = ret - KBD_TAB_BASE;

            /* flush dirty entries before switching section */
            keybind_save_changes ();

            if (kbd_section_idx < 0)
            {
                /* top-level: entering a group */
                if (idx >= 0 && idx < keybind_group_count)
                {
                    const keybind_group_t *grp = &keybind_groups[idx];

                    kbd_group_idx = idx;

                    if (grp->nsections == 1)
                    {
                        /* single section -- show directly, keep top-level tabs */
                        kbd_section_idx = -1;
                        kbd_current_section = grp->sections[0].section;
                    }
                    else
                    {
                        /* multiple sections -- show subtabs */
                        kbd_section_idx = 0;
                        kbd_current_section = grp->sections[0].section;
                    }
                }
            }
            else
            {
                /* sub-level: switching section within group */
                const keybind_group_t *grp = &keybind_groups[kbd_group_idx];

                if (idx >= 0 && idx < grp->nsections)
                {
                    kbd_section_idx = idx;
                    kbd_current_section = grp->sections[idx].section;
                }
            }
            continue;
        }

        break;
    }

    g_ptr_array_free (kbd_changes, TRUE);
    kbd_changes = NULL;
    kbd_table = NULL;

    if (kbd_dict != NULL)
    {
        guint di;

        for (di = 0; di < kbd_dict->len; di++)
            kbd_action_clear (&g_array_index (kbd_dict, kbd_action_t, di));
        g_array_free (kbd_dict, TRUE);
        kbd_dict = NULL;
    }
    kbd_dict_section = NULL;

    if (kbd_row_map != NULL)
    {
        g_array_free (kbd_row_map, TRUE);
        kbd_row_map = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */
