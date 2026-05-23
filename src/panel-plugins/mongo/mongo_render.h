/** \file mongo_render.h
 *  \brief MongoDB plugin: BSON value rendering helpers.
 */

#ifndef MC_PANEL_MONGO_RENDER_H
#define MC_PANEL_MONGO_RENDER_H

#include "lib/global.h"

struct _bson_value_t;
typedef struct _bson_value_t bson_value_t;

/*** API ***************************************************************************************/

/* Single-line render of a BSON value; worst case writes "?". */
void mongo_render_value (const bson_value_t *v, char *buf, gsize buflen);

/* Short type tag: "oid"/"str"/"int"/"i64"/"dbl"/"bool"/"null"/"date"/
   "doc"/"arr"/"bin"/"?". */
void mongo_render_type (const bson_value_t *v, char *buf, gsize buflen);

/* Display-only post-processing of libbson Extended JSON: shortens every
   "base64": "<value>" whose value exceeds @keep chars to "<kept>...". This is
   a textual reducer, not a semantic JSON transform; it matches any literal
   "base64" key, and the output is lossy. Returns a newly-allocated string
   (caller g_free). */
char *mongo_render_truncate_base64 (const char *json, gsize keep);

/* Re-indent compact JSON with @indent spaces per level, stripping existing
   whitespace outside strings. Input is assumed to be valid Extended JSON as
   produced by libbson (bson_as_*_extended_json); this is a formatter, not a
   validator, so malformed input only yields best-effort output. The transform
   is lossless (whitespace only). Returns a newly-allocated string (caller
   g_free); NULL only when @json is NULL. */
char *mongo_render_pretty_json (const char *json, int indent);

#endif
