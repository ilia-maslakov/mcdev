/*
   Typed events for runtime extensions.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/event.h"
#include "lib/util.h"
#include "lib/runtime-events.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define MC_RUNTIME_EVENT_PRIORITY_MIN (-100)
#define MC_RUNTIME_EVENT_PRIORITY_MAX 100
#define MC_RUNTIME_EVENT_MAX_ERRORS   3

/*** file scope type declarations ****************************************************************/

typedef struct mc_runtime_event_subscription
{
    mc_runtime_subscription_t token;
    mc_runtime_event_id_t event_id;
    int priority;
    guint errors;
    guint ref_count;
    gboolean active;
    gpointer runtime_context;
    mc_runtime_event_callback_t callback;
    gpointer user_data;
    GDestroyNotify user_data_destroy;
} mc_runtime_event_subscription_t;

typedef struct
{
    mc_runtime_event_snapshot_t *snapshot;
} mc_runtime_pending_event_t;

typedef struct
{
    mc_runtime_handle_t handle;
    gpointer object;
} mc_runtime_handle_entry_t;

typedef struct
{
    GPtrArray *subscriptions[MC_RUNTIME_EVENT_COUNT];
    gboolean bridge_registered[MC_RUNTIME_EVENT_COUNT];
    guint event_dispatch_depth[MC_RUNTIME_EVENT_COUNT];
    guint dispatch_depth;
    guint64 next_token;
    GHashTable *handles_by_object[MC_RUNTIME_HANDLE_VIEWER + 1];
    GHashTable *handles_by_id[MC_RUNTIME_HANDLE_VIEWER + 1];
    guint64 next_handle_id;
    guint64 next_handle_generation;
    GQueue pending_events;
    gboolean draining_pending_events;
} mc_runtime_events_state_t;

/*** forward declarations (file scope functions) *************************************************/

static gboolean mc_runtime_events_bridge_callback (const gchar *event_group_name,
                                                   const gchar *event_name, gpointer init_data,
                                                   gpointer event_data);

/*** file scope variables ************************************************************************/

static mc_runtime_events_state_t *mc_runtime_events = NULL;

static const char *const mc_runtime_event_names[MC_RUNTIME_EVENT_COUNT] = {
    NULL,
    MCEVENT_RUNTIME_STARTUP,
    MCEVENT_RUNTIME_SHUTDOWN,
    MCEVENT_RUNTIME_PANEL_CHDIR,
    MCEVENT_RUNTIME_PANEL_SELECTION_CHANGED,
    MCEVENT_RUNTIME_PANEL_FILE_OPEN,
    MCEVENT_RUNTIME_EDITOR_OPEN,
    MCEVENT_RUNTIME_EDITOR_SAVE,
    MCEVENT_RUNTIME_EDITOR_KEY,
    MCEVENT_RUNTIME_VIEWER_OPEN,
};

/*** file scope functions ************************************************************************/

static gboolean
mc_runtime_event_string_in_set (const char *value, const char *const *set, gsize set_size)
{
    gsize i;

    if (value == NULL)
        return FALSE;

    for (i = 0; i < set_size; i++)
        if (strcmp (value, set[i]) == 0)
            return TRUE;

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_event_set_error (GError **mcerror, const char *message)
{
    mc_propagate_error (mcerror, 0, "%s", message);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_event_set_error_for_name (GError **mcerror, const char *prefix, const char *name)
{
    mc_propagate_error (mcerror, 0, "%s '%s'", prefix, name != NULL ? name : "(null)");
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_event_handle_has_kind (const mc_runtime_handle_t *handle,
                                  mc_runtime_handle_kind_t expected_kind)
{
    return mc_runtime_handle_is_valid (handle) && handle->kind == expected_kind;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_file_snapshot_validate (const mc_runtime_file_snapshot_t *file, GError **mcerror)
{
    if (file == NULL || file->name == NULL || file->path == NULL)
        return mc_runtime_event_set_error (mcerror, "Invalid runtime file snapshot");

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_key_snapshot_clear (mc_runtime_key_snapshot_t *key)
{
    g_free (key->name);
    g_free (key->text);
    memset (key, 0, sizeof (*key));
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_key_snapshot_copy (mc_runtime_key_snapshot_t *destination,
                              const mc_runtime_key_snapshot_t *source)
{
    destination->name = g_strdup (source->name);
    destination->code = source->code;
    destination->text = g_strdup (source->text);
    destination->shift = source->shift;
    destination->ctrl = source->ctrl;
    destination->alt = source->alt;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_event_snapshot_copy_selected (GPtrArray *destination, const GPtrArray *source)
{
    guint i;

    for (i = 0; i < source->len; i++)
    {
        const mc_runtime_file_snapshot_t *file =
            (const mc_runtime_file_snapshot_t *) g_ptr_array_index ((GPtrArray *) source, i);

        g_ptr_array_add (destination, mc_runtime_file_snapshot_copy (file));
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_event_subscription_ref (mc_runtime_event_subscription_t *subscription)
{
    subscription->ref_count++;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_event_subscription_unref (mc_runtime_event_subscription_t *subscription)
{
    subscription->ref_count--;

    if (subscription->ref_count != 0)
        return;

    if (subscription->user_data_destroy != NULL)
        subscription->user_data_destroy (subscription->user_data);
    g_free (subscription);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_pending_event_free (mc_runtime_pending_event_t *pending_event)
{
    if (pending_event == NULL)
        return;

    mc_runtime_event_snapshot_free (pending_event->snapshot);
    g_free (pending_event);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_handles_equal (const mc_runtime_handle_t *first, const mc_runtime_handle_t *second)
{
    return first->kind == second->kind && first->id == second->id
        && first->generation == second->generation;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_handle_kind_is_valid (mc_runtime_handle_kind_t kind)
{
    return kind > MC_RUNTIME_HANDLE_INVALID && kind <= MC_RUNTIME_HANDLE_VIEWER;
}

/* --------------------------------------------------------------------------------------------- */

static guint64
mc_runtime_next_nonzero_id (guint64 *next_id)
{
    guint64 id;

    id = (*next_id)++;
    if (id == 0)
        id = (*next_id)++;

    return id;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_event_queue (mc_runtime_events_state_t *state,
                        const mc_runtime_event_snapshot_t *snapshot)
{
    mc_runtime_event_snapshot_t *copy;
    GList *link;

    copy = mc_runtime_event_snapshot_copy (snapshot);
    if (copy == NULL)
        return;

    if (snapshot->event_id == MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED)
    {
        for (link = g_queue_peek_head_link (&state->pending_events); link != NULL;
             link = g_list_next (link))
        {
            mc_runtime_pending_event_t *pending_event = (mc_runtime_pending_event_t *) link->data;

            if (pending_event->snapshot->event_id == MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED
                && mc_runtime_handles_equal (
                    &pending_event->snapshot->data.panel_selection_changed.panel,
                    &snapshot->data.panel_selection_changed.panel))
            {
                mc_runtime_event_snapshot_free (pending_event->snapshot);
                pending_event->snapshot = copy;
                return;
            }
        }
    }

    {
        mc_runtime_pending_event_t *pending_event = g_new (mc_runtime_pending_event_t, 1);

        pending_event->snapshot = copy;
        g_queue_push_tail (&state->pending_events, pending_event);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_event_subscription_deactivate (mc_runtime_events_state_t *state,
                                          mc_runtime_event_subscription_t *subscription)
{
    GPtrArray *subscriptions;
    guint i;

    if (!subscription->active)
        return;

    subscriptions = state->subscriptions[subscription->event_id];
    for (i = 0; i < subscriptions->len; i++)
        if (g_ptr_array_index (subscriptions, i) == subscription)
        {
            g_ptr_array_remove_index (subscriptions, i);
            break;
        }

    subscription->active = FALSE;
    mc_runtime_event_subscription_unref (subscription);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_event_dispatch (mc_runtime_events_state_t *state, mc_runtime_event_snapshot_t *snapshot)
{
    GPtrArray *subscriptions;
    GPtrArray *dispatch_list;
    guint i;

    subscriptions = state->subscriptions[snapshot->event_id];
    dispatch_list =
        g_ptr_array_new_with_free_func ((GDestroyNotify) mc_runtime_event_subscription_unref);

    for (i = 0; i < subscriptions->len; i++)
    {
        mc_runtime_event_subscription_t *subscription =
            (mc_runtime_event_subscription_t *) g_ptr_array_index (subscriptions, i);

        mc_runtime_event_subscription_ref (subscription);
        g_ptr_array_add (dispatch_list, subscription);
    }

    state->dispatch_depth++;
    state->event_dispatch_depth[snapshot->event_id]++;

    for (i = 0; i < dispatch_list->len; i++)
    {
        mc_runtime_event_subscription_t *subscription =
            (mc_runtime_event_subscription_t *) g_ptr_array_index (dispatch_list, i);
        mc_runtime_event_result_t result;

        result = subscription->callback (subscription->runtime_context, snapshot,
                                         subscription->user_data);

        if (result == MC_RUNTIME_EVENT_ERROR)
        {
            subscription->errors++;
            if (subscription->errors >= MC_RUNTIME_EVENT_MAX_ERRORS)
                mc_runtime_event_subscription_deactivate (state, subscription);
        }

        if (snapshot->event_id == MC_RUNTIME_EVENT_EDITOR_KEY && result == MC_RUNTIME_EVENT_CONSUME)
        {
            snapshot->consumed = TRUE;
            break;
        }
    }

    state->event_dispatch_depth[snapshot->event_id]--;
    state->dispatch_depth--;
    g_ptr_array_free (dispatch_list, TRUE);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_event_drain_queue (mc_runtime_events_state_t *state)
{
    if (state->draining_pending_events || state->dispatch_depth != 0)
        return;

    state->draining_pending_events = TRUE;

    while (state->dispatch_depth == 0 && !g_queue_is_empty (&state->pending_events))
    {
        mc_runtime_pending_event_t *pending_event =
            (mc_runtime_pending_event_t *) g_queue_pop_head (&state->pending_events);

        mc_runtime_event_dispatch (state, pending_event->snapshot);
        mc_runtime_pending_event_free (pending_event);
    }

    state->draining_pending_events = FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_event_dispatch_or_queue (mc_runtime_events_state_t *state,
                                    mc_runtime_event_snapshot_t *snapshot)
{
    if (state->event_dispatch_depth[snapshot->event_id] != 0)
    {
        mc_runtime_event_queue (state, snapshot);
        return;
    }

    mc_runtime_event_dispatch (state, snapshot);
    mc_runtime_event_drain_queue (state);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_events_bridge_callback (const gchar *event_group_name, const gchar *event_name,
                                   gpointer init_data, gpointer event_data)
{
    mc_runtime_events_state_t *state = (mc_runtime_events_state_t *) init_data;
    mc_runtime_event_snapshot_t *snapshot = (mc_runtime_event_snapshot_t *) event_data;
    mc_runtime_event_id_t event_id;

    if (state == NULL || state != mc_runtime_events
        || strcmp (event_group_name, MCEVENT_GROUP_RUNTIME) != 0)
        return TRUE;

    event_id = mc_runtime_event_id_from_name (event_name);
    if (event_id == MC_RUNTIME_EVENT_INVALID || snapshot == NULL || snapshot->event_id != event_id)
        return TRUE;

    mc_runtime_event_dispatch_or_queue (state, snapshot);

    return !snapshot->consumed;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_events_init (GError **mcerror)
{
    mc_runtime_events_state_t *state;
    mc_runtime_event_id_t event_id;
    mc_runtime_handle_kind_t handle_kind;

    mc_return_val_if_error (mcerror, FALSE);

    if (mc_runtime_events != NULL)
        return mc_runtime_event_set_error (mcerror, "Runtime event system already initialized");

    state = g_new0 (mc_runtime_events_state_t, 1);
    state->next_token = 1;
    state->next_handle_id = 1;
    state->next_handle_generation = 1;
    g_queue_init (&state->pending_events);

    for (event_id = MC_RUNTIME_EVENT_STARTUP; event_id < MC_RUNTIME_EVENT_COUNT; event_id++)
        state->subscriptions[event_id] = g_ptr_array_new ();

    for (handle_kind = MC_RUNTIME_HANDLE_PANEL; handle_kind <= MC_RUNTIME_HANDLE_VIEWER;
         handle_kind++)
    {
        state->handles_by_object[handle_kind] = g_hash_table_new (g_direct_hash, g_direct_equal);
        state->handles_by_id[handle_kind] =
            g_hash_table_new_full (g_int64_hash, g_int64_equal, NULL, g_free);
    }

    mc_runtime_events = state;

    for (event_id = MC_RUNTIME_EVENT_STARTUP; event_id < MC_RUNTIME_EVENT_COUNT; event_id++)
    {
        if (!mc_event_add (MCEVENT_GROUP_RUNTIME, mc_runtime_event_name (event_id),
                           mc_runtime_events_bridge_callback, state, mcerror))
        {
            mc_runtime_events_deinit ();
            return FALSE;
        }
        state->bridge_registered[event_id] = TRUE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_events_deinit (void)
{
    mc_runtime_events_state_t *state = mc_runtime_events;
    mc_runtime_event_id_t event_id;
    mc_runtime_handle_kind_t handle_kind;

    if (state == NULL)
        return;

    for (event_id = MC_RUNTIME_EVENT_STARTUP; event_id < MC_RUNTIME_EVENT_COUNT; event_id++)
    {
        GPtrArray *subscriptions = state->subscriptions[event_id];

        if (state->bridge_registered[event_id])
            mc_event_del (MCEVENT_GROUP_RUNTIME, mc_runtime_event_name (event_id),
                          mc_runtime_events_bridge_callback, state);

        while (subscriptions->len != 0)
        {
            mc_runtime_event_subscription_t *subscription =
                (mc_runtime_event_subscription_t *) g_ptr_array_index (subscriptions, 0);

            mc_runtime_event_subscription_deactivate (state, subscription);
        }

        g_ptr_array_free (subscriptions, TRUE);
    }

    while (!g_queue_is_empty (&state->pending_events))
        mc_runtime_pending_event_free (
            (mc_runtime_pending_event_t *) g_queue_pop_head (&state->pending_events));

    for (handle_kind = MC_RUNTIME_HANDLE_PANEL; handle_kind <= MC_RUNTIME_HANDLE_VIEWER;
         handle_kind++)
    {
        g_hash_table_destroy (state->handles_by_object[handle_kind]);
        g_hash_table_destroy (state->handles_by_id[handle_kind]);
    }

    mc_runtime_events = NULL;
    g_free (state);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_events_is_initialized (void)
{
    return mc_runtime_events != NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
mc_runtime_event_name (mc_runtime_event_id_t event_id)
{
    if (event_id <= MC_RUNTIME_EVENT_INVALID || event_id >= MC_RUNTIME_EVENT_COUNT)
        return NULL;

    return mc_runtime_event_names[event_id];
}

/* --------------------------------------------------------------------------------------------- */

mc_runtime_event_id_t
mc_runtime_event_id_from_name (const char *event_name)
{
    mc_runtime_event_id_t event_id;

    if (event_name == NULL)
        return MC_RUNTIME_EVENT_INVALID;

    for (event_id = MC_RUNTIME_EVENT_STARTUP; event_id < MC_RUNTIME_EVENT_COUNT; event_id++)
        if (strcmp (event_name, mc_runtime_event_names[event_id]) == 0)
            return event_id;

    return MC_RUNTIME_EVENT_INVALID;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_event_name_is_valid (const char *event_name)
{
    return mc_runtime_event_id_from_name (event_name) != MC_RUNTIME_EVENT_INVALID;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_handle_is_valid (const mc_runtime_handle_t *handle)
{
    return handle != NULL && mc_runtime_handle_kind_is_valid (handle->kind) && handle->id != 0
        && handle->generation != 0;
}

/* --------------------------------------------------------------------------------------------- */

mc_runtime_handle_t
mc_runtime_handle_for_object (mc_runtime_handle_kind_t kind, gpointer object)
{
    mc_runtime_events_state_t *state = mc_runtime_events;
    mc_runtime_handle_entry_t *entry;
    mc_runtime_handle_t invalid_handle = { MC_RUNTIME_HANDLE_INVALID, 0, 0 };

    if (state == NULL || !mc_runtime_handle_kind_is_valid (kind) || object == NULL)
        return invalid_handle;

    entry =
        (mc_runtime_handle_entry_t *) g_hash_table_lookup (state->handles_by_object[kind], object);
    if (entry != NULL)
        return entry->handle;

    entry = g_new0 (mc_runtime_handle_entry_t, 1);
    entry->object = object;
    entry->handle.kind = kind;
    entry->handle.id = mc_runtime_next_nonzero_id (&state->next_handle_id);
    entry->handle.generation = mc_runtime_next_nonzero_id (&state->next_handle_generation);

    g_hash_table_insert (state->handles_by_object[kind], object, entry);
    g_hash_table_insert (state->handles_by_id[kind], &entry->handle.id, entry);

    return entry->handle;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_handle_invalidate_object (mc_runtime_handle_kind_t kind, gpointer object)
{
    mc_runtime_events_state_t *state = mc_runtime_events;
    mc_runtime_handle_entry_t *entry;

    if (state == NULL || !mc_runtime_handle_kind_is_valid (kind) || object == NULL)
        return;

    entry =
        (mc_runtime_handle_entry_t *) g_hash_table_lookup (state->handles_by_object[kind], object);
    if (entry == NULL)
        return;

    g_hash_table_remove (state->handles_by_object[kind], object);
    g_hash_table_remove (state->handles_by_id[kind], &entry->handle.id);
}

/* --------------------------------------------------------------------------------------------- */

gpointer
mc_runtime_handle_resolve (const mc_runtime_handle_t *handle,
                           mc_runtime_handle_kind_t expected_kind)
{
    mc_runtime_events_state_t *state = mc_runtime_events;
    mc_runtime_handle_entry_t *entry;

    if (state == NULL || !mc_runtime_handle_is_valid (handle) || handle->kind != expected_kind)
        return NULL;

    entry = (mc_runtime_handle_entry_t *) g_hash_table_lookup (state->handles_by_id[expected_kind],
                                                               &handle->id);
    if (entry == NULL || entry->handle.generation != handle->generation)
        return NULL;

    return entry->object;
}

/* --------------------------------------------------------------------------------------------- */

mc_runtime_file_snapshot_t *
mc_runtime_file_snapshot_new (void)
{
    return g_new0 (mc_runtime_file_snapshot_t, 1);
}

/* --------------------------------------------------------------------------------------------- */

mc_runtime_file_snapshot_t *
mc_runtime_file_snapshot_copy (const mc_runtime_file_snapshot_t *file)
{
    mc_runtime_file_snapshot_t *copy;

    if (file == NULL)
        return NULL;

    copy = mc_runtime_file_snapshot_new ();
    copy->name = g_strdup (file->name);
    copy->path = g_strdup (file->path);
    copy->is_dir = file->is_dir;
    copy->size = file->size;
    copy->mtime = file->mtime;
    copy->marked = file->marked;

    return copy;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_file_snapshot_free (mc_runtime_file_snapshot_t *file)
{
    if (file == NULL)
        return;

    g_free (file->name);
    g_free (file->path);
    g_free (file);
}

/* --------------------------------------------------------------------------------------------- */

mc_runtime_event_snapshot_t *
mc_runtime_event_snapshot_new (mc_runtime_event_id_t event_id)
{
    mc_runtime_event_snapshot_t *snapshot;

    if (mc_runtime_event_name (event_id) == NULL)
        return NULL;

    snapshot = g_new0 (mc_runtime_event_snapshot_t, 1);
    snapshot->event_id = event_id;

    if (event_id == MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED)
        snapshot->data.panel_selection_changed.selected =
            g_ptr_array_new_with_free_func ((GDestroyNotify) mc_runtime_file_snapshot_free);

    return snapshot;
}

/* --------------------------------------------------------------------------------------------- */

mc_runtime_event_snapshot_t *
mc_runtime_event_snapshot_copy (const mc_runtime_event_snapshot_t *snapshot)
{
    mc_runtime_event_snapshot_t *copy;

    if (snapshot == NULL)
        return NULL;

    copy = mc_runtime_event_snapshot_new (snapshot->event_id);
    if (copy == NULL)
        return NULL;

    copy->consumed = snapshot->consumed;

    switch (snapshot->event_id)
    {
    case MC_RUNTIME_EVENT_STARTUP:
        copy->data.startup.run_mode = g_strdup (snapshot->data.startup.run_mode);
        copy->data.startup.config_dir = g_strdup (snapshot->data.startup.config_dir);
        copy->data.startup.data_dir = g_strdup (snapshot->data.startup.data_dir);
        break;

    case MC_RUNTIME_EVENT_SHUTDOWN:
        copy->data.shutdown.reason = g_strdup (snapshot->data.shutdown.reason);
        break;

    case MC_RUNTIME_EVENT_PANEL_CHDIR:
        copy->data.panel_chdir.panel = snapshot->data.panel_chdir.panel;
        copy->data.panel_chdir.old_path = g_strdup (snapshot->data.panel_chdir.old_path);
        copy->data.panel_chdir.new_path = g_strdup (snapshot->data.panel_chdir.new_path);
        copy->data.panel_chdir.cause = g_strdup (snapshot->data.panel_chdir.cause);
        break;

    case MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED:
        copy->data.panel_selection_changed.panel = snapshot->data.panel_selection_changed.panel;
        copy->data.panel_selection_changed.current =
            mc_runtime_file_snapshot_copy (snapshot->data.panel_selection_changed.current);
        copy->data.panel_selection_changed.selected_count =
            snapshot->data.panel_selection_changed.selected_count;
        copy->data.panel_selection_changed.selected_truncated =
            snapshot->data.panel_selection_changed.selected_truncated;
        if (snapshot->data.panel_selection_changed.selected != NULL)
            mc_runtime_event_snapshot_copy_selected (
                copy->data.panel_selection_changed.selected,
                snapshot->data.panel_selection_changed.selected);
        break;

    case MC_RUNTIME_EVENT_PANEL_FILE_OPEN:
        copy->data.panel_file_open.panel = snapshot->data.panel_file_open.panel;
        copy->data.panel_file_open.path = g_strdup (snapshot->data.panel_file_open.path);
        copy->data.panel_file_open.open_mode = g_strdup (snapshot->data.panel_file_open.open_mode);
        copy->data.panel_file_open.is_dir = snapshot->data.panel_file_open.is_dir;
        break;

    case MC_RUNTIME_EVENT_EDITOR_OPEN:
        copy->data.editor_open.editor = snapshot->data.editor_open.editor;
        copy->data.editor_open.path = g_strdup (snapshot->data.editor_open.path);
        copy->data.editor_open.readonly = snapshot->data.editor_open.readonly;
        copy->data.editor_open.line = snapshot->data.editor_open.line;
        copy->data.editor_open.column = snapshot->data.editor_open.column;
        break;

    case MC_RUNTIME_EVENT_EDITOR_SAVE:
        copy->data.editor_save.editor = snapshot->data.editor_save.editor;
        copy->data.editor_save.path = g_strdup (snapshot->data.editor_save.path);
        copy->data.editor_save.previous_path = g_strdup (snapshot->data.editor_save.previous_path);
        copy->data.editor_save.save_as = snapshot->data.editor_save.save_as;
        break;

    case MC_RUNTIME_EVENT_EDITOR_KEY:
        copy->data.editor_key.editor = snapshot->data.editor_key.editor;
        mc_runtime_key_snapshot_copy (&copy->data.editor_key.key, &snapshot->data.editor_key.key);
        break;

    case MC_RUNTIME_EVENT_VIEWER_OPEN:
        copy->data.viewer_open.viewer = snapshot->data.viewer_open.viewer;
        copy->data.viewer_open.path = g_strdup (snapshot->data.viewer_open.path);
        copy->data.viewer_open.source_kind = g_strdup (snapshot->data.viewer_open.source_kind);
        copy->data.viewer_open.start_line = snapshot->data.viewer_open.start_line;
        break;

    case MC_RUNTIME_EVENT_INVALID:
    case MC_RUNTIME_EVENT_COUNT:
        g_assert_not_reached ();
        break;

    default:
        g_assert_not_reached ();
        break;
    }

    return copy;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_event_snapshot_free (mc_runtime_event_snapshot_t *snapshot)
{
    if (snapshot == NULL)
        return;

    switch (snapshot->event_id)
    {
    case MC_RUNTIME_EVENT_STARTUP:
        g_free (snapshot->data.startup.run_mode);
        g_free (snapshot->data.startup.config_dir);
        g_free (snapshot->data.startup.data_dir);
        break;

    case MC_RUNTIME_EVENT_SHUTDOWN:
        g_free (snapshot->data.shutdown.reason);
        break;

    case MC_RUNTIME_EVENT_PANEL_CHDIR:
        g_free (snapshot->data.panel_chdir.old_path);
        g_free (snapshot->data.panel_chdir.new_path);
        g_free (snapshot->data.panel_chdir.cause);
        break;

    case MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED:
        mc_runtime_file_snapshot_free (snapshot->data.panel_selection_changed.current);
        if (snapshot->data.panel_selection_changed.selected != NULL)
            g_ptr_array_free (snapshot->data.panel_selection_changed.selected, TRUE);
        break;

    case MC_RUNTIME_EVENT_PANEL_FILE_OPEN:
        g_free (snapshot->data.panel_file_open.path);
        g_free (snapshot->data.panel_file_open.open_mode);
        break;

    case MC_RUNTIME_EVENT_EDITOR_OPEN:
        g_free (snapshot->data.editor_open.path);
        break;

    case MC_RUNTIME_EVENT_EDITOR_SAVE:
        g_free (snapshot->data.editor_save.path);
        g_free (snapshot->data.editor_save.previous_path);
        break;

    case MC_RUNTIME_EVENT_EDITOR_KEY:
        mc_runtime_key_snapshot_clear (&snapshot->data.editor_key.key);
        break;

    case MC_RUNTIME_EVENT_VIEWER_OPEN:
        g_free (snapshot->data.viewer_open.path);
        g_free (snapshot->data.viewer_open.source_kind);
        break;

    case MC_RUNTIME_EVENT_INVALID:
    case MC_RUNTIME_EVENT_COUNT:
        break;

    default:
        g_assert_not_reached ();
        break;
    }

    g_free (snapshot);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_event_snapshot_validate (const mc_runtime_event_snapshot_t *snapshot, GError **mcerror)
{
    static const char *const panel_chdir_causes[] = { "user",   "command", "history",
                                                      "plugin", "reload",  "other" };
    static const char *const panel_open_modes[] = { "view", "edit", "execute", "other" };
    static const char *const shutdown_reasons[] = { "normal", "quit" };

    mc_return_val_if_error (mcerror, FALSE);

    if (snapshot == NULL || mc_runtime_event_name (snapshot->event_id) == NULL)
        return mc_runtime_event_set_error (mcerror, "Invalid runtime event snapshot");

    switch (snapshot->event_id)
    {
    case MC_RUNTIME_EVENT_STARTUP:
        if (snapshot->data.startup.run_mode == NULL || snapshot->data.startup.config_dir == NULL
            || snapshot->data.startup.data_dir == NULL)
            return mc_runtime_event_set_error (mcerror, "Invalid startup event snapshot");
        break;

    case MC_RUNTIME_EVENT_SHUTDOWN:
        if (!mc_runtime_event_string_in_set (snapshot->data.shutdown.reason, shutdown_reasons,
                                             G_N_ELEMENTS (shutdown_reasons)))
            return mc_runtime_event_set_error (mcerror, "Invalid shutdown event reason");
        break;

    case MC_RUNTIME_EVENT_PANEL_CHDIR:
        if (!mc_runtime_event_handle_has_kind (&snapshot->data.panel_chdir.panel,
                                               MC_RUNTIME_HANDLE_PANEL)
            || snapshot->data.panel_chdir.old_path == NULL
            || snapshot->data.panel_chdir.new_path == NULL
            || !mc_runtime_event_string_in_set (snapshot->data.panel_chdir.cause,
                                                panel_chdir_causes,
                                                G_N_ELEMENTS (panel_chdir_causes)))
            return mc_runtime_event_set_error (mcerror, "Invalid panel.chdir event snapshot");
        break;

    case MC_RUNTIME_EVENT_PANEL_SELECTION_CHANGED:
    {
        const GPtrArray *selected = snapshot->data.panel_selection_changed.selected;
        guint i;

        if (!mc_runtime_event_handle_has_kind (&snapshot->data.panel_selection_changed.panel,
                                               MC_RUNTIME_HANDLE_PANEL)
            || selected == NULL || selected->len > MC_RUNTIME_EVENT_SELECTED_LIMIT
            || snapshot->data.panel_selection_changed.selected_count < selected->len
            || (snapshot->data.panel_selection_changed.selected_count
                    > MC_RUNTIME_EVENT_SELECTED_LIMIT
                && !snapshot->data.panel_selection_changed.selected_truncated))
            return mc_runtime_event_set_error (mcerror,
                                               "Invalid panel.selection_changed event snapshot");

        if (snapshot->data.panel_selection_changed.current != NULL
            && !mc_runtime_file_snapshot_validate (snapshot->data.panel_selection_changed.current,
                                                   mcerror))
            return FALSE;

        for (i = 0; i < selected->len; i++)
            if (!mc_runtime_file_snapshot_validate (
                    (const mc_runtime_file_snapshot_t *) g_ptr_array_index ((GPtrArray *) selected,
                                                                            i),
                    mcerror))
                return FALSE;
        break;
    }

    case MC_RUNTIME_EVENT_PANEL_FILE_OPEN:
        if (!mc_runtime_event_handle_has_kind (&snapshot->data.panel_file_open.panel,
                                               MC_RUNTIME_HANDLE_PANEL)
            || snapshot->data.panel_file_open.path == NULL
            || !mc_runtime_event_string_in_set (snapshot->data.panel_file_open.open_mode,
                                                panel_open_modes, G_N_ELEMENTS (panel_open_modes)))
            return mc_runtime_event_set_error (mcerror, "Invalid panel.file_open event snapshot");
        break;

    case MC_RUNTIME_EVENT_EDITOR_OPEN:
        if (!mc_runtime_event_handle_has_kind (&snapshot->data.editor_open.editor,
                                               MC_RUNTIME_HANDLE_EDITOR)
            || snapshot->data.editor_open.path == NULL)
            return mc_runtime_event_set_error (mcerror, "Invalid editor.open event snapshot");
        break;

    case MC_RUNTIME_EVENT_EDITOR_SAVE:
        if (!mc_runtime_event_handle_has_kind (&snapshot->data.editor_save.editor,
                                               MC_RUNTIME_HANDLE_EDITOR)
            || snapshot->data.editor_save.path == NULL)
            return mc_runtime_event_set_error (mcerror, "Invalid editor.save event snapshot");
        break;

    case MC_RUNTIME_EVENT_EDITOR_KEY:
        if (!mc_runtime_event_handle_has_kind (&snapshot->data.editor_key.editor,
                                               MC_RUNTIME_HANDLE_EDITOR)
            || snapshot->data.editor_key.key.name == NULL)
            return mc_runtime_event_set_error (mcerror, "Invalid editor.key event snapshot");
        break;

    case MC_RUNTIME_EVENT_VIEWER_OPEN:
        if (!mc_runtime_event_handle_has_kind (&snapshot->data.viewer_open.viewer,
                                               MC_RUNTIME_HANDLE_VIEWER)
            || snapshot->data.viewer_open.path == NULL
            || snapshot->data.viewer_open.source_kind == NULL)
            return mc_runtime_event_set_error (mcerror, "Invalid viewer.open event snapshot");
        break;

    case MC_RUNTIME_EVENT_INVALID:
    case MC_RUNTIME_EVENT_COUNT:
        return mc_runtime_event_set_error (mcerror, "Invalid runtime event snapshot");

    default:
        return mc_runtime_event_set_error (mcerror, "Invalid runtime event snapshot");
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

mc_runtime_subscription_t
mc_runtime_events_subscribe (gpointer runtime_context, mc_runtime_event_id_t event_id, int priority,
                             mc_runtime_event_callback_t callback, gpointer user_data,
                             GDestroyNotify user_data_destroy, GError **mcerror)
{
    mc_runtime_events_state_t *state = mc_runtime_events;
    mc_runtime_event_subscription_t *subscription;
    GPtrArray *subscriptions;
    guint i;

    mc_return_val_if_error (mcerror, 0);

    if (state == NULL)
    {
        mc_runtime_event_set_error (mcerror, "Runtime event system is not initialized");
        return 0;
    }

    if (runtime_context == NULL || callback == NULL)
    {
        mc_runtime_event_set_error (mcerror,
                                    "Runtime event subscription requires a context and callback");
        return 0;
    }

    if (mc_runtime_event_name (event_id) == NULL)
    {
        mc_runtime_event_set_error_for_name (mcerror, "Invalid runtime event", NULL);
        return 0;
    }

    if (priority < MC_RUNTIME_EVENT_PRIORITY_MIN || priority > MC_RUNTIME_EVENT_PRIORITY_MAX)
    {
        mc_runtime_event_set_error (mcerror,
                                    "Runtime event priority is outside the supported range");
        return 0;
    }

    subscription = g_new0 (mc_runtime_event_subscription_t, 1);
    subscription->token = state->next_token++;
    if (subscription->token == 0)
        subscription->token = state->next_token++;
    subscription->event_id = event_id;
    subscription->priority = priority;
    subscription->ref_count = 1;
    subscription->active = TRUE;
    subscription->runtime_context = runtime_context;
    subscription->callback = callback;
    subscription->user_data = user_data;
    subscription->user_data_destroy = user_data_destroy;

    subscriptions = state->subscriptions[event_id];
    for (i = 0; i < subscriptions->len; i++)
    {
        const mc_runtime_event_subscription_t *existing =
            (const mc_runtime_event_subscription_t *) g_ptr_array_index (subscriptions, i);

        if (subscription->priority > existing->priority)
        {
            g_ptr_array_insert (subscriptions, i, subscription);
            return subscription->token;
        }
    }

    g_ptr_array_add (subscriptions, subscription);
    return subscription->token;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_events_unsubscribe (gpointer runtime_context, mc_runtime_subscription_t subscription)
{
    mc_runtime_events_state_t *state = mc_runtime_events;
    mc_runtime_event_id_t event_id;

    if (state == NULL || runtime_context == NULL || subscription == 0)
        return FALSE;

    for (event_id = MC_RUNTIME_EVENT_STARTUP; event_id < MC_RUNTIME_EVENT_COUNT; event_id++)
    {
        GPtrArray *subscriptions = state->subscriptions[event_id];
        guint i;

        for (i = 0; i < subscriptions->len; i++)
        {
            mc_runtime_event_subscription_t *item =
                (mc_runtime_event_subscription_t *) g_ptr_array_index (subscriptions, i);

            if (item->token == subscription && item->runtime_context == runtime_context)
            {
                mc_runtime_event_subscription_deactivate (state, item);
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_events_unsubscribe_all (gpointer runtime_context)
{
    mc_runtime_events_state_t *state = mc_runtime_events;
    mc_runtime_event_id_t event_id;

    if (state == NULL || runtime_context == NULL)
        return;

    for (event_id = MC_RUNTIME_EVENT_STARTUP; event_id < MC_RUNTIME_EVENT_COUNT; event_id++)
    {
        GPtrArray *subscriptions = state->subscriptions[event_id];
        guint i = 0;

        while (i < subscriptions->len)
        {
            mc_runtime_event_subscription_t *subscription =
                (mc_runtime_event_subscription_t *) g_ptr_array_index (subscriptions, i);

            if (subscription->runtime_context == runtime_context)
                mc_runtime_event_subscription_deactivate (state, subscription);
            else
                i++;
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_event_publish (mc_runtime_event_snapshot_t *snapshot, GError **mcerror)
{
    mc_return_val_if_error (mcerror, FALSE);

    if (mc_runtime_events == NULL)
        return mc_runtime_event_set_error (mcerror, "Runtime event system is not initialized");

    if (!mc_runtime_event_snapshot_validate (snapshot, mcerror))
        return FALSE;

    snapshot->consumed = FALSE;

    if (!mc_event_raise (MCEVENT_GROUP_RUNTIME, mc_runtime_event_name (snapshot->event_id),
                         snapshot))
        return mc_runtime_event_set_error (mcerror, "Unable to publish runtime event");

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
