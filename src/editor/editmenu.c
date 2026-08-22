/*
   Editor menu definitions and initialisation

   Copyright (C) 1996-2025
   Free Software Foundation, Inc.

   Written by:
   Paul Sheer, 1996, 1997
   Andrew Borodin <aborodin@vmail.ru> 2012

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

/** \file
 *  \brief Source: editor menu definitions and initialisation
 *  \author Paul Sheer
 *  \date 1996, 1997
 */

#include <config.h>

#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <stdlib.h>

#include "lib/global.h"

#include "lib/editor-plugin.h"
#include "lib/extension-runtime.h"
#include "lib/plugin-prefs.h"
#include "lib/tty/key.h"  // ALT
#include "lib/widget.h"

#include "src/setup.h"  // drop_menus

#include "edit-impl.h"
#include "editwidget.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

#define MC_EDITOR_RUNTIME_ACTION_BASE 1000000L

/*** file scope type declarations ****************************************************************/

/*** forward declarations (file scope functions) *************************************************/

/*** file scope variables ************************************************************************/

/* Indices of top-level menus in the menubar; set in edit_init_menu().
 * menu_idx_navigate is -1 when no plugin contributes Navigate entries. */
static int menu_idx_file = 0;
static int menu_idx_edit = 1;
static int menu_idx_search = 2;
static int menu_idx_command = 3;
static int menu_idx_navigate = -1;
static int menu_idx_window = 4;
static int menu_idx_plugins = 5;
static int menu_idx_options = 6;
static GPtrArray *runtime_menu_actions = NULL;

typedef struct
{
    char *runtime_name;
    char *id;
    char *menu_path;
    char *label;
    char *shortcut;
    gint position;
    guint registration_order;
} edit_runtime_menu_action_t;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static void
edit_runtime_menu_action_free (gpointer data)
{
    edit_runtime_menu_action_t *action = (edit_runtime_menu_action_t *) data;

    if (action == NULL)
        return;
    g_free (action->runtime_name);
    g_free (action->id);
    g_free (action->menu_path);
    g_free (action->label);
    g_free (action->shortcut);
    g_free (action);
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_runtime_menu_collect (const char *runtime_name, const char *id, const char *menu_path,
                           const char *label, const char *shortcut, gint position,
                           gpointer user_data)
{
    GPtrArray *actions = (GPtrArray *) user_data;
    edit_runtime_menu_action_t *action;

    if (actions == NULL || runtime_name == NULL || id == NULL || menu_path == NULL
        || menu_path[0] == '\0' || label == NULL || label[0] == '\0')
        return;

    action = g_new0 (edit_runtime_menu_action_t, 1);
    action->runtime_name = g_strdup (runtime_name);
    action->id = g_strdup (id);
    action->menu_path = g_strdup (menu_path);
    action->label = g_strdup (label);
    action->shortcut = g_strdup (shortcut);
    action->position = position;
    action->registration_order = actions->len;
    g_ptr_array_add (actions, action);
}

/* --------------------------------------------------------------------------------------------- */

static gint
edit_runtime_menu_action_compare (gconstpointer left, gconstpointer right)
{
    const edit_runtime_menu_action_t *a = *(const edit_runtime_menu_action_t *const *) left;
    const edit_runtime_menu_action_t *b = *(const edit_runtime_menu_action_t *const *) right;
    int result;

    result = g_strcmp0 (a->menu_path, b->menu_path);
    if (result != 0)
        return result;
    if (a->position != b->position)
        return a->position < b->position ? -1 : 1;
    if (a->registration_order != b->registration_order)
        return a->registration_order < b->registration_order ? -1 : 1;
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_runtime_menu_reload (void)
{
    g_clear_pointer (&runtime_menu_actions, g_ptr_array_unref);
    runtime_menu_actions = g_ptr_array_new_with_free_func (edit_runtime_menu_action_free);
    mc_runtime_plugins_enumerate_menu_actions ("mcedit", edit_runtime_menu_collect,
                                               runtime_menu_actions);
    g_ptr_array_sort (runtime_menu_actions, edit_runtime_menu_action_compare);
}

/* --------------------------------------------------------------------------------------------- */

static GList *
create_runtime_menu_entries (const char *menu_path)
{
    GList *entries = NULL;
    guint i;

    for (i = 0; runtime_menu_actions != NULL && i < runtime_menu_actions->len; i++)
    {
        const edit_runtime_menu_action_t *action =
            (const edit_runtime_menu_action_t *) g_ptr_array_index (runtime_menu_actions, i);
        menu_entry_t *entry;

        if (g_strcmp0 (action->menu_path, menu_path) != 0)
            continue;

        entry = menu_entry_new (action->label, MC_EDITOR_RUNTIME_ACTION_BASE + (long) i);
        if (action->shortcut != NULL)
            menu_entry_set_shortcut (entry, action->shortcut);
        entries = g_list_prepend (entries, entry);
    }

    return g_list_reverse (entries);
}

/* --------------------------------------------------------------------------------------------- */

static GList *
append_runtime_menu_entries (GList *entries, const char *menu_path)
{
    GList *runtime_entries = create_runtime_menu_entries (menu_path);

    if (runtime_entries == NULL)
        return entries;
    if (entries != NULL)
        entries = g_list_append (entries, menu_separator_new ());
    return g_list_concat (entries, runtime_entries);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
edit_runtime_menu_is_builtin (const char *menu_path)
{
    static const char *const names[] = { "File",   "Edit",    "Search", "Command", "Navigate",
                                         "Window", "Plugins", "Lua",    "Options", NULL };
    int i;

    for (i = 0; names[i] != NULL; i++)
        if (g_strcmp0 (menu_path, names[i]) == 0)
            return TRUE;
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

static GList *
create_file_menu (void)
{
    GList *entries = NULL;

    entries = g_list_prepend (entries, menu_entry_new (_ ("&Open file..."), CK_EditFile));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&New"), CK_EditNew));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Close"), CK_Close));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&History..."), CK_History));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Save"), CK_Save));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Save &as..."), CK_SaveAs));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Insert file..."), CK_InsertFile));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Cop&y to file..."), CK_BlockSave));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&User menu..."), CK_UserMenu));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("A&bout..."), CK_About));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Quit"), CK_Quit));

    return g_list_reverse (entries);
}

/* --------------------------------------------------------------------------------------------- */

static GList *
create_edit_menu (void)
{
    GList *entries = NULL;

    entries = g_list_prepend (entries, menu_entry_new (_ ("&Undo"), CK_Undo));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Redo"), CK_Redo));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Undo &History..."), CK_UndoHistory));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries =
        g_list_prepend (entries, menu_entry_new (_ ("&Toggle ins/overw"), CK_InsertOverwrite));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("To&ggle mark"), CK_Mark));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Mark columns"), CK_MarkColumn));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Mark &all"), CK_MarkAll));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Unmar&k"), CK_Unmark));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("Cop&y"), CK_Copy));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Mo&ve"), CK_Move));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Delete"), CK_Remove));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("Co&py to clipfile"), CK_Store));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Cut to clipfile"), CK_Cut));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Pa&ste from clipfile"), CK_Paste));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Beginning"), CK_Top));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&End"), CK_Bottom));

    return g_list_reverse (entries);
}

/* --------------------------------------------------------------------------------------------- */

static GList *
create_search_replace_menu (void)
{
    GList *entries = NULL;

    entries = g_list_prepend (entries, menu_entry_new (_ ("&Search..."), CK_Search));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Search &again"), CK_SearchContinue));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Replace..."), CK_Replace));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Toggle bookmark"), CK_Bookmark));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Next bookmark"), CK_BookmarkNext));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Prev bookmark"), CK_BookmarkPrev));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Flush bookmarks"), CK_BookmarkFlush));

    return g_list_reverse (entries);
}

/* --------------------------------------------------------------------------------------------- */

static GList *
create_command_menu (void)
{
    GList *entries = NULL;

    entries = g_list_prepend (entries, menu_entry_new (_ ("&Go to line..."), CK_Goto));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Toggle line state"), CK_ShowNumbers));
    entries =
        g_list_prepend (entries, menu_entry_new (_ ("Go to matching &bracket"), CK_MatchBracket));
    entries = g_list_prepend (entries,
                              menu_entry_new (_ ("Toggle s&yntax highlighting"), CK_SyntaxOnOff));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Togg&le right margin"), CK_ShowMargin));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("Encod&ing..."), CK_SelectCodepage));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Fold / Unfold block"), CK_FoldToggle));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Unfold all"), CK_UnfoldAll));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Refresh screen"), CK_Refresh));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (
        entries, menu_entry_new (_ ("&Start/Stop record macro"), CK_MacroStartStopRecord));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Macro e&xplorer..."), CK_MacroExplorer));
    entries = g_list_prepend (
        entries, menu_entry_new (_ ("Record/Repeat &actions"), CK_RepeatStartStopRecord));

    return g_list_reverse (entries);
}

/* --------------------------------------------------------------------------------------------- */

/**
 * Create the 'window' popup menu
 */

static GList *
create_window_menu (void)
{
    GList *entries = NULL;

    entries = g_list_prepend (entries, menu_entry_new (_ ("&Move"), CK_WindowMove));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Resize"), CK_WindowResize));
    entries =
        g_list_prepend (entries, menu_entry_new (_ ("&Toggle fullscreen"), CK_WindowFullscreen));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Next"), CK_WindowNext));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Previous"), CK_WindowPrev));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&List..."), CK_WindowList));

    return g_list_reverse (entries);
}

/* --------------------------------------------------------------------------------------------- */

static GList *
create_options_menu (void)
{
    GList *entries = NULL;

    entries = g_list_prepend (entries, menu_entry_new (_ ("&General..."), CK_Options));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Save &mode..."), CK_OptionsSaveMode));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Learn &keys..."), CK_LearnKeys));
    entries =
        g_list_prepend (entries, menu_entry_new (_ ("Syntax &highlighting..."), CK_SyntaxChoose));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("Pl&ugin info..."), CK_EditPluginsInfo));
#ifdef ENABLE_LUA_PLUGIN
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Lua scripts..."), CK_EditLuaScripts));
#endif
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("S&yntax file"), CK_EditSyntaxFile));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Menu file"), CK_EditUserMenu));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Save setup"), CK_SaveSetup));

    return g_list_reverse (entries);
}

/* --------------------------------------------------------------------------------------------- */

static GList *
create_plugins_menu (void)
{
    const GSList *plugins;
    GList *entries = NULL;
    long command_id = MC_EDITOR_PLUGIN_CMD_BASE;

    for (plugins = mc_editor_plugin_list (); plugins != NULL; plugins = g_slist_next (plugins))
    {
        const mc_editor_plugin_t *plugin = (const mc_editor_plugin_t *) plugins->data;
        const char *label;

        /* command_id must keep its 1:1 correspondence with the plugin
           list position, so we increment it even when skipping a plugin. */
        if (plugin->activate == NULL || (plugin->flags & MC_EPF_HAS_MENU) == 0
            || (plugin->name != NULL
                && mc_plugin_prefs_is_disabled (MC_PLUGIN_KIND_EDITOR, plugin->name)))
        {
            command_id++;
            continue;
        }

        label = plugin->display_name != NULL ? plugin->display_name : plugin->name;
        entries = g_list_prepend (entries, menu_entry_new (label, command_id));
        command_id++;
    }

    if (entries == NULL)
        entries =
            g_list_prepend (entries, menu_entry_new (_ ("(no plugins loaded)"), CK_IgnoreKey));

    return g_list_reverse (entries);
}

/* --------------------------------------------------------------------------------------------- */

/* Collect cmd_menu_entries from all plugins that target @menu_name.
 * Returns a GList<menu_entry_t*> or NULL if no entries matched. */
static GList *
create_plugin_menu_entries (const char *menu_name)
{
    const GSList *plugins;
    GList *entries = NULL;
    gsize plugin_idx = 0;

    for (plugins = mc_editor_plugin_list (); plugins != NULL;
         plugins = g_slist_next (plugins), plugin_idx++)
    {
        const mc_editor_plugin_t *plugin = (const mc_editor_plugin_t *) plugins->data;
        int i;

        if (plugin->cmd_menu_entries == NULL || plugin->cmd_menu_entry_count <= 0)
            continue;

        if (plugin->name != NULL
            && mc_plugin_prefs_is_disabled (MC_PLUGIN_KIND_EDITOR, plugin->name))
            continue;

        for (i = 0; i < plugin->cmd_menu_entry_count; i++)
        {
            const mc_ep_cmd_menu_entry_t *e = &plugin->cmd_menu_entries[i];
            menu_entry_t *me;
            long cmd;

            if (g_strcmp0 (e->menu_name, menu_name) != 0)
                continue;

            if (e->label == NULL)
            {
                entries = g_list_prepend (entries, menu_separator_new ());
                continue;
            }

            /* Reject out-of-range action indices: the encoding multiplexes
               plugin_idx and action_index into a single command id, so an
               action_index that overflows MC_EDITOR_PLUGIN_ACTIONS_MAX
               would silently target a neighbouring plugin's action slot.
               Also require it to be a valid index into plugin->actions. */
            if (e->action_index < 0 || e->action_index >= MC_EDITOR_PLUGIN_ACTIONS_MAX
                || e->action_index >= plugin->action_count)
                continue;

            cmd = MC_EDITOR_PLUGIN_ACTION_BASE + (long) plugin_idx * MC_EDITOR_PLUGIN_ACTIONS_MAX
                + (long) e->action_index;

            me = menu_entry_new (_ (e->label), cmd);
            {
                char *dynamic = NULL;

                if (plugin->get_menu_shortcut != NULL)
                    dynamic = plugin->get_menu_shortcut (e->action_index);
                if (dynamic != NULL)
                {
                    menu_entry_set_shortcut (me, dynamic);
                    g_free (dynamic);
                }
                else if (e->shortcut != NULL)
                    menu_entry_set_shortcut (me, e->shortcut);
            }
            entries = g_list_prepend (entries, me);
        }
    }

    return entries != NULL ? g_list_reverse (entries) : NULL;
}

/* --------------------------------------------------------------------------------------------- */

static void
edit_drop_menu_cmd (WDialog *h, int which)
{
    WMenuBar *menubar;

    menubar = menubar_find (h);
    menubar_activate (menubar, drop_menus, which);
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
edit_init_menu (WMenuBar *menubar)
{
    GList *navigate_entries;
    int idx = 0;
    guint i;

    edit_runtime_menu_reload ();

    menubar_add_menu (
        menubar,
        menu_new (_ ("&File"), append_runtime_menu_entries (create_file_menu (), "File"),
                  "[Internal File Editor]"));
    menu_idx_file = idx++;

    menubar_add_menu (
        menubar,
        menu_new (_ ("&Edit"), append_runtime_menu_entries (create_edit_menu (), "Edit"),
                  "[Internal File Editor]"));
    menu_idx_edit = idx++;

    menubar_add_menu (menubar,
                      menu_new (_ ("&Search"),
                                append_runtime_menu_entries (create_search_replace_menu (),
                                                             "Search"),
                                "[Internal File Editor]"));
    menu_idx_search = idx++;

    menubar_add_menu (
        menubar,
        menu_new (_ ("&Command"), append_runtime_menu_entries (create_command_menu (), "Command"),
                  "[Internal File Editor]"));
    menu_idx_command = idx++;

    navigate_entries = create_plugin_menu_entries (MC_EP_MENU_NAVIGATE);
    navigate_entries = append_runtime_menu_entries (navigate_entries, "Navigate");
    if (navigate_entries != NULL)
    {
        menubar_add_menu (menubar,
                          menu_new (_ ("&Navigate"), navigate_entries, "[Internal File Editor]"));
        menu_idx_navigate = idx++;
    }
    else
        menu_idx_navigate = -1;

    for (i = 0; runtime_menu_actions != NULL && i < runtime_menu_actions->len; i++)
    {
        const edit_runtime_menu_action_t *action =
            (const edit_runtime_menu_action_t *) g_ptr_array_index (runtime_menu_actions, i);
        GList *entries;

        if (edit_runtime_menu_is_builtin (action->menu_path)
            || (i > 0
                && g_strcmp0 (((const edit_runtime_menu_action_t *) g_ptr_array_index (
                                  runtime_menu_actions, i - 1))
                                 ->menu_path,
                              action->menu_path)
                       == 0))
            continue;

        entries = create_runtime_menu_entries (action->menu_path);
        menubar_add_menu (
            menubar, menu_new (action->menu_path, entries, "[Internal File Editor]"));
        idx++;
    }

    menubar_add_menu (
        menubar,
        menu_new (_ ("&Window"), append_runtime_menu_entries (create_window_menu (), "Window"),
                  "[Internal File Editor]"));
    menu_idx_window = idx++;

    menubar_add_menu (
        menubar,
        menu_new (_ ("Pl&ugins"), append_runtime_menu_entries (create_plugins_menu (), "Plugins"),
                  "[Internal File Editor]"));
    menu_idx_plugins = idx++;

    menubar_add_menu (
        menubar,
        menu_new (_ ("&Options"), append_runtime_menu_entries (create_options_menu (), "Options"),
                  "[Internal File Editor]"));
    menu_idx_options = idx++;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_runtime_menu_action (long command)
{
    const edit_runtime_menu_action_t *action;
    const char *error = NULL;
    guint index;

    if (command < MC_EDITOR_RUNTIME_ACTION_BASE)
        return FALSE;

    index = (guint) (command - MC_EDITOR_RUNTIME_ACTION_BASE);
    if (runtime_menu_actions == NULL || index >= runtime_menu_actions->len)
        return TRUE;

    action = (const edit_runtime_menu_action_t *) g_ptr_array_index (runtime_menu_actions, index);
    if (!mc_runtime_plugins_invoke_action (action->runtime_name, "mcedit", action->id, &error))
        message (D_ERROR, _ ("Lua action"), "%s", error != NULL ? error : _ ("Action failed"));
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_runtime_invoke_action (const char *action_id)
{
    const char *error = NULL;

    if (mc_runtime_plugins_invoke_action ("lua", "mcedit", action_id, &error))
        return TRUE;
    message (D_ERROR, _ ("Lua action"), "%s", error != NULL ? error : _ ("Action failed"));
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */

void
edit_menu_cmd (WDialog *h)
{
    edit_drop_menu_cmd (h, -1);
}

/* --------------------------------------------------------------------------------------------- */

gboolean
edit_drop_hotkey_menu (WDialog *h, int key)
{
    int m = -1;

    switch (key)
    {
    case ALT ('f'):
        m = menu_idx_file;
        break;
    case ALT ('e'):
        m = menu_idx_edit;
        break;
    case ALT ('s'):
        m = menu_idx_search;
        break;
    case ALT ('c'):
        m = menu_idx_command;
        break;
    case ALT ('n'):
        m = menu_idx_navigate;
        break;
    case ALT ('w'):
        m = menu_idx_window;
        break;
    case ALT ('p'):
        m = menu_idx_plugins;
        break;
    case ALT ('o'):
        m = menu_idx_options;
        break;
    default:
        return FALSE;
    }

    if (m < 0)
        return FALSE;

    edit_drop_menu_cmd (h, m);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
