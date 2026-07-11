/*
   Panel view modes -- a dynamic, user-editable list of named listing formats.

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

#ifndef MC__FILEMANAGER_PANEL_MODES_H
#define MC__FILEMANAGER_PANEL_MODES_H

#include "lib/global.h"

#include "panel.h"  // WPanel

/*** typedefs(not structures) and defined constants **********************************************/

/*** enums ***************************************************************************************/

/*** structures declarations (and typedefs of structures) ****************************************/

/* One view mode. `types`/`widths` are comma-separated lists: each `types`
 * entry is a native panel field id (name, size, mtime, perm, ...); each
 * `widths` entry is an int, 0 (or missing) meaning auto width. The same for
 * the status line. `id` is a stable key that survives rename/reorder. */
typedef struct
{
    guint id;
    char *name;
    char *types;
    char *widths;
    char *status_types;
    char *status_widths;
} panel_mode_t;

/*** global variables defined in .c file *********************************************************/

/*** declarations of public functions ************************************************************/

void panel_modes_init (void);
void panel_modes_deinit (void);
void panel_modes_save (void);

guint panel_modes_count (void);
panel_mode_t *panel_modes_get (guint index);
panel_mode_t *panel_modes_get_by_id (guint id);
int panel_modes_index_by_id (guint id);

panel_mode_t *panel_modes_add (const char *name);
panel_mode_t *panel_modes_dup (guint index);
void panel_modes_remove (guint index);

void panel_mode_set (panel_mode_t *mode, const char *name, const char *types, const char *widths,
                     const char *status_types, const char *status_widths);

/* Build an mc listing-format string from a mode. `status` selects the status
 * line definition instead of the columns. Caller frees the result. */
char *panel_mode_to_format (const panel_mode_t *mode, gboolean status);

/* Normalize user input into the editor's comma lists: accepts a pasted
 * listing format string too (optional "half"/"full" prefix, "|" column
 * separators, a leading flowed-column count and ":width" field suffixes,
 * which move into the widths list). Both results are newly allocated. */
void panel_mode_normalize (const char *types, const char *widths, char **norm_types,
                           char **norm_widths);

/* Validate comma lists of field types and widths. Returns TRUE if every type
 * is a known field id and every width is a non-negative integer; otherwise
 * FALSE and *error (caller frees) describes the problem. */
gboolean panel_mode_validate (const char *types, const char *widths, char **error);

/* Apply a mode to a panel: synthesize the format strings, switch the panel to
 * the user listing format and enable the user status line, then re-layout.
 * No-op for plugin panels (they control their own columns). */
void panel_apply_mode (WPanel *panel, const panel_mode_t *mode);

/* Switcher dialog (Left/Right menu, Alt-t): a bare list; Enter applies the
 * selected mode to the panel. */
void panel_modes_cmd (WPanel *panel);

/* Management dialog (Options menu): edit the global list of modes
 * (New/Edit/Delete); does not switch any panel. */
void panel_modes_manage_cmd (void);

/*** inline functions ****************************************************************************/

#endif /* MC__FILEMANAGER_PANEL_MODES_H */
