/*
   In-memory indexes over ctags entries.

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

#include "lib/global.h"
#include "lib/strutil.h"

#include "ctags-parser.h"
#include "ctags-index.h"

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

/* Returns a GPtrArray stored in @ht under @key, creating it if absent. */
static GPtrArray *
index_ensure_bucket (GHashTable *ht, const char *key)
{
    GPtrArray *arr;

    arr = (GPtrArray *) g_hash_table_lookup (ht, key);
    if (arr == NULL)
    {
        arr = g_ptr_array_new ();
        g_hash_table_insert (ht, g_strdup (key), arr);
    }
    return arr;
}

/* --------------------------------------------------------------------------------------------- */

static void
index_bucket_free (gpointer p)
{
    g_ptr_array_free ((GPtrArray *) p, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static int
entry_name_cmp (gconstpointer a, gconstpointer b)
{
    const ctags_entry_t *ea = *(const ctags_entry_t **) a;
    const ctags_entry_t *eb = *(const ctags_entry_t **) b;

    return strcmp (ea->name, eb->name);
}

/* --------------------------------------------------------------------------------------------- */

/* Build canonical file path: join root_dir + entry->file if relative. */
static char *
make_canon_path (const ctags_entry_t *e, const char *root_dir)
{
    if (e->file == NULL)
        return NULL;

    if (g_path_is_absolute (e->file))
        return g_strdup (e->file);

    if (root_dir != NULL)
        return g_build_filename (root_dir, e->file, NULL);

    return g_strdup (e->file);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

ctags_index_t *
ctags_index_build (GPtrArray *entries, const char *root_dir)
{
    ctags_index_t *idx;
    guint i;

    if (entries == NULL)
        return NULL;

    idx = g_new0 (ctags_index_t, 1);
    idx->by_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, index_bucket_free);
    idx->by_name_ci = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, index_bucket_free);
    idx->by_file = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, index_bucket_free);
    idx->by_basename = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, index_bucket_free);
    idx->by_scope = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, index_bucket_free);
    idx->all = g_ptr_array_new ();

    for (i = 0; i < entries->len; i++)
    {
        ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (entries, i);
        char *canon;
        char *ci_name;

        g_ptr_array_add (idx->all, e);

        /* by_name */
        g_ptr_array_add (index_ensure_bucket (idx->by_name, e->name), e);

        /* by_name_ci */
        ci_name = g_utf8_casefold (e->name, -1);
        g_ptr_array_add (index_ensure_bucket (idx->by_name_ci, ci_name), e);
        g_free (ci_name);

        /* by_file and by_basename */
        canon = make_canon_path (e, root_dir);
        if (canon != NULL)
        {
            const char *base;

            g_ptr_array_add (index_ensure_bucket (idx->by_file, canon), e);

            base = strrchr (canon, G_DIR_SEPARATOR);
            base = (base != NULL) ? base + 1 : canon;
            g_ptr_array_add (index_ensure_bucket (idx->by_basename, base), e);
            g_free (canon);
        }

        /* by_scope */
        if (e->scope != NULL)
            g_ptr_array_add (index_ensure_bucket (idx->by_scope, e->scope), e);
    }

    /* Sort all entries by name for prefix search */
    g_ptr_array_sort (idx->all, entry_name_cmp);

    return idx;
}

/* --------------------------------------------------------------------------------------------- */

void
ctags_index_free (ctags_index_t *idx)
{
    if (idx == NULL)
        return;

    if (idx->by_name != NULL)
        g_hash_table_destroy (idx->by_name);
    if (idx->by_name_ci != NULL)
        g_hash_table_destroy (idx->by_name_ci);
    if (idx->by_file != NULL)
        g_hash_table_destroy (idx->by_file);
    if (idx->by_basename != NULL)
        g_hash_table_destroy (idx->by_basename);
    if (idx->by_scope != NULL)
        g_hash_table_destroy (idx->by_scope);
    if (idx->all != NULL)
        g_ptr_array_free (idx->all, FALSE);

    g_free (idx);
}

/* --------------------------------------------------------------------------------------------- */

const GPtrArray *
ctags_index_find_exact (const ctags_index_t *idx, const char *name)
{
    if (idx == NULL || name == NULL)
        return NULL;
    return (const GPtrArray *) g_hash_table_lookup (idx->by_name, name);
}

/* --------------------------------------------------------------------------------------------- */

const GPtrArray *
ctags_index_find_exact_ci (const ctags_index_t *idx, const char *name)
{
    char *ci;
    GPtrArray *result;

    if (idx == NULL || name == NULL)
        return NULL;

    ci = g_utf8_casefold (name, -1);
    result = (GPtrArray *) g_hash_table_lookup (idx->by_name_ci, ci);
    g_free (ci);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
ctags_index_find_prefix (const ctags_index_t *idx, const char *prefix, gboolean ci)
{
    GPtrArray *result;
    gsize prefix_len;
    guint i;

    if (idx == NULL || prefix == NULL || *prefix == '\0')
        return NULL;

    prefix_len = strlen (prefix);
    result = g_ptr_array_new ();

    if (!ci)
    {
        /* Case-sensitive: use sorted all[] with binary search start */
        guint lo = 0, hi = idx->all->len;

        while (lo < hi)
        {
            guint mid = lo + (hi - lo) / 2;
            ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (idx->all, mid);

            if (strncmp (e->name, prefix, prefix_len) < 0)
                lo = mid + 1;
            else
                hi = mid;
        }

        for (i = lo; i < idx->all->len; i++)
        {
            ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (idx->all, i);

            if (strncmp (e->name, prefix, prefix_len) != 0)
                break;
            g_ptr_array_add (result, e);
        }
    }
    else
    {
        /* Case-insensitive: linear scan over by_name_ci */
        char *ci_prefix = g_utf8_casefold (prefix, -1);

        for (i = 0; i < idx->all->len; i++)
        {
            ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (idx->all, i);
            char *ci_name = g_utf8_casefold (e->name, -1);
            gboolean match = g_str_has_prefix (ci_name, ci_prefix);

            g_free (ci_name);
            if (match)
                g_ptr_array_add (result, e);
        }

        g_free (ci_prefix);
    }

    if (result->len == 0)
    {
        g_ptr_array_free (result, FALSE);
        return NULL;
    }

    return result;
}

/* --------------------------------------------------------------------------------------------- */

const GPtrArray *
ctags_index_find_file (const ctags_index_t *idx, const char *canon_path)
{
    if (idx == NULL || canon_path == NULL)
        return NULL;
    return (const GPtrArray *) g_hash_table_lookup (idx->by_file, canon_path);
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
ctags_index_find_basename (const ctags_index_t *idx, const char *name)
{
    GPtrArray *result;
    GHashTableIter iter;
    gpointer key, value;
    gsize name_len;

    if (idx == NULL || name == NULL || *name == '\0')
        return NULL;

    name_len = strlen (name);
    result = g_ptr_array_new ();

    /* Match if the basename starts with @name, or @name is a path-tail
       aligned at a directory boundary.  The latter must require the
       character before the tail to be G_DIR_SEPARATOR; otherwise a single-
       letter query like "h" would return every path ending in ...h. */
    g_hash_table_iter_init (&iter, idx->by_file);
    while (g_hash_table_iter_next (&iter, &key, &value))
    {
        const char *path = (const char *) key;
        gsize path_len = strlen (path);
        const char *base = strrchr (path, G_DIR_SEPARATOR);
        gboolean matched;

        base = (base != NULL) ? base + 1 : path;

        matched = g_str_has_prefix (base, name);
        if (!matched && path_len > name_len && path[path_len - name_len - 1] == G_DIR_SEPARATOR
            && strcmp (path + path_len - name_len, name) == 0)
            matched = TRUE;

        if (matched)
        {
            GPtrArray *bucket = (GPtrArray *) value;
            guint i;

            for (i = 0; i < bucket->len; i++)
                g_ptr_array_add (result, g_ptr_array_index (bucket, i));
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

const GPtrArray *
ctags_index_find_scope (const ctags_index_t *idx, const char *scope)
{
    if (idx == NULL || scope == NULL)
        return NULL;
    return (const GPtrArray *) g_hash_table_lookup (idx->by_scope, scope);
}

/* --------------------------------------------------------------------------------------------- */

GList *
ctags_index_get_files (const ctags_index_t *idx)
{
    if (idx == NULL)
        return NULL;
    return g_hash_table_get_keys (idx->by_file);
}

/* --------------------------------------------------------------------------------------------- */
