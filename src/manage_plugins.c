/*
   Manage Plugins dialog: enable/disable individual editor and panel plugins.

   Copyright (C) 2025-2026
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

#include "lib/global.h"
#include "lib/tty/tty.h"
#include "lib/keybind.h"  // CK_Enter
#include "lib/widget.h"
#include "lib/widget/table.h"
#include "lib/editor-plugin.h"
#include "lib/panel-plugin.h"
#include "lib/plugin-prefs.h"

#include "src/editor-plugins/builtin-plugins.h"  // editor_plugins_register_all

#include "manage_plugins.h"

/*** file scope macro definitions ****************************************************************/

#define MP_LIST_MAX_H 16

/*** file scope type declarations ****************************************************************/

typedef struct
{
    const char *kind;                      /* "mcedit" or "panel" (display only) */
    mc_plugin_kind_t kind_id;              /* prefs namespace */
    const char *name;                      /* plugin id */
    const char *desc;                      /* display name */
    const mc_panel_plugin_t *panel_plugin; /* panel plugin for settings, else NULL */
} mp_row_t;

/* Passed to the dialog callback so Enter/F4 can reach the selected row. */
typedef struct
{
    WTable *tbl;
    const GArray *rows;
} mp_ctx_t;

/*** file scope functions ************************************************************************/

/* Return TRUE if a row for the given plugin (kind, name) already exists in @rows. */
static gboolean
mp_row_exists (const GArray *rows, mc_plugin_kind_t kind_id, const char *name)
{
    guint i;

    for (i = 0; i < rows->len; i++)
    {
        const mp_row_t *r = &g_array_index (rows, mp_row_t, i);
        if (r->kind_id == kind_id && g_strcmp0 (r->name, name) == 0)
            return TRUE;
    }
    return FALSE;
}

static GArray *
mp_collect_rows (GPtrArray *strpool)
{
    GArray *rows;
    const GSList *l;

    /* Trigger builtin editor plugin registration so this dialog shows them even
     * when the user has not opened an editor yet in this session.  Disabled
     * plugins are filtered out by mc_editor_plugin_add() and are restored below
     * from plugins.ini. */
#ifdef USE_INTERNAL_EDIT
    editor_plugins_register_all ();
#endif

    rows = g_array_new (FALSE, FALSE, sizeof (mp_row_t));

    for (l = mc_editor_plugin_list (); l != NULL; l = g_slist_next (l))
    {
        const mc_editor_plugin_t *p = (const mc_editor_plugin_t *) l->data;
        mp_row_t r;

        if (p->name == NULL)
            continue;

        r.kind = "mcedit";
        r.kind_id = MC_PLUGIN_KIND_EDITOR;
        r.name = p->name;
        r.desc = p->display_name != NULL ? p->display_name : p->name;
        r.panel_plugin = NULL;
        g_array_append_val (rows, r);
    }

    for (l = mc_panel_plugin_list (); l != NULL; l = g_slist_next (l))
    {
        const mc_panel_plugin_t *p = (const mc_panel_plugin_t *) l->data;
        mp_row_t r;

        if (p->name == NULL)
            continue;

        r.kind = "panel";
        r.kind_id = MC_PLUGIN_KIND_PANEL;
        r.name = p->name;
        r.desc = p->display_name != NULL ? p->display_name : p->name;
        r.panel_plugin = p;
        g_array_append_val (rows, r);
    }

    /* Disabled plugins are not in the runtime registries; pull their names from
     * plugins.ini so the user can re-enable them.  Their display metadata is
     * unavailable until the plugin is registered again. */
    {
        static const struct
        {
            mc_plugin_kind_t kind_id;
            const char *kind;
        } kinds[] = {
            { MC_PLUGIN_KIND_EDITOR, "mcedit" },
            { MC_PLUGIN_KIND_PANEL, "panel" },
        };
        gsize k;

        for (k = 0; k < G_N_ELEMENTS (kinds); k++)
        {
            gchar **disabled = mc_plugin_prefs_list_disabled (kinds[k].kind_id);
            gchar **it;

            if (disabled == NULL)
                continue;

            for (it = disabled; *it != NULL; it++)
            {
                mp_row_t r;
                char *owned;

                if (mp_row_exists (rows, kinds[k].kind_id, *it))
                    continue;

                owned = g_strdup (*it);
                g_ptr_array_add (strpool, owned);

                r.kind = kinds[k].kind;
                r.kind_id = kinds[k].kind_id;
                r.name = owned;
                r.desc = _ ("(disabled, not loaded)");
                r.panel_plugin = NULL;
                g_array_append_val (rows, r);
            }
            g_strfreev (disabled);
        }
    }

    return rows;
}

/* --------------------------------------------------------------------------------------------- */

static int
mp_get_nrows (const void *data)
{
    const GArray *rows = (const GArray *) data;
    return rows != NULL ? (int) rows->len : 0;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mp_get_text (const void *data, int row, int col)
{
    const GArray *rows = (const GArray *) data;
    const mp_row_t *r;

    if (rows == NULL || row < 0 || row >= (int) rows->len)
        return "";

    r = &g_array_index (rows, mp_row_t, (guint) row);

    switch (col)
    {
    case 1:
        return r->kind != NULL ? r->kind : "";
    case 2:
        return r->name != NULL ? r->name : "";
    case 3:
        return r->desc != NULL ? r->desc : "";
    default:
        return "";
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mp_get_checked (const void *data, int row, int col)
{
    const GArray *rows = (const GArray *) data;
    const mp_row_t *r;

    (void) col;

    if (rows == NULL || row < 0 || row >= (int) rows->len)
        return FALSE;

    r = &g_array_index (rows, mp_row_t, (guint) row);
    /* Check == enabled.  Disabled plugins are listed in plugins.ini. */
    return !mc_plugin_prefs_is_disabled (r->kind_id, r->name);
}

/* --------------------------------------------------------------------------------------------- */

static void
mp_set_checked (void *data, int row, int col, gboolean val)
{
    GArray *rows = (GArray *) data;
    const mp_row_t *r;

    (void) col;

    if (rows == NULL || row < 0 || row >= (int) rows->len)
        return;

    r = &g_array_index (rows, mp_row_t, (guint) row);
    mc_plugin_prefs_set_disabled (r->kind_id, r->name, !val);
}

/* --------------------------------------------------------------------------------------------- */

/* Open the settings dialog of the plugin under the table cursor, if it has one. */
static void
mp_invoke_settings (const mp_ctx_t *ctx)
{
    int row;
    const mp_row_t *r;

    if (ctx == NULL || ctx->rows == NULL)
        return;

    row = table_get_current (ctx->tbl);
    if (row < 0 || row >= (int) ctx->rows->len)
        return;

    r = &g_array_index (ctx->rows, mp_row_t, (guint) row);
    if (r->panel_plugin == NULL || r->panel_plugin->configure == NULL)
    {
        message (D_NORMAL, _ ("Manage Plugins"), "%s", _ ("This plugin has no settings."));
        return;
    }

    r->panel_plugin->configure ();
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
mp_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    const mp_ctx_t *ctx = (const mp_ctx_t *) DIALOG (w)->data.p;

    switch (msg)
    {
    case MSG_NOTIFY:
        if (ctx != NULL && sender == WIDGET (ctx->tbl) && parm == CK_Enter)
        {
            mp_invoke_settings (ctx);
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    case MSG_UNHANDLED_KEY:
        if (ctx != NULL && parm == KEY_F (4))
        {
            mp_invoke_settings (ctx);
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
manage_plugins_dialog (void)
{
    GArray *rows;
    GPtrArray *strpool; /* owns names restored from plugins.ini */
    WDialog *dlg;
    WTable *tbl;
    int n_rows, table_h, dlg_h, dlg_w, table_w;
    int check_w, kind_w, name_w, desc_w;
    table_column_def_t col_defs[4];
    table_datasource_t ds;
    mp_ctx_t ctx;

    strpool = g_ptr_array_new_with_free_func (g_free);
    rows = mp_collect_rows (strpool);
    n_rows = (int) rows->len;

    if (n_rows == 0)
    {
        g_array_free (rows, TRUE);
        g_ptr_array_free (strpool, TRUE);
        message (D_NORMAL, _ ("Manage Plugins"), "%s", _ ("No plugins loaded."));
        return;
    }

    table_h = MIN (n_rows, MP_LIST_MAX_H);
    table_h = MAX (table_h, 3);

    /* border(1) + table(N) + hline(1) + button(1) + border(1) */
    dlg_h = table_h + 4;
    dlg_w = MIN (COLS - 4, 78);

    /* columns: check(4) | kind(8) | name(14) | description(rest) */
    table_w = dlg_w - 2;
    check_w = 4;
    kind_w = 8;
    name_w = 14;
    desc_w = table_w - check_w - kind_w - name_w - 5; /* 5 = 1 margin + 3 seps + 1 scrollbar */
    if (desc_w < 10)
        desc_w = 10;

    col_defs[0].width = check_w;
    col_defs[0].align = J_CENTER;
    col_defs[0].type = TABLE_COL_CHECK;
    col_defs[1].width = kind_w;
    col_defs[1].align = J_LEFT;
    col_defs[1].type = TABLE_COL_TEXT;
    col_defs[2].width = name_w;
    col_defs[2].align = J_LEFT;
    col_defs[2].type = TABLE_COL_TEXT;
    col_defs[3].width = desc_w;
    col_defs[3].align = J_LEFT;
    col_defs[3].type = TABLE_COL_TEXT;

    dlg = dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w,
                      WPOS_KEEP_DEFAULT, TRUE, dialog_colors, mp_dlg_callback, NULL,
                      "[Manage Plugins]", _ ("Manage Plugins"));

    tbl = table_new (1, 1, table_h, table_w, 4, col_defs);
    tbl->scrollbar = TRUE;

    ctx.tbl = tbl;
    ctx.rows = rows;
    dlg->data.p = &ctx;

    ds.get_nrows = mp_get_nrows;
    ds.get_text = mp_get_text;
    ds.get_checked = mp_get_checked;
    ds.set_checked = mp_set_checked;
    ds.data = rows;
    table_set_datasource (tbl, ds);

    group_add_widget (GROUP (dlg), tbl);
    group_add_widget (GROUP (dlg), hline_new (dlg_h - 3, -1, -1));
    group_add_widget (
        GROUP (dlg),
        button_new (dlg_h - 2, (dlg_w - 10) / 2, B_CANCEL, NORMAL_BUTTON, _ ("&Close"), NULL));

    dlg_run (dlg);
    widget_destroy (WIDGET (dlg));

    g_array_free (rows, TRUE);
    g_ptr_array_free (strpool, TRUE);
}
