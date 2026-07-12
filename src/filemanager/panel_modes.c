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

#include "filemanager.h"  // left_panel, right_panel
#include "layout.h"       // get_panel_type
#include "panel.h"        // panel_get_field_by_id
#include "panel_modes.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define PANEL_MODES_SECTION "Panel modes"

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
    panel_mode_set (mode, name, types, widths, status_types, status_widths);
    return mode;
}

/* --------------------------------------------------------------------------------------------- */

/* Deep copy preserving the id (does not bump panel_modes_next_id). */
static panel_mode_t *
panel_mode_copy (const panel_mode_t *m)
{
    panel_mode_t *c =
        panel_mode_new (m->name, m->types, m->widths, m->status_types, m->status_widths);

    panel_modes_next_id--;  // give back the freshly burnt id
    c->id = m->id;
    return c;
}

/* --------------------------------------------------------------------------------------------- */

/* Snapshot of the whole mode list, used to roll back the manager on Cancel. */
static GPtrArray *
panel_modes_clone (void)
{
    GPtrArray *dst = g_ptr_array_new_with_free_func (panel_mode_free);
    guint i;

    for (i = 0; i < panel_modes->len; i++)
        g_ptr_array_add (dst, panel_mode_copy (g_ptr_array_index (panel_modes, i)));
    return dst;
}

/* --------------------------------------------------------------------------------------------- */

/* One width entry: empty or a non-negative integer (0 means auto). Returns
   FALSE on anything else; *n is always set (0 unless a positive width). */
static gboolean
panel_mode_parse_width_str (const char *ws, long *n)
{
    char *endp = NULL;
    long v;

    *n = 0;
    if (ws[0] == '\0')
        return TRUE;
    v = strtol (ws, &endp, 10);
    if (endp == ws || *endp != '\0' || v < 0)
        return FALSE;
    *n = v;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* Parse the i-th comma-separated width; 0 means auto (missing/zero/invalid).
   wlen bounds warr: the widths list may be shorter than the types list. */
static long
panel_mode_parse_width (char **warr, guint wlen, guint i)
{
    long n = 0;

    if (warr != NULL && i < wlen)
        (void) panel_mode_parse_width_str (g_strstrip (warr[i]), &n);
    return n;
}

/* --------------------------------------------------------------------------------------------- */

/* Append the column part after the "half " prefix. If every column is the same
   field set (e.g. "name,name"), emit mc's multi-column form "N fields" so the
   listing flows across N side-by-side columns; otherwise emit distinct columns
   joined by " | ". Width 0 or missing leaves a field auto-sized. */
static void
panel_mode_append_columns (GString *out, const char *types, const char *widths)
{
    char **tarr;
    char **warr;
    guint wlen;
    guint i;
    guint cols = 0;
    int first_idx = -1;
    gboolean all_same = TRUE;

    if (types == NULL || types[0] == '\0')
        return;

    tarr = g_strsplit (types, ",", -1);
    warr = widths != NULL ? g_strsplit (widths, ",", -1) : NULL;
    wlen = warr != NULL ? g_strv_length (warr) : 0;

    for (i = 0; tarr[i] != NULL; i++)
    {
        g_strstrip (tarr[i]);
        if (tarr[i][0] == '\0')
            continue;
        if (first_idx < 0)
            first_idx = (int) i;
        else if (strcmp (tarr[first_idx], tarr[i]) != 0)
            all_same = FALSE;
        cols++;
    }

    if (cols >= 2 && all_same)
    {
        /* Repeated identical columns -> mc multi-column listing (files flow). */
        long w = panel_mode_parse_width (warr, wlen, (guint) first_idx);

        g_string_append_printf (out, "%u %s", cols, tarr[first_idx]);
        if (w > 0)
            g_string_append_printf (out, ":%ld", w);
    }
    else
    {
        gboolean first = TRUE;

        for (i = 0; tarr[i] != NULL; i++)
        {
            long w;

            if (tarr[i][0] == '\0')
                continue;
            w = panel_mode_parse_width (warr, wlen, i);
            /* Column separator goes BETWEEN columns only, never before the
               first (the caller already put the "half " prefix into `out`). */
            if (!first)
                g_string_append (out, " | ");
            first = FALSE;
            g_string_append (out, tarr[i]);
            if (w > 0)
                g_string_append_printf (out, ":%ld", w);
        }
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
    g_ptr_array_add (panel_modes,
                     panel_mode_new ("Brief", "type name,type name", "0,0",
                                     "type name space bsize space perm space", "0"));
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

    /* First pass: learn every stored id, so ids invented for rows that lack
       the key (hand-edited config) cannot collide with a stored one. */
    for (i = 0; i < count; i++)
    {
        char key[BUF_TINY];
        int id;

        g_snprintf (key, sizeof (key), "%d_id", i);
        id = mc_config_get_int (mc_global.main_config, PANEL_MODES_SECTION, key, 0);
        if (id > 0 && (guint) id >= panel_modes_next_id)
            panel_modes_next_id = (guint) id + 1;
    }

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
            panel_modes_next_id--;  // give back the freshly burnt id
            m->id = (guint) id;
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
    {
        g_ptr_array_remove_index (panel_modes, index);
        /* At least one mode must always exist; reseed if the list was emptied. */
        if (panel_modes->len == 0)
            panel_modes_seed_defaults ();
    }
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

void
panel_mode_normalize (const char *types, const char *widths, char **norm_types, char **norm_widths)
{
    char *t;
    char **cols;
    char **uw;
    guint uwlen;
    guint repeat = 1;
    guint r;
    guint ci;
    guint i;
    GPtrArray *ncols;
    GArray *ncolw;
    gboolean any_width = FALSE;
    GString *nt;

    t = g_strdup (types != NULL ? types : "");
    g_strstrip (t);

    /* A pasted listing format starts with the panel width keyword; modes are
       always half-width, so both are just dropped. */
    if (g_str_has_prefix (t, "half ") || g_str_has_prefix (t, "full "))
        memmove (t, t + 5, strlen (t + 5) + 1);
    g_strchug (t);

    /* "N fields": one column set flowed over N columns -> N repeated columns
       (only when the rest holds no column separator of its own). */
    if (g_ascii_isdigit (t[0]) && t[1] == ' ' && strchr (t, ',') == NULL && strchr (t, '|') == NULL)
    {
        repeat = (guint) (t[0] - '0');
        if (repeat == 0)
            repeat = 1;
        memmove (t, t + 1, strlen (t + 1) + 1);
    }

    /* The listing format separates columns with "|", the editor with ",". */
    g_strdelimit (t, "|", ',');

    uw = (widths != NULL && widths[0] != '\0') ? g_strsplit (widths, ",", -1) : NULL;
    uwlen = uw != NULL ? g_strv_length (uw) : 0;

    ncols = g_ptr_array_new_with_free_func (g_free);
    ncolw = g_array_new (FALSE, FALSE, sizeof (long));

    cols = g_strsplit (t, ",", -1);
    for (i = 0; cols[i] != NULL; i++)
    {
        char **fields;
        GString *col = g_string_new (NULL);
        long cw = -1;
        guint j;

        fields = g_strsplit_set (cols[i], " \t", -1);
        for (j = 0; fields[j] != NULL; j++)
        {
            char *colon;

            if (fields[j][0] == '\0')
                continue;

            /* "field:width" -> strip the suffix into the column width. */
            colon = strchr (fields[j], ':');
            if (colon != NULL)
            {
                long n;

                if (panel_mode_parse_width_str (colon + 1, &n))
                {
                    *colon = '\0';
                    cw = n;
                    any_width = TRUE;
                }
            }
            if (fields[j][0] == '\0')
                continue;

            if (col->len != 0)
                g_string_append_c (col, ' ');
            g_string_append (col, fields[j]);
        }
        g_strfreev (fields);

        if (col->len == 0)
            g_string_free (col, TRUE);
        else
        {
            g_ptr_array_add (ncols, g_string_free (col, FALSE));
            g_array_append_val (ncolw, cw);
        }
    }
    g_strfreev (cols);
    g_free (t);

    /* Columns without a ":width" suffix keep the entered width (by position). */
    for (ci = 0; ci < ncolw->len; ci++)
        if (g_array_index (ncolw, long, ci) < 0)
        {
            long n = 0;

            if (ci < uwlen)
                (void) panel_mode_parse_width_str (g_strstrip (uw[ci]), &n);
            g_array_index (ncolw, long, ci) = n;
        }

    nt = g_string_new (NULL);
    for (r = 0; r < repeat; r++)
        for (ci = 0; ci < ncols->len; ci++)
        {
            if (nt->len != 0)
                g_string_append_c (nt, ',');
            g_string_append (nt, (const char *) g_ptr_array_index (ncols, ci));
        }
    *norm_types = g_string_free (nt, FALSE);

    if (!any_width)
        // no suffixes anywhere: leave the widths input exactly as entered
        *norm_widths = g_strdup (widths != NULL ? widths : "");
    else
    {
        GString *nw = g_string_new (NULL);

        for (r = 0; r < repeat; r++)
            for (ci = 0; ci < ncolw->len; ci++)
            {
                if (nw->len != 0)
                    g_string_append_c (nw, ',');
                g_string_append_printf (nw, "%ld", g_array_index (ncolw, long, ci));
            }
        *norm_widths = g_string_free (nw, FALSE);
    }

    g_ptr_array_free (ncols, TRUE);
    g_array_free (ncolw, TRUE);
    if (uw != NULL)
        g_strfreev (uw);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
panel_mode_validate (const char *types, const char *widths, char **error)
{
    char **tarr;
    char **warr;
    guint i;
    guint ncols = 0;
    gboolean ok = TRUE;
    gboolean has_field = FALSE;

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
        gboolean col_has_field = FALSE;
        guint j;

        for (j = 0; ok && fields[j] != NULL; j++)
        {
            if (fields[j][0] == '\0')
                continue;
            col_has_field = TRUE;
            has_field = TRUE;
            if (panel_get_field_by_id (fields[j]) == NULL)
            {
                if (error != NULL)
                    *error = g_strdup_printf (_ ("Unknown column type: %s"), fields[j]);
                ok = FALSE;
            }
        }
        g_strfreev (fields);

        /* The listing format holds the column count in a single digit, so
           more than 9 repeated columns cannot be expressed. */
        if (ok && col_has_field && ++ncols > 9)
        {
            if (error != NULL)
                *error = g_strdup (_ ("Too many columns (at most 9)."));
            ok = FALSE;
        }
    }
    g_strfreev (tarr);
    if (!ok)
        return FALSE;

    /* Whitespace/comma-only input parses to no fields at all. */
    if (!has_field)
    {
        if (error != NULL)
            *error = g_strdup (_ ("Column types must not be empty."));
        return FALSE;
    }

    if (widths == NULL || widths[0] == '\0')
        return TRUE;

    warr = g_strsplit (widths, ",", -1);
    for (i = 0; ok && warr[i] != NULL; i++)
    {
        char *ws = g_strstrip (warr[i]);
        long n;

        if (!panel_mode_parse_width_str (ws, &n))
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

    /* set_panel_formats reverts to the default format on a bad string (a mode
       can be corrupted in the config); only record the mode as active if the
       format actually took. */
    if (set_panel_formats (panel) == 0)
        panel->view_mode_id = mode->id;
}

/* --------------------------------------------------------------------------------------------- */
/*** dialogs *************************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* Editor for a single mode. Returns TRUE if the mode was changed. */
static gboolean
panel_mode_edit_dialog (panel_mode_t *mode)
{
    char *name = g_strdup (mode->name);
    char *types = g_strdup (mode->types);
    char *widths = g_strdup (mode->widths);
    char *stypes = g_strdup (mode->status_types);
    char *swidths = g_strdup (mode->status_widths);
    gboolean ok = FALSE;

    while (TRUE)
    {
        char *new_name = NULL;
        char *new_types = NULL;
        char *new_widths = NULL;
        char *new_stypes = NULL;
        char *new_swidths = NULL;
        char *err = NULL;
        int res;

        {
            quick_widget_t quick_widgets[] = {
                // clang-format off
                QUICK_LABELED_INPUT (_ ("Name:"), input_label_above, name, "pm-name",
                                     &new_name, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
                QUICK_START_COLUMNS,
                    QUICK_LABELED_INPUT (_ ("Column types:"), input_label_above, types,
                                         "pm-types", &new_types, NULL, FALSE, FALSE,
                                         INPUT_COMPLETE_NONE),
                    QUICK_LABELED_INPUT (_ ("Column widths:"), input_label_above, widths,
                                         "pm-widths", &new_widths, NULL, FALSE, FALSE,
                                         INPUT_COMPLETE_NONE),
                QUICK_NEXT_COLUMN,
                    QUICK_LABELED_INPUT (_ ("Status column types:"), input_label_above,
                                         stypes, "pm-stypes", &new_stypes, NULL, FALSE, FALSE,
                                         INPUT_COMPLETE_NONE),
                    QUICK_LABELED_INPUT (_ ("Status column widths:"), input_label_above,
                                         swidths, "pm-swidths", &new_swidths, NULL, FALSE,
                                         FALSE, INPUT_COMPLETE_NONE),
                QUICK_STOP_COLUMNS,
                QUICK_BUTTONS_OK_CANCEL,
                QUICK_END,
                // clang-format on
            };

            WRect r = { -1, -1, 0, 68 };
            quick_dialog_t qdlg = {
                .rect = r,
                .title = _ ("Panel mode"),
                .help = "[Panel modes]",
                .widgets = quick_widgets,
                .callback = NULL,
                .mouse_callback = NULL,
            };

            res = quick_dialog (&qdlg);
        }

        if (res != B_ENTER)
        {
            g_free (new_name);
            g_free (new_types);
            g_free (new_widths);
            g_free (new_stypes);
            g_free (new_swidths);
            break;
        }

        g_free (name);
        name = new_name;
        g_free (types);
        g_free (widths);
        // accept a pasted listing format ("half name | size:7 | type mode:3")
        panel_mode_normalize (new_types, new_widths, &types, &widths);
        g_free (new_types);
        g_free (new_widths);
        g_free (stypes);
        g_free (swidths);
        panel_mode_normalize (new_stypes, new_swidths, &stypes, &swidths);
        g_free (new_stypes);
        g_free (new_swidths);

        if (panel_mode_validate (types, widths, &err)
            && panel_mode_validate (stypes, swidths, &err))
        {
            panel_mode_set (mode, name, types, widths, stypes, swidths);
            ok = TRUE;
            break;
        }

        message (D_ERROR, MSG_ERROR, "%s", err);
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

/* Shared tail of New/Copy: the fresh mode was appended at the end of the
   list; edit it and drop it again if the editor was cancelled. */
static void
panel_modes_edit_appended (panel_mode_t *m)
{
    if (m == NULL)
        return;
    if (!panel_mode_edit_dialog (m))
        panel_modes_remove (panel_modes_count () - 1);
    panel_modes_lb_fill (panel_modes_count () - 1);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_modes_action_new (void)
{
    panel_modes_edit_appended (panel_modes_add (NULL));
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_modes_action_edit (void)
{
    int cur = panel_modes_lb->current;
    panel_mode_t *m = panel_modes_lb_current ();

    if (m != NULL && panel_mode_edit_dialog (m))
        panel_modes_lb_fill (cur);
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_modes_action_copy (void)
{
    panel_modes_edit_appended (panel_modes_dup ((guint) panel_modes_lb->current));
}

/* --------------------------------------------------------------------------------------------- */

static void
panel_modes_action_delete (void)
{
    int cur = panel_modes_lb->current;
    panel_mode_t *m = panel_modes_lb_current ();
    char *msg;
    int res;

    if (m == NULL)
        return;
    if (panel_modes_count () <= 1)
    {
        message (D_ERROR, MSG_ERROR, "%s", _ ("Cannot delete the last panel mode."));
        return;
    }
    msg = g_strdup_printf (_ ("Delete the panel mode \"%s\"?"), m->name);
    res = query_dialog (_ ("Delete panel mode"), msg, D_ERROR, 2, _ ("&Yes"), _ ("&No"));
    g_free (msg);
    if (res != 0)
        return;
    panel_modes_remove ((guint) cur);
    panel_modes_lb_fill (cur > 0 ? cur - 1 : 0);
}

/* --------------------------------------------------------------------------------------------- */

/* Drop every mode and reseed the built-in defaults. */
static void
panel_modes_action_reset (void)
{
    if (panel_modes == NULL)
        return;
    g_ptr_array_set_size (panel_modes, 0);  // frees entries via the array free func
    panel_modes_seed_defaults ();
    panel_modes_lb_fill (0);
}

/* --------------------------------------------------------------------------------------------- */

static int
panel_modes_reset_cb (WButton *button, int action)
{
    (void) button;
    (void) action;
    panel_modes_action_reset ();
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

static widget_cb_fn panel_modes_lb_super_cb = NULL;

/* The listbox keymap binds Del to its own (here no-op) delete and swallows it;
   intercept Del at the widget level so it removes a panel mode instead. */
static cb_ret_t
panel_modes_lb_key_cb (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_KEY && parm == KEY_DC)
    {
        panel_modes_action_delete ();
        return MSG_HANDLED;
    }
    return panel_modes_lb_super_cb (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/*** switcher dialog -- Left/Right menu and Alt-t ************************************************/
/* --------------------------------------------------------------------------------------------- */

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
    case MSG_NOTIFY:
        if (sender == WIDGET (panel_modes_lb) && parm == CK_Enter)
        {
            panel_modes_action_edit ();
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    case MSG_UNHANDLED_KEY:
        switch (parm)
        {
        case KEY_IC:
            panel_modes_action_new ();
            return MSG_HANDLED;
        case KEY_DC:
        case KEY_F (8):
            panel_modes_action_delete ();
            return MSG_HANDLED;
        case KEY_F (4):
            panel_modes_action_edit ();
            return MSG_HANDLED;
        case KEY_F (5):
            panel_modes_action_copy ();
            return MSG_HANDLED;
        default:
            return MSG_NOT_HANDLED;
        }

    case MSG_POST_KEY:
        /* Keep focus on the list and redraw after actions change it. */
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

/* The mode list changed: re-apply the (possibly edited) mode to a panel that
   references it, or drop the reference if the mode was deleted. */
static void
panel_modes_sync_panel (WPanel *panel)
{
    const panel_mode_t *m;

    if (panel == NULL || panel->view_mode_id == 0)
        return;

    m = panel_modes_get_by_id (panel->view_mode_id);
    if (m == NULL)
        panel->view_mode_id = 0;
    else
        panel_apply_mode (panel, m);
}

/* --------------------------------------------------------------------------------------------- */

void
panel_modes_manage_cmd (void)
{
    WDialog *dlg;
    WGroup *g;
    GPtrArray *snapshot;
    guint saved_next_id;
    int nlines;
    int by;
    int res;

    if (panel_modes_count () == 0)
        panel_modes_init ();

    /* Snapshot the live list so Cancel can roll back in-place edits. */
    snapshot = panel_modes_clone ();
    saved_next_id = panel_modes_next_id;

    nlines = (int) panel_modes_count ();
    if (nlines > 12)
        nlines = 12;
    if (nlines < 1)
        nlines = 1;

    dlg = dlg_create (TRUE, 0, 0, nlines + 6, 52, WPOS_CENTER, FALSE, dialog_colors,
                      panel_modes_manage_callback, NULL, "[Panel modes]", _ ("File panel modes"));
    g = GROUP (dlg);

    panel_modes_lb = listbox_new (2, 2, nlines, 48, FALSE, panel_modes_lb_manage_cb);
    panel_modes_lb_super_cb = WIDGET (panel_modes_lb)->callback;
    WIDGET (panel_modes_lb)->callback = panel_modes_lb_key_cb;
    panel_modes_lb_fill (0);
    group_add_widget (g, panel_modes_lb);

    by = nlines + 3;
    group_add_widget (g, hline_new (by - 1, -1, -1));
    /* Add/remove modes are Ins/Del keys (handled in the dialog callback); the
       row keeps only Reset-defaults, Ok and Cancel. */
    group_add_widget (
        g, button_new (by, 4, B_USER, NORMAL_BUTTON, _ ("&Defaults"), panel_modes_reset_cb));
    group_add_widget (g, button_new (by, 30, B_ENTER, NORMAL_BUTTON, _ ("&Ok"), NULL));
    group_add_widget (g, button_new (by, 38, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    res = dlg_run (dlg);

    panel_modes_lb = NULL;

    if (res == B_ENTER)
    {
        /* OK: keep the edits and write them to disk now. */
        g_ptr_array_free (snapshot, TRUE);
        panel_modes_save ();
        mc_config_save_file (mc_global.main_config, NULL);

        /* Panels displaying an edited or deleted mode must not keep the
           stale baked format (or a dangling mode id). left_panel/right_panel
           are valid only while the pane holds a listing. */
        if (get_panel_type (0) == view_listing)
            panel_modes_sync_panel (left_panel);
        if (get_panel_type (1) == view_listing)
            panel_modes_sync_panel (right_panel);
        do_refresh ();
    }
    else
    {
        /* Cancel/Esc: discard everything done in this dialog. */
        g_ptr_array_free (panel_modes, TRUE);
        panel_modes = snapshot;
        panel_modes_next_id = saved_next_id;
    }

    widget_destroy (WIDGET (dlg));
    panel_modes_lb_super_cb = NULL;
}

/* --------------------------------------------------------------------------------------------- */
