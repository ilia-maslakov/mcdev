/*
   Docker panel plugin -- logs viewer integration.

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

#ifndef MC__PANEL_PLUGINS_DOCKER_LOGS_H
#define MC__PANEL_PLUGINS_DOCKER_LOGS_H

#include "lib/global.h"

#include "docker-internal.h"  // docker_connection_t

/* Identity + initial defaults for one logs viewer session. Caller fills
 * the fields; docker_logs_open() copies them (the connection is cloned), so
 * the caller keeps ownership. */
typedef struct
{
    docker_connection_t *conn; /* active profile; cloned by open. */
    char *container_id;        /* container id or name for `docker logs`. */
    char *container_name;      /* display name for title/announcement; may be NULL. */
    char *help_file;           /* path to the plugin's hlp file (NULL = default mc.hlp). */
    int options_key;           /* viewer keycode that opens the logs options dialog. */

    /* Initial dialog defaults (applied to live state before first prepare). */
    char *initial_since; /* "5m"; "" = unset. */
    int initial_tail;    /* 0 = unset; >0 -> docker logs --tail=N. */
    gboolean initial_follow;
    char *initial_pipe_through; /* may be NULL. */
} docker_logs_identity_t;

/* Open the logs viewer for the given container with the given initial params.
 * Builds the source-controller context, runs the initial docker command, hands
 * the viewer ownership of everything. Returns TRUE on success. */
gboolean docker_logs_open (const docker_logs_identity_t *id);

#endif /* MC__PANEL_PLUGINS_DOCKER_LOGS_H */
