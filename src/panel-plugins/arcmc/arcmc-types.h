/*
   Archive browser panel plugin -shared types and constants.

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

#ifndef ARCMC_TYPES_H
#define ARCMC_TYPES_H

#include "lib/global.h"
#include "lib/panel-plugin.h"
#include "lib/widget.h"

/*** typedefs (not langstruc strstruc)
 * *************************************************************/

typedef struct
{
    char *full_path; /* full path inside the archive */
    char *name;      /* basename for display */
    mode_t mode;     /* S_IFDIR | perms  or  S_IFREG | perms */
    off_t size;
    time_t mtime;
    gboolean is_virtual_dir; /* TRUE if synthesized from path components */
} arcmc_entry_t;

/* Format indices for arcmc_pack_opts_t.format */
enum
{
    ARCMC_FMT_ZIP = 0,
    ARCMC_FMT_7Z = 1,
    ARCMC_FMT_TAR_GZ = 2,
    ARCMC_FMT_TAR_BZ2 = 3,
    ARCMC_FMT_TAR_XZ = 4,
    ARCMC_FMT_TAR = 5,
    ARCMC_FMT_CPIO = 6,
    ARCMC_FMT_OTHER_COUNT = 5 /* number of "Other" formats (tar.gz .. cpio) */
};

#define ARCMC_FMT_COUNT    (ARCMC_FMT_CPIO + 1)
#define ARCMC_FMT_EXT_BASE 100 /* external archiver formats start here */

typedef struct
{
    char *archive_path;
    int format;      /* ARCMC_FMT_* */
    int compression; /* 0=store, 1=fastest, 2=normal, 3=maximum */
    char *password;
    gboolean encrypt_files;
    gboolean encrypt_header;
    gboolean store_paths;
    gboolean delete_after;
} arcmc_pack_opts_t;

/* Saved state for nested archive browsing (stack frame). */
typedef struct arcmc_nest_frame_t
{
    struct arcmc_nest_frame_t *prev;
    char *archive_path; /* path to the outer archive on disk */
    char *current_dir;  /* dir inside the outer archive where we were */
    char *password;
    char *extfs_helper;
    GPtrArray *all_entries;
    char *temp_file; /* extracted temp file (to unlink on pop), or NULL */
} arcmc_nest_frame_t;

typedef struct
{
    mc_panel_host_t *host;
    char *archive_path;             /* path to the archive on disk */
    char *current_dir;              /* current directory inside the archive ("" = root) */
    char *password;                 /* password (if encrypted), NULL otherwise */
    GPtrArray *all_entries;         /* flat list of all entries from the archive */
    char *title_buf;                /* buffer for get_title */
    char *extfs_helper;             /* full path to extfs helper, or NULL for libarchive mode */
    arcmc_nest_frame_t *nest_stack; /* stack of outer archives for nested browsing */
    GHashTable *bulk_cache;         /* bulk extract cache: filename -> local_path */
    char *bulk_temp_dir;            /* temp directory for bulk-extracted files */
} arcmc_data_t;

/* Progress dialog context for pack/extract operations */
typedef struct
{
    WDialog *dlg;
    WHLine *hline_top;      /* top separator -shows "{32%} title" */
    WLabel *lbl_archive;    /* archive path */
    WLabel *lbl_file;       /* current file path */
    WLabel *lbl_file_size;  /* "184 KB / 1.06 MB" */
    WGauge *gauge_file;     /* per-file progress */
    WLabel *lbl_total_size; /* "182 MB / 559 MB @ 17 MB/s -00:00:22" */
    WLabel *lbl_ratio;      /* "182 MB -> 9 MB = 5%" */
    WGauge *gauge_total;    /* total progress */

    /* tracking state */
    char *op_title;      /* operation title (e.g. "Creating archive...") */
    off_t total_bytes;   /* total bytes to process */
    off_t done_bytes;    /* bytes processed so far */
    off_t written_bytes; /* compressed bytes written */
    off_t file_bytes;    /* current file total size */
    off_t file_done;     /* current file bytes done */
    gint64 start_time;   /* g_get_monotonic_time() at start */
    int gauge_cols;      /* visual bar length in characters */
    int last_total_col;  /* last drawn column of total gauge */
    int last_file_col;   /* last drawn column of file gauge */
    gboolean aborted;    /* user pressed Abort/Esc */
    gboolean visible;    /* dlg_init() called */
} arcmc_progress_t;

/* External archiver tool descriptor */
typedef struct
{
    const char *name;          /* display name, e.g. "RAR" */
    const char *ext;           /* default extension, e.g. ".rar" */
    const char *pack_bin;      /* pack binary, e.g. "rar", NULL = no pack support */
    const char *pack_args;     /* pack arguments template, e.g. "a -r -o+" */
    const char *unpack_bin;    /* unpack binary, e.g. "unrar" */
    const char *unpack_args;   /* unpack arguments template, e.g. "x -o+" */
    const char *test_bin;      /* test binary, e.g. "unrar", NULL = no test */
    const char *test_args;     /* test arguments template, e.g. "t" */
    const char *extfs_helper;  /* extfs helper name for browsing, e.g. "urar" */
    const char *list_file_arg; /* file-list argument template, e.g. "@%s" for RAR/7z, NULL = not
                                  supported */
} arcmc_ext_archiver_t;

/* External archivers table */
extern arcmc_ext_archiver_t ext_archivers[]; /* mutable: config may override fields */
extern const size_t ext_archivers_count;

/*** declarations (functions)
 * **********************************************************************/

void arcmc_entry_free (gpointer p);

#endif /* ARCMC_TYPES_H */
