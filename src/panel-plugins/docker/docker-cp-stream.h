#ifndef MC__PANEL_PLUGINS_DOCKER_CP_STREAM_H
#define MC__PANEL_PLUGINS_DOCKER_CP_STREAM_H

#include <sys/types.h>

#include "lib/global.h"

typedef struct
{
    int fd;
    int errfd;
    pid_t child_pid;
} docker_cp_stream_t;

gboolean docker_cp_stream_open (const char *container_id, const char *container_path,
                                docker_cp_stream_t *stream, char **err_text);
char *docker_cp_stream_read_stderr (docker_cp_stream_t *stream);
void docker_cp_stream_reap (docker_cp_stream_t *stream);

#endif /* MC__PANEL_PLUGINS_DOCKER_CP_STREAM_H */
