/*
   Archive browser panel plugin -archiver settings dialog.

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
#include "lib/tty/key.h"
#include "lib/keybind.h"
#include "lib/widget.h"

#include "arcmc-types.h"
#include "arcmc-config.h"
#include "archive-io.h"
#include "dialog-settings.h"

/*** file scope variables ************************************************************************/

/* ext archivers table widget - used by the dialog callback to identify Enter sender */
static WTable *settings_tbl_ext = NULL;

/* Builtin libarchive formats for the settings dialog */
static const struct
{
    const char *name;
    const char *ext;
    gboolean can_pack;
    gboolean can_unpack;
} builtin_formats[] = {
    /* pack + unpack */
    { "ZIP", ".zip", TRUE, TRUE },
    { "7Z", ".7z", TRUE, TRUE },
    { "TAR.GZ", ".tar.gz", TRUE, TRUE },
    { "TAR.BZ2", ".tar.bz2", TRUE, TRUE },
    { "TAR.XZ", ".tar.xz", TRUE, TRUE },
    { "TAR", ".tar", TRUE, TRUE },
    { "CPIO", ".cpio", TRUE, TRUE },
    /* unpack only (libarchive reads but arcmc does not write) */
    { "TAR.ZST", ".tar.zst", FALSE, TRUE },
    { "TAR.LZ", ".tar.lz", FALSE, TRUE },
    { "TAR.LZMA", ".tar.lzma", FALSE, TRUE },
    { "ISO", ".iso", FALSE, TRUE },
    { "XAR", ".xar", FALSE, TRUE },
    { "CAB", ".cab", FALSE, TRUE },
};

/*** file scope macro definitions ****************************************************************/

/* Column widths for the settings table: Format(7) | Ext(8) | Pack(9) | Unpack(11) | On(3) */
#define SETTINGS_TABLE_NCOLS 5
#define SETTINGS_TABLE_WIDTH 42 /* 7+1+8+1+9+1+11+1+3 = 42 */

/*** file scope functions ************************************************************************/

/* Show sub-dialog for editing external archiver parameters. */
static void
arcmc_show_ext_params_dialog (size_t idx)
{
    arcmc_ext_archiver_t *a;
    char *pack_bin = NULL;
    char *pack_args = NULL;
    char *unpack_bin = NULL;
    char *unpack_args = NULL;
    char *test_bin = NULL;
    char *test_args = NULL;
    char *list_file_arg = NULL;
    char *extfs_helper = NULL;
    char title[64];
    int ret;

    a = &ext_archivers[idx];
    g_snprintf (title, sizeof (title), "%s parameters", a->name);

    {
        /* *INDENT-OFF* */
        quick_widget_t quick_widgets[] = {
            QUICK_LABELED_INPUT (N_ ("Pack binary:"), input_label_above,
                                 a->pack_bin != NULL ? a->pack_bin : "", "arcmc-ext-pack-bin",
                                 &pack_bin, NULL, FALSE, FALSE, INPUT_COMPLETE_FILENAMES),
            QUICK_LABELED_INPUT (N_ ("Pack arguments:"), input_label_above,
                                 a->pack_args != NULL ? a->pack_args : "", "arcmc-ext-pack-args",
                                 &pack_args, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Unpack binary:"), input_label_above,
                                 a->unpack_bin != NULL ? a->unpack_bin : "", "arcmc-ext-unpack-bin",
                                 &unpack_bin, NULL, FALSE, FALSE, INPUT_COMPLETE_FILENAMES),
            QUICK_LABELED_INPUT (N_ ("Unpack arguments:"), input_label_above,
                                 a->unpack_args != NULL ? a->unpack_args : "",
                                 "arcmc-ext-unpack-args", &unpack_args, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Test binary:"), input_label_above,
                                 a->test_bin != NULL ? a->test_bin : "", "arcmc-ext-test-bin",
                                 &test_bin, NULL, FALSE, FALSE, INPUT_COMPLETE_FILENAMES),
            QUICK_LABELED_INPUT (N_ ("Test arguments:"), input_label_above,
                                 a->test_args != NULL ? a->test_args : "", "arcmc-ext-test-args",
                                 &test_args, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("File list argument:"), input_label_above,
                                 a->list_file_arg != NULL ? a->list_file_arg : "",
                                 "arcmc-ext-list-file-arg", &list_file_arg, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Extfs helper:"), input_label_above,
                                 a->extfs_helper != NULL ? a->extfs_helper : "",
                                 "arcmc-ext-extfs-helper", &extfs_helper, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
        };
        /* *INDENT-ON* */

        WRect r = { -1, -1, 0, 50 };

        quick_dialog_t qdlg = {
            .rect = r,
            .title = title,
            .help = "[arcmc]",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        ret = quick_dialog (&qdlg);
    }

    if (ret == B_ENTER)
    {
        /* helper: set field from dialog result, empty string -> NULL */
#define SET_FIELD(field, val)                                                                      \
    do                                                                                             \
    {                                                                                              \
        if (val != NULL && val[0] != '\0')                                                         \
            a->field = val;                                                                        \
        else                                                                                       \
        {                                                                                          \
            a->field = NULL;                                                                       \
            g_free (val);                                                                          \
        }                                                                                          \
    }                                                                                              \
    while (0)

        SET_FIELD (pack_bin, pack_bin);
        SET_FIELD (pack_args, pack_args);
        SET_FIELD (unpack_bin, unpack_bin);
        SET_FIELD (unpack_args, unpack_args);
        SET_FIELD (test_bin, test_bin);
        SET_FIELD (test_args, test_args);
        SET_FIELD (list_file_arg, list_file_arg);
        SET_FIELD (extfs_helper, extfs_helper);

#undef SET_FIELD

        arcmc_config_save ();
    }
    else
    {
        g_free (pack_bin);
        g_free (pack_args);
        g_free (unpack_bin);
        g_free (unpack_args);
        g_free (test_bin);
        g_free (test_args);
        g_free (list_file_arg);
        g_free (extfs_helper);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Dialog callback for the settings dialog - F4 edits ext archiver params,
   double-click on ext archiver row also opens the editor. */
static cb_ret_t
settings_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_UNHANDLED_KEY:
        if (parm == KEY_F (4) && settings_tbl_ext != NULL
            && widget_get_state (WIDGET (settings_tbl_ext), WST_FOCUSED))
        {
            int row = table_get_current (settings_tbl_ext);

            if (row >= 0 && row < (int) ext_archivers_count)
                arcmc_show_ext_params_dialog ((size_t) row);
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    case MSG_NOTIFY:
        /* double-click on ext archiver row */
        if (sender != NULL && settings_tbl_ext != NULL && sender == WIDGET (settings_tbl_ext)
            && parm == CK_Enter)
        {
            int row = table_get_current (settings_tbl_ext);

            if (row >= 0 && row < (int) ext_archivers_count)
                arcmc_show_ext_params_dialog ((size_t) row);
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/*** public functions ****************************************************************************/

void
arcmc_show_settings_dialog (void)
{
    static const table_column_def_t col_defs[SETTINGS_TABLE_NCOLS] = {
        { 7, J_LEFT, TABLE_COL_TEXT },    /* Format */
        { 8, J_LEFT, TABLE_COL_TEXT },    /* Ext */
        { 9, J_LEFT, TABLE_COL_TEXT },    /* Pack */
        { 11, J_LEFT, TABLE_COL_TEXT },   /* Unpack */
        { 3, J_CENTER, TABLE_COL_CHECK }, /* On */
    };

    WDialog *dlg;
    WGroup *g;
    WTable *tbl_builtin;
    WTable *tbl_ext;
    int dlg_width = 49;
    int dlg_height;
    int builtin_lines;
    int ext_lines;
    int y;
    size_t i;

    builtin_lines = ((int) G_N_ELEMENTS (builtin_formats) * 2 + 2) / 3;
    ext_lines = ((int) ext_archivers_count * 2 + 2) / 3;
    /* header(1) + separator(1) + builtins + hline(1) + externals + hline(1) + button(1) */
    dlg_height = 1 + 1 + builtin_lines + 1 + ext_lines + 1 + 1 + 2;

    dlg = dlg_create (TRUE, 0, 0, dlg_height, dlg_width, WPOS_CENTER, TRUE, dialog_colors,
                      settings_dlg_callback, NULL, "[arcmc]", _ ("Archiver settings"));
    g = GROUP (dlg);

    y = 1;

    /* column header */
    {
        WLabel *lbl;

        lbl = label_new (y++, 3, _ ("Format  Ext      Pack      Unpack      On"));
        lbl->color_idx = DLG_COLOR_TITLE;
        group_add_widget (g, lbl);
    }

    {
        WHLine *hl;

        hl = hline_new (y++, -1, -1);
        hl->text_color_idx = DLG_COLOR_TITLE;
        hline_set_text (hl, _ (" Builtin (libarchive) "));
        group_add_widget (g, hl);
    }

    /* builtin libarchive formats */
    tbl_builtin = table_new (y, 2, builtin_lines, dlg_width - 4, SETTINGS_TABLE_NCOLS, col_defs);
    for (i = 0; i < G_N_ELEMENTS (builtin_formats); i++)
    {
        table_add_row (tbl_builtin, builtin_formats[i].name, builtin_formats[i].ext,
                       builtin_formats[i].can_pack ? "builtin" : "-",
                       builtin_formats[i].can_unpack ? "builtin" : "-", "");
        table_set_checked (tbl_builtin, (int) i, 4, arcmc_builtin_enabled[i]);
    }
    group_add_widget (g, tbl_builtin);
    y += builtin_lines;

    {
        WHLine *hl;

        hl = hline_new (y++, -1, -1);
        hl->text_color_idx = DLG_COLOR_TITLE;
        hline_set_text (hl, _ (" External archivers "));
        group_add_widget (g, hl);
    }

    /* external archivers */
    tbl_ext = table_new (y, 2, ext_lines, dlg_width - 4, SETTINGS_TABLE_NCOLS, col_defs);
    settings_tbl_ext = tbl_ext;
    for (i = 0; i < ext_archivers_count; i++)
    {
        const arcmc_ext_archiver_t *a = &ext_archivers[i];
        const char *pack_str;
        const char *unpack_str;
        gboolean have_pack, have_unpack;

        if (a->pack_bin != NULL)
        {
            pack_str = a->pack_bin;
            have_pack = arcmc_check_bin_available (a->pack_bin);
        }
        else
        {
            pack_str = "-";
            have_pack = FALSE;
        }

        if (a->unpack_bin != NULL)
        {
            unpack_str = a->unpack_bin;
            have_unpack = arcmc_check_bin_available (a->unpack_bin);
        }
        else
        {
            unpack_str = "-";
            have_unpack = FALSE;
        }

        (void) have_pack;
        (void) have_unpack;

        table_add_row (tbl_ext, a->name, a->ext, pack_str, unpack_str, "");
        table_set_checked (tbl_ext, (int) i, 4,
                           arcmc_ext_enabled != NULL ? arcmc_ext_enabled[i] : TRUE);
    }
    group_add_widget (g, tbl_ext);
    y += ext_lines;

    group_add_widget (g, hline_new (y++, -1, -1));

    group_add_widget (
        g,
        button_new (dlg_height - 2, (dlg_width - 8) / 2, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));

    if (dlg_run (dlg) == B_ENTER)
    {
        /* read back check states */
        for (i = 0; i < G_N_ELEMENTS (builtin_formats); i++)
            arcmc_builtin_enabled[i] = table_get_checked (tbl_builtin, (int) i, 4);

        for (i = 0; i < ext_archivers_count; i++)
            if (arcmc_ext_enabled != NULL)
                arcmc_ext_enabled[i] = table_get_checked (tbl_ext, (int) i, 4);

        arcmc_config_save ();
    }

    settings_tbl_ext = NULL;
    widget_destroy (WIDGET (dlg));
}

/* --------------------------------------------------------------------------------------------- */
