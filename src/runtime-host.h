/** \file src/runtime-host.h
 *  \brief Host services exposed to application-wide runtime extensions
 */

#ifndef MC__RUNTIME_HOST_H
#define MC__RUNTIME_HOST_H

#include "lib/extension-runtime.h"

/*** declarations of public functions ************************************************************/

struct WEdit;
struct WView;

void runtime_host_services_init (void);
void runtime_host_flush_errors (void);
void runtime_host_clear_errors (void);
void runtime_host_set_current_editor (struct WEdit *edit);
void runtime_host_clear_current_editor (struct WEdit *edit);
void runtime_host_set_current_viewer (struct WView *view);
void runtime_host_clear_current_viewer (struct WView *view);

/* Internal editor service entry points.  Kept visible to the editor regression
 * tests; runtime extensions receive them only through the host-services table. */
gboolean runtime_host_editor_insert (const mc_runtime_handle_t *handle, const char *text,
                                     const char **error);
gboolean runtime_host_editor_text (const mc_runtime_handle_t *handle,
                                   const mc_runtime_editor_range_t *range, gboolean has_revision,
                                   guint64 revision, mc_runtime_string_t *text, const char **error);
gboolean runtime_host_editor_edit (const mc_runtime_handle_t *handle,
                                   const mc_runtime_editor_edit_t *edit_spec,
                                   mc_runtime_editor_edit_result_t *result, const char **error);
gboolean runtime_host_editor_replace_selection (const mc_runtime_handle_t *handle, const char *text,
                                                gsize text_length,
                                                mc_runtime_editor_edit_result_t *result,
                                                const char **error);

#endif
