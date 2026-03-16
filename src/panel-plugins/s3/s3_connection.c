/*
   S3 panel plugin -- connection management and edit dialog.

   Copyright (C) 2026
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

#include "lib/global.h"
#include "lib/widget.h"

#include "s3_types.h"

/*** file scope macro definitions ****************************************************************/

#define S3_DLG_HEIGHT 20
#define S3_DLG_WIDTH  56

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
s3_connection_free (gpointer p)
{
    s3_connection_t *c = (s3_connection_t *) p;

    g_free (c->label);
    g_free (c->access_key);
    g_free (c->secret_key);
    g_free (c->region);
    g_free (c->endpoint);
    g_free (c->buckets);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

s3_connection_t *
s3_connection_dup (const s3_connection_t *src)
{
    s3_connection_t *dst;

    dst = g_new0 (s3_connection_t, 1);
    dst->label = g_strdup (src->label);
    dst->access_key = g_strdup (src->access_key);
    dst->secret_key = g_strdup (src->secret_key);
    dst->region = g_strdup (src->region);
    dst->endpoint = g_strdup (src->endpoint);
    dst->use_path_style = src->use_path_style;
    dst->timeout = src->timeout;
    dst->connect_timeout = src->connect_timeout;
    dst->buckets = g_strdup (src->buckets);

    return dst;
}

/* --------------------------------------------------------------------------------------------- */

void
s3_connection_copy_from (s3_connection_t *dst, const s3_connection_t *src)
{
    g_free (dst->label);
    g_free (dst->access_key);
    g_free (dst->secret_key);
    g_free (dst->region);
    g_free (dst->endpoint);
    g_free (dst->buckets);

    dst->label = g_strdup (src->label);
    dst->access_key = g_strdup (src->access_key);
    dst->secret_key = g_strdup (src->secret_key);
    dst->region = g_strdup (src->region);
    dst->endpoint = g_strdup (src->endpoint);
    dst->use_path_style = src->use_path_style;
    dst->timeout = src->timeout;
    dst->connect_timeout = src->connect_timeout;
    dst->buckets = g_strdup (src->buckets);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
s3_show_connection_dialog (s3_connection_t *conn, const char *help_file)
{
    s3_connection_t *backup;
    char *label = g_strdup (conn->label != NULL ? conn->label : "");
    char *access_key = g_strdup (conn->access_key != NULL ? conn->access_key : "");
    char *secret_key = g_strdup (conn->secret_key != NULL ? conn->secret_key : "");
    char *region = g_strdup (conn->region != NULL ? conn->region : "us-east-1");
    char *endpoint = g_strdup (conn->endpoint != NULL ? conn->endpoint : "");
    char *buckets = g_strdup (conn->buckets != NULL ? conn->buckets : "");
    gboolean use_path_style = conn->use_path_style;
    int ret;

    backup = s3_connection_dup (conn);

    /* clang-format off */
    quick_widget_t quick_widgets[] = {
        QUICK_LABELED_INPUT (N_("Connection name:"), input_label_above,
                            label, "s3-conn-label",
                            &label, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_LABELED_INPUT (N_("Access Key:"), input_label_above,
                            access_key, "s3-conn-access-key",
                            &access_key, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_LABELED_INPUT (N_("Secret Key:"), input_label_above,
                            secret_key, "s3-conn-secret-key",
                            &secret_key, NULL, TRUE, TRUE, INPUT_COMPLETE_NONE),
        QUICK_LABELED_INPUT (N_("Region:"), input_label_above,
                            region, "s3-conn-region",
                            &region, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_LABELED_INPUT (N_("Endpoint URL (for MinIO/Ceph):"), input_label_above,
                            endpoint, "s3-conn-endpoint",
                            &endpoint, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_LABELED_INPUT (N_("Buckets (comma-separated, if auto-detect fails):"), input_label_above,
                            buckets, "s3-conn-buckets",
                            &buckets, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_CHECKBOX (N_("Use &path-style URLs"), &use_path_style, NULL),
        QUICK_BUTTONS_OK_CANCEL,
        QUICK_END,
    };
    /* clang-format on */

    WRect r = { -1, -1, S3_DLG_HEIGHT, S3_DLG_WIDTH };

    quick_dialog_t qdlg = {
        .rect = r,
        .title = N_ ("S3 Connection"),
        .help = "[S3 Connection]",
        .help_file = help_file,
        .widgets = quick_widgets,
        .callback = NULL,
        .mouse_callback = NULL,
    };

    ret = quick_dialog (&qdlg);

    if (ret == B_ENTER)
    {
        g_free (conn->label);
        conn->label = label;

        g_free (conn->access_key);
        conn->access_key = access_key;

        g_free (conn->secret_key);
        conn->secret_key = (secret_key != NULL && secret_key[0] != '\0') ? secret_key : NULL;
        if (conn->secret_key == NULL)
            g_free (secret_key);

        g_free (conn->region);
        conn->region = (region != NULL && region[0] != '\0') ? region : g_strdup ("us-east-1");
        if (conn->region != region)
            g_free (region);

        g_free (conn->endpoint);
        conn->endpoint = (endpoint != NULL && endpoint[0] != '\0') ? endpoint : NULL;
        if (conn->endpoint == NULL)
            g_free (endpoint);

        g_free (conn->buckets);
        conn->buckets = (buckets != NULL && buckets[0] != '\0') ? buckets : NULL;
        if (conn->buckets == NULL)
            g_free (buckets);

        conn->use_path_style = use_path_style;

        s3_connection_free (backup);
        return TRUE;
    }

    /* Cancel - rollback */
    g_free (label);
    g_free (access_key);
    g_free (secret_key);
    g_free (region);
    g_free (endpoint);
    g_free (buckets);

    s3_connection_copy_from (conn, backup);
    s3_connection_free (backup);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
