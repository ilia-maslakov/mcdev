#include <config.h>

#include <stdarg.h>
#include <stdio.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/editor-plugin.h"
#include "lib/tty/key.h"

#include "src/editor/edit-impl.h"
#include "src/editor/editwidget.h"
#include "src/editor-plugins/etags/etags.h"
#include "src/editor-plugins/spell/spell.h"

typedef struct
{
    mc_editor_host_t *host;
    GArray *spell_keymap;
} editor_builtin_plugin_data_t;

typedef struct
{
    long command;
    int key;
} spell_keybind_t;

#define SPELL_KEYMAP_FILE    "spell.keymap"
#define SPELL_DEBUG_LOG_PATH "/tmp/mc-spell.log"

/* --------------------------------------------------------------------------------------------- */

static void spell_plugin_debug_log (const char *fmt, ...) G_GNUC_PRINTF (1, 2);

/* --------------------------------------------------------------------------------------------- */

static void
spell_plugin_debug_log (const char *fmt, ...)
{
    FILE *fp;
    va_list ap;

    fp = fopen (SPELL_DEBUG_LOG_PATH, "a");
    if (fp == NULL)
        return;

    va_start (ap, fmt);
    vfprintf (fp, fmt, ap);
    va_end (ap);
    fputc ('\n', fp);
    fclose (fp);
}

/* --------------------------------------------------------------------------------------------- */

static void *
editor_builtin_plugin_open (mc_editor_host_t *host, void *editor_dialog)
{
    editor_builtin_plugin_data_t *data;

    (void) editor_dialog;

    data = g_new0 (editor_builtin_plugin_data_t, 1);
    data->host = host;
    data->spell_keymap = NULL;
    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void
editor_builtin_plugin_close (void *plugin_data)
{
    editor_builtin_plugin_data_t *data = (editor_builtin_plugin_data_t *) plugin_data;

    if (data != NULL)
    {
        if (data->spell_keymap != NULL)
            g_array_free (data->spell_keymap, TRUE);
    }

    g_free (plugin_data);
}

/* --------------------------------------------------------------------------------------------- */

static long
spell_keymap_action_from_name (const char *name)
{
    if (g_strcmp0 (name, "SpellCheck") == 0)
        return CK_SpellCheck;
    if (g_strcmp0 (name, "SpellCheckCurrentWord") == 0)
        return CK_SpellCheckCurrentWord;
    if (g_strcmp0 (name, "SpellCheckSelectLang") == 0)
        return CK_SpellCheckSelectLang;
    return CK_IgnoreKey;
}

/* --------------------------------------------------------------------------------------------- */

static void
spell_keymap_add_binding (GArray *keymap, long command, const char *keybind)
{
    spell_keybind_t bind;
    char *caption = NULL;

    if (keymap == NULL || command == CK_IgnoreKey || keybind == NULL || *keybind == '\0')
        return;

    bind.key = tty_keyname_to_keycode (keybind, &caption);
    g_free (caption);

    if (bind.key == 0)
        return;

    bind.command = command;
    g_array_append_val (keymap, bind);
    spell_plugin_debug_log ("spell: keymap bind %s -> key=%d command=%ld", keybind, bind.key,
                            bind.command);
}

/* --------------------------------------------------------------------------------------------- */

static void
spell_keymap_parse_line (GArray *keymap, char *line)
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

    action = spell_keymap_action_from_name (name);
    if (action == CK_IgnoreKey || *value == '\0')
        return;

    values = g_strsplit (value, ";", -1);
    for (it = values; it != NULL && *it != NULL; it++)
    {
        char *keybind = g_strstrip (*it);

        if (*keybind != '\0')
            spell_keymap_add_binding (keymap, action, keybind);
    }
    g_strfreev (values);
}

/* --------------------------------------------------------------------------------------------- */

static void
spell_keymap_load_file (GArray *keymap, const char *fname)
{
    char *contents = NULL;
    gsize len = 0;
    gchar **lines, **it;

    if (keymap == NULL || fname == NULL)
        return;

    if (!exist_file (fname))
    {
        spell_plugin_debug_log ("spell: keymap file not found: %s", fname);
        return;
    }

    if (!g_file_get_contents (fname, &contents, &len, NULL))
    {
        spell_plugin_debug_log ("spell: keymap read failed: %s", fname);
        return;
    }

    spell_plugin_debug_log ("spell: loading keymap file: %s", fname);
    lines = g_strsplit (contents, "\n", -1);
    for (it = lines; it != NULL && *it != NULL; it++)
        spell_keymap_parse_line (keymap, *it);
    g_strfreev (lines);
    g_free (contents);
}

/* --------------------------------------------------------------------------------------------- */

static GArray *
spell_keymap_load (void)
{
    GArray *keymap;
    char *fname;

    keymap = g_array_new (FALSE, FALSE, sizeof (spell_keybind_t));

    if (mc_global.share_data_dir != NULL)
    {
        fname = g_build_filename (mc_global.share_data_dir, SPELL_KEYMAP_FILE, (char *) NULL);
        spell_keymap_load_file (keymap, fname);
        g_free (fname);
    }
    else
        spell_plugin_debug_log ("spell: share_data_dir is NULL, skip system keymap");

    if (mc_global.sysconfig_dir != NULL)
    {
        fname = g_build_filename (mc_global.sysconfig_dir, SPELL_KEYMAP_FILE, (char *) NULL);
        spell_keymap_load_file (keymap, fname);
        g_free (fname);
    }
    else
        spell_plugin_debug_log ("spell: sysconfig_dir is NULL, skip sysconfig keymap");

    fname = mc_config_get_full_path (SPELL_KEYMAP_FILE);
    spell_keymap_load_file (keymap, fname);
    g_free (fname);

    spell_plugin_debug_log ("spell: keymap loaded total binds=%u", keymap->len);
    return keymap;
}

/* --------------------------------------------------------------------------------------------- */

static long
spell_keymap_lookup_command (const GArray *keymap, int key)
{
    guint i;

    if (keymap == NULL)
        return CK_IgnoreKey;

    for (i = 0; i < keymap->len; i++)
    {
        const spell_keybind_t *bind;

        bind = &g_array_index (keymap, spell_keybind_t, i);
        if (bind->key == key)
            return bind->command;
    }

    return CK_IgnoreKey;
}

/* --------------------------------------------------------------------------------------------- */

static void *
spell_plugin_open (mc_editor_host_t *host, void *editor_dialog)
{
    editor_builtin_plugin_data_t *data;

    spell_runtime_init ();
    data = (editor_builtin_plugin_data_t *) editor_builtin_plugin_open (host, editor_dialog);
    if (data != NULL)
        data->spell_keymap = spell_keymap_load ();
    return data;
}

/* --------------------------------------------------------------------------------------------- */

static void
spell_plugin_close (void *plugin_data)
{
    editor_builtin_plugin_close (plugin_data);
    spell_runtime_shutdown ();
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
mail_plugin_activate (void *plugin_data, void *edit)
{
    editor_builtin_plugin_data_t *data = (editor_builtin_plugin_data_t *) plugin_data;

    if (edit == NULL)
        return MC_EPR_FAILED;

    edit_mail_dialog ((WEdit *) edit);

    if (data != NULL && data->host != NULL && data->host->redraw != NULL)
        (data->host->redraw) (data->host);

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
scripts_plugin_handle_action (void *plugin_data, long command, void *edit)
{
    editor_builtin_plugin_data_t *data = (editor_builtin_plugin_data_t *) plugin_data;
    int macro_number;

    if (edit == NULL || (command / CK_PipeBlock (0)) != 1)
        return MC_EPR_NOT_SUPPORTED;

    macro_number = (int) (command - CK_PipeBlock (0));
    edit_block_process_cmd ((WEdit *) edit, macro_number);

    if (data != NULL && data->host != NULL && data->host->redraw != NULL)
        (data->host->redraw) (data->host);

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
etags_plugin_handle_action (void *plugin_data, long command, void *edit)
{
    editor_builtin_plugin_data_t *data = (editor_builtin_plugin_data_t *) plugin_data;

    if (edit == NULL || command != CK_Find)
        return MC_EPR_NOT_SUPPORTED;

    edit_get_match_keyword_cmd ((WEdit *) edit);

    if (data != NULL && data->host != NULL && data->host->redraw != NULL)
        (data->host->redraw) (data->host);

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
spell_plugin_activate (void *plugin_data, void *edit)
{
    editor_builtin_plugin_data_t *data = (editor_builtin_plugin_data_t *) plugin_data;

    if (edit == NULL)
        return MC_EPR_NOT_SUPPORTED;

    edit_spellcheck_file ((WEdit *) edit);

    if (data != NULL && data->host != NULL && data->host->redraw != NULL)
        (data->host->redraw) (data->host);

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
spell_plugin_configure (void *plugin_data, void *edit)
{
    editor_builtin_plugin_data_t *data = (editor_builtin_plugin_data_t *) plugin_data;

    (void) edit;
    edit_spell_plugin_settings ();

    if (data != NULL && data->host != NULL && data->host->redraw != NULL)
        (data->host->redraw) (data->host);

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
spell_plugin_query_state (void *plugin_data, void *edit, mc_ep_state_t *state)
{
    (void) plugin_data;
    (void) edit;
    return spell_query_state (state);
}

/* --------------------------------------------------------------------------------------------- */

static mc_ep_result_t
spell_plugin_handle_action (void *plugin_data, long command, void *edit)
{
    editor_builtin_plugin_data_t *data = (editor_builtin_plugin_data_t *) plugin_data;
    mc_ep_state_t state = { TRUE, TRUE, NULL };

    if (spell_query_state (&state) != MC_EPR_OK || !state.available || !state.enabled)
    {
        message (D_ERROR, _ ("Spell"), "%s",
                 state.reason != NULL ? state.reason : _ ("Spell backend is unavailable."));
        return MC_EPR_NOT_SUPPORTED;
    }

    if (edit == NULL && command != CK_SpellCheckSelectLang)
        return MC_EPR_NOT_SUPPORTED;

    switch (command)
    {
    case CK_SpellCheck:
        edit_spellcheck_file ((WEdit *) edit);
        break;
    case CK_SpellCheckSelectLang:
        edit_set_spell_lang ();
        break;
    default:
        return MC_EPR_NOT_SUPPORTED;
    }

    if (data != NULL && data->host != NULL && data->host->redraw != NULL)
        (data->host->redraw) (data->host);

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */
static mc_ep_result_t
spell_plugin_handle_key (void *plugin_data, int key, void *edit)
{
    editor_builtin_plugin_data_t *data = (editor_builtin_plugin_data_t *) plugin_data;
    mc_ep_state_t state = { TRUE, TRUE, NULL };
    long command;

    if (edit == NULL || data == NULL)
    {
        spell_plugin_debug_log ("spell: key event ignored key=%d (no edit/data)", key);
        return MC_EPR_NOT_SUPPORTED;
    }

    command = spell_keymap_lookup_command (data->spell_keymap, key);
    if (command == CK_IgnoreKey)
    {
        spell_plugin_debug_log ("spell: key=%d not mapped in spell.keymap", key);
        return MC_EPR_NOT_SUPPORTED;
    }

    spell_plugin_debug_log ("spell: key=%d resolved command=%ld", key, command);

    if (spell_query_state (&state) != MC_EPR_OK || !state.available || !state.enabled)
    {
        spell_plugin_debug_log ("spell: key command rejected by state, reason=%s",
                                state.reason != NULL ? state.reason : "unavailable");
        message (D_ERROR, _ ("Spell"), "%s",
                 state.reason != NULL ? state.reason : _ ("Spell backend is unavailable."));
        return MC_EPR_NOT_SUPPORTED;
    }

    switch (command)
    {
    case CK_SpellCheck:
        edit_spellcheck_file ((WEdit *) edit);
        break;
    case CK_SpellCheckCurrentWord:
        edit_suggest_current_word ((WEdit *) edit);
        break;
    case CK_SpellCheckSelectLang:
        edit_set_spell_lang ();
        break;
    default:
        spell_plugin_debug_log ("spell: key command %ld not supported", command);
        return MC_EPR_NOT_SUPPORTED;
    }

    spell_plugin_debug_log ("spell: key command handled command=%ld", command);

    if (data != NULL && data->host != NULL && data->host->redraw != NULL)
        (data->host->redraw) (data->host);

    return MC_EPR_OK;
}

/* --------------------------------------------------------------------------------------------- */

static const mc_editor_plugin_t edit_builtin_mail_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "mail",
    .display_name = "&Mail...",
    .flags = MC_EPF_HAS_MENU,
    .open = editor_builtin_plugin_open,
    .close = editor_builtin_plugin_close,
    .activate = mail_plugin_activate,
    .configure = NULL,
    .handle_action = NULL,
    .query_state = NULL,
    .handle_key = NULL,
    .handle_event = NULL,
    .on_file_open = NULL,
    .on_file_close = NULL,
};

/* --------------------------------------------------------------------------------------------- */

static const mc_editor_plugin_t edit_builtin_scripts_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "scripts",
    .display_name = "Scripts",
    .flags = MC_EPF_NONE,
    .open = editor_builtin_plugin_open,
    .close = editor_builtin_plugin_close,
    .activate = NULL,
    .configure = NULL,
    .handle_action = scripts_plugin_handle_action,
    .query_state = NULL,
    .handle_key = NULL,
    .handle_event = NULL,
    .on_file_open = NULL,
    .on_file_close = NULL,
};

/* --------------------------------------------------------------------------------------------- */

static const mc_editor_plugin_t edit_builtin_etags_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "etags",
    .display_name = "Etags",
    .flags = MC_EPF_NONE,
    .open = editor_builtin_plugin_open,
    .close = editor_builtin_plugin_close,
    .activate = NULL,
    .configure = NULL,
    .handle_action = etags_plugin_handle_action,
    .query_state = NULL,
    .handle_key = NULL,
    .handle_event = NULL,
    .on_file_open = NULL,
    .on_file_close = NULL,
};

/* --------------------------------------------------------------------------------------------- */

static const mc_editor_plugin_t edit_builtin_spell_plugin = {
    .api_version = MC_EDITOR_PLUGIN_API_VERSION,
    .name = "spell",
    .display_name = "Spell",
    .flags = MC_EPF_HAS_MENU,
    .open = spell_plugin_open,
    .close = spell_plugin_close,
    .activate = spell_plugin_activate,
    .configure = spell_plugin_configure,
    .handle_action = spell_plugin_handle_action,
    .query_state = spell_plugin_query_state,
    .handle_key = spell_plugin_handle_key,
    .handle_event = NULL,
    .on_file_open = NULL,
    .on_file_close = NULL,
};

/* --------------------------------------------------------------------------------------------- */

void
editor_plugins_register_all (void)
{
    /* This pass is repeatable: mc_editor_plugin_add() rejects duplicates by
       name, and plugins skipped while disabled can be registered after the
       preference changes. */
    (void) mc_editor_plugin_add (&edit_builtin_mail_plugin);
    (void) mc_editor_plugin_add (&edit_builtin_scripts_plugin);
    (void) mc_editor_plugin_add (&edit_builtin_etags_plugin);
    (void) mc_editor_plugin_add (&edit_builtin_spell_plugin);
}
