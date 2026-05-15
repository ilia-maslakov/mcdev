/** \file ctags-index.h
 *  \brief Header: in-memory indexes over ctags entries
 */

#ifndef MC__CTAGS_INDEX_H
#define MC__CTAGS_INDEX_H

#include "lib/global.h"

#include "ctags-parser.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/* In-memory index built from a GPtrArray of ctags_entry_t*.
 * All entry pointers are borrowed from the source array; the index does not own them. */
typedef struct
{
    GHashTable *by_name;     /* name -> GPtrArray<ctags_entry_t*> */
    GHashTable *by_name_ci;  /* g_utf8_casefold(name) -> GPtrArray<ctags_entry_t*> */
    GHashTable *by_file;     /* canonical file path -> GPtrArray<ctags_entry_t*> */
    GHashTable *by_basename; /* basename -> GPtrArray<ctags_entry_t*> */
    GHashTable *by_scope;    /* scope name -> GPtrArray<ctags_entry_t*> */
    GPtrArray *all;          /* all entries, sorted by name for prefix search */
} ctags_index_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Build index from entries.  root_dir is used to normalise relative file paths. */
ctags_index_t *ctags_index_build (GPtrArray *entries, const char *root_dir);

void ctags_index_free (ctags_index_t *idx);

/* Exact name lookup.  Returns borrowed GPtrArray or NULL. */
const GPtrArray *ctags_index_find_exact (const ctags_index_t *idx, const char *name);

/* Case-insensitive exact lookup.  Returns borrowed GPtrArray or NULL. */
const GPtrArray *ctags_index_find_exact_ci (const ctags_index_t *idx, const char *name);

/* Prefix match.  Returns new GPtrArray (free with g_ptr_array_free(r, FALSE)), or NULL.
 * @ci: TRUE = case-insensitive. */
GPtrArray *ctags_index_find_prefix (const ctags_index_t *idx, const char *prefix, gboolean ci);

/* Entries for a canonical file path.  Returns borrowed GPtrArray or NULL. */
const GPtrArray *ctags_index_find_file (const ctags_index_t *idx, const char *canon_path);

/* Entries by file basename or path suffix.  Returns new GPtrArray or NULL. */
GPtrArray *ctags_index_find_basename (const ctags_index_t *idx, const char *name);

/* Members of a scope (class/struct/namespace).  Returns borrowed GPtrArray or NULL. */
const GPtrArray *ctags_index_find_scope (const ctags_index_t *idx, const char *scope);

/* All file paths stored in the index.  Returns a new GList of borrowed strings.
 * Caller must g_list_free() the result (do not free the strings). */
GList *ctags_index_get_files (const ctags_index_t *idx);

/*** inline functions ****************************************************************************/

#endif /* MC__CTAGS_INDEX_H */
