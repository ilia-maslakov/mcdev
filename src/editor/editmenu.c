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
#include "lib/plugin-prefs.h"
#include "lib/tty/key.h"  // ALT
#include "lib/widget.h"

#include "src/setup.h"  // drop_menus

#include "edit-impl.h"
#include "editwidget.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

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
static int menu_idx_format = 4;
static int menu_idx_window = 5;
static int menu_idx_plugins = 6;
static int menu_idx_options = 7;

/* --------------------------------------------------------------------------------------------- */
/*** file scope functions ************************************************************************/
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

static GList *
create_format_menu (void)
{
    GList *entries = NULL;

    entries = g_list_prepend (entries, menu_entry_new (_ ("Insert &literal..."), CK_InsertLiteral));
    entries = g_list_prepend (entries, menu_entry_new (_ ("Insert &date/time"), CK_Date));
    entries = g_list_prepend (entries, menu_separator_new ());
    entries =
        g_list_prepend (entries, menu_entry_new (_ ("&Format paragraph"), CK_ParagraphFormat));
    entries = g_list_prepend (entries, menu_entry_new (_ ("&Sort..."), CK_Sort));
    entries =
        g_list_prepend (entries, menu_entry_new (_ ("&Paste output of..."), CK_ExternalCommand));
    entries =
        g_list_prepend (entries, menu_entry_new (_ ("&External formatter"), CK_PipeBlock (0)));

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

    menubar_add_menu (menubar,
                      menu_new (_ ("&File"), create_file_menu (), "[Internal File Editor]"));
    menu_idx_file = idx++;

    menubar_add_menu (menubar,
                      menu_new (_ ("&Edit"), create_edit_menu (), "[Internal File Editor]"));
    menu_idx_edit = idx++;

    menubar_add_menu (
        menubar, menu_new (_ ("&Search"), create_search_replace_menu (), "[Internal File Editor]"));
    menu_idx_search = idx++;

    menubar_add_menu (menubar,
                      menu_new (_ ("&Command"), create_command_menu (), "[Internal File Editor]"));
    menu_idx_command = idx++;

    navigate_entries = create_plugin_menu_entries (MC_EP_MENU_NAVIGATE);
    if (navigate_entries != NULL)
    {
        menubar_add_menu (menubar,
                          menu_new (_ ("&Navigate"), navigate_entries, "[Internal File Editor]"));
        menu_idx_navigate = idx++;
    }
    else
        menu_idx_navigate = -1;

    menubar_add_menu (menubar,
                      menu_new (_ ("For&mat"), create_format_menu (), "[Internal File Editor]"));
    menu_idx_format = idx++;

    menubar_add_menu (menubar,
                      menu_new (_ ("&Window"), create_window_menu (), "[Internal File Editor]"));
    menu_idx_window = idx++;

    menubar_add_menu (menubar,
                      menu_new (_ ("Pl&ugins"), create_plugins_menu (), "[Internal File Editor]"));
    menu_idx_plugins = idx++;

    menubar_add_menu (menubar,
                      menu_new (_ ("&Options"), create_options_menu (), "[Internal File Editor]"));
    menu_idx_options = idx++;
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
    case ALT ('m'):
        m = menu_idx_format;
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
