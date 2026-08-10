/*
   Plugin file-operation associations from magic.ini

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

/** \file magic.c
 *  \brief Matching of plugin file-operation associations
 */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include "lib/global.h"
#include "lib/fileloc.h"
#include "lib/mcconfig.h"
#include "lib/search.h"
#include "lib/strutil.h"
#include "lib/util.h"

#ifdef USE_FILE_CMD
#include "src/setup.h"  // use_file_to_check_type
#endif

#include "magic.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#ifdef USE_FILE_CMD
#ifdef FILE_B
#define MAGIC_FILE_CMD "file -z " FILE_B FILE_S FILE_L
#else
#define MAGIC_FILE_CMD "file -z " FILE_S FILE_L
#endif
#endif

/*** file scope type declarations ****************************************************************/

typedef struct
{
    mc_config_t *ini;
    gchar **groups;
} magic_config_t;

typedef struct
{
    gboolean checked;
    char text[BUF_2K];
} magic_type_info_t;

/*** file scope variables ************************************************************************/

static magic_config_t magic_user_config = { NULL, NULL };
static magic_config_t magic_system_config = { NULL, NULL };
static gboolean magic_configs_loaded = FALSE;
static mc_search_t *magic_action_regex = NULL;

/*** file scope functions ************************************************************************/

static void
magic_config_clear (magic_config_t *config)
{
    if (config == NULL)
        return;

    g_strfreev (config->groups);
    config->groups = NULL;
    mc_config_deinit (config->ini);
    config->ini = NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
magic_config_load (magic_config_t *config, const char *path)
{
    if (config == NULL || path == NULL || !g_file_test (path, G_FILE_TEST_IS_REGULAR))
        return;

    config->ini = mc_config_init (path, TRUE);
    if (config->ini != NULL)
        config->groups = mc_config_get_groups (config->ini, NULL);
}

/* --------------------------------------------------------------------------------------------- */

static void
magic_load (void)
{
    char *path;

    if (magic_configs_loaded)
        return;
    magic_configs_loaded = TRUE;

    path = mc_config_get_full_path (MC_MAGIC_FILE);
    magic_config_load (&magic_user_config, path);
    g_free (path);

    path = mc_build_filename (mc_global.sysconfig_dir, MC_MAGIC_FILE, (char *) NULL);
    magic_config_load (&magic_system_config, path);
    g_free (path);

    if (magic_system_config.ini == NULL)
    {
        path = mc_build_filename (mc_global.share_data_dir, MC_MAGIC_FILE, (char *) NULL);
        magic_config_load (&magic_system_config, path);
        g_free (path);
    }
}

/* --------------------------------------------------------------------------------------------- */

/* The whole grammar of a directive, so that widening it later is a change to
   this line rather than to the code below.  Compiled once and released by
   mc_magic_flush() along with the configs. */
static mc_search_t *
magic_action_search (void)
{
    if (magic_action_regex == NULL)
    {
        magic_action_regex =
            mc_search_new ("^%plugin\\{([A-Za-z0-9_-]+):([A-Za-z0-9_-]+)\\}$", NULL);
        if (magic_action_regex != NULL)
            magic_action_regex->search_type = MC_SEARCH_T_REGEX;
    }

    return magic_action_regex;
}

/* --------------------------------------------------------------------------------------------- */

static char *
magic_fetch_group (const mc_search_t *search, const char *subject, int group)
{
    gint start = -1;
    gint end = -1;

    if (!g_match_info_fetch_pos (search->regex_match_info, group, &start, &end) || start < 0
        || end < start)
        return NULL;

    return g_strndup (subject + start, (gsize) (end - start));
}

/* --------------------------------------------------------------------------------------------- */

static mc_magic_action_state_t
magic_parse_action (const char *value, mc_magic_action_t *result)
{
    mc_search_t *search;
    char *stripped;
    gboolean matched;

    if (value == NULL || result == NULL)
        return MC_MAGIC_ACTION_ERROR;

    search = magic_action_search ();
    if (search == NULL)
        return MC_MAGIC_ACTION_ERROR;

    stripped = g_strstrip (g_strdup (value));
    matched = mc_search_run (search, stripped, 0, strlen (stripped), NULL)
        && search->regex_match_info != NULL;

    if (matched)
    {
        result->plugin_name = magic_fetch_group (search, stripped, 1);
        result->operation_name = magic_fetch_group (search, stripped, 2);
        matched = result->plugin_name != NULL && result->operation_name != NULL;
        if (!matched)
            mc_magic_action_clear (result);
    }

    g_free (stripped);

    return matched ? MC_MAGIC_ACTION_FOUND : MC_MAGIC_ACTION_ERROR;
}

/* --------------------------------------------------------------------------------------------- */

#ifdef USE_FILE_CMD
static gboolean
magic_type_get (const mc_magic_source_t *source, char **local_copy, magic_type_info_t *type)
{
    const char *path;
    char *quoted, *command;
    FILE *file;

    if (type->checked)
        return type->text[0] != '\0';
    type->checked = TRUE;

    path = source->local_path;
    if (path == NULL && *local_copy != NULL)
        path = *local_copy;
    if (path == NULL && source->get_local_copy != NULL)
    {
        if (source->get_local_copy (source->data, local_copy) != MC_PPR_OK || *local_copy == NULL)
            return FALSE;
        path = *local_copy;
    }
    if (path == NULL)
        return FALSE;

    quoted = name_quote (path, FALSE);
    if (quoted == NULL)
        return FALSE;

    command = g_strconcat (MAGIC_FILE_CMD, quoted, " 2>/dev/null", (char *) NULL);
    g_free (quoted);
    file = popen (command, "r");
    g_free (command);
    if (file == NULL)
        return FALSE;

    if (fgets (type->text, sizeof (type->text), file) == NULL)
        type->text[0] = '\0';
    (void) pclose (file);
    type->text[sizeof (type->text) - 1] = '\0';
    g_strchomp (type->text);

    /* file(1) without -b prefixes its result with "path: ". */
    if (g_str_has_prefix (type->text, path) && type->text[strlen (path)] == ':')
    {
        char *description = type->text + strlen (path) + 1;

        while (whitespace (*description))
            description++;
        memmove (type->text, description, strlen (description) + 1);
    }

    return type->text[0] != '\0';
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
magic_type_matches (const char *pattern, gboolean ignore_case, const mc_magic_source_t *source,
                    char **local_copy, magic_type_info_t *type)
{
    mc_search_t *search;
    gboolean matched = FALSE;

    if (!magic_type_get (source, local_copy, type))
        return FALSE;

    search = mc_search_new (pattern, NULL);
    if (search != NULL)
    {
        search->search_type = MC_SEARCH_T_REGEX;
        search->is_case_sensitive = !ignore_case;
        matched = mc_search_run (search, type->text, 0, strlen (type->text), NULL);
        mc_search_free (search);
    }

    return matched;
}
#endif

/* --------------------------------------------------------------------------------------------- */

static gboolean
magic_group_matches (mc_config_t *ini, const char *group, const mc_magic_source_t *source,
                     char **local_copy, magic_type_info_t *type)
{
    const char *filename;
    size_t filename_len;
    gchar *pattern;
    gboolean type_used = FALSE;

    filename = x_basename (source->display_name);
    filename_len = strlen (filename);

#ifdef USE_FILE_CMD
    if (use_file_to_check_type)
    {
        pattern = mc_config_get_string_raw (ini, group, "Type", NULL);
        if (pattern != NULL)
        {
            gboolean ignore_case = mc_config_get_bool (ini, group, "TypeIgnoreCase", FALSE);
            gboolean type_found;

            type_found = magic_type_matches (pattern, ignore_case, source, local_copy, type);
            type_used = TRUE;
            g_free (pattern);
            if (!type_found)
                return FALSE;
        }
    }
#else
    (void) local_copy;
    (void) type;
#endif

    pattern = mc_config_get_string_raw (ini, group, "Regex", NULL);
    if (pattern != NULL)
    {
        mc_search_t *search;
        gboolean ignore_case = mc_config_get_bool (ini, group, "RegexIgnoreCase", FALSE);
        gboolean matched = FALSE;

        search = mc_search_new (pattern, NULL);
        g_free (pattern);
        if (search != NULL)
        {
            search->search_type = MC_SEARCH_T_REGEX;
            search->is_case_sensitive = !ignore_case;
            matched = mc_search_run (search, filename, 0, filename_len, NULL);
            mc_search_free (search);
        }
        return matched;
    }

    pattern = mc_config_get_string_raw (ini, group, "Shell", NULL);
    if (pattern != NULL)
    {
        gboolean ignore_case = mc_config_get_bool (ini, group, "ShellIgnoreCase", FALSE);
        int (*cmp_func) (const char *s1, const char *s2, size_t n) =
            ignore_case ? strncasecmp : strncmp;
        size_t pattern_len = strlen (pattern);
        gboolean matched;

        if (pattern[0] == '.' && filename_len >= pattern_len)
            matched = cmp_func (pattern, filename + filename_len - pattern_len, pattern_len) == 0;
        else
            matched = pattern_len == filename_len && cmp_func (pattern, filename, pattern_len) == 0;
        g_free (pattern);
        return matched;
    }

    return type_used;
}

/* --------------------------------------------------------------------------------------------- */

static mc_magic_action_state_t
magic_find_in_config (magic_config_t *config, const mc_magic_source_t *source, const char *action,
                      char **local_copy, mc_magic_action_t *result, gboolean *matched_out,
                      magic_type_info_t *type)
{
    char **iter;

    *matched_out = FALSE;
    if (config->ini == NULL || config->groups == NULL)
        return MC_MAGIC_ACTION_NONE;

    for (iter = config->groups; *iter != NULL; iter++)
    {
        const char *group = *iter;
        gchar *value;

        if (strcmp (group, "Default") == 0 || strcmp (group, "magic.ini") == 0)
            continue;
        if (!magic_group_matches (config->ini, group, source, local_copy, type))
            continue;

        value = mc_config_get_string_raw (config->ini, group, action, NULL);
        if (value == NULL || value[0] == '\0')
        {
            /* A rule with an Open= but no View= says nothing about viewing;
               the next file, the system one, still may. */
            g_free (value);
            continue;
        }

        *matched_out = TRUE;

        {
            mc_magic_action_state_t state = magic_parse_action (value, result);

            g_free (value);
            return state;
        }
    }

    if (mc_config_has_group (config->ini, "Default"))
    {
        gchar *value = mc_config_get_string_raw (config->ini, "Default", action, NULL);

        if (value != NULL && value[0] != '\0')
        {
            mc_magic_action_state_t state;

            *matched_out = TRUE;
            state = magic_parse_action (value, result);
            g_free (value);
            return state;
        }
        g_free (value);
    }

    return MC_MAGIC_ACTION_NONE;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

mc_magic_action_state_t
mc_magic_find_action (const mc_magic_source_t *source, const char *action, char **local_copy,
                      mc_magic_action_t *result)
{
    mc_magic_action_state_t state;
    magic_type_info_t type = { FALSE, { '\0' } };
    gboolean matched;

    if (source == NULL || source->display_name == NULL || action == NULL || local_copy == NULL
        || result == NULL)
        return MC_MAGIC_ACTION_ERROR;

    mc_magic_action_clear (result);
    magic_load ();

    /* file(1) runs at most once for the file, however many rules ask. */
    state = magic_find_in_config (&magic_user_config, source, action, local_copy, result, &matched,
                                  &type);
    if (matched)
        return state;

    return magic_find_in_config (&magic_system_config, source, action, local_copy, result, &matched,
                                 &type);
}

/* --------------------------------------------------------------------------------------------- */

void
mc_magic_action_clear (mc_magic_action_t *action)
{
    if (action == NULL)
        return;

    g_free (action->plugin_name);
    g_free (action->operation_name);
    action->plugin_name = NULL;
    action->operation_name = NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_magic_flush (void)
{
    magic_config_clear (&magic_user_config);
    magic_config_clear (&magic_system_config);
    magic_configs_loaded = FALSE;
    mc_search_free (magic_action_regex);
    magic_action_regex = NULL;
}
