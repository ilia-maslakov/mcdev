/*
   Structured view panel plugin.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.
 */

#include <config.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "lib/global.h"
#include "lib/keybind.h"
#include "lib/panel-plugin.h"
#include "lib/widget.h"

#include "src/filemanager/dir.h"
#include "src/mctree/mctree-resolver.h"
#include "src/mctree/mctree-view.h"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    char *id;
    char *text;
    mctree_node_t *node;
} mctree_panel_row_t;

typedef struct
{
    mc_panel_host_t *host;
    char *path;
    char *title;
    char *diagnostic;
    mctree_resolver_config_t config;
    mctree_resolver_result_t result;
    mctree_model_t *model;
    mctree_view_t *view;
    GPtrArray *rows;
    GHashTable *id_to_row;
    gboolean skip_next_reload;
} mctree_panel_data_t;

/*** forward declarations (file scope functions) *************************************************/

static void *mctree_open (mc_panel_host_t *host, const char *open_path);
static void mctree_close (void *plugin_data);
static mc_pp_result_t mctree_get_items (void *plugin_data, void *list_ptr);
static mc_pp_result_t mctree_handle_key (void *plugin_data, int key);
static mc_pp_result_t mctree_reload (void *plugin_data);
static const mc_panel_column_t *mctree_get_columns (void *plugin_data, size_t *count);
static const char *mctree_get_column_value (void *plugin_data, const char *fname,
                                            const char *column_id);
static const char *mctree_get_title (void *plugin_data);
static const char *mctree_get_footer (void *plugin_data);
static const char *mctree_get_default_format (void *plugin_data);
static void *mctree_action_current_file (mc_panel_host_t *host, const char *open_path);

/*** file scope variables ************************************************************************/

static const mc_panel_column_t mctree_columns[] = {
    { "tree", N_ ("Tree"), 40, TRUE, J_LEFT, TRUE },
};

static const mc_pp_action_t mctree_actions[] = {
    { N_ ("Structured &view"), mctree_action_current_file },
};

static const mc_pp_cmd_menu_entry_t mctree_menu[] = {
    { N_ ("Structured &view"), 0, NULL, 0, MC_PP_MENU_PANEL },
};

static const mc_panel_plugin_t mctree_plugin = {
    .api_version = MC_PANEL_PLUGIN_API_VERSION,
    .name = "mctree",
    .display_name = N_ ("Structured view"),
    .proto = "mctree",
    .prefix = "mctree:",
    .flags = MC_PPF_CUSTOM_TITLE | MC_PPF_SHOW_IN_MENU,

    .open = mctree_open,
    .close = mctree_close,
    .get_items = mctree_get_items,

    .handle_key = mctree_handle_key,
    .get_columns = mctree_get_columns,
    .get_column_value = mctree_get_column_value,
    .get_title = mctree_get_title,
    .get_footer = mctree_get_footer,
    .get_default_format = mctree_get_default_format,
    .reload = mctree_reload,

    .actions = mctree_actions,
    .action_count = G_N_ELEMENTS (mctree_actions),
    .cmd_menu_entries = mctree_menu,
    .cmd_menu_entry_count = G_N_ELEMENTS (mctree_menu),
    .default_sort_id = "name",
    .default_sort_reverse = FALSE,
};

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
mctree_panel_row_free (mctree_panel_row_t *row)
{
    if (row == NULL)
        return;

    g_free (row->id);
    g_free (row->text);
    g_free (row);
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_display_value (const mctree_node_t *node)
{
    if (node == NULL)
        return g_strdup ("");

    if (node->value != NULL)
        return g_strdup (node->value);

    if (mctree_node_child_count (node) > 0 && !node->expanded)
        return g_strdup_printf ("(%u children)", mctree_node_descendant_count (node));

    return g_strdup ("");
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_format_row_text (const mctree_visible_row_t *visible)
{
    const mctree_node_t *node;
    char *indent;
    char *value;
    char *text;
    const char *marker;
    const char *label;

    node = visible->node;
    indent = g_strnfill ((gsize) visible->depth * 2, ' ');
    value = mctree_display_value (node);
    marker = mctree_node_child_count (node) == 0 ? " " : (node->expanded ? "-" : "+");
    label = node->key != NULL ? node->key : mctree_node_type_name (node->type);

    if (value[0] != '\0')
        text = g_strdup_printf ("%s%s %s: %s", indent, marker, label, value);
    else
        text = g_strdup_printf ("%s%s %s", indent, marker, label);

    g_free (value);
    g_free (indent);
    return text;
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_panel_clear_rows (mctree_panel_data_t *data)
{
    if (data->id_to_row != NULL)
        g_hash_table_remove_all (data->id_to_row);
    if (data->rows != NULL)
        g_ptr_array_set_size (data->rows, 0);
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_panel_rebuild_rows (mctree_panel_data_t *data)
{
    guint i;

    mctree_panel_clear_rows (data);
    mctree_view_rebuild (data->view);

    for (i = 0; i < mctree_view_row_count (data->view); i++)
    {
        mctree_visible_row_t *visible;
        mctree_panel_row_t *row;

        visible = &g_array_index (data->view->rows, mctree_visible_row_t, i);
        row = g_new0 (mctree_panel_row_t, 1);
        row->id = g_strdup_printf ("%06u", i);
        row->text = mctree_format_row_text (visible);
        row->node = visible->node;

        g_hash_table_insert (data->id_to_row, row->id, row);
        g_ptr_array_add (data->rows, row);
    }
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_panel_sync_cursor (mctree_panel_data_t *data)
{
    const GString *current;
    mctree_panel_row_t *row;
    guint i;

    if (data == NULL || data->host == NULL || data->host->get_current == NULL)
        return FALSE;

    current = data->host->get_current (data->host);
    if (current == NULL || current->str == NULL)
        return FALSE;

    row = (mctree_panel_row_t *) g_hash_table_lookup (data->id_to_row, current->str);
    if (row == NULL)
        return FALSE;

    for (i = 0; i < data->rows->len; i++)
    {
        if (g_ptr_array_index (data->rows, i) == row)
        {
            data->view->cursor = (int) i;
            return TRUE;
        }
    }

    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_panel_set_diagnostic (mctree_panel_data_t *data, const char *text)
{
    g_free (data->diagnostic);
    data->diagnostic = g_strdup (text);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_panel_load (mctree_panel_data_t *data)
{
    GError *error = NULL;

    g_clear_pointer (&data->model, mctree_model_free);
    mctree_resolver_result_clear (&data->result);
    mctree_panel_clear_rows (data);

    data->model = mctree_resolve_file (data->path, &data->config, &data->result, &error);
    if (data->model == NULL)
    {
        if (error != NULL)
        {
            mctree_panel_set_diagnostic (data, error->message);
            g_error_free (error);
        }
        else if (data->result.too_large)
            mctree_panel_set_diagnostic (data, _ ("File is too large for structured view"));
        else if (data->result.diagnostic != NULL)
            mctree_panel_set_diagnostic (data, data->result.diagnostic);
        else
            mctree_panel_set_diagnostic (data, _ ("Cannot build structured view"));

        mctree_view_set_model (data->view, NULL);
        return FALSE;
    }

    mctree_panel_set_diagnostic (data, NULL);
    mctree_view_set_model (data->view, data->model);
    mctree_panel_rebuild_rows (data);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_path_from_open_path (const char *open_path)
{
    if (open_path == NULL || open_path[0] == '\0')
        return NULL;

    if (g_str_has_prefix (open_path, "mctree:"))
        return g_strdup (open_path + strlen ("mctree:"));

    return g_strdup (open_path);
}

/* --------------------------------------------------------------------------------------------- */

static mctree_panel_data_t *
mctree_panel_data_new (mc_panel_host_t *host, const char *path)
{
    mctree_panel_data_t *data;
    const char *base;

    if (path == NULL || path[0] == '\0')
        return NULL;

    data = g_new0 (mctree_panel_data_t, 1);
    data->host = host;
    data->path = g_strdup (path);
    base = strrchr (path, PATH_SEP);
    data->title = g_strdup_printf ("structured:%s", base != NULL ? base + 1 : path);
    mctree_resolver_config_init (&data->config);
    data->view = mctree_view_new ();
    data->rows = g_ptr_array_new_with_free_func ((GDestroyNotify) mctree_panel_row_free);
    data->id_to_row = g_hash_table_new (g_str_hash, g_str_equal);

    mctree_panel_load (data);
    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void *
mctree_open (mc_panel_host_t *host, const char *open_path)
{
    char *path;
    mctree_panel_data_t *data;

    path = mctree_path_from_open_path (open_path);
    data = mctree_panel_data_new (host, path);
    g_free (path);

    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_close (void *plugin_data)
{
    mctree_panel_data_t *data = (mctree_panel_data_t *) plugin_data;

    if (data == NULL)
        return;

    mctree_resolver_result_clear (&data->result);
    g_clear_pointer (&data->model, mctree_model_free);
    mctree_view_free (data->view);
    g_hash_table_remove_all (data->id_to_row);
    g_ptr_array_free (data->rows, TRUE);
    g_hash_table_destroy (data->id_to_row);
    g_free (data->path);
    g_free (data->title);
    g_free (data->diagnostic);
    g_free (data);
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
mctree_get_items (void *plugin_data, void *list_ptr)
{
    mctree_panel_data_t *data = (mctree_panel_data_t *) plugin_data;
    guint i;

    if (data->diagnostic != NULL)
    {
        mc_pp_add_entry (list_ptr, data->diagnostic, S_IFREG | 0444, 0, time (NULL));
        return MC_PPR_OK;
    }

    mctree_panel_rebuild_rows (data);
    for (i = 0; i < data->rows->len; i++)
    {
        mctree_panel_row_t *row = (mctree_panel_row_t *) g_ptr_array_index (data->rows, i);

        mc_pp_add_entry (list_ptr, row->id, S_IFREG | 0444, 0, time (NULL));
    }

    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
mctree_handle_key (void *plugin_data, int key)
{
    mctree_panel_data_t *data = (mctree_panel_data_t *) plugin_data;
    gboolean changed = FALSE;

    if (data == NULL || data->diagnostic != NULL)
        return MC_PPR_FAILED;

    mctree_panel_sync_cursor (data);

    switch (key)
    {
    case CK_Enter:
    case CK_Right:
        changed = mctree_view_expand_current (data->view);
        break;
    case CK_Left:
        changed = mctree_view_collapse_current (data->view);
        break;
    default:
        return MC_PPR_FAILED;
    }

    if (!changed)
        return MC_PPR_FAILED;

    mctree_panel_rebuild_rows (data);
    data->skip_next_reload = TRUE;
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_pp_result_t
mctree_reload (void *plugin_data)
{
    mctree_panel_data_t *data = (mctree_panel_data_t *) plugin_data;

    if (data == NULL)
        return MC_PPR_FAILED;

    if (data->skip_next_reload)
    {
        data->skip_next_reload = FALSE;
        return MC_PPR_OK;
    }

    mctree_panel_load (data);
    return MC_PPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static const mc_panel_column_t *
mctree_get_columns (void *plugin_data, size_t *count)
{
    (void) plugin_data;

    if (count != NULL)
        *count = G_N_ELEMENTS (mctree_columns);

    return mctree_columns;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mctree_get_column_value (void *plugin_data, const char *fname, const char *column_id)
{
    mctree_panel_data_t *data = (mctree_panel_data_t *) plugin_data;
    mctree_panel_row_t *row;

    if (data == NULL || fname == NULL || column_id == NULL || strcmp (column_id, "tree") != 0)
        return NULL;

    row = (mctree_panel_row_t *) g_hash_table_lookup (data->id_to_row, fname);
    return row != NULL ? row->text : fname;
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mctree_get_title (void *plugin_data)
{
    mctree_panel_data_t *data = (mctree_panel_data_t *) plugin_data;

    return data != NULL && data->title != NULL ? data->title : "structured";
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mctree_get_footer (void *plugin_data)
{
    mctree_panel_data_t *data = (mctree_panel_data_t *) plugin_data;

    if (data == NULL || data->result.provider == NULL)
        return NULL;

    return mctree_content_type_name (data->result.content_type);
}

/* --------------------------------------------------------------------------------------------- */

static const char *
mctree_get_default_format (void *plugin_data)
{
    (void) plugin_data;
    return "half tree";
}

/* --------------------------------------------------------------------------------------------- */

static void *
mctree_action_current_file (mc_panel_host_t *host, const char *open_path)
{
    const GString *current;
    char *path;
    mctree_panel_data_t *data;

    if (host == NULL || host->get_current == NULL)
        return NULL;

    current = host->get_current (host);
    if (current == NULL || current->str == NULL || strcmp (current->str, "..") == 0)
        return NULL;

    path = g_build_filename (open_path != NULL ? open_path : ".", current->str, (char *) NULL);
    data = mctree_panel_data_new (host, path);
    if (data != NULL && data->diagnostic != NULL && host->message != NULL)
        host->message (host, D_ERROR, _ ("Structured view"), data->diagnostic);
    g_free (path);

    return data;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

const mc_panel_plugin_t *mc_panel_plugin_register (void);

const mc_panel_plugin_t *
mc_panel_plugin_register (void)
{
    return &mctree_plugin;
}

/* --------------------------------------------------------------------------------------------- */
