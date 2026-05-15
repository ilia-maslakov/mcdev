/*
   Ctags plugin configuration.

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

#include <stdlib.h>
#include <string.h>

#include "lib/global.h"
#include "lib/tty/tty.h"
#include "lib/mcconfig.h"
#include "lib/widget.h"

#include "src/editor-plugins/ctags/ctags-history.h"

#include "ctags-config.h"

/*** file scope macro definitions ****************************************************************/

#define CFG_GROUP          "ctags"
#define CFG_CTAGS_CMD      "ctags_cmd"
#define CFG_CTAGS_ARGS     "ctags_args"
#define CFG_CASE_SENS      "case_sensitive"
#define CFG_AUTO_DISC      "auto_discover"

#define DEFAULT_CTAGS_CMD  "ctags"
#define DEFAULT_CTAGS_ARGS "--sort=yes --fields=+ln"

/*** public functions ****************************************************************************/

/* --------------------------------------------------------------------------------------------- */

void
ctags_config_init (ctags_config_t *cfg)
{
    if (cfg == NULL)
        return;

    cfg->ctags_cmd = g_strdup (DEFAULT_CTAGS_CMD);
    cfg->ctags_args = g_strdup (DEFAULT_CTAGS_ARGS);
    cfg->case_sensitive = FALSE;
    cfg->auto_discover = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
ctags_config_free (ctags_config_t *cfg)
{
    if (cfg == NULL)
        return;

    g_free (cfg->ctags_cmd);
    g_free (cfg->ctags_args);
    cfg->ctags_cmd = NULL;
    cfg->ctags_args = NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
ctags_config_load (ctags_config_t *cfg)
{
    char *s;

    if (cfg == NULL)
        return;

    ctags_config_init (cfg);

    s = mc_config_get_string (mc_global.main_config, CFG_GROUP, CFG_CTAGS_CMD, NULL);
    if (s != NULL)
    {
        g_free (cfg->ctags_cmd);
        cfg->ctags_cmd = s;
    }

    s = mc_config_get_string (mc_global.main_config, CFG_GROUP, CFG_CTAGS_ARGS, NULL);
    if (s != NULL)
    {
        g_free (cfg->ctags_args);
        cfg->ctags_args = s;
    }

    cfg->case_sensitive =
        mc_config_get_bool (mc_global.main_config, CFG_GROUP, CFG_CASE_SENS, FALSE);
    cfg->auto_discover = mc_config_get_bool (mc_global.main_config, CFG_GROUP, CFG_AUTO_DISC, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

void
ctags_config_save (const ctags_config_t *cfg)
{
    if (cfg == NULL)
        return;

    mc_config_set_string (mc_global.main_config, CFG_GROUP, CFG_CTAGS_CMD,
                          cfg->ctags_cmd != NULL ? cfg->ctags_cmd : DEFAULT_CTAGS_CMD);
    mc_config_set_string (mc_global.main_config, CFG_GROUP, CFG_CTAGS_ARGS,
                          cfg->ctags_args != NULL ? cfg->ctags_args : DEFAULT_CTAGS_ARGS);
    mc_config_set_bool (mc_global.main_config, CFG_GROUP, CFG_CASE_SENS, cfg->case_sensitive);
    mc_config_set_bool (mc_global.main_config, CFG_GROUP, CFG_AUTO_DISC, cfg->auto_discover);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
ctags_config_dialog (ctags_config_t *cfg)
{
    WDialog *dlg;
    WInput *inp_cmd, *inp_args;
    WCheck *chk_case, *chk_auto;
    int dlg_h = 11;
    int dlg_w = MIN (COLS - 4, 72);
    int inp_w = dlg_w - 4;
    int x_ok, x_cancel;

    if (cfg == NULL)
        return FALSE;

    dlg = dlg_create (TRUE, (LINES - dlg_h) / 2, (COLS - dlg_w) / 2, dlg_h, dlg_w,
                      WPOS_KEEP_DEFAULT, TRUE, dialog_colors, dlg_default_callback, NULL, "[Ctags]",
                      _ ("Ctags Configuration"));

    group_add_widget (GROUP (dlg), label_new (1, 2, _ ("Ctags executable:")));
    inp_cmd = input_new (2, 2, input_colors, inp_w,
                         cfg->ctags_cmd != NULL ? cfg->ctags_cmd : DEFAULT_CTAGS_CMD,
                         MC_HISTORY_CTAGS_CMD, INPUT_COMPLETE_FILENAMES);
    group_add_widget (GROUP (dlg), inp_cmd);

    group_add_widget (GROUP (dlg), label_new (3, 2, _ ("Command line options:")));
    inp_args = input_new (4, 2, input_colors, inp_w,
                          cfg->ctags_args != NULL ? cfg->ctags_args : DEFAULT_CTAGS_ARGS, NULL,
                          INPUT_COMPLETE_NONE);
    group_add_widget (GROUP (dlg), inp_args);

    group_add_widget (GROUP (dlg), hline_new (5, -1, -1));

    chk_case = check_new (6, 2, cfg->case_sensitive, _ ("Case sensitive filter"));
    group_add_widget (GROUP (dlg), chk_case);

    chk_auto =
        check_new (7, 2, cfg->auto_discover, _ ("Auto-discover tags file in parent directories"));
    group_add_widget (GROUP (dlg), chk_auto);

    group_add_widget (GROUP (dlg), hline_new (8, -1, -1));

    x_ok = (dlg_w - 20) / 2;
    x_cancel = x_ok + 10;
    group_add_widget (GROUP (dlg), button_new (9, x_ok, B_ENTER, DEFPUSH_BUTTON, _ ("&OK"), NULL));
    group_add_widget (GROUP (dlg),
                      button_new (9, x_cancel, B_CANCEL, NORMAL_BUTTON, _ ("&Cancel"), NULL));

    if (dlg_run (dlg) != B_ENTER)
    {
        widget_destroy (WIDGET (dlg));
        return FALSE;
    }

    g_free (cfg->ctags_cmd);
    cfg->ctags_cmd = g_strdup (input_get_ctext (inp_cmd));

    g_free (cfg->ctags_args);
    cfg->ctags_args = g_strdup (input_get_ctext (inp_args));

    cfg->case_sensitive = CHECK (chk_case)->state;
    cfg->auto_discover = CHECK (chk_auto)->state;

    widget_destroy (WIDGET (dlg));

    ctags_config_save (cfg);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
