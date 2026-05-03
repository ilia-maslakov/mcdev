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

#include <config.h>

#include <string.h>

#include "lib/global.h"
#include "lib/mcconfig.h"
#include "lib/terminal.h"
#include "lib/tty/key.h"

#include "mcterm_key.h"

/*** file scope macro definitions ****************************************************************/

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

static GHashTable *mcterm_enc_map = NULL;

/*** file scope functions ************************************************************************/

static void
mcterm_remember_sequence (int key_code, char *raw)
{
    if (raw != NULL && *raw != '\0')
        g_hash_table_replace (mcterm_enc_map, GINT_TO_POINTER (tty_normalize_keycode (key_code)),
                              raw);
    else
        g_free (raw);
}

/* --------------------------------------------------------------------------------------------- */

static size_t
mcterm_copy_seq (unsigned char *buf, size_t bufsz, const char *seq)
{
    size_t len;

    if (seq == NULL)
        return 0;

    len = strlen (seq);
    if (len == 0 || len > bufsz)
        return 0;

    memcpy (buf, seq, len);
    return len;
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_load_section_rec (const char *terminal, mc_config_t *cfg, GHashTable *visited)
{
    char *section_name;
    gchar **profile_keys, **keys;

    if (terminal == NULL || cfg == NULL || g_hash_table_contains (visited, terminal))
        return;

    g_hash_table_add (visited, g_strdup (terminal));

    section_name = g_strconcat ("terminal:", terminal, (char *) NULL);
    keys = mc_config_get_keys (cfg, section_name, NULL);

    for (profile_keys = keys; *profile_keys != NULL; profile_keys++)
    {
        if (g_ascii_strcasecmp (*profile_keys, "copy") == 0)
        {
            char *valcopy = mc_config_get_string (cfg, section_name, *profile_keys, "");
            mcterm_load_section_rec (valcopy, cfg, visited);
            g_free (valcopy);
            continue;
        }

        {
            int key_code = tty_keyname_to_keycode (*profile_keys, NULL);

            if (key_code != 0)
            {
                gchar **values = mc_config_get_string_list (cfg, section_name, *profile_keys, NULL);

                if (values != NULL)
                {
                    /* A list means decoder aliases; the encoder sends one
                       sequence -- use the first value as the canonical one. */
                    char *raw = convert_controls (values[0]);

                    mcterm_remember_sequence (key_code, raw);
                    g_strfreev (values);
                }
                else
                {
                    char *value = mc_config_get_string (cfg, section_name, *profile_keys, "");
                    char *raw = convert_controls (value);

                    g_free (value);
                    mcterm_remember_sequence (key_code, raw);
                }
            }
        }
    }

    g_strfreev (keys);
    g_free (section_name);
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_load_terminal (mc_config_t *cfg)
{
    GHashTable *visited;

    /* Load both base and 256-colour variant under one visited set so that
       if xterm-256color has copy=xterm, the xterm section is not walked twice. */
    visited = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    mcterm_load_section_rec ("xterm", cfg, visited);
    mcterm_load_section_rec ("xterm-256color", cfg, visited);
    g_hash_table_destroy (visited);
}

/* --------------------------------------------------------------------------------------------- */

static size_t
mcterm_copy_enc_seq (int key, unsigned char *buf, size_t bufsz)
{
    const char *raw;

    if (mcterm_enc_map == NULL)
        return 0;

    raw = g_hash_table_lookup (mcterm_enc_map, GINT_TO_POINTER (tty_normalize_keycode (key)));
    return mcterm_copy_seq (buf, bufsz, raw);
}

/* --------------------------------------------------------------------------------------------- */

/*** public functions ****************************************************************************/

void
mcterm_key_table_init (const char *global_config_path, mc_config_t *cfg)
{
    g_clear_pointer (&mcterm_enc_map, g_hash_table_destroy);
    mcterm_enc_map = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

    if (global_config_path != NULL)
    {
        mc_config_t *global_cfg = mc_config_init (global_config_path, TRUE);

        mcterm_load_terminal (global_cfg);
        mc_config_deinit (global_cfg);
    }

    mcterm_load_terminal (cfg);
    /* Do NOT load the outer $TERM key file here: this table encodes keys for
     * the embedded xterm-256color child, not for decoding the outer terminal. */
}

/* --------------------------------------------------------------------------------------------- */

size_t
mcterm_encode_key_xterm (int key, unsigned char *buf, size_t bufsz, gboolean app_cursor)
{
    if (bufsz == 0)
        return 0;

    if ((key & ~0x1F) == KEY_M_CTRL)
        key &= 0x1F;

    if (key == '\n' || key == '\r')
    {
        buf[0] = '\r';
        return 1;
    }

    if (key >= 0x01 && key <= 0x1F) /* C0: Ctrl+A..Z plus Ctrl+\, ], ^, _ and ESC */
    {
        buf[0] = (unsigned char) key;
        return 1;
    }
    if (key == 0x7F)
    {
        buf[0] = 0x7F;
        return 1;
    }
    if (key >= 0x20 && key < 0x80) /* printable ASCII */
    {
        buf[0] = (unsigned char) key;
        return 1;
    }
    if (key >= 0x80 && key <= 0xFF)
    {
        buf[0] = (unsigned char) key;
        return 1;
    }

    if (key == KEY_BACKSPACE)
    {
        buf[0] = 0x7F;
        return 1;
    }
    if (key == KEY_ENTER)
    {
        buf[0] = '\r';
        return 1;
    }

    if ((key & KEY_M_MASK) == 0 && app_cursor)
        switch (key)
        {
        case KEY_END:
            return mcterm_copy_seq (buf, bufsz, "\x1bOF");
        case KEY_UP:
            return mcterm_copy_seq (buf, bufsz, "\x1bOA");
        case KEY_DOWN:
            return mcterm_copy_seq (buf, bufsz, "\x1bOB");
        case KEY_LEFT:
            return mcterm_copy_seq (buf, bufsz, "\x1bOD");
        case KEY_RIGHT:
            return mcterm_copy_seq (buf, bufsz, "\x1bOC");
        case KEY_HOME:
            return mcterm_copy_seq (buf, bufsz, "\x1bOH");
        default:
            break;
        }

    {
        size_t n = mcterm_copy_enc_seq (key, buf, bufsz);

        if (n > 0)
            return n;
    }

    if ((key & KEY_M_ALT) != 0 && bufsz >= 2)
    {
        size_t n = mcterm_encode_key_xterm (key & ~KEY_M_ALT, buf + 1, bufsz - 1, app_cursor);

        if (n > 0)
        {
            buf[0] = 0x1B;
            return n + 1;
        }
        return 0;
    }

    return 0;
}
