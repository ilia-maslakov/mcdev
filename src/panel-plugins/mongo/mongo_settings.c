/*
   MongoDB panel plugin -- general-options settings dialog.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

   This file is part of the Midnight Commander.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
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
#include "lib/widget.h"

#include "mongo_settings.h"

/*** file scope functions ************************************************************************/

/* Parse @text as a non-negative int. On success writes *out and returns TRUE.
   Empty string is rejected. */
static gboolean
parse_int_field (const char *text, int *out)
{
    char *endp = NULL;
    long v;

    if (text == NULL || text[0] == '\0')
        return FALSE;
    v = strtol (text, &endp, 10);
    if (endp == text || (endp != NULL && *endp != '\0') || v < 0 || v > G_MAXINT)
        return FALSE;
    *out = (int) v;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/* A connection name becomes the [Cluster.<name>] section, so it must not break
   the ini group syntax or carry leading/trailing whitespace. */
static gboolean
valid_cluster_name (const char *name)
{
    size_t len;

    if (name == NULL || name[0] == '\0')
        return FALSE;
    if (strpbrk (name, "[]\r\n") != NULL)
        return FALSE;
    if (g_ascii_isspace (name[0]))
        return FALSE;
    len = strlen (name);
    if (g_ascii_isspace (name[len - 1]))
        return FALSE;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/

gboolean
mongo_connection_dialog_run (mongo_cluster_t *c, gboolean is_new)
{
    char *r_name = NULL, *r_uri = NULL, *r_desc = NULL;
    gboolean read_only;
    int rc;

    if (c == NULL)
        return FALSE;

    read_only = c->read_only;

    {
        quick_widget_t quick_widgets[] = {
            // clang-format off
            QUICK_LABELED_INPUT (N_ ("Connection name:"), input_label_above,
                                 c->name != NULL ? c->name : "", "mongo-conn-name",
                                 &r_name, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("URI (mongodb:// or mongodb+srv://):"), input_label_above,
                                 c->uri != NULL ? c->uri : "", "mongo-conn-uri",
                                 &r_uri, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Description (optional):"), input_label_above,
                                 c->description != NULL ? c->description : "", "mongo-conn-desc",
                                 &r_desc, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_CHECKBOX (N_ ("&Read-only (block insert / edit / delete)"), &read_only, NULL),
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
            // clang-format on
        };
        WRect r = { -1, -1, 0, 64 };
        quick_dialog_t qdlg = {
            .rect = r,
            .title = is_new ? N_ ("New MongoDB connection") : N_ ("Edit MongoDB connection"),
            .help = "[MongoDB Plugin]",
            .help_file = MC_PLUGIN_DIR "/mongo_panel.hlp",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };
        rc = quick_dialog (&qdlg);
    }

    if (rc != B_ENTER)
    {
        g_free (r_name);
        g_free (r_uri);
        g_free (r_desc);
        return FALSE;
    }

    if (r_name != NULL)
        r_name = g_strstrip (r_name);
    if (r_uri != NULL)
        r_uri = g_strstrip (r_uri);
    if (r_desc != NULL)
        r_desc = g_strstrip (r_desc);

    if (!valid_cluster_name (r_name))
    {
        message (D_ERROR, _ ("MongoDB connection"), "%s",
                 _ ("Connection name must be non-empty and contain no '[', ']' characters."));
        g_free (r_name);
        g_free (r_uri);
        g_free (r_desc);
        return FALSE;
    }
    if (r_uri == NULL || r_uri[0] == '\0')
    {
        message (D_ERROR, _ ("MongoDB connection"), "%s", _ ("URI must not be empty."));
        g_free (r_name);
        g_free (r_uri);
        g_free (r_desc);
        return FALSE;
    }

    g_free (c->name);
    c->name = g_strdup (r_name);
    g_free (c->uri);
    c->uri = g_strdup (r_uri);
    g_free (c->description);
    c->description = (r_desc != NULL && r_desc[0] != '\0') ? g_strdup (r_desc) : NULL;
    c->read_only = read_only;

    g_free (r_name);
    g_free (r_uri);
    g_free (r_desc);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_settings_dialog_run (mongo_config_t *cfg)
{
    static const char *strategy_items[2] = { N_ ("&auto"), N_ ("&none") };

    char *r_default = NULL;
    char *r_leaf = NULL, *r_fanout = NULL;
    char *r_srvsel = NULL, *r_connect = NULL, *r_socket = NULL, *r_opmax = NULL;
    int r_strategy;
    char buf_leaf[16], buf_fanout[16];
    char buf_srvsel[16], buf_connect[16], buf_socket[16], buf_opmax[16];
    int rc;

    if (cfg == NULL)
        return FALSE;

    r_strategy = cfg->bucket_strategy == MONGO_BUCKET_NONE ? 1 : 0;
    g_snprintf (buf_leaf, sizeof (buf_leaf), "%d", cfg->bucket_leaf_size);
    g_snprintf (buf_fanout, sizeof (buf_fanout), "%d", cfg->bucket_fanout);
    g_snprintf (buf_srvsel, sizeof (buf_srvsel), "%d", cfg->server_selection_timeout_ms);
    g_snprintf (buf_connect, sizeof (buf_connect), "%d", cfg->connect_timeout_ms);
    g_snprintf (buf_socket, sizeof (buf_socket), "%d", cfg->socket_timeout_ms);
    g_snprintf (buf_opmax, sizeof (buf_opmax), "%d", cfg->op_max_time_ms);

    {
        quick_widget_t quick_widgets[] = {
            // clang-format off
            QUICK_LABELED_INPUT (N_ ("Default cluster:"), input_label_left,
                                 cfg->default_cluster != NULL ? cfg->default_cluster : "",
                                 "mongo-default-cluster", &r_default, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_SEPARATOR (TRUE),
            QUICK_LABEL (N_ ("Bucket strategy:"), NULL),
            QUICK_RADIO (2, strategy_items, &r_strategy, NULL),
            QUICK_LABELED_INPUT (N_ ("Leaf size (flat docs):"), input_label_left, buf_leaf,
                                 "mongo-leaf", &r_leaf, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Fanout:"), input_label_left, buf_fanout,
                                 "mongo-fanout", &r_fanout, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_SEPARATOR (TRUE),
            QUICK_LABELED_INPUT (N_ ("Server selection (ms):"), input_label_left, buf_srvsel,
                                 "mongo-srvsel", &r_srvsel, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Connect (ms):"), input_label_left, buf_connect,
                                 "mongo-connect", &r_connect, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Socket (ms):"), input_label_left, buf_socket,
                                 "mongo-socket", &r_socket, NULL, FALSE, FALSE,
                                 INPUT_COMPLETE_NONE),
            QUICK_LABELED_INPUT (N_ ("Op max time (ms):"), input_label_left, buf_opmax,
                                 "mongo-opmax", &r_opmax, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
            // clang-format on
        };
        WRect r = { -1, -1, 0, 60 };
        quick_dialog_t qdlg = {
            .rect = r,
            .title = N_ ("MongoDB settings"),
            .help = "[MongoDB Plugin]",
            .help_file = MC_PLUGIN_DIR "/mongo_panel.hlp",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };
        rc = quick_dialog (&qdlg);
    }

    if (rc != B_ENTER)
    {
        g_free (r_default);
        g_free (r_leaf);
        g_free (r_fanout);
        g_free (r_srvsel);
        g_free (r_connect);
        g_free (r_socket);
        g_free (r_opmax);
        return FALSE;
    }

    {
        int leaf, fanout, srvsel, connect, socket_to, opmax;
        gboolean ok = parse_int_field (r_leaf, &leaf) && parse_int_field (r_fanout, &fanout)
            && parse_int_field (r_srvsel, &srvsel) && parse_int_field (r_connect, &connect)
            && parse_int_field (r_socket, &socket_to) && parse_int_field (r_opmax, &opmax);

        if (ok && (leaf <= 0 || fanout < 2))
            ok = FALSE;

        if (!ok)
        {
            message (D_ERROR, _ ("MongoDB settings"), "%s",
                     _ ("Numeric fields must be valid; leaf size > 0, fanout >= 2."));
            g_free (r_default);
            g_free (r_leaf);
            g_free (r_fanout);
            g_free (r_srvsel);
            g_free (r_connect);
            g_free (r_socket);
            g_free (r_opmax);
            return FALSE;
        }

        g_free (cfg->default_cluster);
        cfg->default_cluster =
            (r_default != NULL && r_default[0] != '\0') ? g_strdup (r_default) : NULL;
        cfg->bucket_strategy = r_strategy == 1 ? MONGO_BUCKET_NONE : MONGO_BUCKET_AUTO;
        cfg->bucket_leaf_size = leaf;
        cfg->bucket_fanout = fanout;
        cfg->server_selection_timeout_ms = srvsel;
        cfg->connect_timeout_ms = connect;
        cfg->socket_timeout_ms = socket_to;
        cfg->op_max_time_ms = opmax;
    }

    g_free (r_default);
    g_free (r_leaf);
    g_free (r_fanout);
    g_free (r_srvsel);
    g_free (r_connect);
    g_free (r_socket);
    g_free (r_opmax);

    {
        char *err = NULL;
        if (!mongo_config_save_general (cfg, &err))
        {
            message (D_ERROR, _ ("MongoDB settings"), "%s",
                     err != NULL ? err : _ ("Failed to save settings."));
            g_free (err);
            return FALSE;
        }
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
