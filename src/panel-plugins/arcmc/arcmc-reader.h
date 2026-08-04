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

#ifndef ARCMC_READER_H
#define ARCMC_READER_H

#include "arcmc-types.h"

struct archive;

typedef struct arcmc_archive_reader_ctx arcmc_archive_reader_ctx_t;

struct archive *arcmc_archive_reader_open (const arcmc_data_t *data,
                                           arcmc_archive_reader_ctx_t **ctx);
void arcmc_archive_reader_close (struct archive *archive, arcmc_archive_reader_ctx_t *ctx);

#endif /* ARCMC_READER_H */
