/*
   Structured tree resolver and provider registry.

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

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#ifdef HAVE_MCTREE_MAGIC
#include <magic.h>
#endif

#include "src/mctree/mctree-providers.h"
#include "src/mctree/mctree-resolver.h"

/*** file scope macro definitions ****************************************************************/

#define MCTREE_DEFAULT_MAX_PARSE_SIZE (2 * 1024 * 1024)
/* Both the parsers and the model walkers recurse per nesting level, so the
 * depth must be capped independently of the file size. */
#define MCTREE_DEFAULT_MAX_DEPTH 200
/* Chained YAML aliases multiply nodes ("billion laughs"), so expansion is
 * capped by a node budget independent of the file size. */
#define MCTREE_DEFAULT_MAX_ALIAS_NODES 10000
#define MCTREE_DEFAULT_EXPAND_DEPTH    2

/*** file scope variables ************************************************************************/

static const mctree_provider_t *const mctree_providers[] = {
    &mctree_json_provider,
    &mctree_xml_provider,
    &mctree_yaml_provider,
};

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static mctree_content_type_t
mctree_type_from_extension (const char *path)
{
    char *lower;
    mctree_content_type_t type = MCTREE_CONTENT_UNKNOWN;

    if (path == NULL)
        return MCTREE_CONTENT_UNKNOWN;

    lower = g_ascii_strdown (path, -1);

    if (g_str_has_suffix (lower, ".json"))
        type = MCTREE_CONTENT_JSON;
    else if (g_str_has_suffix (lower, ".xml") || g_str_has_suffix (lower, ".xhtml"))
        type = MCTREE_CONTENT_XML;
    else if (g_str_has_suffix (lower, ".yaml") || g_str_has_suffix (lower, ".yml"))
        type = MCTREE_CONTENT_YAML;

    g_free (lower);
    return type;
}

/* --------------------------------------------------------------------------------------------- */

#ifdef HAVE_MCTREE_MAGIC
static mctree_content_type_t
mctree_type_from_magic (const char *path)
{
    magic_t cookie;
    const char *mime;
    mctree_content_type_t type = MCTREE_CONTENT_UNKNOWN;

    if (path == NULL)
        return MCTREE_CONTENT_UNKNOWN;

    cookie = magic_open (MAGIC_MIME_TYPE | MAGIC_ERROR);
    if (cookie == NULL)
        return MCTREE_CONTENT_UNKNOWN;

    if (magic_load (cookie, NULL) == 0)
    {
        mime = magic_file (cookie, path);
        if (mime != NULL)
        {
            if (strcmp (mime, "application/json") == 0 || strcmp (mime, "text/json") == 0)
                type = MCTREE_CONTENT_JSON;
            else if (strcmp (mime, "application/xml") == 0 || strcmp (mime, "text/xml") == 0
                     || strcmp (mime, "application/xhtml+xml") == 0)
                type = MCTREE_CONTENT_XML;
            else if (strcmp (mime, "application/x-yaml") == 0 || strcmp (mime, "text/x-yaml") == 0
                     || strcmp (mime, "application/yaml") == 0)
                type = MCTREE_CONTENT_YAML;
        }
    }

    magic_close (cookie);
    return type;
}
#endif

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_provider_available (const mctree_provider_t *provider)
{
    return provider != NULL && provider->state == MCTREE_PROVIDER_ENABLED && provider->probe != NULL
        && provider->parse != NULL;
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_try_provider (const mctree_provider_t *provider, const unsigned char *data, gsize len)
{
    if (!mctree_provider_available (provider))
        return FALSE;

    return provider->probe (data, len);
}

/* --------------------------------------------------------------------------------------------- */

static const mctree_provider_t *
mctree_select_provider (const char *path, const unsigned char *data, gsize len,
                        const mctree_resolver_config_t *config, mctree_content_type_t *type_hint)
{
    mctree_content_type_t extension_type = MCTREE_CONTENT_UNKNOWN;
    mctree_content_type_t magic_type = MCTREE_CONTENT_UNKNOWN;
    gsize i;

    if (config->use_extension)
        extension_type = mctree_type_from_extension (path);
#ifdef HAVE_MCTREE_MAGIC
    if (config->use_magic)
        magic_type = mctree_type_from_magic (path);
#else
    (void) path;
#endif

    if (extension_type != MCTREE_CONTENT_UNKNOWN)
    {
        const mctree_provider_t *provider;

        /* The extension states the user's intent: return the provider without
           probing, so parse errors surface as real diagnostics instead of a
           generic "no provider accepted the file". */
        provider = mctree_provider_for_type (extension_type);
        if (provider != NULL)
        {
            *type_hint = extension_type;
            return provider;
        }
    }

    if (magic_type != MCTREE_CONTENT_UNKNOWN)
    {
        const mctree_provider_t *provider;

        provider = mctree_provider_for_type (magic_type);
        if (mctree_try_provider (provider, data, len))
        {
            *type_hint = magic_type;
            return provider;
        }
        if (provider != NULL && provider->state != MCTREE_PROVIDER_ENABLED)
        {
            *type_hint = magic_type;
            return provider;
        }
    }

    if (!config->use_probe)
        return NULL;

    for (i = 0; i < G_N_ELEMENTS (mctree_providers); i++)
    {
        const mctree_provider_t *provider = mctree_providers[i];

        if (mctree_try_provider (provider, data, len))
        {
            *type_hint = provider->content_type;
            return provider;
        }
    }

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_read_file_limited (const char *path, gsize max_parse_size, gsize *len, gboolean *too_large,
                          GError **error)
{
    struct stat st;
    char *contents = NULL;

    *len = 0;
    *too_large = FALSE;

    if (stat (path, &st) != 0)
    {
        g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s: %s", path,
                     g_strerror (errno));
        return NULL;
    }

    if (!S_ISREG (st.st_mode))
    {
        g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL, _ ("%s is not a regular file"), path);
        return NULL;
    }

    if (st.st_size < 0 || (guint64) st.st_size > max_parse_size)
    {
        *too_large = TRUE;
        return NULL;
    }

    if (!g_file_get_contents (path, &contents, len, error))
        return NULL;

    return contents;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

void
mctree_resolver_config_init (mctree_resolver_config_t *config)
{
    if (config == NULL)
        return;

    config->use_extension = TRUE;
    config->use_magic = TRUE;
    config->use_probe = TRUE;
    config->max_parse_size = MCTREE_DEFAULT_MAX_PARSE_SIZE;
    config->max_depth = MCTREE_DEFAULT_MAX_DEPTH;
    config->max_alias_nodes = MCTREE_DEFAULT_MAX_ALIAS_NODES;
    config->default_expand_depth = MCTREE_DEFAULT_EXPAND_DEPTH;
    config->scalar_preview_limit = MCTREE_DEFAULT_SCALAR_PREVIEW_LIMIT;
}

/* --------------------------------------------------------------------------------------------- */

void
mctree_resolver_result_clear (mctree_resolver_result_t *result)
{
    if (result == NULL)
        return;

    g_free (result->diagnostic);
    memset (result, 0, sizeof (*result));
}

/* --------------------------------------------------------------------------------------------- */

const mctree_provider_t *
mctree_provider_for_type (mctree_content_type_t type)
{
    gsize i;

    for (i = 0; i < G_N_ELEMENTS (mctree_providers); i++)
        if (mctree_providers[i]->content_type == type)
            return mctree_providers[i];

    return NULL;
}

/* --------------------------------------------------------------------------------------------- */

const char *
mctree_content_type_name (mctree_content_type_t type)
{
    switch (type)
    {
    case MCTREE_CONTENT_JSON:
        return "JSON";
    case MCTREE_CONTENT_XML:
        return "XML";
    case MCTREE_CONTENT_YAML:
        return "YAML";
    case MCTREE_CONTENT_UNKNOWN:
    default:
        return "unknown";
    }
}

/* --------------------------------------------------------------------------------------------- */

mctree_model_t *
mctree_resolve_file (const char *path, const mctree_resolver_config_t *config_ptr,
                     mctree_resolver_result_t *result, GError **error)
{
    mctree_resolver_config_t local_config;
    mctree_content_type_t type_hint = MCTREE_CONTENT_UNKNOWN;
    const mctree_provider_t *provider;
    mctree_model_t *model;
    char *contents;
    gsize len = 0;
    gboolean too_large = FALSE;

    if (result != NULL)
        memset (result, 0, sizeof (*result));

    if (config_ptr == NULL)
    {
        mctree_resolver_config_init (&local_config);
        config_ptr = &local_config;
    }

    contents = mctree_read_file_limited (path, config_ptr->max_parse_size, &len, &too_large, error);
    if (contents == NULL)
    {
        if (result != NULL)
            result->too_large = too_large;
        return NULL;
    }

    provider = mctree_select_provider (path, (const unsigned char *) contents, len, config_ptr,
                                       &type_hint);

    if (result != NULL)
    {
        result->content_type = type_hint;
        result->provider = provider;
    }

    if (!mctree_provider_available (provider))
    {
        if (result != NULL)
            result->diagnostic =
                g_strdup (provider == NULL ? "No structured provider accepted the file"
                                           : "Structured provider is not available");
        g_free (contents);
        return NULL;
    }

    model = provider->parse ((const unsigned char *) contents, len, config_ptr, error);
    g_free (contents);

    if (model != NULL)
        mctree_model_expand_to_depth (model, config_ptr->default_expand_depth);

    return model;
}
