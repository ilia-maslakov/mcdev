/*
   Midnight Commander - mcterm key encoding.

   Translates MC keycodes back to terminal byte sequences for forwarding
   to the PTY child process.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   Written by:
   Ilia Maslakov <il.smind@gmail.com>, 2026

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

#ifndef MC__MCTERM_KEY_H
#define MC__MCTERM_KEY_H

#include "lib/global.h"
#include "lib/mcconfig.h"

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures)*****************************************/

/*** declarations of public functions ************************************************************/

void mcterm_key_table_init (const char *global_config_path, mc_config_t *cfg);
size_t mcterm_encode_key_xterm (int key, unsigned char *buf, size_t bufsz, gboolean app_cursor);

/*** inline functions ****************************************************************************/

#endif /* MC__MCTERM_KEY_H */
