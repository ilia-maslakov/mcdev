/*
   Ctags repository management.

   Copyright (C) 2025-2026
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
#include "lib/util.h" /* exist_file */
#include "lib/strutil.h"
#include "lib/mcconfig.h"

#include "ctags-parser.h"
#include "ctags-index.h"
#include "ctags-repository.h"

/*** file scope macro definitions ****************************************************************/

#define TAGS_FILENAME "tags"

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static long
ctags_excmd_resolve (const char *file_path, const char *excmd)
{
    FILE *f;
    char buf[4096];
    const char *pattern;
    const char *pat_end;
    gsize pat_len;
    long line_num = 0;
    long cur_line = 0;

    /* excmd format: /^pattern/ or /^pattern$/ */
    if (excmd[0] != '/')
        return 0;

    pattern = excmd + 1;
    if (*pattern == '^')
        pattern++;

    pat_end = strrchr (pattern, '/');
    if (pat_end == NULL || pat_end == pattern)
        return 0;

    pat_len = (gsize) (pat_end - pattern);
    if (pat_len > 0 && pattern[pat_len - 1] == '$')
        pat_len--;

    if (pat_len == 0)
        return 0;

    f = fopen (file_path, "r");
    if (f == NULL)
        return 0;

    while (fgets (buf, (int) sizeof (buf), f) != NULL)
    {
        cur_line++;
        if (strncmp (buf, pattern, pat_len) == 0)
        {
            line_num = cur_line;
            break;
        }
    }

    fclose (f);
    return line_num;
}

/* --------------------------------------------------------------------------------------------- */

/* Returns a newly-allocated path for the history file of @repo.
 * Stored in ~/.local/share/mc/ctags/<hash_of_root_dir>.history */
static char *
ctags_repo_history_path (const ctags_repo_t *repo)
{
    const char *data_dir;
    char *ctags_dir;
    char *path;

    if (repo->root_dir == NULL)
        return NULL;

    data_dir = mc_config_get_data_path ();
    /* mc_config_get_data_path() can return NULL very early in startup or in
       a stripped runtime; g_build_filename(NULL, "ctags", NULL) would yield
       just "ctags", and g_mkdir_with_parents would create that directory in
       the current working directory.  Refuse instead. */
    if (data_dir == NULL)
        return NULL;

    ctags_dir = g_build_filename (data_dir, "ctags", NULL);

    if (!g_file_test (ctags_dir, G_FILE_TEST_IS_DIR))
        g_mkdir_with_parents (ctags_dir, 0700);

    path = g_strdup_printf ("%s/%08x.history", ctags_dir, g_str_hash (repo->root_dir));
    g_free (ctags_dir);
    return path;
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_repo_history_load (ctags_repo_t *repo)
{
    char *hpath;
    char *contents = NULL;
    gchar **lines;
    gchar **it;

    hpath = ctags_repo_history_path (repo);
    if (hpath == NULL)
        return;

    if (!g_file_get_contents (hpath, &contents, NULL, NULL))
    {
        g_free (hpath);
        return;
    }
    g_free (hpath);

    lines = g_strsplit (contents, "\n", -1);
    g_free (contents);

    for (it = lines; *it != NULL; it++)
    {
        gchar **parts;
        ctags_entry_t *e;
        long line_num;

        if (**it == '\0')
            continue;

        parts = g_strsplit (*it, "\t", 3);
        if (g_strv_length (parts) < 3)
        {
            g_strfreev (parts);
            continue;
        }

        line_num = strtol (parts[2], NULL, 10);
        if (*parts[1] == '\0' || line_num <= 0)
        {
            g_strfreev (parts);
            continue;
        }

        e = g_new0 (ctags_entry_t, 1);
        e->name = *parts[0] != '\0' ? g_strdup (parts[0]) : NULL;
        e->file = g_strdup (parts[1]);
        e->line = line_num;
        g_ptr_array_add (repo->nav_history, e);

        g_strfreev (parts);
    }

    g_strfreev (lines);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_repo_history_save (const ctags_repo_t *repo)
{
    char *hpath;
    GString *buf;
    guint i;

    hpath = ctags_repo_history_path (repo);
    if (hpath == NULL)
        return;

    buf = g_string_new (NULL);

    for (i = 0; i < repo->nav_history->len; i++)
    {
        const ctags_entry_t *e = (const ctags_entry_t *) g_ptr_array_index (repo->nav_history, i);

        g_string_append_printf (buf, "%s\t%s\t%ld\n", e->name != NULL ? e->name : "",
                                e->file != NULL ? e->file : "", e->line);
    }

    g_file_set_contents (hpath, buf->str, (gssize) buf->len, NULL);
    g_free (hpath);
    g_string_free (buf, TRUE);
}

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

long
ctags_entry_resolve_line (const ctags_entry_t *e, const char *file_path)
{
    long l;

    if (e == NULL)
        return 1;
    if (e->line > 0)
        return e->line;
    if (e->excmd == NULL || file_path == NULL)
        return 1;

    l = ctags_excmd_resolve (file_path, e->excmd);
    return l > 0 ? l : 1;
}

/* --------------------------------------------------------------------------------------------- */

ctags_repo_t *
ctags_repo_load (const char *tags_path)
{
    ctags_repo_t *repo;

    if (tags_path == NULL || !exist_file (tags_path))
        return NULL;

    repo = g_new0 (ctags_repo_t, 1);
    repo->tags_path = g_strdup (tags_path);
    repo->root_dir = g_path_get_dirname (tags_path);
    repo->entries = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);
    repo->nav_history = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);

    if (ctags_parse_file (tags_path, repo->entries) == 0)
    {
        ctags_repo_free (repo);
        return NULL;
    }

    repo->index = ctags_index_build (repo->entries, repo->root_dir);
    if (repo->index == NULL)
    {
        ctags_repo_free (repo);
        return NULL;
    }

    ctags_repo_history_load (repo);
    return repo;
}

/* --------------------------------------------------------------------------------------------- */

void
ctags_repo_free (ctags_repo_t *repo)
{
    if (repo == NULL)
        return;

    g_free (repo->tags_path);
    g_free (repo->root_dir);
    ctags_index_free (repo->index);
    if (repo->entries != NULL)
        g_ptr_array_free (repo->entries, TRUE);
    if (repo->nav_history != NULL)
        g_ptr_array_free (repo->nav_history, TRUE);

    g_free (repo);
}

/* --------------------------------------------------------------------------------------------- */

char *
ctags_repo_discover (const char *from_path)
{
    char *dir, *candidate, *parent;

    if (from_path == NULL)
        return NULL;

    if (g_file_test (from_path, G_FILE_TEST_IS_DIR))
        dir = g_strdup (from_path);
    else
        dir = g_path_get_dirname (from_path);

    do
    {
        candidate = g_build_filename (dir, TAGS_FILENAME, NULL);
        if (exist_file (candidate))
        {
            g_free (dir);
            return candidate;
        }
        g_free (candidate);

        parent = g_path_get_dirname (dir);
        if (strcmp (parent, dir) == 0)
        {
            /* Reached filesystem root */
            g_free (parent);
            break;
        }
        g_free (dir);
        dir = parent;
    }
    while (TRUE);

    g_free (dir);
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
ctags_path_is_under (const char *file, const char *root)
{
    size_t root_len;

    if (file == NULL || root == NULL)
        return FALSE;

    root_len = strlen (root);
    if (root_len == 0)
        return TRUE;

    if (strncmp (file, root, root_len) != 0)
        return FALSE;

    /* Path-component boundary: either the root already ends with '/', or @file
     * either terminates at @root or continues with a separator. */
    if (root[root_len - 1] == G_DIR_SEPARATOR)
        return TRUE;

    return file[root_len] == '\0' || file[root_len] == G_DIR_SEPARATOR;
}

/* --------------------------------------------------------------------------------------------- */

ctags_repo_t *
ctags_repos_find_for_file (GSList *repos, const char *file_path)
{
    for (; repos != NULL; repos = g_slist_next (repos))
    {
        ctags_repo_t *repo = (ctags_repo_t *) repos->data;

        if (ctags_path_is_under (file_path, repo->root_dir))
            return repo;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
ctags_repos_find_exact (GSList *repos, const char *name)
{
    GPtrArray *result;

    if (name == NULL)
        return NULL;

    result = g_ptr_array_new ();

    for (; repos != NULL; repos = g_slist_next (repos))
    {
        ctags_repo_t *repo = (ctags_repo_t *) repos->data;
        const GPtrArray *found;
        guint i;

        if (repo->index == NULL)
            continue;

        found = ctags_index_find_exact (repo->index, name);
        if (found == NULL)
            found = ctags_index_find_exact_ci (repo->index, name);

        if (found != NULL)
            for (i = 0; i < found->len; i++)
                g_ptr_array_add (result, g_ptr_array_index (found, i));
    }

    if (result->len == 0)
    {
        g_ptr_array_free (result, FALSE);
        return NULL;
    }

    return result;
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
ctags_repos_find_prefix (GSList *repos, const char *prefix, gboolean ci)
{
    GPtrArray *result;

    if (prefix == NULL || *prefix == '\0')
        return NULL;

    result = g_ptr_array_new ();

    for (; repos != NULL; repos = g_slist_next (repos))
    {
        ctags_repo_t *repo = (ctags_repo_t *) repos->data;
        GPtrArray *found;
        guint i;

        if (repo->index == NULL)
            continue;

        found = ctags_index_find_prefix (repo->index, prefix, ci);
        if (found != NULL)
        {
            for (i = 0; i < found->len; i++)
                g_ptr_array_add (result, g_ptr_array_index (found, i));
            g_ptr_array_free (found, FALSE);
        }
    }

    if (result->len == 0)
    {
        g_ptr_array_free (result, FALSE);
        return NULL;
    }

    return result;
}

/* --------------------------------------------------------------------------------------------- */

void
ctags_repo_history_push (ctags_repo_t *repo, const char *name, const char *file, long line)
{
    ctags_entry_t *e;

    if (repo == NULL || repo->nav_history == NULL || file == NULL || line <= 0)
        return;

    /* Skip duplicate of the most recent entry */
    if (repo->nav_history->len > 0)
    {
        const ctags_entry_t *last = (const ctags_entry_t *) g_ptr_array_index (
            repo->nav_history, repo->nav_history->len - 1);
        if (last->line == line && g_strcmp0 (last->file, file) == 0)
            return;
    }

    e = g_new0 (ctags_entry_t, 1);
    e->name = (name != NULL && *name != '\0') ? g_strdup (name) : NULL;
    e->file = g_strdup (file);
    e->line = line;
    g_ptr_array_add (repo->nav_history, e);

    /* Trim oldest entries */
    while (repo->nav_history->len > CTAGS_HISTORY_MAX)
        g_ptr_array_remove_index (repo->nav_history, 0);

    ctags_repo_history_save (repo);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
ctags_repos_find_basename (GSList *repos, const char *name)
{
    GPtrArray *result;

    if (name == NULL || *name == '\0')
        return NULL;

    result = g_ptr_array_new ();

    for (; repos != NULL; repos = g_slist_next (repos))
    {
        ctags_repo_t *repo = (ctags_repo_t *) repos->data;
        GPtrArray *found;
        guint i;

        if (repo->index == NULL)
            continue;

        found = ctags_index_find_basename (repo->index, name);
        if (found != NULL)
        {
            for (i = 0; i < found->len; i++)
                g_ptr_array_add (result, g_ptr_array_index (found, i));
            g_ptr_array_free (found, FALSE);
        }
    }

    if (result->len == 0)
    {
        g_ptr_array_free (result, FALSE);
        return NULL;
    }

    return result;
}

/* --------------------------------------------------------------------------------------------- */
