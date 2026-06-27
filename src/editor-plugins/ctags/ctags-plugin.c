/*
   Ctags editor plugin.

   Copyright (C) 2025-2026
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

#include <config.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "src/editor-plugins/ctags/ctags-history.h"
#include "lib/tty/key.h"
#include "lib/strutil.h"
#include "lib/util.h"
#include "lib/widget.h"
#include "lib/editor-plugin.h"

#include "src/editor/editwidget.h"
#include "src/editor/edit-impl.h"
#include "src/editor/editcomplete.h" /* edit_complete_word_cmd for Complete fallback */

#include "ctags-parser.h"
#include "ctags-index.h"
#include "ctags-repository.h"
#include "ctags-config.h"
#include "ctags-fuzzy.h"
#include "ctags-ui.h"
#include "ctags-plugin.h"

/*** file scope macro definitions ****************************************************************/

/* Plugin-local command IDs -- not registered in lib/keybind.h */
#define CTAGS_CMD_NONE           0L
#define CTAGS_CMD_MENU           1L
#define CTAGS_CMD_GOTO           2L
#define CTAGS_CMD_BACK           3L
#define CTAGS_CMD_FORWARD        4L
#define CTAGS_CMD_HISTORY        5L
#define CTAGS_CMD_SEARCH_SYMBOL  6L
#define CTAGS_CMD_SEARCH_FILE    7L
#define CTAGS_CMD_SEARCH_CURRENT 8L
#define CTAGS_CMD_COMPLETE       9L
#define CTAGS_CMD_MEMBERS        10L
#define CTAGS_CMD_LOAD_TAGS      11L
#define CTAGS_CMD_MANAGE_REPOS   12L
#define CTAGS_CMD_REINDEX        13L
#define CTAGS_CMD_REINDEX_FILE   14L
#define CTAGS_CMD_CONFIG         15L
#define CTAGS_CMD_FIND_SYMBOL    16L
#define CTAGS_CMD_GREP_REFS      17L

#define CTAGS_KEYMAP_FILE        "ctags.keymap"

/*** file scope type declarations ****************************************************************/

typedef struct
{
    long command;
    int key;
} ctags_keybind_t;

typedef struct
{
    mc_editor_host_t *host;
    GSList *repos; /* GSList<ctags_repo_t*> owned */
    ctags_config_t cfg;
    GArray *keymap;
} ctags_data_t;

/*** file scope variables ************************************************************************/

/*** file scope functions ************************************************************************/

/* --------------------------------------------------------------------------------------------- */
/* Keymap loading */
/* --------------------------------------------------------------------------------------------- */

static long
ctags_keymap_action_from_name (const char *name)
{
    if (g_strcmp0 (name, "CtagsMenu") == 0)
        return CTAGS_CMD_MENU;
    if (g_strcmp0 (name, "CtagsGoto") == 0)
        return CTAGS_CMD_GOTO;
    if (g_strcmp0 (name, "CtagsBack") == 0)
        return CTAGS_CMD_BACK;
    if (g_strcmp0 (name, "CtagsForward") == 0)
        return CTAGS_CMD_FORWARD;
    if (g_strcmp0 (name, "CtagsHistory") == 0)
        return CTAGS_CMD_HISTORY;
    if (g_strcmp0 (name, "CtagsSearchSymbol") == 0)
        return CTAGS_CMD_SEARCH_SYMBOL;
    if (g_strcmp0 (name, "CtagsSearchFile") == 0)
        return CTAGS_CMD_SEARCH_FILE;
    if (g_strcmp0 (name, "CtagsSearchCurrent") == 0)
        return CTAGS_CMD_SEARCH_CURRENT;
    if (g_strcmp0 (name, "CtagsComplete") == 0)
        return CTAGS_CMD_COMPLETE;
    if (g_strcmp0 (name, "CtagsMembers") == 0)
        return CTAGS_CMD_MEMBERS;
    if (g_strcmp0 (name, "CtagsLoadTags") == 0)
        return CTAGS_CMD_LOAD_TAGS;
    if (g_strcmp0 (name, "CtagsManageRepos") == 0)
        return CTAGS_CMD_MANAGE_REPOS;
    if (g_strcmp0 (name, "CtagsReindex") == 0)
        return CTAGS_CMD_REINDEX;
    if (g_strcmp0 (name, "CtagsReindexFile") == 0)
        return CTAGS_CMD_REINDEX_FILE;
    if (g_strcmp0 (name, "CtagsConfig") == 0)
        return CTAGS_CMD_CONFIG;
    if (g_strcmp0 (name, "CtagsFindSymbol") == 0)
        return CTAGS_CMD_FIND_SYMBOL;
    if (g_strcmp0 (name, "CtagsGrepRefs") == 0)
        return CTAGS_CMD_GREP_REFS;
    return CTAGS_CMD_NONE;
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_keymap_add_binding (GArray *keymap, long command, const char *keybind)
{
    ctags_keybind_t bind;
    char *caption = NULL;

    if (keymap == NULL || command == CTAGS_CMD_NONE || keybind == NULL || *keybind == '\0')
        return;

    bind.key = tty_keyname_to_keycode (keybind, &caption);
    g_free (caption);

    if (bind.key == 0)
        return;

    bind.command = command;
    g_array_append_val (keymap, bind);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_keymap_parse_line (GArray *keymap, char *line)
{
    char *eq, *name, *value;
    long action;
    gchar **values, **it;

    if (keymap == NULL || line == NULL)
        return;

    line = g_strstrip (line);
    if (*line == '\0' || *line == '#' || *line == ';' || *line == '[')
        return;

    eq = strchr (line, '=');
    if (eq == NULL)
        return;

    *eq = '\0';
    name = g_strstrip (line);
    value = g_strstrip (eq + 1);

    action = ctags_keymap_action_from_name (name);
    if (action == CTAGS_CMD_NONE)
        return;

    /* Later definitions override earlier ones: drop any existing bindings for this action.
     * This makes ~/.config/mc/ctags.keymap (loaded last) override the system file, and
     * "Action =" with an empty value unbind the action. */
    {
        guint i = 0;

        while (i < keymap->len)
        {
            const ctags_keybind_t *b = &g_array_index (keymap, ctags_keybind_t, i);

            if (b->command == action)
                g_array_remove_index (keymap, i);
            else
                i++;
        }
    }

    if (*value == '\0')
        return;

    values = g_strsplit (value, ";", -1);
    for (it = values; it != NULL && *it != NULL; it++)
    {
        char *keybind = g_strstrip (*it);

        if (*keybind != '\0')
            ctags_keymap_add_binding (keymap, action, keybind);
    }
    g_strfreev (values);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_keymap_load_file (GArray *keymap, const char *fname)
{
    char *contents = NULL;
    gsize len = 0;
    gchar **lines, **it;

    if (keymap == NULL || fname == NULL)
        return;

    if (!exist_file (fname))
        return;

    if (!g_file_get_contents (fname, &contents, &len, NULL))
        return;

    lines = g_strsplit (contents, "\n", -1);
    for (it = lines; it != NULL && *it != NULL; it++)
        ctags_keymap_parse_line (keymap, *it);
    g_strfreev (lines);
    g_free (contents);
}

/* --------------------------------------------------------------------------------------------- */

static GArray *
ctags_keymap_load (void)
{
    GArray *keymap;
    char *fname;

    keymap = g_array_new (FALSE, FALSE, sizeof (ctags_keybind_t));

    if (mc_global.share_data_dir != NULL)
    {
        fname = g_build_filename (mc_global.share_data_dir, CTAGS_KEYMAP_FILE, (char *) NULL);
        ctags_keymap_load_file (keymap, fname);
        g_free (fname);
    }

    if (mc_global.sysconfig_dir != NULL)
    {
        fname = g_build_filename (mc_global.sysconfig_dir, CTAGS_KEYMAP_FILE, (char *) NULL);
        ctags_keymap_load_file (keymap, fname);
        g_free (fname);
    }

    /* User override: ~/.config/mc/ctags.keymap (mc_config_get_full_path doesn't
     * work for unregistered filenames, so build the path from the config dir). */
    if (mc_config_get_path () != NULL)
    {
        fname = g_build_filename (mc_config_get_path (), CTAGS_KEYMAP_FILE, (char *) NULL);
        ctags_keymap_load_file (keymap, fname);
        g_free (fname);
    }

    return keymap;
}

/* --------------------------------------------------------------------------------------------- */

static long
ctags_keymap_lookup_command (const GArray *keymap, int key)
{
    guint i;

    if (keymap == NULL)
        return CTAGS_CMD_NONE;

    for (i = 0; i < keymap->len; i++)
    {
        const ctags_keybind_t *bind;

        bind = &g_array_index (keymap, ctags_keybind_t, i);
        if (bind->key == key)
            return bind->command;
    }

    return CTAGS_CMD_NONE;
}

/* --------------------------------------------------------------------------------------------- */

/* Find the repo that owns @file_path; fall back to first repo. */
static ctags_repo_t *
ctags_find_repo_for_path (ctags_data_t *d, const char *file_path)
{
    ctags_repo_t *fallback = NULL;
    GSList *l;

    for (l = d->repos; l != NULL; l = g_slist_next (l))
    {
        ctags_repo_t *repo = (ctags_repo_t *) l->data;

        if (fallback == NULL)
            fallback = repo;

        if (ctags_path_is_under (file_path, repo->root_dir))
            return repo;
    }
    return fallback;
}

/* --------------------------------------------------------------------------------------------- */

/* Jump to @path:@line and record the location in the repo's history. */
static void
ctags_do_jump (ctags_data_t *d, WEdit *edit, const char *path, long line, const char *name)
{
    /* Record where we are before jumping so the user can return from history. */
    if (d->host->get_current_file != NULL && d->host->get_cursor_line != NULL)
    {
        char *cur_file = d->host->get_current_file (d->host, edit);
        long cur_line = d->host->get_cursor_line (d->host, edit);

        if (cur_file != NULL && cur_line > 0)
        {
            const char *base = strrchr (cur_file, G_DIR_SEPARATOR);
            base = (base != NULL) ? base + 1 : cur_file;
            ctags_repo_history_push (ctags_find_repo_for_path (d, cur_file), base, cur_file,
                                     cur_line);
        }
        g_free (cur_file);
    }

    if (d->host->jump_to != NULL && !d->host->jump_to (d->host, edit, path, line))
    {
        /* The host refused (e.g. its own navigation stack is full).
           Don't pretend the jump happened: skip the history push and
           surface the error so the user knows nothing moved. */
        if (d->host->message != NULL)
        {
            char *text = g_strdup_printf (_ ("Cannot jump to %s:%ld."), path, line);
            d->host->message (d->host, D_ERROR, _ ("Ctags"), text);
            g_free (text);
        }
        return;
    }

    ctags_repo_history_push (ctags_find_repo_for_path (d, path), name, path, line);
}

/* --------------------------------------------------------------------------------------------- */

static const char *
ctags_first_root_dir (ctags_data_t *d)
{
    GSList *l;

    for (l = d->repos; l != NULL; l = g_slist_next (l))
    {
        ctags_repo_t *repo = (ctags_repo_t *) l->data;
        if (repo->root_dir != NULL)
            return repo->root_dir;
    }
    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

/* Return a newly-allocated absolute path for @e by searching repos for pointer equality.
   If the owner repo cannot be identified, use the first loaded repo root.
   Caller must g_free the result. */
static char *
ctags_resolve_entry_path (ctags_data_t *d, const ctags_entry_t *e)
{
    GSList *l;
    const char *root = NULL;

    if (e == NULL || e->file == NULL)
        return NULL;

    if (g_path_is_absolute (e->file))
        return g_strdup (e->file);

    for (l = d->repos; l != NULL; l = g_slist_next (l))
    {
        ctags_repo_t *repo = (ctags_repo_t *) l->data;
        const GPtrArray *bucket;
        guint i;

        if (repo->index == NULL || repo->root_dir == NULL)
            continue;

        bucket = ctags_index_find_exact (repo->index, e->name);
        if (bucket == NULL)
            bucket = ctags_index_find_exact_ci (repo->index, e->name);

        if (bucket == NULL)
            continue;

        for (i = 0; i < bucket->len; i++)
        {
            if (g_ptr_array_index (bucket, i) == e)
            {
                root = repo->root_dir;
                break;
            }
        }

        if (root != NULL)
            break;
    }

    /* Fallback: first repo with a root dir */
    if (root == NULL)
    {
        for (l = d->repos; l != NULL; l = g_slist_next (l))
        {
            ctags_repo_t *repo = (ctags_repo_t *) l->data;
            if (repo->root_dir != NULL)
            {
                root = repo->root_dir;
                break;
            }
        }
    }

    return root != NULL ? g_build_filename (root, e->file, NULL) : g_strdup (e->file);
}

/* --------------------------------------------------------------------------------------------- */
/* Repository helpers */
/* --------------------------------------------------------------------------------------------- */

/* Ensure at least one repo is loaded for @file_path.
 * If auto_discover is on, tries to find and load a tags file. */
static void
ctags_ensure_repo (ctags_data_t *d, const char *file_path)
{
    char *tags;
    ctags_repo_t *repo;

    if (!d->cfg.auto_discover)
        return;

    /* Already have a repo owning this file? */
    if (file_path != NULL && ctags_repos_find_for_file (d->repos, file_path) != NULL)
        return;

    /* Search parent directories */
    tags = ctags_repo_discover (file_path != NULL ? file_path : ".");
    if (tags == NULL)
        return;

    /* Check if this tags file is already loaded */
    {
        GSList *l;

        for (l = d->repos; l != NULL; l = g_slist_next (l))
        {
            ctags_repo_t *r = (ctags_repo_t *) l->data;

            if (r->tags_path != NULL && strcmp (r->tags_path, tags) == 0)
            {
                g_free (tags);
                return;
            }
        }
    }

    repo = ctags_repo_load (tags);
    g_free (tags);

    if (repo != NULL)
        d->repos = g_slist_prepend (d->repos, repo);
}

/* --------------------------------------------------------------------------------------------- */
/* Navigation actions */
/* --------------------------------------------------------------------------------------------- */

static void
ctags_goto_declaration (ctags_data_t *d, WEdit *edit)
{
    char *word;
    GPtrArray *found;
    ctags_entry_t *chosen = NULL;

    word = d->host->get_cursor_word != NULL ? d->host->get_cursor_word (d->host, edit) : NULL;

    if (word == NULL || *word == '\0')
    {
        g_free (word);
        word = ctags_ui_prompt_symbol (NULL);
        if (word == NULL)
            return;
    }

    {
        char *file =
            d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;

        ctags_ensure_repo (d, file);
        g_free (file);
    }

    found = ctags_repos_find_exact (d->repos, word);

    if (found == NULL)
    {
        message (D_NORMAL, _ ("Ctags"), _ ("Symbol '%s' not found in any loaded repository."),
                 word);
        g_free (word);
        return;
    }

    if (found->len == 1)
        chosen = (ctags_entry_t *) g_ptr_array_index (found, 0);
    else
    {
        ctags_ui_set_root_dir (ctags_first_root_dir (d));
        chosen = ctags_ui_select (_ ("Go to Declaration"), found, word);
    }

    if (chosen != NULL && chosen->file != NULL)
    {
        char *path = ctags_resolve_entry_path (d, chosen);

        ctags_do_jump (d, edit, path, ctags_entry_resolve_line (chosen, path), chosen->name);
        g_free (path);
    }

    g_ptr_array_free (found, FALSE);
    g_free (word);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

/* Always shows the selector with all matching entries -- never auto-jumps on a single result. */
static void
ctags_find_symbol (ctags_data_t *d, WEdit *edit)
{
    char *word;
    GPtrArray *found;
    ctags_entry_t *chosen;

    word = d->host->get_cursor_word != NULL ? d->host->get_cursor_word (d->host, edit) : NULL;

    if (word == NULL || *word == '\0')
    {
        g_free (word);
        word = ctags_ui_prompt_symbol (NULL);
        if (word == NULL)
            return;
    }

    {
        char *file =
            d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;

        ctags_ensure_repo (d, file);
        g_free (file);
    }

    found = ctags_repos_find_exact (d->repos, word);

    if (found == NULL)
        found = ctags_repos_find_prefix (d->repos, word, !d->cfg.case_sensitive);

    /* ctags_ui_select shows a "not found" message when found is NULL */
    ctags_ui_set_root_dir (ctags_first_root_dir (d));
    chosen = ctags_ui_select (_ ("Find Declaration"), found, word);

    if (chosen != NULL && chosen->file != NULL)
    {
        char *path = ctags_resolve_entry_path (d, chosen);

        ctags_do_jump (d, edit, path, ctags_entry_resolve_line (chosen, path), chosen->name);
        g_free (path);
    }

    if (found != NULL)
        g_ptr_array_free (found, FALSE);
    g_free (word);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_grep_refs (ctags_data_t *d, WEdit *edit)
{
    char *word;
    GPtrArray *in_index;
    char *cmd;
    char *file_list_path = NULL;
    int file_list_fd;
    FILE *file_list_fp;
    FILE *fp;
    char buf[4096];
    GPtrArray *results;
    ctags_entry_t *chosen = NULL;
    char *chosen_file = NULL;
    long chosen_line = 0;
    GSList *l;
    gboolean have_files = FALSE;

    word = d->host->get_cursor_word != NULL ? d->host->get_cursor_word (d->host, edit) : NULL;

    if (word == NULL || *word == '\0')
    {
        g_free (word);
        message (D_NORMAL, _ ("Ctags"), _ ("No word at cursor."));
        return;
    }

    {
        char *file =
            d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;
        ctags_ensure_repo (d, file);
        g_free (file);
    }

    /* Validate: word must exist in the ctags index */
    in_index = ctags_repos_find_exact (d->repos, word);
    if (in_index == NULL)
    {
        message (D_NORMAL, _ ("Ctags"), _ ("'%s' is not a known symbol."), word);
        g_free (word);
        return;
    }
    g_ptr_array_free (in_index, FALSE);

    /* Build the grep target list in a temp file, one path per line.  This sidesteps
     * the ARG_MAX limit hit by very large indexes when all paths were placed on the
     * command line, and avoids per-path shell quoting (xargs -d '\n' treats each
     * line as a literal argument). */
    file_list_fd = g_file_open_tmp ("ctags-refs-XXXXXX", &file_list_path, NULL);
    if (file_list_fd < 0)
    {
        message (D_ERROR, _ ("Ctags"), _ ("Failed to create temporary file."));
        g_free (word);
        return;
    }
    file_list_fp = fdopen (file_list_fd, "w");
    if (file_list_fp == NULL)
    {
        close (file_list_fd);
        unlink (file_list_path);
        g_free (file_list_path);
        message (D_ERROR, _ ("Ctags"), _ ("Failed to open temporary file."));
        g_free (word);
        return;
    }

    for (l = d->repos; l != NULL; l = g_slist_next (l))
    {
        ctags_repo_t *repo = (ctags_repo_t *) l->data;
        GList *files, *fl;

        if (repo->index == NULL)
            continue;

        files = ctags_index_get_files (repo->index);

        for (fl = files; fl != NULL; fl = g_list_next (fl))
        {
            /* NUL-separated so we can feed it to POSIX-portable `xargs -0`. */
            fputs ((const char *) fl->data, file_list_fp);
            fputc ('\0', file_list_fp);
            have_files = TRUE;
        }

        g_list_free (files);
    }
    fclose (file_list_fp);

    if (!have_files)
    {
        unlink (file_list_path);
        g_free (file_list_path);
        message (D_NORMAL, _ ("Ctags"), _ ("No indexed files found."));
        g_free (word);
        return;
    }

    /* Tag names can contain shell-meta characters (C++ operators, dots, etc.); quote
     * the pattern even though the ctags index only ever holds parsed names. */
    {
        char *q_word = g_shell_quote (word);
        char *q_list = g_shell_quote (file_list_path);

        /* xargs -0 is supported by GNU, BSD, busybox; -d/-a are GNU-only. */
        cmd = g_strdup_printf ("xargs -0 grep -n -w -- %s 2>/dev/null < %s", q_word, q_list);
        g_free (q_word);
        g_free (q_list);
    }

    fp = popen (cmd, "r");
    g_free (cmd);

    if (fp == NULL)
    {
        unlink (file_list_path);
        g_free (file_list_path);
        message (D_ERROR, _ ("Ctags"), _ ("Failed to run grep."));
        g_free (word);
        return;
    }

    results = g_ptr_array_new_with_free_func (ctags_entry_free_ptr);

    while (fgets (buf, (int) sizeof (buf), fp) != NULL)
    {
        char *colon1, *colon2;
        long line_num;
        ctags_entry_t *e;

        g_strchomp (buf);

        colon1 = strchr (buf, ':');
        if (colon1 == NULL)
            continue;
        *colon1 = '\0';

        colon2 = strchr (colon1 + 1, ':');
        if (colon2 == NULL)
            continue;
        *colon2 = '\0';

        line_num = strtol (colon1 + 1, NULL, 10);
        if (line_num <= 0)
            continue;

        e = g_new0 (ctags_entry_t, 1);
        e->name = g_strdup (g_strstrip (colon2 + 1));
        e->file = g_strdup (buf);
        e->line = line_num;

        g_ptr_array_add (results, e);
    }

    pclose (fp);
    unlink (file_list_path);
    g_free (file_list_path);
    file_list_path = NULL;

    if (results->len == 0)
    {
        g_ptr_array_free (results, TRUE);
        message (D_NORMAL, _ ("Ctags"), _ ("No references found for '%s'."), word);
        g_free (word);
        return;
    }

    chosen = ctags_ui_select_refs (_ ("Find References"), results);

    /* Extract what we need before freeing the array */
    if (chosen != NULL && chosen->file != NULL)
    {
        chosen_file = g_strdup (chosen->file);
        chosen_line = chosen->line;
    }

    g_ptr_array_free (results, TRUE);
    g_free (word);

    if (chosen_file != NULL)
        ctags_do_jump (d, edit, chosen_file, chosen_line, NULL);

    g_free (chosen_file);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_search_symbol (ctags_data_t *d, WEdit *edit)
{
    char *word;
    GPtrArray *all;
    ctags_entry_t *chosen;
    GSList *l;

    {
        char *file =
            d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;
        ctags_ensure_repo (d, file);
        g_free (file);
    }

    if (d->repos == NULL)
    {
        message (D_NORMAL, _ ("Ctags"), _ ("No repository loaded."));
        return;
    }

    word = d->host->get_cursor_word != NULL ? d->host->get_cursor_word (d->host, edit) : NULL;

    /* Collect all entries from all repos */
    all = g_ptr_array_new ();
    for (l = d->repos; l != NULL; l = g_slist_next (l))
    {
        ctags_repo_t *repo = (ctags_repo_t *) l->data;
        guint i;

        if (repo->entries == NULL)
            continue;
        for (i = 0; i < repo->entries->len; i++)
            g_ptr_array_add (all, g_ptr_array_index (repo->entries, i));
    }

    /* Show selector directly; word under cursor pre-fills the filter */
    ctags_ui_set_root_dir (ctags_first_root_dir (d));
    chosen = ctags_ui_select (_ ("Search Symbol"), all, word);

    if (chosen != NULL && chosen->file != NULL)
    {
        char *path = ctags_resolve_entry_path (d, chosen);

        ctags_do_jump (d, edit, path, ctags_entry_resolve_line (chosen, path), chosen->name);
        g_free (path);
    }

    g_ptr_array_free (all, FALSE);
    g_free (word);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_search_current_file (ctags_data_t *d, WEdit *edit)
{
    char *file_path;
    GPtrArray *found = NULL;
    ctags_entry_t *chosen;
    GSList *l;

    file_path =
        d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;

    if (file_path == NULL)
    {
        message (D_NORMAL, _ ("Ctags"), _ ("No file is open."));
        return;
    }

    ctags_ensure_repo (d, file_path);

    for (l = d->repos; l != NULL; l = g_slist_next (l))
    {
        ctags_repo_t *repo = (ctags_repo_t *) l->data;
        const GPtrArray *bucket;

        if (repo->index == NULL)
            continue;

        bucket = ctags_index_find_file (repo->index, file_path);
        if (bucket == NULL)
        {
            /* Try with relative path */
            const char *rel = file_path;

            if (ctags_path_is_under (file_path, repo->root_dir))
                rel = file_path + strlen (repo->root_dir) + 1;

            bucket = ctags_index_find_file (repo->index, rel);
        }

        if (bucket != NULL)
        {
            guint i;

            if (found == NULL)
                found = g_ptr_array_new ();

            for (i = 0; i < bucket->len; i++)
                g_ptr_array_add (found, g_ptr_array_index (bucket, i));
        }
    }

    g_free (file_path);

    if (found == NULL)
    {
        message (D_NORMAL, _ ("Ctags"), _ ("No tags found for the current file."));
        return;
    }

    ctags_ui_set_root_dir (ctags_first_root_dir (d));
    chosen = ctags_ui_select (_ ("Symbols in Current File"), found, NULL);

    if (chosen != NULL && chosen->file != NULL)
    {
        char *path = ctags_resolve_entry_path (d, chosen);

        ctags_do_jump (d, edit, path, ctags_entry_resolve_line (chosen, path), chosen->name);
        g_free (path);
    }

    g_ptr_array_free (found, FALSE);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_search_file (ctags_data_t *d, WEdit *edit)
{
    char *query;
    GPtrArray *found;
    ctags_entry_t *chosen;

    query = ctags_ui_prompt_file (NULL);
    if (query == NULL || *query == '\0')
    {
        g_free (query);
        return;
    }

    {
        char *file =
            d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;
        ctags_ensure_repo (d, file);
        g_free (file);
    }

    found = ctags_repos_find_basename (d->repos, query);

    if (found == NULL)
    {
        message (D_NORMAL, _ ("Ctags"), _ ("No files matching '%s'."), query);
        g_free (query);
        return;
    }

    ctags_ui_set_root_dir (ctags_first_root_dir (d));
    chosen = ctags_ui_select (_ ("Search File"), found, query);

    if (chosen != NULL && chosen->file != NULL)
    {
        char *path = ctags_resolve_entry_path (d, chosen);

        ctags_do_jump (d, edit, path, 1, chosen->name);
        g_free (path);
    }

    g_ptr_array_free (found, FALSE);
    g_free (query);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_complete_symbol (ctags_data_t *d, WEdit *edit)
{
    char *word;
    GPtrArray *found;
    gsize word_len;

    word = d->host->get_cursor_word != NULL ? d->host->get_cursor_word (d->host, edit) : NULL;
    if (word == NULL || *word == '\0')
    {
        /* Fall back to mcedit's built-in word completion if there's nothing to do here. */
        g_free (word);
        edit_complete_word_cmd (edit);
        return;
    }
    word_len = strlen (word);

    {
        char *file =
            d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;
        ctags_ensure_repo (d, file);
        g_free (file);
    }

    found = ctags_repos_find_prefix (d->repos, word, !d->cfg.case_sensitive);

    if (found == NULL)
    {
        /* No tag matches the prefix -- defer to mcedit's word completion. */
        g_free (word);
        edit_complete_word_cmd (edit);
        return;
    }

    /* Deduplicate by name */
    {
        GHashTable *seen = g_hash_table_new (g_str_hash, g_str_equal);
        GPtrArray *deduped = g_ptr_array_new ();
        guint i;

        for (i = 0; i < found->len; i++)
        {
            ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (found, i);

            if (!g_hash_table_contains (seen, e->name))
            {
                g_hash_table_add (seen, e->name);
                g_ptr_array_add (deduped, e);
            }
        }

        g_ptr_array_free (found, FALSE);
        g_hash_table_destroy (seen);
        found = deduped;
    }

    /* Replace the prefix the user typed (which may differ in case from the tag)
     * with the full tag name.  Without this, "s_is" + match "S_ISBLK" produces
     * "s_isBLK" instead of "S_ISBLK". */
    if (found->len == 1)
    {
        ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (found, 0);

        if (d->host->insert_text != NULL && e->name != NULL && *e->name != '\0')
            d->host->insert_text (d->host, edit, e->name, word_len);
    }
    else
    {
        ctags_entry_t *chosen;

        ctags_ui_set_root_dir (ctags_first_root_dir (d));
        chosen = ctags_ui_select (_ ("Complete Symbol"), found, word);
        if (chosen != NULL && d->host->insert_text != NULL && chosen->name != NULL
            && *chosen->name != '\0')
            d->host->insert_text (d->host, edit, chosen->name, word_len);
    }

    g_ptr_array_free (found, FALSE);
    g_free (word);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

/* Extract a trailing // comment from a ctags excmd regex like /^... \/\/ text$/
 * Returns a newly-allocated string, or NULL if none found. */
static char *
ctags_extract_comment (const char *excmd)
{
    const char *p, *start, *end;
    GString *result;

    if (excmd == NULL)
        return NULL;

    /* Find \/\/ (escaped // inside the regex) */
    for (p = excmd; *p != '\0'; p++)
        if (p[0] == '\\' && p[1] == '/' && p[2] == '\\' && p[3] == '/')
            break;

    if (*p == '\0')
        return NULL;

    start = p + 4;
    end = start + strlen (start);

    /* Trim trailing $/ or bare $ */
    if (end >= start + 2 && *(end - 1) == '/' && *(end - 2) == '$')
        end -= 2;
    else if (end >= start + 1 && *(end - 1) == '$')
        end -= 1;

    if (end <= start)
        return NULL;

    /* Copy and unescape \/ -> / */
    result = g_string_new_len (start, (gssize) (end - start));
    {
        gsize j;
        for (j = 0; j + 1 < result->len; j++)
        {
            if (result->str[j] == '\\' && result->str[j + 1] == '/')
            {
                g_string_erase (result, (gssize) j, 1);
                /* don't advance j -- '/' is now at j */
            }
        }
    }

    g_strstrip (result->str);
    if (result->len == 0)
    {
        g_string_free (result, TRUE);
        return NULL;
    }

    return g_string_free (result, FALSE);
}

/* --------------------------------------------------------------------------------------------- */

/* Build display rows from a flat array of member entries.
 * Fields whose typeref is an anonymous struct/union are expanded inline with a group header.
 * Returns GPtrArray<ctags_member_row_t*> with free func; caller must g_ptr_array_free(..., TRUE).
 */
static GPtrArray *
ctags_build_member_rows (ctags_data_t *d, GPtrArray *entries)
{
    GPtrArray *rows;
    guint i;

    rows = g_ptr_array_new_with_free_func ((GDestroyNotify) ctags_member_row_free);

    for (i = 0; i < entries->len; i++)
    {
        ctags_entry_t *e = (ctags_entry_t *) g_ptr_array_index (entries, i);
        const char *tv = NULL;
        gboolean is_anon;

        /* Resolve the typeref value part (after the first ':') */
        if (e->typeref != NULL)
        {
            const char *colon = strchr (e->typeref, ':');
            tv = colon != NULL ? colon + 1 : e->typeref;
        }
        is_anon = (tv != NULL && strstr (tv, "__anon") != NULL);

        if (is_anon)
        {
            GPtrArray *children;
            GSList *l;
            ctags_member_row_t *hdr;
            guint j;

            /* Collect children of the anonymous scope from all repos */
            children = g_ptr_array_new ();
            for (l = d->repos; l != NULL; l = g_slist_next (l))
            {
                ctags_repo_t *repo = (ctags_repo_t *) l->data;
                const GPtrArray *kids;

                if (repo->index == NULL)
                    continue;
                kids = ctags_index_find_scope (repo->index, tv);
                if (kids != NULL)
                    for (j = 0; j < kids->len; j++)
                        g_ptr_array_add (children, g_ptr_array_index (kids, j));
            }

            /* Group header row */
            hdr = g_new0 (ctags_member_row_t, 1);
            hdr->original = e;
            hdr->display_name = g_strdup (e->name != NULL ? e->name : "");
            hdr->note = g_strdup_printf ("%u fields", children->len);
            hdr->is_group = TRUE;
            g_ptr_array_add (rows, hdr);

            /* Child rows (indented as "parent.child") */
            for (j = 0; j < children->len; j++)
            {
                ctags_entry_t *child = (ctags_entry_t *) g_ptr_array_index (children, j);
                ctags_member_row_t *row = g_new0 (ctags_member_row_t, 1);

                row->original = child;
                row->display_name = g_strdup_printf ("  %s.%s", e->name != NULL ? e->name : "",
                                                     child->name != NULL ? child->name : "");
                row->note = ctags_extract_comment (child->excmd);
                row->is_group = FALSE;
                g_ptr_array_add (rows, row);
            }

            g_ptr_array_free (children, FALSE);
        }
        else
        {
            ctags_member_row_t *row = g_new0 (ctags_member_row_t, 1);

            row->original = e;
            row->display_name = g_strdup (e->name != NULL ? e->name : "");
            row->note = ctags_extract_comment (e->excmd);
            row->is_group = FALSE;
            g_ptr_array_add (rows, row);
        }
    }

    return rows;
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_show_members (ctags_data_t *d, WEdit *edit)
{
    char *scope;
    const GPtrArray *members;
    GPtrArray *copy;
    ctags_entry_t *chosen;
    char *cur_word;
    GSList *l;

    {
        char *file =
            d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;
        ctags_ensure_repo (d, file);
        g_free (file);
    }

    cur_word = d->host->get_cursor_word != NULL ? d->host->get_cursor_word (d->host, edit) : NULL;

    /* If the word under cursor yields members, skip the input dialog. */
    copy = g_ptr_array_new ();
    if (cur_word != NULL)
    {
        for (l = d->repos; l != NULL; l = g_slist_next (l))
        {
            ctags_repo_t *repo = (ctags_repo_t *) l->data;
            guint i;

            if (repo->index == NULL)
                continue;

            members = ctags_index_find_scope (repo->index, cur_word);
            if (members != NULL)
                for (i = 0; i < members->len; i++)
                    g_ptr_array_add (copy, g_ptr_array_index (members, i));
        }
    }

    if (copy->len > 0)
    {
        scope = cur_word;
        cur_word = NULL;
    }
    else
    {
        /* No direct hit - ask the user to enter a scope name. */
        g_ptr_array_free (copy, FALSE);
        copy = NULL;
        scope = ctags_ui_prompt_scope (cur_word);
        g_free (cur_word);

        if (scope == NULL || *scope == '\0')
        {
            g_free (scope);
            return;
        }

        copy = g_ptr_array_new ();
        for (l = d->repos; l != NULL; l = g_slist_next (l))
        {
            ctags_repo_t *repo = (ctags_repo_t *) l->data;
            guint i;

            if (repo->index == NULL)
                continue;

            members = ctags_index_find_scope (repo->index, scope);
            if (members != NULL)
                for (i = 0; i < members->len; i++)
                    g_ptr_array_add (copy, g_ptr_array_index (members, i));
        }

        if (copy->len == 0)
        {
            g_ptr_array_free (copy, FALSE);
            message (D_NORMAL, _ ("Ctags"), _ ("No members found for '%s'."), scope);
            g_free (scope);
            return;
        }
    }

    {
        GPtrArray *rows = ctags_build_member_rows (d, copy);

        ctags_ui_set_root_dir (ctags_first_root_dir (d));
        chosen = ctags_ui_select_members (scope, rows);
        g_ptr_array_free (rows, TRUE);
    }

    if (chosen != NULL && chosen->file != NULL)
    {
        char *path = ctags_resolve_entry_path (d, chosen);

        ctags_do_jump (d, edit, path, ctags_entry_resolve_line (chosen, path), chosen->name);
        g_free (path);
    }

    g_ptr_array_free (copy, FALSE);
    g_free (scope);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_load_tags (ctags_data_t *d, WEdit *edit)
{
    char *path;
    ctags_repo_t *repo;

    path = ctags_ui_prompt_tags_path ();
    if (path == NULL || *path == '\0')
    {
        g_free (path);
        return;
    }

    repo = ctags_repo_load (path);
    if (repo == NULL)
    {
        message (D_ERROR, _ ("Ctags"), _ ("Failed to load tags file: %s"), path);
        g_free (path);
        return;
    }

    d->repos = g_slist_prepend (d->repos, repo);
    message (D_NORMAL, _ ("Ctags"), _ ("Loaded %u symbols from %s"), repo->entries->len, path);

    g_free (path);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);

    (void) edit;
}

/* --------------------------------------------------------------------------------------------- */

/* Build a fresh ctags index for @root_dir into @tags_path.
   The temp file is created via mkstemp() in the same directory, so the
   final rename() is atomic on the same filesystem, and the slot can't be
   pre-created as a symlink by a co-located untrusted user.
   Returns TRUE on success; on failure the temp file is removed. */
static gboolean
ctags_run_ctags (ctags_data_t *d, const char *root_dir, const char *tags_path)
{
    char *tmp_path;
    char *cmd;
    int fd, ret;
    gboolean ok = FALSE;

    tmp_path = g_strdup_printf ("%s.tmp.XXXXXX", tags_path);
    fd = mkstemp (tmp_path);
    if (fd < 0)
    {
        message (D_ERROR, _ ("Ctags"), _ ("Cannot create temp file: %s"), g_strerror (errno));
        g_free (tmp_path);
        return FALSE;
    }
    close (fd);  // ctags(1) will (re)create with -f

    {
        char *q_root = g_shell_quote (root_dir);
        char *q_tmp = g_shell_quote (tmp_path);
        char *q_exe = g_shell_quote (d->cfg.ctags_cmd != NULL ? d->cfg.ctags_cmd : "ctags");

        cmd = g_strdup_printf ("cd %s && %s %s -f %s -R . 2>/dev/null", q_root, q_exe,
                               d->cfg.ctags_args != NULL ? d->cfg.ctags_args : "", q_tmp);

        g_free (q_root);
        g_free (q_tmp);
        g_free (q_exe);
    }

    ret = system (cmd);
    g_free (cmd);

    if (ret != -1 && WIFEXITED (ret) && WEXITSTATUS (ret) == 0)
    {
        if (rename (tmp_path, tags_path) == 0)
            ok = TRUE;
        else
            message (D_ERROR, _ ("Ctags"), _ ("Cannot rename %s to %s: %s"), tmp_path, tags_path,
                     g_strerror (errno));
    }

    if (!ok)
        unlink (tmp_path);

    g_free (tmp_path);
    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_reindex (ctags_data_t *d, WEdit *edit)
{
    GSList *l;
    char *file =
        d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;

    ctags_ensure_repo (d, file);
    g_free (file);

    if (d->repos == NULL)
    {
        /* No tags file found -- offer to create one */
        char *root;
        char *tags_path;
        ctags_repo_t *repo;

        root =
            input_dialog (_ ("Ctags: Create Index"), _ ("Project root directory to index:"),
                          MC_HISTORY_CTAGS_TAGS_PATH, mc_config_get_home_dir (), INPUT_COMPLETE_CD);
        if (root == NULL || *root == '\0')
        {
            g_free (root);
            return;
        }

        tags_path = g_build_filename (root, "tags", (char *) NULL);

        if (!ctags_run_ctags (d, root, tags_path))
        {
            message (D_ERROR, _ ("Ctags"), _ ("Failed to create index in %s"), root);
            g_free (tags_path);
            g_free (root);
            return;
        }

        g_free (root);

        repo = ctags_repo_load (tags_path);
        g_free (tags_path);

        if (repo != NULL)
        {
            d->repos = g_slist_prepend (d->repos, repo);
            message (D_NORMAL, _ ("Ctags"), _ ("Indexed: %u symbols"), repo->entries->len);
        }

        if (d->host->redraw != NULL)
            d->host->redraw (d->host);
        return;
    }

    for (l = d->repos; l != NULL; l = g_slist_next (l))
    {
        ctags_repo_t *repo = (ctags_repo_t *) l->data;

        if (repo->tags_path == NULL || repo->root_dir == NULL)
            continue;

        if (!ctags_run_ctags (d, repo->root_dir, repo->tags_path))
        {
            message (D_ERROR, _ ("Ctags"), _ ("Reindex failed for %s"), repo->tags_path);
            continue;
        }

        {
            ctags_repo_t *new_repo = ctags_repo_load (repo->tags_path);

            if (new_repo != NULL)
            {
                /* Swap entries and index in place so the iterator remains valid. */
                g_ptr_array_free (repo->entries, TRUE);
                ctags_index_free (repo->index);
                repo->entries = new_repo->entries;
                repo->index = new_repo->index;
                new_repo->entries = NULL;
                new_repo->index = NULL;
                ctags_repo_free (new_repo);

                message (D_NORMAL, _ ("Ctags"), _ ("Reindexed: %u symbols"), repo->entries->len);
            }
        }
    }

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_show_history (ctags_data_t *d, WEdit *edit)
{
    GPtrArray *all;
    ctags_entry_t *chosen = NULL;
    GSList *l;
    guint i;

    /* Ensure the repo for the current file is loaded so its saved history is available. */
    {
        char *file =
            d->host->get_current_file != NULL ? d->host->get_current_file (d->host, edit) : NULL;
        ctags_ensure_repo (d, file);
        g_free (file);
    }

    all = g_ptr_array_new ();

    /* Collect history from all repos, newest entries first (iterate in reverse) */
    for (l = d->repos; l != NULL; l = g_slist_next (l))
    {
        ctags_repo_t *repo = (ctags_repo_t *) l->data;

        if (repo->nav_history == NULL)
            continue;

        for (i = repo->nav_history->len; i > 0; i--)
            g_ptr_array_add (all, g_ptr_array_index (repo->nav_history, i - 1));
    }

    if (all->len == 0)
    {
        g_ptr_array_free (all, FALSE);
        message (D_NORMAL, _ ("Ctags"), _ ("Navigation history is empty."));
        return;
    }

    chosen = ctags_ui_select_refs (_ ("Navigation History"), all);

    if (chosen != NULL && chosen->file != NULL)
    {
        /* chosen points into some repo->nav_history; ctags_do_jump() pushes
           the current position first, which can trim the oldest entry from
           that array and free `chosen` underneath us.  Copy fields out
           before the jump. */
        char *file = g_strdup (chosen->file);
        long line = chosen->line;

        ctags_do_jump (d, edit, file, line, NULL);
        g_free (file);
    }

    g_ptr_array_free (all, FALSE);

    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_dispatch_command (ctags_data_t *d, WEdit *edit, long command)
{
    switch (command)
    {
    case CTAGS_CMD_MENU:
        /* handled at call site */
        break;
    case CTAGS_CMD_GOTO:
        ctags_goto_declaration (d, edit);
        break;
    case CTAGS_CMD_FIND_SYMBOL:
        ctags_find_symbol (d, edit);
        break;
    case CTAGS_CMD_GREP_REFS:
        ctags_grep_refs (d, edit);
        break;
    case CTAGS_CMD_HISTORY:
        ctags_show_history (d, edit);
        break;
    case CTAGS_CMD_BACK:
        edit_load_back_cmd (edit);
        if (d->host->redraw != NULL)
            d->host->redraw (d->host);
        break;
    case CTAGS_CMD_FORWARD:
        edit_load_forward_cmd (edit);
        if (d->host->redraw != NULL)
            d->host->redraw (d->host);
        break;
    case CTAGS_CMD_SEARCH_SYMBOL:
        ctags_search_symbol (d, edit);
        break;
    case CTAGS_CMD_SEARCH_FILE:
        ctags_search_file (d, edit);
        break;
    case CTAGS_CMD_SEARCH_CURRENT:
        ctags_search_current_file (d, edit);
        break;
    case CTAGS_CMD_COMPLETE:
        ctags_complete_symbol (d, edit);
        break;
    case CTAGS_CMD_MEMBERS:
        ctags_show_members (d, edit);
        break;
    case CTAGS_CMD_LOAD_TAGS:
        ctags_load_tags (d, edit);
        break;
    case CTAGS_CMD_MANAGE_REPOS:
        ctags_ui_manage_repos (&d->repos);
        if (d->host->redraw != NULL)
            d->host->redraw (d->host);
        break;
    case CTAGS_CMD_REINDEX:
        ctags_reindex (d, edit);
        break;
    case CTAGS_CMD_CONFIG:
        ctags_config_dialog (&d->cfg);
        break;
    default:
        break;
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_show_menu (ctags_data_t *d, WEdit *edit)
{
    struct
    {
        const char *label;
        long cmd;
    } items[] = {
        { N_ ("Go to declaration"), CTAGS_CMD_GOTO },
        { N_ ("Find declaration"), CTAGS_CMD_FIND_SYMBOL },
        { N_ ("Find references"), CTAGS_CMD_GREP_REFS },
        { N_ ("Complete symbol"), CTAGS_CMD_COMPLETE },
        { N_ ("Navigation history"), CTAGS_CMD_HISTORY },
        { N_ ("Go back"), CTAGS_CMD_BACK },
        { N_ ("Go forward"), CTAGS_CMD_FORWARD },
        { N_ ("Search symbol"), CTAGS_CMD_SEARCH_SYMBOL },
        { N_ ("Search file"), CTAGS_CMD_SEARCH_FILE },
        { N_ ("Symbols in current file"), CTAGS_CMD_SEARCH_CURRENT },
        { N_ ("Class/struct members"), CTAGS_CMD_MEMBERS },
        { N_ ("Load tags file"), CTAGS_CMD_LOAD_TAGS },
        { N_ ("Manage repositories"), CTAGS_CMD_MANAGE_REPOS },
        { N_ ("Reindex repository"), CTAGS_CMD_REINDEX },
        { N_ ("Configuration"), CTAGS_CMD_CONFIG },
    };

    int n = (int) G_N_ELEMENTS (items);
    int dlg_h = n + 2;
    int dlg_w = 32;
    WDialog *dlg;
    WListbox *list;
    int i;
    long cmd = CTAGS_CMD_NONE;

    dlg = dlg_create (TRUE, 0, 0, dlg_h, dlg_w, WPOS_CENTER | WPOS_TRYUP, TRUE, dialog_colors, NULL,
                      NULL, "[Ctags]", _ ("Ctags"));
    list = listbox_new (1, 1, dlg_h - 2, dlg_w - 2, FALSE, NULL);

    for (i = 0; i < n; i++)
        listbox_add_item (list, LISTBOX_APPEND_AT_END, 0, _ (items[i].label),
                          GINT_TO_POINTER ((gint) items[i].cmd), FALSE);

    group_add_widget (GROUP (dlg), list);

    if (dlg_run (dlg) == B_ENTER)
    {
        void *data = NULL;
        listbox_get_current (list, NULL, &data);
        cmd = (long) GPOINTER_TO_INT (data);
    }

    widget_destroy (WIDGET (dlg));

    if (cmd != CTAGS_CMD_NONE)
        ctags_dispatch_command (d, edit, cmd);
}

/* --------------------------------------------------------------------------------------------- */
/* Plugin callbacks */
/* --------------------------------------------------------------------------------------------- */

static void *
ctags_plugin_open (mc_editor_host_t *host, void *editor_dialog)
{
    ctags_data_t *d;

    (void) editor_dialog;

    d = g_new0 (ctags_data_t, 1);
    d->host = host;
    d->repos = NULL;
    ctags_config_load (&d->cfg);
    d->keymap = ctags_keymap_load ();
    return d;
}

/* --------------------------------------------------------------------------------------------- */

static void
ctags_plugin_close (void *plugin_data)
{
    ctags_data_t *d = (ctags_data_t *) plugin_data;

    if (d == NULL)
        return;

    g_slist_free_full (d->repos, (GDestroyNotify) ctags_repo_free);
    ctags_config_free (&d->cfg);
    if (d->keymap != NULL)
        g_array_free (d->keymap, TRUE);
    g_free (d);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
ctags_plugin_activate (void *plugin_data, void *edit)
{
    ctags_data_t *d = (ctags_data_t *) plugin_data;

    if (edit == NULL || d == NULL)
        return MC_EPR_FAILED;

    ctags_show_menu (d, (WEdit *) edit);
    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
ctags_plugin_handle_key (void *plugin_data, int key, void *edit)
{
    ctags_data_t *d = (ctags_data_t *) plugin_data;
    WEdit *e = (WEdit *) edit;
    long command;

    if (d == NULL || e == NULL)
        return MC_EPR_NOT_SUPPORTED;

    command = ctags_keymap_lookup_command (d->keymap, key);
    if (command == CTAGS_CMD_NONE)
        return MC_EPR_NOT_SUPPORTED;

    if (command == CTAGS_CMD_MENU)
        ctags_show_menu (d, e);
    else
        ctags_dispatch_command (d, e, command);

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
ctags_plugin_on_file_open (void *plugin_data, void *edit)
{
    ctags_data_t *d = (ctags_data_t *) plugin_data;
    WEdit *e = (WEdit *) edit;
    char *file;

    if (d == NULL || e == NULL)
        return MC_EPR_OK;

    if (!d->cfg.auto_discover)
        return MC_EPR_OK;

    file = d->host->get_current_file != NULL ? d->host->get_current_file (d->host, e) : NULL;
    if (file != NULL)
    {
        ctags_ensure_repo (d, file);
        g_free (file);
    }

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------------------------- */
/* Named actions exposed via the Navigate menu */
/* --------------------------------------------------------------------------------------------- */

#define CTAGS_ACT_GOTO           0
#define CTAGS_ACT_FIND           1
#define CTAGS_ACT_GREP_REFS      2
#define CTAGS_ACT_HISTORY        3
#define CTAGS_ACT_BACK           4
#define CTAGS_ACT_FORWARD        5
#define CTAGS_ACT_MEMBERS        6
#define CTAGS_ACT_SEARCH_SYMBOL  7
#define CTAGS_ACT_SEARCH_CURRENT 8
#define CTAGS_ACT_COMPLETE       9

static mc_ep_result_t
ctags_act_goto (void *pd, void *edit)
{
    ctags_goto_declaration ((ctags_data_t *) pd, (WEdit *) edit);
    return MC_EPR_OK;
}

static mc_ep_result_t
ctags_act_find (void *pd, void *edit)
{
    ctags_find_symbol ((ctags_data_t *) pd, (WEdit *) edit);
    return MC_EPR_OK;
}

static mc_ep_result_t
ctags_act_grep_refs (void *pd, void *edit)
{
    ctags_grep_refs ((ctags_data_t *) pd, (WEdit *) edit);
    return MC_EPR_OK;
}

static mc_ep_result_t
ctags_act_history (void *pd, void *edit)
{
    ctags_show_history ((ctags_data_t *) pd, (WEdit *) edit);
    return MC_EPR_OK;
}

static mc_ep_result_t
ctags_act_back (void *pd, void *edit)
{
    ctags_data_t *d = (ctags_data_t *) pd;
    edit_load_back_cmd ((WEdit *) edit);
    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
    return MC_EPR_OK;
}

static mc_ep_result_t
ctags_act_forward (void *pd, void *edit)
{
    ctags_data_t *d = (ctags_data_t *) pd;
    edit_load_forward_cmd ((WEdit *) edit);
    if (d->host->redraw != NULL)
        d->host->redraw (d->host);
    return MC_EPR_OK;
}

static mc_ep_result_t
ctags_act_members (void *pd, void *edit)
{
    ctags_show_members ((ctags_data_t *) pd, (WEdit *) edit);
    return MC_EPR_OK;
}

static mc_ep_result_t
ctags_act_search_symbol (void *pd, void *edit)
{
    ctags_search_symbol ((ctags_data_t *) pd, (WEdit *) edit);
    return MC_EPR_OK;
}

static mc_ep_result_t
ctags_act_search_current (void *pd, void *edit)
{
    ctags_search_current_file ((ctags_data_t *) pd, (WEdit *) edit);
    return MC_EPR_OK;
}

static mc_ep_result_t
ctags_act_complete (void *pd, void *edit)
{
    ctags_complete_symbol ((ctags_data_t *) pd, (WEdit *) edit);
    return MC_EPR_OK;
}

/* Maps menu-action index to internal CTAGS_CMD_* used in the keymap. */
static const long ctags_action_to_cmd[] = {
    [CTAGS_ACT_GOTO] = CTAGS_CMD_GOTO,
    [CTAGS_ACT_FIND] = CTAGS_CMD_FIND_SYMBOL,
    [CTAGS_ACT_GREP_REFS] = CTAGS_CMD_GREP_REFS,
    [CTAGS_ACT_HISTORY] = CTAGS_CMD_HISTORY,
    [CTAGS_ACT_BACK] = CTAGS_CMD_BACK,
    [CTAGS_ACT_FORWARD] = CTAGS_CMD_FORWARD,
    [CTAGS_ACT_MEMBERS] = CTAGS_CMD_MEMBERS,
    [CTAGS_ACT_SEARCH_SYMBOL] = CTAGS_CMD_SEARCH_SYMBOL,
    [CTAGS_ACT_SEARCH_CURRENT] = CTAGS_CMD_SEARCH_CURRENT,
    [CTAGS_ACT_COMPLETE] = CTAGS_CMD_COMPLETE,
};

/* Resolve current keymap binding for an action and return its display text. */
static char *
ctags_get_menu_shortcut (int action_index)
{
    GArray *keymap;
    long cmd;
    int key = 0;
    guint i;
    char *result = NULL;

    if (action_index < 0 || action_index >= (int) G_N_ELEMENTS (ctags_action_to_cmd))
        return NULL;

    cmd = ctags_action_to_cmd[action_index];

    /* Load fresh so user changes to ctags.keymap show up on next menu open. */
    keymap = ctags_keymap_load ();
    for (i = 0; i < keymap->len; i++)
    {
        const ctags_keybind_t *b = &g_array_index (keymap, ctags_keybind_t, i);

        if (b->command == cmd)
        {
            key = b->key;
            break;
        }
    }
    g_array_free (keymap, TRUE);

    if (key != 0)
        result = tty_keycode_to_keyname (key);

    return result;
}

static const mc_ep_action_t ctags_actions[] = {
    /* CTAGS_ACT_GOTO */ { "Go to declaration", ctags_act_goto },
    /* CTAGS_ACT_FIND */ { "Find declaration", ctags_act_find },
    /* CTAGS_ACT_GREP_REFS */ { "Find references", ctags_act_grep_refs },
    /* CTAGS_ACT_HISTORY */ { "Navigation history", ctags_act_history },
    /* CTAGS_ACT_BACK */ { "Go back", ctags_act_back },
    /* CTAGS_ACT_FORWARD */ { "Go forward", ctags_act_forward },
    /* CTAGS_ACT_MEMBERS */ { "Class/struct members", ctags_act_members },
    /* CTAGS_ACT_SEARCH_SYMBOL */ { "Search symbol", ctags_act_search_symbol },
    /* CTAGS_ACT_SEARCH_CURRENT */ { "Symbols in current file", ctags_act_search_current },
    /* CTAGS_ACT_COMPLETE */ { "Complete symbol", ctags_act_complete },
};

/* Shortcuts are resolved dynamically from ctags.keymap via get_menu_shortcut. */
static const mc_ep_cmd_menu_entry_t ctags_menu_entries[] = {
    { MC_EP_MENU_NAVIGATE, N_ ("Go to &declaration"), CTAGS_ACT_GOTO, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("&Find declaration"), CTAGS_ACT_FIND, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("Find &references"), CTAGS_ACT_GREP_REFS, NULL },
    { MC_EP_MENU_NAVIGATE, NULL, 0, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("Navigation &history"), CTAGS_ACT_HISTORY, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("Go &back"), CTAGS_ACT_BACK, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("Go for&ward"), CTAGS_ACT_FORWARD, NULL },
    { MC_EP_MENU_NAVIGATE, NULL, 0, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("Class/struct &members"), CTAGS_ACT_MEMBERS, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("&Search symbol"), CTAGS_ACT_SEARCH_SYMBOL, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("S&ymbols in file"), CTAGS_ACT_SEARCH_CURRENT, NULL },
    { MC_EP_MENU_NAVIGATE, N_ ("&Complete symbol"), CTAGS_ACT_COMPLETE, NULL },
};

static const mc_editor_plugin_t ctags_plugin_descriptor = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "ctags",
    .display_name = "&Ctags...",
    .flags = MC_EPF_HAS_MENU,
    .open = ctags_plugin_open,
    .close = ctags_plugin_close,
    .activate = ctags_plugin_activate,
    .configure = NULL,
    .handle_action = NULL,
    .query_state = NULL,
    .handle_key = ctags_plugin_handle_key,
    .handle_event = NULL,
    .on_file_open = ctags_plugin_on_file_open,
    .on_file_close = NULL,
    .actions = ctags_actions,
    .action_count = G_N_ELEMENTS (ctags_actions),
    .cmd_menu_entries = ctags_menu_entries,
    .cmd_menu_entry_count = G_N_ELEMENTS (ctags_menu_entries),
    .get_menu_shortcut = ctags_get_menu_shortcut,
};

/* --------------------------------------------------------------------------------------------- */

const mc_editor_plugin_t *
ctags_get_plugin (void)
{
    return &ctags_plugin_descriptor;
}

/* --------------------------------------------------------------------------------------------- */
