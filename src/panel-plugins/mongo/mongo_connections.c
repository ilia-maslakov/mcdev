/*
   MongoDB panel plugin -- connection management (create/edit) and settings.

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

#include <string.h>

#include "lib/global.h"
#include "lib/panel-plugin.h"

#include "mongo_internal.h"
#include "mongo_config.h"
#include "mongo_settings.h"
#include "mongo_ui.h"

/*** public functions ****************************************************************************/

/* F7 on the cluster list: prompt for a new connection and persist it to
   mongo.ini as a [Cluster.<name>] section. */
mc_pp_result_t
mongo_create_connection (mongo_data_t *data)
{
    mongo_cluster_t *c;
    char *err = NULL;

    c = g_new0 (mongo_cluster_t, 1);
    if (!mongo_connection_dialog_run (c, TRUE))
    {
        mongo_cluster_free (c);
        return MC_PPR_OK;
    }

    if (mongo_config_find_cluster (data->cfg, c->name) != NULL)
    {
        char *text = g_strdup_printf (_ ("A connection named '%s' already exists."), c->name);
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        mongo_cluster_free (c);
        return MC_PPR_OK;
    }

    if (!mongo_config_save_cluster (c, &err))
    {
        char *text =
            g_strdup_printf (_ ("Cannot save connection: %s"), err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        mongo_cluster_free (c);
        return MC_PPR_FAILED;
    }

    g_ptr_array_add (data->cfg->clusters, c);
    g_free (data->focus_after_up);
    data->focus_after_up = g_strdup (c->name);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/* F4 on the cluster list: edit the selected connection. Edits happen on a
   scratch copy so a duplicate-name or save error leaves the live entry intact;
   on success the [Cluster.*] section is rewritten (renaming drops the old one). */
mc_pp_result_t
mongo_edit_connection (mongo_data_t *data)
{
    const GString *cur;
    mongo_cluster_t *orig;
    mongo_cluster_t edited;
    char *err = NULL;

    cur = (data->host != NULL && data->host->get_current != NULL)
        ? data->host->get_current (data->host)
        : NULL;
    if (cur == NULL || cur->len == 0)
        return MC_PPR_OK;

    orig = (mongo_cluster_t *) mongo_config_find_cluster (data->cfg, cur->str);
    if (orig == NULL)
        return MC_PPR_OK;

    memset (&edited, 0, sizeof (edited));
    edited.name = g_strdup (orig->name);
    edited.uri = g_strdup (orig->uri);
    edited.description = orig->description != NULL ? g_strdup (orig->description) : NULL;
    edited.read_only = orig->read_only;

    if (!mongo_connection_dialog_run (&edited, FALSE))
    {
        g_free (edited.name);
        g_free (edited.uri);
        g_free (edited.description);
        return MC_PPR_OK;
    }

    if (g_strcmp0 (orig->name, edited.name) != 0)
    {
        const mongo_cluster_t *clash = mongo_config_find_cluster (data->cfg, edited.name);
        if (clash != NULL && clash != orig)
        {
            char *text =
                g_strdup_printf (_ ("A connection named '%s' already exists."), edited.name);
            mongo_show_message (data->host, TRUE, text);
            g_free (text);
            g_free (edited.name);
            g_free (edited.uri);
            g_free (edited.description);
            return MC_PPR_OK;
        }
    }

    if (!mongo_config_save_cluster (&edited, &err))
    {
        char *text =
            g_strdup_printf (_ ("Cannot save connection: %s"), err != NULL ? err : "unknown");
        mongo_show_message (data->host, TRUE, text);
        g_free (text);
        g_free (err);
        g_free (edited.name);
        g_free (edited.uri);
        g_free (edited.description);
        return MC_PPR_FAILED;
    }

    if (g_strcmp0 (orig->name, edited.name) != 0)
        (void) mongo_config_delete_cluster (orig->name, NULL);

    g_free (data->focus_after_up);
    data->focus_after_up = g_strdup (edited.name);

    /* Move the edited values into the live entry; ownership transfers. */
    g_free (orig->name);
    orig->name = edited.name;
    g_free (orig->uri);
    orig->uri = edited.uri;
    g_free (orig->description);
    orig->description = edited.description;
    orig->read_only = edited.read_only;

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

/* General plugin options dialog, invoked from the Manage Plugins screen.
   Standalone: loads its own config and saves on OK. */
void
mongo_configure (void)
{
    mongo_config_t *cfg;

    cfg = mongo_config_load ();
    if (cfg == NULL)
        cfg = mongo_config_new_empty ();
    (void) mongo_settings_dialog_run (cfg);
    mongo_config_free (cfg);
}

/* --------------------------------------------------------------------------------------------- */
