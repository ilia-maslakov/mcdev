/** \file ctags-repository.h
 *  \brief Header: ctags repository management
 */

#ifndef MC__CTAGS_REPOSITORY_H
#define MC__CTAGS_REPOSITORY_H

#include "lib/global.h"

#include "ctags-parser.h"
#include "ctags-index.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

#define CTAGS_HISTORY_MAX 50

typedef struct
{
    char *tags_path;    /* absolute path to the tags file */
    char *root_dir;     /* directory containing the tags file */
    GPtrArray *entries; /* owned ctags_entry_t* */
    ctags_index_t *index;
    GPtrArray *nav_history; /* owned ctags_entry_t*, oldest at [0], newest at [len-1] */
} ctags_repo_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Resolve the effective line number for @e in @file_path.
 * Uses e->line if set; otherwise scans the file to match e->excmd pattern.
 * Returns 1 as fallback (never 0). */
long ctags_entry_resolve_line (const ctags_entry_t *e, const char *file_path);

/* Load a tags file and build indexes.  Returns NULL on failure. */
ctags_repo_t *ctags_repo_load (const char *tags_path);

void ctags_repo_free (ctags_repo_t *repo);

/* Search parent directories of @from_path for a file named "tags".
 * Returns an allocated path or NULL if not found. */
char *ctags_repo_discover (const char *from_path);

/* Return TRUE when @file lies inside the @root directory tree, with proper path-component
 * boundary (so "/tmp/proj2/a" is not considered to be under "/tmp/proj"). */
gboolean ctags_path_is_under (const char *file, const char *root);

/* Find a loaded repo that contains @file_path.  Returns borrowed pointer or NULL. */
ctags_repo_t *ctags_repos_find_for_file (GSList *repos, const char *file_path);

/* Return all unique results for an exact symbol name across all repos.
 * Returns new GPtrArray (free with g_ptr_array_free(r, FALSE)), or NULL. */
GPtrArray *ctags_repos_find_exact (GSList *repos, const char *name);

/* Return all prefix matches across all repos.
 * Returns new GPtrArray (free with g_ptr_array_free(r, FALSE)), or NULL. */
GPtrArray *ctags_repos_find_prefix (GSList *repos, const char *prefix, gboolean ci);

/* Return file matches by basename/path suffix across all repos.
 * Returns new GPtrArray or NULL. */
GPtrArray *ctags_repos_find_basename (GSList *repos, const char *name);

/* Prepend a navigation entry to @repo's history, trim to CTAGS_HISTORY_MAX, and persist.
 * @name may be NULL.  @file must be non-NULL and @line > 0. */
void ctags_repo_history_push (ctags_repo_t *repo, const char *name, const char *file, long line);

/*** inline functions ****************************************************************************/

#endif /* MC__CTAGS_REPOSITORY_H */
