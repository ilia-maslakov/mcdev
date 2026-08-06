/*
   SQLite database browser panel plugin.

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

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sqlite3.h>

#include "lib/global.h"
#include "lib/panel-plugin.h"
#include "lib/vfs/vfs.h"
#include "lib/widget.h"

#include "src/viewer/mcviewer.h"

#include "sqlite_json.h"

/*** file scope macro definitions ****************************************************************/

#define SQLITE_ROW_PAGE_SIZE         200
#define SQLITE_BLOB_PREVIEW          256
#define SQLITE_NESTED_JSON_MAX_BYTES (1024 * 1024)

/*** file scope type declarations ****************************************************************/

typedef enum
{
    SQLITE_LEVEL_OBJECTS = 0,
    SQLITE_LEVEL_OBJECT,
    SQLITE_LEVEL_ROWS
} sqlite_level_t;

typedef struct
{
    mc_panel_host_t *host;
    sqlite3 *db;
    char *db_path;
    char *object_name;
    char *title;
    char *focus_after_up;
    sqlite_level_t level;
    gint64 row_count;
    gint64 page_first;
} sqlite_data_t;

/*** forward declarations (file scope functions) *************************************************/

static void *sqlite_open (mc_panel_host_t *host, const char *open_path);
static void sqlite_close (void *plugin_data);
static mc_pp_result_t sqlite_get_items (void *plugin_data, void *list_ptr);
static mc_pp_result_t sqlite_chdir (void *plugin_data, const char *path);
static mc_pp_result_t sqlite_get_local_copy (void *plugin_data, const char *fname,
                                             char **local_path);
static mc_pp_result_t sqlite_view (void *plugin_data, const char *fname, const struct stat *st,
                                   gboolean plain_view);
static const char *sqlite_get_title (void *plugin_data);
static const char *sqlite_get_focus_name (void *plugin_data);
static mc_pp_result_t sqlite_reload (void *plugin_data);
static mc_pp_result_t sqlite_get_help_info (void *plugin_data, const char **filename,
                                            const char **node);
static char *sqlite_get_location (void *plugin_data);

/*** file scope variables ************************************************************************/

static const mc_panel_plugin_t sqlite_plugin = {
    .api_version = MC_PANEL_PLUGIN_API_VERSION,
    .name = "sqlite",
    .display_name = N_ ("SQLite database"),
    /* prefix is enough to activate the plugin.  Locations are added by the
       plugin itself, because a panel title is not a round-trippable path. */
    .proto = NULL,
    .prefix = "sqlite:",
    .flags = MC_PPF_NAVIGATE | MC_PPF_GET_FILES | MC_PPF_CUSTOM_TITLE | MC_PPF_SHOW_IN_MENU
        | MC_PPF_SHOW_IN_DRIVE_MENU | MC_PPF_NO_MOVE,

    .open = sqlite_open,
    .close = sqlite_close,
    .get_items = sqlite_get_items,
    .chdir = sqlite_chdir,
    .get_local_copy = sqlite_get_local_copy,
    .view = sqlite_view,
    .get_title = sqlite_get_title,
    .get_focus_name = sqlite_get_focus_name,
    .reload = sqlite_reload,
    .get_help_info = sqlite_get_help_info,
    .get_location = sqlite_get_location,

    .default_sort_id = "name",
};

/*** file scope functions ************************************************************************/

static void
sqlite_show_error (mc_panel_host_t *host, const char *text)
{
    if (host != NULL && host->message != NULL)
        host->message (host, D_ERROR, _ ("SQLite database"), text);
}

/* --------------------------------------------------------------------------------------------- */

static char *
sqlite_quote_identifier (const char *identifier)
{
    GString *quoted;
    const char *p;

    quoted = g_string_new ("\"");
    for (p = identifier; *p != '\0'; p++)
    {
        if (*p == '"')
            g_string_append_c (quoted, '"');
        g_string_append_c (quoted, *p);
    }
    g_string_append_c (quoted, '"');

    return g_string_free (quoted, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static void
sqlite_update_title (sqlite_data_t *data)
{
    char *base;

    base = g_path_get_basename (data->db_path);
    g_free (data->title);

    if (data->object_name == NULL)
        data->title = base;
    else if (data->level == SQLITE_LEVEL_ROWS)
        data->title = g_strdup_printf ("%s / %s / rows", base, data->object_name);
    else
        data->title = g_strdup_printf ("%s / %s", base, data->object_name);
}

/* --------------------------------------------------------------------------------------------- */

/* Takes ownership of @name. */
static void
sqlite_set_focus_after_up (sqlite_data_t *data, char *name)
{
    g_free (data->focus_after_up);
    data->focus_after_up = name;
}

/* --------------------------------------------------------------------------------------------- */

static void
sqlite_focus_database_file (sqlite_data_t *data)
{
    if (data->host != NULL)
    {
        g_free (data->host->focus_after);
        data->host->focus_after = g_path_get_basename (data->db_path);
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
sqlite_page_name (gint64 first, gint64 last)
{
    return g_strdup_printf ("rows-%012" G_GINT64_FORMAT "-%012" G_GINT64_FORMAT, first, last);
}

/* --------------------------------------------------------------------------------------------- */

static char *
sqlite_row_name (gint64 number)
{
    return g_strdup_printf ("row-%012" G_GINT64_FORMAT ".json", number);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_parse_page_name (const char *name, gint64 *first)
{
    char *end = NULL;
    char *expected;
    gint64 start;
    gint64 last;
    gboolean ok = FALSE;

    if (!g_str_has_prefix (name, "rows-"))
        return FALSE;

    start = g_ascii_strtoll (name + strlen ("rows-"), &end, 10);
    if (end == name + strlen ("rows-") || !g_str_has_prefix (end, "-"))
        return FALSE;

    last = g_ascii_strtoll (end + 1, &end, 10);
    if (*end != '\0' || start < 1 || last < start)
        return FALSE;

    expected = sqlite_page_name (start, last);
    ok = strcmp (name, expected) == 0;
    g_free (expected);

    if (ok)
        *first = start;
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_parse_row_name (const char *name, gint64 *number)
{
    char *end = NULL;
    char *expected;
    gint64 value;
    gboolean ok = FALSE;

    if (!g_str_has_prefix (name, "row-"))
        return FALSE;

    value = g_ascii_strtoll (name + strlen ("row-"), &end, 10);
    if (end == name + strlen ("row-") || strcmp (end, ".json") != 0 || value < 1)
        return FALSE;

    expected = sqlite_row_name (value);
    ok = strcmp (name, expected) == 0;
    g_free (expected);

    if (ok)
        *number = value;
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_open_database (const char *path, sqlite3 **db_out, char **error)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    int rc;

    *db_out = NULL;
    *error = NULL;

    rc = sqlite3_open_v2 (path, &db, SQLITE_OPEN_READONLY, NULL);
    if (rc != SQLITE_OK)
    {
        *error = g_strdup (db != NULL ? sqlite3_errmsg (db) : _ ("Cannot open database."));
        if (db != NULL)
            sqlite3_close (db);
        return FALSE;
    }

    /* sqlite3_open_v2() alone accepts an arbitrary regular file. Preparing a
       schema query makes invalid database files fail before the panel opens. */
    rc = sqlite3_prepare_v2 (db, "SELECT name FROM sqlite_master LIMIT 1", -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        *error = g_strdup (sqlite3_errmsg (db));
        sqlite3_close (db);
        return FALSE;
    }
    sqlite3_finalize (stmt);

    *db_out = db;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_is_database_file (const char *path)
{
    static const unsigned char magic[] = "SQLite format 3";
    unsigned char header[sizeof (magic)];
    ssize_t got;
    int fd;

    fd = open (path, O_RDONLY);
    if (fd == -1)
        return FALSE;

    got = read (fd, header, sizeof (header));
    close (fd);
    return got == (ssize_t) sizeof (header) && memcmp (header, magic, sizeof (magic)) == 0;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_update_row_count (sqlite_data_t *data)
{
    char *quoted;
    char *sql;
    sqlite3_stmt *stmt = NULL;
    int rc;

    if (data->object_name == NULL)
        return TRUE;

    quoted = sqlite_quote_identifier (data->object_name);
    sql = g_strdup_printf ("SELECT count(*) FROM %s", quoted);
    g_free (quoted);

    rc = sqlite3_prepare_v2 (data->db, sql, -1, &stmt, NULL);
    g_free (sql);
    if (rc != SQLITE_OK)
        return FALSE;

    rc = sqlite3_step (stmt);
    if (rc == SQLITE_ROW)
        data->row_count = sqlite3_column_int64 (stmt, 0);
    sqlite3_finalize (stmt);

    return rc == SQLITE_ROW;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_object_exists (sqlite_data_t *data, const char *name)
{
    sqlite3_stmt *stmt = NULL;
    gboolean found = FALSE;
    int rc;

    rc = sqlite3_prepare_v2 (
        data->db,
        "SELECT 1 FROM sqlite_master "
        "WHERE name = ?1 AND type IN ('table', 'view') AND name NOT LIKE 'sqlite_%'",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        return FALSE;

    sqlite3_bind_text (stmt, 1, name, -1, SQLITE_TRANSIENT);
    found = sqlite3_step (stmt) == SQLITE_ROW;
    sqlite3_finalize (stmt);
    return found;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_set_object (sqlite_data_t *data, const char *name)
{
    if (!sqlite_object_exists (data, name))
        return FALSE;

    g_free (data->object_name);
    data->object_name = g_strdup (name);
    data->row_count = 0;
    data->page_first = 0;
    data->level = SQLITE_LEVEL_OBJECT;

    if (!sqlite_update_row_count (data))
    {
        g_free (data->object_name);
        data->object_name = NULL;
        data->level = SQLITE_LEVEL_OBJECTS;
        return FALSE;
    }

    sqlite_update_title (data);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
sqlite_get_location (void *plugin_data)
{
    sqlite_data_t *data = (sqlite_data_t *) plugin_data;
    char *location;

    if (data == NULL || data->db_path == NULL)
        return NULL;

    /* The trailing ":/" is part of the location syntax.  It lets the host
       append a selected child name when copying this location to another
       panel, without confusing that child with part of the database path. */
    location = g_strdup_printf ("sqlite:%s:/", data->db_path);
    if (data->object_name != NULL)
    {
        char *escaped;
        char *next;

        escaped = g_uri_escape_string (data->object_name, NULL, TRUE);
        next = g_strconcat (location, escaped, (char *) NULL);
        g_free (location);
        g_free (escaped);
        location = next;
    }
    if (data->level == SQLITE_LEVEL_ROWS)
    {
        char *page;
        char *next;

        page = sqlite_page_name (
            data->page_first, MIN (data->page_first + SQLITE_ROW_PAGE_SIZE - 1, data->row_count));
        next = g_strdup_printf ("%s/%s", location, page);
        g_free (location);
        g_free (page);
        location = next;
    }

    return location;
}

/* --------------------------------------------------------------------------------------------- */

static void
sqlite_remember_location (sqlite_data_t *data)
{
    char *location;

    if (data->host == NULL || data->host->add_history == NULL)
        return;

    location = sqlite_get_location (data);
    if (location != NULL)
    {
        data->host->add_history (data->host, location);
        g_free (location);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_apply_navigation (sqlite_data_t *data, const char *navigation)
{
    char **parts;
    char *object;
    gboolean ok = FALSE;

    if (navigation == NULL || *navigation == '\0')
        return TRUE;

    parts = g_strsplit (navigation, "/", 3);
    if (parts[0] == NULL || parts[0][0] == '\0' || parts[2] != NULL)
        goto done;

    object = g_uri_unescape_string (parts[0], NULL);
    if (object == NULL || !sqlite_set_object (data, object))
    {
        g_free (object);
        goto done;
    }
    g_free (object);

    if (parts[1] != NULL)
    {
        gint64 first;

        if (!sqlite_parse_page_name (parts[1], &first) || first > data->row_count)
            goto done;
        data->page_first = first;
        data->level = SQLITE_LEVEL_ROWS;
        sqlite_update_title (data);
    }

    ok = TRUE;

done:
    g_strfreev (parts);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_split_location (const char *location, char **db_path, char **navigation)
{
    const char *marker;

    *db_path = NULL;
    *navigation = NULL;

    /* A nested location uses sqlite:/path/to/db:/object/page.  Locate the
       separator by checking candidates: unlike object names, the database
       component must be an existing regular file. */
    marker = location;
    while ((marker = strstr (marker, ":/")) != NULL)
    {
        char *candidate = g_strndup (location, (gsize) (marker - location));

        if (g_file_test (candidate, G_FILE_TEST_IS_REGULAR))
        {
            *db_path = candidate;
            *navigation = g_strdup (marker + 2);
            return TRUE;
        }
        g_free (candidate);
        marker += 2;
    }

    if (!g_file_test (location, G_FILE_TEST_IS_REGULAR))
        return FALSE;

    *db_path = g_strdup (location);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static sqlite_data_t *
sqlite_open_path (mc_panel_host_t *host, const char *path, const char *navigation,
                  gboolean report_errors)
{
    sqlite_data_t *data;
    char *canonical;
    char *error = NULL;

    if (!g_file_test (path, G_FILE_TEST_IS_REGULAR))
    {
        if (report_errors)
            sqlite_show_error (host, _ ("The selected path is not a regular local file."));
        return NULL;
    }
    if (!sqlite_is_database_file (path))
    {
        if (report_errors)
            sqlite_show_error (host, _ ("The selected file is not an SQLite database."));
        return NULL;
    }

    canonical = g_canonicalize_filename (path, NULL);
    data = g_new0 (sqlite_data_t, 1);
    data->host = host;
    data->db_path = canonical;
    data->level = SQLITE_LEVEL_OBJECTS;

    if (!sqlite_open_database (data->db_path, &data->db, &error))
    {
        if (report_errors)
            sqlite_show_error (host, error != NULL ? error : _ ("Cannot open database."));
        g_free (error);
        g_free (data->db_path);
        g_free (data);
        return NULL;
    }

    sqlite_update_title (data);
    if (!sqlite_apply_navigation (data, navigation))
    {
        if (report_errors)
            sqlite_show_error (host, _ ("The requested table or row page no longer exists."));
        sqlite3_close (data->db);
        g_free (data->db_path);
        g_free (data->object_name);
        g_free (data->title);
        g_free (data);
        return NULL;
    }

    sqlite_remember_location (data);
    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void *
sqlite_open (mc_panel_host_t *host, const char *open_path)
{
    if (open_path != NULL && g_str_has_prefix (open_path, sqlite_plugin.prefix))
    {
        const char *path = open_path + strlen (sqlite_plugin.prefix);
        char *db_path = NULL;
        char *navigation = NULL;
        sqlite_data_t *data;

        if (!sqlite_split_location (path, &db_path, &navigation))
        {
            sqlite_show_error (host, _ ("The selected path is not a regular local file."));
            return NULL;
        }

        data = sqlite_open_path (host, db_path, navigation, TRUE);
        g_free (db_path);
        g_free (navigation);
        return data;
    }

    /* Called by Enter on a local file.  A non-SQLite file must fail quietly so
       normal file associations and executable handling still run. */
    if (open_path != NULL && g_file_test (open_path, G_FILE_TEST_IS_REGULAR))
        return sqlite_open_path (host, open_path, NULL, FALSE);

    {
        char *path;

        path = input_expand_dialog (_ ("SQLite database"), _ ("Database file:"),
                                    "sqlite-database-path", "", INPUT_COMPLETE_FILENAMES);
        if (path != NULL && *path != '\0')
        {
            sqlite_data_t *data = sqlite_open_path (host, path, NULL, TRUE);

            g_free (path);
            return data;
        }

        g_free (path);
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
sqlite_close (void *plugin_data)
{
    sqlite_data_t *data = (sqlite_data_t *) plugin_data;

    if (data == NULL)
        return;

    if (data->db != NULL)
        sqlite3_close (data->db);
    g_free (data->db_path);
    g_free (data->object_name);
    g_free (data->title);
    g_free (data->focus_after_up);
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_get_objects (sqlite_data_t *data, void *list_ptr)
{
    sqlite3_stmt *stmt = NULL;
    int rc;

    rc = sqlite3_prepare_v2 (data->db,
                             "SELECT name FROM sqlite_master "
                             "WHERE type IN ('table', 'view') AND name NOT LIKE 'sqlite_%' "
                             "ORDER BY name COLLATE NOCASE",
                             -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        return MC_PPR_FAILED;

    while ((rc = sqlite3_step (stmt)) == SQLITE_ROW)
    {
        const unsigned char *name = sqlite3_column_text (stmt, 0);

        if (name != NULL)
            mc_pp_add_entry (list_ptr, (const char *) name, S_IFDIR | 0755, 0, 0);
    }
    sqlite3_finalize (stmt);

    return rc == SQLITE_DONE ? MC_PPR_OK : MC_PPR_FAILED;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_get_object_items (sqlite_data_t *data, void *list_ptr)
{
    gint64 first;

    mc_pp_add_entry (list_ptr, "schema.sql", S_IFREG | 0444, 0, 0);

    for (first = 1; first <= data->row_count; first += SQLITE_ROW_PAGE_SIZE)
    {
        char *name;
        gint64 last = MIN (first + SQLITE_ROW_PAGE_SIZE - 1, data->row_count);

        name = sqlite_page_name (first, last);
        mc_pp_add_entry (list_ptr, name, S_IFDIR | 0755, last - first + 1, 0);
        g_free (name);
    }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_get_rows (sqlite_data_t *data, void *list_ptr)
{
    gint64 last;
    gint64 row;

    if (data->page_first < 1 || data->page_first > data->row_count)
        return MC_PPR_FAILED;

    last = MIN (data->page_first + SQLITE_ROW_PAGE_SIZE - 1, data->row_count);
    for (row = data->page_first; row <= last; row++)
    {
        char *name = sqlite_row_name (row);

        mc_pp_add_entry (list_ptr, name, S_IFREG | 0444, 0, 0);
        g_free (name);
    }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_get_items (void *plugin_data, void *list_ptr)
{
    sqlite_data_t *data = (sqlite_data_t *) plugin_data;

    if (data == NULL || data->db == NULL)
        return MC_PPR_FAILED;

    switch (data->level)
    {
    case SQLITE_LEVEL_OBJECTS:
        return sqlite_get_objects (data, list_ptr);
    case SQLITE_LEVEL_OBJECT:
        return sqlite_get_object_items (data, list_ptr);
    case SQLITE_LEVEL_ROWS:
        return sqlite_get_rows (data, list_ptr);
    default:
        return MC_PPR_FAILED;
    }
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_chdir (void *plugin_data, const char *path)
{
    sqlite_data_t *data = (sqlite_data_t *) plugin_data;

    if (data == NULL || path == NULL)
        return MC_PPR_FAILED;

    if (strcmp (path, "..") == 0)
    {
        if (data->level == SQLITE_LEVEL_ROWS)
        {
            char *focus = sqlite_page_name (
                data->page_first,
                MIN (data->page_first + SQLITE_ROW_PAGE_SIZE - 1, data->row_count));

            data->level = SQLITE_LEVEL_OBJECT;
            data->page_first = 0;
            sqlite_set_focus_after_up (data, focus);
        }
        else if (data->level == SQLITE_LEVEL_OBJECT)
        {
            char *focus = g_strdup (data->object_name);

            g_free (data->object_name);
            data->object_name = NULL;
            data->row_count = 0;
            data->level = SQLITE_LEVEL_OBJECTS;
            sqlite_set_focus_after_up (data, focus);
        }
        else
        {
            sqlite_focus_database_file (data);
            return MC_PPR_CLOSE;
        }

        sqlite_update_title (data);
        sqlite_remember_location (data);
        return MC_PPR_OK;
    }

    if (data->level == SQLITE_LEVEL_OBJECTS)
    {
        if (!sqlite_set_object (data, path))
            return MC_PPR_FAILED;
    }
    else if (data->level == SQLITE_LEVEL_OBJECT)
    {
        gint64 first;

        if (!sqlite_parse_page_name (path, &first) || first > data->row_count)
            return MC_PPR_FAILED;
        data->page_first = first;
        data->level = SQLITE_LEVEL_ROWS;
        sqlite_update_title (data);
    }
    else
        return MC_PPR_FAILED;

    sqlite_remember_location (data);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static void
sqlite_append_json_string (GString *out, const char *text, gsize length)
{
    gboolean valid_utf8;
    gsize i;

    valid_utf8 = g_utf8_validate (text, (gssize) length, NULL);
    g_string_append_c (out, '"');

    for (i = 0; i < length; i++)
    {
        unsigned char c = (unsigned char) text[i];

        switch (c)
        {
        case '"':
            g_string_append (out, "\\\"");
            break;
        case '\\':
            g_string_append (out, "\\\\");
            break;
        case '\b':
            g_string_append (out, "\\b");
            break;
        case '\f':
            g_string_append (out, "\\f");
            break;
        case '\n':
            g_string_append (out, "\\n");
            break;
        case '\r':
            g_string_append (out, "\\r");
            break;
        case '\t':
            g_string_append (out, "\\t");
            break;
        default:
            if (c < 0x20 || (!valid_utf8 && c >= 0x80))
                g_string_append_printf (out, "\\u%04x", c);
            else
                g_string_append_c (out, (char) c);
            break;
        }
    }

    g_string_append_c (out, '"');
}

/* --------------------------------------------------------------------------------------------- */

static void
sqlite_append_blob (GString *out, const void *blob, int length)
{
    const unsigned char *bytes = (const unsigned char *) blob;
    int preview = MIN (length, SQLITE_BLOB_PREVIEW);
    int i;

    g_string_append (out, "{\"$blob\": \"");
    for (i = 0; i < preview; i++)
        g_string_append_printf (out, "%02x", bytes[i]);
    g_string_append_printf (out, "\", \"bytes\": %d", length);
    if (preview < length)
        g_string_append (out, ", \"truncated\": true");
    g_string_append_c (out, '}');
}

/* --------------------------------------------------------------------------------------------- */

static void
sqlite_append_column_value (GString *out, sqlite3_stmt *stmt, int column, gboolean unfold_json_text,
                            int indent)
{
    switch (sqlite3_column_type (stmt, column))
    {
    case SQLITE_NULL:
        g_string_append (out, "null");
        break;
    case SQLITE_INTEGER:
        g_string_append_printf (out, "%" G_GINT64_FORMAT,
                                (gint64) sqlite3_column_int64 (stmt, column));
        break;
    case SQLITE_FLOAT:
    {
        double value = sqlite3_column_double (stmt, column);

        if (isfinite (value))
        {
            char number[G_ASCII_DTOSTR_BUF_SIZE];

            g_ascii_dtostr (number, sizeof (number), value);
            g_string_append (out, number);
        }
        else
            g_string_append (out, "null");
        break;
    }
    case SQLITE_TEXT:
    {
        const unsigned char *text = sqlite3_column_text (stmt, column);
        gsize length = (gsize) sqlite3_column_bytes (stmt, column);

        if (unfold_json_text && length <= SQLITE_NESTED_JSON_MAX_BYTES)
        {
            const char *first = (const char *) text;
            const char *end = first + length;
            char *pretty;

            while (first < end
                   && (*first == ' ' || *first == '\t' || *first == '\r' || *first == '\n'))
                first++;
            if (first < end && (*first == '{' || *first == '['))
            {
                pretty = sqlite_json_pretty ((const char *) text, length, indent);
                if (pretty != NULL)
                {
                    g_string_append (out, pretty);
                    g_free (pretty);
                    break;
                }
            }
        }

        sqlite_append_json_string (out, (const char *) text, length);
        break;
    }
    case SQLITE_BLOB:
        sqlite_append_blob (out, sqlite3_column_blob (stmt, column),
                            sqlite3_column_bytes (stmt, column));
        break;
    default:
        g_string_append (out, "null");
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static char *
sqlite_render_row (sqlite_data_t *data, gint64 row_number, gboolean unfold_json_text)
{
    char *quoted;
    char *sql;
    sqlite3_stmt *stmt = NULL;
    GString *out;
    int rc;
    int i;

    quoted = sqlite_quote_identifier (data->object_name);
    sql = g_strdup_printf ("SELECT * FROM %s LIMIT 1 OFFSET ?1", quoted);
    g_free (quoted);

    rc = sqlite3_prepare_v2 (data->db, sql, -1, &stmt, NULL);
    g_free (sql);
    if (rc != SQLITE_OK)
        return NULL;

    sqlite3_bind_int64 (stmt, 1, row_number - 1);
    rc = sqlite3_step (stmt);
    if (rc != SQLITE_ROW)
    {
        sqlite3_finalize (stmt);
        return NULL;
    }

    out = g_string_new ("{\n");
    for (i = 0; i < sqlite3_column_count (stmt); i++)
    {
        const char *name = sqlite3_column_name (stmt, i);

        g_string_append (out, "  ");
        sqlite_append_json_string (out, name, strlen (name));
        g_string_append (out, ": ");
        sqlite_append_column_value (out, stmt, i, unfold_json_text, 2);
        g_string_append (out, i + 1 == sqlite3_column_count (stmt) ? "\n" : ",\n");
    }
    g_string_append (out, "}\n");

    sqlite3_finalize (stmt);
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static char *
sqlite_render_schema (sqlite_data_t *data)
{
    sqlite3_stmt *stmt = NULL;
    GString *out;
    int rc;

    rc = sqlite3_prepare_v2 (data->db,
                             "SELECT type, name, sql FROM sqlite_master "
                             "WHERE type IN ('table', 'view', 'index', 'trigger') "
                             "AND (?1 IS NULL OR tbl_name = ?1) "
                             "ORDER BY type, name",
                             -1, &stmt, NULL);
    if (rc != SQLITE_OK)
        return NULL;

    if (data->object_name != NULL)
        sqlite3_bind_text (stmt, 1, data->object_name, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null (stmt, 1);

    out = g_string_new (NULL);
    while ((rc = sqlite3_step (stmt)) == SQLITE_ROW)
    {
        const unsigned char *type = sqlite3_column_text (stmt, 0);
        const unsigned char *name = sqlite3_column_text (stmt, 1);
        const unsigned char *sql = sqlite3_column_text (stmt, 2);

        g_string_append_printf (out, "-- %s %s\n", type != NULL ? (const char *) type : "object",
                                name != NULL ? (const char *) name : "");
        if (sql != NULL)
            g_string_append (out, (const char *) sql);
        g_string_append (out, ";\n\n");
    }
    sqlite3_finalize (stmt);

    if (rc != SQLITE_DONE)
    {
        g_string_free (out, TRUE);
        return NULL;
    }
    return g_string_free (out, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
sqlite_write_all (int fd, const char *text, gsize length)
{
    while (length > 0)
    {
        ssize_t written = write (fd, text, length);

        if (written < 0)
        {
            if (errno == EINTR)
                continue;
            return FALSE;
        }
        text += written;
        length -= (gsize) written;
    }
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_content_to_local_copy (const char *content, const char *fname, char **local_path)
{
    GError *error = NULL;
    int fd;

    fd = g_file_open_tmp ("mc-sqlite-XXXXXX", local_path, &error);
    if (fd == -1)
    {
        g_clear_error (&error);
        return MC_PPR_FAILED;
    }

    if (!sqlite_write_all (fd, content, strlen (content)))
    {
        close (fd);
        unlink (*local_path);
        g_free (*local_path);
        *local_path = NULL;
        return MC_PPR_FAILED;
    }

    close (fd);
    mc_pp_rename_with_ext (local_path, fname);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_get_local_copy (void *plugin_data, const char *fname, char **local_path)
{
    sqlite_data_t *data = (sqlite_data_t *) plugin_data;
    char *content = NULL;
    mc_pp_result_t result;

    if (data == NULL || fname == NULL || local_path == NULL)
        return MC_PPR_FAILED;

    if (data->level == SQLITE_LEVEL_OBJECT && strcmp (fname, "schema.sql") == 0)
        content = sqlite_render_schema (data);
    else if (data->level == SQLITE_LEVEL_ROWS)
    {
        gint64 row_number;

        if (!sqlite_parse_row_name (fname, &row_number) || row_number < data->page_first
            || row_number >= data->page_first + SQLITE_ROW_PAGE_SIZE
            || row_number > data->row_count)
            return MC_PPR_FAILED;
        content = sqlite_render_row (data, row_number, FALSE);
    }

    if (content == NULL)
        return MC_PPR_FAILED;

    result = sqlite_content_to_local_copy (content, fname, local_path);
    g_free (content);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_view (void *plugin_data, const char *fname, const struct stat *st, gboolean plain_view)
{
    sqlite_data_t *data = (sqlite_data_t *) plugin_data;
    char *content;
    char *local_path = NULL;
    gint64 row_number;
    mc_pp_result_t result;

    (void) st;

    /* Shift-F3 is deliberately left to the standard path: it requests the
       original stored text rather than expanding JSON embedded in a column. */
    if (plain_view || data == NULL || fname == NULL || data->level != SQLITE_LEVEL_ROWS
        || !sqlite_parse_row_name (fname, &row_number) || row_number < data->page_first
        || row_number >= data->page_first + SQLITE_ROW_PAGE_SIZE || row_number > data->row_count)
        return MC_PPR_NOT_SUPPORTED;

    content = sqlite_render_row (data, row_number, TRUE);
    if (content == NULL)
        return MC_PPR_FAILED;

    result = sqlite_content_to_local_copy (content, fname, &local_path);
    g_free (content);
    if (result != MC_PPR_OK)
        return result;

    {
        vfs_path_t *local_vpath = vfs_path_from_str (local_path);

        (void) mcview_viewer (NULL, local_vpath, 0, 0, 0);
        vfs_path_free (local_vpath, TRUE);
    }
    unlink (local_path);
    g_free (local_path);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
sqlite_get_title (void *plugin_data)
{
    sqlite_data_t *data = (sqlite_data_t *) plugin_data;

    return data != NULL && data->title != NULL ? data->title : _ ("SQLite database");
}

/* --------------------------------------------------------------------------------------------- */

static const char *
sqlite_get_focus_name (void *plugin_data)
{
    sqlite_data_t *data = (sqlite_data_t *) plugin_data;
    static GString *focus_name = NULL;

    if (data == NULL || data->focus_after_up == NULL)
        return NULL;

    if (focus_name == NULL)
        focus_name = g_string_new (data->focus_after_up);
    else
        g_string_assign (focus_name, data->focus_after_up);

    g_free (data->focus_after_up);
    data->focus_after_up = NULL;
    return focus_name->str;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_reload (void *plugin_data)
{
    sqlite_data_t *data = (sqlite_data_t *) plugin_data;

    if (data == NULL)
        return MC_PPR_FAILED;
    if (data->object_name == NULL)
        return MC_PPR_OK;
    if (!sqlite_update_row_count (data))
        return MC_PPR_FAILED;
    if (data->level == SQLITE_LEVEL_ROWS && data->page_first > data->row_count)
    {
        data->level = SQLITE_LEVEL_OBJECT;
        data->page_first = 0;
        sqlite_update_title (data);
    }
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
sqlite_get_help_info (void *plugin_data, const char **filename, const char **node)
{
    static const char help_path[] = MC_PLUGIN_DIR "/sqlite_panel.hlp";

    (void) plugin_data;
    *filename = help_path;
    *node = "SQLite Plugin";
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *mc_panel_plugin_register (void);

const mc_panel_plugin_t *
mc_panel_plugin_register (void)
{
    return &sqlite_plugin;
}

/* --------------------------------------------------------------------------------------------- */
