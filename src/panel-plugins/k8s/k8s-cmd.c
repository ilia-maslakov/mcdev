/*
   Kubernetes panel plugin -- kubectl helpers, config, favorites.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026.

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
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/tty/key.h"

#include "src/panel-plugins/k8s/k8s-internal.h"

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static char *
k8s_read_config_string (const char *key, const char *fallback)
{
    char *cfg_path;
    mc_config_t *cfg;
    char *value;

    cfg_path = g_build_filename (mc_config_get_path (), K8S_PANEL_CONFIG_FILE, (char *) NULL);
    cfg = mc_config_init (cfg_path, TRUE);
    g_free (cfg_path);

    value = mc_config_get_string (cfg, K8S_PANEL_CONFIG_GROUP, key, NULL);
    mc_config_deinit (cfg);

    if (value == NULL || value[0] == '\0')
    {
        g_free (value);
        return g_strdup (fallback);
    }
    return value;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
k8s_run_cmd (const char *cmd, char **output, char **err_text)
{
    gchar *std_out = NULL;
    gchar *std_err = NULL;
    gint status = 0;
    GError *error = NULL;
    gboolean spawned;
    gboolean exited_ok;

    spawned = g_spawn_command_line_sync (cmd, &std_out, &std_err, &status, &error);
    if (!spawned)
    {
        if (err_text != NULL)
        {
            if (error != NULL && error->message != NULL)
                *err_text = g_strdup (error->message);
            else
                *err_text = g_strdup (_ ("Failed to start kubectl"));
        }
        if (output != NULL)
            *output = NULL;
        if (error != NULL)
            g_error_free (error);
        g_free (std_out);
        g_free (std_err);
        return FALSE;
    }

#if GLIB_CHECK_VERSION(2, 70, 0)
    exited_ok = g_spawn_check_wait_status (status, NULL);
#else
    exited_ok = g_spawn_check_exit_status (status, NULL);
#endif

    /* On failure callers bail out without freeing *output, so don't hand it
       back: free stdout here and return NULL. stderr still goes to err_text. */
    if (output != NULL)
        *output = exited_ok ? std_out : NULL;
    if (!exited_ok || output == NULL)
        g_free (std_out);

    if (err_text != NULL)
        *err_text = std_err;
    else
        g_free (std_err);

    return exited_ok;
}

/* --------------------------------------------------------------------------------------------- */

char *
k8s_capture_output (const char *cmd)
{
    char *output = NULL;
    char *err_text = NULL;
    size_t len;

    if (!k8s_run_cmd (cmd, &output, &err_text))
    {
        g_free (output);
        output = NULL;
    }
    g_free (err_text);

    if (output == NULL)
        return NULL;

    len = strlen (output);
    while (len > 0 && (output[len - 1] == '\n' || output[len - 1] == '\r'))
        output[--len] = '\0';

    return output;
}

/* --------------------------------------------------------------------------------------------- */

char *
k8s_load_kubectl_cmd (void)
{
    return k8s_read_config_string (K8S_PANEL_CONFIG_KUBECTL, "kubectl");
}

/* --------------------------------------------------------------------------------------------- */

char *
k8s_load_kubeconfig (void)
{
    return k8s_read_config_string (K8S_PANEL_CONFIG_KUBECONFIG, NULL);
}

/* --------------------------------------------------------------------------------------------- */

char *
k8s_build_kubectl (const k8s_data_t *data)
{
    if (data->kubeconfig != NULL && data->kubeconfig[0] != '\0')
    {
        char *quoted = g_shell_quote (data->kubeconfig);
        char *result = g_strdup_printf ("%s --kubeconfig %s", data->kubectl_cmd, quoted);
        g_free (quoted);
        return result;
    }
    return g_strdup (data->kubectl_cmd);
}

/* --------------------------------------------------------------------------------------------- */

char *
k8s_get_current_context (const char *kubectl_cmd)
{
    char *cmd;
    char *result;

    cmd = g_strdup_printf ("%s config current-context", kubectl_cmd);
    result = k8s_capture_output (cmd);
    g_free (cmd);
    return result;
}

/* --------------------------------------------------------------------------------------------- */

int
k8s_load_hotkey (const char *key, const char *fallback_text, int fallback_key)
{
    char *value;
    int result;

    value = k8s_read_config_string (key, fallback_text);

    if (value == NULL || value[0] == '\0')
    {
        g_free (value);
        return fallback_key;
    }
    if (g_ascii_strcasecmp (value, "none") == 0)
    {
        g_free (value);
        return 0;
    }

    result = tty_keyname_to_keycode (value, NULL);
    g_free (value);
    return result != 0 ? tty_normalize_keycode (result) : fallback_key;
}

/* --------------------------------------------------------------------------------------------- */

GPtrArray *
k8s_favorites_load (void)
{
    char *cfg_path;
    mc_config_t *cfg;
    gchar **list;
    gsize len = 0;
    GPtrArray *favs;
    gsize i;

    cfg_path = g_build_filename (mc_config_get_path (), K8S_PANEL_CONFIG_FILE, (char *) NULL);
    cfg = mc_config_init (cfg_path, TRUE);
    g_free (cfg_path);

    favs = g_ptr_array_new_with_free_func (g_free);
    list =
        mc_config_get_string_list (cfg, K8S_FAVORITES_CONFIG_GROUP, K8S_FAVORITES_CONFIG_KEY, &len);
    if (list != NULL)
    {
        for (i = 0; i < len; i++)
            if (list[i] != NULL && list[i][0] != '\0')
                g_ptr_array_add (favs, g_strdup (list[i]));
        g_strfreev (list);
    }
    mc_config_deinit (cfg);
    return favs;
}

/* --------------------------------------------------------------------------------------------- */

void
k8s_favorites_save (GPtrArray *favs)
{
    char *cfg_path;
    mc_config_t *cfg;

    cfg_path = g_build_filename (mc_config_get_path (), K8S_PANEL_CONFIG_FILE, (char *) NULL);
    cfg = mc_config_init (cfg_path, FALSE);
    g_free (cfg_path);

    if (favs->len == 0)
        mc_config_del_key (cfg, K8S_FAVORITES_CONFIG_GROUP, K8S_FAVORITES_CONFIG_KEY);
    else
        mc_config_set_string_list (cfg, K8S_FAVORITES_CONFIG_GROUP, K8S_FAVORITES_CONFIG_KEY,
                                   (const gchar **) favs->pdata, favs->len);
    mc_config_save_file (cfg, NULL);
    mc_config_deinit (cfg);
}

/* --------------------------------------------------------------------------------------------- */

/* Per-context namespace favorites. Stored in panels.k8s.ini section
 * [k8s-ns-favs] with one key per context: <ctx> = ns1;ns2;... */
GPtrArray *
k8s_ns_favs_load (const char *context)
{
    char *cfg_path;
    mc_config_t *cfg;
    gchar **list;
    gsize len = 0;
    GPtrArray *favs;
    gsize i;

    favs = g_ptr_array_new_with_free_func (g_free);
    if (context == NULL || context[0] == '\0')
        return favs;

    cfg_path = g_build_filename (mc_config_get_path (), K8S_PANEL_CONFIG_FILE, (char *) NULL);
    cfg = mc_config_init (cfg_path, TRUE);
    g_free (cfg_path);

    list = mc_config_get_string_list (cfg, K8S_NS_FAVS_CONFIG_GROUP, context, &len);
    if (list != NULL)
    {
        for (i = 0; i < len; i++)
            if (list[i] != NULL && list[i][0] != '\0')
                g_ptr_array_add (favs, g_strdup (list[i]));
        g_strfreev (list);
    }
    mc_config_deinit (cfg);
    return favs;
}

/* --------------------------------------------------------------------------------------------- */

void
k8s_ns_favs_save (const char *context, GPtrArray *favs)
{
    char *cfg_path;
    mc_config_t *cfg;

    if (context == NULL || context[0] == '\0')
        return;

    cfg_path = g_build_filename (mc_config_get_path (), K8S_PANEL_CONFIG_FILE, (char *) NULL);
    cfg = mc_config_init (cfg_path, FALSE);
    g_free (cfg_path);

    if (favs == NULL || favs->len == 0)
        mc_config_del_key (cfg, K8S_NS_FAVS_CONFIG_GROUP, context);
    else
        mc_config_set_string_list (cfg, K8S_NS_FAVS_CONFIG_GROUP, context,
                                   (const gchar **) favs->pdata, favs->len);
    mc_config_save_file (cfg, NULL);
    mc_config_deinit (cfg);
}

/* --------------------------------------------------------------------------------------------- */
