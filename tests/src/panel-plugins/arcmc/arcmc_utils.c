/*
   src/panel-plugins/arcmc - tests for archive manager utility functions

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

#define TEST_SUITE_NAME "/src/panel-plugins/arcmc"

#include "tests/mctest.h"

#include <string.h>

/* --------------------------------------------------------------------------------------------- */
/* Minimal type and data definitions copied from arcmc sources for isolated testing.              */
/* --------------------------------------------------------------------------------------------- */

/* Format IDs (from arcmc-types.h) */
enum
{
    ARCMC_FMT_ZIP = 0,
    ARCMC_FMT_7Z = 1,
    ARCMC_FMT_TAR_GZ = 2,
    ARCMC_FMT_TAR_BZ2 = 3,
    ARCMC_FMT_TAR_XZ = 4,
    ARCMC_FMT_TAR = 5,
    ARCMC_FMT_CPIO = 6,
};

/* External archiver descriptor (from arcmc-types.h) */
typedef struct
{
    const char *name;
    const char *ext;
    const char *pack_bin;
    const char *pack_args;
    const char *unpack_bin;
    const char *unpack_args;
    const char *test_bin;
    const char *test_args;
    const char *extfs_helper;
    const char *list_file_arg;
} arcmc_ext_archiver_t;

/* Copy of ext_archivers[] from archive-io.c */
static arcmc_ext_archiver_t ext_archivers[] = {
    { "RAR", ".rar", "rar", "a -r", "unrar", "x -o+", "unrar", "t", "urar", "@%s" },
    { "ARJ", ".arj", "arj", "a -r", "arj", "x -y", "arj", "t", "uarj", "!%s" },
    { "ACE", ".ace", NULL, NULL, "unace", "x -o", "unace", "t", "uace", NULL },
    { "ARC", ".arc", "arc", "a", "arc", "x", NULL, NULL, "uarc", NULL },
    { "ALZ", ".alz", NULL, NULL, "unalz", "", NULL, NULL, "ualz", NULL },
    { "ZOO", ".zoo", "zoo", "a", "zoo", "x", NULL, NULL, "uzoo", NULL },
    { "HA", ".ha", "ha", "a", "ha", "x", "ha", "t", "uha", NULL },
    { "WIM", ".wim", NULL, NULL, "wimlib-imagex", "extract", NULL, NULL, "uwim", NULL },
    { "LHA", ".lha", "lha", "a", "lha", "x", "lha", "t", "ulha", "@%s" },
    { "LZH", ".lzh", "lha", "a", "lha", "x", "lha", "t", "ulha", "@%s" },
    { "DEB", ".deb", NULL, NULL, "dpkg-deb", "-x", NULL, NULL, "deb", NULL },
    { "RPM", ".rpm", NULL, NULL, NULL, NULL, NULL, NULL, "rpm", NULL },
};

static const size_t ext_archivers_count = G_N_ELEMENTS (ext_archivers);

/* --------------------------------------------------------------------------------------------- */
/* Copied utility functions under test                                                           */
/* --------------------------------------------------------------------------------------------- */

/* ---- get_parent_dir (archive-io.c) ---- */

static char *
get_parent_dir (const char *current_dir)
{
    char *slash;

    if (current_dir == NULL || current_dir[0] == '\0')
        return g_strdup ("");

    slash = strrchr (current_dir, '/');
    if (slash == NULL)
        return g_strdup ("");

    return g_strndup (current_dir, (gsize) (slash - current_dir));
}

/* ---- build_child_path (archive-io.c) ---- */

static char *
build_child_path (const char *current_dir, const char *name)
{
    if (current_dir == NULL || current_dir[0] == '\0')
        return g_strdup (name);

    return g_strdup_printf ("%s/%s", current_dir, name);
}

/* ---- is_direct_child (archive-io.c) ---- */

static const char *
is_direct_child (const char *entry_path, const char *dir)
{
    size_t dir_len;
    const char *rest;

    if (dir == NULL || dir[0] == '\0')
    {
        /* root: direct child if no '/' in path */
        if (strchr (entry_path, '/') == NULL)
            return entry_path;
        return NULL;
    }

    dir_len = strlen (dir);

    if (strncmp (entry_path, dir, dir_len) != 0)
        return NULL;

    if (entry_path[dir_len] != '/')
        return NULL;

    rest = entry_path + dir_len + 1;

    /* must not contain further '/' (i.e., must be direct child) */
    if (rest[0] == '\0' || strchr (rest, '/') != NULL)
        return NULL;

    return rest;
}

/* ---- arcmc_is_supported_archive (arcmc.c) ---- */

static gboolean
arcmc_is_supported_archive (const char *filename)
{
    static const char *const exts[] = {
        ".tar.gz",  ".tgz",  ".tar.bz2", ".tbz2",     ".tar.xz", ".txz",
        ".tar.zst", ".tzst", ".tar.lz",  ".tar.lzma", ".tlz",    ".tar",
        ".zip",     ".7z",   ".cpio",    ".iso",      ".xar",    ".cab",
    };

    size_t flen, i;

    if (filename == NULL)
        return FALSE;

    flen = strlen (filename);

    for (i = 0; i < G_N_ELEMENTS (exts); i++)
    {
        size_t elen = strlen (exts[i]);

        if (flen >= elen && g_ascii_strcasecmp (filename + flen - elen, exts[i]) == 0)
            return TRUE;
    }

    /* also accept extensions handled by external archivers */
    for (i = 0; i < ext_archivers_count; i++)
    {
        size_t elen = strlen (ext_archivers[i].ext);

        if (flen >= elen && g_ascii_strcasecmp (filename + flen - elen, ext_archivers[i].ext) == 0)
            return TRUE;
    }

    return FALSE;
}

/* ---- arcmc_detect_fmt_id (archive-io.c, static) ---- */

static int
arcmc_detect_fmt_id (const char *filename)
{
    static const struct
    {
        const char *ext;
        int fmt;
    } map[] = {
        { ".tar.gz", ARCMC_FMT_TAR_GZ },   { ".tgz", ARCMC_FMT_TAR_GZ },
        { ".tar.bz2", ARCMC_FMT_TAR_BZ2 }, { ".tbz2", ARCMC_FMT_TAR_BZ2 },
        { ".tar.xz", ARCMC_FMT_TAR_XZ },   { ".txz", ARCMC_FMT_TAR_XZ },
        { ".tar", ARCMC_FMT_TAR },         { ".zip", ARCMC_FMT_ZIP },
        { ".7z", ARCMC_FMT_7Z },           { ".cpio", ARCMC_FMT_CPIO },
    };

    size_t flen, i;

    flen = strlen (filename);

    for (i = 0; i < G_N_ELEMENTS (map); i++)
    {
        size_t elen = strlen (map[i].ext);

        if (flen >= elen && g_ascii_strcasecmp (filename + flen - elen, map[i].ext) == 0)
            return map[i].fmt;
    }

    return -1;
}

/* ---- arcmc_find_ext_archiver (archive-io.c) ---- */

static const arcmc_ext_archiver_t *
arcmc_find_ext_archiver (const char *archive_path)
{
    const char *basename_ptr;
    size_t blen, i;

    if (archive_path == NULL)
        return NULL;

    basename_ptr = strrchr (archive_path, '/');
    if (basename_ptr != NULL)
        basename_ptr++;
    else
        basename_ptr = archive_path;

    blen = strlen (basename_ptr);

    for (i = 0; i < ext_archivers_count; i++)
    {
        size_t elen = strlen (ext_archivers[i].ext);

        if (blen >= elen
            && g_ascii_strcasecmp (basename_ptr + blen - elen, ext_archivers[i].ext) == 0)
            return &ext_archivers[i];
    }

    return NULL;
}

/* ---- arcmc_check_bin_available (archive-io.c) ---- */

static gboolean
arcmc_check_bin_available (const char *bin_name)
{
    char *full_path;

    if (bin_name == NULL || bin_name[0] == '\0')
        return FALSE;

    full_path = g_find_program_in_path (bin_name);
    if (full_path != NULL)
    {
        g_free (full_path);
        return TRUE;
    }

    return FALSE;
}

/* ======================================================================================= */
/*                                     TEST DATA                                           */
/* ======================================================================================= */

/* ---- get_parent_dir ---- */

/* @DataSource("test_get_parent_dir_ds") */
static const struct test_get_parent_dir_ds
{
    const char *input;
    const char *expected;
} test_get_parent_dir_ds[] = {
    { "dir/subdir", "dir" }, /* 0: simple parent */
    { "single", "" },        /* 1: no slash -> root */
    { "", "" },              /* 2: empty string */
    { NULL, "" },            /* 3: NULL */
    { "a/b/c", "a/b" },      /* 4: nested path */
};

/* ---- build_child_path ---- */

/* @DataSource("test_build_child_path_ds") */
static const struct test_build_child_path_ds
{
    const char *current_dir;
    const char *name;
    const char *expected;
} test_build_child_path_ds[] = {
    { "dir", "file", "dir/file" }, /* 0: simple join */
    { "", "file", "file" },        /* 1: empty dir */
    { NULL, "file", "file" },      /* 2: NULL dir */
    { "a/b", "c", "a/b/c" },       /* 3: nested join */
};

/* ---- is_direct_child ---- */

/* @DataSource("test_is_direct_child_ds") */
static const struct test_is_direct_child_ds
{
    const char *entry_path;
    const char *dir;
    const char *expected;
} test_is_direct_child_ds[] = {
    { "dir/file", "dir", "file" },   /* 0: direct child */
    { "dir/sub/file", "dir", NULL }, /* 1: nested - not direct */
    { "file", "", "file" },          /* 2: root dir (empty) */
    { "file", NULL, "file" },        /* 3: root dir (NULL) */
    { "other/file", "dir", NULL },   /* 4: wrong prefix */
};

/* ---- arcmc_is_supported_archive ---- */

/* @DataSource("test_is_supported_ds") */
static const struct test_is_supported_ds
{
    const char *filename;
    gboolean expected;
} test_is_supported_ds[] = {
    { "file.tar.gz", TRUE }, /* 0: tar.gz */
    { "file.zip", TRUE },    /* 1: zip */
    { "file.rar", TRUE },    /* 2: rar (ext archiver) */
    { "file.txt", FALSE },   /* 3: unsupported */
    { NULL, FALSE },         /* 4: NULL */
    { "FILE.ZIP", TRUE },    /* 5: case insensitive */
};

/* ---- arcmc_detect_fmt_id ---- */

/* @DataSource("test_detect_fmt_ds") */
static const struct test_detect_fmt_ds
{
    const char *filename;
    int expected;
} test_detect_fmt_ds[] = {
    { "f.tar.gz", ARCMC_FMT_TAR_GZ }, /* 0: tar.gz */
    { "f.tgz", ARCMC_FMT_TAR_GZ },    /* 1: tgz alias */
    { "f.zip", ARCMC_FMT_ZIP },       /* 2: zip */
    { "f.7z", ARCMC_FMT_7Z },         /* 3: 7z */
    { "f.txt", -1 },                  /* 4: unknown */
};

/* ---- arcmc_find_ext_archiver ---- */

/* @DataSource("test_find_ext_ds") */
static const struct test_find_ext_ds
{
    const char *archive_path;
    const char *expected_name;
} test_find_ext_ds[] = {
    { "a.rar", "RAR" },       /* 0: rar */
    { "/path/a.arj", "ARJ" }, /* 1: arj with path */
    { "a.zip", NULL },        /* 2: zip - not ext archiver */
    { NULL, NULL },           /* 3: NULL */
};

/* ---- arcmc_check_bin_available ---- */

/* @DataSource("test_check_bin_ds") */
static const struct test_check_bin_ds
{
    const char *bin_name;
    gboolean expected;
} test_check_bin_ds[] = {
    { "ls", TRUE },               /* 0: ls always exists */
    { "nonexistent_xyz", FALSE }, /* 1: nonexistent binary */
    { NULL, FALSE },              /* 2: NULL */
    { "", FALSE },                /* 3: empty string */
};

/* ======================================================================================= */
/*                                       TESTS                                             */
/* ======================================================================================= */

/* @Test(dataSource = "test_get_parent_dir_ds") */
START_PARAMETRIZED_TEST (test_get_parent_dir, test_get_parent_dir_ds)
{
    char *result;

    result = get_parent_dir (data->input);
    mctest_assert_str_eq (result, data->expected);

    g_free (result);
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test(dataSource = "test_build_child_path_ds") */
START_PARAMETRIZED_TEST (test_build_child_path, test_build_child_path_ds)
{
    char *result;

    result = build_child_path (data->current_dir, data->name);
    mctest_assert_str_eq (result, data->expected);

    g_free (result);
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test(dataSource = "test_is_direct_child_ds") */
START_PARAMETRIZED_TEST (test_is_direct_child, test_is_direct_child_ds)
{
    const char *result;

    result = is_direct_child (data->entry_path, data->dir);

    if (data->expected == NULL)
    {
        mctest_assert_null (result);
    }
    else
    {
        mctest_assert_str_eq (result, data->expected);
    }
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test(dataSource = "test_is_supported_ds") */
START_PARAMETRIZED_TEST (test_is_supported_archive, test_is_supported_ds)
{
    gboolean result;

    result = arcmc_is_supported_archive (data->filename);
    ck_assert_int_eq (result, data->expected);
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test(dataSource = "test_detect_fmt_ds") */
START_PARAMETRIZED_TEST (test_detect_fmt_id, test_detect_fmt_ds)
{
    int result;

    result = arcmc_detect_fmt_id (data->filename);
    ck_assert_int_eq (result, data->expected);
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test(dataSource = "test_find_ext_ds") */
START_PARAMETRIZED_TEST (test_find_ext_archiver, test_find_ext_ds)
{
    const arcmc_ext_archiver_t *result;

    result = arcmc_find_ext_archiver (data->archive_path);

    if (data->expected_name == NULL)
    {
        mctest_assert_null (result);
    }
    else
    {
        ck_assert_msg (result != NULL, "expected non-NULL archiver for '%s'", data->archive_path);
        mctest_assert_str_eq (result->name, data->expected_name);
    }
}
END_PARAMETRIZED_TEST

/* --------------------------------------------------------------------------------------------- */

/* @Test(dataSource = "test_check_bin_ds") */
START_PARAMETRIZED_TEST (test_check_bin_available, test_check_bin_ds)
{
    gboolean result;

    result = arcmc_check_bin_available (data->bin_name);
    ck_assert_int_eq (result, data->expected);
}
END_PARAMETRIZED_TEST

/* ======================================================================================= */

int
main (void)
{
    TCase *tc_core;

    tc_core = tcase_create ("Core");

    /* path utilities */
    mctest_add_parameterized_test (tc_core, test_get_parent_dir, test_get_parent_dir_ds);
    mctest_add_parameterized_test (tc_core, test_build_child_path, test_build_child_path_ds);
    mctest_add_parameterized_test (tc_core, test_is_direct_child, test_is_direct_child_ds);

    /* archive detection */
    mctest_add_parameterized_test (tc_core, test_is_supported_archive, test_is_supported_ds);

    /* format detection */
    mctest_add_parameterized_test (tc_core, test_detect_fmt_id, test_detect_fmt_ds);

    /* external archiver lookup */
    mctest_add_parameterized_test (tc_core, test_find_ext_archiver, test_find_ext_ds);

    /* binary availability */
    mctest_add_parameterized_test (tc_core, test_check_bin_available, test_check_bin_ds);

    return mctest_run_all (tc_core);
}

/* --------------------------------------------------------------------------------------------- */
