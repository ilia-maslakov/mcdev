/** \file editor-plugin.h
 *  \brief Header: editor plugin API for mcedit extensions
 */

#ifndef MC__EDITOR_PLUGIN_H
#define MC__EDITOR_PLUGIN_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define MC_EDITOR_PLUGIN_API_VERSION 4
#define MC_EDITOR_PLUGIN_ENTRY       "mc_editor_plugin_register"
#define MC_EDITOR_PLUGIN_CMD_BASE    30000L /* Plugins-menu: base + plugin_index */
#define MC_EDITOR_PLUGIN_ACTION_BASE 31000L /* per-action menu commands           */
#define MC_EDITOR_PLUGIN_ACTIONS_MAX 256    /* max named actions per plugin        */
#ifndef MC_EDITOR_PLUGINS_DIR
#define MC_EDITOR_PLUGINS_DIR "/usr/lib/mc/editor-plugins"
#endif

/* Well-known target menu names for mc_ep_cmd_menu_entry_t.menu_name */
#define MC_EP_MENU_COMMAND  "Command"
#define MC_EP_MENU_NAVIGATE "Navigate"

/*** enums ***************************************************************************************/

typedef enum
{
    MC_EPR_OK = 0,
    MC_EPR_FAILED = -1,
    MC_EPR_NOT_SUPPORTED = -2
} mc_ep_result_t;

typedef enum
{
    MC_EPF_NONE = 0,
    MC_EPF_HAS_MENU = 1 << 0
} mc_ep_flags_t;

typedef struct mc_ep_state_t
{
    gboolean available;
    gboolean enabled;
    const char *reason;
} mc_ep_state_t;

/*** enums ***************************************************************************************/

/* Event IDs for handle_event() */
#define MC_EP_EVENT_FILE_SAVED   1 /* file written to disk; payload = const char *path */
#define MC_EP_EVENT_FILE_RENAMED 2 /* file renamed via Save As; payload = const char *new_path */
#define MC_EP_EVENT_FOCUS_IN     3 /* editor window got focus */
#define MC_EP_EVENT_FOCUS_OUT    4 /* editor window lost focus */

/*** structures declarations (and typedefs of structures)*****************************************/

/* What mcedit provides to a plugin */
typedef struct mc_editor_host_t
{
    /* v2 */
    void (*redraw) (struct mc_editor_host_t *host);
    void (*message) (struct mc_editor_host_t *host, int flags, const char *title, const char *text);
    void *host_data; /* opaque, owned by editor core */

    /* v3: generic editor service callbacks.
     * Strings returned by get_* are allocated by core, freed by plugin with g_free().
     * Strings passed to jump_to / insert_text are owned by the plugin. */

    /* Word under cursor.  NULL if not on a word. */
    char *(*get_cursor_word) (struct mc_editor_host_t *host, void *edit);
    /* Absolute path of currently edited file.  NULL if no file is open. */
    char *(*get_current_file) (struct mc_editor_host_t *host, void *edit);
    /* Current cursor line, 1-based.  0 if unavailable. */
    long (*get_cursor_line) (struct mc_editor_host_t *host, void *edit);
    /* Open file at line, recording current position in the navigation stack. */
    gboolean (*jump_to) (struct mc_editor_host_t *host, void *edit, const char *file, long line);
    /* Insert text at cursor, first removing remove_before bytes backwards. */
    void (*insert_text) (struct mc_editor_host_t *host, void *edit, const char *text,
                         gsize remove_before);
} mc_editor_host_t;

/* A named action a plugin exposes for menu or keyboard use.
 * Invoked directly (not via activate/handle_action). */
typedef struct
{
    const char *label; /* action description */
    mc_ep_result_t (*callback) (void *plugin_data, void *edit);
} mc_ep_action_t;

/* An entry injected into a named editor top-level menu.
 * label = NULL means a separator (action_index and shortcut are ignored). */
typedef struct
{
    const char *menu_name; /* MC_EP_MENU_NAVIGATE, MC_EP_MENU_COMMAND, etc. */
    const char *label;     /* translatable menu item text with & accelerator; NULL = separator */
    int action_index;      /* index into mc_editor_plugin_t.actions[] */
    const char *shortcut;  /* shortcut text shown right-aligned in the menu; NULL = none */
} mc_ep_cmd_menu_entry_t;

/* What a plugin provides (callback table) */
typedef struct mc_editor_plugin_t
{
    int api_version;          /* MC_EDITOR_PLUGIN_API_VERSION */
    const char *name;         /* plugin id: "mail", "lsp" */
    const char *display_name; /* UI label */
    mc_ep_flags_t flags;

    /* Required */
    void *(*open) (mc_editor_host_t *host, void *editor_dialog /* opaque */);
    void (*close) (void *plugin_data);

    /* Optional */
    mc_ep_result_t (*activate) (void *plugin_data, void *edit /* opaque */);
    mc_ep_result_t (*configure) (void *plugin_data, void *edit /* opaque */);
    mc_ep_result_t (*handle_action) (void *plugin_data, long command, void *edit /* opaque */);
    mc_ep_result_t (*query_state) (void *plugin_data, void *edit /* opaque */,
                                   mc_ep_state_t *state);
    mc_ep_result_t (*handle_key) (void *plugin_data, int key, void *edit /* opaque */);
    mc_ep_result_t (*handle_event) (void *plugin_data, void *edit /* opaque */, int event_id,
                                    void *payload);
    mc_ep_result_t (*on_file_open) (void *plugin_data, void *edit /* opaque */);
    mc_ep_result_t (*on_file_close) (void *plugin_data, void *edit /* opaque */);

    /* v4: named actions and top-level menu entries */
    const mc_ep_action_t *actions; /* NULL = none */
    int action_count;
    const mc_ep_cmd_menu_entry_t *cmd_menu_entries; /* NULL = none */
    int cmd_menu_entry_count;

    /* v4: resolve current shortcut text for action_index at menu-build time.
     * Returns a newly-allocated string (caller frees with g_free) or NULL.
     * NULL falls back to cmd_menu_entries[].shortcut.  May be NULL. */
    char *(*get_menu_shortcut) (int action_index);
} mc_editor_plugin_t;

typedef const mc_editor_plugin_t *(*mc_editor_plugin_register_fn) (void);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

/* Registry */
gboolean mc_editor_plugin_add (const mc_editor_plugin_t *plugin);
const GSList *mc_editor_plugin_list (void);
const mc_editor_plugin_t *mc_editor_plugin_find_by_name (const char *name);

/* Loader */
void mc_editor_plugins_load (void);
void mc_editor_plugins_shutdown (void);

/*** inline functions ****************************************************************************/

#endif /* MC__EDITOR_PLUGIN_H */
