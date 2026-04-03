/*
   Docker tar helpers.

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

#include <config.h>

#include <errno.h>
#include <unistd.h>

#include "lib/global.h"

#include "docker-tar.h"

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_tar_read_full (int fd, void *buf, size_t count)
{
    size_t done = 0;

    while (done < count)
    {
        ssize_t n = read (fd, (char *) buf + done, count - done);

        if (n > 0)
            done += n;
        else if (n == 0)
            return FALSE;
        else if (errno != EINTR)
            return FALSE;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_tar_skip (int fd, guint64 count)
{
    char scratch[4096];

    while (count > 0)
    {
        size_t chunk = (count > sizeof (scratch)) ? sizeof (scratch) : (size_t) count;

        if (!docker_tar_read_full (fd, scratch, chunk))
            return FALSE;
        count -= chunk;
    }

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
docker_tar_is_zero_block (const void *block)
{
    const unsigned char *p = (const unsigned char *) block;
    size_t i;

    for (i = 0; i < DOCKER_TAR_BLOCK_SIZE; i++)
        if (p[i] != 0)
            return FALSE;

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

guint64
docker_tar_parse_octal (const char *field, size_t len, gboolean *ok)
{
    guint64 val = 0;
    size_t i;
    gboolean got_digit = FALSE;

    for (i = 0; i < len; i++)
    {
        if (field[i] >= '0' && field[i] <= '7')
        {
            val = val * 8 + (guint64) (field[i] - '0');
            got_digit = TRUE;
        }
        else if (field[i] == ' ' || field[i] == '\0')
        {
            if (got_digit)
                break;
        }
        else
        {
            if (ok != NULL)
                *ok = FALSE;
            return 0;
        }
    }

    if (ok != NULL)
        *ok = TRUE;

    return val;
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_tar_header_get_path (const struct docker_tar_header *hdr, const char *longname_override)
{
    if (longname_override != NULL && longname_override[0] != '\0')
        return g_strdup (longname_override);

    if (hdr->prefix[0] != '\0')
    {
        char prefix_buf[156];
        char name_buf[101];

        g_strlcpy (prefix_buf, hdr->prefix, sizeof (prefix_buf));
        g_strlcpy (name_buf, hdr->name, sizeof (name_buf));
        return g_strdup_printf ("%s/%s", prefix_buf, name_buf);
    }

    return g_strndup (hdr->name, sizeof (hdr->name));
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_tar_header_get_linkname (const struct docker_tar_header *hdr, const char *longlink_override)
{
    if (longlink_override != NULL && longlink_override[0] != '\0')
        return g_strdup (longlink_override);

    if (hdr->linkname[0] == '\0')
        return NULL;

    return g_strndup (hdr->linkname, sizeof (hdr->linkname));
}

/* --------------------------------------------------------------------------------------------- */

char *
docker_tar_read_longname (int fd, guint64 size)
{
    char *buf;
    guint64 padded;

    padded = (size + DOCKER_TAR_BLOCK_SIZE - 1) & ~(guint64) (DOCKER_TAR_BLOCK_SIZE - 1);

    if (size == 0 || size > 65536)
    {
        if (!docker_tar_skip (fd, padded))
            return NULL;
        return g_strdup ("");
    }

    buf = g_malloc0 (padded + 1);

    if (!docker_tar_read_full (fd, buf, (size_t) padded))
    {
        g_free (buf);
        return NULL;
    }

    buf[size] = '\0';
    return buf;
}
