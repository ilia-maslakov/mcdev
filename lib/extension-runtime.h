/** \file lib/extension-runtime.h
 *  \brief Header: public ABI for application runtime extensions
 */

#ifndef MC__EXTENSION_RUNTIME_H
#define MC__EXTENSION_RUNTIME_H

#include "lib/global.h"
#include "lib/runtime-events.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define MC_RUNTIME_PLUGIN_ABI_VERSION    1
#define MC_RUNTIME_PLUGIN_ENTRY_V1       "mc_runtime_plugin_register_v1"

#define MC_RUNTIME_HOST_CAP_EVENTS       (G_GUINT64_CONSTANT (1) << 0)
#define MC_RUNTIME_HOST_CAP_CONTEXT_DATA (G_GUINT64_CONSTANT (1) << 1)
#define MC_RUNTIME_HOST_CAP_UI           (G_GUINT64_CONSTANT (1) << 2)
#define MC_RUNTIME_HOST_CAP_LOG          (G_GUINT64_CONSTANT (1) << 3)
#define MC_RUNTIME_HOST_CAP_PANEL        (G_GUINT64_CONSTANT (1) << 4)
#define MC_RUNTIME_HOST_CAP_EDITOR       (G_GUINT64_CONSTANT (1) << 5)
#define MC_RUNTIME_HOST_CAP_VIEWER       (G_GUINT64_CONSTANT (1) << 6)

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct mc_runtime_plugin_context mc_runtime_plugin_context_t;

typedef void (*mc_runtime_package_callback_t) (const char *id, const char *display_name,
                                               gboolean enabled, gpointer user_data);
/* Metadata strings remain owned by the runtime and are valid only during the
 * callback.  @workspace identifies the MC workspace served by the package
 * (for example "mcedit"); @origin is runtime-defined (Lua uses "global" and
 * "user"). */
typedef void (*mc_runtime_package_details_callback_t) (const char *id, const char *display_name,
                                                       const char *workspace, const char *origin,
                                                       const char *directory, gboolean enabled,
                                                       gpointer user_data);
typedef void (*mc_runtime_loaded_package_callback_t) (const char *runtime_name, const char *id,
                                                      const char *display_name, gboolean enabled,
                                                      gpointer user_data);
typedef void (*mc_runtime_loaded_package_details_callback_t) (
    const char *runtime_name, const char *id, const char *display_name, const char *workspace,
    const char *origin, const char *directory, gboolean enabled, gpointer user_data);

/* A host-owned byte string.  Runtime extensions must release it with the
   paired string_free callback once they have copied the contents. */
typedef struct
{
    char *data;
    gsize length;
} mc_runtime_string_t;

/* Host-owned file snapshots returned by the panel object API.  The snapshots
   contain public data only and are released with file_list_free(). */
typedef struct
{
    mc_runtime_file_snapshot_t **items;
    guint len;
    guint total_count;
    gboolean truncated;
} mc_runtime_file_list_t;

typedef struct
{
    guint abi_version;
    gsize struct_size;

    gboolean (*ui_status) (const char *text);
    gboolean (*ui_message) (const char *title, const char *text);
    void (*log) (const char *source, const char *level, const char *message);

    mc_runtime_handle_t (*panel_active) (void);
    mc_runtime_handle_t (*panel_passive) (void);
    gboolean (*panel_cwd) (const mc_runtime_handle_t *panel, mc_runtime_string_t *path,
                           const char **error);
    gboolean (*panel_current) (const mc_runtime_handle_t *panel, mc_runtime_file_snapshot_t **file,
                               const char **error);
    gboolean (*panel_selected) (const mc_runtime_handle_t *panel, mc_runtime_file_list_t *files,
                                const char **error);
    gboolean (*panel_refresh) (const mc_runtime_handle_t *panel, const char **error);
    gboolean (*panel_chdir) (const mc_runtime_handle_t *panel, const char *path,
                             const char **error);

    mc_runtime_handle_t (*editor_current) (void);
    gboolean (*editor_path) (const mc_runtime_handle_t *editor, mc_runtime_string_t *path,
                             const char **error);
    gboolean (*editor_cursor) (const mc_runtime_handle_t *editor, guint64 *line, guint64 *column,
                               const char **error);
    gboolean (*editor_set_cursor) (const mc_runtime_handle_t *editor, guint64 line, guint64 column,
                                   const char **error);
    gboolean (*editor_is_readonly) (const mc_runtime_handle_t *editor, gboolean *readonly,
                                    const char **error);
    gboolean (*editor_get_text) (const mc_runtime_handle_t *editor, gint64 from, gint64 to,
                                 mc_runtime_string_t *text, const char **error);
    gboolean (*editor_insert) (const mc_runtime_handle_t *editor, const char *text,
                               const char **error);
    gboolean (*editor_save) (const mc_runtime_handle_t *editor, const char **error);

    mc_runtime_handle_t (*viewer_current) (void);
    gboolean (*viewer_path) (const mc_runtime_handle_t *viewer, mc_runtime_string_t *path,
                             const char **error);
    gboolean (*viewer_position) (const mc_runtime_handle_t *viewer, gint64 *offset,
                                 const char **error);
    gboolean (*viewer_goto) (const mc_runtime_handle_t *viewer, gint64 offset, const char **error);
    gboolean (*viewer_mode) (const mc_runtime_handle_t *viewer, mc_runtime_string_t *mode,
                             const char **error);

    void (*string_free) (mc_runtime_string_t *string);
    void (*file_snapshot_free) (mc_runtime_file_snapshot_t *file);
    void (*file_list_free) (mc_runtime_file_list_t *files);

    /* Optional v1 extension, appended for ABI compatibility.  Returns the
     * current editor selection as a byte string, or "no_selection". */
    gboolean (*editor_selected_text) (const mc_runtime_handle_t *editor, mc_runtime_string_t *text,
                                      const char **error);
} mc_runtime_host_services_v1_t;

typedef struct
{
    guint abi_version;
    gsize struct_size;
    guint64 capability_flags;

    mc_runtime_subscription_t (*subscribe) (mc_runtime_plugin_context_t *context,
                                            mc_runtime_event_id_t event_id, int priority,
                                            mc_runtime_event_callback_t callback,
                                            gpointer user_data, GDestroyNotify user_data_destroy,
                                            GError **mcerror);
    gboolean (*unsubscribe) (mc_runtime_plugin_context_t *context,
                             mc_runtime_subscription_t subscription);
    void (*unsubscribe_all) (mc_runtime_plugin_context_t *context);

    gpointer (*context_get_data) (mc_runtime_plugin_context_t *context);
    void (*context_set_data) (mc_runtime_plugin_context_t *context, gpointer data,
                              GDestroyNotify data_destroy);

    gboolean (*ui_status) (mc_runtime_plugin_context_t *context, const char *text);
    gboolean (*ui_message) (mc_runtime_plugin_context_t *context, const char *title,
                            const char *text);
    void (*log) (mc_runtime_plugin_context_t *context, const char *source, const char *level,
                 const char *message);

    mc_runtime_handle_t (*panel_active) (mc_runtime_plugin_context_t *context);
    mc_runtime_handle_t (*panel_passive) (mc_runtime_plugin_context_t *context);
    gboolean (*panel_cwd) (mc_runtime_plugin_context_t *context, const mc_runtime_handle_t *panel,
                           mc_runtime_string_t *path, const char **error);
    gboolean (*panel_current) (mc_runtime_plugin_context_t *context,
                               const mc_runtime_handle_t *panel, mc_runtime_file_snapshot_t **file,
                               const char **error);
    gboolean (*panel_selected) (mc_runtime_plugin_context_t *context,
                                const mc_runtime_handle_t *panel, mc_runtime_file_list_t *files,
                                const char **error);
    gboolean (*panel_refresh) (mc_runtime_plugin_context_t *context,
                               const mc_runtime_handle_t *panel, const char **error);
    gboolean (*panel_chdir) (mc_runtime_plugin_context_t *context, const mc_runtime_handle_t *panel,
                             const char *path, const char **error);

    mc_runtime_handle_t (*editor_current) (mc_runtime_plugin_context_t *context);
    gboolean (*editor_path) (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor, mc_runtime_string_t *path,
                             const char **error);
    gboolean (*editor_cursor) (mc_runtime_plugin_context_t *context,
                               const mc_runtime_handle_t *editor, guint64 *line, guint64 *column,
                               const char **error);
    gboolean (*editor_set_cursor) (mc_runtime_plugin_context_t *context,
                                   const mc_runtime_handle_t *editor, guint64 line, guint64 column,
                                   const char **error);
    gboolean (*editor_is_readonly) (mc_runtime_plugin_context_t *context,
                                    const mc_runtime_handle_t *editor, gboolean *readonly,
                                    const char **error);
    gboolean (*editor_get_text) (mc_runtime_plugin_context_t *context,
                                 const mc_runtime_handle_t *editor, gint64 from, gint64 to,
                                 mc_runtime_string_t *text, const char **error);
    gboolean (*editor_insert) (mc_runtime_plugin_context_t *context,
                               const mc_runtime_handle_t *editor, const char *text,
                               const char **error);
    gboolean (*editor_save) (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor, const char **error);

    mc_runtime_handle_t (*viewer_current) (mc_runtime_plugin_context_t *context);
    gboolean (*viewer_path) (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *viewer, mc_runtime_string_t *path,
                             const char **error);
    gboolean (*viewer_position) (mc_runtime_plugin_context_t *context,
                                 const mc_runtime_handle_t *viewer, gint64 *offset,
                                 const char **error);
    gboolean (*viewer_goto) (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *viewer, gint64 offset, const char **error);
    gboolean (*viewer_mode) (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *viewer, mc_runtime_string_t *mode,
                             const char **error);

    void (*string_free) (mc_runtime_plugin_context_t *context, mc_runtime_string_t *string);
    void (*file_snapshot_free) (mc_runtime_plugin_context_t *context,
                                mc_runtime_file_snapshot_t *file);
    void (*file_list_free) (mc_runtime_plugin_context_t *context, mc_runtime_file_list_t *files);

    /* Optional v1 extension, appended for ABI compatibility. */
    gboolean (*editor_selected_text) (mc_runtime_plugin_context_t *context,
                                      const mc_runtime_handle_t *editor, mc_runtime_string_t *text,
                                      const char **error);
} mc_runtime_host_api_v1_t;

typedef struct
{
    guint abi_version;
    gsize struct_size;
    guint64 capability_flags;
    const char *runtime_name;
    guint64 required_host_capabilities;

    gboolean (*init) (const mc_runtime_host_api_v1_t *host_api,
                      mc_runtime_plugin_context_t *context, GError **mcerror);
    void (*shutdown) (mc_runtime_plugin_context_t *context);

    void (*enumerate_packages) (mc_runtime_plugin_context_t *context,
                                mc_runtime_package_callback_t callback, gpointer user_data);

    /* Optional v1 extension.  Older runtimes may expose only
     * enumerate_packages(); callers then receive NULL metadata fields. */
    void (*enumerate_package_details) (mc_runtime_plugin_context_t *context,
                                       mc_runtime_package_details_callback_t callback,
                                       gpointer user_data);
} mc_runtime_plugin_descriptor_v1_t;

typedef const mc_runtime_plugin_descriptor_v1_t *(*mc_runtime_plugin_register_v1_fn) (void);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

gboolean mc_runtime_plugins_load (GError **mcerror);
void mc_runtime_plugins_shutdown (void);
gboolean mc_runtime_plugins_are_loaded (void);
guint mc_runtime_plugins_count (void);
void mc_runtime_plugins_disable (const char *runtime_name);
void mc_runtime_plugins_set_host_services (const mc_runtime_host_services_v1_t *services);
void mc_runtime_plugins_enumerate_packages (mc_runtime_loaded_package_callback_t callback,
                                            gpointer user_data);
void
mc_runtime_plugins_enumerate_package_details (mc_runtime_loaded_package_details_callback_t callback,
                                              gpointer user_data);

#ifdef HAVE_TESTS
void mc_runtime_plugins_set_directory_for_tests (const char *directory);
#endif

/*** inline functions ****************************************************************************/

#endif
