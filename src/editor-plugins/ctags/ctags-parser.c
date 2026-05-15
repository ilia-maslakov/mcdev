/*
   Ctags file parser.

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/global.h"
#include "lib/strutil.h"

#include "ctags-parser.h"

/*** file scope macro definitions ****************************************************************/

#define CTAGS_LINE_MAX 4096

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */

static void
parse_extended_fields (ctags_entry_t *e, const char *fields)
{
    gchar **parts;
    guint i;

    if (fields == NULL || *fields == '\0')
        return;

    /* First field after ;"\t is the kind (single character) */
    if (*fields != '\t' && *fields != '\0')
    {
        e->kind = *fields;
        fields++;
    }

    if (*fields == '\0')
        return;

    parts = g_strsplit (fields, "\t", -1);
    for (i = 0; parts[i] != NULL; i++)
    {
        char *p = parts[i];
        char *colon;

        if (*p == '\0')
            continue;

        /* Kind field: single character with no colon */
        if (e->kind == '\0' && p[1] == '\0' && p[0] != '\0')
        {
            e->kind = p[0];
            continue;
        }

        colon = strchr (p, ':');
        if (colon == NULL)
        {
            /* Bare single-char kind */
            if (e->kind == '\0' && p[1] == '\0')
                e->kind = p[0];
            continue;
        }

        *colon = '\0';
        colon++;

        if (strcmp (p, "line") == 0)
        {
            if (e->line == 0)
                e->line = atol (colon);
        }
        else if (strcmp (p, "class") == 0 || strcmp (p, "struct") == 0 || strcmp (p, "union") == 0
                 || strcmp (p, "namespace") == 0 || strcmp (p, "interface") == 0
                 || strcmp (p, "module") == 0 || strcmp (p, "package") == 0)
        {
            if (e->scope == NULL)
            {
                e->scope = g_strdup (colon);
                e->scope_kind = g_strdup (p);
            }
        }
        else if (strcmp (p, "scope") == 0)
        {
            if (e->scope == NULL)
            {
                /* scope field may be "class:Name" or just "Name" */
                char *inner = strchr (colon, ':');
                if (inner != NULL)
                {
                    e->scope_kind = g_strndup (colon, inner - colon);
                    e->scope = g_strdup (inner + 1);
                }
                else
                {
                    e->scope = g_strdup (colon);
                }
            }
        }
        else if (strcmp (p, "signature") == 0)
        {
            g_free (e->signature);
            e->signature = g_strdup (colon);
        }
        else if (strcmp (p, "typeref") == 0)
        {
            g_free (e->typeref);
            e->typeref = g_strdup (colon);
        }
    }
    g_strfreev (parts);
}

/* --------------------------------------------------------------------------------------------- */

/* Parse a single line from a ctags file.  Returns a heap-allocated entry or NULL. */
static ctags_entry_t *
parse_ctags_line (const char *line)
{
    const char *tab1, *tab2, *semicol;
    ctags_entry_t *e;
    char *excmd_end;

    /* Skip pseudo-tags and blank lines */
    if (line[0] == '!' || line[0] == '\0' || line[0] == '\n')
        return NULL;

    tab1 = strchr (line, '\t');
    if (tab1 == NULL)
        return NULL;

    tab2 = strchr (tab1 + 1, '\t');
    if (tab2 == NULL)
        return NULL;

    e = g_new0 (ctags_entry_t, 1);
    e->name = g_strndup (line, tab1 - line);
    e->file = g_strndup (tab1 + 1, tab2 - tab1 - 1);

    /* Find the ;" marker in the excmd field */
    semicol = strstr (tab2 + 1, ";\"");
    if (semicol != NULL)
    {
        e->excmd = g_strndup (tab2 + 1, semicol - tab2 - 1);
        /* Extended fields start after ;"\t or ;" */
        parse_extended_fields (e, semicol + 2);
    }
    else
    {
        /* No extended format; strip trailing newline */
        e->excmd = g_strdup (tab2 + 1);
        excmd_end = e->excmd + strlen (e->excmd) - 1;
        while (excmd_end >= e->excmd && (*excmd_end == '\n' || *excmd_end == '\r'))
            *excmd_end-- = '\0';
    }

    /* Extract line from excmd if it is a plain number */
    if (e->line == 0 && e->excmd != NULL)
    {
        char *endp = NULL;
        long lnum;

        lnum = strtol (e->excmd, &endp, 10);
        if (endp != e->excmd && endp != NULL && (*endp == '\0' || *endp == '\r' || *endp == '\n'))
            e->line = lnum;
    }

    /* Discard empty names */
    if (e->name == NULL || e->name[0] == '\0')
    {
        ctags_entry_free (e);
        return NULL;
    }

    return e;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
ctags_entry_free (ctags_entry_t *e)
{
    if (e == NULL)
        return;
    g_free (e->name);
    g_free (e->file);
    g_free (e->excmd);
    g_free (e->scope);
    g_free (e->scope_kind);
    g_free (e->signature);
    g_free (e->typeref);
    g_free (e);
}

/* --------------------------------------------------------------------------------------------- */

void
ctags_entry_free_ptr (gpointer p)
{
    ctags_entry_free ((ctags_entry_t *) p);
}

/* --------------------------------------------------------------------------------------------- */

gsize
ctags_parse_file (const char *path, GPtrArray *out)
{
    FILE *f;
    char buf[CTAGS_LINE_MAX];
    gsize added = 0;

    if (path == NULL || out == NULL)
        return 0;

    f = fopen (path, "r");
    if (f == NULL)
        return 0;

    while (fgets (buf, sizeof (buf), f) != NULL)
    {
        ctags_entry_t *e;
        size_t len;

        len = strlen (buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';

        e = parse_ctags_line (buf);
        if (e != NULL)
        {
            g_ptr_array_add (out, e);
            added++;
        }
    }

    fclose (f);
    return added;
}

/* --------------------------------------------------------------------------------------------- */
