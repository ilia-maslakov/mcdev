/*
   Archive browser panel plugin -config persistence.

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

#ifndef ARCMC_CONFIG_H
#define ARCMC_CONFIG_H

#include "lib/global.h"

/*** constants *********************************************************************************/

#define ARCMC_BUILTIN_COUNT 13

/*** global variables **************************************************************************/

extern gboolean arcmc_builtin_enabled[ARCMC_BUILTIN_COUNT];
extern gboolean *arcmc_ext_enabled;

/*** declarations (functions) ******************************************************************/

void arcmc_config_load (void);
void arcmc_config_save (void);

#endif /* ARCMC_CONFIG_H */
