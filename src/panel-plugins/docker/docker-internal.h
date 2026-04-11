#ifndef MC__PANEL_PLUGINS_DOCKER_INTERNAL_H
#define MC__PANEL_PLUGINS_DOCKER_INTERNAL_H

#include <sys/stat.h>

#include "lib/global.h"
#include "lib/keybind.h"
#include "lib/panel-plugin.h"
#include "lib/widget.h"

#include "src/panel-plugins/docker/docker-cp-stream.h"
#include "src/panel-plugins/docker/docker-tar.h"

/*** Connection model ***/

typedef enum
{
    DOCKER_CONN_LOCAL = 0,
    DOCKER_CONN_SSH
} docker_conn_type_t;

typedef struct
{
    char *id;    /* stable ASCII id used in paths, e.g. "local", "prod" */
    char *label; /* display name, e.g. "Local Docker", "Production" */
    docker_conn_type_t type;
    char *docker_path; /* docker executable path, NULL means "docker" */
    /* SSH fields (only for DOCKER_CONN_SSH) */
    char *host;     /* required for SSH */
    char *user;     /* optional SSH username, NULL = current user */
    int port;       /* optional SSH port, 0 = default */
    char *key_path; /* optional private key file, NULL = default */
} docker_connection_t;

/*** View enum ***/

typedef enum
{
    DOCKER_VIEW_PROFILES = 0, /* profile list - initial view */
    DOCKER_VIEW_ROOT,         /* docker resource root for active profile */
    DOCKER_VIEW_CONTAINERS_PROJECTS,
    DOCKER_VIEW_CONTAINERS_ITEMS,
    DOCKER_VIEW_CONTAINER_DETAILS,
    DOCKER_VIEW_CONTAINER_FILES,
    DOCKER_VIEW_CONTAINER_MOUNTS,
    DOCKER_VIEW_IMAGES,
    DOCKER_VIEW_VOLUMES,
    DOCKER_VIEW_NETWORKS
} docker_view_t;

/*** Item model ***/

typedef struct
{
    char *name; /* display name */
    char *id;   /* real object id/name used by docker CLI */
    gboolean is_dir;
    gboolean is_link;
    off_t size;
    char *link_target;

    char *status;
    char *image;
    char *ports;
    char *driver;
    char *scope;
    char *created;
} docker_item_t;

typedef struct
{
    simple_status_msg_t status_msg;
    const char *text;
} docker_files_status_msg_t;

/*** Plugin instance state ***/

typedef struct
{
    mc_panel_host_t *host;

    docker_view_t view;
    GPtrArray *items;

    /* Connection/profile state */
    GPtrArray *connections;           /* all loaded profiles, owned */
    docker_connection_t *active_conn; /* currently active profile, not owned */
    char *connections_file;

    char *root_focus;
    char *current_project;
    char *current_container_id;
    char *current_container_name;
    char *files_cwd;
    GHashTable *files_cache;
    GHashTable *files_focus_cache; /* key: container_id + cwd, value: focused entry name */

    char *title_buf;
    char *help_filename;

    char *pending_focus;         /* item to focus after next reload (nav back-cache) */
    GArray *nav_stack;           /* navigation back-cache: stack of docker_nav_frame_t */
    GPtrArray *projects_cache;   /* cached CONTAINERS_PROJECTS listing; reused on re-entry */
    GPtrArray *containers_cache; /* cached CONTAINERS_ITEMS listing */
    char *containers_cache_proj; /* project id the containers_cache belongs to */
} docker_data_t;

/*** Summary structs for viewer ***/

typedef struct
{
    const char *name;
    const char *id;
    const char *image;
    const char *status;
    const char *started;
    const char *restart_count;
    const char *command;
    const char *entrypoint;
    const char *ports;
    const char *ip;
    const char *mounts;
    const char *memory_limit;
    const char *cpu_shares;
    const char *stats;
} docker_container_summary_t;

typedef struct
{
    const char *name;
    const char *scope;
    const char *mountpoint;
    const char *created;
    const char *state;
    const char *labels;
    const char *options;
} docker_volume_summary_t;

/*** String constants ***/

extern const char docker_daemon_info_file[];
extern const char docker_version_file[];
extern const char docker_inspect_file[];
extern const char docker_exec_entry[];
extern const char docker_files_entry[];
extern const char docker_logs_entry[];
extern const char docker_mounts_entry[];
extern const char docker_ungrouped_project[];

/*** Connection lifecycle ***/

docker_connection_t *docker_connection_new_local (void);
docker_connection_t *docker_connection_clone (const docker_connection_t *conn);
void docker_connection_free (gpointer p);
const char *docker_connection_get_docker_path (const docker_connection_t *conn);

/*** Config ***/

char *docker_connections_get_file_path (void);
GPtrArray *docker_connections_load (const char *path);
gboolean docker_connections_save (const char *path, GPtrArray *connections);
docker_connection_t *docker_connections_find_by_id (GPtrArray *connections, const char *id);
docker_connection_t *docker_connections_find_by_label (GPtrArray *connections, const char *label);
docker_connection_t *docker_connections_find_default_local (GPtrArray *connections);

/*** Utilities ***/

void docker_item_free (gpointer p);
docker_item_t *docker_item_clone (const docker_item_t *item);
GPtrArray *docker_items_clone (const GPtrArray *items);
void clear_files_cache (docker_data_t *data);
gboolean docker_parse_port (const char *port_str, int *port_out);
void clear_items (docker_data_t *data);
const docker_item_t *find_item_by_name (const docker_data_t *data, const char *name);
docker_view_t view_from_root_path (const char *path);

/* Low-level command runner (used internally and by docker_conn_run) */
gboolean run_cmd (const char *cmd, char **output, char **err_text);
char *strip_trailing_newlines (char *text);

/*** Profile-aware command execution ***/

gboolean docker_conn_run (const docker_connection_t *conn, const char *docker_args, char **output,
                          char **err_text);
char *docker_conn_capture (const docker_connection_t *conn, const char *docker_args);
char *docker_conn_capture_inspect (const docker_connection_t *conn, const char *obj_id,
                                   const char *format);

/* Build a shell command string for the given connection (for exec/logs viewer, interactive) */
char *docker_conn_build_shell_cmd (const docker_connection_t *conn, const char *docker_args);
/* Build a non-interactive pipe command (for TAR streaming, batch operations) */
char *docker_conn_build_pipe_cmd (const docker_connection_t *conn, const char *docker_args);

/*** Other utilities ***/

gboolean write_temp_content (const char *prefix, const char *content, char **local_path);
void set_view (docker_data_t *data, docker_view_t new_view);
const char *docker_get_path (docker_data_t *data);

/*** UI ***/

gboolean docker_ui_view_container_summary (const docker_container_summary_t *summary);
gboolean docker_ui_view_volume_summary (const docker_volume_summary_t *summary);
gboolean docker_ui_viewer_command (const char *cmd);
gboolean docker_ui_show_create_container_dialog (char **image, char **name, char **command,
                                                 gboolean *detach);
gboolean docker_ui_show_connection_dialog (docker_connection_t *conn, gboolean is_new,
                                           const char *help_file);

/*** Containers ***/

gboolean docker_containers_is_ungrouped_project (const char *project);
gboolean docker_containers_reload_projects (docker_data_t *data, char **err_text);
gboolean docker_containers_reload_items (docker_data_t *data, char **err_text);
gboolean docker_containers_resolve_current (docker_data_t *data, char **err_text);
gboolean docker_containers_reload_details (docker_data_t *data);
mc_pp_result_t docker_containers_enter (docker_data_t *data, const char *name);
gboolean docker_containers_view_summary (docker_data_t *data, const char *fname);
gboolean docker_containers_view_logs (docker_data_t *data, const char *fname);
mc_pp_result_t docker_containers_get_local_copy (docker_data_t *data, const char *fname,
                                                 char **local_path);
mc_pp_result_t docker_containers_delete_items (docker_data_t *data, const char **names, int count);
mc_pp_result_t docker_containers_create_item (docker_data_t *data);
const mc_panel_column_t *docker_containers_get_columns (size_t *count);
const char *docker_containers_get_column_value (docker_data_t *data, const char *fname,
                                                const char *column_id);
const char *docker_containers_get_default_format (void);

/*** Container files ***/

gboolean docker_container_files_reload (docker_data_t *data, char **err_text);
gboolean docker_container_mounts_reload (docker_data_t *data, char **err_text);
mc_pp_result_t docker_container_files_chdir (docker_data_t *data, const char *path);
mc_pp_result_t docker_container_files_enter_mounts (docker_data_t *data, const char *name);
mc_pp_result_t docker_container_files_get_local_copy (docker_data_t *data, const char *fname,
                                                      char **local_path);
const char *docker_container_files_get_default_format (docker_data_t *data);

/*** Images ***/

gboolean docker_images_reload (docker_data_t *data, char **err_text);
mc_pp_result_t docker_images_delete_items (docker_data_t *data, const char **names, int count);

/*** Volumes ***/

gboolean docker_volumes_reload (docker_data_t *data, char **err_text);
gboolean docker_volumes_view_summary (docker_data_t *data, const char *fname);
mc_pp_result_t docker_volumes_delete_items (docker_data_t *data, const char **names, int count);
const mc_panel_column_t *docker_volumes_get_columns (size_t *count);
const char *docker_volumes_get_column_value (docker_data_t *data, const char *fname,
                                             const char *column_id);
const char *docker_volumes_get_default_format (void);

/*** Networks ***/

gboolean docker_networks_reload (docker_data_t *data, char **err_text);
mc_pp_result_t docker_networks_delete_items (docker_data_t *data, const char **names, int count);

#endif /* MC__PANEL_PLUGINS_DOCKER_INTERNAL_H */
