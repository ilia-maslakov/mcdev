/*
   Panelize plugin - preset storage and migration.

   Copyright (C) 1995-2026
   Free Software Foundation, Inc.

   Written by:
   Janne Kukonlehto, 1995
   Jakub Jelinek, 1995
   Andrew Borodin <aborodin@vmail.ru> 2011-2023
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

#include <ctype.h>
#include <string.h>

#include "lib/global.h"
#include "lib/mcconfig.h"

#include "panelize_config.h"

/*** file scope macro definitions ****************************************************************/

#define PANELIZE_INI_FILE  "panelize.ini"

#define SECT_PRESETS       "Presets"
#define SECT_LABELS        "PresetLabels"

#define LEGACY_SECTION     "Panelize"
#define LEGACY_OTHER_LABEL "Other command"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    const char *en_label;
    const char *slug;
    const char *command;
} builtin_preset_t;

/*** file scope variables ************************************************************************/

/* Four legacy built-ins. Slugs are the stable INI keys; en_label is matched
   exactly during migration from the old [Panelize] section. Non-English
   installations whose old ini contains translated labels fall back to
   slug-from-label generation. */
static const builtin_preset_t builtins[] = {
    { N_ ("Modified git files"), "git-modified", "git ls-files --modified" },
    { N_ ("Find rejects after patching"), "find-rej", "find . -name \\*.rej -print" },
    { N_ ("Find *.orig after patching"), "find-orig", "find . -name \\*.orig -print" },
    { N_ ("Find SUID and SGID programs"), "find-suid-sgid",
      "find . \\( \\( -perm -04000 -a -perm /011 \\) -o \\( -perm -02000 -a -perm /01 \\) \\) "
      "-print" },
};

/*** file scope functions ************************************************************************/

static void
preset_free (gpointer p)
{
    panelize_preset_t *preset = (panelize_preset_t *) p;

    if (preset == NULL)
        return;
    g_free (preset->key);
    g_free (preset->label);
    g_free (preset->command);
    g_free (preset);
}

/* --------------------------------------------------------------------------------------------- */

static panelize_preset_t *
preset_new (const char *key, const char *label, const char *command)
{
    panelize_preset_t *p;

    p = g_new0 (panelize_preset_t, 1);
    p->key = g_strdup (key);
    p->label = g_strdup (label);
    p->command = g_strdup (command);
    return p;
}

/* --------------------------------------------------------------------------------------------- */

/* Generate a stable ASCII slug from an arbitrary label.
   Replaces runs of non-alnum chars with single hyphens; lowercases.
   Empty result becomes "preset". */
static char *
slug_from_label (const char *label)
{
    GString *s;
    const char *p;
    gboolean last_was_sep = TRUE;

    s = g_string_new (NULL);
    for (p = label; *p != '\0'; p++)
    {
        unsigned char c = (unsigned char) *p;
        if (c < 128 && (g_ascii_isalnum (c)))
        {
            g_string_append_c (s, g_ascii_tolower (c));
            last_was_sep = FALSE;
        }
        else if (!last_was_sep)
        {
            g_string_append_c (s, '-');
            last_was_sep = TRUE;
        }
    }
    /* trim trailing hyphen */
    while (s->len > 0 && s->str[s->len - 1] == '-')
        g_string_truncate (s, s->len - 1);

    if (s->len == 0)
        g_string_assign (s, "preset");

    return g_string_free (s, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
slug_in_use (GPtrArray *presets, const char *slug)
{
    guint i;

    for (i = 0; i < presets->len; i++)
    {
        panelize_preset_t *p = g_ptr_array_index (presets, i);
        if (strcmp (p->key, slug) == 0)
            return TRUE;
    }
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
slug_unique (GPtrArray *presets, const char *label)
{
    char *base;
    char *try;
    int n;

    base = slug_from_label (label);
    if (!slug_in_use (presets, base))
        return base;

    for (n = 2;; n++)
    {
        try = g_strdup_printf ("%s-%d", base, n);
        if (!slug_in_use (presets, try))
        {
            g_free (base);
            return try;
        }
        g_free (try);
    }
}

/* --------------------------------------------------------------------------------------------- */

static const char *
match_builtin_slug (const char *label)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS (builtins); i++)
        if (strcmp (label, builtins[i].en_label) == 0)
            return builtins[i].slug;
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
seed_defaults (GPtrArray *presets)
{
    size_t i;

    for (i = 0; i < G_N_ELEMENTS (builtins); i++)
    {
        panelize_preset_t *p =
            preset_new (builtins[i].slug, _ (builtins[i].en_label), builtins[i].command);
        g_ptr_array_add (presets, p);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
migrate_from_legacy (GPtrArray *presets)
{
    gchar **keys;
    gchar **k;
    gboolean migrated = FALSE;

    if (mc_global.main_config == NULL
        || !mc_config_has_group (mc_global.main_config, LEGACY_SECTION))
        return FALSE;

    keys = mc_config_get_keys (mc_global.main_config, LEGACY_SECTION, NULL);
    if (keys == NULL)
        return FALSE;

    for (k = keys; *k != NULL; k++)
    {
        const char *label = *k;
        char *command;
        const char *slug_match;
        char *slug;
        panelize_preset_t *p;

        /* Skip the legacy "Other command" pseudo-entry -- it carried an empty
           command and only served as the slot for arbitrary input. The new UI
           uses a dedicated "Custom command..." path instead. */
        if (strcmp (label, LEGACY_OTHER_LABEL) == 0)
            continue;

        command = mc_config_get_string (mc_global.main_config, LEGACY_SECTION, label, "");
        if (command[0] == '\0')
        {
            g_free (command);
            continue;
        }

        slug_match = match_builtin_slug (label);
        slug = (slug_match != NULL) ? g_strdup (slug_match) : slug_unique (presets, label);

        p = preset_new (slug, label, command);
        g_ptr_array_add (presets, p);
        g_free (slug);
        g_free (command);
        migrated = TRUE;
    }

    g_strfreev (keys);
    return migrated;
}

/* --------------------------------------------------------------------------------------------- */

static char *
config_path (void)
{
    return g_build_filename (mc_config_get_path (), PANELIZE_INI_FILE, (char *) NULL);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

GPtrArray *
panelize_config_load (void)
{
    GPtrArray *presets;
    char *path;
    mc_config_t *cfg;

    presets = g_ptr_array_new_with_free_func (preset_free);

    path = config_path ();
    cfg = mc_config_init (path, FALSE);
    g_free (path);

    if (cfg != NULL && mc_config_has_group (cfg, SECT_PRESETS))
    {
        gchar **keys = mc_config_get_keys (cfg, SECT_PRESETS, NULL);
        gchar **k;

        for (k = keys; k != NULL && *k != NULL; k++)
        {
            char *cmd = mc_config_get_string (cfg, SECT_PRESETS, *k, "");
            char *label = mc_config_get_string (cfg, SECT_LABELS, *k, *k);
            panelize_preset_t *p;

            /* Built-ins: re-translate the label so locale changes between
               sessions are reflected. We detect a built-in by slug match. */
            {
                size_t i;
                for (i = 0; i < G_N_ELEMENTS (builtins); i++)
                    if (strcmp (*k, builtins[i].slug) == 0)
                    {
                        g_free (label);
                        label = g_strdup (_ (builtins[i].en_label));
                        break;
                    }
            }

            p = preset_new (*k, label, cmd);
            g_ptr_array_add (presets, p);
            g_free (cmd);
            g_free (label);
        }
        g_strfreev (keys);
    }
    else
    {
        /* Try legacy migration first, fall back to built-in defaults. */
        if (!migrate_from_legacy (presets))
            seed_defaults (presets);
        panelize_config_save (presets);
    }

    if (cfg != NULL)
        mc_config_deinit (cfg);

    return presets;
}

/* --------------------------------------------------------------------------------------------- */

void
panelize_config_free (GPtrArray *presets)
{
    if (presets != NULL)
        g_ptr_array_free (presets, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

void
panelize_config_save (GPtrArray *presets)
{
    char *path;
    mc_config_t *cfg;
    guint i;

    path = config_path ();
    cfg = mc_config_init (path, FALSE);
    if (cfg == NULL)
    {
        g_free (path);
        return;
    }

    mc_config_del_group (cfg, SECT_PRESETS);
    mc_config_del_group (cfg, SECT_LABELS);

    for (i = 0; i < presets->len; i++)
    {
        panelize_preset_t *p = g_ptr_array_index (presets, i);
        mc_config_set_string (cfg, SECT_PRESETS, p->key, p->command);
        mc_config_set_string (cfg, SECT_LABELS, p->key, p->label);
    }

    mc_config_save_to_file (cfg, path, NULL);
    mc_config_deinit (cfg);
    g_free (path);
}

/* --------------------------------------------------------------------------------------------- */

void
panelize_config_add (GPtrArray *presets, const char *label, const char *command)
{
    char *slug;
    panelize_preset_t *p;

    if (label == NULL || label[0] == '\0' || command == NULL || command[0] == '\0')
        return;

    slug = slug_unique (presets, label);
    p = preset_new (slug, label, command);
    g_ptr_array_add (presets, p);
    g_free (slug);
}

/* --------------------------------------------------------------------------------------------- */

void
panelize_config_remove (GPtrArray *presets, guint index)
{
    if (index < presets->len)
        g_ptr_array_remove_index (presets, index);
}

/* --------------------------------------------------------------------------------------------- */
