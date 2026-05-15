/** \file ctags-parser.h
 *  \brief Header: ctags file parser
 */

#ifndef MC__CTAGS_PARSER_H
#define MC__CTAGS_PARSER_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/* Single entry from a ctags file */
typedef struct
{
    char *name;       /* symbol name */
    char *file;       /* file path as written in tags (may be relative) */
    char *excmd;      /* ex command: regex pattern or NULL */
    long line;        /* 1-based line number; 0 if unknown */
    char kind;        /* kind letter: f=function, c=class, v=variable, etc. '\0'=unknown */
    char *scope;      /* scope field value, e.g. "Foo" for class:Foo */
    char *scope_kind; /* scope field name, e.g. "class", "namespace" */
    char *signature;  /* function signature, may be NULL */
    char *typeref;    /* type reference, may be NULL */
} ctags_entry_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Parse a ctags-format tags file.  Appends ctags_entry_t* into @out.
 * @out must be a GPtrArray with a free function that calls ctags_entry_free.
 * Returns the number of entries added. */
gsize ctags_parse_file (const char *path, GPtrArray *out);

/* Free a single entry. */
void ctags_entry_free (ctags_entry_t *e);

/* Convenience: g_ptr_array free func wrapper. */
void ctags_entry_free_ptr (gpointer p);

/*** inline functions ****************************************************************************/

#endif /* MC__CTAGS_PARSER_H */
