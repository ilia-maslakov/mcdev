/*
   Editor macros engine

   Copyright (C) 2001-2026
   Free Software Foundation, Inc.

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

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/tty/key.h"  // tty_keyname_to_keycode*()
#include "lib/keybind.h"  // keybind_lookup_actionname()
#include "lib/fileloc.h"
#include "lib/widget.h"    // dialog, listbox, button, query_dialog, message
#include "lib/charsets.h"  // convert_from_8bit_to_utf_c, convert_to_display_c

#include "src/setup.h"    // macro_action_t
#include "src/history.h"  // MC_HISTORY_EDIT_REPEAT

#include "edit-impl.h"  // edit_load_file_from_filename
#include "editwidget.h"

#include "editmacros.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static int
edit_macro_comparator (gconstpointer *macro1, gconstpointer *macro2)
{
    const macros_t *m1 = (const macros_t *) macro1;
    const macros_t *m2 = (const macros_t *) macro2;

    return m1->hotkey - m2->hotkey;
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_macro_sort_by_hotkey (void)
{
    if (macros_list != NULL && macros_list->len != 0)
        g_array_sort (macros_list, (GCompareFunc) edit_macro_comparator);
}

/* --------------------------------------------------------------------------------------------- */

static int
edit_get_macro (WEdit *edit, int hotkey)
{
    macros_t *array_start;
    macros_t *result;
    macros_t search_macro = {
        .hotkey = hotkey,
    };

    (void) edit;

    result = bsearch (&search_macro, macros_list->data, macros_list->len, sizeof (macros_t),
                      (GCompareFunc) edit_macro_comparator);

    if (result == NULL || result->macro == NULL)
        return (-1);

    array_start = &g_array_index (macros_list, struct macros_t, 0);

    return (int) (result - array_start);
}

/* --------------------------------------------------------------------------------------------- */

/** returns FALSE on error */
static gboolean
edit_delete_macro (WEdit *edit, int hotkey)
{
    mc_config_t *macros_config = NULL;
    const char *section_name = "editor";
    gchar *macros_fname;
    int indx;
    char *skeyname;

    // clear array of actions for current hotkey
    while ((indx = edit_get_macro (edit, hotkey)) != -1)
    {
        macros_t *macros;

        macros = &g_array_index (macros_list, struct macros_t, indx);
        g_array_free (macros->macro, TRUE);
        g_array_remove_index (macros_list, indx);
    }

    macros_fname = mc_config_get_full_path (MC_MACRO_FILE);
    macros_config = mc_config_init (macros_fname, FALSE);
    g_free (macros_fname);

    if (macros_config == NULL)
        return FALSE;

    skeyname = tty_keycode_to_keyname (hotkey);
    while (mc_config_del_key (macros_config, section_name, skeyname))
        ;
    g_free (skeyname);
    mc_config_save_file (macros_config, NULL);
    mc_config_deinit (macros_config);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/** returns FALSE on error */
gboolean
edit_store_macro_cmd (WEdit *edit)
{
    int i;
    int hotkey;
    GString *macros_string = NULL;
    const char *section_name = "editor";
    gchar *macros_fname;
    GArray *macros = NULL;
    int tmp_act;
    mc_config_t *macros_config;
    char *skeyname;

    hotkey =
        editcmd_dialog_raw_key_query (_ ("Save macro"), _ ("Press the macro's new hotkey:"), TRUE);
    if (hotkey == ESC_CHAR)
        return FALSE;

    tmp_act = keybind_lookup_keymap_command (WIDGET (edit)->keymap, hotkey);
    // return FALSE if try assign macro into restricted hotkeys
    if (tmp_act == CK_MacroStartRecord || tmp_act == CK_MacroStopRecord
        || tmp_act == CK_MacroStartStopRecord)
        return FALSE;

    edit_delete_macro (edit, hotkey);

    macros_fname = mc_config_get_full_path (MC_MACRO_FILE);
    macros_config = mc_config_init (macros_fname, FALSE);
    g_free (macros_fname);

    if (macros_config == NULL)
        return FALSE;

    edit_push_undo_action (edit, KEY_PRESS + edit->start_display);

    skeyname = tty_keycode_to_keyname (hotkey);

    for (i = 0; i < macro_index; i++)
    {
        macro_action_t m_act;
        const char *action_name;

        action_name = keybind_lookup_actionname (record_macro_buf[i].action);
        if (action_name == NULL)
            break;

        if (macros == NULL)
        {
            macros = g_array_new (TRUE, FALSE, sizeof (macro_action_t));
            macros_string = g_string_sized_new (250);
        }

        m_act.action = record_macro_buf[i].action;
        m_act.ch = record_macro_buf[i].ch;
        g_array_append_val (macros, m_act);
        g_string_append_printf (macros_string, "%s:%i;", action_name, (int) record_macro_buf[i].ch);
    }

    if (macros == NULL)
        mc_config_del_key (macros_config, section_name, skeyname);
    else
    {
        macros_t macro;

        macro.hotkey = hotkey;
        macro.macro = macros;
        g_array_append_val (macros_list, macro);
        mc_config_set_string (macros_config, section_name, skeyname, macros_string->str);
    }

    g_free (skeyname);

    edit_macro_sort_by_hotkey ();

    if (macros_string != NULL)
        g_string_free (macros_string, TRUE);
    mc_config_save_file (macros_config, NULL);
    mc_config_deinit (macros_config);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/** return FALSE on error */

gboolean
edit_load_macro_cmd (WEdit *edit)
{
    mc_config_t *macros_config = NULL;
    gchar **profile_keys, **keys;
    gchar **values, **curr_values;
    const char *section_name = "editor";
    gchar *macros_fname;

    (void) edit;

    if (macros_list == NULL || macros_list->len != 0)
        return FALSE;

    macros_fname = mc_config_get_full_path (MC_MACRO_FILE);
    macros_config = mc_config_init (macros_fname, TRUE);
    g_free (macros_fname);

    if (macros_config == NULL)
        return FALSE;

    keys = mc_config_get_keys (macros_config, section_name, NULL);

    for (profile_keys = keys; *profile_keys != NULL; profile_keys++)
    {
        int hotkey;
        GArray *macros = NULL;

        values = mc_config_get_string_list (macros_config, section_name, *profile_keys, NULL);
        hotkey = tty_keyname_to_keycode (*profile_keys, NULL);

        for (curr_values = values; *curr_values != NULL && *curr_values[0] != '\0'; curr_values++)
        {
            char **macro_pair;

            macro_pair = g_strsplit (*curr_values, ":", 2);

            if (macro_pair != NULL)
            {
                macro_action_t m_act = {
                    .action = 0,
                    .ch = -1,
                };

                if (macro_pair[0] != NULL && macro_pair[0][0] != '\0')
                    m_act.action = keybind_lookup_action (macro_pair[0]);

                if (macro_pair[1] != NULL && macro_pair[1][0] != '\0')
                    m_act.ch = strtol (macro_pair[1], NULL, 0);

                if (m_act.action != 0)
                {
                    // a shell command
                    if ((m_act.action / CK_PipeBlock (0)) == 1)
                    {
                        m_act.action = CK_PipeBlock (0);
                        if (m_act.ch > 0)
                            m_act.action += m_act.ch;
                        m_act.ch = -1;
                    }

                    if (macros == NULL)
                        macros = g_array_new (TRUE, FALSE, sizeof (m_act));

                    g_array_append_val (macros, m_act);
                }

                g_strfreev (macro_pair);
            }
        }

        if (macros != NULL)
        {
            macros_t macro = {
                .hotkey = hotkey,
                .macro = macros,
            };

            g_array_append_val (macros_list, macro);
        }

        g_strfreev (values);
    }

    g_strfreev (keys);
    mc_config_deinit (macros_config);
    edit_macro_sort_by_hotkey ();

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_repeat_macro_cmd (WEdit *edit)
{
    gboolean ok;
    char *f;
    long count_repeat = 0;

    f = input_dialog (_ ("Repeat last commands"), _ ("Repeat times:"), MC_HISTORY_EDIT_REPEAT, NULL,
                      INPUT_COMPLETE_NONE);
    ok = (f != NULL && *f != '\0');

    if (ok)
    {
        char *error = NULL;

        count_repeat = strtol (f, &error, 0);

        ok = (*error == '\0');
    }

    g_free (f);

    if (ok)
    {
        int i, j;

        edit_push_undo_action (edit, KEY_PRESS + edit->start_display);
        edit->force |= REDRAW_PAGE;

        for (j = 0; j < count_repeat; j++)
            for (i = 0; i < macro_index; i++)
                edit_execute_cmd (edit, record_macro_buf[i].action, record_macro_buf[i].ch);

        edit_update_screen (edit);
    }

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

/** returns FALSE on error */
gboolean
edit_execute_macro (WEdit *edit, int hotkey)
{
    gboolean res = FALSE;

    if (hotkey != 0)
    {
        int indx;

        indx = edit_get_macro (edit, hotkey);
        if (indx != -1)
        {
            const macros_t *macros;

            macros = &g_array_index (macros_list, struct macros_t, indx);
            if (macros->macro->len != 0)
            {
                guint i;

                edit->force |= REDRAW_PAGE;

                for (i = 0; i < macros->macro->len; i++)
                {
                    const macro_action_t *m_act;

                    m_act = &g_array_index (macros->macro, struct macro_action_t, i);
                    edit_execute_cmd (edit, m_act->action, m_act->ch);
                    res = TRUE;
                }
            }
        }
    }

    return res;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_begin_end_macro_cmd (WEdit *edit)
{
    // edit is a pointer to the widget
    if (edit != NULL)
    {
        long command = macro_index < 0 ? CK_MacroStartRecord : CK_MacroStopRecord;

        edit_execute_key_command (edit, command, -1);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
edit_begin_end_repeat_cmd (WEdit *edit)
{
    // edit is a pointer to the widget
    if (edit != NULL)
    {
        long command = macro_index < 0 ? CK_RepeatStartRecord : CK_RepeatStopRecord;

        edit_execute_key_command (edit, command, -1);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** Macro Explorer *******************************************************************************/
/* --------------------------------------------------------------------------------------------- */

#define MEXPL_DLG_W   60
#define MEXPL_DLG_H   19
#define MEXPL_INNER   (MEXPL_DLG_W - 4)
#define MEXPL_UPPER_H 7
#define MEXPL_LOWER_H 5
#define B_EDIT_MACRO  (B_USER + 1)

typedef struct
{
    WEdit *edit;
    WListbox *upper;
    WListbox *lower;
    WButton *run;
    WButton *del;
    WButton *edit_file;
    int hotkey;
} mexpl_state_t;

/* --------------------------------------------------------------------------------------------- */

static void
mexpl_format_insert_char (const WEdit *edit, unsigned char byte, char *buf, size_t bufsize)
{
    if (byte == 0)
    {
        g_snprintf (buf, bufsize, "  InsertChar");
        return;
    }

    if (mc_global.utf8_display)
    {
        if (!edit->utf8)
        {
            gunichar uc = (gunichar) convert_from_8bit_to_utf_c ((char) byte, edit->converter);
            char ch_utf8[MB_LEN_MAX + 1] = { 0 };

            if (g_unichar_isprint (uc))
            {
                g_unichar_to_utf8 (uc, ch_utf8);
                g_snprintf (buf, bufsize, "  InsertChar ('%s')", ch_utf8);
            }
            else
                g_snprintf (buf, bufsize, "  InsertChar (0x%02X)", byte);
        }
        else
        {
            if (byte < 128 && g_ascii_isprint ((gchar) byte))
                g_snprintf (buf, bufsize, "  InsertChar ('%c')", (char) byte);
            else
                g_snprintf (buf, bufsize, "  InsertChar (0x%02X)", byte);
        }
    }
    else
    {
        int dc = convert_to_display_c ((int) byte);

        if (dc >= 32 && dc != 127)
            g_snprintf (buf, bufsize, "  InsertChar ('%c')", (char) dc);
        else
            g_snprintf (buf, bufsize, "  InsertChar (0x%02X)", byte);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mexpl_action_label (const WEdit *edit, const macro_action_t *act, char *buf, size_t bufsize,
                    gboolean indent)
{
    long action = act->action;
    const char *prefix = indent ? "  " : "";

    if (action >= CK_PipeBlock (0))
    {
        g_snprintf (buf, bufsize, "%sExternal(%ld)", prefix, action - CK_PipeBlock (0));
        return;
    }

    if (action == CK_InsertChar)
    {
        if (!indent)
            g_strlcpy (buf, "InsertChar", bufsize);
        else
            mexpl_format_insert_char (edit, (unsigned char) act->ch, buf, bufsize);
        return;
    }

    {
        const char *desc = keybind_lookup_actiondesc (action);
        if (desc != NULL)
        {
            g_snprintf (buf, bufsize, "%s%s", prefix, desc);
            return;
        }
    }

    {
        const char *name = keybind_lookup_actionname (action);
        if (name != NULL)
        {
            g_snprintf (buf, bufsize, "%s%s", prefix, name);
            return;
        }
    }

    g_snprintf (buf, bufsize, "%s#%ld", prefix, action);
}

/* --------------------------------------------------------------------------------------------- */

static void
mexpl_populate_lower (mexpl_state_t *state, guint macro_idx)
{
    const macros_t *m;
    guint i;

    listbox_remove_list (state->lower);

    if (macros_list == NULL || macro_idx >= macros_list->len)
    {
        widget_draw (WIDGET (state->lower));
        return;
    }

    m = &g_array_index (macros_list, macros_t, macro_idx);

    for (i = 0; i < m->macro->len;)
    {
        const macro_action_t *act = &g_array_index (m->macro, macro_action_t, i);
        char buf[80];

        if (act->action == CK_InsertChar && state->edit->utf8)
        {
            unsigned char byte = (unsigned char) act->ch;

            if (byte >= 0x80)
            {
                char seq[MB_LEN_MAX + 1] = { 0 };
                int collected = 0;
                gunichar uc;

                seq[collected++] = (char) byte;
                uc = g_utf8_get_char_validated (seq, collected);

                while (uc == (gunichar) -2 && collected < MB_LEN_MAX
                       && i + (guint) collected < m->macro->len)
                {
                    const macro_action_t *next;
                    unsigned char nb;

                    next = &g_array_index (m->macro, macro_action_t, i + (guint) collected);
                    nb = (unsigned char) next->ch;

                    if (next->action != CK_InsertChar || nb < 0x80 || nb >= 0xC0)
                        break;

                    seq[collected++] = (char) nb;
                    uc = g_utf8_get_char_validated (seq, collected);
                }

                if (uc != (gunichar) -1 && uc != (gunichar) -2 && g_unichar_isprint (uc))
                {
                    char ch_utf8[MB_LEN_MAX + 1] = { 0 };

                    g_unichar_to_utf8 (uc, ch_utf8);
                    g_snprintf (buf, sizeof (buf), "  InsertChar ('%s')", ch_utf8);
                    listbox_add_item (state->lower, LISTBOX_APPEND_AT_END, 0, buf, NULL, FALSE);
                    i += (guint) collected;
                    continue;
                }
            }

            mexpl_format_insert_char (state->edit, byte, buf, sizeof (buf));
        }
        else
            mexpl_action_label (state->edit, act, buf, sizeof (buf), TRUE);

        listbox_add_item (state->lower, LISTBOX_APPEND_AT_END, 0, buf, NULL, FALSE);
        i++;
    }

    widget_draw (WIDGET (state->lower));
}

/* --------------------------------------------------------------------------------------------- */

static void
mexpl_populate_upper (mexpl_state_t *state)
{
    guint i;

    listbox_remove_list (state->upper);
    state->hotkey = 0;

    if (macros_list == NULL)
        return;

    for (i = 0; i < macros_list->len; i++)
    {
        const macros_t *m = &g_array_index (macros_list, macros_t, i);
        char *hotkey_name;
        GString *line;
        guint j;
        gboolean first = TRUE;

        hotkey_name = tty_keycode_to_keyname (m->hotkey);
        line = g_string_new (NULL);
        g_string_printf (line, "%-16s", hotkey_name);
        g_free (hotkey_name);

        for (j = 0; j < m->macro->len; j++)
        {
            const macro_action_t *act = &g_array_index (m->macro, macro_action_t, j);
            char act_buf[64];
            int line_w, act_w;

            mexpl_action_label (state->edit, act, act_buf, sizeof (act_buf), FALSE);

            line_w = str_term_width1 (line->str);
            act_w = str_term_width1 (act_buf);

            if (line_w + (first ? 0 : 2) + act_w + (first ? 3 : 5) > MEXPL_INNER - 2)
            {
                g_string_append (line, first ? "..." : ", ...");
                break;
            }

            if (!first)
                g_string_append (line, ", ");
            g_string_append (line, act_buf);
            first = FALSE;
        }

        listbox_add_item (state->upper, LISTBOX_APPEND_AT_END, 0, line->str,
                          GINT_TO_POINTER ((gint) i), FALSE);
        g_string_free (line, TRUE);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mexpl_sync_buttons (mexpl_state_t *state)
{
    gboolean empty = (macros_list == NULL || macros_list->len == 0);

    widget_disable (WIDGET (state->run), empty);
    widget_disable (WIDGET (state->del), empty);
    widget_disable (WIDGET (state->edit_file), empty);
}

/* --------------------------------------------------------------------------------------------- */

static int
mexpl_delete_cb (WButton *b, int action)
{
    mexpl_state_t *state = (mexpl_state_t *) DIALOG (WIDGET (b)->owner)->data.p;
    char *hotkey_name;
    char *msg;
    int r;
    void *data;
    guint macro_idx;
    int new_sel;

    (void) b;
    (void) action;

    if (macros_list == NULL || macros_list->len == 0)
        return 0;

    listbox_get_current (state->upper, NULL, &data);
    macro_idx = (guint) GPOINTER_TO_INT (data);

    if (macro_idx >= macros_list->len)
        return 0;

    hotkey_name = tty_keycode_to_keyname (g_array_index (macros_list, macros_t, macro_idx).hotkey);
    msg = g_strdup_printf (_ ("Delete macro %s?"), hotkey_name);
    g_free (hotkey_name);

    r = query_dialog (_ ("Delete macro"), msg, D_NORMAL, 2, _ ("&Yes"), _ ("&No"));
    g_free (msg);

    if (r != 0)
        return 0;

    edit_delete_macro (state->edit, g_array_index (macros_list, macros_t, macro_idx).hotkey);

    new_sel = (macros_list != NULL && macros_list->len > 0)
        ? (int) MIN (macro_idx, macros_list->len - 1)
        : -1;

    mexpl_populate_upper (state);
    listbox_remove_list (state->lower);

    if (new_sel >= 0)
    {
        listbox_set_current (state->upper, new_sel);
        mexpl_populate_lower (state, (guint) new_sel);
        {
            void *d;
            listbox_get_current (state->upper, NULL, &d);
            state->hotkey =
                g_array_index (macros_list, macros_t, (guint) GPOINTER_TO_INT (d)).hotkey;
        }
    }
    else
        state->hotkey = 0;

    mexpl_sync_buttons (state);
    widget_draw (WIDGET (state->upper));
    widget_draw (WIDGET (state->lower));

    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static long
mexpl_macro_file_line (int hotkey)
{
    char *fname;
    char *skeyname;
    FILE *f;
    char line_buf[512];
    long lineno = 0;
    long found = 1;
    size_t klen;

    fname = mc_config_get_full_path (MC_MACRO_FILE);
    f = fopen (fname, "r");
    g_free (fname);

    if (f == NULL)
        return 1;

    skeyname = tty_keycode_to_keyname (hotkey);
    klen = strlen (skeyname);

    while (fgets (line_buf, sizeof (line_buf), f) != NULL)
    {
        lineno++;
        if (strncmp (line_buf, skeyname, klen) == 0 && line_buf[klen] == '=')
        {
            found = lineno;
            break;
        }
    }

    fclose (f);
    g_free (skeyname);
    return found;
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
mexpl_dlg_cb (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    mexpl_state_t *state = (mexpl_state_t *) DIALOG (w)->data.p;

    if (msg == MSG_NOTIFY && sender == WIDGET (state->upper))
    {
        void *item_data;

        listbox_get_current (state->upper, NULL, &item_data);
        {
            guint idx = (guint) GPOINTER_TO_INT (item_data);

            if (macros_list != NULL && idx < macros_list->len)
            {
                state->hotkey = g_array_index (macros_list, macros_t, idx).hotkey;
                mexpl_populate_lower (state, idx);
            }
        }

        if (parm == CK_Enter)
        {
            DIALOG (w)->ret_value = B_ENTER;
            dlg_close (DIALOG (w));
        }

        return MSG_HANDLED;
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

void
edit_macro_explorer_cmd (WEdit *edit)
{
    WDialog *dlg;
    WGroup *g;
    mexpl_state_t state = {
        .edit = edit,
    };
    WButton *close_button;
    int y;
    int x_run, x_del, x_edit, x_close;
    int buttons_width;
    int ret;

    if (macro_index >= 0)
        return;  // recording in progress

    if (macros_list == NULL || macros_list->len == 0)
    {
        message (D_NORMAL, _ ("Macro Explorer"), "%s", _ ("No macros recorded"));
        return;
    }

    dlg = dlg_create (TRUE, 0, 0, MEXPL_DLG_H, MEXPL_DLG_W, WPOS_CENTER | WPOS_TRYUP, FALSE,
                      dialog_colors, mexpl_dlg_cb, NULL, "[Macro Explorer]", _ ("Macro Explorer"));
    g = GROUP (dlg);
    dlg->data.p = &state;
    widget_want_tab (WIDGET (dlg), TRUE);

    y = 2;

    state.upper = listbox_new (y, 2, MEXPL_UPPER_H, MEXPL_INNER, FALSE, NULL);
    group_add_widget (g, state.upper);
    y += MEXPL_UPPER_H;

    group_add_widget (g, hline_new (y++, -1, -1));

    state.lower = listbox_new (y, 2, MEXPL_LOWER_H, MEXPL_INNER, FALSE, NULL);
    group_add_widget (g, state.lower);
    y += MEXPL_LOWER_H;

    group_add_widget (g, hline_new (y++, -1, -1));

    state.run = button_new (y, 0, B_ENTER, DEFPUSH_BUTTON, _ ("Run"), NULL);
    state.del = button_new (y, 0, B_USER, NORMAL_BUTTON, _ ("Delete"), mexpl_delete_cb);
    state.edit_file = button_new (y, 0, B_EDIT_MACRO, NORMAL_BUTTON, _ ("Edit file"), NULL);
    close_button = button_new (y, 0, B_CANCEL, NORMAL_BUTTON, _ ("Close"), NULL);

    buttons_width = button_get_width (state.run) + button_get_width (state.del)
        + button_get_width (state.edit_file) + button_get_width (close_button) + 6;
    x_run = 1 + (MEXPL_DLG_W - 2 - buttons_width) / 2;
    x_del = x_run + button_get_width (state.run) + 2;
    x_edit = x_del + button_get_width (state.del) + 2;
    x_close = x_edit + button_get_width (state.edit_file) + 2;

    WIDGET (state.run)->rect.x = x_run;
    WIDGET (state.del)->rect.x = x_del;
    WIDGET (state.edit_file)->rect.x = x_edit;
    WIDGET (close_button)->rect.x = x_close;

    group_add_widget (g, state.run);
    group_add_widget (g, state.del);
    group_add_widget (g, state.edit_file);
    group_add_widget (g, close_button);

    mexpl_populate_upper (&state);
    mexpl_sync_buttons (&state);

    listbox_set_current (state.upper, 0);
    {
        void *data0;
        listbox_get_current (state.upper, NULL, &data0);
        guint idx0 = (guint) GPOINTER_TO_INT (data0);
        state.hotkey = g_array_index (macros_list, macros_t, idx0).hotkey;
        mexpl_populate_lower (&state, idx0);
    }

    widget_select (WIDGET (state.upper));

    ret = dlg_run (dlg);
    widget_destroy (WIDGET (dlg));

    if (ret == B_ENTER && state.hotkey != 0)
        edit_execute_macro (edit, state.hotkey);
    else if (ret == B_EDIT_MACRO && state.hotkey != 0)
    {
        char *fname = mc_config_get_full_path (MC_MACRO_FILE);
        vfs_path_t *vpath = vfs_path_from_str (fname);
        edit_arg_t arg;

        g_free (fname);
        edit_arg_init (&arg, vpath, mexpl_macro_file_line (state.hotkey));
        edit_load_file_from_filename (DIALOG (WIDGET (edit)->owner), &arg);
        vfs_path_free (vpath, TRUE);
    }
}

/* --------------------------------------------------------------------------------------------- */
