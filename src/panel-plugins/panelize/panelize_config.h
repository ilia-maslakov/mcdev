/** \file panelize_config.h
 *  \brief Header: Panelize plugin preset storage.
 *
 *  Presets live in $XDG_CONFIG_HOME/mc/panelize.ini, schema:
 *    [Presets]      slug=command
 *    [PresetLabels] slug=display_label
 *
 *  On first load, if the file is missing, migration is attempted from the
 *  main mc ini [Panelize] section (legacy format: label=command). If neither
 *  source exists, four built-in defaults are seeded.
 */

#ifndef MC__PANELIZE_CONFIG_H
#define MC__PANELIZE_CONFIG_H

#include "lib/global.h"

typedef struct
{
    char *key;     /* stable INI key (English slug) */
    char *label;   /* display label, translatable for built-ins */
    char *command; /* shell command */
} panelize_preset_t;

/* Load the preset list. Caller takes ownership; free with panelize_config_free. */
GPtrArray *panelize_config_load (void);

void panelize_config_free (GPtrArray *presets);

/* Persist current preset list to panelize.ini. */
void panelize_config_save (GPtrArray *presets);

/* Add a new preset to the list, generating a unique slug from the label. */
void panelize_config_add (GPtrArray *presets, const char *label, const char *command);

/* Remove a preset by index. */
void panelize_config_remove (GPtrArray *presets, guint index);

#endif
