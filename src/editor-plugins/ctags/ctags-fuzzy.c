/*
   Fuzzy symbol matching for the ctags plugin.

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

#include "ctags-parser.h"
#include "ctags-fuzzy.h"

/*** file scope macro definitions ****************************************************************/

#define SCORE_CONSECUTIVE   8 /* bonus per char in a consecutive run */
#define SCORE_WORD_BOUNDARY 5 /* bonus for matching at start / after '_' / after case change */
#define SCORE_MATCH         1 /* base score per matched character */

/*** file scope type declarations ****************************************************************/

typedef struct
{
    ctags_entry_t *entry;
    int score;
} fuzzy_result_t;

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static gboolean
is_word_boundary (const char *name, int pos)
{
    if (pos == 0)
        return TRUE;
    if (name[pos - 1] == '_')
        return TRUE;
    /* camelCase boundary: previous char lowercase, current uppercase */
    if (g_ascii_islower (name[pos - 1]) && g_ascii_isupper (name[pos]))
        return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static int
compare_fuzzy_result (const void *a, const void *b)
{
    const fuzzy_result_t *ra = (const fuzzy_result_t *) a;
    const fuzzy_result_t *rb = (const fuzzy_result_t *) b;

    return rb->score - ra->score; /* descending */
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
ctags_fuzzy_match (const char *name, const char *query)
{
    const char *q;

    if (name == NULL || query == NULL)
        return FALSE;
    if (*query == '\0')
        return TRUE;

    q = query;
    while (*name != '\0' && *q != '\0')
    {
        if (g_ascii_tolower (*name) == g_ascii_tolower (*q))
            q++;
        name++;
    }
    return *q == '\0';
}

/* --------------------------------------------------------------------------------------------- */

int
ctags_fuzzy_score (const char *name, const char *query)
{
    const char *q;
    int score = 0;
    int name_pos = 0;
    int consecutive = 0;
    int prev_matched = -1;

    if (name == NULL || query == NULL || *query == '\0')
        return 0;

    q = query;
    while (name[name_pos] != '\0' && *q != '\0')
    {
        if (g_ascii_tolower (name[name_pos]) == g_ascii_tolower (*q))
        {
            score += SCORE_MATCH;

            if (prev_matched == name_pos - 1)
            {
                consecutive++;
                score += SCORE_CONSECUTIVE * consecutive;
            }
            else
                consecutive = 0;

            if (is_word_boundary (name, name_pos))
                score += SCORE_WORD_BOUNDARY;

            prev_matched = name_pos;
            q++;
        }
        name_pos++;
    }

    if (*q != '\0')
        return 0; /* not all query chars matched */

    return score;
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
ctags_fuzzy_search (const GPtrArray *entries, const char *query)
{
    GArray *results;
    GPtrArray *out;
    guint i;

    if (entries == NULL || query == NULL || *query == '\0')
        return NULL;

    results = g_array_new (FALSE, FALSE, sizeof (fuzzy_result_t));

    for (i = 0; i < entries->len; i++)
    {
        ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (entries, i);
        int score;

        if (e->name == NULL)
            continue;

        score = ctags_fuzzy_score (e->name, query);
        if (score > 0)
        {
            fuzzy_result_t r;

            r.entry = e;
            r.score = score;
            g_array_append_val (results, r);
        }
    }

    if (results->len == 0)
    {
        g_array_free (results, TRUE);
        return NULL;
    }

    g_array_sort (results, compare_fuzzy_result);

    out = g_ptr_array_new ();
    for (i = 0; i < results->len; i++)
        g_ptr_array_add (out, g_array_index (results, fuzzy_result_t, i).entry);

    g_array_free (results, TRUE);
    return out;
}

/* --------------------------------------------------------------------------------------------- */
