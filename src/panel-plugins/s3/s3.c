/*
   S3 object storage browser panel plugin (libcurl).

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

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#include "lib/global.h"
#include "lib/keybind.h"
#include "lib/util.h"
#include "lib/mcconfig.h"
#include "lib/panel-cache.h"
#include "lib/panel-plugin.h"
#include "lib/tty/key.h"
#include "lib/widget.h"

#include "src/viewer/mcviewer.h"

/*** file scope type declarations ****************************************************************/

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
} s3_connection_t;

typedef enum
{
    S3_LEVEL_CONNECTIONS = 0,
    S3_LEVEL_BUCKETS,
    S3_LEVEL_OBJECTS
} s3_level_t;

typedef struct
{
    mc_panel_host_t *host;
    s3_level_t level;
    char *current_bucket;
    char *current_prefix; /* e.g. "photos/2024/" or "" for root of bucket */
    GPtrArray *entries;   /* mc_pp_dir_entry_t* */
    GPtrArray *connections;
    char *connections_file;
    s3_connection_t *active_connection;
    mc_pp_dir_cache_t dir_cache;
    int key_edit;
    char *title_buf;
} s3_data_t;

/* XML parser context for list buckets */
typedef struct
{
    GPtrArray *entries; /* mc_pp_dir_entry_t* */
    gboolean in_bucket;
    gboolean in_name;
    gboolean in_creation_date;
    GString *text;
    char *bucket_name;
    time_t creation_date;
} s3_xml_list_buckets_t;

/* XML parser context for list objects (ListObjectsV2) */
typedef struct
{
    GPtrArray *entries; /* mc_pp_dir_entry_t* */
    gboolean in_contents;
    gboolean in_common_prefixes;
    gboolean in_key;
    gboolean in_size;
    gboolean in_last_modified;
    gboolean in_prefix;
    gboolean in_is_truncated;
    gboolean in_next_continuation_token;
    GString *text;
    char *key;
    off_t size;
    time_t mtime;
    gboolean is_truncated;
    char *next_continuation_token;
    const char *strip_prefix; /* prefix to strip from keys */
} s3_xml_list_objects_t;

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

/* curl file write callback context */
typedef struct
{
    int fd;
} s3_file_write_ctx_t;

/* curl file read callback context */
typedef struct
{
    int fd;
    off_t remaining;
} s3_file_read_ctx_t;

/* progress dialog context */
typedef struct
{
    WDialog *dlg;
    WLabel *lbl_file;
    WLabel *lbl_size;
    WGauge *gauge;
    gboolean visible;
    gboolean aborted;
    gint64 start_time;
    const char *direction; /* "Downloading" or "Uploading" */
    const char *fname;
    int gauge_cols;
    int last_col;
} s3_progress_t;

/*** forward declarations (file scope functions) *************************************************/

static void *s3_open (mc_panel_host_t *host, const char *open_path);
static void s3_close (void *plugin_data);
static mc_pp_result_t s3_get_items (void *plugin_data, void *list_ptr);
static mc_pp_result_t s3_chdir (void *plugin_data, const char *path);
static mc_pp_result_t s3_enter (void *plugin_data, const char *name, const struct stat *st);
static mc_pp_result_t s3_get_local_copy (void *plugin_data, const char *fname, char **local_path);
static mc_pp_result_t s3_put_file (void *plugin_data, const char *local_path,
                                   const char *dest_name);
static mc_pp_result_t s3_delete_items (void *plugin_data, const char **names, int count);
static const char *s3_get_title (void *plugin_data);
static mc_pp_result_t s3_create_item (void *plugin_data);
static mc_pp_result_t s3_view_item (void *plugin_data, const char *fname, const struct stat *st,
                                    gboolean plain_view);
static mc_pp_result_t s3_handle_key (void *plugin_data, int key);

/*** file scope variables ************************************************************************/

#define S3_PANEL_CONFIG_FILE      "panels.s3.ini"
#define S3_PANEL_CONFIG_GROUP     "s3-panel"
#define S3_PANEL_KEY_EDIT         "hotkey_edit"
#define S3_PANEL_KEY_EDIT_DEFAULT "f4"

/* KEY_F(n) = 1000 + n, XCTRL(c) = c & 0x1f - matching lib/tty definitions */
#define S3_KEY_F(n) (1000 + (n))
#define S3_XCTRL(c) ((c) & 0x1f)

#define S3_DIR_CACHE_TTL 60

/* --------------------------------------------------------------------------------------------- */
/* Logging                                                                                       */
/* --------------------------------------------------------------------------------------------- */

static gboolean
s3_logging_enabled (void)
{
    const char *val;

    val = g_getenv ("MC_LOG_ENABLE");
    if (val != NULL)
        return (*val == '1' || g_ascii_strcasecmp (val, "true") == 0);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void G_GNUC_PRINTF (1, 2)
s3_log (const char *fmt, ...)
{
    const char *log_file;
    FILE *f;
    va_list args;

    if (!s3_logging_enabled ())
        return;

    log_file = g_getenv ("MC_LOG_FILE");
    if (log_file == NULL || *log_file == '\0')
        log_file = "/tmp/mc.log";

    f = fopen (log_file, "a");
    if (f == NULL)
        return;

    (void) fprintf (f, "[s3] ");
    va_start (args, fmt);
    (void) vfprintf (f, fmt, args);
    va_end (args);
    (void) fputc ('\n', f);
    (void) fclose (f);
}

/* --------------------------------------------------------------------------------------------- */

#define S3_LOG(fmt, ...) s3_log (fmt, ##__VA_ARGS__)

static guint s3_curl_refcount = 0;

static const mc_panel_plugin_t s3_plugin = {
    .api_version = MC_PANEL_PLUGIN_API_VERSION,
    .name = "s3",
    .display_name = "S3 object storage",
    .proto = "s3",
    .prefix = NULL,
    .flags = MC_PPF_NAVIGATE | MC_PPF_GET_FILES | MC_PPF_DELETE | MC_PPF_CUSTOM_TITLE
        | MC_PPF_CREATE | MC_PPF_PUT_FILES | MC_PPF_SHOW_IN_MENU,

    .open = s3_open,
    .close = s3_close,
    .get_items = s3_get_items,

    .chdir = s3_chdir,
    .enter = s3_enter,
    .get_local_copy = s3_get_local_copy,
    .put_file = s3_put_file,
    .save_file = s3_put_file,
    .delete_items = s3_delete_items,
    .get_title = s3_get_title,
    .view = s3_view_item,
    .handle_key = s3_handle_key,
    .create_item = s3_create_item,
};

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/* Config / hotkey helpers                                                                        */
/* --------------------------------------------------------------------------------------------- */

static char *
s3_read_config_string (const char *path, const char *key)
{
    char *value;
    mc_config_t *cfg;

    if (path == NULL || !g_file_test (path, G_FILE_TEST_IS_REGULAR))
        return NULL;

    cfg = mc_config_init (path, TRUE);
    if (cfg == NULL)
        return NULL;

    value = mc_config_get_string (cfg, S3_PANEL_CONFIG_GROUP, key, NULL);
    mc_config_deinit (cfg);

    if (value == NULL)
        return NULL;

    if (value[0] == '\0')
    {
        g_free (value);
        return NULL;
    }
    return value;
}

/* --------------------------------------------------------------------------------------------- */

static int
s3_parse_hotkey (const char *str)
{
    int n;

    if (str == NULL || str[0] == '\0')
        return -1;

    if (g_ascii_strcasecmp (str, "none") == 0)
        return -1;

    /* "f1" .. "f24" */
    if ((str[0] == 'f' || str[0] == 'F') && str[1] >= '0' && str[1] <= '9')
    {
        n = atoi (str + 1);
        if (n >= 1 && n <= 24)
            return S3_KEY_F (n);
    }

    /* "ctrl-x" */
    if (g_ascii_strncasecmp (str, "ctrl-", 5) == 0 && str[5] != '\0')
        return S3_XCTRL (str[5]);

    /* "shift-f7" etc. */
    if (g_ascii_strncasecmp (str, "shift-", 6) == 0 && (str[6] == 'f' || str[6] == 'F'))
    {
        n = atoi (str + 7);
        if (n >= 1 && n <= 24)
            return S3_KEY_F (n + 12);
    }

    return -1;
}

/* --------------------------------------------------------------------------------------------- */

static int
s3_load_hotkey (const char *key, const char *default_str, int default_val)
{
    char *user_cfg;
    char *val;
    int code;

    user_cfg = g_build_filename (g_get_user_config_dir (), "mc", S3_PANEL_CONFIG_FILE, (char *) NULL);
    val = s3_read_config_string (user_cfg, key);
    g_free (user_cfg);

    if (val != NULL)
    {
        code = s3_parse_hotkey (val);
        g_free (val);
        return code;
    }

    (void) default_str;
    return default_val;
}

/* --------------------------------------------------------------------------------------------- */
/* Connection management                                                                         */
/* --------------------------------------------------------------------------------------------- */

static void
s3_connection_free (gpointer p)
{
    s3_connection_t *c = (s3_connection_t *) p;

    g_free (c->label);
    g_free (c->access_key);
    g_free (c->secret_key);
    g_free (c->region);
    g_free (c->endpoint);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

static s3_connection_t *
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

    return dst;
}

/* --------------------------------------------------------------------------------------------- */

static void
s3_connection_copy_from (s3_connection_t *dst, const s3_connection_t *src)
{
    g_free (dst->label);
    g_free (dst->access_key);
    g_free (dst->secret_key);
    g_free (dst->region);
    g_free (dst->endpoint);

    dst->label = g_strdup (src->label);
    dst->access_key = g_strdup (src->access_key);
    dst->secret_key = g_strdup (src->secret_key);
    dst->region = g_strdup (src->region);
    dst->endpoint = g_strdup (src->endpoint);
    dst->use_path_style = src->use_path_style;
    dst->timeout = src->timeout;
    dst->connect_timeout = src->connect_timeout;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/* Connection persistence                                                                        */
/* --------------------------------------------------------------------------------------------- */

static char *
s3_get_connections_file_path (void)
{
    return g_build_filename (g_get_user_config_dir (), "mc", "s3-connections.ini", (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

static GPtrArray *
s3_load_connections (const char *filepath)
{
    GPtrArray *arr;
    GKeyFile *kf;
    gchar **groups;
    gsize n_groups, i;

    arr = g_ptr_array_new_with_free_func (s3_connection_free);

    kf = g_key_file_new ();
    if (!g_key_file_load_from_file (kf, filepath, G_KEY_FILE_NONE, NULL))
    {
        g_key_file_free (kf);
        return arr;
    }

    groups = g_key_file_get_groups (kf, &n_groups);
    for (i = 0; i < n_groups; i++)
    {
        s3_connection_t *conn;
        GError *error = NULL;
        char *raw_secret;

        conn = g_new0 (s3_connection_t, 1);
        conn->label = g_strdup (groups[i]);
        conn->access_key = g_key_file_get_string (kf, groups[i], "access_key", NULL);
        conn->region = g_key_file_get_string (kf, groups[i], "region", NULL);
        conn->endpoint = g_key_file_get_string (kf, groups[i], "endpoint", NULL);

        raw_secret = g_key_file_get_string (kf, groups[i], "secret_key", NULL);
        conn->secret_key = mc_password_decode (raw_secret, "s3");
        g_free (raw_secret);

        error = NULL;
        conn->use_path_style = g_key_file_get_boolean (kf, groups[i], "use_path_style", &error);
        if (error != NULL)
        {
            g_error_free (error);
            conn->use_path_style = FALSE;
        }

        error = NULL;
        conn->timeout = g_key_file_get_integer (kf, groups[i], "timeout", &error);
        if (error != NULL)
        {
            g_error_free (error);
            conn->timeout = 0;
        }

        error = NULL;
        conn->connect_timeout = g_key_file_get_integer (kf, groups[i], "connect_timeout", &error);
        if (error != NULL)
        {
            g_error_free (error);
            conn->connect_timeout = 0;
        }

        if (conn->access_key == NULL || conn->access_key[0] == '\0')
        {
            s3_connection_free (conn);
            continue;
        }

        if (conn->region == NULL || conn->region[0] == '\0')
        {
            g_free (conn->region);
            conn->region = g_strdup ("us-east-1");
        }

        /* Empty endpoint means AWS */
        if (conn->endpoint != NULL && conn->endpoint[0] == '\0')
        {
            g_free (conn->endpoint);
            conn->endpoint = NULL;
        }

        g_ptr_array_add (arr, conn);
    }

    g_strfreev (groups);
    g_key_file_free (kf);
    return arr;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
s3_save_connections (const char *filepath, GPtrArray *connections)
{
    GKeyFile *kf;
    gchar *data;
    gsize length;
    gboolean ok;
    gchar *dir;
    guint i;

    kf = g_key_file_new ();

    for (i = 0; i < connections->len; i++)
    {
        const s3_connection_t *conn =
            (const s3_connection_t *) g_ptr_array_index (connections, i);

        g_key_file_set_string (kf, conn->label, "access_key", conn->access_key);

        if (conn->secret_key != NULL && conn->secret_key[0] != '\0')
        {
            char *enc = mc_password_encode (conn->secret_key, "s3");

            if (enc != NULL)
            {
                g_key_file_set_string (kf, conn->label, "secret_key", enc);
                g_free (enc);
            }
        }

        g_key_file_set_string (kf, conn->label, "region",
                               conn->region != NULL ? conn->region : "us-east-1");

        if (conn->endpoint != NULL && conn->endpoint[0] != '\0')
            g_key_file_set_string (kf, conn->label, "endpoint", conn->endpoint);

        g_key_file_set_boolean (kf, conn->label, "use_path_style", conn->use_path_style);

        if (conn->timeout > 0)
            g_key_file_set_integer (kf, conn->label, "timeout", conn->timeout);
        if (conn->connect_timeout > 0)
            g_key_file_set_integer (kf, conn->label, "connect_timeout", conn->connect_timeout);
    }

    data = g_key_file_to_data (kf, &length, NULL);
    g_key_file_free (kf);

    if (data == NULL)
        return FALSE;

    dir = g_path_get_dirname (filepath);
    g_mkdir_with_parents (dir, 0700);
    g_free (dir);

    ok = g_file_set_contents (filepath, data, (gssize) length, NULL);
    g_free (data);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */
/* Find helpers                                                                                  */
/* --------------------------------------------------------------------------------------------- */

static s3_connection_t *
s3_find_connection (const s3_data_t *data, const char *label)
{
    guint i;

    for (i = 0; i < data->connections->len; i++)
    {
        s3_connection_t *c = (s3_connection_t *) g_ptr_array_index (data->connections, i);

        if (strcmp (c->label, label) == 0)
            return c;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_dir_entry_t *
s3_find_entry (const s3_data_t *data, const char *name)
{
    guint i;

    if (data->entries == NULL)
        return NULL;

    for (i = 0; i < data->entries->len; i++)
    {
        mc_pp_dir_entry_t *e = (mc_pp_dir_entry_t *) g_ptr_array_index (data->entries, i);

        if (strcmp (e->name, name) == 0)
            return e;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */
/* URL construction                                                                              */
/* --------------------------------------------------------------------------------------------- */

/**
 * Return a copy of endpoint with trailing slashes removed.
 */
static char *
s3_strip_trailing_slashes (const char *str)
{
    char *copy;
    size_t len;

    copy = g_strdup (str);
    len = strlen (copy);
    while (len > 0 && copy[len - 1] == '/')
        copy[--len] = '\0';
    return copy;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Build the S3 service URL (for listing all buckets).
 * Virtual-hosted: https://s3.REGION.amazonaws.com/
 * Path-style / custom endpoint: ENDPOINT/
 */
static char *
s3_build_url_service (const s3_connection_t *conn)
{
    if (conn->endpoint != NULL && conn->endpoint[0] != '\0')
    {
        char *ep = s3_strip_trailing_slashes (conn->endpoint);
        char *url = g_strdup_printf ("%s/", ep);

        g_free (ep);
        return url;
    }

    return g_strdup_printf ("https://s3.%s.amazonaws.com/", conn->region);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Build the URL for bucket-level operations.
 * Virtual-hosted: https://BUCKET.s3.REGION.amazonaws.com/
 * Path-style: ENDPOINT/BUCKET/
 */
static char *
s3_build_url_bucket (const s3_connection_t *conn, const char *bucket)
{
    if (conn->endpoint != NULL && conn->endpoint[0] != '\0')
    {
        char *ep = s3_strip_trailing_slashes (conn->endpoint);
        char *url = g_strdup_printf ("%s/%s/", ep, bucket);

        g_free (ep);
        return url;
    }

    if (conn->use_path_style)
        return g_strdup_printf ("https://s3.%s.amazonaws.com/%s/", conn->region, bucket);

    return g_strdup_printf ("https://%s.s3.%s.amazonaws.com/", bucket, conn->region);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Build the URL for listing objects in a bucket (ListObjectsV2).
 * Appends ?list-type=2&prefix=P&delimiter=/ and optional continuation-token.
 */
static char *
s3_build_url_object_list (const s3_connection_t *conn, const char *bucket, const char *prefix,
                          const char *continuation_token)
{
    char *base;
    char *escaped_prefix = NULL;
    char *escaped_token = NULL;
    char *url;

    base = s3_build_url_bucket (conn, bucket);

    if (prefix != NULL && prefix[0] != '\0')
        escaped_prefix = curl_easy_escape (NULL, prefix, 0);

    if (continuation_token != NULL && continuation_token[0] != '\0')
        escaped_token = curl_easy_escape (NULL, continuation_token, 0);

    if (escaped_prefix != NULL && escaped_token != NULL)
        url = g_strdup_printf ("%s?list-type=2&prefix=%s&delimiter=/&continuation-token=%s", base,
                               escaped_prefix, escaped_token);
    else if (escaped_prefix != NULL)
        url = g_strdup_printf ("%s?list-type=2&prefix=%s&delimiter=/", base, escaped_prefix);
    else if (escaped_token != NULL)
        url = g_strdup_printf ("%s?list-type=2&delimiter=/&continuation-token=%s", base,
                               escaped_token);
    else
        url = g_strdup_printf ("%s?list-type=2&delimiter=/", base);

    if (escaped_prefix != NULL)
        curl_free (escaped_prefix);
    if (escaped_token != NULL)
        curl_free (escaped_token);
    g_free (base);

    return url;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Escape an S3 object key for use in a URL path.
 * Escapes each path component individually, preserving '/' separators.
 * Caller must g_free() the result.
 */
static char *
s3_escape_key (const char *key)
{
    gchar **parts;
    GString *result;
    int i;

    if (key == NULL || key[0] == '\0')
        return g_strdup ("");

    parts = g_strsplit (key, "/", -1);
    result = g_string_new ("");

    for (i = 0; parts[i] != NULL; i++)
    {
        char *escaped;

        if (i > 0)
            g_string_append_c (result, '/');

        if (parts[i][0] == '\0')
            continue;

        escaped = curl_easy_escape (NULL, parts[i], 0);
        g_string_append (result, escaped);
        curl_free (escaped);
    }

    g_strfreev (parts);
    return g_string_free (result, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Build the URL for an individual object.
 * Virtual-hosted: https://BUCKET.s3.REGION.amazonaws.com/KEY
 * Path-style: ENDPOINT/BUCKET/KEY
 */
static char *
s3_build_url_object (const s3_connection_t *conn, const char *bucket, const char *key)
{
    char *escaped_key;
    char *url;

    escaped_key = s3_escape_key (key);

    if (conn->endpoint != NULL && conn->endpoint[0] != '\0')
    {
        char *ep = s3_strip_trailing_slashes (conn->endpoint);

        url = g_strdup_printf ("%s/%s/%s", ep, bucket, escaped_key);
        g_free (ep);
    }
    else if (conn->use_path_style)
        url = g_strdup_printf ("https://s3.%s.amazonaws.com/%s/%s", conn->region, bucket,
                               escaped_key);
    else
        url = g_strdup_printf ("https://%s.s3.%s.amazonaws.com/%s", bucket, conn->region,
                               escaped_key);

    g_free (escaped_key);
    return url;
}

/* --------------------------------------------------------------------------------------------- */
/* Formatting helpers                                                                            */
/* --------------------------------------------------------------------------------------------- */

static void
s3_format_size (off_t size, char *buf, size_t buf_size)
{
    if (size < 1024)
        g_snprintf (buf, buf_size, "%d B", (int) size);
    else if (size < 1024 * 1024)
        g_snprintf (buf, buf_size, "%.1f KiB", (double) size / 1024.0);
    else if (size < (off_t) 1024 * 1024 * 1024)
        g_snprintf (buf, buf_size, "%.1f MiB", (double) size / (1024.0 * 1024.0));
    else
        g_snprintf (buf, buf_size, "%.1f GiB", (double) size / (1024.0 * 1024.0 * 1024.0));
}

/* --------------------------------------------------------------------------------------------- */
/* curl helpers                                                                                  */
/* --------------------------------------------------------------------------------------------- */

static size_t
s3_write_cb (void *ptr, size_t size, size_t nmemb, void *userdata)
{
    s3_buf_ctx_t *ctx = (s3_buf_ctx_t *) userdata;
    size_t total = size * nmemb;

    g_string_append_len (ctx->buf, (const char *) ptr, (gssize) total);
    return total;
}

/* --------------------------------------------------------------------------------------------- */

static size_t
s3_file_write_cb (void *ptr, size_t size, size_t nmemb, void *userdata)
{
    s3_file_write_ctx_t *ctx = (s3_file_write_ctx_t *) userdata;
    size_t total = size * nmemb;
    ssize_t written;

    written = write (ctx->fd, ptr, total);
    return (written < 0) ? 0 : (size_t) written;
}

/* --------------------------------------------------------------------------------------------- */

static size_t
s3_file_read_cb (void *ptr, size_t size, size_t nmemb, void *userdata)
{
    s3_file_read_ctx_t *ctx = (s3_file_read_ctx_t *) userdata;
    size_t want = size * nmemb;
    ssize_t n;

    if (ctx->remaining <= 0)
        return 0;

    if ((off_t) want > ctx->remaining)
        want = (size_t) ctx->remaining;

    n = read (ctx->fd, ptr, want);
    if (n <= 0)
        return 0;

    ctx->remaining -= n;
    return (size_t) n;
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/* Progress dialog                                                                               */
/* --------------------------------------------------------------------------------------------- */

static cb_ret_t
s3_progress_dlg_callback (Widget *w, Widget *sender, widget_msg_t msg, int parm, void *data)
{
    if (msg == MSG_ACTION && parm == CK_Cancel)
    {
        DIALOG (w)->ret_value = B_CANCEL;
        return MSG_HANDLED;
    }

    return dlg_default_callback (w, sender, msg, parm, data);
}

/* --------------------------------------------------------------------------------------------- */

static int
s3_progress_btn_callback (MC_UNUSED WButton *button, MC_UNUSED int action)
{
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static s3_progress_t *
s3_progress_create (const char *direction, const char *fname)
{
    s3_progress_t *p;
    WGroup *g;
    WButton *abort_btn;
    int dlg_width = 56;
    int y = 2, x = 3;
    int gauge_width;
    int btn_width;
    char title[128];

    p = g_new0 (s3_progress_t, 1);
    p->direction = direction;
    p->fname = fname;
    p->start_time = g_get_monotonic_time ();
    p->visible = FALSE;
    p->aborted = FALSE;
    p->last_col = -1;

    gauge_width = dlg_width - 2 * x;
    p->gauge_cols = gauge_width - 7;

    g_snprintf (title, sizeof (title), " %s ", direction);
    p->dlg = dlg_create (TRUE, 0, 0, 10, dlg_width, WPOS_CENTER, FALSE, dialog_colors,
                         s3_progress_dlg_callback, NULL, NULL, title);
    g = GROUP (p->dlg);

    /* filename */
    p->lbl_file = label_new (y++, x, fname);
    group_add_widget (g, p->lbl_file);

    /* size info */
    p->lbl_size = label_new (y++, x, "");
    group_add_widget (g, p->lbl_size);

    /* gauge */
    p->gauge = gauge_new (y++, x, gauge_width, FALSE, 100, 0);
    group_add_widget_autopos (g, p->gauge, WPOS_KEEP_TOP | WPOS_KEEP_HORZ, NULL);

    /* separator */
    group_add_widget (g, hline_new (y++, -1, -1));

    /* Abort button */
    abort_btn =
        button_new (y, 0, B_CANCEL, NORMAL_BUTTON, N_ ("&Abort"), s3_progress_btn_callback);
    btn_width = button_get_width (abort_btn);
    WIDGET (abort_btn)->rect.x = (dlg_width - btn_width) / 2;
    group_add_widget (g, abort_btn);
    widget_select (WIDGET (abort_btn));

    return p;
}

/* --------------------------------------------------------------------------------------------- */

static void
s3_progress_destroy (s3_progress_t *p)
{
    if (p == NULL)
        return;

    if (p->visible)
        dlg_run_done (p->dlg);

    widget_destroy (WIDGET (p->dlg));
    g_free (p);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
s3_progress_check_buttons (s3_progress_t *p)
{
    int c;
    Gpm_Event event;

    if (p == NULL || p->aborted || !p->visible)
        return (p == NULL || !p->aborted);

    event.x = -1;
    c = tty_get_event (&event, FALSE, FALSE);
    if (c == EV_NONE)
        return TRUE;

    p->dlg->ret_value = 0;
    dlg_process_event (p->dlg, c, &event);

    if (p->dlg->ret_value == B_CANCEL)
    {
        if (query_dialog (N_ ("S3"), N_ ("Abort current operation?"), D_NORMAL, 2, N_ ("&Yes"),
                          N_ ("&No"))
            == 0)
        {
            p->aborted = TRUE;
            return FALSE;
        }
        p->dlg->ret_value = 0;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
s3_progress_update (s3_progress_t *p, curl_off_t total, curl_off_t now)
{
    char buf_done[32], buf_total[32], buf_speed[32];
    char line[256];
    int col;
    gint64 now_time;
    double elapsed;

    if (p == NULL)
        return TRUE;
    if (p->aborted)
        return FALSE;

    now_time = g_get_monotonic_time ();

    /* defer display for 0.5 second */
    if (!p->visible)
    {
        if (now_time - p->start_time < G_USEC_PER_SEC / 2)
            return TRUE;

        dlg_init (p->dlg);
        p->visible = TRUE;
    }

    if (!s3_progress_check_buttons (p))
        return FALSE;

    col = (total > 0) ? (int) ((gint64) p->gauge_cols * now / total) : 0;
    if (col == p->last_col)
        return TRUE;
    p->last_col = col;

    s3_format_size ((off_t) now, buf_done, sizeof (buf_done));
    elapsed = (double) (now_time - p->start_time) / G_USEC_PER_SEC;

    if (total > 0)
    {
        s3_format_size ((off_t) total, buf_total, sizeof (buf_total));

        if (elapsed > 0.5 && now > 0)
        {
            double speed = (double) now / elapsed;
            double eta = (total > now) ? (double) (total - now) / speed : 0;
            int eta_sec = (int) eta;

            s3_format_size ((off_t) speed, buf_speed, sizeof (buf_speed));
            g_snprintf (line, sizeof (line), " %s / %s @ %s/s - %d:%02d", buf_done, buf_total,
                        buf_speed, eta_sec / 60, eta_sec % 60);
        }
        else
            g_snprintf (line, sizeof (line), " %s / %s", buf_done, buf_total);

        gauge_set_value (p->gauge, p->gauge_cols, col);
        gauge_show (p->gauge, TRUE);
    }
    else
    {
        g_snprintf (line, sizeof (line), " %s", buf_done);
        gauge_show (p->gauge, FALSE);
    }

    label_set_text (p->lbl_size, line);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static int
s3_progress_cb (void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                curl_off_t ulnow)
{
    s3_progress_t *p = (s3_progress_t *) clientp;
    curl_off_t total, now;

    if (p == NULL)
        return 0;

    if (ultotal > 0)
    {
        total = ultotal;
        now = ulnow;
    }
    else
    {
        total = dltotal;
        now = dlnow;
    }

    return s3_progress_update (p, total, now) ? 0 : 1;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Set up common curl options for S3 requests:
 * - AWS SigV4 signing via CURLOPT_AWS_SIGV4
 * - Access key / secret key via CURLOPT_USERPWD
 * - Timeouts
 */
static void
s3_setup_curl (CURL *curl, const s3_connection_t *conn)
{
    char *sigv4;
    char *userpwd;
    long timeout;
    long connect_timeout;

    sigv4 = g_strdup_printf ("aws:amz:%s:s3", conn->region);
    curl_easy_setopt (curl, CURLOPT_AWS_SIGV4, sigv4);
    g_free (sigv4);

    userpwd = g_strdup_printf ("%s:%s", conn->access_key,
                               conn->secret_key != NULL ? conn->secret_key : "");
    curl_easy_setopt (curl, CURLOPT_USERPWD, userpwd);
    g_free (userpwd);

    timeout = (conn->timeout > 0) ? (long) conn->timeout : 30L;
    connect_timeout = (conn->connect_timeout > 0) ? (long) conn->connect_timeout : 10L;

    curl_easy_setopt (curl, CURLOPT_TIMEOUT, timeout);
    curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, connect_timeout);
    curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Perform a simple GET request, return the response body as GString.
 * Returns NULL on failure. Caller owns the returned GString.
 */
static GString *
s3_perform_get (const s3_connection_t *conn, const char *url, long *http_code_out)
{
    CURL *curl;
    s3_buf_ctx_t ctx;
    CURLcode res;
    long http_code = 0;

    S3_LOG ("perform_get: URL = %s", url);

    curl = curl_easy_init ();
    if (curl == NULL)
    {
        S3_LOG ("perform_get: curl_easy_init failed");
        return NULL;
    }

    ctx.buf = g_string_new ("");

    curl_easy_setopt (curl, CURLOPT_URL, url);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, s3_write_cb);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, &ctx);
    s3_setup_curl (curl, conn);

    res = curl_easy_perform (curl);
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup (curl);

    S3_LOG ("perform_get: result=%d (%s) http_code=%ld body_len=%lu", (int) res,
            curl_easy_strerror (res), http_code, (unsigned long) ctx.buf->len);

    if (http_code_out != NULL)
        *http_code_out = http_code;

    if (res != CURLE_OK || http_code < 200 || http_code >= 300)
    {
        S3_LOG ("perform_get: FAILED, body='%.500s'", ctx.buf->str);
        g_string_free (ctx.buf, TRUE);
        return NULL;
    }

    return ctx.buf;
}

/* --------------------------------------------------------------------------------------------- */
/* ISO 8601 date parser                                                                          */
/* --------------------------------------------------------------------------------------------- */

static time_t
s3_parse_iso8601 (const char *str)
{
    struct tm tm;
    int year, month, day, hour, minute, second;

    if (str == NULL)
        return 0;

    memset (&tm, 0, sizeof (tm));

    if (sscanf (str, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 6)
    {
        /* Try date-only format */
        if (sscanf (str, "%d-%d-%d", &year, &month, &day) < 3)
            return 0;
        hour = minute = second = 0;
    }

    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;

    return timegm (&tm);
}

/* --------------------------------------------------------------------------------------------- */
/* XML parsers (GMarkupParser / SAX-style)                                                       */
/* --------------------------------------------------------------------------------------------- */

/* --- ListAllMyBucketsResult parser --- */

static void
s3_xml_buckets_start (GMarkupParseContext *context, const gchar *element_name,
                      const gchar **attribute_names, const gchar **attribute_values,
                      gpointer user_data, GError **error)
{
    s3_xml_list_buckets_t *ctx = (s3_xml_list_buckets_t *) user_data;

    (void) context;
    (void) attribute_names;
    (void) attribute_values;
    (void) error;

    if (strcmp (element_name, "Bucket") == 0)
    {
        ctx->in_bucket = TRUE;
        g_free (ctx->bucket_name);
        ctx->bucket_name = NULL;
        ctx->creation_date = 0;
    }
    else if (ctx->in_bucket && strcmp (element_name, "Name") == 0)
        ctx->in_name = TRUE;
    else if (ctx->in_bucket && strcmp (element_name, "CreationDate") == 0)
        ctx->in_creation_date = TRUE;

    g_string_truncate (ctx->text, 0);
}

/* --------------------------------------------------------------------------------------------- */

static void
s3_xml_buckets_end (GMarkupParseContext *context, const gchar *element_name, gpointer user_data,
                    GError **error)
{
    s3_xml_list_buckets_t *ctx = (s3_xml_list_buckets_t *) user_data;

    (void) context;
    (void) error;

    if (strcmp (element_name, "Bucket") == 0)
    {
        if (ctx->bucket_name != NULL && ctx->bucket_name[0] != '\0')
        {
            mc_pp_dir_entry_t *entry;

            entry = g_new0 (mc_pp_dir_entry_t, 1);
            entry->name = ctx->bucket_name;
            ctx->bucket_name = NULL;
            entry->is_dir = TRUE;
            memset (&entry->st, 0, sizeof (entry->st));
            entry->st.st_mode = S_IFDIR | 0755;
            entry->st.st_mtime = ctx->creation_date;

            g_ptr_array_add (ctx->entries, entry);
        }
        else
            g_free (ctx->bucket_name);

        ctx->bucket_name = NULL;
        ctx->in_bucket = FALSE;
    }
    else if (ctx->in_name && strcmp (element_name, "Name") == 0)
    {
        g_free (ctx->bucket_name);
        ctx->bucket_name = g_strdup (ctx->text->str);
        ctx->in_name = FALSE;
    }
    else if (ctx->in_creation_date && strcmp (element_name, "CreationDate") == 0)
    {
        ctx->creation_date = s3_parse_iso8601 (ctx->text->str);
        ctx->in_creation_date = FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
s3_xml_buckets_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
                     gpointer user_data, GError **error)
{
    s3_xml_list_buckets_t *ctx = (s3_xml_list_buckets_t *) user_data;

    (void) context;
    (void) error;

    g_string_append_len (ctx->text, text, (gssize) text_len);
}

/* --------------------------------------------------------------------------------------------- */

static const GMarkupParser s3_xml_buckets_parser = {
    .start_element = s3_xml_buckets_start,
    .end_element = s3_xml_buckets_end,
    .text = s3_xml_buckets_text,
    .passthrough = NULL,
    .error = NULL,
};

/* --------------------------------------------------------------------------------------------- */

/* --- ListObjectsV2 (ListBucketResult) parser --- */

static void
s3_xml_objects_start (GMarkupParseContext *context, const gchar *element_name,
                      const gchar **attribute_names, const gchar **attribute_values,
                      gpointer user_data, GError **error)
{
    s3_xml_list_objects_t *ctx = (s3_xml_list_objects_t *) user_data;

    (void) context;
    (void) attribute_names;
    (void) attribute_values;
    (void) error;

    if (strcmp (element_name, "Contents") == 0)
    {
        ctx->in_contents = TRUE;
        g_free (ctx->key);
        ctx->key = NULL;
        ctx->size = 0;
        ctx->mtime = 0;
    }
    else if (strcmp (element_name, "CommonPrefixes") == 0)
        ctx->in_common_prefixes = TRUE;
    else if ((ctx->in_contents || ctx->in_common_prefixes) && strcmp (element_name, "Key") == 0)
        ctx->in_key = TRUE;
    else if (ctx->in_contents && strcmp (element_name, "Size") == 0)
        ctx->in_size = TRUE;
    else if (ctx->in_contents && strcmp (element_name, "LastModified") == 0)
        ctx->in_last_modified = TRUE;
    else if ((ctx->in_common_prefixes) && strcmp (element_name, "Prefix") == 0)
        ctx->in_prefix = TRUE;
    else if (strcmp (element_name, "IsTruncated") == 0)
        ctx->in_is_truncated = TRUE;
    else if (strcmp (element_name, "NextContinuationToken") == 0)
        ctx->in_next_continuation_token = TRUE;

    g_string_truncate (ctx->text, 0);
}

/* --------------------------------------------------------------------------------------------- */

static void
s3_xml_objects_end (GMarkupParseContext *context, const gchar *element_name, gpointer user_data,
                    GError **error)
{
    s3_xml_list_objects_t *ctx = (s3_xml_list_objects_t *) user_data;

    (void) context;
    (void) error;

    if (strcmp (element_name, "Contents") == 0)
    {
        if (ctx->key != NULL)
        {
            const char *display_name = ctx->key;
            size_t prefix_len;

            /* Strip the prefix to get the bare filename */
            if (ctx->strip_prefix != NULL)
            {
                prefix_len = strlen (ctx->strip_prefix);
                if (strncmp (ctx->key, ctx->strip_prefix, prefix_len) == 0)
                    display_name = ctx->key + prefix_len;
            }

            /* Skip empty names (the prefix itself) and trailing slashes (dirs) */
            if (display_name[0] != '\0' && strcmp (display_name, "/") != 0)
            {
                mc_pp_dir_entry_t *entry;
                size_t dlen = strlen (display_name);

                /* Skip entries that end with / (these are virtual dirs, handled by CommonPrefixes)
                 */
                if (dlen > 0 && display_name[dlen - 1] != '/')
                {
                    entry = g_new0 (mc_pp_dir_entry_t, 1);
                    entry->name = g_strdup (display_name);
                    entry->is_dir = FALSE;
                    memset (&entry->st, 0, sizeof (entry->st));
                    entry->st.st_mode = S_IFREG | 0644;
                    entry->st.st_size = ctx->size;
                    entry->st.st_mtime = ctx->mtime;

                    g_ptr_array_add (ctx->entries, entry);
                }
            }
        }
        g_free (ctx->key);
        ctx->key = NULL;
        ctx->in_contents = FALSE;
    }
    else if (strcmp (element_name, "CommonPrefixes") == 0)
    {
        ctx->in_common_prefixes = FALSE;
    }
    else if (ctx->in_key && strcmp (element_name, "Key") == 0)
    {
        g_free (ctx->key);
        ctx->key = g_strdup (ctx->text->str);
        ctx->in_key = FALSE;
    }
    else if (ctx->in_size && strcmp (element_name, "Size") == 0)
    {
        ctx->size = (off_t) g_ascii_strtoll (ctx->text->str, NULL, 10);
        ctx->in_size = FALSE;
    }
    else if (ctx->in_last_modified && strcmp (element_name, "LastModified") == 0)
    {
        ctx->mtime = s3_parse_iso8601 (ctx->text->str);
        ctx->in_last_modified = FALSE;
    }
    else if (ctx->in_prefix && strcmp (element_name, "Prefix") == 0)
    {
        const char *pfx = ctx->text->str;
        const char *display_name = pfx;
        size_t prefix_len;

        /* Strip the current prefix */
        if (ctx->strip_prefix != NULL)
        {
            prefix_len = strlen (ctx->strip_prefix);
            if (strncmp (pfx, ctx->strip_prefix, prefix_len) == 0)
                display_name = pfx + prefix_len;
        }

        if (display_name[0] != '\0')
        {
            mc_pp_dir_entry_t *entry;
            char *name;
            size_t dlen;

            /* Remove trailing slash for display */
            name = g_strdup (display_name);
            dlen = strlen (name);
            if (dlen > 0 && name[dlen - 1] == '/')
                name[dlen - 1] = '\0';

            if (name[0] != '\0')
            {
                entry = g_new0 (mc_pp_dir_entry_t, 1);
                entry->name = name;
                entry->is_dir = TRUE;
                memset (&entry->st, 0, sizeof (entry->st));
                entry->st.st_mode = S_IFDIR | 0755;
                entry->st.st_mtime = time (NULL);

                g_ptr_array_add (ctx->entries, entry);
            }
            else
                g_free (name);
        }
        ctx->in_prefix = FALSE;
    }
    else if (ctx->in_is_truncated && strcmp (element_name, "IsTruncated") == 0)
    {
        ctx->is_truncated =
            (g_ascii_strcasecmp (ctx->text->str, "true") == 0);
        ctx->in_is_truncated = FALSE;
    }
    else if (ctx->in_next_continuation_token
             && strcmp (element_name, "NextContinuationToken") == 0)
    {
        g_free (ctx->next_continuation_token);
        ctx->next_continuation_token = g_strdup (ctx->text->str);
        ctx->in_next_continuation_token = FALSE;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
s3_xml_objects_text (GMarkupParseContext *context, const gchar *text, gsize text_len,
                     gpointer user_data, GError **error)
{
    s3_xml_list_objects_t *ctx = (s3_xml_list_objects_t *) user_data;

    (void) context;
    (void) error;

    g_string_append_len (ctx->text, text, (gssize) text_len);
}

/* --------------------------------------------------------------------------------------------- */

static const GMarkupParser s3_xml_objects_parser = {
    .start_element = s3_xml_objects_start,
    .end_element = s3_xml_objects_end,
    .text = s3_xml_objects_text,
    .passthrough = NULL,
    .error = NULL,
};

/* --------------------------------------------------------------------------------------------- */
/* S3 API operations                                                                             */
/* --------------------------------------------------------------------------------------------- */

/**
 * List all buckets. Returns a GPtrArray of mc_pp_dir_entry_t*, or NULL on error.
 */
static GPtrArray *
s3_api_list_buckets (const s3_connection_t *conn)
{
    char *url;
    GString *body;
    s3_xml_list_buckets_t ctx;
    GMarkupParseContext *xml;

    url = s3_build_url_service (conn);
    body = s3_perform_get (conn, url, NULL);
    g_free (url);

    if (body == NULL)
        return NULL;

    memset (&ctx, 0, sizeof (ctx));
    ctx.entries = g_ptr_array_new_with_free_func (mc_pp_dir_entry_free);
    ctx.text = g_string_new ("");

    xml = g_markup_parse_context_new (&s3_xml_buckets_parser, 0, &ctx, NULL);
    g_markup_parse_context_parse (xml, body->str, (gssize) body->len, NULL);
    g_markup_parse_context_end_parse (xml, NULL);
    g_markup_parse_context_free (xml);

    g_string_free (body, TRUE);
    g_string_free (ctx.text, TRUE);
    g_free (ctx.bucket_name);

    return ctx.entries;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * List objects in a bucket with given prefix.
 * Handles pagination via IsTruncated / NextContinuationToken.
 * Returns a GPtrArray of mc_pp_dir_entry_t*, or NULL on error.
 */
static GPtrArray *
s3_api_list_objects (const s3_connection_t *conn, const char *bucket, const char *prefix)
{
    GPtrArray *entries;
    char *continuation_token = NULL;

    entries = g_ptr_array_new_with_free_func (mc_pp_dir_entry_free);

    while (TRUE)
    {
        char *url;
        GString *body;
        s3_xml_list_objects_t ctx;
        GMarkupParseContext *xml;

        url = s3_build_url_object_list (conn, bucket, prefix, continuation_token);
        body = s3_perform_get (conn, url, NULL);
        g_free (url);
        g_free (continuation_token);
        continuation_token = NULL;

        if (body == NULL)
        {
            if (entries->len == 0)
            {
                g_ptr_array_free (entries, TRUE);
                return NULL;
            }
            break;
        }

        memset (&ctx, 0, sizeof (ctx));
        ctx.entries = entries;
        ctx.text = g_string_new ("");
        ctx.strip_prefix = prefix;

        xml = g_markup_parse_context_new (&s3_xml_objects_parser, 0, &ctx, NULL);
        g_markup_parse_context_parse (xml, body->str, (gssize) body->len, NULL);
        g_markup_parse_context_end_parse (xml, NULL);
        g_markup_parse_context_free (xml);

        g_string_free (body, TRUE);
        g_string_free (ctx.text, TRUE);
        g_free (ctx.key);

        if (!ctx.is_truncated)
        {
            g_free (ctx.next_continuation_token);
            break;
        }

        continuation_token = ctx.next_continuation_token;
        if (continuation_token == NULL)
            break;
    }

    return entries;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Download an object to a file descriptor.
 * Returns TRUE on success.
 */
static gboolean
s3_api_download_object (const s3_connection_t *conn, const char *bucket, const char *key, int fd,
                        const char *display_name)
{
    CURL *curl;
    s3_file_write_ctx_t ctx;
    s3_progress_t *progress;
    char *url;
    CURLcode res;
    long http_code = 0;

    curl = curl_easy_init ();
    if (curl == NULL)
        return FALSE;

    url = s3_build_url_object (conn, bucket, key);
    S3_LOG ("download_object: URL = %s", url);
    ctx.fd = fd;

    curl_easy_setopt (curl, CURLOPT_URL, url);
    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, s3_file_write_cb);
    curl_easy_setopt (curl, CURLOPT_WRITEDATA, &ctx);
    s3_setup_curl (curl, conn);

    progress = s3_progress_create ("Downloading", display_name != NULL ? display_name : key);
    curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION, s3_progress_cb);
    curl_easy_setopt (curl, CURLOPT_XFERINFODATA, progress);
    curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);

    res = curl_easy_perform (curl);
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup (curl);
    g_free (url);

    s3_progress_destroy (progress);

    S3_LOG ("download_object: result=%d http_code=%ld", (int) res, http_code);
    return (res == CURLE_OK && http_code >= 200 && http_code < 300);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Upload a file to an S3 object.
 * Returns TRUE on success.
 */
static gboolean
s3_api_upload_object (const s3_connection_t *conn, const char *bucket, const char *key,
                      int fd, off_t file_size, const char *display_name)
{
    CURL *curl;
    s3_file_read_ctx_t ctx;
    s3_progress_t *progress;
    char *url;
    CURLcode res;
    long http_code = 0;

    curl = curl_easy_init ();
    if (curl == NULL)
        return FALSE;

    url = s3_build_url_object (conn, bucket, key);
    ctx.fd = fd;
    ctx.remaining = file_size;

    curl_easy_setopt (curl, CURLOPT_URL, url);
    curl_easy_setopt (curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt (curl, CURLOPT_READFUNCTION, s3_file_read_cb);
    curl_easy_setopt (curl, CURLOPT_READDATA, &ctx);
    curl_easy_setopt (curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t) file_size);
    s3_setup_curl (curl, conn);

    progress = s3_progress_create ("Uploading", display_name != NULL ? display_name : key);
    curl_easy_setopt (curl, CURLOPT_XFERINFOFUNCTION, s3_progress_cb);
    curl_easy_setopt (curl, CURLOPT_XFERINFODATA, progress);
    curl_easy_setopt (curl, CURLOPT_NOPROGRESS, 0L);

    res = curl_easy_perform (curl);
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup (curl);
    g_free (url);

    s3_progress_destroy (progress);

    return (res == CURLE_OK && http_code >= 200 && http_code < 300);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Delete an S3 object.
 * Returns TRUE on success.
 */
static gboolean
s3_api_delete_object (const s3_connection_t *conn, const char *bucket, const char *key)
{
    CURL *curl;
    char *url;
    CURLcode res;
    long http_code = 0;

    curl = curl_easy_init ();
    if (curl == NULL)
        return FALSE;

    url = s3_build_url_object (conn, bucket, key);

    curl_easy_setopt (curl, CURLOPT_URL, url);
    curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt (curl, CURLOPT_NOBODY, 0L);
    s3_setup_curl (curl, conn);

    res = curl_easy_perform (curl);
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup (curl);
    g_free (url);

    /* 204 No Content is the expected response for successful DELETE */
    return (res == CURLE_OK && http_code >= 200 && http_code < 300);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Recursively delete all objects under a given prefix (virtual directory).
 * Returns TRUE on success.
 */
static gboolean
s3_api_delete_prefix (const s3_connection_t *conn, const char *bucket, const char *prefix)
{
    GPtrArray *entries;
    guint i;
    gboolean ok = TRUE;

    entries = s3_api_list_objects (conn, bucket, prefix);
    if (entries == NULL)
        return FALSE;

    for (i = 0; i < entries->len; i++)
    {
        const mc_pp_dir_entry_t *e = (const mc_pp_dir_entry_t *) g_ptr_array_index (entries, i);
        char *full_key;

        full_key = g_strdup_printf ("%s%s%s", prefix, e->name,
                                    e->is_dir ? "/" : "");

        if (e->is_dir)
        {
            if (!s3_api_delete_prefix (conn, bucket, full_key))
                ok = FALSE;
        }
        else
        {
            if (!s3_api_delete_object (conn, bucket, full_key))
                ok = FALSE;
        }

        g_free (full_key);
    }

    g_ptr_array_free (entries, TRUE);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Create a new S3 bucket.
 * For non-us-east-1 regions, sends CreateBucketConfiguration XML.
 */
static gboolean
s3_api_create_bucket (const s3_connection_t *conn, const char *bucket)
{
    CURL *curl;
    char *url;
    CURLcode res;
    long http_code = 0;

    curl = curl_easy_init ();
    if (curl == NULL)
        return FALSE;

    url = s3_build_url_bucket (conn, bucket);

    curl_easy_setopt (curl, CURLOPT_URL, url);
    curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "PUT");
    s3_setup_curl (curl, conn);

    /* For non-us-east-1, we need to send location constraint */
    if (conn->region != NULL && strcmp (conn->region, "us-east-1") != 0)
    {
        char *body;
        struct curl_slist *headers = NULL;

        body = g_strdup_printf (
            "<CreateBucketConfiguration xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">"
            "<LocationConstraint>%s</LocationConstraint>"
            "</CreateBucketConfiguration>",
            conn->region);

        headers = curl_slist_append (headers, "Content-Type: application/xml");
        curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt (curl, CURLOPT_POSTFIELDS, body);

        res = curl_easy_perform (curl);
        curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all (headers);
        g_free (body);
    }
    else
    {
        curl_easy_setopt (curl, CURLOPT_POSTFIELDS, "");
        res = curl_easy_perform (curl);
        curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
    }

    curl_easy_cleanup (curl);
    g_free (url);

    return (res == CURLE_OK && http_code >= 200 && http_code < 300);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Delete an S3 bucket (must be empty).
 */
static gboolean
s3_api_delete_bucket (const s3_connection_t *conn, const char *bucket)
{
    CURL *curl;
    char *url;
    CURLcode res;
    long http_code = 0;

    curl = curl_easy_init ();
    if (curl == NULL)
        return FALSE;

    url = s3_build_url_bucket (conn, bucket);

    curl_easy_setopt (curl, CURLOPT_URL, url);
    curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    s3_setup_curl (curl, conn);

    res = curl_easy_perform (curl);
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup (curl);
    g_free (url);

    return (res == CURLE_OK && http_code >= 200 && http_code < 300);
}

/* --------------------------------------------------------------------------------------------- */
/* Entry loading / caching                                                                       */
/* --------------------------------------------------------------------------------------------- */

static void
s3_reload_entries (s3_data_t *data)
{
    S3_LOG ("reload_entries: level=%d bucket='%s' prefix='%s'", (int) data->level,
            data->current_bucket != NULL ? data->current_bucket : "(null)",
            data->current_prefix != NULL ? data->current_prefix : "(null)");

    if (data->entries != NULL)
    {
        g_ptr_array_free (data->entries, TRUE);
        data->entries = NULL;
    }

    if (data->active_connection == NULL)
    {
        S3_LOG ("reload_entries: no active connection");
        return;
    }

    if (data->level == S3_LEVEL_BUCKETS)
    {
        char *cache_key = g_strdup ("__buckets__");
        GPtrArray *cached;

        cached = mc_pp_dir_cache_lookup (&data->dir_cache, cache_key);
        if (cached != NULL)
        {
            data->entries = cached;
            S3_LOG ("reload_entries: cache hit, %u entries", data->entries->len);
            g_free (cache_key);
            return;
        }

        data->entries = s3_api_list_buckets (data->active_connection);
        S3_LOG ("reload_entries: list_buckets returned %s (%u entries)",
                data->entries != NULL ? "OK" : "NULL",
                data->entries != NULL ? data->entries->len : 0);
        if (data->entries != NULL)
            mc_pp_dir_cache_store (&data->dir_cache, cache_key, data->entries);

        g_free (cache_key);
    }
    else if (data->level == S3_LEVEL_OBJECTS)
    {
        char *cache_key;
        GPtrArray *cached;
        const char *prefix;

        prefix = (data->current_prefix != NULL) ? data->current_prefix : "";
        cache_key = g_strdup_printf ("%s/%s", data->current_bucket, prefix);

        cached = mc_pp_dir_cache_lookup (&data->dir_cache, cache_key);
        if (cached != NULL)
        {
            data->entries = cached;
            g_free (cache_key);
            return;
        }

        data->entries =
            s3_api_list_objects (data->active_connection, data->current_bucket, prefix);
        if (data->entries != NULL)
            mc_pp_dir_cache_store (&data->dir_cache, cache_key, data->entries);

        g_free (cache_key);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
s3_invalidate_current (s3_data_t *data)
{
    if (data->level == S3_LEVEL_BUCKETS)
        mc_pp_dir_cache_invalidate (&data->dir_cache, "__buckets__");
    else if (data->level == S3_LEVEL_OBJECTS && data->current_bucket != NULL)
    {
        char *cache_key;
        const char *prefix;

        prefix = (data->current_prefix != NULL) ? data->current_prefix : "";
        cache_key = g_strdup_printf ("%s/%s", data->current_bucket, prefix);
        mc_pp_dir_cache_invalidate (&data->dir_cache, cache_key);
        g_free (cache_key);
    }
}

/* --------------------------------------------------------------------------------------------- */
/* Connection dialog                                                                             */
/* --------------------------------------------------------------------------------------------- */

#define S3_DLG_HEIGHT 18
#define S3_DLG_WIDTH  56

static gboolean
s3_show_connection_dialog (s3_connection_t *conn)
{
    s3_connection_t *backup;
    char *label = g_strdup (conn->label != NULL ? conn->label : "");
    char *access_key = g_strdup (conn->access_key != NULL ? conn->access_key : "");
    char *secret_key = g_strdup (conn->secret_key != NULL ? conn->secret_key : "");
    char *region = g_strdup (conn->region != NULL ? conn->region : "us-east-1");
    char *endpoint = g_strdup (conn->endpoint != NULL ? conn->endpoint : "");
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
        QUICK_CHECKBOX (N_("Use &path-style URLs"), &use_path_style, NULL),
        QUICK_BUTTONS_OK_CANCEL,
        QUICK_END,
    };
    /* clang-format on */

    WRect r = { -1, -1, S3_DLG_HEIGHT, S3_DLG_WIDTH };

    quick_dialog_t qdlg = {
        .rect = r,
        .title = N_ ("S3 Connection"),
        .help = "[S3 Plugin]",
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

    s3_connection_copy_from (conn, backup);
    s3_connection_free (backup);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
/* Disconnect / level transitions                                                                */
/* --------------------------------------------------------------------------------------------- */

static void
s3_disconnect (s3_data_t *data)
{
    data->active_connection = NULL;
    data->level = S3_LEVEL_CONNECTIONS;

    g_free (data->current_bucket);
    data->current_bucket = NULL;
    g_free (data->current_prefix);
    data->current_prefix = NULL;

    if (data->entries != NULL)
    {
        g_ptr_array_free (data->entries, TRUE);
        data->entries = NULL;
    }

    mc_pp_dir_cache_clear (&data->dir_cache);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
s3_activate_connection (s3_data_t *data, s3_connection_t *conn)
{
    s3_disconnect (data);

    data->active_connection = conn;
    data->level = S3_LEVEL_BUCKETS;

    S3_LOG ("activate_connection: label='%s' region='%s' endpoint='%s'", conn->label,
            conn->region != NULL ? conn->region : "(null)",
            conn->endpoint != NULL ? conn->endpoint : "(null)");

    s3_reload_entries (data);

    S3_LOG ("activate_connection: entries=%s",
            data->entries != NULL ? "loaded" : "NULL (failed)");
    return (data->entries != NULL);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
s3_enter_bucket (s3_data_t *data, const char *bucket)
{
    g_free (data->current_bucket);
    data->current_bucket = g_strdup (bucket);
    g_free (data->current_prefix);
    data->current_prefix = g_strdup ("");
    data->level = S3_LEVEL_OBJECTS;

    s3_reload_entries (data);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
s3_enter_prefix (s3_data_t *data, const char *dir_name)
{
    char *new_prefix;

    if (data->current_prefix != NULL && data->current_prefix[0] != '\0')
        new_prefix = g_strdup_printf ("%s%s/", data->current_prefix, dir_name);
    else
        new_prefix = g_strdup_printf ("%s/", dir_name);

    g_free (data->current_prefix);
    data->current_prefix = new_prefix;

    if (data->entries != NULL)
    {
        g_ptr_array_free (data->entries, TRUE);
        data->entries = NULL;
    }

    s3_reload_entries (data);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Go up one prefix level, or back to bucket list if at root of bucket.
 */
static void
s3_go_up_prefix (s3_data_t *data)
{
    char *last_slash;

    if (data->current_prefix == NULL || data->current_prefix[0] == '\0')
    {
        /* At root of bucket -> go back to bucket list */
        g_free (data->current_bucket);
        data->current_bucket = NULL;
        g_free (data->current_prefix);
        data->current_prefix = NULL;
        data->level = S3_LEVEL_BUCKETS;

        if (data->entries != NULL)
        {
            g_ptr_array_free (data->entries, TRUE);
            data->entries = NULL;
        }

        s3_reload_entries (data);
        return;
    }

    /* Remove trailing slash for processing */
    {
        char *tmp = g_strdup (data->current_prefix);
        size_t len = strlen (tmp);

        if (len > 0 && tmp[len - 1] == '/')
            tmp[len - 1] = '\0';

        last_slash = strrchr (tmp, '/');
        if (last_slash != NULL)
        {
            *(last_slash + 1) = '\0';
            g_free (data->current_prefix);
            data->current_prefix = tmp;
        }
        else
        {
            g_free (tmp);
            g_free (data->current_prefix);
            data->current_prefix = g_strdup ("");
        }
    }

    if (data->entries != NULL)
    {
        g_ptr_array_free (data->entries, TRUE);
        data->entries = NULL;
    }

    s3_reload_entries (data);
}

/* --------------------------------------------------------------------------------------------- */
/* Plugin callbacks                                                                              */
/* --------------------------------------------------------------------------------------------- */

static void *
s3_open (mc_panel_host_t *host, const char *open_path)
{
    s3_data_t *data;

    (void) open_path;

    if (s3_curl_refcount == 0)
        curl_global_init (CURL_GLOBAL_DEFAULT);
    s3_curl_refcount++;

    data = g_new0 (s3_data_t, 1);
    data->host = host;
    data->level = S3_LEVEL_CONNECTIONS;
    data->current_bucket = NULL;
    data->current_prefix = NULL;
    data->entries = NULL;
    data->title_buf = NULL;
    data->active_connection = NULL;
    data->key_edit =
        s3_load_hotkey (S3_PANEL_KEY_EDIT, S3_PANEL_KEY_EDIT_DEFAULT, S3_KEY_F (4));

    data->connections_file = s3_get_connections_file_path ();
    data->connections = s3_load_connections (data->connections_file);

    mc_pp_dir_cache_init (&data->dir_cache, S3_DIR_CACHE_TTL);

    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void
s3_close (void *plugin_data)
{
    s3_data_t *data = (s3_data_t *) plugin_data;

    if (data->entries != NULL)
        g_ptr_array_free (data->entries, TRUE);

    mc_pp_dir_cache_destroy (&data->dir_cache);

    g_ptr_array_free (data->connections, TRUE);

    g_free (data->current_bucket);
    g_free (data->current_prefix);
    g_free (data->title_buf);
    g_free (data->connections_file);
    g_free (data);

    if (s3_curl_refcount > 0)
        s3_curl_refcount--;

    if (s3_curl_refcount == 0)
        curl_global_cleanup ();
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_get_items (void *plugin_data, void *list_ptr)
{
    s3_data_t *data = (s3_data_t *) plugin_data;
    guint i;

    switch (data->level)
    {
    case S3_LEVEL_CONNECTIONS:
        for (i = 0; i < data->connections->len; i++)
        {
            const s3_connection_t *conn =
                (const s3_connection_t *) g_ptr_array_index (data->connections, i);

            mc_pp_add_entry (list_ptr, conn->label, S_IFDIR | 0755, 0, time (NULL));
        }
        return MC_PPR_OK;

    case S3_LEVEL_BUCKETS:
    case S3_LEVEL_OBJECTS:
        if (data->entries != NULL)
        {
            for (i = 0; i < data->entries->len; i++)
            {
                const mc_pp_dir_entry_t *e =
                    (const mc_pp_dir_entry_t *) g_ptr_array_index (data->entries, i);
                mode_t mode;
                off_t size;

                mode = e->st.st_mode;
                if (!S_ISDIR (mode) && !S_ISREG (mode))
                    mode = S_IFREG | 0644;

                size = S_ISDIR (mode) ? 0 : e->st.st_size;
                mc_pp_add_entry (list_ptr, e->name, mode, size,
                                 e->st.st_mtime != 0 ? e->st.st_mtime : time (NULL));
            }
        }
        return MC_PPR_OK;

    default:
        return MC_PPR_FAILED;
    }
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_chdir (void *plugin_data, const char *path)
{
    s3_data_t *data = (s3_data_t *) plugin_data;

    if (strcmp (path, "..") == 0)
    {
        switch (data->level)
        {
        case S3_LEVEL_CONNECTIONS:
            return MC_PPR_CLOSE;

        case S3_LEVEL_BUCKETS:
            s3_disconnect (data);
            return MC_PPR_OK;

        case S3_LEVEL_OBJECTS:
            s3_go_up_prefix (data);
            return MC_PPR_OK;

        default:
            return MC_PPR_FAILED;
        }
    }

    switch (data->level)
    {
    case S3_LEVEL_CONNECTIONS:
    {
        s3_connection_t *conn = s3_find_connection (data, path);

        if (conn == NULL)
            return MC_PPR_FAILED;

        return s3_activate_connection (data, conn) ? MC_PPR_OK : MC_PPR_FAILED;
    }

    case S3_LEVEL_BUCKETS:
    {
        mc_pp_dir_entry_t *entry = s3_find_entry (data, path);

        if (entry == NULL || !entry->is_dir)
            return MC_PPR_FAILED;

        s3_enter_bucket (data, path);
        return MC_PPR_OK;
    }

    case S3_LEVEL_OBJECTS:
    {
        mc_pp_dir_entry_t *entry = s3_find_entry (data, path);

        if (entry == NULL || !entry->is_dir)
            return MC_PPR_FAILED;

        s3_enter_prefix (data, path);
        return MC_PPR_OK;
    }

    default:
        return MC_PPR_FAILED;
    }
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_enter (void *plugin_data, const char *name, const struct stat *st)
{
    s3_data_t *data = (s3_data_t *) plugin_data;

    (void) st;

    switch (data->level)
    {
    case S3_LEVEL_CONNECTIONS:
    {
        s3_connection_t *conn = s3_find_connection (data, name);

        if (conn == NULL)
            return MC_PPR_FAILED;

        return s3_activate_connection (data, conn) ? MC_PPR_OK : MC_PPR_FAILED;
    }

    case S3_LEVEL_BUCKETS:
    {
        mc_pp_dir_entry_t *entry = s3_find_entry (data, name);

        if (entry == NULL)
            return MC_PPR_FAILED;

        s3_enter_bucket (data, name);
        return MC_PPR_OK;
    }

    case S3_LEVEL_OBJECTS:
    {
        mc_pp_dir_entry_t *entry = s3_find_entry (data, name);

        if (entry == NULL)
            return MC_PPR_FAILED;

        if (entry->is_dir)
        {
            s3_enter_prefix (data, name);
            return MC_PPR_OK;
        }

        return MC_PPR_NOT_SUPPORTED;
    }

    default:
        return MC_PPR_FAILED;
    }
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_get_local_copy (void *plugin_data, const char *fname, char **local_path)
{
    s3_data_t *data = (s3_data_t *) plugin_data;
    char *key;
    int local_fd;
    GError *error = NULL;

    S3_LOG ("get_local_copy: fname='%s'", fname);

    if (data->level != S3_LEVEL_OBJECTS || data->active_connection == NULL
        || data->current_bucket == NULL)
    {
        S3_LOG ("get_local_copy: precondition failed level=%d", (int) data->level);
        return MC_PPR_FAILED;
    }

    if (data->current_prefix != NULL && data->current_prefix[0] != '\0')
        key = g_strdup_printf ("%s%s", data->current_prefix, fname);
    else
        key = g_strdup (fname);

    S3_LOG ("get_local_copy: key='%s' bucket='%s'", key, data->current_bucket);

    local_fd = g_file_open_tmp ("mc-pp-s3-XXXXXX", local_path, &error);
    if (local_fd == -1)
    {
        S3_LOG ("get_local_copy: failed to create temp file");
        if (error != NULL)
            g_error_free (error);
        g_free (key);
        return MC_PPR_FAILED;
    }

    if (!s3_api_download_object (data->active_connection, data->current_bucket, key, local_fd,
                                    fname))
    {
        S3_LOG ("get_local_copy: download FAILED");
        close (local_fd);
        unlink (*local_path);
        g_free (*local_path);
        *local_path = NULL;
        g_free (key);
        return MC_PPR_FAILED;
    }

    S3_LOG ("get_local_copy: OK -> %s", *local_path);
    close (local_fd);
    g_free (key);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_put_file (void *plugin_data, const char *local_path, const char *dest_name)
{
    s3_data_t *data = (s3_data_t *) plugin_data;
    char *key;
    int local_fd;
    struct stat st;

    if (data->level != S3_LEVEL_OBJECTS || data->active_connection == NULL
        || data->current_bucket == NULL)
        return MC_PPR_FAILED;

    local_fd = open (local_path, O_RDONLY);
    if (local_fd < 0)
        return MC_PPR_FAILED;

    if (fstat (local_fd, &st) != 0)
    {
        close (local_fd);
        return MC_PPR_FAILED;
    }

    if (data->current_prefix != NULL && data->current_prefix[0] != '\0')
        key = g_strdup_printf ("%s%s", data->current_prefix, dest_name);
    else
        key = g_strdup (dest_name);

    if (!s3_api_upload_object (data->active_connection, data->current_bucket, key, local_fd,
                               st.st_size, dest_name))
    {
        close (local_fd);
        g_free (key);
        return MC_PPR_FAILED;
    }

    close (local_fd);
    g_free (key);

    s3_invalidate_current (data);
    s3_reload_entries (data);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_delete_items (void *plugin_data, const char **names, int count)
{
    s3_data_t *data = (s3_data_t *) plugin_data;
    int i;
    gboolean failed = FALSE;

    switch (data->level)
    {
    case S3_LEVEL_CONNECTIONS:
        for (i = 0; i < count; i++)
        {
            guint j;

            for (j = 0; j < data->connections->len; j++)
            {
                const s3_connection_t *conn =
                    (const s3_connection_t *) g_ptr_array_index (data->connections, j);

                if (strcmp (conn->label, names[i]) == 0)
                {
                    g_ptr_array_remove_index (data->connections, j);
                    break;
                }
            }
        }
        s3_save_connections (data->connections_file, data->connections);
        return MC_PPR_OK;

    case S3_LEVEL_BUCKETS:
        for (i = 0; i < count; i++)
        {
            if (!s3_api_delete_bucket (data->active_connection, names[i]))
                failed = TRUE;
        }
        s3_invalidate_current (data);
        s3_reload_entries (data);
        return failed ? MC_PPR_FAILED : MC_PPR_OK;

    case S3_LEVEL_OBJECTS:
        if (data->active_connection == NULL || data->current_bucket == NULL)
            return MC_PPR_FAILED;

        for (i = 0; i < count; i++)
        {
            mc_pp_dir_entry_t *entry = s3_find_entry (data, names[i]);
            char *full_key;

            if (entry == NULL)
                continue;

            if (data->current_prefix != NULL && data->current_prefix[0] != '\0')
                full_key = g_strdup_printf ("%s%s", data->current_prefix, names[i]);
            else
                full_key = g_strdup (names[i]);

            if (entry->is_dir)
            {
                char *prefix_key = g_strdup_printf ("%s/", full_key);

                if (!s3_api_delete_prefix (data->active_connection, data->current_bucket,
                                           prefix_key))
                    failed = TRUE;
                g_free (prefix_key);
            }
            else
            {
                if (!s3_api_delete_object (data->active_connection, data->current_bucket,
                                           full_key))
                    failed = TRUE;
            }

            g_free (full_key);
        }

        s3_invalidate_current (data);
        s3_reload_entries (data);
        return failed ? MC_PPR_FAILED : MC_PPR_OK;

    default:
        return MC_PPR_FAILED;
    }
}

/* --------------------------------------------------------------------------------------------- */

static const char *
s3_get_title (void *plugin_data)
{
    s3_data_t *data = (s3_data_t *) plugin_data;

    g_free (data->title_buf);

    {
        const char *label_raw =
            (data->active_connection != NULL && data->active_connection->label != NULL)
                ? data->active_connection->label
                : NULL;
        char *label = NULL;

        /* strip trailing slashes from label for clean title */
        if (label_raw != NULL)
        {
            label = g_strdup (label_raw);
            {
                size_t len = strlen (label);
                while (len > 0 && label[len - 1] == '/')
                    label[--len] = '\0';
            }
        }

        switch (data->level)
        {
        case S3_LEVEL_CONNECTIONS:
            data->title_buf = g_strdup ("/");
            break;

        case S3_LEVEL_BUCKETS:
            if (label != NULL)
                data->title_buf = g_strdup_printf ("/%s", label);
            else
                data->title_buf = g_strdup ("/");
            break;

        case S3_LEVEL_OBJECTS:
            if (label != NULL && data->current_bucket != NULL)
            {
                const char *prefix =
                    (data->current_prefix != NULL && data->current_prefix[0] != '\0')
                        ? data->current_prefix
                        : "";
                data->title_buf =
                    g_strdup_printf ("/%s/%s/%s", label, data->current_bucket, prefix);
            }
            else
                data->title_buf = g_strdup ("/");
            break;

        default:
            data->title_buf = g_strdup ("/");
            break;
        }

        g_free (label);
    }

    return data->title_buf;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_create_item (void *plugin_data)
{
    s3_data_t *data = (s3_data_t *) plugin_data;

    switch (data->level)
    {
    case S3_LEVEL_CONNECTIONS:
    {
        s3_connection_t *conn;

        conn = g_new0 (s3_connection_t, 1);
        conn->region = g_strdup ("us-east-1");

        if (!s3_show_connection_dialog (conn))
        {
            s3_connection_free (conn);
            return MC_PPR_FAILED;
        }

        if (conn->label == NULL || conn->label[0] == '\0' || conn->access_key == NULL
            || conn->access_key[0] == '\0')
        {
            s3_connection_free (conn);
            return MC_PPR_FAILED;
        }

        g_ptr_array_add (data->connections, conn);
        s3_save_connections (data->connections_file, data->connections);
        return MC_PPR_OK;
    }

    case S3_LEVEL_BUCKETS:
    {
        char *bucket_name = NULL;

        bucket_name = input_dialog (N_ ("Create S3 Bucket"), N_ ("Bucket name:"),
                                    "s3-create-bucket", "", INPUT_COMPLETE_NONE);
        if (bucket_name == NULL || bucket_name[0] == '\0')
        {
            g_free (bucket_name);
            return MC_PPR_FAILED;
        }

        if (!s3_api_create_bucket (data->active_connection, bucket_name))
        {
            g_free (bucket_name);
            return MC_PPR_FAILED;
        }

        g_free (bucket_name);
        s3_invalidate_current (data);
        s3_reload_entries (data);
        return MC_PPR_OK;
    }

    default:
        return MC_PPR_NOT_SUPPORTED;
    }
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_edit_connection (s3_data_t *data)
{
    s3_connection_t *conn;

    if (data->level == S3_LEVEL_CONNECTIONS)
    {
        const GString *current_name;

        current_name = data->host->get_current (data->host);
        if (current_name == NULL || current_name->len == 0)
            return MC_PPR_FAILED;

        conn = s3_find_connection (data, current_name->str);
    }
    else
        conn = data->active_connection;

    if (conn == NULL)
        return MC_PPR_FAILED;

    if (!s3_show_connection_dialog (conn))
        return MC_PPR_FAILED;

    if (conn->label == NULL || conn->label[0] == '\0' || conn->access_key == NULL
        || conn->access_key[0] == '\0')
        return MC_PPR_FAILED;

    s3_save_connections (data->connections_file, data->connections);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
/* Object info helpers for F3 view                                                               */
/* --------------------------------------------------------------------------------------------- */

/* curl header callback: capture specific headers */
typedef struct
{
    char *etag;
    char *content_type;
    curl_off_t content_length;
    char *last_modified;
} s3_head_result_t;

/* --------------------------------------------------------------------------------------------- */

static size_t
s3_header_cb (char *buffer, size_t size, size_t nitems, void *userdata)
{
    s3_head_result_t *hr = (s3_head_result_t *) userdata;
    size_t total = size * nitems;
    char *line;
    char *colon;
    char *value;
    size_t vlen;

    line = g_strndup (buffer, total);

    colon = strchr (line, ':');
    if (colon == NULL)
    {
        g_free (line);
        return total;
    }

    *colon = '\0';
    value = colon + 1;
    while (*value == ' ')
        value++;

    /* trim trailing \r\n */
    vlen = strlen (value);
    while (vlen > 0 && (value[vlen - 1] == '\r' || value[vlen - 1] == '\n'))
        vlen--;
    value[vlen] = '\0';

    if (g_ascii_strcasecmp (line, "ETag") == 0)
    {
        g_free (hr->etag);
        /* strip surrounding quotes */
        if (value[0] == '"' && vlen > 1 && value[vlen - 1] == '"')
        {
            value[vlen - 1] = '\0';
            hr->etag = g_strdup (value + 1);
        }
        else
            hr->etag = g_strdup (value);
    }
    else if (g_ascii_strcasecmp (line, "Content-Type") == 0)
    {
        g_free (hr->content_type);
        hr->content_type = g_strdup (value);
    }
    else if (g_ascii_strcasecmp (line, "Content-Length") == 0)
    {
        hr->content_length = g_ascii_strtoll (value, NULL, 10);
    }
    else if (g_ascii_strcasecmp (line, "Last-Modified") == 0)
    {
        g_free (hr->last_modified);
        hr->last_modified = g_strdup (value);
    }

    g_free (line);
    return total;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
s3_api_head_object (const s3_connection_t *conn, const char *bucket, const char *key,
                    s3_head_result_t *result)
{
    CURL *curl;
    char *url;
    CURLcode res;
    long http_code = 0;

    curl = curl_easy_init ();
    if (curl == NULL)
        return FALSE;

    url = s3_build_url_object (conn, bucket, key);

    curl_easy_setopt (curl, CURLOPT_URL, url);
    curl_easy_setopt (curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt (curl, CURLOPT_HEADERFUNCTION, s3_header_cb);
    curl_easy_setopt (curl, CURLOPT_HEADERDATA, result);
    s3_setup_curl (curl, conn);

    res = curl_easy_perform (curl);
    curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup (curl);
    g_free (url);

    return (res == CURLE_OK && http_code >= 200 && http_code < 300);
}

/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */

static void
s3_format_relative_time (time_t t, char *buf, size_t buf_size)
{
    time_t now;
    long diff;

    now = time (NULL);
    diff = (long) (now - t);

    if (diff < 0)
        g_snprintf (buf, buf_size, "in the future");
    else if (diff < 60)
        g_snprintf (buf, buf_size, "%ld seconds ago", diff);
    else if (diff < 3600)
        g_snprintf (buf, buf_size, "%ld minutes ago", diff / 60);
    else if (diff < 86400)
        g_snprintf (buf, buf_size, "%ld hours ago", diff / 3600);
    else if (diff < 86400 * 30)
        g_snprintf (buf, buf_size, "%ld days ago", diff / 86400);
    else if (diff < 86400 * 365)
        g_snprintf (buf, buf_size, "%ld months ago", diff / (86400 * 30));
    else
        g_snprintf (buf, buf_size, "%ld years ago", diff / (86400 * 365));
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_view_object_info (s3_data_t *data, const char *fname)
{
    s3_head_result_t hr;
    char *key;

    S3_LOG ("view_object_info: fname='%s'", fname);
    GString *info;
    GError *error = NULL;
    char *tmp_path = NULL;
    int fd;
    char size_buf[64];
    char time_buf[64];
    mc_pp_dir_entry_t *entry;

    if (data->active_connection == NULL || data->current_bucket == NULL)
        return MC_PPR_FAILED;

    if (data->current_prefix != NULL && data->current_prefix[0] != '\0')
        key = g_strdup_printf ("%s%s", data->current_prefix, fname);
    else
        key = g_strdup (fname);

    memset (&hr, 0, sizeof (hr));

    if (!s3_api_head_object (data->active_connection, data->current_bucket, key, &hr))
    {
        g_free (key);
        g_free (hr.etag);
        g_free (hr.content_type);
        g_free (hr.last_modified);
        return MC_PPR_FAILED;
    }

    /* format size */
    entry = s3_find_entry (data, fname);
    if (entry != NULL && !entry->is_dir)
        s3_format_size (entry->st.st_size, size_buf, sizeof (size_buf));
    else if (hr.content_length > 0)
        s3_format_size ((off_t) hr.content_length, size_buf, sizeof (size_buf));
    else
        g_strlcpy (size_buf, "unknown", sizeof (size_buf));

    /* format time */
    if (entry != NULL && entry->st.st_mtime > 0)
        s3_format_relative_time (entry->st.st_mtime, time_buf, sizeof (time_buf));
    else
        g_strlcpy (time_buf, hr.last_modified != NULL ? hr.last_modified : "unknown",
                    sizeof (time_buf));

    info = g_string_new ("");
    g_string_append_printf (info, "Object Info\n");
    g_string_append_printf (info, "Name:\n  %s\n", fname);
    g_string_append_printf (info, "Size:\n  %s\n", size_buf);
    g_string_append_printf (info, "Last Modified:\n  %s\n", time_buf);
    g_string_append_printf (info, "ETag:\n  %s\n", hr.etag != NULL ? hr.etag : "N/A");
    g_string_append_printf (info, "Metadata:\n");
    g_string_append_printf (info, "  Content-Type: %s\n",
                            hr.content_type != NULL ? hr.content_type : "N/A");

    g_free (key);
    g_free (hr.etag);
    g_free (hr.content_type);
    g_free (hr.last_modified);

    fd = g_file_open_tmp ("mc-s3-info-XXXXXX", &tmp_path, &error);
    if (fd == -1)
    {
        if (error != NULL)
            g_error_free (error);
        g_string_free (info, TRUE);
        return MC_PPR_FAILED;
    }
    close (fd);

    if (!g_file_set_contents (tmp_path, info->str, (gssize) info->len, NULL))
    {
        unlink (tmp_path);
        g_free (tmp_path);
        g_string_free (info, TRUE);
        return MC_PPR_FAILED;
    }
    g_string_free (info, TRUE);

    {
        vfs_path_t *tmp_vpath;

        tmp_vpath = vfs_path_from_str (tmp_path);
        (void) mcview_viewer (NULL, tmp_vpath, 0, 0, 0);
        vfs_path_free (tmp_vpath, TRUE);
    }

    unlink (tmp_path);
    g_free (tmp_path);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_view_item (void *plugin_data, const char *fname, const struct stat *st, gboolean plain_view)
{
    s3_data_t *data = (s3_data_t *) plugin_data;

    (void) st;

    S3_LOG ("view_item: fname='%s' level=%d plain_view=%d", fname != NULL ? fname : "(null)",
            (int) data->level, (int) plain_view);

    if (fname == NULL)
        return MC_PPR_FAILED;

    switch (data->level)
    {
    case S3_LEVEL_CONNECTIONS:
    {
        /* Show connection details */
        const s3_connection_t *conn;
        GString *info;
        GError *error = NULL;
        char *tmp_path = NULL;
        int fd;

        conn = s3_find_connection (data, fname);
        if (conn == NULL)
            return MC_PPR_FAILED;

        info = g_string_new ("");
        g_string_append_printf (info, "[%s]\n", conn->label);
        g_string_append_printf (info, "access_key=%s\n", conn->access_key);
        if (conn->secret_key != NULL)
            g_string_append (info, "secret_key=***\n");
        g_string_append_printf (info, "region=%s\n",
                                conn->region != NULL ? conn->region : "us-east-1");
        if (conn->endpoint != NULL && conn->endpoint[0] != '\0')
            g_string_append_printf (info, "endpoint=%s\n", conn->endpoint);
        g_string_append_printf (info, "use_path_style=%s\n",
                                conn->use_path_style ? "true" : "false");
        if (conn->timeout > 0)
            g_string_append_printf (info, "timeout=%d\n", conn->timeout);
        if (conn->connect_timeout > 0)
            g_string_append_printf (info, "connect_timeout=%d\n", conn->connect_timeout);

        fd = g_file_open_tmp ("mc-s3-view-XXXXXX", &tmp_path, &error);
        if (fd == -1)
        {
            if (error != NULL)
                g_error_free (error);
            g_string_free (info, TRUE);
            return MC_PPR_FAILED;
        }
        close (fd);

        if (!g_file_set_contents (tmp_path, info->str, (gssize) info->len, NULL))
        {
            unlink (tmp_path);
            g_free (tmp_path);
            g_string_free (info, TRUE);
            return MC_PPR_FAILED;
        }
        g_string_free (info, TRUE);

        {
            vfs_path_t *tmp_vpath;

            tmp_vpath = vfs_path_from_str (tmp_path);
            (void) mcview_viewer (NULL, tmp_vpath, 0, 0, 0);
            vfs_path_free (tmp_vpath, TRUE);
        }

        unlink (tmp_path);
        g_free (tmp_path);
        return MC_PPR_OK;
    }

    case S3_LEVEL_OBJECTS:
        if (plain_view)
        {
            /* Shift-F3: stream object via pipe into mcview (growing buffer) */
            const s3_connection_t *conn = data->active_connection;
            char *key;
            char *url;
            char *escaped_url;
            char *escaped_key;
            char *escaped_secret;
            char *cmd;

            if (conn == NULL || data->current_bucket == NULL)
                return MC_PPR_FAILED;

            if (data->current_prefix != NULL && data->current_prefix[0] != '\0')
                key = g_strdup_printf ("%s%s", data->current_prefix, fname);
            else
                key = g_strdup (fname);

            url = s3_build_url_object (conn, data->current_bucket, key);
            g_free (key);

            escaped_url = g_shell_quote (url);
            escaped_key = g_shell_quote (conn->access_key);
            escaped_secret = g_shell_quote (conn->secret_key != NULL ? conn->secret_key : "");

            cmd = g_strdup_printf ("curl -sf --aws-sigv4 'aws:amz:%s:s3' -u %s:%s %s",
                                    conn->region != NULL ? conn->region : "us-east-1",
                                    escaped_key, escaped_secret, escaped_url);

            g_free (escaped_url);
            g_free (escaped_key);
            g_free (escaped_secret);
            g_free (url);

            S3_LOG ("view_stream: cmd len=%d", (int) strlen (cmd));
            (void) mcview_viewer (cmd, NULL, 0, 0, 0);
            g_free (cmd);
            return MC_PPR_OK;
        }
        /* F3: show object info */
        return s3_view_object_info (data, fname);

    default:
        return MC_PPR_NOT_SUPPORTED;
    }
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
s3_handle_key (void *plugin_data, int key)
{
    s3_data_t *data = (s3_data_t *) plugin_data;

    if (key == CK_Edit || (data->key_edit >= 0 && key == data->key_edit))
        return s3_edit_connection (data);

    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *mc_panel_plugin_register (void);

const mc_panel_plugin_t *
mc_panel_plugin_register (void)
{
    return &s3_plugin;
}

/* --------------------------------------------------------------------------------------------- */
