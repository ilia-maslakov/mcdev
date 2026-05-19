/** \file panelize_url.h
 *  \brief Header: Panelize plugin URL parsing.
 *
 *  Activation URL format:
 *    panelize:from-file=<path>[;label=<label>][;nul]
 *
 *  Legacy activation path for callers that need to hand the plugin a
 *  pre-built path list without going through the dialog.
 */

#ifndef MC__PANELIZE_URL_H
#define MC__PANELIZE_URL_H

#include "lib/global.h"

typedef struct
{
    char *file;       /* path to file containing paths, NULL if not provided */
    char *label;      /* display label for the resulting panel, NULL if not provided */
    gboolean nul_sep; /* TRUE = NUL-separated, FALSE = newline-separated */
} panelize_url_t;

/* Parse a panelize:... URL. Returns TRUE on success and fills out_url
   (caller frees with panelize_url_clear). Returns FALSE if open_path is
   NULL, empty, or does not start with "panelize:". */
gboolean panelize_url_parse (const char *open_path, panelize_url_t *out_url);

void panelize_url_clear (panelize_url_t *url);

#endif
