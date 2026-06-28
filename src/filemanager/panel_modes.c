/*
   Panel view modes -- a dynamic, user-editable list of named listing formats.

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

/** \file panel_modes.c
 *  \brief Source: dynamic list of named panel listing modes
 */

#include <config.h>

#include <stdlib.h>  // strtol
#include <string.h>

#include "lib/global.h"
#include "lib/mcconfig.h"  // mc_config_*
#include "lib/strutil.h"
#include "lib/tty/key.h"  // KEY_IC, KEY_DC, KEY_F
#include "lib/widget.h"

#include "panel.h"  // panel_get_field_by_id
#include "panel_modes.h"

/*** file scope macro definitions ****************************************************************/

#define PANEL_MODES_SECTION "Panel modes"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static GPtrArray *panel_modes = NULL;
static guint panel_modes_next_id = 1;

/* The listbox of the currently open modes dialog (NULL when closed). Only one
   modes dialog can be open at a time. */
static WListbox *panel_modes_lb = NULL;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
panel_mode_free (gpointer data)
{
    panel_mode_t *mode = (panel_mode_t *) data;

    if (mode == NULL)
        return;
    g_free (mode->name);
    g_free (mode->types);
    g_free (mode->widths);
    g_free (mode->status_types);
    g_free (mode->status_widths);
    g_free (mode);
}

/* --------------------------------------------------------------------------------------------- */

static panel_mode_t *
panel_mode_new (const char *name, const char *types, const char *widths, const char *status_types,
                const char *status_widths)
{
    panel_mode_t *mode = g_new0 (panel_mode_t, 1);

    mode->id = panel_modes_next_id++;
    mode->name = g_strdup (name != NULL ? name : "");
    mode->types = g_strdup (types != NULL ? types : "");
    mode->widths = g_strdup (widths != NULL ? widths : "");
    mode->status_types = g_strdup (status_types != NULL ? status_types : "");
    mode->status_widths = g_strdup (status_widths != NULL ? status_widths : "");
    return mode;
}

/* --------------------------------------------------------------------------------------------- */

/* Append "type[:width]" tokens to `out`, joined by " | ". Width 0 or missing
   is emitted without a ":width" so mc keeps its auto-expand behaviour. */
static void
panel_mode_append_columns (GString *out, const char *types, const char *widths)
{
    char **tarr;
    char **warr;
    guint i;
    gboolean first = TRUE;

    if (types == NULL || types[0] == '\0')
        return;

    tarr = g_strsplit (types, ",", -1);
    warr = widths != NULL ? g_strsplit (widths, ",", -1) : NULL;

    for (i = 0; tarr[i] != NULL; i++)
    {
        char *type = g_strstrip (tarr[i]);
        long w = 0;

        if (type[0] == '\0')
            continue;

        if (warr != NULL && warr[i] != NULL)
        {
            char *ws = g_strstrip (warr[i]);
            char *endp = NULL;

            if (ws[0] != '\0')
            {
                long n = strtol (ws, &endp, 10);
                if (endp != ws && *endp == '\0' && n > 0)
                    w = n;
            }
        }

        /* Column separator goes BETWEEN columns only, never before the first
           one (the caller has already put the "half " prefix into `out`). */
        if (!first)
            g_string_append (out, " | ");
        first = FALSE;
        g_string_append (out, type);
        if (w > 0)
            g_string_append_printf (out, ":%ld", w);
    }

    g_strfreev (tarr);
    if (warr != NULL)
        g_strfreev (warr);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Native field ids; English names, translated for display elsewhere. */
static void
panel_modes_seed_defaults (void)
{
    g_ptr_array_add (
        panel_modes,
        panel_mode_new ("Brief", "type name", "0", "type name space bsize space perm space", "0"));
    g_ptr_array_add (panel_modes,
                     panel_mode_new ("Full", "type name,size,mtime", "0,0,0", "type name", "0"));
    g_ptr_array_add (
        panel_modes,
        panel_mode_new (
            "Long", "perm space nlink space owner space group space size space mtime space name",
            "0", "perm space nlink space owner space group space size space mtime space name",
            "0"));
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_modes_load (void)
{
    int count;
    int i;

    if (mc_global.main_config == NULL
        || !mc_config_has_group (mc_global.main_config, PANEL_MODES_SECTION))
    {
        panel_modes_seed_defaults ();
        return;
    }

    count = mc_config_get_int (mc_global.main_config, PANEL_MODES_SECTION, "count", 0);

    for (i = 0; i < count; i++)
    {
        char key[BUF_TINY];
        panel_mode_t *m;
        int id;
        char *name;
        char *types;
        char *widths;
        char *stypes;
        char *swidths;

        g_snprintf (key, sizeof (key), "%d_name", i);
        name = mc_config_get_string (mc_global.main_config, PANEL_MODES_SECTION, key, "");
        g_snprintf (key, sizeof (key), "%d_types", i);
        types = mc_config_get_string (mc_global.main_config, PANEL_MODES_SECTION, key, "name");
        g_snprintf (key, sizeof (key), "%d_widths", i);
        widths = mc_config_get_string (mc_global.main_config, PANEL_MODES_SECTION, key, "0");
        g_snprintf (key, sizeof (key), "%d_status_types", i);
        stypes =
            mc_config_get_string (mc_global.main_config, PANEL_MODES_SECTION, key, "name,size");
        g_snprintf (key, sizeof (key), "%d_status_widths", i);
        swidths = mc_config_get_string (mc_global.main_config, PANEL_MODES_SECTION, key, "0,0");
        g_snprintf (key, sizeof (key), "%d_id", i);
        id = mc_config_get_int (mc_global.main_config, PANEL_MODES_SECTION, key, 0);

        m = panel_mode_new (name, types, widths, stypes, swidths);
        if (id > 0)
        {
            m->id = (guint) id;
            if ((guint) id >= panel_modes_next_id)
                panel_modes_next_id = (guint) id + 1;
        }
        g_ptr_array_add (panel_modes, m);

        g_free (name);
        g_free (types);
        g_free (widths);
        g_free (stypes);
        g_free (swidths);
    }

    if (panel_modes->len == 0)
        panel_modes_seed_defaults ();
}

/* --------------------------------------------------------------------------------------------- */

void
panel_modes_init (void)
{
    if (panel_modes != NULL)
        return;

    panel_modes = g_ptr_array_new_with_free_func (panel_mode_free);
    panel_modes_load ();
}

/* --------------------------------------------------------------------------------------------- */

void
panel_modes_save (void)
{
    guint i;

    if (panel_modes == NULL || mc_global.main_config == NULL)
        return;

    mc_config_del_group (mc_global.main_config, PANEL_MODES_SECTION);
    mc_config_set_int (mc_global.main_config, PANEL_MODES_SECTION, "count", (int) panel_modes->len);

    for (i = 0; i < panel_modes->len; i++)
    {
        panel_mode_t *m = g_ptr_array_index (panel_modes, i);
        char key[BUF_TINY];

        g_snprintf (key, sizeof (key), "%u_id", i);
        mc_config_set_int (mc_global.main_config, PANEL_MODES_SECTION, key, (int) m->id);
        g_snprintf (key, sizeof (key), "%u_name", i);
        mc_config_set_string (mc_global.main_config, PANEL_MODES_SECTION, key, m->name);
        g_snprintf (key, sizeof (key), "%u_types", i);
        mc_config_set_string (mc_global.main_config, PANEL_MODES_SECTION, key, m->types);
        g_snprintf (key, sizeof (key), "%u_widths", i);
        mc_config_set_string (mc_global.main_config, PANEL_MODES_SECTION, key, m->widths);
        g_snprintf (key, sizeof (key), "%u_status_types", i);
        mc_config_set_string (mc_global.main_config, PANEL_MODES_SECTION, key, m->status_types);
        g_snprintf (key, sizeof (key), "%u_status_widths", i);
        mc_config_set_string (mc_global.main_config, PANEL_MODES_SECTION, key, m->status_widths);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
panel_modes_deinit (void)
{
    if (panel_modes != NULL)
    {
        g_ptr_array_free (panel_modes, TRUE);
        panel_modes = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

guint
panel_modes_count (void)
{
    return panel_modes != NULL ? panel_modes->len : 0;
}

/* --------------------------------------------------------------------------------------------- */

panel_mode_t *
panel_modes_get (guint index)
{
    if (panel_modes == NULL || index >= panel_modes->len)
        return NULL;
    return (panel_mode_t *) g_ptr_array_index (panel_modes, index);
}

/* --------------------------------------------------------------------------------------------- */

int
panel_modes_index_by_id (guint id)
{
    guint i;

    if (panel_modes == NULL)
        return -1;

    for (i = 0; i < panel_modes->len; i++)
        if (((panel_mode_t *) g_ptr_array_index (panel_modes, i))->id == id)
            return (int) i;

    return -1;
}

/* --------------------------------------------------------------------------------------------- */

panel_mode_t *
panel_modes_get_by_id (guint id)
{
    int idx = panel_modes_index_by_id (id);

    return idx < 0 ? NULL : panel_modes_get ((guint) idx);
}

/* --------------------------------------------------------------------------------------------- */

panel_mode_t *
panel_modes_add (const char *name)
{
    panel_mode_t *mode;

    if (panel_modes == NULL)
        panel_modes_init ();

    mode = panel_mode_new (name != NULL ? name : "New mode", "name", "0", "name,size", "0,0");
    g_ptr_array_add (panel_modes, mode);
    return mode;
}

/* --------------------------------------------------------------------------------------------- */

panel_mode_t *
panel_modes_dup (guint index)
{
    panel_mode_t *src = panel_modes_get (index);
    panel_mode_t *mode;
    char *name;

    if (src == NULL)
        return NULL;

    name = g_strdup_printf ("%s (copy)", src->name);
    mode = panel_mode_new (name, src->types, src->widths, src->status_types, src->status_widths);
    g_free (name);
    g_ptr_array_add (panel_modes, mode);
    return mode;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_modes_remove (guint index)
{
    if (panel_modes != NULL && index < panel_modes->len)
        g_ptr_array_remove_index (panel_modes, index);
}

/* --------------------------------------------------------------------------------------------- */

void
panel_modes_move (guint index, int dir)
{
    guint other;
    gpointer tmp;

    if (panel_modes == NULL || index >= panel_modes->len)
        return;

    if (dir < 0)
    {
        if (index == 0)
            return;
        other = index - 1;
    }
    else
    {
        if (index + 1 >= panel_modes->len)
            return;
        other = index + 1;
    }

    tmp = g_ptr_array_index (panel_modes, index);
    g_ptr_array_index (panel_modes, index) = g_ptr_array_index (panel_modes, other);
    g_ptr_array_index (panel_modes, other) = tmp;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_mode_set (panel_mode_t *mode, const char *name, const char *types, const char *widths,
                const char *status_types, const char *status_widths)
{
    if (mode == NULL)
        return;

    g_free (mode->name);
    mode->name = g_strdup (name != NULL ? name : "");
    g_free (mode->types);
    mode->types = g_strdup (types != NULL ? types : "");
    g_free (mode->widths);
    mode->widths = g_strdup (widths != NULL ? widths : "");
    g_free (mode->status_types);
    mode->status_types = g_strdup (status_types != NULL ? status_types : "");
    g_free (mode->status_widths);
    mode->status_widths = g_strdup (status_widths != NULL ? status_widths : "");
}

/* --------------------------------------------------------------------------------------------- */

char *
panel_mode_to_format (const panel_mode_t *mode, gboolean status)
{
    GString *s;

    if (mode == NULL)
        return NULL;

    /* "half" = the normal two-panel width; the columns follow. */
    s = g_string_new ("half ");
    if (status)
        panel_mode_append_columns (s, mode->status_types, mode->status_widths);
    else
        panel_mode_append_columns (s, mode->types, mode->widths);

    /* Nothing was appended -> fall back to just the name column. */
    if (strcmp (s->str, "half ") == 0)
        g_string_append (s, "name");

    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_mode_validate (const char *types, const char *widths, char **error)
{
    char **tarr;
    char **warr;
    guint i;
    gboolean ok = TRUE;

    if (error != NULL)
        *error = NULL;

    if (types == NULL || types[0] == '\0')
    {
        if (error != NULL)
            *error = g_strdup (_ ("Column types must not be empty."));
        return FALSE;
    }

    /* A column is one comma-separated entry; it may hold several whitespace-
       separated field ids (e.g. "type name", or "perm space nlink ..."). */
    tarr = g_strsplit (types, ",", -1);
    for (i = 0; ok && tarr[i] != NULL; i++)
    {
        char **fields = g_strsplit_set (tarr[i], " \t", -1);
        guint j;

        for (j = 0; ok && fields[j] != NULL; j++)
        {
            if (fields[j][0] == '\0')
                continue;
            if (panel_get_field_by_id (fields[j]) == NULL)
            {
                if (error != NULL)
                    *error = g_strdup_printf (_ ("Unknown column type: %s"), fields[j]);
                ok = FALSE;
            }
        }
        g_strfreev (fields);
    }
    g_strfreev (tarr);
    if (!ok)
        return FALSE;

    if (widths == NULL || widths[0] == '\0')
        return TRUE;

    warr = g_strsplit (widths, ",", -1);
    for (i = 0; ok && warr[i] != NULL; i++)
    {
        char *ws = g_strstrip (warr[i]);
        char *endp = NULL;
        long n;

        if (ws[0] == '\0')
            continue;
        n = strtol (ws, &endp, 10);
        if (endp == ws || *endp != '\0' || n < 0)
        {
            if (error != NULL)
                *error = g_strdup_printf (_ ("Invalid column width: %s"), ws);
            ok = FALSE;
        }
    }
    g_strfreev (warr);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

void
panel_apply_mode (WPanel *panel, const panel_mode_t *mode)
{
    char *fmt;
    char *status;

    if (panel == NULL || mode == NULL)
        return;
    /* Plugin panels render their own columns; leave them alone. */
    if (panel->is_plugin_panel)
        return;

    fmt = panel_mode_to_format (mode, FALSE);
    status = panel_mode_to_format (mode, TRUE);

    g_free (panel->user_format);
    panel->user_format = fmt;
    g_free (panel->user_status_format[list_user]);
    panel->user_status_format[list_user] = status;

    panel->list_format = list_user;
    panel->user_mini_status = TRUE;
    panel->view_mode_id = mode->id;

    set_panel_formats (panel);
}

/* --------------------------------------------------------------------------------------------- */
/*** dialogs *************************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Editor for a single mode (mockup 2). Returns TRUE if the mode was changed. */
static gboolean
panel_mode_edit_dialog (panel_mode_t *mode)
{
    char *name = NULL;
    char *types = NULL;
    char *widths = NULL;
    char *stypes = NULL;
    char *swidths = NULL;
    char *err = NULL;
    int res;
    gboolean ok = FALSE;

    {
        quick_widget_t quick_widgets[] = {
            // clang-format off
            QUICK_LABELED_INPUT (N_ ("Name:"), input_label_above, mode->name, "pm-name",
                                 &name, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_START_COLUMNS,
                QUICK_LABELED_INPUT (N_ ("Column types:"), input_label_above, mode->types,
                                     "pm-types", &types, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
                QUICK_LABELED_INPUT (N_ ("Column widths:"), input_label_above, mode->widths,
                                     "pm-widths", &widths, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_NEXT_COLUMN,
                QUICK_LABELED_INPUT (N_ ("Status column types:"), input_label_above,
                                     mode->status_types, "pm-stypes", &stypes, NULL, FALSE, FALSE,
                                     INPUT_COMPLETE_NONE),
                QUICK_LABELED_INPUT (N_ ("Status column widths:"), input_label_above,
                                     mode->status_widths, "pm-swidths", &swidths, NULL, FALSE,
                                     FALSE, INPUT_COMPLETE_NONE),
            QUICK_STOP_COLUMNS,
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
            // clang-format on
        };

        WRect r = { -1, -1, 0, 68 };
        quick_dialog_t qdlg = {
            .rect = r,
            .title = N_ ("Panel mode"),
            .help = "[Panel modes]",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        res = quick_dialog (&qdlg);
    }

    if (res == B_ENTER)
    {
        if (!panel_mode_validate (types, widths, &err)
            || !panel_mode_validate (stypes, swidths, &err))
            message (D_ERROR, MSG_ERROR, "%s", err);
        else
        {
            panel_mode_set (mode, name, types, widths, stypes, swidths);
            ok = TRUE;
        }
        g_free (err);
    }

    g_free (name);
    g_free (types);
    g_free (widths);
    g_free (stypes);
    g_free (swidths);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_modes_lb_fill (int select)
{
    guint i;

    listbox_remove_list (panel_modes_lb);
    for (i = 0; i < panel_modes_count (); i++)
    {
        panel_mode_t *m = panel_modes_get (i);

        listbox_add_item (panel_modes_lb, LISTBOX_APPEND_AT_END, 0,
                          m->name[0] != '\0' ? m->name : _ ("(unnamed)"), GUINT_TO_POINTER (m->id),
                          FALSE);
    }
    if (select >= 0 && select < (int) panel_modes_count ())
        listbox_set_current (panel_modes_lb, select);
}

/* --------------------------------------------------------------------------------------------- */

static panel_mode_t *
panel_modes_lb_current (void)
{
    void *data = NULL;

    if (panel_modes_lb == NULL || listbox_get_length (panel_modes_lb) == 0)
        return NULL;
    listbox_get_current (panel_modes_lb, NULL, &data);
    return panel_modes_get_by_id (GPOINTER_TO_UINT (data));
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_modes_action_new (void)
{
    panel_mode_t *m = panel_modes_add (NULL);

    if (!panel_mode_edit_dialog (m))
        panel_modes_remove (panel_modes_count () - 1);
    panel_modes_lb_fill (panel_modes_count () - 1);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_modes_action_edit (void)
{
    int cur = panel_modes_lb->current;
    panel_mode_t *m = panel_modes_lb_current ();

    if (m != NULL)
        panel_mode_edit_dialog (m);
    panel_modes_lb_fill (cur);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_modes_action_delete (void)
{
    int cur = panel_modes_lb->current;

    if (panel_modes_lb_current () != NULL)
        panel_modes_remove ((guint) cur);
    panel_modes_lb_fill (cur > 0 ? cur - 1 : 0);
}

/* --------------------------------------------------------------------------------------------- */

static int
panel_modes_new_cb (WButton *button, int action)
{
    (void) button;
    (void) action;
    panel_modes_action_new ();
    widget_draw (WIDGET (panel_modes_lb));
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
panel_modes_edit_cb (WButton *button, int action)
{
    (void) button;
    (void) action;
    panel_modes_action_edit ();
    widget_draw (WIDGET (panel_modes_lb));
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
panel_modes_delete_cb (WButton *button, int action)
{
    (void) button;
    (void) action;
    panel_modes_action_delete ();
    widget_draw (WIDGET (panel_modes_lb));
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

/* Enter on the list in management mode edits the current entry (and stays). */
static lcback_ret_t
panel_modes_lb_manage_cb (WListbox *l)
{
    (void) l;
    panel_modes_action_edit ();
    return LISTBOX_CONT;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** switcher dialog -- Left/Right menu and Alt-t ************************************************/
/* --------------------------------------------------------------------------------------------- */

/* A bare list of modes. Enter applies the selected mode to the panel; Esc
   cancels. No buttons, no list editing. */
void
panel_modes_cmd (WPanel *panel)
{
    WDialog *dlg;
    int nlines;
    int res;

    if (panel == NULL)
        return;

    if (panel_modes_count () == 0)
        panel_modes_init ();

    nlines = (int) panel_modes_count ();
    if (nlines > 12)
        nlines = 12;
    if (nlines < 1)
        nlines = 1;

    dlg = dlg_create (TRUE, 0, 0, nlines + 4, 44, WPOS_CENTER, FALSE, dialog_colors, NULL, NULL,
                      "[Panel modes]", _ ("Panel mode"));

    panel_modes_lb = listbox_new (2, 2, nlines, 40, FALSE, NULL);
    panel_modes_lb_fill (panel_modes_index_by_id (panel->view_mode_id));
    group_add_widget (GROUP (dlg), panel_modes_lb);

    res = dlg_run (dlg);

    if (res == B_ENTER)
    {
        panel_mode_t *m = panel_modes_lb_current ();

        if (m != NULL)
            panel_apply_mode (panel, m);
    }

    widget_destroy (WIDGET (dlg));
    panel_modes_lb = NULL;
}

/* --------------------------------------------------------------------------------------------- */
/*** management dialog -- Options menu **********************************************************/
/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
panel_modes_manage_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_UNHANDLED_KEY:
        switch (parm)
        {
        case KEY_IC:
            panel_modes_action_new ();
            return MSG_HANDLED;
        case KEY_DC:
            panel_modes_action_delete ();
            return MSG_HANDLED;
        case KEY_F (4):
            panel_modes_action_edit ();
            return MSG_HANDLED;
        default:
            return MSG_NOT_HANDLED;
        }

    case MSG_POST_KEY:
        /* Keep the focus on the list (buttons act via their hotkeys), and
           refresh it after an action may have changed the contents. */
        if (panel_modes_lb != NULL)
        {
            Widget *lw = WIDGET (panel_modes_lb);

            if (!widget_get_state (lw, WST_FOCUSED))
                widget_select (lw);
            else
                widget_draw (lw);
        }
        return MSG_HANDLED;

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Edit the global list of modes. New/Edit/Delete (also Ins/F4/Del); Enter
   edits the current entry. Does not switch any panel. */
void
panel_modes_manage_cmd (void)
{
    WDialog *dlg;
    WGroup *g;
    int nlines;
    int by;

    if (panel_modes_count () == 0)
        panel_modes_init ();

    nlines = (int) panel_modes_count ();
    if (nlines > 12)
        nlines = 12;
    if (nlines < 1)
        nlines = 1;

    dlg = dlg_create (TRUE, 0, 0, nlines + 6, 52, WPOS_CENTER, FALSE, dialog_colors,
                      panel_modes_manage_callback, NULL, "[Panel modes]", _ ("File panel modes"));
    g = GROUP (dlg);

    panel_modes_lb = listbox_new (2, 2, nlines, 48, FALSE, panel_modes_lb_manage_cb);
    panel_modes_lb_fill (0);
    group_add_widget (g, panel_modes_lb);

    by = nlines + 3;
    group_add_widget (g, hline_new (by - 1, -1, -1));
    group_add_widget (g, button_new (by, 2, B_USER, NORMAL_BUTTON, _ ("&New"), panel_modes_new_cb));
    group_add_widget (g,
                      button_new (by, 12, B_USER, NORMAL_BUTTON, _ ("&Edit"), panel_modes_edit_cb));
    group_add_widget (
        g, button_new (by, 23, B_USER, NORMAL_BUTTON, _ ("&Delete"), panel_modes_delete_cb));
    group_add_widget (g, button_new (by, 35, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    dlg_run (dlg);

    widget_destroy (WIDGET (dlg));
    panel_modes_lb = NULL;
}

/* --------------------------------------------------------------------------------------------- */
