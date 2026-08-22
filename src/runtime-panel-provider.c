/** \file runtime-panel-provider.c
 *  \brief Host bridge from runtime panel providers to native panel plugins
 */

#include <config.h>

#include <string.h>

#include "lib/extension-runtime.h"
#include "lib/panel-plugin.h"
#include "lib/runtime-events.h"

#include "runtime-panel-provider.h"

typedef struct
{
    mc_runtime_plugin_context_t *context;
    guint64 runtime_provider_id;
    mc_runtime_panel_provider_dispatch_t dispatch;
    mc_runtime_panel_provider_response_free_t response_free;
    mc_panel_plugin_t plugin;
    char *id;
    char *title;
    char *prefix;
    char *proto;
    char *help_file;
    char *help_node;
    mc_runtime_panel_action_t *actions;
    mc_pp_action_t *native_actions;
    mc_pp_cmd_menu_entry_t *menu_entries;
    guint actions_count;
    gboolean active;
    guint open_instances;
} runtime_panel_provider_t;

typedef struct
{
    runtime_panel_provider_t *provider;
    mc_panel_host_t *host;
    guint64 runtime_instance_id;
    guint64 revision;
    char *title;
    char *location;
    char *footer;
    char *focus_id;
    char *view_help_node;
    char *default_format;
    mc_panel_column_t *columns;
    size_t columns_count;
    GHashTable *name_to_id;
    GHashTable *id_to_name;
    GHashTable *column_values;
    GHashTable *entry_help;
} runtime_panel_instance_t;

static GPtrArray *runtime_panel_providers = NULL;

static void *runtime_panel_run_action (const mc_panel_plugin_t *plugin, void *plugin_data,
                                       mc_panel_host_t *host, const char *open_path,
                                       int action_index);
static void runtime_panel_provider_free (runtime_panel_provider_t *provider);

static gboolean
runtime_panel_set_error (const char **error, const char *value)
{
    if (error != NULL)
        *error = value;
    return FALSE;
}

static void
runtime_panel_response_clear (runtime_panel_provider_t *provider,
                              mc_runtime_panel_provider_response_t *response)
{
    if (provider->response_free != NULL)
        provider->response_free (provider->context, response);
}

static gboolean
runtime_panel_dispatch (runtime_panel_provider_t *provider,
                        mc_runtime_panel_provider_operation_t operation,
                        const mc_runtime_panel_provider_request_t *request,
                        mc_runtime_panel_provider_response_t *response, const char **error)
{
    memset (response, 0, sizeof (*response));
    response->struct_size = sizeof (*response);
    response->operation_version = 1;
    if (provider == NULL || !provider->active || provider->dispatch == NULL)
        return runtime_panel_set_error (error, "closed");
    return provider->dispatch (provider->context, provider->runtime_provider_id, operation, request,
                               response, error);
}

static void
runtime_panel_instance_clear_view (runtime_panel_instance_t *instance)
{
    size_t i;

    g_clear_pointer (&instance->title, g_free);
    g_clear_pointer (&instance->location, g_free);
    g_clear_pointer (&instance->footer, g_free);
    g_clear_pointer (&instance->focus_id, g_free);
    g_clear_pointer (&instance->view_help_node, g_free);
    g_clear_pointer (&instance->default_format, g_free);
    for (i = 0; i < instance->columns_count; i++)
    {
        g_free ((char *) instance->columns[i].id);
        g_free ((char *) instance->columns[i].title);
    }
    g_clear_pointer (&instance->columns, g_free);
    instance->columns_count = 0;
    g_hash_table_remove_all (instance->name_to_id);
    g_hash_table_remove_all (instance->id_to_name);
    g_hash_table_remove_all (instance->column_values);
    g_hash_table_remove_all (instance->entry_help);
}

static void *
runtime_panel_open (const mc_panel_plugin_t *plugin, mc_panel_host_t *host, const char *open_path)
{
    runtime_panel_provider_t *provider;
    runtime_panel_instance_t *instance;
    mc_runtime_panel_provider_request_t request = { 0 };
    mc_runtime_panel_provider_response_t response;
    const char *error = NULL;
    const char *path = open_path != NULL ? open_path : "";

    if (plugin == NULL || plugin->plugin_context == NULL)
        return NULL;
    provider = (runtime_panel_provider_t *) plugin->plugin_context;
    if (provider->prefix != NULL && g_str_has_prefix (path, provider->prefix))
        path += strlen (provider->prefix);
    request.struct_size = sizeof (request);
    request.operation_version = 1;
    request.path = path;
    if (!runtime_panel_dispatch (provider, MC_RUNTIME_PANEL_PROVIDER_OPEN, &request, &response,
                                 &error)
        || response.instance_id == 0)
    {
        if (host != NULL && host->message != NULL)
            host->message (host, 1, "Lua panel", error != NULL ? error : "Cannot open provider");
        runtime_panel_response_clear (provider, &response);
        return NULL;
    }

    instance = g_new0 (runtime_panel_instance_t, 1);
    instance->provider = provider;
    instance->host = host;
    instance->runtime_instance_id = response.instance_id;
    instance->name_to_id = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    instance->id_to_name = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    instance->column_values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                                     (GDestroyNotify) g_hash_table_destroy);
    instance->entry_help = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
    provider->open_instances++;
    runtime_panel_response_clear (provider, &response);
    return instance;
}

static void
runtime_panel_close (void *plugin_data)
{
    runtime_panel_instance_t *instance = (runtime_panel_instance_t *) plugin_data;
    mc_runtime_panel_provider_request_t request = { 0 };
    mc_runtime_panel_provider_response_t response;

    if (instance == NULL)
        return;
    request.struct_size = sizeof (request);
    request.operation_version = 1;
    request.instance_id = instance->runtime_instance_id;
    if (instance->provider->active)
    {
        (void) runtime_panel_dispatch (instance->provider, MC_RUNTIME_PANEL_PROVIDER_CLOSE,
                                       &request, &response, NULL);
        runtime_panel_response_clear (instance->provider, &response);
    }
    if (instance->provider->open_instances > 0)
        instance->provider->open_instances--;
    runtime_panel_instance_clear_view (instance);
    g_hash_table_destroy (instance->name_to_id);
    g_hash_table_destroy (instance->id_to_name);
    g_hash_table_destroy (instance->column_values);
    g_hash_table_destroy (instance->entry_help);
    if (!instance->provider->active && instance->provider->open_instances == 0)
        runtime_panel_provider_free (instance->provider);
    g_free (instance);
}

static mc_pp_result_t
runtime_panel_get_items (void *plugin_data, void *list)
{
    runtime_panel_instance_t *instance = (runtime_panel_instance_t *) plugin_data;
    mc_runtime_panel_provider_request_t request = { 0 };
    mc_runtime_panel_provider_response_t response;
    const char *error = NULL;
    guint i;

    request.struct_size = sizeof (request);
    request.operation_version = 1;
    request.instance_id = instance->runtime_instance_id;
    if (!runtime_panel_dispatch (instance->provider, MC_RUNTIME_PANEL_PROVIDER_LIST, &request,
                                 &response, &error))
        return MC_PPR_FAILED;
    if (response.view.revision == 0 || response.view.entries_count > 100000
        || (response.view.entries_count != 0 && response.view.entries == NULL))
    {
        runtime_panel_response_clear (instance->provider, &response);
        return MC_PPR_FAILED;
    }

    runtime_panel_instance_clear_view (instance);
    instance->revision = response.view.revision;
    instance->title = g_strdup (response.view.title);
    instance->location = g_strdup (response.view.location);
    instance->footer = g_strdup (response.view.footer);
    instance->focus_id = g_strdup (response.view.focus_id);
    instance->view_help_node = g_strdup (response.view.help_node);
    instance->default_format = g_strdup (response.view.default_format);
    instance->columns_count = response.view.columns_count;
    instance->columns = g_new0 (mc_panel_column_t, instance->columns_count);
    for (i = 0; i < response.view.columns_count; i++)
    {
        const mc_runtime_panel_column_t *column = &response.view.columns[i];

        instance->columns[i].id = g_strdup (column->id);
        instance->columns[i].title = g_strdup (column->title);
        instance->columns[i].min_size = (int) column->min_width;
        instance->columns[i].expands = column->expands;
        instance->columns[i].default_just = column->align == MC_RUNTIME_PANEL_ALIGN_RIGHT ? J_RIGHT
            : column->align == MC_RUNTIME_PANEL_ALIGN_CENTER                              ? J_CENTER
                                                                                          : J_LEFT;
        instance->columns[i].use_in_user_format = column->user_format;
    }
    for (i = 0; i < response.view.entries_count; i++)
    {
        const mc_runtime_panel_entry_t *entry = &response.view.entries[i];
        mode_t type;

        if (entry->id == NULL || entry->id[0] == '\0' || entry->name == NULL
            || entry->name[0] == '\0' || strchr (entry->name, '/') != NULL
            || g_hash_table_contains (instance->id_to_name, entry->id)
            || g_hash_table_contains (instance->name_to_id, entry->name))
        {
            runtime_panel_response_clear (instance->provider, &response);
            return MC_PPR_FAILED;
        }
        switch (entry->kind)
        {
        case MC_RUNTIME_PANEL_ENTRY_DIRECTORY:
            type = S_IFDIR;
            break;
        case MC_RUNTIME_PANEL_ENTRY_SYMLINK:
            type = S_IFLNK;
            break;
        case MC_RUNTIME_PANEL_ENTRY_FILE:
        case MC_RUNTIME_PANEL_ENTRY_SPECIAL:
            type = S_IFREG;
            break;
        default:
            runtime_panel_response_clear (instance->provider, &response);
            return MC_PPR_FAILED;
        }
        g_hash_table_insert (instance->name_to_id, g_strdup (entry->name), g_strdup (entry->id));
        g_hash_table_insert (instance->id_to_name, g_strdup (entry->id), g_strdup (entry->name));
        if (entry->help_node != NULL)
            g_hash_table_insert (instance->entry_help, g_strdup (entry->name),
                                 g_strdup (entry->help_node));
        if (entry->columns_count != 0)
        {
            GHashTable *values = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
            guint j;

            for (j = 0; j < entry->columns_count; j++)
                g_hash_table_insert (values, g_strdup (entry->columns[j].id),
                                     g_strdup (entry->columns[j].value));
            g_hash_table_insert (instance->column_values, g_strdup (entry->name), values);
        }
        mc_pp_add_entry (
            list, entry->name,
            type | ((entry->mode != 0 ? entry->mode : (type == S_IFDIR ? 0755 : 0644)) & 07777),
            (off_t) entry->size, (time_t) entry->mtime);
    }
    runtime_panel_response_clear (instance->provider, &response);
    return MC_PPR_OK;
}

static mc_pp_result_t
runtime_panel_navigate (runtime_panel_instance_t *instance,
                        mc_runtime_panel_provider_operation_t operation, const char *value)
{
    mc_runtime_panel_provider_request_t request = { 0 };
    mc_runtime_panel_provider_response_t response;
    const char *entry_id = NULL;
    gboolean ok;
    gboolean close;

    if (operation == MC_RUNTIME_PANEL_PROVIDER_NAVIGATE_ENTRY)
    {
        entry_id = (const char *) g_hash_table_lookup (instance->name_to_id, value);
        if (entry_id == NULL)
            return MC_PPR_FAILED;
    }
    request.struct_size = sizeof (request);
    request.operation_version = 1;
    request.instance_id = instance->runtime_instance_id;
    request.revision = instance->revision;
    request.entry_id = entry_id;
    request.path = value;
    ok = runtime_panel_dispatch (instance->provider, operation, &request, &response, NULL);
    close = response.close;
    if (ok && response.focus_id != NULL && instance->host != NULL)
    {
        const char *name =
            (const char *) g_hash_table_lookup (instance->id_to_name, response.focus_id);
        if (name != NULL)
        {
            g_free (instance->host->focus_after);
            instance->host->focus_after = g_strdup (name);
        }
    }
    runtime_panel_response_clear (instance->provider, &response);
    return ok ? (close ? MC_PPR_CLOSE : MC_PPR_OK) : MC_PPR_FAILED;
}

static mc_pp_result_t
runtime_panel_chdir (void *plugin_data, const char *path)
{
    runtime_panel_instance_t *instance = (runtime_panel_instance_t *) plugin_data;

    if (g_strcmp0 (path, "..") == 0)
        return runtime_panel_navigate (instance, MC_RUNTIME_PANEL_PROVIDER_NAVIGATE_PARENT, NULL);
    return runtime_panel_navigate (instance, MC_RUNTIME_PANEL_PROVIDER_NAVIGATE_ENTRY, path);
}

static mc_pp_result_t
runtime_panel_enter (void *plugin_data, const char *name, const struct stat *st)
{
    (void) st;
    return runtime_panel_chdir (plugin_data, name);
}

static mc_pp_result_t
runtime_panel_view (void *plugin_data, const char *name, const struct stat *st, gboolean plain_view)
{
    runtime_panel_instance_t *instance = plugin_data;
    mc_runtime_panel_provider_request_t request = { 0 };
    mc_runtime_panel_provider_response_t response;
    const char *entry_id;
    gboolean ok;

    (void) st;
    entry_id = g_hash_table_lookup (instance->name_to_id, name);
    if (entry_id == NULL)
        return MC_PPR_FAILED;
    request.struct_size = sizeof (request);
    request.operation_version = 1;
    request.instance_id = instance->runtime_instance_id;
    request.revision = instance->revision;
    request.entry_id = entry_id;
    request.path = plain_view ? "plain" : "view";
    ok = runtime_panel_dispatch (instance->provider, MC_RUNTIME_PANEL_PROVIDER_VIEW, &request,
                                 &response, NULL);
    ok = ok && response.handled;
    runtime_panel_response_clear (instance->provider, &response);
    return ok ? MC_PPR_OK : MC_PPR_NOT_SUPPORTED;
}

static const char *
runtime_panel_get_title (void *plugin_data)
{
    runtime_panel_instance_t *instance = (runtime_panel_instance_t *) plugin_data;
    return instance->title != NULL ? instance->title : instance->provider->title;
}

static const char *
runtime_panel_get_footer (void *plugin_data)
{
    return ((runtime_panel_instance_t *) plugin_data)->footer;
}

static const char *
runtime_panel_get_focus (void *plugin_data)
{
    runtime_panel_instance_t *instance = (runtime_panel_instance_t *) plugin_data;
    return instance->focus_id != NULL
        ? (const char *) g_hash_table_lookup (instance->id_to_name, instance->focus_id)
        : NULL;
}

static char *
runtime_panel_get_location (void *plugin_data)
{
    return g_strdup (((runtime_panel_instance_t *) plugin_data)->location);
}

static const mc_panel_column_t *
runtime_panel_get_columns (void *plugin_data, size_t *count)
{
    runtime_panel_instance_t *instance = (runtime_panel_instance_t *) plugin_data;

    *count = instance->columns_count;
    return instance->columns;
}

static const char *
runtime_panel_get_column_value (void *plugin_data, const char *name, const char *column_id)
{
    runtime_panel_instance_t *instance = (runtime_panel_instance_t *) plugin_data;
    GHashTable *values = g_hash_table_lookup (instance->column_values, name);

    return values != NULL ? g_hash_table_lookup (values, column_id) : NULL;
}

static const char *
runtime_panel_get_default_format (void *plugin_data)
{
    return ((runtime_panel_instance_t *) plugin_data)->default_format;
}

static mc_pp_result_t
runtime_panel_get_help_info (void *plugin_data, const char **filename, const char **node)
{
    runtime_panel_instance_t *instance = (runtime_panel_instance_t *) plugin_data;
    const char *name = NULL;
    const char *entry_node = NULL;

    if (instance->host != NULL && instance->host->get_current != NULL)
    {
        const GString *current = instance->host->get_current (instance->host);

        if (current != NULL)
            name = current->str;
    }
    if (name != NULL)
        entry_node = g_hash_table_lookup (instance->entry_help, name);
    *filename = instance->provider->help_file;
    *node = entry_node != NULL             ? entry_node
        : instance->view_help_node != NULL ? instance->view_help_node
                                           : instance->provider->help_node;
    return *filename != NULL || *node != NULL ? MC_PPR_OK : MC_PPR_NOT_SUPPORTED;
}

static void *
runtime_panel_run_action (const mc_panel_plugin_t *plugin, void *plugin_data, mc_panel_host_t *host,
                          const char *open_path, int action_index)
{
    runtime_panel_provider_t *provider = plugin != NULL ? plugin->plugin_context : NULL;
    runtime_panel_instance_t *instance = plugin_data;
    const mc_runtime_panel_action_t *action;
    mc_runtime_panel_provider_request_t request = { 0 };
    mc_runtime_panel_provider_response_t response;
    GPtrArray *selected;
    int cursor = 0;

    (void) open_path;
    if (provider == NULL || action_index < 0 || (guint) action_index >= provider->actions_count)
        return NULL;
    action = &provider->actions[action_index];
    if (action->open_path != NULL)
        return runtime_panel_open (plugin, host, action->open_path);
    if (instance == NULL)
        return NULL;

    selected = g_ptr_array_new ();
    if (host != NULL && host->get_marked_count != NULL && host->get_next_marked != NULL)
    {
        int remaining = host->get_marked_count (host);

        while (remaining-- > 0)
        {
            const GString *name = host->get_next_marked (host, &cursor);
            const char *id =
                name != NULL ? g_hash_table_lookup (instance->name_to_id, name->str) : NULL;
            if (id != NULL)
                g_ptr_array_add (selected, (gpointer) id);
        }
    }
    if (selected->len == 0 && host != NULL && host->get_current != NULL)
    {
        const GString *name = host->get_current (host);
        const char *id =
            name != NULL ? g_hash_table_lookup (instance->name_to_id, name->str) : NULL;

        if (id != NULL)
            g_ptr_array_add (selected, (gpointer) id);
    }
    request.struct_size = sizeof (request);
    request.operation_version = 1;
    request.instance_id = instance->runtime_instance_id;
    request.revision = instance->revision;
    request.action_id = action->id;
    request.selected_ids = (const char *const *) selected->pdata;
    request.selected_count = selected->len;
    if (runtime_panel_dispatch (provider, MC_RUNTIME_PANEL_PROVIDER_INVOKE_ACTION, &request,
                                &response, NULL))
    {
        if (response.status != NULL && host != NULL && host->set_hint != NULL)
            host->set_hint (host, response.status);
        if (response.focus_id != NULL && host != NULL)
        {
            const char *name = g_hash_table_lookup (instance->id_to_name, response.focus_id);

            if (name != NULL)
            {
                g_free (host->focus_after);
                host->focus_after = g_strdup (name);
            }
        }
        if (response.refresh && host != NULL && host->refresh != NULL)
            host->refresh (host);
    }
    runtime_panel_response_clear (provider, &response);
    g_ptr_array_free (selected, TRUE);
    return NULL;
}

static void
runtime_panel_provider_free (runtime_panel_provider_t *provider)
{
    guint i;

    for (i = 0; i < provider->actions_count; i++)
    {
        g_free ((char *) provider->actions[i].id);
        g_free ((char *) provider->actions[i].title);
        g_free ((char *) provider->actions[i].key);
        g_free ((char *) provider->actions[i].menu_path);
        g_free ((char *) provider->actions[i].menu_label);
        g_free ((char *) provider->actions[i].help_node);
        g_free ((char *) provider->actions[i].open_path);
    }
    g_free (provider->actions);
    g_free (provider->native_actions);
    g_free (provider->menu_entries);
    g_free (provider->id);
    g_free (provider->title);
    g_free (provider->prefix);
    g_free (provider->proto);
    g_free (provider->help_file);
    g_free (provider->help_node);
    g_free (provider);
}

gboolean
runtime_panel_provider_register (mc_runtime_plugin_context_t *context,
                                 const mc_runtime_panel_provider_t *source,
                                 mc_runtime_handle_t *registration, const char **error)
{
    runtime_panel_provider_t *provider;
    const char *colon;
    guint i;

    if (source == NULL || registration == NULL || source->struct_size < sizeof (*source)
        || source->api_version != 1 || source->id == NULL || source->id[0] == '\0'
        || source->title == NULL || source->prefix == NULL || source->dispatch == NULL
        || source->response_free == NULL)
        return runtime_panel_set_error (error, "invalid_provider");
    colon = strchr (source->prefix, ':');
    if (colon == NULL || colon[1] != '\0' || colon == source->prefix)
        return runtime_panel_set_error (error, "invalid_provider");

    provider = g_new0 (runtime_panel_provider_t, 1);
    provider->context = context;
    provider->runtime_provider_id = source->runtime_provider_id;
    provider->dispatch = source->dispatch;
    provider->response_free = source->response_free;
    provider->id = g_strdup (source->id);
    provider->title = g_strdup (source->title);
    provider->prefix = g_strdup (source->prefix);
    provider->proto = g_strndup (source->prefix, (gsize) (colon - source->prefix));
    if (source->help != NULL)
    {
        provider->help_file = g_strdup (source->help->file);
        provider->help_node = g_strdup (source->help->node);
    }
    provider->actions_count = source->actions_count;
    provider->actions = g_new0 (mc_runtime_panel_action_t, provider->actions_count);
    provider->native_actions = g_new0 (mc_pp_action_t, provider->actions_count);
    provider->menu_entries = g_new0 (mc_pp_cmd_menu_entry_t, provider->actions_count);
    for (i = 0; i < provider->actions_count; i++)
    {
        provider->actions[i] = source->actions[i];
        provider->actions[i].id = g_strdup (source->actions[i].id);
        provider->actions[i].title = g_strdup (source->actions[i].title);
        provider->actions[i].key = g_strdup (source->actions[i].key);
        provider->actions[i].menu_path = g_strdup (source->actions[i].menu_path);
        provider->actions[i].menu_label = g_strdup (source->actions[i].menu_label);
        provider->actions[i].help_node = g_strdup (source->actions[i].help_node);
        provider->actions[i].open_path = g_strdup (source->actions[i].open_path);
        provider->native_actions[i].label = provider->actions[i].title;
        provider->menu_entries[i].label = provider->actions[i].menu_label != NULL
            ? provider->actions[i].menu_label
            : provider->actions[i].title;
        provider->menu_entries[i].shortcut = provider->actions[i].key;
        provider->menu_entries[i].menu_name = provider->actions[i].menu_path;
        provider->menu_entries[i].action_index = (int) i;
    }
    provider->active = TRUE;
    provider->plugin = (mc_panel_plugin_t) {
        .api_version = MC_PANEL_PLUGIN_API_VERSION,
        .name = provider->id,
        .display_name = provider->title,
        .proto = provider->proto,
        .prefix = provider->prefix,
        .flags =
            MC_PPF_NAVIGATE | MC_PPF_CUSTOM_TITLE | MC_PPF_SHOW_IN_MENU | MC_PPF_SHOW_IN_DRIVE_MENU,
        .close = runtime_panel_close,
        .get_items = runtime_panel_get_items,
        .chdir = runtime_panel_chdir,
        .enter = runtime_panel_enter,
        .view = runtime_panel_view,
        .get_title = runtime_panel_get_title,
        .get_footer = runtime_panel_get_footer,
        .get_focus_name = runtime_panel_get_focus,
        .get_location = runtime_panel_get_location,
        .get_columns = runtime_panel_get_columns,
        .get_column_value = runtime_panel_get_column_value,
        .get_default_format = runtime_panel_get_default_format,
        .get_help_info = runtime_panel_get_help_info,
        .plugin_context = provider,
        .open_with_plugin = runtime_panel_open,
        .run_action_with_plugin = runtime_panel_run_action,
        .actions = provider->native_actions,
        .action_count = (int) provider->actions_count,
        .cmd_menu_entries = provider->menu_entries,
        .cmd_menu_entry_count = (int) provider->actions_count,
    };
    if (!mc_panel_plugin_add (&provider->plugin))
    {
        runtime_panel_provider_free (provider);
        return runtime_panel_set_error (error, "duplicate_provider");
    }
    if (runtime_panel_providers == NULL)
        runtime_panel_providers = g_ptr_array_new ();
    g_ptr_array_add (runtime_panel_providers, provider);
    *registration = mc_runtime_handle_for_object (MC_RUNTIME_HANDLE_PANEL_PROVIDER, provider);
    return TRUE;
}

gboolean
runtime_panel_provider_unregister (const mc_runtime_handle_t *registration, const char **error)
{
    runtime_panel_provider_t *provider = (runtime_panel_provider_t *) mc_runtime_handle_resolve (
        registration, MC_RUNTIME_HANDLE_PANEL_PROVIDER);

    if (provider == NULL)
        return runtime_panel_set_error (error, "closed");
    provider->active = FALSE;
    (void) mc_panel_plugin_remove (&provider->plugin);
    mc_runtime_handle_invalidate_object (MC_RUNTIME_HANDLE_PANEL_PROVIDER, provider);
    if (runtime_panel_providers != NULL)
        g_ptr_array_remove_fast (runtime_panel_providers, provider);
    if (provider->open_instances == 0)
        runtime_panel_provider_free (provider);
    return TRUE;
}
