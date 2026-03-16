/*
   S3 panel plugin -- shared type definitions.

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

#ifndef S3_TYPES_H
#define S3_TYPES_H

#include <curl/curl.h>

#include "lib/global.h"
#include "lib/panel-plugin.h"
#include "lib/widget.h"

/*** typedefs (not langstruc strstruc) ***********************************************************/

typedef struct
{
    char *label;             /* display name */
    char *access_key;        /* AWS access key ID */
    char *secret_key;        /* stored XOR+base64 encoded */
    char *region;            /* default "us-east-1" */
    char *endpoint;          /* custom URL for MinIO/Ceph, NULL = AWS */
    gboolean use_path_style; /* path-style URLs */
    int timeout;             /* response timeout, sec (0 = 30s default) */
    int connect_timeout;     /* connect timeout, sec (0 = 10s default) */
    char *buckets;           /* comma-separated bucket list (skip ListBuckets) */
} s3_connection_t;

typedef struct
{
    simple_status_msg_t status_msg; /* base class */
    GString *log;
    Widget *hline_w;
    Widget *button_w;
} s3_connect_status_msg_t;

typedef struct
{
    status_msg_t *sm;
} s3_connect_progress_t;

/* XML parser context for S3 error responses */
typedef struct
{
    gboolean in_code;
    gboolean in_message;
    GString *text;
    char *code;
    char *message;
} s3_xml_error_t;

/* curl write callback context */
typedef struct
{
    GString *buf;
} s3_buf_ctx_t;

/*** connection management (s3_connection.c) *****************************************************/

void s3_connection_free (gpointer p);
s3_connection_t *s3_connection_dup (const s3_connection_t *src);
void s3_connection_copy_from (s3_connection_t *dst, const s3_connection_t *src);
gboolean s3_show_connection_dialog (s3_connection_t *conn, const char *help_file);

/*** connect status dialog (s3_connect_status.c) *************************************************/

void s3_connect_status_init_cb (status_msg_t *sm);
int s3_connect_status_update_cb (status_msg_t *sm);
void s3_connect_status_deinit_cb (status_msg_t *sm);
gboolean s3_connect_status_set_stage (s3_connect_status_msg_t *fsm, const char *fmt, ...)
    G_GNUC_PRINTF (2, 3);
void s3_connect_status_wait_close (s3_connect_status_msg_t *fsm);

#if LIBCURL_VERSION_NUM >= 0x072000
int s3_connect_progress_cb (void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                            curl_off_t ulnow);
#else
int s3_connect_progress_cb (void *clientp, double dltotal, double dlnow, double ultotal,
                            double ulnow);
#endif

#endif /* S3_TYPES_H */
