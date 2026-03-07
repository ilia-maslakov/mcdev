/*
   Archive browser panel plugin -pack dialog UI.

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
#include "lib/widget.h"

#include "arcmc-types.h"
#include "arcmc-config.h"
#include "archive-io.h"
#include "dialog-pack.h"

/*** file scope variables ************************************************************************/

/* names for "Other" format selector (indices match ARCMC_FMT_TAR_GZ .. ARCMC_FMT_CPIO) */
static const char *const other_format_names[] = {
    "tar.gz", "tar.bz2", "tar.xz", "tar", "cpio",
};

#define OTHER_FMT_DISPLAY_LEN 16

/* extensions for each ARCMC_FMT_* value */
const char *const format_extensions[ARCMC_FMT_COUNT] = {
    ".zip",     /* ARCMC_FMT_ZIP */
    ".7z",      /* ARCMC_FMT_7Z */
    ".tar.gz",  /* ARCMC_FMT_TAR_GZ */
    ".tar.bz2", /* ARCMC_FMT_TAR_BZ2 */
    ".tar.xz",  /* ARCMC_FMT_TAR_XZ */
    ".tar",     /* ARCMC_FMT_TAR */
    ".cpio",    /* ARCMC_FMT_CPIO */
};

/* currently selected "Other" format index (0-based within other_format_names) */
static int current_other_fmt_idx = 0;

/* currently selected external archiver index (into ext_archivers[]) */
static int current_ext_fmt_idx = -1;

/* TRUE when current "Other" selection is an external archiver, FALSE when builtin */
static gboolean current_other_is_ext = FALSE;

/* widget IDs for the pack dialog callback */
static unsigned long pack_fmt_radio_id = 0;
static unsigned long pack_path_input_id = 0;
static unsigned long pack_show_password_id = 0;
static unsigned long pack_password_input_id = 0;
static unsigned long pack_verify_input_id = 0;

/*** file scope functions ************************************************************************/

/* Strip any known extension (builtin or external) from `base` in-place. */
static void
pack_strip_known_ext (char *base, size_t base_len)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS (format_extensions); i++)
    {
        size_t ext_len = strlen (format_extensions[i]);

        if (base_len >= ext_len
            && g_ascii_strcasecmp (base + base_len - ext_len, format_extensions[i]) == 0)
        {
            base[base_len - ext_len] = '\0';
            return;
        }
    }

    for (i = 0; i < ext_archivers_count; i++)
    {
        size_t ext_len = strlen (ext_archivers[i].ext);

        if (base_len >= ext_len
            && g_ascii_strcasecmp (base + base_len - ext_len, ext_archivers[i].ext) == 0)
        {
            base[base_len - ext_len] = '\0';
            return;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Replace the extension in the archive path input widget with `new_ext`. */
static void
pack_update_extension_str (Widget *dlg_w, const char *new_ext)
{
    Widget *path_w;
    WInput *path_input;
    const char *old_text;
    char *base;
    char *new_text;

    path_w = widget_find_by_id (dlg_w, pack_path_input_id);
    if (path_w == NULL)
        return;

    path_input = INPUT (path_w);
    old_text = path_input->buffer->str;
    base = g_strdup (old_text);
    pack_strip_known_ext (base, strlen (base));

    if (base[0] == '\0')
    {
        const char *dot;

        g_free (base);
        dot = strrchr (old_text, '.');
        if (dot != NULL)
            base = g_strndup (old_text, (gsize) (dot - old_text));
        else
            base = g_strdup (old_text);
    }

    new_text = g_strconcat (base, new_ext, NULL);
    input_assign_text (path_input, new_text);
    g_free (new_text);
    g_free (base);
}

/* --------------------------------------------------------------------------------------------- */

/* Replace the extension in the archive path input widget.
   `fmt` is an ARCMC_FMT_* value. */
static void
pack_update_extension (Widget *dlg_w, int fmt)
{
    Widget *path_w;
    WInput *path_input;
    const char *old_text;
    const char *ext;
    const char *dot;
    char *base;
    char *new_text;

    path_w = widget_find_by_id (dlg_w, pack_path_input_id);
    if (path_w == NULL)
        return;

    path_input = INPUT (path_w);
    old_text = path_input->buffer->str;
    ext = format_extensions[fmt];

    /* strip known extensions from the end */
    {
        size_t i;
        size_t old_len = strlen (old_text);

        base = g_strdup (old_text);

        for (i = 0; i < G_N_ELEMENTS (format_extensions); i++)
        {
            size_t ext_len = strlen (format_extensions[i]);

            if (old_len >= ext_len
                && g_ascii_strcasecmp (base + old_len - ext_len, format_extensions[i]) == 0)
            {
                base[old_len - ext_len] = '\0';
                break;
            }
        }
    }

    /* if no base name yet, use a dot-less placeholder */
    if (base[0] == '\0')
    {
        g_free (base);
        /* check if old text had at least a dot */
        dot = strrchr (old_text, '.');
        if (dot != NULL)
            base = g_strndup (old_text, (gsize) (dot - old_text));
        else
            base = g_strdup (old_text);
    }

    new_text = g_strconcat (base, ext, NULL);
    input_assign_text (path_input, new_text);
    g_free (new_text);
    g_free (base);
}

/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
pack_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    switch (msg)
    {
    case MSG_NOTIFY:
        /* format radio changed */
        if (sender != NULL && sender->id == pack_fmt_radio_id)
        {
            int sel = RADIO (sender)->sel;

            switch (sel)
            {
            case 0:
                pack_update_extension (w, ARCMC_FMT_ZIP);
                break;
            case 1:
                pack_update_extension (w, ARCMC_FMT_7Z);
                break;
            case 2:
            default:
                if (current_other_is_ext && current_ext_fmt_idx >= 0
                    && current_ext_fmt_idx < (int) ext_archivers_count)
                    pack_update_extension_str (w, ext_archivers[current_ext_fmt_idx].ext);
                else
                    pack_update_extension (w, ARCMC_FMT_TAR_GZ + current_other_fmt_idx);
                break;
            }
            return MSG_HANDLED;
        }

        /* show password checkbox toggled */
        if (sender != NULL && sender->id == pack_show_password_id)
        {
            gboolean shown = (CHECK (sender)->state != 0);
            Widget *pass_w, *verify_w;

            pass_w = widget_find_by_id (w, pack_password_input_id);
            if (pass_w != NULL)
            {
                INPUT (pass_w)->is_password = !shown;
                widget_draw (pass_w);
            }

            verify_w = widget_find_by_id (w, pack_verify_input_id);
            if (verify_w != NULL)
            {
                widget_disable (verify_w, shown);
            }
            return MSG_HANDLED;
        }
        return MSG_NOT_HANDLED;

    default:
        return dlg_default_callback (w, sender, msg, parm, data);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* Entry in the combined "Other" popup: either a builtin format or an external archiver. */
typedef struct
{
    gboolean is_ext; /* TRUE = external archiver, FALSE = builtin */
    int idx;         /* index into other_format_names[] or ext_archivers[] */
} other_entry_t;

static int
sel_other_format_button (WButton *button, int action)
{
    int result;
    WListbox *fmt_list;
    WDialog *fmt_dlg;
    int count = 0;
    other_entry_t *entries;
    int sel_idx = 0;
    int i, j;

    (void) action;

    /* count enabled builtin "Other" formats */
    for (i = 0; i < ARCMC_FMT_OTHER_COUNT; i++)
        if (arcmc_builtin_enabled[2 + i])
            count++;

    /* count enabled external archivers with pack support */
    for (i = 0; i < (int) ext_archivers_count; i++)
        if (ext_archivers[i].pack_bin != NULL
            && (arcmc_ext_enabled == NULL || arcmc_ext_enabled[i]))
            count++;

    if (count == 0)
    {
        message (D_NORMAL, _ ("Other formats"), _ ("No other formats are enabled"));
        return 0;
    }

    entries = g_new (other_entry_t, count);

    {
        Widget *btn_w = WIDGET (button);
        int dlg_y = btn_w->rect.y;
        int dlg_x = btn_w->rect.x + btn_w->rect.cols + 1;
        int dlg_w = OTHER_FMT_DISPLAY_LEN + 4;

        fmt_dlg = dlg_create (TRUE, dlg_y, dlg_x, count + 2, dlg_w, WPOS_KEEP_DEFAULT, TRUE,
                              dialog_colors, NULL, NULL, "[arcmc]", _ ("Format"));
    }

    fmt_list = listbox_new (1, 1, count, OTHER_FMT_DISPLAY_LEN + 2, FALSE, NULL);

    j = 0;

    /* builtin "Other" formats first */
    for (i = 0; i < ARCMC_FMT_OTHER_COUNT; i++)
    {
        if (!arcmc_builtin_enabled[2 + i])
            continue;

        listbox_add_item (fmt_list, LISTBOX_APPEND_AT_END, 0, other_format_names[i], NULL, FALSE);
        entries[j].is_ext = FALSE;
        entries[j].idx = i;
        if (!current_other_is_ext && i == current_other_fmt_idx)
            sel_idx = j;
        j++;
    }

    /* external archivers */
    for (i = 0; i < (int) ext_archivers_count; i++)
    {
        char label[32];

        if (ext_archivers[i].pack_bin == NULL)
            continue;
        if (arcmc_ext_enabled != NULL && !arcmc_ext_enabled[i])
            continue;

        g_snprintf (label, sizeof (label), "+ %s (%s)", ext_archivers[i].name,
                    ext_archivers[i].ext);
        listbox_add_item (fmt_list, LISTBOX_APPEND_AT_END, 0, label, NULL, FALSE);
        entries[j].is_ext = TRUE;
        entries[j].idx = i;
        if (current_other_is_ext && i == current_ext_fmt_idx)
            sel_idx = j;
        j++;
    }

    listbox_set_current (fmt_list, sel_idx);
    group_add_widget_autopos (GROUP (fmt_dlg), fmt_list, WPOS_KEEP_ALL, NULL);

    result = dlg_run (fmt_dlg);
    if (result == B_ENTER)
    {
        Widget *pack_dlg_w;
        Widget *radio_w;
        int selected;
        const other_entry_t *e;

        selected = LISTBOX (fmt_list)->current;
        e = &entries[selected];
        current_other_is_ext = e->is_ext;

        if (e->is_ext)
        {
            char btn_text[32];

            current_ext_fmt_idx = e->idx;
            g_snprintf (btn_text, sizeof (btn_text), "%s (%s)", ext_archivers[e->idx].name,
                        ext_archivers[e->idx].ext);
            button_set_text (button, str_fit_to_term (btn_text, OTHER_FMT_DISPLAY_LEN, J_LEFT_FIT));
        }
        else
        {
            current_other_fmt_idx = e->idx;
            button_set_text (
                button,
                str_fit_to_term (other_format_names[e->idx], OTHER_FMT_DISPLAY_LEN, J_LEFT_FIT));
        }

        /* switch radio to "Other" and update extension */
        pack_dlg_w = WIDGET (WIDGET (button)->owner);
        radio_w = widget_find_by_id (pack_dlg_w, pack_fmt_radio_id);
        if (radio_w != NULL)
        {
            RADIO (radio_w)->sel = 2;
            widget_draw (radio_w);
        }

        if (e->is_ext)
            pack_update_extension_str (pack_dlg_w, ext_archivers[e->idx].ext);
        else
            pack_update_extension (pack_dlg_w, ARCMC_FMT_TAR_GZ + e->idx);
    }
    widget_destroy (WIDGET (fmt_dlg));
    g_free (entries);

    return 0;
}

/*** public functions ****************************************************************************/

gboolean
arcmc_show_pack_dialog (arcmc_pack_opts_t *opts, const char *initial_path)
{
    /* *INDENT-OFF* */
    const char *format_options[] = {
        N_ ("&zip"),
        N_ ("&7z"),
        N_ ("O&ther:"),
    };
    const char *compression_options[] = {
        N_ ("St&ore"),
        N_ ("&Fastest"),
        N_ ("&Normal"),
        N_ ("&Maximum"),
    };
    /* *INDENT-ON* */

    const int format_options_num = G_N_ELEMENTS (format_options);
    const int compression_options_num = G_N_ELEMENTS (compression_options);

    char *archive_path = NULL;
    char *password = NULL;
    char *password_verify = NULL;
    static int format_radio = 0; /* 0=zip, 1=7z, 2=other */
    static int compression = 2;  /* Normal */
    static int encrypt_files = 0;
    static int encrypt_header = 0;
    int show_password = 0;
    static int store_paths = 1;
    static int delete_after = 0;
    int ret;

    /* *INDENT-OFF* */
    quick_widget_t quick_widgets[] = {
        QUICK_LABELED_INPUT (N_ ("Archive path:"), input_label_left, initial_path,
                             "arcmc-pack-path", &archive_path, &pack_path_input_id, FALSE, FALSE,
                             INPUT_COMPLETE_FILENAMES),
        QUICK_START_COLUMNS,
        QUICK_START_GROUPBOX (N_ ("Format")),
        QUICK_RADIO (format_options_num, format_options, &format_radio, &pack_fmt_radio_id),
        QUICK_BUTTON (
            str_fit_to_term (
                current_other_is_ext
                    ? (current_ext_fmt_idx >= 0 && current_ext_fmt_idx < (int) ext_archivers_count
                           ? ext_archivers[current_ext_fmt_idx].name
                           : "---")
                    : other_format_names[current_other_fmt_idx],
                OTHER_FMT_DISPLAY_LEN, J_LEFT_FIT),
            B_USER, sel_other_format_button, NULL),
        QUICK_STOP_GROUPBOX,
        QUICK_NEXT_COLUMN,
        QUICK_START_GROUPBOX (N_ ("Compression")),
        QUICK_RADIO (compression_options_num, compression_options, &compression, NULL),
        QUICK_STOP_GROUPBOX,
        QUICK_STOP_COLUMNS,
        QUICK_START_GROUPBOX (N_ ("Encryption")),
        QUICK_START_COLUMNS,
        QUICK_CHECKBOX (N_ ("Encrypt &files"), &encrypt_files, NULL),
        QUICK_NEXT_COLUMN,
        QUICK_CHECKBOX (N_ ("Encrypt hea&ders"), &encrypt_header, NULL),
        QUICK_STOP_COLUMNS,
        QUICK_CHECKBOX (N_ ("Show pass&word"), &show_password, &pack_show_password_id),
        QUICK_START_COLUMNS,
        QUICK_LABELED_INPUT (N_ ("Password:"), input_label_above, "", "arcmc-pack-pass", &password,
                             &pack_password_input_id, TRUE, TRUE, INPUT_COMPLETE_NONE),
        QUICK_NEXT_COLUMN,
        QUICK_LABELED_INPUT (N_ ("Verify password:"), input_label_above, "",
                             "arcmc-pack-pass-verify", &password_verify, &pack_verify_input_id,
                             TRUE, TRUE, INPUT_COMPLETE_NONE),
        QUICK_STOP_COLUMNS,
        QUICK_STOP_GROUPBOX,
        QUICK_START_GROUPBOX (N_ ("Options")),
        QUICK_CHECKBOX (N_ ("Store relative &paths"), &store_paths, NULL),
        QUICK_CHECKBOX (N_ ("&Delete files after archiving"), &delete_after, NULL),
        QUICK_STOP_GROUPBOX,
        QUICK_BUTTONS_OK_CANCEL,
        QUICK_END,
    };
    /* *INDENT-ON* */

    WRect r = { -1, -1, 0, 60 };

    quick_dialog_t qdlg = {
        .rect = r,
        .title = N_ ("Create archive"),
        .help = "[arcmc]",
        .widgets = quick_widgets,
        .callback = pack_dlg_callback,
        .mouse_callback = NULL,
    };

    ret = quick_dialog (&qdlg);

    if (ret == B_ENTER)
    {
        /* verify passwords match */
        if (password != NULL && password[0] != '\0')
        {
            if (password_verify == NULL || strcmp (password, password_verify) != 0)
            {
                message (D_ERROR, MSG_ERROR, "%s", _ ("Passwords do not match"));
                g_free (archive_path);
                g_free (password);
                g_free (password_verify);
                return FALSE;
            }
        }

        opts->archive_path = archive_path;
        opts->compression = compression;
        opts->encrypt_files = (encrypt_files != 0);
        opts->encrypt_header = (encrypt_header != 0);
        opts->store_paths = (store_paths != 0);
        opts->delete_after = (delete_after != 0);

        /* map radio selection to ARCMC_FMT_* */
        switch (format_radio)
        {
        case 0:
            opts->format = ARCMC_FMT_ZIP;
            break;
        case 1:
            opts->format = ARCMC_FMT_7Z;
            break;
        case 2:
        default:
            if (current_other_is_ext)
                opts->format = ARCMC_FMT_EXT_BASE + current_ext_fmt_idx;
            else
                opts->format = ARCMC_FMT_TAR_GZ + current_other_fmt_idx;
            break;
        }

        if (password != NULL && password[0] != '\0')
            opts->password = password;
        else
        {
            opts->password = NULL;
            g_free (password);
        }

        g_free (password_verify);
        return TRUE;
    }

    g_free (archive_path);
    g_free (password);
    g_free (password_verify);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
