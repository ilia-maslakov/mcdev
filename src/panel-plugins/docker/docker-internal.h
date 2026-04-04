#ifndef MC__PANEL_PLUGINS_DOCKER_INTERNAL_H
#define MC__PANEL_PLUGINS_DOCKER_INTERNAL_H

#include <sys/stat.h>

#include "lib/global.h"
#include "lib/keybind.h"
#include "lib/panel-plugin.h"
#include "lib/widget.h"

#include "src/panel-plugins/docker/docker-cp-stream.h"
#include "src/panel-plugins/docker/docker-tar.h"

typedef enum
{
    DOCKER_VIEW_ROOT = 0,
    DOCKER_VIEW_CONTAINERS_PROJECTS,
    DOCKER_VIEW_CONTAINERS_ITEMS,
    DOCKER_VIEW_CONTAINER_DETAILS,
    DOCKER_VIEW_CONTAINER_FILES,
    DOCKER_VIEW_CONTAINER_MOUNTS,
    DOCKER_VIEW_IMAGES,
    DOCKER_VIEW_VOLUMES,
    DOCKER_VIEW_NETWORKS
} docker_view_t;

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

typedef struct
{
    mc_panel_host_t *host;

    docker_view_t view;
    GPtrArray *items;

    char *root_focus;
    char *current_project;
    char *current_container_id;
    char *current_container_name;
    char *files_cwd;
    GHashTable *files_cache;

    char *title_buf;
} docker_data_t;

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

extern const char docker_daemon_info_file[];
extern const char docker_version_file[];
extern const char docker_inspect_file[];
extern const char docker_exec_entry[];
extern const char docker_files_entry[];
extern const char docker_logs_entry[];
extern const char docker_mounts_entry[];
extern const char docker_ungrouped_project[];

void docker_item_free (gpointer p);
docker_item_t *docker_item_clone (const docker_item_t *item);
GPtrArray *docker_items_clone (const GPtrArray *items);
void clear_files_cache (docker_data_t *data);
void clear_items (docker_data_t *data);
const docker_item_t *find_item_by_name (const docker_data_t *data, const char *name);
docker_view_t view_from_root_path (const char *path);
gboolean run_cmd (const char *cmd, char **output, char **err_text);
char *strip_trailing_newlines (char *text);
char *docker_capture_output (const char *cmd);
char *docker_capture_inspect_field (const char *container_id, const char *format);
gboolean write_temp_content (const char *prefix, const char *content, char **local_path);
void set_view (docker_data_t *data, docker_view_t new_view);
gboolean docker_ui_view_container_summary (const docker_container_summary_t *summary);
gboolean docker_ui_view_volume_summary (const docker_volume_summary_t *summary);
gboolean docker_ui_viewer_command (const char *cmd);
gboolean docker_ui_show_create_container_dialog (char **image, char **name, char **command,
                                                 gboolean *detach);

gboolean docker_containers_is_ungrouped_project (const char *project);
gboolean docker_containers_reload_projects (docker_data_t *data, char **err_text);
gboolean docker_containers_reload_items (docker_data_t *data, char **err_text);
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

gboolean docker_container_files_reload (docker_data_t *data, char **err_text);
gboolean docker_container_mounts_reload (docker_data_t *data, char **err_text);
mc_pp_result_t docker_container_files_chdir (docker_data_t *data, const char *path);
mc_pp_result_t docker_container_files_enter_mounts (docker_data_t *data, const char *name);
mc_pp_result_t docker_container_files_get_local_copy (docker_data_t *data, const char *fname,
                                                      char **local_path);
const char *docker_container_files_get_default_format (docker_data_t *data);

gboolean docker_images_reload (docker_data_t *data, char **err_text);
mc_pp_result_t docker_images_delete_items (docker_data_t *data, const char **names, int count);

gboolean docker_volumes_reload (docker_data_t *data, char **err_text);
gboolean docker_volumes_view_summary (docker_data_t *data, const char *fname);
mc_pp_result_t docker_volumes_delete_items (docker_data_t *data, const char **names, int count);
const mc_panel_column_t *docker_volumes_get_columns (size_t *count);
const char *docker_volumes_get_column_value (docker_data_t *data, const char *fname,
                                             const char *column_id);
const char *docker_volumes_get_default_format (void);

gboolean docker_networks_reload (docker_data_t *data, char **err_text);
mc_pp_result_t docker_networks_delete_items (docker_data_t *data, const char **names, int count);

#endif /* MC__PANEL_PLUGINS_DOCKER_INTERNAL_H */
