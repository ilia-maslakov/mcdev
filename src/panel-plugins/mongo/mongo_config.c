/*
   MongoDB panel plugin -- config loader.

   Copyright (C) 2026
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

#include <string.h>
#include <sys/stat.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/strutil.h"

#include "mongo_config.h"

/*** file scope macro definitions ****************************************************************/

#define MONGO_INI_FILE             "mongo.ini"

#define SECT_GENERAL               "General"
#define SECT_CLUSTER_              "Cluster."

#define DEFAULT_PAGE_SIZE          1000
#define DEFAULT_SRV_SEL_TIMEOUT_MS 5000
#define DEFAULT_CONNECT_TIMEOUT_MS 5000
#define DEFAULT_SOCKET_TIMEOUT_MS  30000
#define DEFAULT_OP_MAX_TIME_MS     15000

#define DEFAULT_BUCKET_LEAF_SIZE   1000
#define DEFAULT_BUCKET_FANOUT      10

/*** public functions ****************************************************************************/

void
mongo_cluster_free (gpointer p)
{
    mongo_cluster_t *c = (mongo_cluster_t *) p;
    if (c == NULL)
        return;
    g_free (c->name);
    g_free (c->uri);
    g_free (c->description);
    g_free (c);
}

/* --------------------------------------------------------------------------------------------- */

char *
mongo_config_path (void)
{
    return g_build_filename (mc_config_get_path (), MONGO_INI_FILE, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_config_save_general (const mongo_config_t *cfg, char **err_out)
{
    char *path;
    mc_config_t *ini;
    GError *gerr = NULL;
    gboolean ok;

    if (err_out != NULL)
        *err_out = NULL;
    if (cfg == NULL)
        return FALSE;

    path = mongo_config_path ();
    /* Load the existing file (read_only = FALSE) so [Cluster.*] sections and
       any unknown keys are preserved when we save. */
    ini = mc_config_init (path, FALSE);
    if (ini == NULL)
    {
        if (err_out != NULL)
            *err_out =
                g_strdup_printf ("Cannot open %s for writing", path != NULL ? path : "mongo.ini");
        g_free (path);
        return FALSE;
    }

    if (cfg->default_cluster != NULL && cfg->default_cluster[0] != '\0')
        mc_config_set_string (ini, SECT_GENERAL, "default_cluster", cfg->default_cluster);
    else
        mc_config_del_key (ini, SECT_GENERAL, "default_cluster");
    mc_config_set_string (ini, SECT_GENERAL, "bucket_strategy",
                          cfg->bucket_strategy == MONGO_BUCKET_NONE ? "none" : "auto");
    mc_config_set_int (ini, SECT_GENERAL, "bucket_leaf_size", cfg->bucket_leaf_size);
    mc_config_set_int (ini, SECT_GENERAL, "bucket_fanout", cfg->bucket_fanout);
    mc_config_set_int (ini, SECT_GENERAL, "server_selection_timeout_ms",
                       cfg->server_selection_timeout_ms);
    mc_config_set_int (ini, SECT_GENERAL, "connect_timeout_ms", cfg->connect_timeout_ms);
    mc_config_set_int (ini, SECT_GENERAL, "socket_timeout_ms", cfg->socket_timeout_ms);
    mc_config_set_int (ini, SECT_GENERAL, "op_max_time_ms", cfg->op_max_time_ms);

    ok = mc_config_save_file (ini, &gerr);
    mc_config_deinit (ini);

    /* mongo.ini may carry [Cluster.*] URIs (often with credentials); keep it
       private even when this saver is what (re)creates the file. */
    if (ok && path != NULL)
        chmod (path, S_IRUSR | S_IWUSR);

    g_free (path);

    if (!ok && err_out != NULL)
        *err_out = g_strdup (gerr != NULL ? gerr->message : "Failed to write mongo.ini");
    if (gerr != NULL)
        g_error_free (gerr);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_config_save_cluster (const mongo_cluster_t *c, char **err_out)
{
    char *path;
    char *sect;
    mc_config_t *ini;
    GError *gerr = NULL;
    gboolean ok;

    if (err_out != NULL)
        *err_out = NULL;
    if (c == NULL || c->name == NULL || c->name[0] == '\0' || c->uri == NULL || c->uri[0] == '\0')
        return FALSE;
    /* The name becomes the [Cluster.<name>] section header. */
    if (strpbrk (c->name, "[]\r\n") != NULL)
    {
        if (err_out != NULL)
            *err_out = g_strdup ("Invalid connection name");
        return FALSE;
    }

    path = mongo_config_path ();
    ini = mc_config_init (path, FALSE);
    if (ini == NULL)
    {
        if (err_out != NULL)
            *err_out =
                g_strdup_printf ("Cannot open %s for writing", path != NULL ? path : "mongo.ini");
        g_free (path);
        return FALSE;
    }

    sect = g_strconcat (SECT_CLUSTER_, c->name, (char *) NULL);
    mc_config_set_string (ini, sect, "uri", c->uri);
    if (c->description != NULL && c->description[0] != '\0')
        mc_config_set_string (ini, sect, "description", c->description);
    mc_config_set_bool (ini, sect, "read_only", c->read_only);
    g_free (sect);

    ok = mc_config_save_file (ini, &gerr);
    mc_config_deinit (ini);

    /* The file stores connection URIs (often with credentials) in plaintext. */
    if (ok && path != NULL)
        chmod (path, S_IRUSR | S_IWUSR);

    g_free (path);

    if (!ok && err_out != NULL)
        *err_out = g_strdup (gerr != NULL ? gerr->message : "Failed to write mongo.ini");
    if (gerr != NULL)
        g_error_free (gerr);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mongo_config_delete_cluster (const char *name, char **err_out)
{
    char *path;
    char *sect;
    mc_config_t *ini;
    GError *gerr = NULL;
    gboolean ok;

    if (err_out != NULL)
        *err_out = NULL;
    if (name == NULL || name[0] == '\0')
        return FALSE;

    path = mongo_config_path ();
    ini = mc_config_init (path, FALSE);
    if (ini == NULL)
    {
        if (err_out != NULL)
            *err_out =
                g_strdup_printf ("Cannot open %s for writing", path != NULL ? path : "mongo.ini");
        g_free (path);
        return FALSE;
    }

    sect = g_strconcat (SECT_CLUSTER_, name, (char *) NULL);
    mc_config_del_group (ini, sect);
    g_free (sect);

    ok = mc_config_save_file (ini, &gerr);
    mc_config_deinit (ini);

    if (ok && path != NULL)
        chmod (path, S_IRUSR | S_IWUSR);

    g_free (path);

    if (!ok && err_out != NULL)
        *err_out = g_strdup (gerr != NULL ? gerr->message : "Failed to write mongo.ini");
    if (gerr != NULL)
        g_error_free (gerr);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

mongo_config_t *
mongo_config_new_empty (void)
{
    mongo_config_t *cfg;

    cfg = g_new0 (mongo_config_t, 1);
    cfg->clusters = g_ptr_array_new_with_free_func (mongo_cluster_free);
    cfg->default_cluster = NULL;
    cfg->page_size = DEFAULT_PAGE_SIZE;
    cfg->server_selection_timeout_ms = DEFAULT_SRV_SEL_TIMEOUT_MS;
    cfg->connect_timeout_ms = DEFAULT_CONNECT_TIMEOUT_MS;
    cfg->socket_timeout_ms = DEFAULT_SOCKET_TIMEOUT_MS;
    cfg->op_max_time_ms = DEFAULT_OP_MAX_TIME_MS;
    cfg->bucket_strategy = MONGO_BUCKET_AUTO;
    cfg->bucket_leaf_size = DEFAULT_BUCKET_LEAF_SIZE;
    cfg->bucket_fanout = DEFAULT_BUCKET_FANOUT;
    return cfg;
}

/* --------------------------------------------------------------------------------------------- */

mongo_config_t *
mongo_config_load (void)
{
    char *path;
    struct stat st;
    mc_config_t *ini;
    mongo_config_t *cfg;
    gchar **groups, **g;

    path = mongo_config_path ();
    if (path == NULL || stat (path, &st) != 0)
    {
        g_free (path);
        return NULL;
    }

    ini = mc_config_init (path, TRUE);
    g_free (path);
    if (ini == NULL)
        return NULL;

    cfg = g_new0 (mongo_config_t, 1);
    cfg->clusters = g_ptr_array_new_with_free_func (mongo_cluster_free);

    cfg->default_cluster = mc_config_get_string (ini, SECT_GENERAL, "default_cluster", "");
    if (cfg->default_cluster != NULL && cfg->default_cluster[0] == '\0')
    {
        g_free (cfg->default_cluster);
        cfg->default_cluster = NULL;
    }
    cfg->page_size = mc_config_get_int (ini, SECT_GENERAL, "page_size", DEFAULT_PAGE_SIZE);
    cfg->server_selection_timeout_ms = mc_config_get_int (
        ini, SECT_GENERAL, "server_selection_timeout_ms", DEFAULT_SRV_SEL_TIMEOUT_MS);
    cfg->connect_timeout_ms =
        mc_config_get_int (ini, SECT_GENERAL, "connect_timeout_ms", DEFAULT_CONNECT_TIMEOUT_MS);
    cfg->socket_timeout_ms =
        mc_config_get_int (ini, SECT_GENERAL, "socket_timeout_ms", DEFAULT_SOCKET_TIMEOUT_MS);
    cfg->op_max_time_ms =
        mc_config_get_int (ini, SECT_GENERAL, "op_max_time_ms", DEFAULT_OP_MAX_TIME_MS);

    cfg->bucket_leaf_size =
        mc_config_get_int (ini, SECT_GENERAL, "bucket_leaf_size", DEFAULT_BUCKET_LEAF_SIZE);
    cfg->bucket_fanout =
        mc_config_get_int (ini, SECT_GENERAL, "bucket_fanout", DEFAULT_BUCKET_FANOUT);
    {
        char *s = mc_config_get_string (ini, SECT_GENERAL, "bucket_strategy", "auto");
        if (g_strcmp0 (s, "none") == 0)
            cfg->bucket_strategy = MONGO_BUCKET_NONE;
        else
            cfg->bucket_strategy = MONGO_BUCKET_AUTO;
        g_free (s);
    }

    groups = mc_config_get_groups (ini, NULL);
    for (g = groups; g != NULL && *g != NULL; g++)
    {
        const char *sect = *g;
        const char *name;
        mongo_cluster_t *c;
        char *uri;

        if (strncmp (sect, SECT_CLUSTER_, sizeof (SECT_CLUSTER_) - 1) != 0)
            continue;
        name = sect + sizeof (SECT_CLUSTER_) - 1;
        if (*name == '\0')
            continue;

        uri = mc_config_get_string (ini, sect, "uri", "");
        if (uri == NULL || uri[0] == '\0')
        {
            g_free (uri);
            continue;
        }

        c = g_new0 (mongo_cluster_t, 1);
        c->name = g_strdup (name);
        c->uri = uri;
        c->description = mc_config_get_string (ini, sect, "description", "");
        if (c->description != NULL && c->description[0] == '\0')
        {
            g_free (c->description);
            c->description = NULL;
        }
        c->read_only = mc_config_get_bool (ini, sect, "read_only", FALSE);

        g_ptr_array_add (cfg->clusters, c);
    }
    g_strfreev (groups);
    mc_config_deinit (ini);

    if (cfg->clusters->len == 0)
    {
        mongo_config_free (cfg);
        return NULL;
    }
    return cfg;
}

/* --------------------------------------------------------------------------------------------- */

void
mongo_config_free (mongo_config_t *cfg)
{
    if (cfg == NULL)
        return;
    g_free (cfg->default_cluster);
    if (cfg->clusters != NULL)
        g_ptr_array_free (cfg->clusters, TRUE);
    g_free (cfg);
}

/* --------------------------------------------------------------------------------------------- */

const mongo_cluster_t *
mongo_config_find_cluster (const mongo_config_t *cfg, const char *name)
{
    guint i;

    if (cfg == NULL || name == NULL)
        return NULL;
    for (i = 0; i < cfg->clusters->len; i++)
    {
        const mongo_cluster_t *c = (const mongo_cluster_t *) g_ptr_array_index (cfg->clusters, i);
        if (g_strcmp0 (c->name, name) == 0)
            return c;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const mongo_cluster_t *
mongo_config_default_cluster (const mongo_config_t *cfg)
{
    if (cfg == NULL || cfg->clusters == NULL || cfg->clusters->len == 0)
        return NULL;
    if (cfg->default_cluster != NULL)
    {
        const mongo_cluster_t *c = mongo_config_find_cluster (cfg, cfg->default_cluster);
        if (c != NULL)
            return c;
    }
    return (const mongo_cluster_t *) g_ptr_array_index (cfg->clusters, 0);
}

/* --------------------------------------------------------------------------------------------- */
