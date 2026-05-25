/** \file mongo_wizard.h
 *  \brief MongoDB plugin: filter-builder wizard (rule list -> JSON).
 */

#ifndef MC_PANEL_MONGO_WIZARD_H
#define MC_PANEL_MONGO_WIZARD_H

#include "lib/global.h"

/* Fetch distinct values for @field (caller g_strfreev), or NULL on error with
   @err_out set. @capped_out is set TRUE when the result was truncated. */
typedef char **(*mongo_wizard_values_fn) (gpointer ctx, const char *field, gboolean *capped_out,
                                          char **err_out);

/* Run the filter-builder wizard. @fields is an optional NULL-terminated list
   of schema field names (not owned) offered for the Field name input; pass
   NULL when none are known. @values_fn (optional) is invoked to list distinct
   values of a field for the value picker. @initial_json (optional) is the
   current Filter contents; if it matches the wizard's own simple shape it is
   imported back into editable rules, otherwise it is ignored. Returns a
   newly-allocated JSON string (caller g_free) to replace the Filter field, or
   NULL if cancelled or empty. */
char *mongo_wizard_run (const char *const *fields, mongo_wizard_values_fn values_fn,
                        gpointer values_ctx, const char *initial_json);

/* Build a filter scaffold from one sampled document: its top-level scalar
   fields become an == match on each (nested objects/arrays skipped). Returns a
   newly-allocated JSON string (caller g_free), or NULL if nothing usable. */
char *mongo_wizard_doc_to_filter (const char *sample_doc_json);

#endif
