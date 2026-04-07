/*
   Kubernetes panel plugin -- UI helpers and dialogs.

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

#include "src/viewer/mcviewer.h"
#include "src/panel-plugins/k8s/k8s-internal.h"

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
k8s_ui_view_command (const char *cmd)
{
    if (cmd == NULL || *cmd == '\0')
        return;

    (void) mcview_viewer (cmd, NULL, 0, 0, 0);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
k8s_ui_show_ns_switch_dialog (k8s_data_t *data)
{
    char *ns = NULL;
    gboolean ok;

    /* clang-format off */
    quick_widget_t quick_widgets[] = {
        QUICK_LABELED_INPUT (N_("Namespace:"), input_label_above,
                             data->namespace != NULL ? data->namespace : "default",
                             "k8s-ns-switch", &ns, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_BUTTONS_OK_CANCEL,
        QUICK_END,
    };
    /* clang-format on */

    WRect r = { -1, -1, 0, 48 };

    quick_dialog_t qdlg = {
        .rect = r,
        .title = N_ ("Switch Namespace"),
        .help = "[Kubernetes Plugin]",
        .widgets = quick_widgets,
        .callback = NULL,
        .mouse_callback = NULL,
    };

    ok = (quick_dialog (&qdlg) == B_ENTER);
    if (ok && ns != NULL && ns[0] != '\0')
    {
        g_free (data->namespace);
        data->namespace = ns;
        ns = NULL;
        k8s_clear_items (data);
    }
    g_free (ns);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
k8s_ui_show_add_context_dialog (char **ctx_out)
{
    char *ctx = NULL;
    gboolean ok;

    /* clang-format off */
    quick_widget_t quick_widgets[] = {
        QUICK_LABELED_INPUT (N_("Context name:"), input_label_above,
                             "", "k8s-add-context", &ctx, NULL, FALSE, FALSE,
                             INPUT_COMPLETE_NONE),
        QUICK_BUTTONS_OK_CANCEL,
        QUICK_END,
    };
    /* clang-format on */

    WRect r = { -1, -1, 0, 56 };

    quick_dialog_t qdlg = {
        .rect = r,
        .title = N_ ("Add Context to Favorites"),
        .help = "[Kubernetes Plugin]",
        .widgets = quick_widgets,
        .callback = NULL,
        .mouse_callback = NULL,
    };

    ok = (quick_dialog (&qdlg) == B_ENTER);
    if (ok && ctx != NULL && ctx[0] != '\0')
        *ctx_out = ctx;
    else
    {
        g_free (ctx);
        ok = FALSE;
    }
    return ok;
}
