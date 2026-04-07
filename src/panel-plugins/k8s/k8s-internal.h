/*
   Kubernetes resources panel plugin -- internal header.

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

#ifndef MC__PANEL_PLUGINS_K8S_INTERNAL_H
#define MC__PANEL_PLUGINS_K8S_INTERNAL_H

#include <sys/stat.h>
#include <time.h>

#include "lib/global.h"
#include "lib/keybind.h"
#include "lib/panel-plugin.h"
#include "lib/widget.h"

/* --------------------------------------------------------------------------------------------- */
/*** typedefs(not structures) and #defines *******************************************************/
/* --------------------------------------------------------------------------------------------- */

#define K8S_PANEL_CONFIG_FILE         "panels.k8s.ini"
#define K8S_PANEL_CONFIG_GROUP        "k8s-panel"
#define K8S_FAVORITES_CONFIG_GROUP    "k8s-favorites"
#define K8S_FAVORITES_CONFIG_KEY      "contexts"
#define K8S_PANEL_KEY_REFRESH         "hotkey_refresh"
#define K8S_PANEL_KEY_FAV_ADD         "hotkey_fav_add"
#define K8S_PANEL_KEY_NS_SWITCH       "hotkey_ns_switch"
#define K8S_PANEL_KEY_REFRESH_DEFAULT    "ctrl-r"
#define K8S_PANEL_KEY_FAV_ADD_DEFAULT    "ctrl-b"
#define K8S_PANEL_KEY_NS_SWITCH_DEFAULT  "ctrl-n"
#define K8S_PANEL_CONFIG_KUBECTL      "kubectl"
#define K8S_PANEL_CONFIG_KUBECONFIG   "kubeconfig"

/* --------------------------------------------------------------------------------------------- */
/*** enums ***************************************************************************************/
/* --------------------------------------------------------------------------------------------- */

typedef enum
{
    K8S_VIEW_FAVORITES = 0,
    K8S_VIEW_NAMESPACES,
    K8S_VIEW_RESOURCE_TYPES,
    K8S_VIEW_PODS,
    K8S_VIEW_POD_DETAILS,
    K8S_VIEW_DEPLOYMENTS,
    K8S_VIEW_SERVICES,
    K8S_VIEW_NODES
} k8s_view_t;

typedef enum
{
    K8S_ITEM_FAVORITE_CONTEXT = 0,
    K8S_ITEM_NAMESPACE,
    K8S_ITEM_RESOURCE_TYPE_DIR,
    K8S_ITEM_POD,
    K8S_ITEM_POD_DETAIL_DIR,
    K8S_ITEM_DEPLOYMENT,
    K8S_ITEM_SERVICE,
    K8S_ITEM_NODE,
    K8S_ITEM_INFO_FILE
} k8s_item_kind_t;

/* --------------------------------------------------------------------------------------------- */
/*** structures declarations *********************************************************************/
/* --------------------------------------------------------------------------------------------- */

typedef struct
{
    char *name;      /* display name / resource name */
    k8s_item_kind_t kind;
    gboolean is_dir;
    /* per-kind fields */
    char *status;    /* pod phase, node status */
    char *restarts;  /* pods */
    char *age;
    char *node_name; /* pods */
    char *ready;     /* deployments: "2/2" */
    char *svc_type;  /* services */
    char *cluster_ip;
    char *ports;
    char *roles;     /* nodes */
    char *version;   /* nodes */
} k8s_item_t;

typedef struct
{
    char *output;
    time_t fetched_at;
} k8s_cache_entry_t;

typedef struct
{
    mc_panel_host_t *host;
    k8s_view_t view;
    char *context;        /* active kubectl context */
    char *namespace;      /* active namespace */
    char *selected_pod;
    char *pending_focus;
    char *title_buf;      /* scratch buffer for get_title / get_footer */
    char *help_filename;
    char *kubectl_cmd;    /* kubectl binary path/name (raw, from config) */
    char *kubeconfig;     /* path to kubeconfig file, or NULL to use default */
    char *kubectl_full;   /* kubectl_cmd [--kubeconfig path] -- use this in commands */
    int key_refresh;
    int key_fav_add;
    int key_ns_switch;
    GPtrArray *items;           /* k8s_item_t*, current view; NULL means needs reload */
    GPtrArray *fav_contexts;    /* char* list of saved context names */
    GHashTable *cache;          /* char* key -> k8s_cache_entry_t* */
} k8s_data_t;

/* --------------------------------------------------------------------------------------------- */
/*** extern declarations *************************************************************************/
/* --------------------------------------------------------------------------------------------- */

extern const char k8s_version_file[];
extern const char k8s_cluster_info_file[];
extern const char k8s_logs_entry[];
extern const char k8s_exec_entry[];
extern const char k8s_describe_entry[];
extern const char k8s_yaml_entry[];

/* --------------------------------------------------------------------------------------------- */
/*** function declarations ***********************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* k8s.c */
void k8s_item_free (gpointer p);
void k8s_clear_items (k8s_data_t *data);
const k8s_item_t *k8s_find_item (const k8s_data_t *data, const char *name);
void k8s_set_pending_focus (k8s_data_t *data, const char *name);

/* k8s-cmd.c */
gboolean k8s_run_cmd (const char *cmd, char **output, char **err_text);
char *k8s_capture_output (const char *cmd);
char *k8s_load_kubectl_cmd (void);
char *k8s_load_kubeconfig (void);
char *k8s_build_kubectl (const k8s_data_t *data);
char *k8s_get_current_context (const char *kubectl_cmd);
int k8s_load_hotkey (const char *key, const char *fallback_text, int fallback_key);
GPtrArray *k8s_favorites_load (void);
void k8s_favorites_save (GPtrArray *favs);
void k8s_cache_entry_free (gpointer p);

/* pods.c */
gboolean k8s_pods_reload (k8s_data_t *data, char **err_text);
void k8s_pods_reload_details (k8s_data_t *data);
mc_pp_result_t k8s_pods_exec (k8s_data_t *data);
mc_pp_result_t k8s_pods_view (k8s_data_t *data, const char *fname);
mc_pp_result_t k8s_pods_delete (k8s_data_t *data, const char **names, int count);
const mc_panel_column_t *k8s_pods_get_columns (size_t *count);
const char *k8s_pods_get_column_value (k8s_data_t *data, const char *name, const char *col);
const char *k8s_pods_get_default_format (void);

/* workloads.c */
gboolean k8s_deployments_reload (k8s_data_t *data, char **err_text);
mc_pp_result_t k8s_deployments_view (k8s_data_t *data, const char *fname);
mc_pp_result_t k8s_deployments_delete (k8s_data_t *data, const char **names, int count);
const mc_panel_column_t *k8s_deployments_get_columns (size_t *count);
const char *k8s_deployments_get_column_value (k8s_data_t *data, const char *name,
                                               const char *col);
const char *k8s_deployments_get_default_format (void);

/* services.c */
gboolean k8s_services_reload (k8s_data_t *data, char **err_text);
mc_pp_result_t k8s_services_view (k8s_data_t *data, const char *fname);
mc_pp_result_t k8s_services_delete (k8s_data_t *data, const char **names, int count);
const mc_panel_column_t *k8s_services_get_columns (size_t *count);
const char *k8s_services_get_column_value (k8s_data_t *data, const char *name, const char *col);
const char *k8s_services_get_default_format (void);

/* nodes.c */
gboolean k8s_nodes_reload (k8s_data_t *data, char **err_text);
const mc_panel_column_t *k8s_nodes_get_columns (size_t *count);
const char *k8s_nodes_get_column_value (k8s_data_t *data, const char *name, const char *col);
const char *k8s_nodes_get_default_format (void);

/* k8s-ui.c */
void k8s_ui_view_command (const char *cmd);
gboolean k8s_ui_show_ns_switch_dialog (k8s_data_t *data);
gboolean k8s_ui_show_add_context_dialog (char **ctx_out);

#endif /* MC__PANEL_PLUGINS_K8S_INTERNAL_H */
