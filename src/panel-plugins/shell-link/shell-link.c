/*
   Shell link connection manager panel plugin.

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

/**
 * \file
 * \brief Source: shell link panel plugin
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/keybind.h"
#include "lib/mcconfig.h"
#include "lib/panel-plugin.h"
#include "lib/tty/key.h"
#include "lib/plugin-prefs.h"
#include "lib/util.h"
#include "lib/widget.h"

#include "src/editor/edit.h"  // edit_file()
#include "src/viewer/mcviewer.h"

/* curses.h, pulled in by tty/key.h, makes refresh() a macro, and
   mc_panel_host_t has a member of that name. */
#undef refresh

#include "lib/vfs/vfs.h"
#include "lib/vfs/utilvfs.h"  // vfs_get_password()

#include "src/execute.h"  // pre_exec(), post_exec()
#include "src/history.h"  // MC_HISTORY_FM_MKDIR
#include "shfs.h"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    char *label;         /* connection name */
    char *host;          /* hostname */
    char *user;          /* username, NULL = current */
    char *password;      /* stored password (optional) */
    char *path;          /* initial path, NULL = / */
    int port;            /* 0 = ssh default */
    gboolean compressed; /* SSH compression (-C) */
} shell_connection_t;

typedef struct
{
    mc_panel_host_t *host;
    GPtrArray *connections;
    char *connections_file;
    int key_edit;
    int key_clone;
    int key_helpers;
    char *title_buf;

    /* The panel shows the helper scripts, which are local, rather than files.
       Independent of whether a host is connected. */
    gboolean helpers_mode;
    char *helpers_host;

    /* NULL until a connection has been entered; until then the panel shows the
       address book. */
    shfs_conn_t *conn;
    const shell_connection_t *active;
    /* Owned here; set when the panel was opened from a URL rather than from the
       address book. data->active points into it. */
    shell_connection_t *transient;
    char *cwd;
} shell_data_t;

/* A stream owns everything it needs to reconnect after the source panel has
   been replaced.  In particular, it must not borrow shell_data_t::conn or
   shell_data_t::active. */
typedef struct
{
    mc_pp_input_stream_t base;
    shell_connection_t *connection;
    char *path;
} shell_input_stream_t;

typedef struct
{
    shfs_conn_t *conn;
    gint64 remaining;
    gboolean finished;
} shell_input_stream_handle_t;

/*** forward declarations (file scope functions) *************************************************/

static void *shell_open (mc_panel_host_t *host, const char *open_path);
static void shell_close (void *plugin_data);
static mc_pp_result_t shell_get_items (void *plugin_data, void *list_ptr);
static mc_pp_result_t shell_enter (void *plugin_data, const char *name, const struct stat *st);
static mc_pp_result_t shell_chdir (void *plugin_data, const char *dir);
static char *shell_remote_path (const shell_data_t *data, const char *name);
static mc_pp_result_t shell_get_local_copy (void *plugin_data, const char *fname,
                                            char **local_path);
static mc_pp_result_t shell_get_input_stream (void *plugin_data, const char *fname,
                                              mc_pp_input_stream_t **stream);
static char *shell_get_location (void *plugin_data);
static gboolean shell_exists (void *plugin_data, const char *name);
static gboolean shell_connect (shell_data_t *data, const shell_connection_t *conn);
static gint64 shell_resume_offset (void *plugin_data, const char *src, const char *dest,
                                   gboolean dest_local);
static mc_pp_result_t shell_resume_copy (void *plugin_data, const char *src, const char *dest,
                                         gboolean dest_local, gint64 offset);
static mc_pp_result_t shell_copy_within (void *plugin_data, const char *fname,
                                         const char *dest_name);
static mc_pp_result_t shell_copy_to_local (void *plugin_data, const char *fname,
                                           const char *local_path);
static mc_pp_result_t shell_put_file (void *plugin_data, const char *local_path,
                                      const char *dest_name);
static mc_pp_result_t shell_save_file (void *plugin_data, const char *local_path,
                                       const char *remote_name);
static void *shell_read_open (void *plugin_data, const char *fname, gint64 offset, gint64 *size);
static gboolean shell_stat_entry (void *plugin_data, const char *name, struct stat *st);
static char *shell_digest_range (void *plugin_data, const char *name, gint64 offset, gint64 length,
                                 const char *algo);
static gssize shell_read_chunk (void *plugin_data, void *handle, void *buf, gsize size);
static gboolean shell_read_close (void *plugin_data, void *handle, char **digest);
static void *shell_write_open (void *plugin_data, const char *fname, gint64 size, gint64 offset);
static gboolean shell_write_chunk (void *plugin_data, void *handle, const void *buf, gsize size);
static gboolean shell_write_close (void *plugin_data, void *handle, char **digest);
static mc_pp_result_t shell_delete_items (void *plugin_data, const char **names, int count);
static const char *shell_get_title (void *plugin_data);
static mc_pp_result_t shell_create_item (void *plugin_data);
static mc_pp_result_t shell_view_item (void *plugin_data, const char *fname, const struct stat *st,
                                       gboolean plain_view);
static mc_pp_result_t shell_get_quick_view (void *plugin_data, const char *fname,
                                            const struct stat *st, char **local_path);
static mc_pp_result_t shell_connection_to_local_copy (const shell_connection_t *conn,
                                                      char **local_path);
static mc_pp_result_t shell_handle_key (void *plugin_data, int key);
static void shell_configure (void);
static shell_connection_t *shell_connection_clone (const shell_connection_t *conn);
static mc_pp_result_t shell_input_stream_open (mc_pp_input_stream_t *stream, void **handle,
                                               GError **error);
static gssize shell_input_stream_read (mc_pp_input_stream_t *stream, void *handle, void *buf,
                                       gsize size, GError **error);
static void shell_input_stream_close (mc_pp_input_stream_t *stream, void *handle);
static void shell_input_stream_free (mc_pp_input_stream_t *stream);

/*** file scope variables ************************************************************************/

#define SHELL_QUICK_VIEW_MAX            (64 * 1024)

#define SHELL_PANEL_CONFIG_FILE         "panels.shell-link.ini"
#define SHELL_PANEL_CONFIG_GROUP        "shell-link-panel"
#define SHELL_PANEL_KEY_EDIT            "hotkey_edit"
#define SHELL_PANEL_KEY_EDIT_DEFAULT    "f4"
#define SHELL_PANEL_KEY_CLONE           "hotkey_clone"
#define SHELL_PANEL_KEY_CLONE_DEFAULT   "shift-f5"
#define SHELL_PANEL_KEY_LOG_LEVEL       "log_level"
#define SHELL_PANEL_KEY_LOG_FILE        "log_file"
#define SHELL_PANEL_KEY_HELPERS         "hotkey_helpers"
#define SHELL_PANEL_KEY_HELPERS_DEFAULT "shift-f7"

/* sentinel value: hotkey is disabled */
#define SHELL_KEY_NONE   0

#define SHELL_DLG_HEIGHT 18
#define SHELL_DLG_WIDTH  52

static const mc_panel_plugin_t shell_plugin = {
    .api_version = MC_PANEL_PLUGIN_API_VERSION,
    .name = "shell-link",
    .display_name = "Shell link (FISH)",
    .proto = "sh",
    .prefix = "sh:",
    .flags = MC_PPF_CUSTOM_TITLE | MC_PPF_CREATE | MC_PPF_DELETE | MC_PPF_SHOW_IN_MENU
        | MC_PPF_SHOW_IN_DRIVE_MENU | MC_PPF_NAVIGATE | MC_PPF_NO_MOVE | MC_PPF_GET_FILES
        | MC_PPF_PUT_FILES,

    .open = shell_open,
    .close = shell_close,
    .get_items = shell_get_items,

    .chdir = shell_chdir,
    .enter = shell_enter,
    .view = shell_view_item,
    .get_local_copy = shell_get_local_copy,
    .copy_to_local = shell_copy_to_local,
    .get_location = shell_get_location,
    .stat_entry = shell_stat_entry,
    .digest_range = shell_digest_range,
    .exists = shell_exists,
    .resume_offset = shell_resume_offset,
    .resume_copy = shell_resume_copy,
    .copy_within = shell_copy_within,
    .put_file = shell_put_file,
    .save_file = shell_save_file,

    .read_open = shell_read_open,
    .read_chunk = shell_read_chunk,
    .read_close = shell_read_close,
    .get_input_stream = shell_get_input_stream,
    .write_open = shell_write_open,
    .write_chunk = shell_write_chunk,
    .write_close = shell_write_close,
    .delete_items = shell_delete_items,
    .get_title = shell_get_title,
    .handle_key = shell_handle_key,
    .create_item = shell_create_item,
    .configure = shell_configure,
    .get_quick_view = shell_get_quick_view,
};

/*** file scope functions ************************************************************************/

static void
shell_connection_free (gpointer p)
{
    shell_connection_t *c = (shell_connection_t *) p;

    g_free (c->label);
    g_free (c->host);
    g_free (c->user);
    g_free (c->password);
    g_free (c->path);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

static shell_connection_t *
shell_connection_clone (const shell_connection_t *conn)
{
    shell_connection_t *copy;

    if (conn == NULL)
        return NULL;

    copy = g_new0 (shell_connection_t, 1);
    copy->label = g_strdup (conn->label);
    copy->host = g_strdup (conn->host);
    copy->user = g_strdup (conn->user);
    copy->password = g_strdup (conn->password);
    copy->path = g_strdup (conn->path);
    copy->port = conn->port;
    copy->compressed = conn->compressed;

    return copy;
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_save_config_defaults (const char *path)
{
    mc_config_t *cfg;

    if (path == NULL || g_file_test (path, G_FILE_TEST_EXISTS))
        return;

    cfg = mc_config_init (path, FALSE);
    if (cfg == NULL)
        return;

    mc_config_set_string (cfg, SHELL_PANEL_CONFIG_GROUP, SHELL_PANEL_KEY_EDIT,
                          SHELL_PANEL_KEY_EDIT_DEFAULT);
    mc_config_set_string (cfg, SHELL_PANEL_CONFIG_GROUP, SHELL_PANEL_KEY_CLONE,
                          SHELL_PANEL_KEY_CLONE_DEFAULT);
    mc_config_set_string (cfg, SHELL_PANEL_CONFIG_GROUP, SHELL_PANEL_KEY_HELPERS,
                          SHELL_PANEL_KEY_HELPERS_DEFAULT);
    mc_config_save_file (cfg, NULL);
    mc_config_deinit (cfg);
}

/* --------------------------------------------------------------------------------------------- */

static int
shell_load_hotkey (const char *key, const char *fallback_text, int fallback_key)
{
    char *config_path;
    int hotkey;

    hotkey = mc_plugin_prefs_load_hotkey (SHELL_PANEL_CONFIG_FILE, SHELL_PANEL_CONFIG_GROUP, key,
                                          fallback_text, fallback_key, NULL);

    config_path = g_build_filename (mc_config_get_path (), SHELL_PANEL_CONFIG_FILE, (char *) NULL);
    shell_save_config_defaults (config_path);
    g_free (config_path);

    return hotkey;
}

/* --------------------------------------------------------------------------------------------- */

static char *
shell_log_default_path (void)
{
    return g_build_filename (mc_config_get_cache_path (), "shell-link.log", (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Read the logging preference and hand it to the library.
 *
 * Done on every panel open, so switching it on takes effect on the next
 * connection instead of the next mc.
 */
static void
shell_log_apply_from_config (void)
{
    mc_config_t *cfg;
    char *path;
    char *file;
    int level;

    path = g_build_filename (mc_config_get_path (), SHELL_PANEL_CONFIG_FILE, (char *) NULL);
    cfg = mc_config_init (path, TRUE);
    g_free (path);

    if (cfg == NULL)
        return;

    level = mc_config_get_int (cfg, SHELL_PANEL_CONFIG_GROUP, SHELL_PANEL_KEY_LOG_LEVEL, 0);
    file = mc_config_get_string (cfg, SHELL_PANEL_CONFIG_GROUP, SHELL_PANEL_KEY_LOG_FILE, "");
    mc_config_deinit (cfg);

    if (level < SHFS_LOG_OFF || level > SHFS_LOG_TRAFFIC)
        level = SHFS_LOG_OFF;

    if (level != SHFS_LOG_OFF && (file == NULL || file[0] == '\0'))
    {
        g_free (file);
        file = shell_log_default_path ();
    }

    /* A file that cannot be opened is reported from the settings dialog, not
       here: this runs on every panel open. */
    shfs_log_set ((shfs_log_level_t) level, file, NULL);

    g_free (file);
}

/* --------------------------------------------------------------------------------------------- */

/** Settings entry point for the Manage Plugins dialog (.configure hook). */
static void
shell_configure (void)
{
    mc_config_t *cfg;
    char *cfg_path;
    char *file = NULL;
    char *entered = NULL;
    int level;
    int ret;

    cfg_path = g_build_filename (mc_config_get_path (), SHELL_PANEL_CONFIG_FILE, (char *) NULL);
    cfg = mc_config_init (cfg_path, FALSE);
    if (cfg == NULL)
    {
        g_free (cfg_path);
        return;
    }

    level = mc_config_get_int (cfg, SHELL_PANEL_CONFIG_GROUP, SHELL_PANEL_KEY_LOG_LEVEL, 0);
    file = mc_config_get_string (cfg, SHELL_PANEL_CONFIG_GROUP, SHELL_PANEL_KEY_LOG_FILE, "");

    if (file == NULL || file[0] == '\0')
    {
        g_free (file);
        file = shell_log_default_path ();
    }

    if (level < SHFS_LOG_OFF || level > SHFS_LOG_TRAFFIC)
        level = SHFS_LOG_OFF;

    {
        const char *levels[] = {
            N_ ("&Off"),
            N_ ("&Errors only"),
            N_ ("&Commands and replies"),
            N_ ("&Full traffic"),
        };

        /* *INDENT-OFF* */
        quick_widget_t quick_widgets[] = {
            QUICK_LABEL (N_ ("Write down the conversation with the remote shell."), NULL),
            QUICK_SEPARATOR (FALSE),
            QUICK_RADIO (4, levels, &level, NULL),
            QUICK_SEPARATOR (TRUE),
            QUICK_LABELED_INPUT (N_ ("Log file:"), input_label_above, file, "shell-link-log-file",
                                 &entered, NULL, FALSE, FALSE, INPUT_COMPLETE_FILENAMES),
            QUICK_LABEL (N_ ("File contents are never written to the log."), NULL),
            QUICK_BUTTONS_OK_CANCEL,
            QUICK_END,
        };
        /* *INDENT-ON* */

        WRect r = { -1, -1, 0, 58 };

        quick_dialog_t qdlg = {
            .rect = r,
            .title = N_ ("Shell link settings"),
            .help = "[shell-link]",
            .widgets = quick_widgets,
            .callback = NULL,
            .mouse_callback = NULL,
        };

        ret = quick_dialog (&qdlg);
    }

    if (ret == B_ENTER)
    {
        GError *error = NULL;
        const char *chosen;

        chosen = (entered != NULL && entered[0] != '\0') ? entered : file;

        if (!shfs_log_set ((shfs_log_level_t) level, chosen, &error))
        {
            message (D_ERROR, MSG_ERROR, "%s",
                     error != NULL ? error->message : _ ("shell: cannot open the log file"));
            g_clear_error (&error);
        }
        else
        {
            mc_config_set_int (cfg, SHELL_PANEL_CONFIG_GROUP, SHELL_PANEL_KEY_LOG_LEVEL, level);
            mc_config_set_string (cfg, SHELL_PANEL_CONFIG_GROUP, SHELL_PANEL_KEY_LOG_FILE, chosen);
            mc_config_save_file (cfg, NULL);
        }
    }

    g_free (entered);
    g_free (file);
    g_free (cfg_path);
    mc_config_deinit (cfg);
}

/* --------------------------------------------------------------------------------------------- */

static char *
get_connections_file_path (void)
{
    return g_build_filename (g_get_user_config_dir (), "mc", "shell-connections.ini",
                             (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

static GPtrArray *
load_connections (const char *filepath)
{
    GPtrArray *arr;
    GKeyFile *kf;
    gchar **groups;
    gsize n_groups, i;

    arr = g_ptr_array_new_with_free_func (shell_connection_free);

    kf = g_key_file_new ();
    if (!g_key_file_load_from_file (kf, filepath, G_KEY_FILE_NONE, NULL))
    {
        g_key_file_free (kf);
        return arr;
    }

    groups = g_key_file_get_groups (kf, &n_groups);
    for (i = 0; i < n_groups; i++)
    {
        shell_connection_t *conn;
        GError *error = NULL;

        conn = g_new0 (shell_connection_t, 1);
        conn->label = g_strdup (groups[i]);
        conn->host = g_key_file_get_string (kf, groups[i], "host", NULL);
        conn->user = g_key_file_get_string (kf, groups[i], "user", NULL);
        {
            char *raw_pw = g_key_file_get_string (kf, groups[i], "password", NULL);

            conn->password = mc_password_decode (raw_pw, "shell-link");
            g_free (raw_pw);
        }
        conn->path = g_key_file_get_string (kf, groups[i], "path", NULL);

        conn->port = g_key_file_get_integer (kf, groups[i], "port", &error);
        if (error != NULL)
        {
            g_error_free (error);
            error = NULL;
            conn->port = 0;
        }
        if (conn->port < 0 || conn->port > 65535)
            conn->port = 0;

        conn->compressed = g_key_file_get_boolean (kf, groups[i], "compressed", &error);
        if (error != NULL)
        {
            g_error_free (error);
            conn->compressed = FALSE;
        }

        if (conn->host == NULL || conn->host[0] == '\0')
        {
            shell_connection_free (conn);
            continue;
        }

        g_ptr_array_add (arr, conn);
    }

    g_strfreev (groups);
    g_key_file_free (kf);
    return arr;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
save_connections (const char *filepath, GPtrArray *connections)
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
        const shell_connection_t *conn =
            (const shell_connection_t *) g_ptr_array_index (connections, i);

        g_key_file_set_string (kf, conn->label, "host", conn->host);

        if (conn->user != NULL)
            g_key_file_set_string (kf, conn->label, "user", conn->user);
        if (conn->password != NULL && conn->password[0] != '\0')
        {
            char *enc = mc_password_encode (conn->password, "shell-link");

            if (enc != NULL)
                g_key_file_set_string (kf, conn->label, "password", enc);
            g_free (enc);
        }
        if (conn->path != NULL)
            g_key_file_set_string (kf, conn->label, "path", conn->path);
        if (conn->port > 0)
            g_key_file_set_integer (kf, conn->label, "port", conn->port);
        g_key_file_set_boolean (kf, conn->label, "compressed", conn->compressed);
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

static const shell_connection_t *
find_connection (const shell_data_t *data, const char *label)
{
    guint i;

    for (i = 0; i < data->connections->len; i++)
    {
        const shell_connection_t *c =
            (const shell_connection_t *) g_ptr_array_index (data->connections, i);

        if (strcmp (c->label, label) == 0)
            return c;
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
shell_label_exists (const shell_data_t *data, const char *label,
                    const shell_connection_t *except_conn)
{
    guint i;

    if (label == NULL || label[0] == '\0')
        return FALSE;

    for (i = 0; i < data->connections->len; i++)
    {
        const shell_connection_t *c =
            (const shell_connection_t *) g_ptr_array_index (data->connections, i);

        if (c == except_conn)
            continue;

        if (strcmp (c->label, label) == 0)
            return TRUE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
show_connection_dialog (shell_connection_t *conn)
{
    char *label = g_strdup (conn->label != NULL ? conn->label : "");
    char *host = g_strdup (conn->host != NULL ? conn->host : "");
    /* Empty, not 22: there is no default of our own to show. An empty field
       means whatever ssh itself resolves for the host. */
    char *port_str = conn->port > 0 ? g_strdup_printf ("%d", conn->port) : g_strdup ("");
    char *user = g_strdup (conn->user != NULL ? conn->user : "");
    char *password = g_strdup (conn->password != NULL ? conn->password : "");
    char *path = g_strdup (conn->path != NULL ? conn->path : "");
    gboolean compressed = conn->compressed;
    int ret;

    /* clang-format off */
    quick_widget_t quick_widgets[] = {
        QUICK_LABELED_INPUT (N_("Connection name:"), input_label_above,
                            label, "shell-conn-label",
                            &label, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_LABELED_INPUT (N_("Host:"), input_label_above,
                            host, "shell-conn-host",
                            &host, NULL, FALSE, FALSE, INPUT_COMPLETE_HOSTNAMES),
        QUICK_LABELED_INPUT (N_("Port:"), input_label_above,
                            port_str, "shell-conn-port",
                            &port_str, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_LABELED_INPUT (N_("User:"), input_label_above,
                            user, "shell-conn-user",
                            &user, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_LABELED_INPUT (N_("Password:"), input_label_above,
                            password, "shell-conn-pass",
                            &password, NULL, TRUE, TRUE, INPUT_COMPLETE_NONE),
        QUICK_LABELED_INPUT (N_("Remote path:"), input_label_above,
                            path, "shell-conn-path",
                            &path, NULL, FALSE, FALSE, INPUT_COMPLETE_NONE),
        QUICK_CHECKBOX (N_("SSH &compression"), &compressed, NULL),
        QUICK_BUTTONS_OK_CANCEL,
        QUICK_END,
    };
    /* clang-format on */

    WRect r = { -1, -1, SHELL_DLG_HEIGHT, SHELL_DLG_WIDTH };

    quick_dialog_t qdlg = {
        .rect = r,
        .title = N_ ("Shell Link Connection"),
        .help = "[Shell Link Plugin]",
        .widgets = quick_widgets,
        .callback = NULL,
        .mouse_callback = NULL,
    };

    ret = quick_dialog (&qdlg);

    if (ret == B_ENTER)
    {
        g_free (conn->label);
        conn->label = label;

        g_free (conn->host);
        conn->host = host;

        /* Anything that is not a port number means "the default"; a typo here
           must not silently send the connection somewhere else. */
        conn->port = 0;
        if (port_str != NULL && port_str[0] != '\0')
        {
            long n;
            char *end = NULL;

            n = strtol (port_str, &end, 10);
            if (end != NULL && *end == '\0' && n > 0 && n <= 65535)
                conn->port = (int) n;
        }

        g_free (conn->user);
        conn->user = (user != NULL && user[0] != '\0') ? user : NULL;
        if (conn->user == NULL)
            g_free (user);

        g_free (conn->password);
        conn->password = (password != NULL && password[0] != '\0') ? password : NULL;
        if (conn->password == NULL)
            g_free (password);

        g_free (conn->path);
        conn->path = (path != NULL && path[0] != '\0') ? path : NULL;
        if (conn->path == NULL)
            g_free (path);

        conn->compressed = compressed;
        g_free (port_str);
        return TRUE;
    }

    g_free (label);
    g_free (host);
    g_free (port_str);
    g_free (user);
    g_free (password);
    g_free (path);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_edit_connection (shell_data_t *data)
{
    const GString *current_name;
    shell_connection_t *conn;
    char *old_label;
    char *old_host;
    char *old_user;
    char *old_password;
    char *old_path;
    int old_port;
    gboolean old_compressed;

    current_name = data->host->get_current (data->host);
    if (current_name == NULL || current_name->len == 0)
        return MC_PPR_FAILED;

    conn = NULL;
    {
        guint i;

        for (i = 0; i < data->connections->len; i++)
        {
            shell_connection_t *c = (shell_connection_t *) g_ptr_array_index (data->connections, i);

            if (strcmp (c->label, current_name->str) == 0)
            {
                conn = c;
                break;
            }
        }
    }

    if (conn == NULL)
        return MC_PPR_FAILED;

    old_label = g_strdup (conn->label);
    old_host = g_strdup (conn->host);
    old_user = g_strdup (conn->user);
    old_password = g_strdup (conn->password);
    old_path = g_strdup (conn->path);
    old_port = conn->port;
    old_compressed = conn->compressed;

    if (!show_connection_dialog (conn))
    {
        g_free (old_label);
        g_free (old_host);
        g_free (old_user);
        g_free (old_password);
        g_free (old_path);
        return MC_PPR_OK;
    }

    if (conn->label == NULL || conn->label[0] == '\0' || conn->host == NULL
        || conn->host[0] == '\0')
    {
        g_free (conn->label);
        g_free (conn->host);
        g_free (conn->user);
        g_free (conn->password);
        g_free (conn->path);
        conn->label = old_label;
        conn->host = old_host;
        conn->user = old_user;
        conn->password = old_password;
        conn->path = old_path;
        conn->port = old_port;
        conn->compressed = old_compressed;
        return MC_PPR_OK;
    }

    if (shell_label_exists (data, conn->label, conn))
    {
        message (D_ERROR, MSG_ERROR, _ ("Connection with this name already exists"));
        g_free (conn->label);
        g_free (conn->host);
        g_free (conn->user);
        g_free (conn->password);
        g_free (conn->path);
        conn->label = old_label;
        conn->host = old_host;
        conn->user = old_user;
        conn->password = old_password;
        conn->path = old_path;
        conn->port = old_port;
        conn->compressed = old_compressed;
        return MC_PPR_OK;
    }

    g_free (old_label);
    g_free (old_host);
    g_free (old_user);
    g_free (old_password);
    g_free (old_path);

    save_connections (data->connections_file, data->connections);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
/* --------------------------------------------------------------------------------------------- */
/*** the panel as an address *********************************************************************/
/* --------------------------------------------------------------------------------------------- */

/**
 * Where this panel is, in a form it can be reopened from.
 *
 * panel_plugin_find_by_path() matches on everything up to the first colon. No
 * password: history outlives the session.
 */
static char *
shell_history_path (const shell_data_t *data)
{
    if (data->conn == NULL || data->active == NULL || data->active->host == NULL)
        return NULL;

    {
        const char *cwd = data->cwd != NULL ? data->cwd : PATH_SEP_STR;
        char *port;
        char *result;

        port = data->active->port > 0 ? g_strdup_printf (":%d", data->active->port) : g_strdup ("");

        if (data->active->user != NULL && data->active->user[0] != '\0')
            result = g_strdup_printf ("sh://%s@%s%s%s", data->active->user, data->active->host,
                                      port, cwd);
        else
            result = g_strdup_printf ("sh://%s%s%s", data->active->host, port, cwd);

        g_free (port);

        return result;
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
shell_get_location (void *plugin_data)
{
    return shell_history_path ((const shell_data_t *) plugin_data);
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_remember_where_we_are (shell_data_t *data)
{
    char *path;

    if (data->host == NULL || data->host->add_history == NULL)
        return;

    path = shell_history_path (data);
    if (path != NULL)
    {
        data->host->add_history (data->host, path);
        g_free (path);
    }
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Take apart sh://[user@]host[/path].
 *
 * The slashes after the colon are optional; any number of them is accepted.
 */
static shell_connection_t *
shell_connection_from_url (const char *url)
{
    shell_connection_t *conn;
    const char *p;
    const char *at;
    const char *slash;

    if (url == NULL)
        return NULL;

    p = strchr (url, ':');
    if (p == NULL)
        return NULL;

    p++;
    while (*p == PATH_SEP)
        p++;

    if (*p == '\0')
        return NULL;

    conn = g_new0 (shell_connection_t, 1);

    slash = strchr (p, PATH_SEP);
    at = memchr (p, '@', slash != NULL ? (gsize) (slash - p) : strlen (p));

    if (at != NULL)
    {
        /* "sh://@host" names no user; the library reads NULL as the local user. */
        if (at != p)
            conn->user = g_strndup (p, (gsize) (at - p));
        p = at + 1;
    }

    if (slash != NULL)
    {
        conn->host = g_strndup (p, (gsize) (slash - p));
        conn->path = g_strdup (slash);
    }
    else
    {
        conn->host = g_strdup (p);
        conn->path = g_strdup (PATH_SEP_STR);
    }

    {
        char *colon;

        /* host:port. Entries written by the old shell filesystem carry a number
           here that was never a port, so those will fail to connect. */
        colon = strrchr (conn->host, ':');
        if (colon != NULL)
        {
            conn->port = (int) g_ascii_strtoll (colon + 1, NULL, 10);
            *colon = '\0';
        }
    }

    if (conn->host[0] == '\0')
    {
        shell_connection_free (conn);
        return NULL;
    }

    conn->label = g_strdup (conn->host);

    return conn;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Open the panel straight at a URL.
 *
 * An address book entry for the same host and user is preferred, so that a
 * stored password still applies; only the directory comes from the URL.
 * Otherwise the connection is built from the URL alone and owned by the panel.
 */
static gboolean
shell_open_url (shell_data_t *data, const char *url)
{
    shell_connection_t *parsed;
    const shell_connection_t *use = NULL;
    guint i;

    parsed = shell_connection_from_url (url);
    if (parsed == NULL)
        return FALSE;

    for (i = 0; i < data->connections->len; i++)
    {
        const shell_connection_t *c =
            (const shell_connection_t *) g_ptr_array_index (data->connections, i);

        if (g_strcmp0 (c->host, parsed->host) == 0 && g_strcmp0 (c->user, parsed->user) == 0)
        {
            use = c;
            break;
        }
    }

    if (use != NULL)
    {
        g_free (parsed->password);
        parsed->password = g_strdup (use->password);
        parsed->compressed = use->compressed;
        g_free (parsed->label);
        parsed->label = g_strdup (use->label);
    }

    data->transient = parsed;

    return shell_connect (data, parsed);
}

/* --------------------------------------------------------------------------------------------- */
/*** helper scripts as a panel ********************************************************************/
/* --------------------------------------------------------------------------------------------- */

static const char *
shell_helper_source_name (shfs_helper_source_t src)
{
    switch (src)
    {
    case SHFS_HELPER_USER:
        return _ ("your copy");
    case SHFS_HELPER_SYSTEM:
        return _ ("installed");
    default:
        return _ ("built in");
    }
}

/* --------------------------------------------------------------------------------------------- */

/** Which host's overrides to show: the connected one, else the selected
    address book entry, else none. */
static char *
shell_helpers_pick_host (shell_data_t *data)
{
    const GString *sel;

    if (data->conn != NULL && data->active != NULL)
        return g_strdup (data->active->host);

    if (data->host != NULL && data->host->get_current != NULL)
    {
        sel = data->host->get_current (data->host);
        if (sel != NULL)
        {
            const shell_connection_t *c = find_connection (data, sel->str);

            if (c != NULL)
                return g_strdup (c->host);
        }
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_helpers_enter (shell_data_t *data)
{
    g_free (data->helpers_host);
    data->helpers_host = shell_helpers_pick_host (data);
    data->helpers_mode = TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_helpers_leave (shell_data_t *data)
{
    data->helpers_mode = FALSE;
    MC_PTR_FREE (data->helpers_host);
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_helpers_list_items (shell_data_t *data, void *list_ptr)
{
    GPtrArray *list;
    guint i;

    list = shfs_helpers_list (data->helpers_host);

    for (i = 0; i < list->len; i++)
    {
        const shfs_helper_t *h = (const shfs_helper_t *) g_ptr_array_index (list, i);
        mode_t mode;

        /* Only an override can be edited or reverted, so only it is shown
           writable. */
        mode = (h->source == SHFS_HELPER_USER) ? (S_IFREG | 0644) : (S_IFREG | 0444);

        mc_pp_add_entry (list_ptr, h->name, mode, (off_t) h->size, time (NULL));
    }

    shfs_helpers_free (list);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/** Write the effective script to a temporary file; the caller unlinks and
    frees the returned path. */
static char *
shell_helpers_to_temp (shell_data_t *data, const char *name, gboolean with_header)
{
    shfs_helper_source_t src;
    char *content;
    char *tmp_path = NULL;
    GString *out;
    gboolean ok;

    content = shfs_helper_content (data->helpers_host, name, &src);
    if (content == NULL)
        return NULL;

    out = g_string_new (NULL);

    if (with_header)
    {
        char *path;

        g_string_append_printf (out, "# %s: %s\n", name, shell_helper_source_name (src));
        if (src == SHFS_HELPER_USER)
            path = shfs_helper_user_path (data->helpers_host, name);
        else if (src == SHFS_HELPER_SYSTEM)
            path = shfs_helper_system_path (name);
        else
            path = NULL;

        if (path != NULL)
        {
            g_string_append_printf (out, "# %s\n", path);
            g_free (path);
        }
        if (data->helpers_host != NULL)
            g_string_append_printf (out, "# host: %s\n", data->helpers_host);
        g_string_append (out, "\n");
    }

    g_string_append (out, content);
    g_free (content);

    ok = mc_pp_write_temp_file ("mc-shell-helper-XXXXXX", out->str, (gssize) out->len, &tmp_path);
    g_string_free (out, TRUE);

    return ok ? tmp_path : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_helpers_view (shell_data_t *data, const char *name)
{
    char *tmp_path;
    vfs_path_t *vpath;

    tmp_path = shell_helpers_to_temp (data, name, TRUE);
    if (tmp_path == NULL)
        return MC_PPR_FAILED;

    vpath = vfs_path_from_str (tmp_path);
    mcview_viewer (NULL, vpath, 0, 0, 0);
    vfs_path_free (vpath, TRUE);

    unlink (tmp_path);
    g_free (tmp_path);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Edit a helper for this host.
 *
 * A built-in or installed script is copied into the per-host directory first,
 * so what gets edited is always the override.
 */
static mc_pp_result_t
shell_helpers_edit (shell_data_t *data, const char *name)
{
    shfs_helper_source_t src;
    char *content;
    char *user_path;
    char *dir;
    edit_arg_t *arg;

    if (data->helpers_host == NULL)
    {
        message (D_ERROR, MSG_ERROR, "%s",
                 _ ("An override belongs to one host. Open a connection, or put the\n"
                    "cursor on an address book entry, and try again."));
        return MC_PPR_FAILED;
    }

    content = shfs_helper_content (data->helpers_host, name, &src);
    if (content == NULL)
        return MC_PPR_FAILED;

    user_path = shfs_helper_user_path (data->helpers_host, name);

    if (src != SHFS_HELPER_USER)
    {
        dir = g_path_get_dirname (user_path);
        if (g_mkdir_with_parents (dir, 0755) != 0)
        {
            message (D_ERROR, MSG_ERROR, _ ("Cannot create %s"), dir);
            g_free (dir);
            g_free (user_path);
            g_free (content);
            return MC_PPR_FAILED;
        }
        g_free (dir);

        if (!g_file_set_contents (user_path, content, -1, NULL))
        {
            message (D_ERROR, MSG_ERROR, _ ("Cannot create %s"), user_path);
            g_free (user_path);
            g_free (content);
            return MC_PPR_FAILED;
        }
    }

    g_free (content);

    arg = edit_arg_new (user_path, 0);
    edit_file (arg);
    edit_arg_free (arg);

    g_free (user_path);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/** Remove the override and go back to whatever was underneath it. */
static mc_pp_result_t
shell_helpers_revert (shell_data_t *data, const char **names, int count)
{
    int i;
    int removed = 0;

    for (i = 0; i < count; i++)
    {
        char *path;

        path = shfs_helper_user_path (data->helpers_host, names[i]);

        if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
            message (D_ERROR, MSG_ERROR, _ ("%s has no override to remove"), names[i]);
        else if (unlink (path) != 0)
            message (D_ERROR, MSG_ERROR, _ ("Cannot remove %s"), path);
        else
            removed++;

        g_free (path);
    }

    return removed == count ? MC_PPR_OK : MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */
/* Plugin callbacks */
/* --------------------------------------------------------------------------------------------- */

static void *
shell_open (mc_panel_host_t *host, const char *open_path)
{
    shell_data_t *data;

    data = g_new0 (shell_data_t, 1);
    data->host = host;
    data->title_buf = NULL;
    data->key_edit =
        shell_load_hotkey (SHELL_PANEL_KEY_EDIT, SHELL_PANEL_KEY_EDIT_DEFAULT, KEY_F (4));
    data->key_clone =
        shell_load_hotkey (SHELL_PANEL_KEY_CLONE, SHELL_PANEL_KEY_CLONE_DEFAULT, KEY_F (15));
    data->key_helpers =
        shell_load_hotkey (SHELL_PANEL_KEY_HELPERS, SHELL_PANEL_KEY_HELPERS_DEFAULT, KEY_F (17));

    shell_log_apply_from_config ();

    data->connections_file = get_connections_file_path ();
    data->connections = load_connections (data->connections_file);

    /* A failure here is not fatal: the panel falls back to the address book. */
    if (open_path != NULL && open_path[0] != '\0')
        shell_open_url (data, open_path);

    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_close (void *plugin_data)
{
    shell_data_t *data = (shell_data_t *) plugin_data;

    if (data->conn != NULL)
        shfs_conn_close (data->conn);
    g_free (data->cwd);

    g_ptr_array_free (data->connections, TRUE);

    if (data->transient != NULL)
        shell_connection_free (data->transient);

    g_free (data->title_buf);
    g_free (data->helpers_host);
    g_free (data->connections_file);
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */
/*** libshfs connect contract ********************************************************************/
/* --------------------------------------------------------------------------------------------- */

static shfs_hostkey_action_t
shell_cb_hostkey (shfs_hostkey_status_t status, const char *host, const char *fingerprint,
                  void *user_data)
{
    char *msg;
    int rc;

    (void) user_data;

    if (status == SHFS_HOSTKEY_MISMATCH)
    {
        msg = g_strdup_printf (_ ("%s\nis found in the list of known hosts but\n"
                                  "KEYS DO NOT MATCH! THIS COULD BE A MITM ATTACK!\n"
                                  "Are you sure you want to add it to the list of known hosts and "
                                  "continue connecting?"),
                               host);
        query_set_sel (2);
        rc = query_dialog (MSG_ERROR, msg, D_ERROR, 3, _ ("&Yes"), _ ("&Ignore"), _ ("&No"));
    }
    else
    {
        msg = g_strdup_printf (
            _ ("The authenticity of host\n%s\ncan't be established!\n"
               "Key fingerprint is\n%s.\n"
               "Do you want to add it to the list of known hosts and continue connecting?"),
            host, fingerprint);
        query_set_sel (2);
        rc = query_dialog (_ ("Warning"), msg, D_NORMAL, 3, _ ("&Yes"), _ ("&Ignore"), _ ("&No"));
    }

    g_free (msg);

    switch (rc)
    {
    case 0:
        return SHFS_HOSTKEY_TRUST_STORE;
    case 1:
        return SHFS_HOSTKEY_TRUST_ONCE;
    default:
        return SHFS_HOSTKEY_REJECT;
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
shell_cb_password (const char *host, const char *user, gboolean retry, void *user_data)
{
    char *p, *passwd;

    (void) host;
    (void) retry;
    (void) user_data;

    p = g_strdup_printf (_ ("shell: Enter password for %s "), user);
    passwd = vfs_get_password (p);
    g_free (p);

    return passwd;
}

/* --------------------------------------------------------------------------------------------- */

static char *
shell_cb_passphrase (const char *keyfile, void *user_data)
{
    char *p, *passwd;

    (void) user_data;

    p = g_strdup_printf (_ ("shell: Enter passphrase for %s "), x_basename (keyfile));
    passwd = vfs_get_password (p);
    g_free (p);

    return passwd;
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_cb_status (const char *text, void *user_data)
{
    (void) user_data;

    vfs_print_message ("%s", text);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
shell_cb_cancelled (void *user_data)
{
    (void) user_data;

    return tty_got_interrupt ();
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Say once that a helper override is older than this build expects.
 *
 * An old helper does not fail; it answers with less than it should (a
 * capability mask without the newer digests, a listing in a shape the parser
 * no longer reads), so the loss shows up as weaker checking or wrong output
 * rather than as an error.
 */
static void
shell_warn_stale_helpers (const shfs_conn_t *conn)
{
    const GPtrArray *stale;
    GString *text;
    guint i;

    stale = shfs_conn_stale_helpers (conn);
    if (stale == NULL || stale->len == 0)
        return;

    text = g_string_new (_ ("These helper scripts were written for an older revision\n"
                            "and may quietly do less than they should:\n\n"));

    for (i = 0; i < stale->len; i++)
    {
        const shfs_helper_t *h = (const shfs_helper_t *) g_ptr_array_index (stale, i);

        g_string_append_printf (text, "%s: %s\n", h->name, h->path);
        g_string_append_printf (text, _ ("    declares revision %d, expected %d\n"), h->version,
                                h->expected_version);
    }

    g_string_append (text, _ ("\nDelete a script to go back to the one shipped with mc."));

    message (D_ERROR, _ ("Outdated helper scripts"), "%s", text->str);

    g_string_free (text, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_cb_terminal_acquire (void *user_data)
{
    (void) user_data;

    pre_exec ();
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_cb_terminal_release (void *user_data)
{
    (void) user_data;

    post_exec ();
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
shell_connect (shell_data_t *data, const shell_connection_t *conn)
{
    shfs_conn_params_t params;
    shfs_connect_cb_t cb;
    GError *error = NULL;

    memset (&params, 0, sizeof (params));
    params.host = conn->host;
    params.user = conn->user;
    params.password = conn->password;
    params.port = conn->port;
    params.compressed = conn->compressed;

    memset (&cb, 0, sizeof (cb));
    cb.hostkey = shell_cb_hostkey;
    cb.password = shell_cb_password;
    cb.passphrase = shell_cb_passphrase;
    cb.status = shell_cb_status;
    cb.cancelled = shell_cb_cancelled;
    cb.terminal_acquire = shell_cb_terminal_acquire;
    cb.terminal_release = shell_cb_terminal_release;
    cb.user_data = data;

    tty_enable_interrupt_key ();
    data->conn = shfs_conn_open (&params, &cb, &error);
    tty_disable_interrupt_key ();

    if (data->conn == NULL)
    {
        message (D_ERROR, MSG_ERROR, "%s",
                 error != NULL ? error->message : _ ("shell: connection failed"));
        g_clear_error (&error);
        return FALSE;
    }

    shell_warn_stale_helpers (data->conn);

    data->active = conn;
    g_free (data->cwd);
    data->cwd =
        g_strdup ((conn->path != NULL && conn->path[0] != '\0') ? conn->path : PATH_SEP_STR);

    shell_remember_where_we_are (data);

    return TRUE;
}
/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_get_items (void *plugin_data, void *list_ptr)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GPtrArray *entries;
    GError *error = NULL;
    guint i;

    if (data->helpers_mode)
        return shell_helpers_list_items (data, list_ptr);

    if (data->conn == NULL)
    {
        for (i = 0; i < data->connections->len; i++)
        {
            const shell_connection_t *conn =
                (const shell_connection_t *) g_ptr_array_index (data->connections, i);

            mc_pp_add_entry (list_ptr, conn->label, S_IFDIR | 0755, 0, time (NULL));
        }

        return MC_PPR_OK;
    }

    tty_enable_interrupt_key ();
    entries = shfs_list_dir (data->conn, data->cwd, &error);
    tty_disable_interrupt_key ();

    if (entries == NULL)
    {
        message (D_ERROR, MSG_ERROR, "%s",
                 error != NULL ? error->message : _ ("shell: cannot read the directory"));
        g_clear_error (&error);
        return MC_PPR_FAILED;
    }

    for (i = 0; i < entries->len; i++)
    {
        const shfs_entry_t *e = (const shfs_entry_t *) g_ptr_array_index (entries, i);

        mc_pp_add_entry (list_ptr, e->name, e->st.st_mode, e->st.st_size, e->st.st_mtime);
    }

    shfs_entries_free (entries);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Move around the remote host, and back out of it.
 *
 * Leaving the top directory closes the connection and returns the panel to the
 * address book.
 */
static mc_pp_result_t
shell_chdir (void *plugin_data, const char *dir)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    char *newdir;

    if (data->helpers_mode)
    {
        /* Nothing to descend into here, so the only move is back out. */
        if (dir != NULL && strcmp (dir, "..") == 0)
        {
            shell_helpers_leave (data);
            return MC_PPR_OK;
        }

        return MC_PPR_NOT_SUPPORTED;
    }

    if (data->conn == NULL)
        return MC_PPR_NOT_SUPPORTED;

    if (dir == NULL || strcmp (dir, ".") == 0)
        return MC_PPR_OK;

    if (strcmp (dir, "..") == 0)
    {
        char *slash;

        if (strcmp (data->cwd, PATH_SEP_STR) == 0)
        {
            // Already at the root of the host: step out of the host itself.
            shfs_conn_close (data->conn);
            data->conn = NULL;
            data->active = NULL;
            MC_PTR_FREE (data->cwd);
            return MC_PPR_OK;
        }

        newdir = g_strdup (data->cwd);
        slash = strrchr (newdir, PATH_SEP);
        if (slash == NULL || slash == newdir)
            newdir[1] = '\0';
        else
            *slash = '\0';
    }
    else if (dir[0] == PATH_SEP)
        newdir = g_strdup (dir);
    else
        newdir = mc_build_filename (data->cwd, dir, (char *) NULL);

    g_free (data->cwd);
    data->cwd = newdir;

    shell_remember_where_we_are (data);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_enter (void *plugin_data, const char *name, const struct stat *st)
{
    shell_data_t *data = (shell_data_t *) plugin_data;

    if (data->helpers_mode)
        return shell_helpers_edit (data, name);

    if (data->conn == NULL)
    {
        const shell_connection_t *conn;

        conn = find_connection (data, name);
        if (conn == NULL)
            return MC_PPR_FAILED;

        return shell_connect (data, conn) ? MC_PPR_OK : MC_PPR_FAILED;
    }

    if (st == NULL || !S_ISDIR (st->st_mode))
        return MC_PPR_NOT_SUPPORTED;

    return shell_chdir (plugin_data, name);
}

/* --------------------------------------------------------------------------------------------- */
/*** file operations on the remote host **********************************************************/
/* --------------------------------------------------------------------------------------------- */

/** Absolute path of @name inside the directory currently shown. */
static char *
shell_remote_path (const shell_data_t *data, const char *name)
{
    if (name[0] == PATH_SEP)
        return g_strdup (name);

    return mc_build_filename (data->cwd, name, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_input_stream_open (mc_pp_input_stream_t *stream, void **handle, GError **error)
{
    shell_input_stream_t *source = (shell_input_stream_t *) stream;
    shell_input_stream_handle_t *stream_handle;
    shfs_conn_params_t params;
    shfs_connect_cb_t cb;
    gint64 remaining;

    if (handle != NULL)
        *handle = NULL;

    if (source == NULL || source->connection == NULL || source->path == NULL || handle == NULL)
    {
        g_set_error (error, SHFS_ERROR, SHFS_ERR_FAILED, "%s",
                     _ ("shell: invalid archive input stream"));
        return MC_PPR_FAILED;
    }

    memset (&params, 0, sizeof (params));
    params.host = source->connection->host;
    params.user = source->connection->user;
    params.password = source->connection->password;
    params.port = source->connection->port;
    params.compressed = source->connection->compressed;

    memset (&cb, 0, sizeof (cb));
    cb.hostkey = shell_cb_hostkey;
    cb.password = shell_cb_password;
    cb.passphrase = shell_cb_passphrase;
    cb.status = shell_cb_status;
    cb.cancelled = shell_cb_cancelled;
    cb.terminal_acquire = shell_cb_terminal_acquire;
    cb.terminal_release = shell_cb_terminal_release;

    stream_handle = g_new0 (shell_input_stream_handle_t, 1);

    tty_enable_interrupt_key ();
    stream_handle->conn = shfs_conn_open (&params, &cb, error);
    tty_disable_interrupt_key ();

    if (stream_handle->conn == NULL)
    {
        g_free (stream_handle);
        return MC_PPR_FAILED;
    }

    shell_warn_stale_helpers (stream_handle->conn);

    if (!shfs_get_begin (stream_handle->conn, source->path, 0, 0, &remaining, error))
    {
        shfs_conn_close (stream_handle->conn);
        g_free (stream_handle);
        return MC_PPR_FAILED;
    }

    stream_handle->remaining = remaining;
    stream_handle->finished = (remaining == 0);
    *handle = stream_handle;

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static gssize
shell_input_stream_read (mc_pp_input_stream_t *stream, void *handle, void *buf, gsize size,
                         GError **error)
{
    shell_input_stream_handle_t *stream_handle = (shell_input_stream_handle_t *) handle;
    gssize bytes;

    (void) stream;

    if (stream_handle == NULL || stream_handle->conn == NULL)
    {
        g_set_error (error, SHFS_ERROR, SHFS_ERR_CLOSED, "%s",
                     _ ("shell: archive input stream is closed"));
        return -1;
    }

    bytes = shfs_get_read (stream_handle->conn, buf, size, error);
    if (bytes > 0)
    {
        stream_handle->remaining -= bytes;
        stream_handle->finished = (stream_handle->remaining == 0);
    }
    else if (bytes == 0)
        stream_handle->finished = TRUE;

    return bytes;
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_input_stream_close (mc_pp_input_stream_t *stream, void *handle)
{
    shell_input_stream_handle_t *stream_handle = (shell_input_stream_handle_t *) handle;

    (void) stream;

    if (stream_handle == NULL)
        return;

    if (stream_handle->conn != NULL)
    {
        if (stream_handle->finished)
        {
            GError *error = NULL;

            (void) shfs_get_finish (stream_handle->conn, NULL, &error);
            g_clear_error (&error);
        }
        shfs_conn_close (stream_handle->conn);
    }

    g_free (stream_handle);
}

/* --------------------------------------------------------------------------------------------- */

static void
shell_input_stream_free (mc_pp_input_stream_t *stream)
{
    shell_input_stream_t *source = (shell_input_stream_t *) stream;

    if (source == NULL)
        return;

    shell_connection_free (source->connection);
    g_free (source->path);
    g_free (source);
}

/* --------------------------------------------------------------------------------------------- */

static const mc_pp_input_stream_ops_t shell_input_stream_ops = {
    .open = shell_input_stream_open,
    .read = shell_input_stream_read,
    .close = shell_input_stream_close,
    .free = shell_input_stream_free,
};

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_get_input_stream (void *plugin_data, const char *fname, mc_pp_input_stream_t **stream)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    shell_input_stream_t *source;

    if (stream != NULL)
        *stream = NULL;

    if (data == NULL || stream == NULL || fname == NULL || data->helpers_mode || data->conn == NULL
        || data->active == NULL)
        return MC_PPR_NOT_SUPPORTED;

    source = g_new0 (shell_input_stream_t, 1);
    source->base.ops = &shell_input_stream_ops;
    source->connection = shell_connection_clone (data->active);
    source->path = shell_remote_path (data, fname);

    if (source->connection == NULL || source->path == NULL)
    {
        shell_input_stream_free (&source->base);
        return MC_PPR_FAILED;
    }

    *stream = &source->base;
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
shell_fetch_to_fd (shell_data_t *data, const char *name, int fd, gint64 offset, gint64 max_bytes,
                   GError **error)
{
    char *rpath;
    char buffer[BUF_8K];
    gint64 remaining = 0;
    gint64 done = 0;
    gboolean ok = TRUE;

    rpath = shell_remote_path (data, name);

    if (!shfs_get_begin (data->conn, rpath, offset, max_bytes, &remaining, error))
    {
        g_free (rpath);
        return FALSE;
    }

    g_free (rpath);

    while (TRUE)
    {
        gssize n;

        tty_enable_interrupt_key ();
        n = shfs_get_read (data->conn, buffer, sizeof (buffer), error);
        tty_disable_interrupt_key ();

        if (n < 0)
            return FALSE;
        if (n == 0)
            break;

        if (write (fd, buffer, n) != n)
        {
            ok = FALSE;
            break;
        }

        done += n;
        vfs_print_message ("%s: %" G_GINT64_FORMAT "/%" G_GINT64_FORMAT, _ ("shell: fetching"),
                           done, remaining);
    }

    if (!shfs_get_finish (data->conn, NULL, error))
        return FALSE;

    if (!ok)
        g_set_error (error, SHFS_ERROR, SHFS_ERR_FAILED, "%s", _ ("shell: local write failed"));

    /* Nothing else repaints the progress line. */
    vfs_print_message ("%s", "");

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_copy_to_local (void *plugin_data, const char *fname, const char *local_path)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    gboolean ok;
    int fd;

    if (data->helpers_mode)
        return MC_PPR_NOT_SUPPORTED;

    if (data->conn == NULL)
        return MC_PPR_NOT_SUPPORTED;

    fd = open (local_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return MC_PPR_FAILED;

    ok = shell_fetch_to_fd (data, fname, fd, 0, 0, &error);
    close (fd);

    if (!ok)
    {
        unlink (local_path);
        message (D_ERROR, MSG_ERROR, "%s",
                 error != NULL ? error->message : _ ("shell: cannot fetch the file"));
        g_clear_error (&error);
        return MC_PPR_FAILED;
    }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_get_local_copy (void *plugin_data, const char *fname, char **local_path)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    vfs_path_t *tmp_vpath = NULL;
    char *tmp_path;
    gboolean ok;
    int fd;

    if (data->helpers_mode)
        return MC_PPR_NOT_SUPPORTED;

    if (data->conn == NULL)
        return MC_PPR_NOT_SUPPORTED;

    fd = mc_mkstemps (&tmp_vpath, "mcshell", NULL);
    if (fd < 0)
        return MC_PPR_FAILED;

    tmp_path = g_strdup (vfs_path_as_str (tmp_vpath));
    vfs_path_free (tmp_vpath, TRUE);

    ok = shell_fetch_to_fd (data, fname, fd, 0, 0, &error);
    close (fd);

    if (!ok)
    {
        unlink (tmp_path);
        g_free (tmp_path);
        if (!mc_pp_quiet_messages ())
            message (D_ERROR, MSG_ERROR, "%s",
                     error != NULL ? error->message : _ ("shell: cannot fetch the file"));
        g_clear_error (&error);
        return MC_PPR_FAILED;
    }

    /* The viewer and the editor decide what a file is by its extension. */
    mc_pp_rename_with_ext (&tmp_path, fname);

    *local_path = tmp_path;

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_put_file (void *plugin_data, const char *local_path, const char *dest_name)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    struct stat st;
    char buffer[BUF_8K];
    char *rpath;
    gint64 done = 0;
    gboolean ok = TRUE;
    int fd;

    if (data->helpers_mode)
        return MC_PPR_NOT_SUPPORTED;

    if (data->conn == NULL)
        return MC_PPR_NOT_SUPPORTED;

    fd = open (local_path, O_RDONLY);
    if (fd < 0)
        return MC_PPR_FAILED;

    if (fstat (fd, &st) < 0)
    {
        close (fd);
        return MC_PPR_FAILED;
    }

    rpath = shell_remote_path (data, dest_name);

    if (!shfs_put_begin (data->conn, rpath, SHFS_WRITE_TRUNCATE, 0, (gint64) st.st_size,
                         (gint64) st.st_size, SHFS_DIGEST_NONE, &error))
    {
        g_free (rpath);
        close (fd);
        message (D_ERROR, MSG_ERROR, "%s",
                 error != NULL ? error->message : _ ("shell: cannot store the file"));
        g_clear_error (&error);
        return MC_PPR_FAILED;
    }

    g_free (rpath);

    while (ok)
    {
        ssize_t n;

        n = read (fd, buffer, sizeof (buffer));
        if (n < 0)
        {
            ok = FALSE;
            break;
        }
        if (n == 0)
            break;

        if (!shfs_put_write (data->conn, buffer, n, &error))
        {
            ok = FALSE;
            break;
        }

        done += n;
        vfs_print_message ("%s: %" G_GINT64_FORMAT "/%" G_GINT64_FORMAT, _ ("shell: storing file"),
                           done, (gint64) st.st_size);
    }

    close (fd);

    if (!shfs_put_finish (data->conn, NULL, &error))
        ok = FALSE;

    vfs_print_message ("%s", "");

    if (!ok)
    {
        message (D_ERROR, MSG_ERROR, "%s",
                 error != NULL ? error->message : _ ("shell: cannot store the file"));
        g_clear_error (&error);
        return MC_PPR_FAILED;
    }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
shell_stat_entry (void *plugin_data, const char *name, struct stat *st)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    char *rpath;
    gboolean ok;

    if (data->conn == NULL || data->helpers_mode)
        return FALSE;

    rpath = shell_remote_path (data, name);
    ok = shfs_file_stat (data->conn, rpath, st, NULL);
    g_free (rpath);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

/** A digest of part of a remote file, formatted the way the named algorithm
    normally prints it, so another plugin's answer can be compared. */
static char *
shell_digest_range (void *plugin_data, const char *name, gint64 offset, gint64 length,
                    const char *algo)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    shfs_digest_algo_t want;
    shfs_digest_t digest;
    char *rpath;
    gboolean ok;

    if (data->conn == NULL || data->helpers_mode || algo == NULL)
        return NULL;

    if (strcmp (algo, "sha256") == 0)
        want = SHFS_DIGEST_SHA256;
    else if (strcmp (algo, "md5") == 0)
        want = SHFS_DIGEST_MD5;
    else if (strcmp (algo, "cksum") == 0)
        want = SHFS_DIGEST_CKSUM;
    else
        return NULL;

    /* cksum is the floor every host has; the other two are only answered when
       the host said it can. */
    if (want != SHFS_DIGEST_CKSUM && (shfs_conn_digest_algos (data->conn) & want) == 0)
        return NULL;

    rpath = shell_remote_path (data, name);
    ok = shfs_checksum_range (data->conn, rpath, offset, length, want, &digest, NULL);
    g_free (rpath);

    return ok ? g_strdup (digest.hex) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
shell_exists (void *plugin_data, const char *name)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    char *rpath;
    gboolean found;

    if (data->conn == NULL || data->helpers_mode)
        return FALSE;

    rpath = shell_remote_path (data, name);
    found = shfs_exists_path (data->conn, rpath, NULL);
    g_free (rpath);

    return found;
}

/* --------------------------------------------------------------------------------------------- */
/*** continuing a transfer that stopped part way *************************************************/
/* --------------------------------------------------------------------------------------------- */

/**
 * How much of a half-written destination is worth keeping: how far the two
 * files agree. Both directions come through here; @dest_local says which side
 * is remote.
 */
static gint64
shell_resume_offset (void *plugin_data, const char *src, const char *dest, gboolean dest_local)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    char *rpath;
    const char *lpath;
    gint64 dest_size;
    gint64 source_size;
    gint64 result;

    if (data->conn == NULL || data->helpers_mode)
        return -1;

    if (dest_local)
    {
        struct stat st;

        /* Out of the host: the remote file is the source. */
        rpath = shell_remote_path (data, src);
        lpath = dest;

        if (stat (dest, &st) != 0)
        {
            g_free (rpath);
            return -1;
        }

        dest_size = (gint64) st.st_size;

        source_size = shfs_file_size (data->conn, rpath, NULL);
        if (source_size < 0)
        {
            g_free (rpath);
            return -1;
        }
    }
    else
    {
        /* Into the host: the local file is the source. */
        rpath = shell_remote_path (data, dest);
        lpath = src;

        struct stat st;

        dest_size = shfs_file_size (data->conn, rpath, NULL);
        if (dest_size < 0 || stat (src, &st) != 0)
        {
            g_free (rpath);
            return -1;
        }

        source_size = (gint64) st.st_size;
    }

    result = shfs_resume_probe (data->conn, rpath, lpath, source_size, dest_size, &error);
    g_clear_error (&error);
    g_free (rpath);

    return result;
}

/* --------------------------------------------------------------------------------------------- */

/** Carry on from @offset. Nothing here shortens either file. */
static mc_pp_result_t
shell_resume_copy (void *plugin_data, const char *src, const char *dest, gboolean dest_local,
                   gint64 offset)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    gboolean ok;

    if (data->conn == NULL || data->helpers_mode)
        return MC_PPR_NOT_SUPPORTED;

    if (dest_local)
    {
        int fd;

        /* No O_TRUNC: the bytes already written are what we continue from. */
        fd = open (dest, O_WRONLY);
        if (fd < 0)
            return MC_PPR_FAILED;

        if (lseek (fd, (off_t) offset, SEEK_SET) == (off_t) -1)
        {
            close (fd);
            return MC_PPR_FAILED;
        }

        ok = shell_fetch_to_fd (data, src, fd, offset, 0, &error);
        close (fd);
    }
    else
    {
        char buffer[BUF_8K];
        struct stat st;
        char *rpath;
        gint64 left;
        int fd;

        fd = open (src, O_RDONLY);
        if (fd < 0)
            return MC_PPR_FAILED;

        if (fstat (fd, &st) != 0 || lseek (fd, (off_t) offset, SEEK_SET) == (off_t) -1)
        {
            close (fd);
            return MC_PPR_FAILED;
        }

        left = (gint64) st.st_size - offset;
        rpath = shell_remote_path (data, dest);

        ok = shfs_put_begin (data->conn, rpath, SHFS_WRITE_AT, offset, left, (gint64) st.st_size,
                             SHFS_DIGEST_NONE, &error);
        g_free (rpath);

        while (ok && left > 0)
        {
            ssize_t n;

            n = read (fd, buffer, (size_t) MIN ((gint64) sizeof (buffer), left));
            if (n <= 0)
            {
                ok = FALSE;
                break;
            }

            ok = shfs_put_write (data->conn, buffer, (gsize) n, &error);
            left -= n;

            vfs_print_message ("%s: %" G_GINT64_FORMAT "/%" G_GINT64_FORMAT,
                               _ ("shell: continuing"), (gint64) st.st_size - left,
                               (gint64) st.st_size);
        }

        close (fd);

        if (!shfs_put_finish (data->conn, NULL, &error))
            ok = FALSE;

        vfs_print_message ("%s", "");
    }

    if (!ok)
    {
        message (D_ERROR, MSG_ERROR, "%s",
                 error != NULL ? error->message : _ ("shell: cannot continue the transfer"));
        g_clear_error (&error);
        return MC_PPR_FAILED;
    }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/** Copy a file to another name on the same host.
 *
 * The protocol has no server side copy, and a connection carries one transfer
 * at a time, so this cannot be a read piped into a write. The file goes down to
 * a temporary local file and back up, in that order. */
static mc_pp_result_t
shell_copy_within (void *plugin_data, const char *fname, const char *dest_name)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    char *tmp_path = NULL;
    mc_pp_result_t r;

    if (data->helpers_mode)
        return MC_PPR_NOT_SUPPORTED;

    if (data->conn == NULL)
        return MC_PPR_NOT_SUPPORTED;

    r = shell_get_local_copy (plugin_data, fname, &tmp_path);
    if (r != MC_PPR_OK)
        return r;

    r = shell_put_file (plugin_data, tmp_path, dest_name);

    unlink (tmp_path);
    g_free (tmp_path);

    return r;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_save_file (void *plugin_data, const char *local_path, const char *remote_name)
{
    return shell_put_file (plugin_data, local_path, remote_name);
}

/* --------------------------------------------------------------------------------------------- */
/*** streaming, for moving a file between two plugin panels **************************************/
/* --------------------------------------------------------------------------------------------- */

static void *
shell_read_open (void *plugin_data, const char *fname, gint64 offset, gint64 *size)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    char *rpath;
    gboolean ok;

    if (data->helpers_mode)
        return NULL;

    if (data->conn == NULL)
        return NULL;

    rpath = shell_remote_path (data, fname);
    ok = shfs_get_begin (data->conn, rpath, offset, 0, size, &error);
    g_free (rpath);

    if (!ok)
    {
        g_clear_error (&error);
        return NULL;
    }

    /* One transfer at a time per connection, so the connection itself is the
       handle. */
    return data->conn;
}

/* --------------------------------------------------------------------------------------------- */

static gssize
shell_read_chunk (void *plugin_data, void *handle, void *buf, gsize size)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    gssize n;

    (void) handle;

    n = shfs_get_read (data->conn, buf, size, &error);
    g_clear_error (&error);

    return n;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
shell_read_close (void *plugin_data, void *handle, char **digest)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    shfs_digest_t d;
    gboolean ok;

    (void) handle;

    ok = shfs_get_finish (data->conn, &d, &error);
    g_clear_error (&error);

    if (digest != NULL)
        *digest = (ok && d.algo != SHFS_DIGEST_NONE) ? g_strdup (d.hex) : NULL;

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static void *
shell_write_open (void *plugin_data, const char *fname, gint64 size, gint64 offset)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    char *rpath;
    gboolean ok;

    if (data->helpers_mode)
        return NULL;

    if (data->conn == NULL)
        return NULL;

    rpath = shell_remote_path (data, fname);
    /* @size is the final file length; this call writes only the part from
       @offset on. Passing @size for both makes the helper wait for bytes that
       never come. */
    ok = shfs_put_begin (data->conn, rpath, offset == 0 ? SHFS_WRITE_TRUNCATE : SHFS_WRITE_AT,
                         offset, size - offset, size, SHFS_DIGEST_NONE, &error);
    g_free (rpath);

    if (!ok)
    {
        g_clear_error (&error);
        return NULL;
    }

    return data->conn;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
shell_write_chunk (void *plugin_data, void *handle, const void *buf, gsize size)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    gboolean ok;

    (void) handle;

    ok = shfs_put_write (data->conn, buf, size, &error);
    g_clear_error (&error);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
shell_write_close (void *plugin_data, void *handle, char **digest)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    GError *error = NULL;
    shfs_digest_t d;
    gboolean ok;

    (void) handle;

    ok = shfs_put_finish (data->conn, &d, &error);
    g_clear_error (&error);

    if (digest != NULL)
        *digest = (ok && d.algo != SHFS_DIGEST_NONE) ? g_strdup (d.hex) : NULL;

    return ok;
}
/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_delete_items (void *plugin_data, const char **names, int count)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    int i;

    if (data->helpers_mode)
        return shell_helpers_revert (data, names, count);

    if (data->conn != NULL)
    {
        gboolean all_ok = TRUE;

        for (i = 0; i < count; i++)
        {
            GError *error = NULL;
            char *rpath;
            gboolean ok;

            rpath = shell_remote_path (data, names[i]);

            /* Files and directories need different commands, and the protocol
               has nothing to ask with: metadata only arrives with a listing.
               So try the file case and fall back. */
            ok = shfs_unlink_path (data->conn, rpath, NULL);
            if (!ok)
                ok = shfs_rmdir_path (data->conn, rpath, &error);

            if (!ok)
            {
                message (D_ERROR, MSG_ERROR, _ ("Cannot delete %s"), names[i]);
                g_clear_error (&error);
                all_ok = FALSE;
            }

            g_free (rpath);
        }

        return all_ok ? MC_PPR_OK : MC_PPR_FAILED;
    }

    for (i = 0; i < count; i++)
    {
        guint j;

        for (j = 0; j < data->connections->len; j++)
        {
            const shell_connection_t *conn =
                (const shell_connection_t *) g_ptr_array_index (data->connections, j);

            if (strcmp (conn->label, names[i]) == 0)
            {
                g_ptr_array_remove_index (data->connections, j);
                break;
            }
        }
    }

    save_connections (data->connections_file, data->connections);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
shell_get_title (void *plugin_data)
{
    shell_data_t *data = (shell_data_t *) plugin_data;

    g_free (data->title_buf);

    if (data->helpers_mode)
        data->title_buf = data->helpers_host != NULL
            ? g_strdup_printf (_ ("helpers for %s"), data->helpers_host)
            : g_strdup (_ ("helpers (no host selected)"));
    else if (data->conn == NULL)
        data->title_buf = g_strdup ("/");
    else
        data->title_buf = g_strdup_printf ("%s:%s", data->active->host, data->cwd);

    return data->title_buf;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_create_item (void *plugin_data)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    shell_connection_t *conn;

    if (data->helpers_mode)
        return MC_PPR_NOT_SUPPORTED;

    if (data->conn != NULL)
    {
        GError *error = NULL;
        char *name, *rpath;
        gboolean ok;

        name = input_dialog (_ ("Create a directory"), _ ("Enter directory name:"),
                             MC_HISTORY_FM_MKDIR, "", INPUT_COMPLETE_NONE);
        if (name == NULL || name[0] == '\0')
        {
            g_free (name);
            return MC_PPR_FAILED;
        }

        rpath = shell_remote_path (data, name);
        ok = shfs_mkdir_path (data->conn, rpath, 0755, &error);
        g_free (rpath);

        if (!ok)
        {
            g_free (name);
            message (D_ERROR, MSG_ERROR, "%s",
                     error != NULL ? error->message : _ ("shell: cannot create the directory"));
            g_clear_error (&error);
            return MC_PPR_FAILED;
        }

        g_free (data->host->focus_after);
        data->host->focus_after = name;

        return MC_PPR_OK;
    }

    conn = g_new0 (shell_connection_t, 1);

    if (!show_connection_dialog (conn))
    {
        shell_connection_free (conn);
        return MC_PPR_SKIPPED;
    }

    if (conn->label == NULL || conn->label[0] == '\0' || conn->host == NULL
        || conn->host[0] == '\0')
    {
        shell_connection_free (conn);
        return MC_PPR_FAILED;
    }

    if (shell_label_exists (data, conn->label, NULL))
    {
        message (D_ERROR, MSG_ERROR, _ ("Connection with this name already exists"));
        shell_connection_free (conn);
        return MC_PPR_FAILED;
    }

    g_ptr_array_add (data->connections, conn);
    save_connections (data->connections_file, data->connections);

    g_free (data->host->focus_after);
    data->host->focus_after = g_strdup (conn->label);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_connection_to_local_copy (const shell_connection_t *conn, char **local_path)
{
    GString *ini;
    char *tmp_path = NULL;
    gboolean ok;

    if (conn == NULL || local_path == NULL)
        return MC_PPR_FAILED;

    ini = g_string_new ("");
    g_string_append_printf (ini, "[%s]\n", conn->label);
    g_string_append_printf (ini, "host=%s\n", conn->host);
    if (conn->port > 0)
        g_string_append_printf (ini, "port=%d\n", conn->port);
    if (conn->user != NULL)
        g_string_append_printf (ini, "user=%s\n", conn->user);
    if (conn->password != NULL)
        g_string_append (ini, "password=***\n");
    if (conn->path != NULL)
        g_string_append_printf (ini, "path=%s\n", conn->path);
    g_string_append_printf (ini, "compressed=%s\n", conn->compressed ? "true" : "false");

    ok = mc_pp_write_temp_file ("mc-shell-view-XXXXXX", ini->str, (gssize) ini->len, &tmp_path);
    g_string_free (ini, TRUE);

    if (!ok)
        return MC_PPR_FAILED;

    *local_path = tmp_path;

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_get_quick_view (void *plugin_data, const char *fname, const struct stat *st,
                      char **local_path)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    const shell_connection_t *conn;

    if (fname == NULL || local_path == NULL)
        return MC_PPR_FAILED;

    if (data->helpers_mode)
    {
        *local_path = shell_helpers_to_temp (data, fname, TRUE);
        return *local_path != NULL ? MC_PPR_OK : MC_PPR_FAILED;
    }

    if (data->conn != NULL)
    {
        GError *error = NULL;
        vfs_path_t *tmp_vpath = NULL;
        char *tmp_path;
        gboolean ok;
        int fd;

        if (st != NULL && !S_ISREG (st->st_mode))
            return MC_PPR_NOT_SUPPORTED;

        fd = mc_mkstemps (&tmp_vpath, "mcshell", NULL);
        if (fd < 0)
            return MC_PPR_FAILED;

        tmp_path = g_strdup (vfs_path_as_str (tmp_vpath));
        vfs_path_free (tmp_vpath, TRUE);

        /* One screenful is the whole point; do not drag the file across. */
        ok = shell_fetch_to_fd (data, fname, fd, 0, SHELL_QUICK_VIEW_MAX, &error);
        close (fd);
        g_clear_error (&error);

        if (!ok)
        {
            unlink (tmp_path);
            g_free (tmp_path);
            return MC_PPR_FAILED;
        }

        /* The viewer and the editor decide what a file is by its extension. */
        mc_pp_rename_with_ext (&tmp_path, fname);
        *local_path = tmp_path;

        return MC_PPR_OK;
    }

    conn = find_connection (data, fname);
    return conn != NULL ? shell_connection_to_local_copy (conn, local_path) : MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_view_item (void *plugin_data, const char *fname, const struct stat *st, gboolean plain_view)
{
    shell_data_t *data = (shell_data_t *) plugin_data;
    const shell_connection_t *conn;
    char *tmp_path = NULL;
    mc_pp_result_t result;

    (void) st;
    (void) plain_view;

    if (fname == NULL)
        return MC_PPR_FAILED;

    if (data->helpers_mode)
        return shell_helpers_view (data, fname);

    /* Connected, so the entry under the cursor is a remote file: hand it back
       and the core fetches a copy and views it the ordinary way. FAILED would
       count as handled. */
    if (data->conn != NULL)
        return MC_PPR_NOT_SUPPORTED;

    conn = find_connection (data, fname);
    result = shell_connection_to_local_copy (conn, &tmp_path);
    if (result != MC_PPR_OK)
        return result;

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
shell_clone_connection (shell_data_t *data)
{
    const GString *current_name;
    const shell_connection_t *src;
    shell_connection_t *conn;
    char *new_label;

    current_name = data->host->get_current (data->host);
    if (current_name == NULL || current_name->len == 0)
        return MC_PPR_OK;

    src = find_connection (data, current_name->str);
    if (src == NULL)
        return MC_PPR_OK;

    new_label = input_dialog (_ ("Clone Connection"), _ ("New connection name:"),
                              "shell-link-clone", src->label, INPUT_COMPLETE_NONE);
    if (new_label == NULL || new_label[0] == '\0')
    {
        g_free (new_label);
        return MC_PPR_OK;
    }

    if (shell_label_exists (data, new_label, NULL))
    {
        message (D_ERROR, MSG_ERROR, _ ("Connection with this name already exists"));
        g_free (new_label);
        return MC_PPR_OK;
    }

    conn = g_new0 (shell_connection_t, 1);
    conn->label = new_label;
    conn->host = g_strdup (src->host);
    conn->user = g_strdup (src->user);
    conn->password = g_strdup (src->password);
    conn->path = g_strdup (src->path);
    conn->port = src->port;
    conn->compressed = src->compressed;

    g_ptr_array_add (data->connections, conn);
    save_connections (data->connections_file, data->connections);

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
shell_handle_key (void *plugin_data, int key)
{
    shell_data_t *data = (shell_data_t *) plugin_data;

    if (data->key_helpers != SHELL_KEY_NONE && key == data->key_helpers)
    {
        if (data->helpers_mode)
            shell_helpers_leave (data);
        else
            shell_helpers_enter (data);

        if (data->host != NULL && data->host->refresh != NULL)
            data->host->refresh (data->host);

        return MC_PPR_OK;
    }

    if (data->helpers_mode)
    {
        if (key == CK_Edit || (data->key_edit != SHELL_KEY_NONE && key == data->key_edit))
        {
            const GString *sel;

            sel = (data->host != NULL && data->host->get_current != NULL)
                ? data->host->get_current (data->host)
                : NULL;

            if (sel == NULL)
                return MC_PPR_FAILED;

            return shell_helpers_edit (data, sel->str);
        }

        /* An override has one place it can live, and Enter puts it there, so
           copy and move mean nothing here. */
        return MC_PPR_NOT_SUPPORTED;
    }

    /* The keys below act on address book entries. Inside a host they mean copy
       and move files, so leave them to the core. */
    if (data->conn != NULL)
        return MC_PPR_NOT_SUPPORTED;

    if (key == CK_EditNew)
        return shell_create_item (data);

    if (key == CK_Edit || (data->key_edit != SHELL_KEY_NONE && key == data->key_edit))
        return shell_edit_connection (data);

    if (key == CK_Copy || key == CK_CopySingle || key == CK_Move || key == CK_MoveSingle
        || (data->key_clone != SHELL_KEY_NONE && key == data->key_clone))
        return shell_clone_connection (data);

    return MC_PPR_NOT_SUPPORTED;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *mc_panel_plugin_register (void);

const mc_panel_plugin_t *
mc_panel_plugin_register (void)
{
    return &shell_plugin;
}

/* --------------------------------------------------------------------------------------------- */
