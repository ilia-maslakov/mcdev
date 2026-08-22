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
#define MC_RUNTIME_HOST_CAP_PROCESS      (G_GUINT64_CONSTANT (1) << 7)

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct mc_runtime_plugin_context mc_runtime_plugin_context_t;

typedef enum
{
    MC_RUNTIME_DIALOG_LABEL,
    MC_RUNTIME_DIALOG_INPUT,
    MC_RUNTIME_DIALOG_CHECKBOX,
    MC_RUNTIME_DIALOG_SELECT,
    MC_RUNTIME_DIALOG_SEPARATOR,
    MC_RUNTIME_DIALOG_HBOX,
    MC_RUNTIME_DIALOG_VBOX,
    MC_RUNTIME_DIALOG_SPACER,
    MC_RUNTIME_DIALOG_BUTTON
} mc_runtime_dialog_control_type_t;

typedef struct
{
    const char *id;
    const char *label;
} mc_runtime_dialog_option_t;

typedef struct mc_runtime_dialog_control mc_runtime_dialog_control_t;

struct mc_runtime_dialog_control
{
    mc_runtime_dialog_control_type_t type;
    const char *id;
    const char *text;
    const char *label;
    const char *value;
    guint x, y, width, height;
    gboolean has_x, has_y, has_width, has_height;
    gboolean expand_x, expand_y, default_button, cancel_button, checked;
    const mc_runtime_dialog_option_t *options;
    guint options_count;
    const mc_runtime_dialog_control_t *controls;
    guint controls_count;
};

typedef struct
{
    const char *title;
    guint width, height;
    gboolean has_width, has_height;
    const mc_runtime_dialog_control_t *controls;
    guint controls_count;
} mc_runtime_dialog_t;

typedef struct
{
    const char *id;
    const char *value;
    gboolean checked;
    gboolean is_boolean;
} mc_runtime_dialog_value_t;

typedef struct
{
    char *button_id;
    mc_runtime_dialog_value_t *values;
    guint values_count;
} mc_runtime_dialog_result_t;

typedef struct
{
    char *path;
    gsize path_length;
    char *name;
    gsize name_length;
    gboolean has_path;
    gboolean modified;
    gboolean readonly;
    guint64 revision;
    guint64 byte_length;
    guint64 line_count;
} mc_runtime_editor_info_t;

typedef struct
{
    guint64 offset;
    guint64 line;
    guint64 column;
} mc_runtime_editor_position_t;

typedef struct
{
    guint64 from;
    guint64 to;
} mc_runtime_editor_range_t;

typedef enum
{
    MC_RUNTIME_EDITOR_SELECTION_NONE,
    MC_RUNTIME_EDITOR_SELECTION_LINEAR,
    MC_RUNTIME_EDITOR_SELECTION_COLUMN
} mc_runtime_editor_selection_kind_t;

typedef struct
{
    mc_runtime_editor_selection_kind_t kind;
    guint64 revision;
    mc_runtime_editor_position_t anchor;
    mc_runtime_editor_position_t cursor;
    mc_runtime_editor_range_t *ranges;
    guint ranges_count;
    char *text;
    gsize text_length;
    gboolean has_text;
    gboolean text_truncated;
} mc_runtime_editor_selection_t;

typedef struct
{
    guint64 revision;
    mc_runtime_editor_position_t cursor;
} mc_runtime_editor_edit_result_t;

typedef struct
{
    guint64 from;
    guint64 to;
    const char *text;
    gsize text_length;
} mc_runtime_editor_change_t;

typedef struct
{
    guint64 revision;
    const mc_runtime_editor_change_t *changes;
    guint changes_count;
    gboolean has_cursor;
    mc_runtime_editor_position_t cursor;
} mc_runtime_editor_edit_t;

typedef enum
{
    MC_RUNTIME_ERROR_PHASE_STARTUP,
    MC_RUNTIME_ERROR_PHASE_EVENT,
    MC_RUNTIME_ERROR_PHASE_MACRO
} mc_runtime_error_phase_t;

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
typedef void (*mc_runtime_loaded_runtime_callback_t) (
    const char *runtime_name, const char *display_name, guint abi_version,
    guint64 capability_flags, guint64 required_host_capabilities, gpointer user_data);
typedef void (*mc_runtime_loaded_package_details_callback_t) (
    const char *runtime_name, const char *id, const char *display_name, const char *workspace,
    const char *origin, const char *directory, gboolean enabled, gpointer user_data);
typedef void (*mc_runtime_action_callback_t) (const char *id, const char *label,
                                              const char *shortcut, gpointer user_data);
typedef void (*mc_runtime_loaded_action_callback_t) (const char *runtime_name, const char *id,
                                                     const char *label, const char *shortcut,
                                                     gpointer user_data);
/* A menu action reuses invoke_action().  @menu_path is a stable, untranslated
 * top-level menu name.  Lower @position values are shown first among runtime
 * entries in that menu. */
typedef void (*mc_runtime_menu_action_callback_t) (const char *id, const char *menu_path,
                                                   const char *label, const char *shortcut,
                                                   gint position, gpointer user_data);
typedef void (*mc_runtime_loaded_menu_action_callback_t) (
    const char *runtime_name, const char *id, const char *menu_path, const char *label,
    const char *shortcut, gint position, gpointer user_data);

/* A host-owned byte string.  Runtime extensions must release it with the
   paired string_free callback once they have copied the contents. */
typedef struct
{
    char *data;
    gsize length;
} mc_runtime_string_t;

typedef struct
{
    mc_runtime_string_t out;
    mc_runtime_string_t err;
    int exit_code;   /* -1 when the process did not exit normally */
    int term_signal; /* 0 when the process was not terminated by a signal */
    gboolean out_truncated;
    gboolean err_truncated;
} mc_runtime_process_result_t;

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

    /* Optional v1 extension. The host owns presentation and may defer it
     * until the UI is ready; details is the complete diagnostic. */
    void (*runtime_error) (const char *runtime_name, const char *package_id,
                           mc_runtime_error_phase_t phase, const char *summary,
                           const char *details);

    gboolean (*ui_dialog) (const mc_runtime_dialog_t *dialog, mc_runtime_dialog_result_t *result,
                           const char **error);
    void (*dialog_result_free) (mc_runtime_dialog_result_t *result);

    gboolean (*editor_info) (const mc_runtime_handle_t *editor, mc_runtime_editor_info_t *info,
                             const char **error);
    void (*editor_info_free) (mc_runtime_editor_info_t *info);
    gboolean (*editor_selection) (const mc_runtime_handle_t *editor,
                                  mc_runtime_editor_selection_t *selection, const char **error);
    void (*editor_selection_free) (mc_runtime_editor_selection_t *selection);
    gboolean (*editor_replace_selection) (const mc_runtime_handle_t *editor, const char *text,
                                          gsize text_length,
                                          mc_runtime_editor_edit_result_t *result,
                                          const char **error);
    gboolean (*editor_replace) (const mc_runtime_handle_t *editor, guint64 from, guint64 to,
                                const char *text, gsize text_length,
                                mc_runtime_editor_edit_result_t *result, const char **error);
    gboolean (*process_run_shell) (const char *command, gsize max_output,
                                   mc_runtime_process_result_t *result, const char **error);
    void (*process_result_free) (mc_runtime_process_result_t *result);

    /* Optional v1 extension. Requests repaint after a persistent runtime UI
     * indicator has changed. @area is a stable, untranslated UI area name. */
    void (*ui_refresh) (const char *area);

    /* Optional v1 extension. Returns the configured editor tab stop width. */
    gboolean (*editor_tab_width) (const mc_runtime_handle_t *editor, guint *tab_width,
                                  const char **error);

    /* Revision-aware editor operations.  These are append-only v1 additions;
     * the earlier helpers remain available for source and binary compatibility. */
    gboolean (*editor_text) (const mc_runtime_handle_t *editor,
                             const mc_runtime_editor_range_t *range, gboolean has_revision,
                             guint64 revision, mc_runtime_string_t *text, const char **error);
    gboolean (*editor_edit) (const mc_runtime_handle_t *editor,
                             const mc_runtime_editor_edit_t *edit,
                             mc_runtime_editor_edit_result_t *result, const char **error);
    gboolean (*editor_replace_selection_v2) (const mc_runtime_handle_t *editor, guint64 revision,
                                             const char *text, gsize text_length,
                                             mc_runtime_editor_edit_result_t *result,
                                             const char **error);
    gboolean (*ui_text_width) (const char *text, gsize text_length, guint *width,
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

    /* Optional v1 extension, appended for ABI compatibility. */
    void (*runtime_error) (mc_runtime_plugin_context_t *context, const char *runtime_name,
                           const char *package_id, mc_runtime_error_phase_t phase,
                           const char *summary, const char *details);

    gboolean (*ui_dialog) (mc_runtime_plugin_context_t *context, const mc_runtime_dialog_t *dialog,
                           mc_runtime_dialog_result_t *result, const char **error);
    void (*dialog_result_free) (mc_runtime_plugin_context_t *context,
                                mc_runtime_dialog_result_t *result);

    gboolean (*editor_info) (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor, mc_runtime_editor_info_t *info,
                             const char **error);
    void (*editor_info_free) (mc_runtime_plugin_context_t *context,
                              mc_runtime_editor_info_t *info);
    gboolean (*editor_selection) (mc_runtime_plugin_context_t *context,
                                  const mc_runtime_handle_t *editor,
                                  mc_runtime_editor_selection_t *selection, const char **error);
    void (*editor_selection_free) (mc_runtime_plugin_context_t *context,
                                   mc_runtime_editor_selection_t *selection);
    gboolean (*editor_replace_selection) (mc_runtime_plugin_context_t *context,
                                          const mc_runtime_handle_t *editor, const char *text,
                                          gsize text_length,
                                          mc_runtime_editor_edit_result_t *result,
                                          const char **error);
    gboolean (*editor_replace) (mc_runtime_plugin_context_t *context,
                                const mc_runtime_handle_t *editor, guint64 from, guint64 to,
                                const char *text, gsize text_length,
                                mc_runtime_editor_edit_result_t *result, const char **error);
    gboolean (*process_run_shell) (mc_runtime_plugin_context_t *context, const char *command,
                                   gsize max_output, mc_runtime_process_result_t *result,
                                   const char **error);
    void (*process_result_free) (mc_runtime_plugin_context_t *context,
                                 mc_runtime_process_result_t *result);

    /* Optional v1 extension. Indicators are owned by runtime package @owner;
     * setting the same owner/area/id replaces it. */
    gboolean (*ui_indicator_set) (mc_runtime_plugin_context_t *context, const char *owner,
                                  const char *area, const char *id, const char *text,
                                  gint priority, const char **error);
    gboolean (*ui_indicator_clear) (mc_runtime_plugin_context_t *context, const char *owner,
                                    const char *area, const char *id, const char **error);
    void (*ui_indicators_clear_owner) (mc_runtime_plugin_context_t *context, const char *owner);

    gboolean (*editor_tab_width) (mc_runtime_plugin_context_t *context,
                                  const mc_runtime_handle_t *editor, guint *tab_width,
                                  const char **error);

    gboolean (*editor_text) (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor,
                             const mc_runtime_editor_range_t *range, gboolean has_revision,
                             guint64 revision, mc_runtime_string_t *text, const char **error);
    gboolean (*editor_edit) (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor,
                             const mc_runtime_editor_edit_t *edit,
                             mc_runtime_editor_edit_result_t *result, const char **error);
    gboolean (*editor_replace_selection_v2) (
        mc_runtime_plugin_context_t *context, const mc_runtime_handle_t *editor, guint64 revision,
        const char *text, gsize text_length, mc_runtime_editor_edit_result_t *result,
        const char **error);
    gboolean (*ui_text_width) (mc_runtime_plugin_context_t *context, const char *text,
                               gsize text_length, guint *width, const char **error);
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

    /* Optional v1 extension. Actions are registered callbacks, not package
     * entry files to be evaluated a second time. */
    void (*enumerate_actions) (mc_runtime_plugin_context_t *context, const char *workspace,
                               mc_runtime_action_callback_t callback, gpointer user_data);
    gboolean (*invoke_action) (mc_runtime_plugin_context_t *context, const char *workspace,
                               const char *action_id, const char **error);

    /* Optional append-only v1 extension.  Menu entries invoke the same named
     * actions exposed through invoke_action(). */
    void (*enumerate_menu_actions) (mc_runtime_plugin_context_t *context, const char *workspace,
                                    mc_runtime_menu_action_callback_t callback,
                                    gpointer user_data);

    /* Optional append-only display metadata. */
    const char *display_name;
} mc_runtime_plugin_descriptor_v1_t;

typedef const mc_runtime_plugin_descriptor_v1_t *(*mc_runtime_plugin_register_v1_fn) (void);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

gboolean mc_runtime_plugins_load (GError **mcerror);
void mc_runtime_plugins_shutdown (void);
gboolean mc_runtime_plugins_are_loaded (void);
guint mc_runtime_plugins_count (void);
void mc_runtime_plugins_enumerate_runtimes (mc_runtime_loaded_runtime_callback_t callback,
                                            gpointer user_data);
void mc_runtime_plugins_disable (const char *runtime_name);
void mc_runtime_plugins_set_host_services (const mc_runtime_host_services_v1_t *services);

/* Host-side rendering helper. The caller owns the returned UTF-8 string. */
char *mc_runtime_ui_indicators_compose (const char *area, int max_width);
void mc_runtime_plugins_enumerate_packages (mc_runtime_loaded_package_callback_t callback,
                                            gpointer user_data);
void
mc_runtime_plugins_enumerate_package_details (mc_runtime_loaded_package_details_callback_t callback,
                                              gpointer user_data);
void mc_runtime_plugins_enumerate_actions (const char *workspace,
                                           mc_runtime_loaded_action_callback_t callback,
                                           gpointer user_data);
gboolean mc_runtime_plugins_invoke_action (const char *runtime_name, const char *workspace,
                                           const char *action_id, const char **error);
void mc_runtime_plugins_enumerate_menu_actions (
    const char *workspace, mc_runtime_loaded_menu_action_callback_t callback, gpointer user_data);

#ifdef HAVE_TESTS
void mc_runtime_plugins_set_directory_for_tests (const char *directory);
#endif

/*** inline functions ****************************************************************************/

#endif
