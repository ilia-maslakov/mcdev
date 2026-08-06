/*
   JSON pretty-printer used by the SQLite panel plugin.

   Copyright (C) 2026
   Free Software Foundation, Inc.

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

#ifndef MC__PANEL_PLUGIN_SQLITE_JSON_H
#define MC__PANEL_PLUGIN_SQLITE_JSON_H

#include <glib.h>

/* Return a newly allocated, formatted JSON value, or NULL if @json is not
   valid JSON.  Lines within the returned value are indented by @indent. */
char *sqlite_json_pretty (const char *json, gsize length, int indent);

#endif /* MC__PANEL_PLUGIN_SQLITE_JSON_H */
