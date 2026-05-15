/** \file ctags-fuzzy.h
 *  \brief Header: fuzzy symbol matching
 */

#ifndef MC__CTAGS_FUZZY_H
#define MC__CTAGS_FUZZY_H

#include "lib/global.h"

#include "ctags-parser.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Returns TRUE if every character of @query appears in @name in order (case-insensitive).
 * Example: query="wid" matches "Width", "fillWidth", "WorkingId". */
gboolean ctags_fuzzy_match (const char *name, const char *query);

/* Returns a quality score for the fuzzy match (higher = better fit).
 * Returns 0 if query does not match name at all.
 * Bonuses: consecutive run of matched chars, match at word boundary (start, after '_',
 * after lowercase->uppercase transition). */
int ctags_fuzzy_score (const char *name, const char *query);

/* Search @entries for fuzzy matches against @query.
 * Returns a new GPtrArray of borrowed ctags_entry_t* pointers sorted by score descending,
 * or NULL if nothing matches.  Free with g_ptr_array_free(result, FALSE). */
GPtrArray *ctags_fuzzy_search (const GPtrArray *entries, const char *query);

/*** inline functions ****************************************************************************/

#endif /* MC__CTAGS_FUZZY_H */
