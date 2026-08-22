/** \file lib/runtime-events.h
 *  \brief Header: typed events for runtime extensions
 */

#ifndef MC__RUNTIME_EVENTS_H
#define MC__RUNTIME_EVENTS_H

#include "lib/global.h"

/*** typedefs(not structures) and defined constants **********************************************/

#define MCEVENT_GROUP_RUNTIME                   "Runtime"

#define MCEVENT_RUNTIME_STARTUP                 "startup"
#define MCEVENT_RUNTIME_SHUTDOWN                "shutdown"
#define MCEVENT_RUNTIME_PANEL_CHDIR             "panel.chdir"
#define MCEVENT_RUNTIME_PANEL_SELECTION_CHANGED "panel.selection_changed"
#define MCEVENT_RUNTIME_PANEL_FILE_OPEN         "panel.file_open"
#define MCEVENT_RUNTIME_EDITOR_OPEN             "editor.open"
#define MCEVENT_RUNTIME_EDITOR_SAVE             "editor.save"
#define MCEVENT_RUNTIME_EDITOR_KEY              "editor.key"
#define MCEVENT_RUNTIME_VIEWER_OPEN             "viewer.open"

#define MC_RUNTIME_EVENT_SELECTED_LIMIT         4096

/*** enums ***************************************************************************************/

typedef enum
{
    MC_RUNTIME_EVENT_INVALID = 0,
    MC_RUNTIME_EVENT_STARTUP,
    MC_RUNTIME_EVENT_SHUTDOWN,
    MC_RUNTIME_EVENT_PANEL_CHDIR,
    MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED,
    MC_RUNTIME_EVENT_PANEL_FILE_OPEN,
    MC_RUNTIME_EVENT_EDITOR_OPEN,
    MC_RUNTIME_EVENT_EDITOR_SAVE,
    MC_RUNTIME_EVENT_EDITOR_KEY,
    MC_RUNTIME_EVENT_VIEWER_OPEN,
    MC_RUNTIME_EVENT_COUNT
} mc_runtime_event_id_t;

typedef enum
{
    MC_RUNTIME_HANDLE_INVALID = 0,
    MC_RUNTIME_HANDLE_PANEL,
    MC_RUNTIME_HANDLE_EDITOR,
    MC_RUNTIME_HANDLE_VIEWER
} mc_runtime_handle_kind_t;

typedef enum
{
    MC_RUNTIME_EVENT_PASS = 0,
    MC_RUNTIME_EVENT_CONSUME,
    MC_RUNTIME_EVENT_ERROR
} mc_runtime_event_result_t;

/*** structures declarations (and typedefs of structures)*****************************************/

typedef struct
{
    mc_runtime_handle_kind_t kind;
    guint64 id;
    guint64 generation;
} mc_runtime_handle_t;

typedef struct
{
    char *name;
    char *path;
    gboolean is_dir;
    guint64 size;
    gint64 mtime;
    gboolean marked;
} mc_runtime_file_snapshot_t;

typedef struct
{
    char *name;
    int code;
    char *text;
    gboolean shift;
    gboolean ctrl;
    gboolean alt;
} mc_runtime_key_snapshot_t;

typedef struct
{
    mc_runtime_event_id_t event_id;
    gboolean consumed;

    union
    {
        struct
        {
            char *run_mode;
            char *config_dir;
            char *data_dir;
        } startup;

        struct
        {
            char *reason;
        } shutdown;

        struct
        {
            mc_runtime_handle_t panel;
            char *old_path;
            char *new_path;
            char *cause;
        } panel_chdir;

        struct
        {
            mc_runtime_handle_t panel;
            mc_runtime_file_snapshot_t *current;
            GPtrArray *selected;
            guint selected_count;
            gboolean selected_truncated;
        } panel_selection_changed;

        struct
        {
            mc_runtime_handle_t panel;
            char *path;
            char *open_mode;
            gboolean is_dir;
        } panel_file_open;

        struct
        {
            mc_runtime_handle_t editor;
            char *path;
            gboolean readonly;
            guint line;
            guint column;
        } editor_open;

        struct
        {
            mc_runtime_handle_t editor;
            char *path;
            char *previous_path;
            gboolean save_as;
        } editor_save;

        struct
        {
            mc_runtime_handle_t editor;
            mc_runtime_key_snapshot_t key;
        } editor_key;

        struct
        {
            mc_runtime_handle_t viewer;
            char *path;
            char *source_kind;
            guint start_line;
        } viewer_open;
    } data;
} mc_runtime_event_snapshot_t;

typedef guint64 mc_runtime_subscription_t;

typedef mc_runtime_event_result_t (*mc_runtime_event_callback_t) (
    gpointer runtime_context, const mc_runtime_event_snapshot_t *snapshot, gpointer user_data);

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

gboolean mc_runtime_events_init (GError **mcerror);
void mc_runtime_events_deinit (void);
gboolean mc_runtime_events_is_initialized (void);

const char *mc_runtime_event_name (mc_runtime_event_id_t event_id);
mc_runtime_event_id_t mc_runtime_event_id_from_name (const char *event_name);
gboolean mc_runtime_event_name_is_valid (const char *event_name);

gboolean mc_runtime_handle_is_valid (const mc_runtime_handle_t *handle);
mc_runtime_handle_t mc_runtime_handle_for_object (mc_runtime_handle_kind_t kind, gpointer object);
void mc_runtime_handle_invalidate_object (mc_runtime_handle_kind_t kind, gpointer object);
gpointer mc_runtime_handle_resolve (const mc_runtime_handle_t *handle,
                                    mc_runtime_handle_kind_t expected_kind);

mc_runtime_file_snapshot_t *mc_runtime_file_snapshot_new (void);
mc_runtime_file_snapshot_t *mc_runtime_file_snapshot_copy (const mc_runtime_file_snapshot_t *file);
void mc_runtime_file_snapshot_free (mc_runtime_file_snapshot_t *file);

mc_runtime_event_snapshot_t *mc_runtime_event_snapshot_new (mc_runtime_event_id_t event_id);
mc_runtime_event_snapshot_t *
mc_runtime_event_snapshot_copy (const mc_runtime_event_snapshot_t *snapshot);
void mc_runtime_event_snapshot_free (mc_runtime_event_snapshot_t *snapshot);
gboolean mc_runtime_event_snapshot_validate (const mc_runtime_event_snapshot_t *snapshot,
                                             GError **mcerror);

mc_runtime_subscription_t
mc_runtime_events_subscribe (gpointer runtime_context, mc_runtime_event_id_t event_id, int priority,
                             mc_runtime_event_callback_t callback, gpointer user_data,
                             GDestroyNotify user_data_destroy, GError **mcerror);
gboolean mc_runtime_events_unsubscribe (gpointer runtime_context,
                                        mc_runtime_subscription_t subscription);
void mc_runtime_events_unsubscribe_all (gpointer runtime_context);

gboolean mc_runtime_event_publish (mc_runtime_event_snapshot_t *snapshot, GError **mcerror);

/*** inline functions ****************************************************************************/

#endif
