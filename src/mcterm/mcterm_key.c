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

/* Private encoding map: normalized keycode -> raw ESC byte sequence. */
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
mcterm_load_section (const char *terminal, mc_config_t *cfg)
{
    char *section_name;
    gchar **profile_keys, **keys;

    if (terminal == NULL || cfg == NULL)
        return;

    section_name = g_strconcat ("terminal:", terminal, (char *) NULL);
    keys = mc_config_get_keys (cfg, section_name, NULL);

    for (profile_keys = keys; *profile_keys != NULL; profile_keys++)
    {
        if (g_ascii_strcasecmp (*profile_keys, "copy") == 0)
        {
            char *valcopy = mc_config_get_string (cfg, section_name, *profile_keys, "");
            mcterm_load_section (valcopy, cfg);
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
                    gchar **v;

                    for (v = values; *v != NULL; v++)
                    {
                        char *raw = convert_controls (*v);

                        mcterm_remember_sequence (key_code, raw);
                    }
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
    mcterm_load_section ("xterm", cfg);
    mcterm_load_section ("xterm-256color", cfg);
}

/* --------------------------------------------------------------------------------------------- */

static void
mcterm_load_term_keys_file (void)
{
    const char *term;
    const char *config_path;
    char *term_file;
    mc_config_t *term_cfg;
    gchar **keys;
    gchar **pk;

    term = getenv ("TERM");
    if (term == NULL || *term == '\0')
        term = "unknown";

    config_path = mc_config_get_path ();
    if (config_path == NULL)
        return;

    term_file = g_build_filename (config_path, "term", term, (char *) NULL);
    if (!g_file_test (term_file, G_FILE_TEST_EXISTS))
    {
        g_free (term_file);
        return;
    }

    term_cfg = mc_config_init (term_file, TRUE);
    keys = mc_config_get_keys (term_cfg, "keys", NULL);

    for (pk = keys; *pk != NULL; pk++)
    {
        int key_code = tty_keyname_to_keycode (*pk, NULL);

        if (key_code != 0)
        {
            char *value = mc_config_get_string_raw (term_cfg, "keys", *pk, NULL);

            if (value != NULL)
            {
                mcterm_remember_sequence (key_code, convert_controls (value));
                g_free (value);
            }
        }
    }

    g_strfreev (keys);
    mc_config_deinit (term_cfg);
    g_free (term_file);
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

    if (global_config_path != NULL)
        mcterm_load_term_keys_file ();
}

/* --------------------------------------------------------------------------------------------- */

/* Translate MC keycode to PTY byte sequence.  Special keys use the private
 * xterm-256color encoding map; keys absent from the map return 0. */
size_t
mcterm_encode_key_xterm (int key, unsigned char *buf, size_t bufsz, gboolean app_cursor)
{
    if (bufsz == 0)
        return 0;

    /* XCTRL(x) = KEY_M_CTRL|(x&0x1F): strip modifier to get raw C0 byte. */
    if ((key & ~0x1F) == KEY_M_CTRL)
        key &= 0x1F;

    /* Enter must come before the Ctrl range: '\n' (0x0A) == Ctrl+J but
     * must be forwarded as CR to the PTY. */
    if (key == '\n' || key == '\r')
    {
        buf[0] = '\r';
        return 1;
    }

    /* C0 control bytes and printable ASCII */
    if (key >= 0x01 && key <= 0x1A) /* Ctrl+A .. Ctrl+Z */
    {
        buf[0] = (unsigned char) key;
        return 1;
    }
    if (key == 0x1B) /* bare ESC */
    {
        buf[0] = 0x1B;
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
    /* Raw UTF-8 lead/continuation bytes arrive as individual MSG_KEY events
     * in the range 0x80-0xFF.  Pass each byte through unchanged so the PTY
     * receives the original byte sequence. */
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

    /* In application cursor mode, unmodified cursor keys use SS3 forms. */
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

    /* Alt fallback: ESC prefix + base key encoding. */
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
