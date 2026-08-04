/*
   Archive browser panel plugin -libarchive reader abstraction.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.
 */

#include <config.h>

#include <errno.h>

#include <archive.h>

#include "arcmc-reader.h"

/*** file scope macro definitions ****************************************************************/

#define ARCMC_READER_BUFFER_SIZE (32 * 1024)

/*** file scope type declarations ****************************************************************/

struct arcmc_archive_reader_ctx
{
    mc_pp_input_stream_t *stream;
    void *handle;
    char buffer[ARCMC_READER_BUFFER_SIZE];
};

/*** file scope functions ************************************************************************/

static void
arcmc_archive_reader_set_error (struct archive *archive, GError *error, const char *fallback)
{
    archive_set_error (archive, EIO, "%s", error != NULL ? error->message : fallback);
    g_clear_error (&error);
}

/* --------------------------------------------------------------------------------------------- */

static int
arcmc_archive_reader_stream_open (struct archive *archive, void *client_data)
{
    arcmc_archive_reader_ctx_t *ctx = (arcmc_archive_reader_ctx_t *) client_data;
    GError *error = NULL;

    if (ctx->stream == NULL || ctx->stream->ops == NULL || ctx->stream->ops->open == NULL
        || ctx->stream->ops->read == NULL || ctx->stream->ops->close == NULL
        || ctx->stream->ops->open (ctx->stream, &ctx->handle, &error) != MC_PPR_OK
        || ctx->handle == NULL)
    {
        arcmc_archive_reader_set_error (archive, error, "Cannot open archive input stream");
        return ARCHIVE_FATAL;
    }

    return ARCHIVE_OK;
}

/* --------------------------------------------------------------------------------------------- */

static la_ssize_t
arcmc_archive_reader_stream_read (struct archive *archive, void *client_data, const void **buffer)
{
    arcmc_archive_reader_ctx_t *ctx = (arcmc_archive_reader_ctx_t *) client_data;
    GError *error = NULL;
    gssize bytes;

    bytes = ctx->stream->ops->read (ctx->stream, ctx->handle, ctx->buffer, sizeof (ctx->buffer),
                                    &error);
    if (bytes < 0)
    {
        arcmc_archive_reader_set_error (archive, error, "Cannot read archive input stream");
        return -1;
    }

    *buffer = ctx->buffer;
    return (la_ssize_t) bytes;
}

/* --------------------------------------------------------------------------------------------- */

static la_int64_t
arcmc_archive_reader_stream_skip (struct archive *archive, void *client_data, la_int64_t request)
{
    arcmc_archive_reader_ctx_t *ctx = (arcmc_archive_reader_ctx_t *) client_data;
    la_int64_t skipped = 0;

    while (skipped < request)
    {
        GError *error = NULL;
        gsize want = (gsize) MIN ((la_int64_t) sizeof (ctx->buffer), request - skipped);
        gssize bytes = ctx->stream->ops->read (ctx->stream, ctx->handle, ctx->buffer, want, &error);

        if (bytes < 0)
        {
            arcmc_archive_reader_set_error (archive, error, "Cannot skip archive input stream");
            return -1;
        }
        if (bytes == 0)
            break;

        skipped += bytes;
    }

    return skipped;
}

/* --------------------------------------------------------------------------------------------- */

static int
arcmc_archive_reader_stream_close (struct archive *archive, void *client_data)
{
    arcmc_archive_reader_ctx_t *ctx = (arcmc_archive_reader_ctx_t *) client_data;

    (void) archive;

    if (ctx->handle != NULL)
    {
        ctx->stream->ops->close (ctx->stream, ctx->handle);
        ctx->handle = NULL;
    }

    return ARCHIVE_OK;
}

/* --------------------------------------------------------------------------------------------- */

struct archive *
arcmc_archive_reader_open (const arcmc_data_t *data, arcmc_archive_reader_ctx_t **ctx)
{
    struct archive *archive;
    arcmc_archive_reader_ctx_t *stream_ctx = NULL;
    int result;

    if (data == NULL || ctx == NULL)
        return NULL;

    *ctx = NULL;

    archive = archive_read_new ();
    archive_read_support_filter_all (archive);
    archive_read_support_format_all (archive);

    if (data->password != NULL)
        archive_read_add_passphrase (archive, data->password);

    if (data->input_stream == NULL)
        result = archive_read_open_filename (archive, data->archive_path, 10240);
    else
    {
        stream_ctx = g_new0 (arcmc_archive_reader_ctx_t, 1);
        stream_ctx->stream = data->input_stream;
        result = archive_read_open2 (
            archive, stream_ctx, arcmc_archive_reader_stream_open, arcmc_archive_reader_stream_read,
            arcmc_archive_reader_stream_skip, arcmc_archive_reader_stream_close);
    }

    if (result != ARCHIVE_OK)
    {
        archive_read_free (archive);
        g_free (stream_ctx);
        return NULL;
    }

    *ctx = stream_ctx;

    return archive;
}

/* --------------------------------------------------------------------------------------------- */

void
arcmc_archive_reader_close (struct archive *archive, arcmc_archive_reader_ctx_t *ctx)
{
    if (archive != NULL)
        archive_read_free (archive);
    g_free (ctx);
}
