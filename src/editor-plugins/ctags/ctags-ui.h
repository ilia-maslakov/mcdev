/** \file ctags-ui.h
 *  \brief Header: ctags UI dialogs
 */

#ifndef MC__CTAGS_UI_H
#define MC__CTAGS_UI_H

#include "lib/global.h"

#include "ctags-parser.h"
#include "ctags-repository.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/* One display row in the members dialog.  May be a real entry or a synthetic group header. */
typedef struct
{
    const ctags_entry_t *original; /* entry to jump to; NULL for non-jumpable group header */
    char *display_name;            /* owned; indented for children, "scope.child" format */
    char *note;                    /* owned; extracted from excmd comment or synthesized */
    gboolean is_group;             /* TRUE for anonymous struct group row */
} ctags_member_row_t;

void ctags_member_row_free (ctags_member_row_t *row);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Show a filterable selection list of ctags entries.
 * @title: dialog title.
 * @entries: GPtrArray<ctags_entry_t*> to choose from.
 * @initial_filter: pre-fill the filter input (may be NULL).
 * Returns the selected entry, or NULL if cancelled. */
ctags_entry_t *ctags_ui_select (const char *title, GPtrArray *entries, const char *initial_filter);

/* Prompt for a symbol name (with history) and return it, or NULL if cancelled. */
char *ctags_ui_prompt_symbol (const char *initial);

/* Prompt for a file name/path and return it, or NULL if cancelled. */
char *ctags_ui_prompt_file (const char *initial);

/* Prompt for a scope/class name and return it, or NULL if cancelled. */
char *ctags_ui_prompt_scope (const char *initial);

/* Prompt for a tags file path and return it, or NULL if cancelled. */
char *ctags_ui_prompt_tags_path (void);

/* Show the "Manage repositories" dialog.
 * @repos: current list of ctags_repo_t* (may be modified).
 * Returns TRUE if repos list changed. */
gboolean ctags_ui_manage_repos (GSList **repos);

/* Show grep references in a three-column table: Code | FileName | Line.
 * @entries: GPtrArray<ctags_entry_t*> of grep results.
 * Returns the selected entry, or NULL if cancelled. */
ctags_entry_t *ctags_ui_select_refs (const char *title, GPtrArray *entries);

/* Show a members dialog.  @rows is GPtrArray<ctags_member_row_t*> built by the caller.
 * Returns the selected entry, or NULL if cancelled or a group header is chosen. */
ctags_entry_t *ctags_ui_select_members (const char *scope, GPtrArray *rows);

/* Set the root directory used to resolve relative paths in the code preview panel.
 * Call before ctags_ui_select(); the pointer is borrowed and must remain valid for the
 * duration of the dialog. */
void ctags_ui_set_root_dir (const char *root_dir);

/*** inline functions ****************************************************************************/

#endif /* MC__CTAGS_UI_H */
