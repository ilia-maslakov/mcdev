/*
   Runtime extension loader.

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

/** \file lib/extension-runtime.c
 *  \brief Source: dynamic loader for application-wide runtime extensions
 */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include "lib/global.h"
#include "lib/extension-runtime.h"
#include "lib/runtime-events.h"
#include "lib/strutil.h"
#include "lib/util.h"

#ifdef HAVE_GMODULE
#include <gmodule.h>
#endif

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#ifndef MC_RUNTIME_PLUGINS_DIR
#define MC_RUNTIME_PLUGINS_DIR "/usr/lib/mc/runtime-plugins"
#endif

#define MC_RUNTIME_HOST_SERVICES_OBJECTS_SIZE                                                      \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, file_list_free)                               \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->file_list_free))
#define MC_RUNTIME_HOST_SERVICES_EDITOR_SELECTED_TEXT_SIZE                                         \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, editor_selected_text)                         \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->editor_selected_text))
#define MC_RUNTIME_HOST_SERVICES_RUNTIME_ERROR_SIZE                                                \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, runtime_error)                                \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->runtime_error))
#define MC_RUNTIME_HOST_SERVICES_DIALOG_SIZE                                                       \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, dialog_result_free)                           \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->dialog_result_free))
#define MC_RUNTIME_HOST_SERVICES_EDITOR_INFO_SIZE                                                  \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, editor_info_free)                             \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->editor_info_free))
#define MC_RUNTIME_HOST_SERVICES_EDITOR_SELECTION_SIZE                                             \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, editor_selection_free)                        \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->editor_selection_free))
#define MC_RUNTIME_HOST_SERVICES_EDITOR_REPLACE_SELECTION_SIZE                                     \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, editor_replace_selection)                     \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->editor_replace_selection))
#define MC_RUNTIME_HOST_SERVICES_EDITOR_REPLACE_SIZE                                               \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, editor_replace)                               \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->editor_replace))
#define MC_RUNTIME_HOST_SERVICES_PROCESS_SIZE                                                      \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, process_result_free)                          \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->process_result_free))
#define MC_RUNTIME_HOST_SERVICES_UI_REFRESH_SIZE                                                   \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, ui_refresh)                                  \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->ui_refresh))
#define MC_RUNTIME_HOST_SERVICES_EDITOR_TAB_WIDTH_SIZE                                             \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, editor_tab_width)                            \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->editor_tab_width))
#define MC_RUNTIME_HOST_SERVICES_EDITOR_TEXT_SIZE                                                  \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, editor_text)                                 \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->editor_text))
#define MC_RUNTIME_HOST_SERVICES_EDITOR_EDIT_SIZE                                                  \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, editor_edit)                                 \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->editor_edit))
#define MC_RUNTIME_HOST_SERVICES_EDITOR_REPLACE_SELECTION_V2_SIZE                                  \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, editor_replace_selection_v2)                 \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->editor_replace_selection_v2))
#define MC_RUNTIME_HOST_SERVICES_UI_TEXT_WIDTH_SIZE                                                \
    (G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, ui_text_width)                               \
     + sizeof (((mc_runtime_host_services_v1_t *) NULL)->ui_text_width))
#define MC_RUNTIME_PLUGIN_DESCRIPTOR_ENUMERATE_SIZE                                                \
    (G_STRUCT_OFFSET (mc_runtime_plugin_descriptor_v1_t, enumerate_packages)                       \
     + sizeof (((mc_runtime_plugin_descriptor_v1_t *) NULL)->enumerate_packages))
#define MC_RUNTIME_PLUGIN_DESCRIPTOR_ENUMERATE_DETAILS_SIZE                                        \
    (G_STRUCT_OFFSET (mc_runtime_plugin_descriptor_v1_t, enumerate_package_details)                \
     + sizeof (((mc_runtime_plugin_descriptor_v1_t *) NULL)->enumerate_package_details))
#define MC_RUNTIME_PLUGIN_DESCRIPTOR_ACTIONS_SIZE                                                 \
    (G_STRUCT_OFFSET (mc_runtime_plugin_descriptor_v1_t, invoke_action)                           \
     + sizeof (((mc_runtime_plugin_descriptor_v1_t *) NULL)->invoke_action))
#define MC_RUNTIME_PLUGIN_DESCRIPTOR_MENU_ACTIONS_SIZE                                            \
    (G_STRUCT_OFFSET (mc_runtime_plugin_descriptor_v1_t, enumerate_menu_actions)                  \
     + sizeof (((mc_runtime_plugin_descriptor_v1_t *) NULL)->enumerate_menu_actions))
#define MC_RUNTIME_PLUGIN_DESCRIPTOR_DISPLAY_NAME_SIZE                                            \
    (G_STRUCT_OFFSET (mc_runtime_plugin_descriptor_v1_t, display_name)                            \
     + sizeof (((mc_runtime_plugin_descriptor_v1_t *) NULL)->display_name))

/*** file scope type declarations ****************************************************************/

typedef struct mc_runtime_plugin_instance mc_runtime_plugin_instance_t;

typedef struct
{
    mc_runtime_plugin_context_t *context;
    char *owner;
    char *area;
    char *id;
    char *text;
    gint priority;
    guint64 serial;
} mc_runtime_ui_indicator_t;

typedef struct
{
    const char *runtime_name;
    mc_runtime_loaded_package_callback_t callback;
    gpointer user_data;
} mc_runtime_package_enumeration_t;

typedef struct
{
    const char *runtime_name;
    mc_runtime_loaded_package_details_callback_t callback;
    gpointer user_data;
} mc_runtime_package_details_enumeration_t;

typedef struct
{
    const char *runtime_name;
    mc_runtime_loaded_action_callback_t callback;
    gpointer user_data;
} mc_runtime_action_enumeration_t;

typedef struct
{
    const char *runtime_name;
    mc_runtime_loaded_menu_action_callback_t callback;
    gpointer user_data;
} mc_runtime_menu_action_enumeration_t;

struct mc_runtime_plugin_context
{
    mc_runtime_plugin_instance_t *instance;
    gpointer data;
    GDestroyNotify data_destroy;
};

static GPtrArray *mc_runtime_ui_indicators = NULL;
static guint64 mc_runtime_ui_indicator_serial = 0;

struct mc_runtime_plugin_instance
{
#ifdef HAVE_GMODULE
    GModule *module;
#endif
    const mc_runtime_plugin_descriptor_v1_t *descriptor;
    mc_runtime_plugin_context_t *context;
};

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

static GPtrArray *mc_runtime_plugin_instances = NULL;
static gboolean mc_runtime_plugins_loaded = FALSE;
static GHashTable *mc_runtime_disabled_plugin_names = NULL;
static const mc_runtime_host_services_v1_t *mc_runtime_host_services = NULL;

#if defined(HAVE_TESTS) && defined(HAVE_GMODULE)
static char *mc_runtime_plugins_directory_for_tests = NULL;
#endif

/*** file scope functions ************************************************************************/

static gboolean
mc_runtime_plugin_context_is_known (const mc_runtime_plugin_context_t *context)
{
    guint i;

    if (mc_runtime_plugin_instances == NULL || context == NULL)
        return FALSE;

    for (i = 0; i < mc_runtime_plugin_instances->len; i++)
    {
        const mc_runtime_plugin_instance_t *instance =
            (const mc_runtime_plugin_instance_t *) g_ptr_array_index (mc_runtime_plugin_instances,
                                                                      i);

        if (instance->context == context)
            return TRUE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_subscription_t
mc_runtime_host_subscribe (mc_runtime_plugin_context_t *context, mc_runtime_event_id_t event_id,
                           int priority, mc_runtime_event_callback_t callback, gpointer user_data,
                           GDestroyNotify user_data_destroy, GError **mcerror)
{
    if (!mc_runtime_plugin_context_is_known (context))
    {
        mc_propagate_error (mcerror, 0, "%s", "Unknown runtime plugin context");
        return 0;
    }

    return mc_runtime_events_subscribe (context, event_id, priority, callback, user_data,
                                        user_data_destroy, mcerror);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_unsubscribe (mc_runtime_plugin_context_t *context,
                             mc_runtime_subscription_t subscription)
{
    if (!mc_runtime_plugin_context_is_known (context))
        return FALSE;

    return mc_runtime_events_unsubscribe (context, subscription);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_unsubscribe_all (mc_runtime_plugin_context_t *context)
{
    if (mc_runtime_plugin_context_is_known (context))
        mc_runtime_events_unsubscribe_all (context);
}

/* --------------------------------------------------------------------------------------------- */

static gpointer
mc_runtime_host_context_get_data (mc_runtime_plugin_context_t *context)
{
    if (!mc_runtime_plugin_context_is_known (context))
        return NULL;

    return context->data;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_context_set_data (mc_runtime_plugin_context_t *context, gpointer data,
                                  GDestroyNotify data_destroy)
{
    if (!mc_runtime_plugin_context_is_known (context))
        return;

    if (context->data_destroy != NULL)
        context->data_destroy (context->data);

    context->data = data;
    context->data_destroy = data_destroy;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_ui_status (mc_runtime_plugin_context_t *context, const char *text)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL
        || mc_runtime_host_services->ui_status == NULL)
        return FALSE;

    return mc_runtime_host_services->ui_status (text);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_ui_message (mc_runtime_plugin_context_t *context, const char *title,
                            const char *text)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL
        || mc_runtime_host_services->ui_message == NULL)
        return FALSE;

    return mc_runtime_host_services->ui_message (title, text);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_ui_indicator_free (mc_runtime_ui_indicator_t *indicator)
{
    if (indicator == NULL)
        return;

    g_free (indicator->owner);
    g_free (indicator->area);
    g_free (indicator->id);
    g_free (indicator->text);
    g_free (indicator);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_ui_refresh (const char *area)
{
    if (mc_runtime_host_services != NULL
        && mc_runtime_host_services->struct_size >= MC_RUNTIME_HOST_SERVICES_UI_REFRESH_SIZE
        && mc_runtime_host_services->ui_refresh != NULL)
        mc_runtime_host_services->ui_refresh (area);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_ui_indicator_set (mc_runtime_plugin_context_t *context, const char *owner,
                                  const char *area, const char *id, const char *text,
                                  gint priority, const char **error)
{
    guint i;
    mc_runtime_ui_indicator_t *indicator = NULL;

    if (!mc_runtime_plugin_context_is_known (context))
    {
        if (error != NULL)
            *error = "invalid_context";
        return FALSE;
    }
    if (owner == NULL || owner[0] == '\0' || area == NULL || area[0] == '\0' || id == NULL
        || id[0] == '\0' || text == NULL || text[0] == '\0')
    {
        if (error != NULL)
            *error = "invalid_argument";
        return FALSE;
    }

    if (mc_runtime_ui_indicators == NULL)
        mc_runtime_ui_indicators =
            g_ptr_array_new_with_free_func ((GDestroyNotify) mc_runtime_ui_indicator_free);

    for (i = 0; i < mc_runtime_ui_indicators->len; i++)
    {
        mc_runtime_ui_indicator_t *candidate =
            (mc_runtime_ui_indicator_t *) g_ptr_array_index (mc_runtime_ui_indicators, i);

        if (candidate->context == context && strcmp (candidate->owner, owner) == 0
            && strcmp (candidate->area, area) == 0 && strcmp (candidate->id, id) == 0)
        {
            indicator = candidate;
            break;
        }
    }

    if (indicator == NULL)
    {
        indicator = g_new0 (mc_runtime_ui_indicator_t, 1);
        indicator->context = context;
        indicator->owner = g_strdup (owner);
        indicator->area = g_strdup (area);
        indicator->id = g_strdup (id);
        indicator->serial = ++mc_runtime_ui_indicator_serial;
        g_ptr_array_add (mc_runtime_ui_indicators, indicator);
    }

    g_free (indicator->text);
    indicator->text = g_strdup (text);
    indicator->priority = priority;
    mc_runtime_ui_refresh (area);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_ui_indicator_clear (mc_runtime_plugin_context_t *context, const char *owner,
                                    const char *area, const char *id, const char **error)
{
    guint i;

    if (!mc_runtime_plugin_context_is_known (context))
    {
        if (error != NULL)
            *error = "invalid_context";
        return FALSE;
    }
    if (owner == NULL || owner[0] == '\0' || area == NULL || area[0] == '\0' || id == NULL
        || id[0] == '\0')
    {
        if (error != NULL)
            *error = "invalid_argument";
        return FALSE;
    }

    if (mc_runtime_ui_indicators != NULL)
        for (i = 0; i < mc_runtime_ui_indicators->len; i++)
        {
            const mc_runtime_ui_indicator_t *indicator =
                (const mc_runtime_ui_indicator_t *) g_ptr_array_index (mc_runtime_ui_indicators, i);

            if (indicator->context == context && strcmp (indicator->owner, owner) == 0
                && strcmp (indicator->area, area) == 0 && strcmp (indicator->id, id) == 0)
            {
                g_ptr_array_remove_index (mc_runtime_ui_indicators, i);
                mc_runtime_ui_refresh (area);
                break;
            }
        }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_ui_indicators_clear_owner (mc_runtime_plugin_context_t *context, const char *owner)
{
    guint i;
    GHashTable *areas;

    if (!mc_runtime_plugin_context_is_known (context) || owner == NULL || owner[0] == '\0'
        || mc_runtime_ui_indicators == NULL)
        return;

    areas = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    for (i = mc_runtime_ui_indicators->len; i > 0; i--)
    {
        const mc_runtime_ui_indicator_t *indicator =
            (const mc_runtime_ui_indicator_t *) g_ptr_array_index (mc_runtime_ui_indicators, i - 1);

        if (indicator->context == context && strcmp (indicator->owner, owner) == 0)
        {
            g_hash_table_add (areas, g_strdup (indicator->area));
            g_ptr_array_remove_index (mc_runtime_ui_indicators, i - 1);
        }
    }

    {
        GHashTableIter iter;
        gpointer key;

        g_hash_table_iter_init (&iter, areas);
        while (g_hash_table_iter_next (&iter, &key, NULL))
            mc_runtime_ui_refresh ((const char *) key);
    }
    g_hash_table_destroy (areas);
}

/* --------------------------------------------------------------------------------------------- */

static gint
mc_runtime_ui_indicator_compare (gconstpointer left, gconstpointer right)
{
    const mc_runtime_ui_indicator_t *a = *(mc_runtime_ui_indicator_t *const *) left;
    const mc_runtime_ui_indicator_t *b = *(mc_runtime_ui_indicator_t *const *) right;

    if (a->priority != b->priority)
        return a->priority > b->priority ? -1 : 1;
    return a->serial < b->serial ? -1 : a->serial > b->serial ? 1 : 0;
}

/* --------------------------------------------------------------------------------------------- */

char *
mc_runtime_ui_indicators_compose (const char *area, int max_width)
{
    GPtrArray *matches;
    GString *result;
    guint i;
    int used = 0;

    if (area == NULL || max_width <= 0 || mc_runtime_ui_indicators == NULL)
        return g_strdup ("");

    matches = g_ptr_array_new ();
    for (i = 0; i < mc_runtime_ui_indicators->len; i++)
    {
        mc_runtime_ui_indicator_t *indicator =
            (mc_runtime_ui_indicator_t *) g_ptr_array_index (mc_runtime_ui_indicators, i);

        if (strcmp (indicator->area, area) == 0)
            g_ptr_array_add (matches, indicator);
    }
    g_ptr_array_sort (matches, mc_runtime_ui_indicator_compare);

    result = g_string_new ("");
    for (i = 0; i < matches->len; i++)
    {
        const mc_runtime_ui_indicator_t *indicator =
            (const mc_runtime_ui_indicator_t *) g_ptr_array_index (matches, i);
        const int width = str_term_width1 (indicator->text);
        const int separator = result->len == 0 ? 0 : 1;

        if (width <= 0 || used + separator + width > max_width)
            continue;
        if (separator != 0)
            g_string_append_c (result, ' ');
        g_string_append (result, indicator->text);
        used += separator + width;
    }

    g_ptr_array_free (matches, TRUE);
    return g_string_free (result, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_log (mc_runtime_plugin_context_t *context, const char *source, const char *level,
                     const char *message)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL
        || mc_runtime_host_services->log == NULL)
        return;

    mc_runtime_host_services->log (source, level, message);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_runtime_error (mc_runtime_plugin_context_t *context, const char *runtime_name,
                               const char *package_id, mc_runtime_error_phase_t phase,
                               const char *summary, const char *details)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL
        || mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_RUNTIME_ERROR_SIZE
        || mc_runtime_host_services->runtime_error == NULL)
        return;

    mc_runtime_host_services->runtime_error (runtime_name, package_id, phase, summary, details);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_ui_dialog (mc_runtime_plugin_context_t *context, const mc_runtime_dialog_t *dialog,
                           mc_runtime_dialog_result_t *result, const char **error)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL
        || mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_DIALOG_SIZE
        || mc_runtime_host_services->ui_dialog == NULL)
    {
        if (error != NULL)
            *error = "not_ready";
        return FALSE;
    }

    return mc_runtime_host_services->ui_dialog (dialog, result, error);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_dialog_result_free (mc_runtime_plugin_context_t *context,
                                    mc_runtime_dialog_result_t *result)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL
        || mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_DIALOG_SIZE
        || mc_runtime_host_services->dialog_result_free == NULL)
        return;

    mc_runtime_host_services->dialog_result_free (result);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_objects_are_available (void)
{
    return mc_runtime_host_services != NULL
        && mc_runtime_host_services->struct_size >= MC_RUNTIME_HOST_SERVICES_OBJECTS_SIZE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_objects_prepare (mc_runtime_plugin_context_t *context, const char **error)
{
    if (error != NULL)
        *error = NULL;

    if (!mc_runtime_plugin_context_is_known (context))
    {
        if (error != NULL)
            *error = "no active MC context";
        return FALSE;
    }

    if (!mc_runtime_host_objects_are_available ())
    {
        if (error != NULL)
            *error = "not_ready";
        return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
mc_runtime_host_panel_active (mc_runtime_plugin_context_t *context)
{
    mc_runtime_handle_t invalid = { MC_RUNTIME_HANDLE_INVALID, 0, 0 };

    if (!mc_runtime_host_objects_prepare (context, NULL)
        || mc_runtime_host_services->panel_active == NULL)
        return invalid;

    return mc_runtime_host_services->panel_active ();
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
mc_runtime_host_panel_passive (mc_runtime_plugin_context_t *context)
{
    mc_runtime_handle_t invalid = { MC_RUNTIME_HANDLE_INVALID, 0, 0 };

    if (!mc_runtime_host_objects_prepare (context, NULL)
        || mc_runtime_host_services->panel_passive == NULL)
        return invalid;

    return mc_runtime_host_services->panel_passive ();
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_panel_cwd (mc_runtime_plugin_context_t *context, const mc_runtime_handle_t *panel,
                           mc_runtime_string_t *path, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->panel_cwd == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->panel_cwd (panel, path, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_panel_current (mc_runtime_plugin_context_t *context,
                               const mc_runtime_handle_t *panel, mc_runtime_file_snapshot_t **file,
                               const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->panel_current == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->panel_current (panel, file, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_panel_selected (mc_runtime_plugin_context_t *context,
                                const mc_runtime_handle_t *panel, mc_runtime_file_list_t *files,
                                const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->panel_selected == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->panel_selected (panel, files, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_panel_refresh (mc_runtime_plugin_context_t *context,
                               const mc_runtime_handle_t *panel, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->panel_refresh == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->panel_refresh (panel, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_panel_chdir (mc_runtime_plugin_context_t *context, const mc_runtime_handle_t *panel,
                             const char *path, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->panel_chdir == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->panel_chdir (panel, path, error);
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
mc_runtime_host_editor_current (mc_runtime_plugin_context_t *context)
{
    mc_runtime_handle_t invalid = { MC_RUNTIME_HANDLE_INVALID, 0, 0 };

    if (!mc_runtime_host_objects_prepare (context, NULL)
        || mc_runtime_host_services->editor_current == NULL)
        return invalid;

    return mc_runtime_host_services->editor_current ();
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_path (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor, mc_runtime_string_t *path,
                             const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->editor_path == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_path (editor, path, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_cursor (mc_runtime_plugin_context_t *context,
                               const mc_runtime_handle_t *editor, guint64 *line, guint64 *column,
                               const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->editor_cursor == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_cursor (editor, line, column, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_set_cursor (mc_runtime_plugin_context_t *context,
                                   const mc_runtime_handle_t *editor, guint64 line, guint64 column,
                                   const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->editor_set_cursor == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_set_cursor (editor, line, column, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_is_readonly (mc_runtime_plugin_context_t *context,
                                    const mc_runtime_handle_t *editor, gboolean *readonly,
                                    const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->editor_is_readonly == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_is_readonly (editor, readonly, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_get_text (mc_runtime_plugin_context_t *context,
                                 const mc_runtime_handle_t *editor, gint64 from, gint64 to,
                                 mc_runtime_string_t *text, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->editor_get_text == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_get_text (editor, from, to, text, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_insert (mc_runtime_plugin_context_t *context,
                               const mc_runtime_handle_t *editor, const char *text,
                               const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->editor_insert == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_insert (editor, text, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_save (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->editor_save == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_save (editor, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_tab_width (mc_runtime_plugin_context_t *context,
                                  const mc_runtime_handle_t *editor, guint *tab_width,
                                  const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_EDITOR_TAB_WIDTH_SIZE
        || mc_runtime_host_services->editor_tab_width == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_tab_width (editor, tab_width, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_text (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor,
                             const mc_runtime_editor_range_t *range, gboolean has_revision,
                             guint64 revision, mc_runtime_string_t *text, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_EDITOR_TEXT_SIZE
        || mc_runtime_host_services->editor_text == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_text (editor, range, has_revision, revision, text,
                                                  error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_edit (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor,
                             const mc_runtime_editor_edit_t *edit_spec,
                             mc_runtime_editor_edit_result_t *result, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_EDITOR_EDIT_SIZE
        || mc_runtime_host_services->editor_edit == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_edit (editor, edit_spec, result, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_replace_selection_v2 (
    mc_runtime_plugin_context_t *context, const mc_runtime_handle_t *editor, guint64 revision,
    const char *text, gsize text_length, mc_runtime_editor_edit_result_t *result,
    const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->struct_size
            < MC_RUNTIME_HOST_SERVICES_EDITOR_REPLACE_SELECTION_V2_SIZE
        || mc_runtime_host_services->editor_replace_selection_v2 == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_replace_selection_v2 (editor, revision, text,
                                                                  text_length, result, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_ui_text_width (mc_runtime_plugin_context_t *context, const char *text,
                               gsize text_length, guint *width, const char **error)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL)
    {
        if (error != NULL)
            *error = "invalid_context";
        return FALSE;
    }
    if (mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_UI_TEXT_WIDTH_SIZE
        || mc_runtime_host_services->ui_text_width == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->ui_text_width (text, text_length, width, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_selected_text (mc_runtime_plugin_context_t *context,
                                      const mc_runtime_handle_t *editor, mc_runtime_string_t *text,
                                      const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_EDITOR_SELECTED_TEXT_SIZE
        || mc_runtime_host_services->editor_selected_text == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }

    return mc_runtime_host_services->editor_selected_text (editor, text, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_info (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *editor, mc_runtime_editor_info_t *info,
                             const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_EDITOR_INFO_SIZE
        || mc_runtime_host_services->editor_info == NULL
        || mc_runtime_host_services->editor_info_free == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_info (editor, info, error);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_editor_info_free (mc_runtime_plugin_context_t *context,
                                  mc_runtime_editor_info_t *info)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL
        || mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_EDITOR_INFO_SIZE
        || mc_runtime_host_services->editor_info_free == NULL)
        return;
    mc_runtime_host_services->editor_info_free (info);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_selection (mc_runtime_plugin_context_t *context,
                                  const mc_runtime_handle_t *editor,
                                  mc_runtime_editor_selection_t *selection, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_EDITOR_SELECTION_SIZE
        || mc_runtime_host_services->editor_selection == NULL
        || mc_runtime_host_services->editor_selection_free == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_selection (editor, selection, error);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_editor_selection_free (mc_runtime_plugin_context_t *context,
                                       mc_runtime_editor_selection_t *selection)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL
        || mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_EDITOR_SELECTION_SIZE
        || mc_runtime_host_services->editor_selection_free == NULL)
        return;
    mc_runtime_host_services->editor_selection_free (selection);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_replace_selection (mc_runtime_plugin_context_t *context,
                                          const mc_runtime_handle_t *editor, const char *text,
                                          gsize text_length,
                                          mc_runtime_editor_edit_result_t *result,
                                          const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->struct_size
            < MC_RUNTIME_HOST_SERVICES_EDITOR_REPLACE_SELECTION_SIZE
        || mc_runtime_host_services->editor_replace_selection == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_replace_selection (editor, text, text_length, result,
                                                               error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_editor_replace (mc_runtime_plugin_context_t *context,
                                const mc_runtime_handle_t *editor, guint64 from, guint64 to,
                                const char *text, gsize text_length,
                                mc_runtime_editor_edit_result_t *result, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_EDITOR_REPLACE_SIZE
        || mc_runtime_host_services->editor_replace == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->editor_replace (editor, from, to, text, text_length, result,
                                                     error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_process_run_shell (mc_runtime_plugin_context_t *context, const char *command,
                                   gsize max_output, mc_runtime_process_result_t *result,
                                   const char **error)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL)
    {
        if (error != NULL)
            *error = "invalid_context";
        return FALSE;
    }
    if (mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_PROCESS_SIZE
        || mc_runtime_host_services->process_run_shell == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->process_run_shell (command, max_output, result, error);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_process_result_free (mc_runtime_plugin_context_t *context,
                                     mc_runtime_process_result_t *result)
{
    if (!mc_runtime_plugin_context_is_known (context) || mc_runtime_host_services == NULL
        || mc_runtime_host_services->struct_size < MC_RUNTIME_HOST_SERVICES_PROCESS_SIZE
        || mc_runtime_host_services->process_result_free == NULL)
        return;
    mc_runtime_host_services->process_result_free (result);
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_handle_t
mc_runtime_host_viewer_current (mc_runtime_plugin_context_t *context)
{
    mc_runtime_handle_t invalid = { MC_RUNTIME_HANDLE_INVALID, 0, 0 };

    if (!mc_runtime_host_objects_prepare (context, NULL)
        || mc_runtime_host_services->viewer_current == NULL)
        return invalid;

    return mc_runtime_host_services->viewer_current ();
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_viewer_path (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *viewer, mc_runtime_string_t *path,
                             const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->viewer_path == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->viewer_path (viewer, path, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_viewer_position (mc_runtime_plugin_context_t *context,
                                 const mc_runtime_handle_t *viewer, gint64 *offset,
                                 const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->viewer_position == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->viewer_position (viewer, offset, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_viewer_goto (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *viewer, gint64 offset, const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->viewer_goto == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->viewer_goto (viewer, offset, error);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_viewer_mode (mc_runtime_plugin_context_t *context,
                             const mc_runtime_handle_t *viewer, mc_runtime_string_t *mode,
                             const char **error)
{
    if (!mc_runtime_host_objects_prepare (context, error))
        return FALSE;
    if (mc_runtime_host_services->viewer_mode == NULL)
    {
        if (error != NULL)
            *error = "not_supported";
        return FALSE;
    }
    return mc_runtime_host_services->viewer_mode (viewer, mode, error);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_string_free (mc_runtime_plugin_context_t *context, mc_runtime_string_t *string)
{
    if (mc_runtime_host_objects_prepare (context, NULL)
        && mc_runtime_host_services->string_free != NULL)
        mc_runtime_host_services->string_free (string);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_file_snapshot_free (mc_runtime_plugin_context_t *context,
                                    mc_runtime_file_snapshot_t *file)
{
    if (mc_runtime_host_objects_prepare (context, NULL)
        && mc_runtime_host_services->file_snapshot_free != NULL)
        mc_runtime_host_services->file_snapshot_free (file);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_host_file_list_free (mc_runtime_plugin_context_t *context, mc_runtime_file_list_t *files)
{
    if (mc_runtime_host_objects_prepare (context, NULL)
        && mc_runtime_host_services->file_list_free != NULL)
        mc_runtime_host_services->file_list_free (files);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_package_enumeration_bridge (const char *id, const char *display_name, gboolean enabled,
                                       gpointer user_data)
{
    const mc_runtime_package_enumeration_t *enumeration =
        (const mc_runtime_package_enumeration_t *) user_data;

    if (enumeration != NULL && enumeration->callback != NULL)
        enumeration->callback (enumeration->runtime_name, id, display_name, enabled,
                               enumeration->user_data);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_package_details_enumeration_bridge (const char *id, const char *display_name,
                                               const char *workspace, const char *origin,
                                               const char *directory, gboolean enabled,
                                               gpointer user_data)
{
    const mc_runtime_package_details_enumeration_t *enumeration =
        (const mc_runtime_package_details_enumeration_t *) user_data;

    if (enumeration != NULL && enumeration->callback != NULL)
        enumeration->callback (enumeration->runtime_name, id, display_name, workspace, origin,
                               directory, enabled, enumeration->user_data);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_package_details_legacy_bridge (const char *id, const char *display_name,
                                          gboolean enabled, gpointer user_data)
{
    mc_runtime_package_details_enumeration_bridge (id, display_name, NULL, NULL, NULL, enabled,
                                                   user_data);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_action_enumeration_bridge (const char *id, const char *label, const char *shortcut,
                                      gpointer user_data)
{
    const mc_runtime_action_enumeration_t *enumeration =
        (const mc_runtime_action_enumeration_t *) user_data;

    if (enumeration != NULL && enumeration->callback != NULL)
        enumeration->callback (enumeration->runtime_name, id, label, shortcut,
                               enumeration->user_data);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_menu_action_enumeration_bridge (const char *id, const char *menu_path,
                                           const char *label, const char *shortcut, gint position,
                                           gpointer user_data)
{
    const mc_runtime_menu_action_enumeration_t *enumeration =
        (const mc_runtime_menu_action_enumeration_t *) user_data;

    if (enumeration != NULL && enumeration->callback != NULL)
        enumeration->callback (enumeration->runtime_name, id, menu_path, label, shortcut, position,
                               enumeration->user_data);
}

/* --------------------------------------------------------------------------------------------- */

static mc_runtime_host_api_v1_t mc_runtime_host_api = {
    .abi_version = MC_RUNTIME_PLUGIN_ABI_VERSION,
    .struct_size = sizeof (mc_runtime_host_api_v1_t),
    .capability_flags = MC_RUNTIME_HOST_CAP_EVENTS | MC_RUNTIME_HOST_CAP_CONTEXT_DATA,
    .subscribe = mc_runtime_host_subscribe,
    .unsubscribe = mc_runtime_host_unsubscribe,
    .unsubscribe_all = mc_runtime_host_unsubscribe_all,
    .context_get_data = mc_runtime_host_context_get_data,
    .context_set_data = mc_runtime_host_context_set_data,
    .ui_status = mc_runtime_host_ui_status,
    .ui_message = mc_runtime_host_ui_message,
    .log = mc_runtime_host_log,
    .panel_active = mc_runtime_host_panel_active,
    .panel_passive = mc_runtime_host_panel_passive,
    .panel_cwd = mc_runtime_host_panel_cwd,
    .panel_current = mc_runtime_host_panel_current,
    .panel_selected = mc_runtime_host_panel_selected,
    .panel_refresh = mc_runtime_host_panel_refresh,
    .panel_chdir = mc_runtime_host_panel_chdir,
    .editor_current = mc_runtime_host_editor_current,
    .editor_path = mc_runtime_host_editor_path,
    .editor_cursor = mc_runtime_host_editor_cursor,
    .editor_set_cursor = mc_runtime_host_editor_set_cursor,
    .editor_is_readonly = mc_runtime_host_editor_is_readonly,
    .editor_get_text = mc_runtime_host_editor_get_text,
    .editor_insert = mc_runtime_host_editor_insert,
    .editor_save = mc_runtime_host_editor_save,
    .viewer_current = mc_runtime_host_viewer_current,
    .viewer_path = mc_runtime_host_viewer_path,
    .viewer_position = mc_runtime_host_viewer_position,
    .viewer_goto = mc_runtime_host_viewer_goto,
    .viewer_mode = mc_runtime_host_viewer_mode,
    .string_free = mc_runtime_host_string_free,
    .file_snapshot_free = mc_runtime_host_file_snapshot_free,
    .file_list_free = mc_runtime_host_file_list_free,
    .editor_selected_text = mc_runtime_host_editor_selected_text,
    .runtime_error = mc_runtime_host_runtime_error,
    .ui_dialog = mc_runtime_host_ui_dialog,
    .dialog_result_free = mc_runtime_host_dialog_result_free,
    .editor_info = mc_runtime_host_editor_info,
    .editor_info_free = mc_runtime_host_editor_info_free,
    .editor_selection = mc_runtime_host_editor_selection,
    .editor_selection_free = mc_runtime_host_editor_selection_free,
    .editor_replace_selection = mc_runtime_host_editor_replace_selection,
    .editor_replace = mc_runtime_host_editor_replace,
    .process_run_shell = mc_runtime_host_process_run_shell,
    .process_result_free = mc_runtime_host_process_result_free,
    .ui_indicator_set = mc_runtime_host_ui_indicator_set,
    .ui_indicator_clear = mc_runtime_host_ui_indicator_clear,
    .ui_indicators_clear_owner = mc_runtime_host_ui_indicators_clear_owner,
    .editor_tab_width = mc_runtime_host_editor_tab_width,
    .editor_text = mc_runtime_host_editor_text,
    .editor_edit = mc_runtime_host_editor_edit,
    .editor_replace_selection_v2 = mc_runtime_host_editor_replace_selection_v2,
    .ui_text_width = mc_runtime_host_ui_text_width,
};

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_plugin_descriptor_is_compatible (const mc_runtime_plugin_descriptor_v1_t *descriptor,
                                            const char **reason)
{
    const gsize required_size = G_STRUCT_OFFSET (mc_runtime_plugin_descriptor_v1_t, shutdown)
        + sizeof (descriptor->shutdown);

    if (reason != NULL)
        *reason = NULL;

    if (descriptor == NULL)
    {
        if (reason != NULL)
            *reason = "registration returned no descriptor";
        return FALSE;
    }

    if (descriptor->abi_version != MC_RUNTIME_PLUGIN_ABI_VERSION)
    {
        if (reason != NULL)
            *reason = "incompatible ABI version";
        return FALSE;
    }

    if (descriptor->struct_size < required_size)
    {
        if (reason != NULL)
            *reason = "descriptor is too small for ABI v1";
        return FALSE;
    }

    if (descriptor->runtime_name == NULL || descriptor->runtime_name[0] == '\0')
    {
        if (reason != NULL)
            *reason = "descriptor has no runtime name";
        return FALSE;
    }

    if (descriptor->init == NULL || descriptor->shutdown == NULL)
    {
        if (reason != NULL)
            *reason = "descriptor misses a lifecycle callback";
        return FALSE;
    }

    if ((descriptor->required_host_capabilities & ~mc_runtime_host_api.capability_flags) != 0)
    {
        if (reason != NULL)
            *reason = "host lacks a required capability";
        return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_plugin_name_is_loaded (const char *runtime_name)
{
    guint i;

    for (i = 0; i < mc_runtime_plugin_instances->len; i++)
    {
        const mc_runtime_plugin_instance_t *instance =
            (const mc_runtime_plugin_instance_t *) g_ptr_array_index (mc_runtime_plugin_instances,
                                                                      i);

        if (strcmp (instance->descriptor->runtime_name, runtime_name) == 0)
            return TRUE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_plugin_is_disabled (const char *runtime_name)
{
    if (runtime_name == NULL)
        return FALSE;

    if (g_strcmp0 (runtime_name, "lua") == 0 && g_strcmp0 (g_getenv ("MC_NO_LUA"), "1") == 0)
        return TRUE;

    return mc_runtime_disabled_plugin_names != NULL
        && g_hash_table_contains (mc_runtime_disabled_plugin_names, runtime_name);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_plugin_context_destroy (mc_runtime_plugin_context_t *context)
{
    guint i;

    if (context == NULL)
        return;

    if (mc_runtime_ui_indicators != NULL)
        for (i = mc_runtime_ui_indicators->len; i > 0; i--)
        {
            const mc_runtime_ui_indicator_t *indicator =
                (const mc_runtime_ui_indicator_t *) g_ptr_array_index (mc_runtime_ui_indicators,
                                                                        i - 1);

            if (indicator->context == context)
                g_ptr_array_remove_index (mc_runtime_ui_indicators, i - 1);
        }

    if (context->data_destroy != NULL)
        context->data_destroy (context->data);

    g_free (context);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_plugin_instance_destroy (mc_runtime_plugin_instance_t *instance, gboolean shutdown)
{
    if (instance == NULL)
        return;

    mc_runtime_events_unsubscribe_all (instance->context);

    if (shutdown && instance->descriptor->shutdown != NULL)
        instance->descriptor->shutdown (instance->context);

    /* A buggy runtime must not leave a callback registered while its state dies. */
    mc_runtime_events_unsubscribe_all (instance->context);
    mc_runtime_plugin_context_destroy (instance->context);
    g_free (instance);
}

/* --------------------------------------------------------------------------------------------- */

#ifdef HAVE_GMODULE

static const char *
mc_runtime_plugins_directory (void)
{
#ifdef HAVE_TESTS
    if (mc_runtime_plugins_directory_for_tests != NULL)
        return mc_runtime_plugins_directory_for_tests;
#endif

    return MC_RUNTIME_PLUGINS_DIR;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_module_filename_has_native_suffix (const char *filename)
{
    if (filename == NULL)
        return FALSE;

    return g_str_has_suffix (filename, ".so") || g_str_has_suffix (filename, ".dylib")
        || g_str_has_suffix (filename, ".bundle") || g_str_has_suffix (filename, ".dll");
}

/* --------------------------------------------------------------------------------------------- */

static gint
mc_runtime_module_filename_compare (gconstpointer first, gconstpointer second)
{
    const char *const *first_name = (const char *const *) first;
    const char *const *second_name = (const char *const *) second;

    return g_strcmp0 (*first_name, *second_name);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_plugin_try_load (const char *module_path, const char *filename)
{
    GModule *module;
    mc_runtime_plugin_register_v1_fn register_fn;
    const mc_runtime_plugin_descriptor_v1_t *descriptor;
    const char *reason;
    mc_runtime_plugin_instance_t *instance;
    GError *mcerror = NULL;

    module = g_module_open (module_path, 0);
    if (module == NULL)
    {
        fprintf (stderr, "Runtime plugin %s not loaded: %s\n", filename, g_module_error ());
        return;
    }

    if (!g_module_symbol (module, MC_RUNTIME_PLUGIN_ENTRY_V1, (gpointer *) &register_fn))
    {
        fprintf (stderr, "Runtime plugin %s: symbol %s not found\n", filename,
                 MC_RUNTIME_PLUGIN_ENTRY_V1);
        g_module_close (module);
        return;
    }

    descriptor = register_fn ();
    if (!mc_runtime_plugin_descriptor_is_compatible (descriptor, &reason))
    {
        fprintf (stderr, "Runtime plugin %s not loaded: %s\n", filename, reason);
        g_module_close (module);
        return;
    }

    if (mc_runtime_plugin_name_is_loaded (descriptor->runtime_name))
    {
        fprintf (stderr, "Runtime plugin %s not loaded: duplicate runtime '%s'\n", filename,
                 descriptor->runtime_name);
        g_module_close (module);
        return;
    }

    if (mc_runtime_plugin_is_disabled (descriptor->runtime_name))
    {
        g_module_close (module);
        return;
    }

    instance = g_new0 (mc_runtime_plugin_instance_t, 1);
    instance->module = module;
    instance->descriptor = descriptor;
    instance->context = g_new0 (mc_runtime_plugin_context_t, 1);
    instance->context->instance = instance;
    g_ptr_array_add (mc_runtime_plugin_instances, instance);

    if (!descriptor->init (&mc_runtime_host_api, instance->context, &mcerror))
    {
        fprintf (stderr, "Runtime plugin %s not loaded: %s\n", filename,
                 mcerror != NULL ? mcerror->message : "initialization failed");
        g_clear_error (&mcerror);
        mc_runtime_plugin_instance_destroy (instance, TRUE);
        g_ptr_array_remove_index (mc_runtime_plugin_instances,
                                  mc_runtime_plugin_instances->len - 1);
        g_module_close (module);
        return;
    }

    g_module_make_resident (module);
}

/* --------------------------------------------------------------------------------------------- */

static void
mc_runtime_plugins_load_from_dir (const char *plugins_dir)
{
    GDir *dir;
    const char *entry;
    GPtrArray *filenames;
    guint i;

    dir = g_dir_open (plugins_dir, 0, NULL);
    if (dir == NULL)
        return;

    filenames = g_ptr_array_new_with_free_func (g_free);
    while ((entry = g_dir_read_name (dir)) != NULL)
        if (mc_runtime_module_filename_has_native_suffix (entry))
            g_ptr_array_add (filenames, g_strdup (entry));
    g_dir_close (dir);

    g_ptr_array_sort (filenames, mc_runtime_module_filename_compare);
    for (i = 0; i < filenames->len; i++)
    {
        const char *filename = (const char *) g_ptr_array_index (filenames, i);
        char *path = g_build_filename (plugins_dir, filename, (char *) NULL);

        mc_runtime_plugin_try_load (path, filename);
        g_free (path);
    }

    g_ptr_array_free (filenames, TRUE);
}

#endif /* HAVE_GMODULE */

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_plugins_load (GError **mcerror)
{
    mc_return_val_if_error (mcerror, FALSE);

    if (mc_runtime_plugins_loaded)
        return TRUE;

    if (!mc_runtime_events_is_initialized ())
    {
        mc_propagate_error (mcerror, 0, "%s", "Runtime events must be initialized first");
        return FALSE;
    }

    mc_runtime_plugin_instances = g_ptr_array_new ();
    mc_runtime_plugins_loaded = TRUE;

#ifdef HAVE_GMODULE
    mc_runtime_plugins_load_from_dir (mc_runtime_plugins_directory ());
#endif

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_plugins_shutdown (void)
{
    if (!mc_runtime_plugins_loaded)
        return;

    while (mc_runtime_plugin_instances->len != 0)
    {
        mc_runtime_plugin_instance_t *instance =
            (mc_runtime_plugin_instance_t *) g_ptr_array_index (
                mc_runtime_plugin_instances, mc_runtime_plugin_instances->len - 1);

        mc_runtime_plugin_instance_destroy (instance, TRUE);
        g_ptr_array_remove_index (mc_runtime_plugin_instances,
                                  mc_runtime_plugin_instances->len - 1);
    }

    g_ptr_array_free (mc_runtime_plugin_instances, TRUE);
    mc_runtime_plugin_instances = NULL;
    mc_runtime_plugins_loaded = FALSE;

    if (mc_runtime_ui_indicators != NULL)
    {
        g_ptr_array_free (mc_runtime_ui_indicators, TRUE);
        mc_runtime_ui_indicators = NULL;
    }
    mc_runtime_ui_indicator_serial = 0;

    if (mc_runtime_disabled_plugin_names != NULL)
    {
        g_hash_table_destroy (mc_runtime_disabled_plugin_names);
        mc_runtime_disabled_plugin_names = NULL;
    }
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_plugins_are_loaded (void)
{
    return mc_runtime_plugins_loaded;
}

/* --------------------------------------------------------------------------------------------- */

guint
mc_runtime_plugins_count (void)
{
    return mc_runtime_plugin_instances != NULL ? mc_runtime_plugin_instances->len : 0;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_plugins_enumerate_runtimes (mc_runtime_loaded_runtime_callback_t callback,
                                       gpointer user_data)
{
    guint i;

    if (callback == NULL || mc_runtime_plugin_instances == NULL)
        return;

    for (i = 0; i < mc_runtime_plugin_instances->len; i++)
    {
        const mc_runtime_plugin_instance_t *instance =
            (const mc_runtime_plugin_instance_t *) g_ptr_array_index (mc_runtime_plugin_instances,
                                                                      i);
        const mc_runtime_plugin_descriptor_v1_t *descriptor = instance->descriptor;
        const char *display_name = descriptor->runtime_name;

        if (descriptor->struct_size >= MC_RUNTIME_PLUGIN_DESCRIPTOR_DISPLAY_NAME_SIZE
            && descriptor->display_name != NULL && descriptor->display_name[0] != '\0')
            display_name = descriptor->display_name;

        callback (descriptor->runtime_name, display_name, descriptor->abi_version,
                  descriptor->capability_flags, descriptor->required_host_capabilities,
                  user_data);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_plugins_enumerate_packages (mc_runtime_loaded_package_callback_t callback,
                                       gpointer user_data)
{
    guint i;

    if (callback == NULL || mc_runtime_plugin_instances == NULL)
        return;

    for (i = 0; i < mc_runtime_plugin_instances->len; i++)
    {
        const mc_runtime_plugin_instance_t *instance =
            (const mc_runtime_plugin_instance_t *) g_ptr_array_index (mc_runtime_plugin_instances,
                                                                      i);
        mc_runtime_package_enumeration_t enumeration;

        if (instance->descriptor->struct_size < MC_RUNTIME_PLUGIN_DESCRIPTOR_ENUMERATE_SIZE
            || instance->descriptor->enumerate_packages == NULL)
            continue;

        enumeration.runtime_name = instance->descriptor->runtime_name;
        enumeration.callback = callback;
        enumeration.user_data = user_data;
        instance->descriptor->enumerate_packages (
            instance->context, mc_runtime_package_enumeration_bridge, &enumeration);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_plugins_enumerate_package_details (mc_runtime_loaded_package_details_callback_t callback,
                                              gpointer user_data)
{
    guint i;

    if (callback == NULL || mc_runtime_plugin_instances == NULL)
        return;

    for (i = 0; i < mc_runtime_plugin_instances->len; i++)
    {
        const mc_runtime_plugin_instance_t *instance =
            (const mc_runtime_plugin_instance_t *) g_ptr_array_index (mc_runtime_plugin_instances,
                                                                      i);
        mc_runtime_package_details_enumeration_t enumeration;

        if (instance->descriptor->struct_size < MC_RUNTIME_PLUGIN_DESCRIPTOR_ENUMERATE_SIZE
            || instance->descriptor->enumerate_packages == NULL)
            continue;

        enumeration.runtime_name = instance->descriptor->runtime_name;
        enumeration.callback = callback;
        enumeration.user_data = user_data;

        if (instance->descriptor->struct_size >= MC_RUNTIME_PLUGIN_DESCRIPTOR_ENUMERATE_DETAILS_SIZE
            && instance->descriptor->enumerate_package_details != NULL)
            instance->descriptor->enumerate_package_details (
                instance->context, mc_runtime_package_details_enumeration_bridge, &enumeration);
        else
            instance->descriptor->enumerate_packages (
                instance->context, mc_runtime_package_details_legacy_bridge, &enumeration);
    }
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_plugins_enumerate_actions (const char *workspace,
                                      mc_runtime_loaded_action_callback_t callback,
                                      gpointer user_data)
{
    guint i;

    if (workspace == NULL || callback == NULL || mc_runtime_plugin_instances == NULL)
        return;

    for (i = 0; i < mc_runtime_plugin_instances->len; i++)
    {
        const mc_runtime_plugin_instance_t *instance =
            (const mc_runtime_plugin_instance_t *) g_ptr_array_index (mc_runtime_plugin_instances,
                                                                      i);
        mc_runtime_action_enumeration_t enumeration;

        if (instance->descriptor->struct_size < MC_RUNTIME_PLUGIN_DESCRIPTOR_ACTIONS_SIZE
            || instance->descriptor->enumerate_actions == NULL)
            continue;

        enumeration.runtime_name = instance->descriptor->runtime_name;
        enumeration.callback = callback;
        enumeration.user_data = user_data;
        instance->descriptor->enumerate_actions (instance->context, workspace,
                                                 mc_runtime_action_enumeration_bridge,
                                                 &enumeration);
    }
}

/* --------------------------------------------------------------------------------------------- */

gboolean
mc_runtime_plugins_invoke_action (const char *runtime_name, const char *workspace,
                                  const char *action_id, const char **error)
{
    guint i;

    if (error != NULL)
        *error = NULL;
    if (runtime_name == NULL || workspace == NULL || action_id == NULL)
    {
        if (error != NULL)
            *error = "invalid_argument";
        return FALSE;
    }

    for (i = 0; mc_runtime_plugin_instances != NULL && i < mc_runtime_plugin_instances->len; i++)
    {
        const mc_runtime_plugin_instance_t *instance =
            (const mc_runtime_plugin_instance_t *) g_ptr_array_index (mc_runtime_plugin_instances,
                                                                      i);

        if (g_strcmp0 (instance->descriptor->runtime_name, runtime_name) != 0)
            continue;
        if (instance->descriptor->struct_size < MC_RUNTIME_PLUGIN_DESCRIPTOR_ACTIONS_SIZE
            || instance->descriptor->invoke_action == NULL)
            break;
        return instance->descriptor->invoke_action (instance->context, workspace, action_id, error);
    }

    if (error != NULL)
        *error = "action_not_found";
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_plugins_enumerate_menu_actions (const char *workspace,
                                           mc_runtime_loaded_menu_action_callback_t callback,
                                           gpointer user_data)
{
    guint i;

    if (workspace == NULL || callback == NULL || mc_runtime_plugin_instances == NULL)
        return;

    for (i = 0; i < mc_runtime_plugin_instances->len; i++)
    {
        const mc_runtime_plugin_instance_t *instance =
            (const mc_runtime_plugin_instance_t *) g_ptr_array_index (mc_runtime_plugin_instances,
                                                                      i);
        mc_runtime_menu_action_enumeration_t enumeration;

        if (instance->descriptor->struct_size < MC_RUNTIME_PLUGIN_DESCRIPTOR_MENU_ACTIONS_SIZE
            || instance->descriptor->enumerate_menu_actions == NULL)
            continue;

        enumeration.runtime_name = instance->descriptor->runtime_name;
        enumeration.callback = callback;
        enumeration.user_data = user_data;
        instance->descriptor->enumerate_menu_actions (
            instance->context, workspace, mc_runtime_menu_action_enumeration_bridge, &enumeration);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_has_panel_services (void)
{
    return mc_runtime_host_objects_are_available ()
        && mc_runtime_host_services->panel_active != NULL
        && mc_runtime_host_services->panel_passive != NULL
        && mc_runtime_host_services->panel_cwd != NULL
        && mc_runtime_host_services->panel_current != NULL
        && mc_runtime_host_services->panel_selected != NULL
        && mc_runtime_host_services->panel_refresh != NULL
        && mc_runtime_host_services->panel_chdir != NULL
        && mc_runtime_host_services->string_free != NULL
        && mc_runtime_host_services->file_snapshot_free != NULL
        && mc_runtime_host_services->file_list_free != NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_has_editor_services (void)
{
    return mc_runtime_host_objects_are_available ()
        && mc_runtime_host_services->editor_current != NULL
        && mc_runtime_host_services->editor_path != NULL
        && mc_runtime_host_services->editor_cursor != NULL
        && mc_runtime_host_services->editor_set_cursor != NULL
        && mc_runtime_host_services->editor_is_readonly != NULL
        && mc_runtime_host_services->editor_get_text != NULL
        && mc_runtime_host_services->editor_insert != NULL
        && mc_runtime_host_services->editor_save != NULL
        && mc_runtime_host_services->string_free != NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_has_process_services (void)
{
    return mc_runtime_host_services != NULL
        && mc_runtime_host_services->struct_size >= MC_RUNTIME_HOST_SERVICES_PROCESS_SIZE
        && mc_runtime_host_services->process_run_shell != NULL
        && mc_runtime_host_services->process_result_free != NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mc_runtime_host_has_viewer_services (void)
{
    return mc_runtime_host_objects_are_available ()
        && mc_runtime_host_services->viewer_current != NULL
        && mc_runtime_host_services->viewer_path != NULL
        && mc_runtime_host_services->viewer_position != NULL
        && mc_runtime_host_services->viewer_goto != NULL
        && mc_runtime_host_services->viewer_mode != NULL
        && mc_runtime_host_services->string_free != NULL;
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_plugins_disable (const char *runtime_name)
{
    if (runtime_name == NULL || runtime_name[0] == '\0' || mc_runtime_plugins_loaded)
        return;

    if (mc_runtime_disabled_plugin_names == NULL)
        mc_runtime_disabled_plugin_names =
            g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    g_hash_table_add (mc_runtime_disabled_plugin_names, g_strdup (runtime_name));
}

/* --------------------------------------------------------------------------------------------- */

void
mc_runtime_plugins_set_host_services (const mc_runtime_host_services_v1_t *services)
{
    const gsize minimum_size =
        G_STRUCT_OFFSET (mc_runtime_host_services_v1_t, log) + sizeof (services->log);

    if (mc_runtime_plugins_loaded)
        return;

    if (services != NULL
        && (services->abi_version != MC_RUNTIME_PLUGIN_ABI_VERSION
            || services->struct_size < minimum_size))
        return;

    mc_runtime_host_services = services;
    mc_runtime_host_api.capability_flags =
        MC_RUNTIME_HOST_CAP_EVENTS | MC_RUNTIME_HOST_CAP_CONTEXT_DATA;
    if (services != NULL && services->ui_status != NULL && services->ui_message != NULL)
        mc_runtime_host_api.capability_flags |= MC_RUNTIME_HOST_CAP_UI;
    if (services != NULL && services->log != NULL)
        mc_runtime_host_api.capability_flags |= MC_RUNTIME_HOST_CAP_LOG;
    if (mc_runtime_host_has_panel_services ())
        mc_runtime_host_api.capability_flags |= MC_RUNTIME_HOST_CAP_PANEL;
    if (mc_runtime_host_has_editor_services ())
        mc_runtime_host_api.capability_flags |= MC_RUNTIME_HOST_CAP_EDITOR;
    if (mc_runtime_host_has_viewer_services ())
        mc_runtime_host_api.capability_flags |= MC_RUNTIME_HOST_CAP_VIEWER;
    if (mc_runtime_host_has_process_services ())
        mc_runtime_host_api.capability_flags |= MC_RUNTIME_HOST_CAP_PROCESS;
}

/* --------------------------------------------------------------------------------------------- */

#ifdef HAVE_TESTS

void
mc_runtime_plugins_set_directory_for_tests (const char *directory)
{
    if (mc_runtime_plugins_loaded)
        return;

#ifdef HAVE_GMODULE
    g_free (mc_runtime_plugins_directory_for_tests);
    mc_runtime_plugins_directory_for_tests = g_strdup (directory);
#else
    (void) directory;
#endif
}

/* --------------------------------------------------------------------------------------------- */

#endif
