/*
   XML and HTML providers for mctree (both backed by libxml2).

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

#ifdef HAVE_MCTREE_LIBXML2
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#endif

#include "src/mctree/mctree-providers.h"

#ifdef HAVE_MCTREE_LIBXML2
/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_xml_probe (const unsigned char *data, gsize len)
{
    xmlDocPtr doc;

    doc = xmlReadMemory ((const char *) data, (int) len, NULL, NULL,
                         XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (doc == NULL)
        return FALSE;

    xmlFreeDoc (doc);
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_xml_node_name (const xmlNode *node)
{
    if (node->ns != NULL && node->ns->prefix != NULL)
        return g_strdup_printf ("%s:%s", (const char *) node->ns->prefix,
                                (const char *) node->name);

    return g_strdup ((const char *) node->name);
}

/* --------------------------------------------------------------------------------------------- */

static char *
mctree_xml_attr_name (const xmlAttr *attr)
{
    if (attr->ns != NULL && attr->ns->prefix != NULL)
        return g_strdup_printf ("%s:%s", (const char *) attr->ns->prefix,
                                (const char *) attr->name);

    return g_strdup ((const char *) attr->name);
}

/* --------------------------------------------------------------------------------------------- */

static void mctree_xml_add_node (mctree_model_t *model, mctree_node_t *parent, xmlNode *xml_node);

static void
mctree_xml_add_attributes (mctree_model_t *model, mctree_node_t *parent, xmlNode *xml_node)
{
    xmlAttr *attr;

    for (attr = xml_node->properties; attr != NULL; attr = attr->next)
    {
        xmlChar *value;
        char *name;

        name = mctree_xml_attr_name (attr);
        value = xmlNodeListGetString (xml_node->doc, attr->children, 1);
        mctree_model_add_node (model, parent, MCTREE_NODE_ATTRIBUTE, name,
                               value != NULL ? (const char *) value : "");
        xmlFree (value);
        g_free (name);
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_xml_add_children (mctree_model_t *model, mctree_node_t *parent, xmlNode *xml_node)
{
    xmlNode *child;

    for (child = xml_node->children; child != NULL; child = child->next)
    {
        if (child->type == XML_ELEMENT_NODE)
            mctree_xml_add_node (model, parent, child);
        else if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE)
        {
            xmlChar *content;
            char *stripped;

            if (xmlIsBlankNode (child))
                continue;

            content = xmlNodeGetContent (child);
            stripped = g_strstrip (g_strdup ((const char *) content));
            if (stripped[0] != '\0')
                mctree_model_add_node (model, parent, MCTREE_NODE_TEXT, NULL, stripped);
            g_free (stripped);
            xmlFree (content);
        }
    }
}

/* --------------------------------------------------------------------------------------------- */

static void
mctree_xml_add_node (mctree_model_t *model, mctree_node_t *parent, xmlNode *xml_node)
{
    char *name;
    mctree_node_t *node;

    name = mctree_xml_node_name (xml_node);
    node = mctree_model_add_node (model, parent, MCTREE_NODE_ELEMENT, name, NULL);
    g_free (name);

    mctree_xml_add_attributes (model, node, xml_node);
    mctree_xml_add_children (model, node, xml_node);
}

/* --------------------------------------------------------------------------------------------- */

static mctree_model_t *
mctree_xml_parse (const unsigned char *data, gsize len, const mctree_resolver_config_t *config,
                  GError **error)
{
    xmlDocPtr doc;
    xmlNode *root_node;
    mctree_model_t *model;
    mctree_node_t *root;

    (void) error;

    doc = xmlReadMemory ((const char *) data, (int) len, NULL, NULL,
                         XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (doc == NULL)
        return NULL;

    root_node = xmlDocGetRootElement (doc);
    if (root_node == NULL)
    {
        xmlFreeDoc (doc);
        return NULL;
    }

    model = mctree_model_new (config->scalar_preview_limit);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "XML", NULL);
    mctree_xml_add_node (model, root, root_node);

    xmlFreeDoc (doc);
    return model;
}

/* --------------------------------------------------------------------------------------------- */

/* The recovering HTML parser accepts almost any text, so the probe must not
   rely on it: only take files that announce themselves as HTML. */

static gboolean
mctree_html_sniff (const unsigned char *data, gsize len)
{
    gsize pos = 0;

    // skip UTF-8 BOM and leading whitespace
    if (len >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        pos = 3;
    while (pos < len
           && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\r' || data[pos] == '\n'))
        pos++;

    if (len - pos >= 14
        && g_ascii_strncasecmp ((const char *) data + pos, "<!doctype html", 14) == 0)
        return TRUE;

    return len - pos >= 5 && g_ascii_strncasecmp ((const char *) data + pos, "<html", 5) == 0;
}

/* --------------------------------------------------------------------------------------------- */

static htmlDocPtr
mctree_html_read (const unsigned char *data, gsize len)
{
    return htmlReadMemory ((const char *) data, (int) len, NULL, NULL,
                           HTML_PARSE_RECOVER | HTML_PARSE_NONET | HTML_PARSE_NOERROR
                               | HTML_PARSE_NOWARNING);
}

/* --------------------------------------------------------------------------------------------- */

static gboolean
mctree_html_probe (const unsigned char *data, gsize len)
{
    htmlDocPtr doc;
    gboolean ok;

    if (!mctree_html_sniff (data, len))
        return FALSE;

    doc = mctree_html_read (data, len);
    ok = doc != NULL && xmlDocGetRootElement (doc) != NULL;
    if (doc != NULL)
        xmlFreeDoc (doc);

    return ok;
}

/* --------------------------------------------------------------------------------------------- */

static mctree_model_t *
mctree_html_parse (const unsigned char *data, gsize len, const mctree_resolver_config_t *config,
                   GError **error)
{
    htmlDocPtr doc;
    xmlNode *root_node;
    mctree_model_t *model;
    mctree_node_t *root;

    (void) error;

    doc = mctree_html_read (data, len);
    if (doc == NULL)
        return NULL;

    root_node = xmlDocGetRootElement (doc);
    if (root_node == NULL)
    {
        xmlFreeDoc (doc);
        return NULL;
    }

    model = mctree_model_new (config->scalar_preview_limit);
    root = mctree_model_add_node (model, NULL, MCTREE_NODE_ROOT, "HTML", NULL);
    mctree_xml_add_node (model, root, root_node);

    xmlFreeDoc (doc);
    return model;
}
#endif

/*** public variables ****************************************************************************/

const mctree_provider_t mctree_xml_provider = {
    .content_type = MCTREE_CONTENT_XML,
    .name = "libxml2",
#ifdef HAVE_MCTREE_LIBXML2
    .state = MCTREE_PROVIDER_ENABLED,
    .probe = mctree_xml_probe,
    .parse = mctree_xml_parse,
#else
    .state = MCTREE_PROVIDER_MISSING,
    .probe = NULL,
    .parse = NULL,
#endif
};

const mctree_provider_t mctree_html_provider = {
    .content_type = MCTREE_CONTENT_HTML,
    .name = "libxml2-html",
#ifdef HAVE_MCTREE_LIBXML2
    .state = MCTREE_PROVIDER_ENABLED,
    .probe = mctree_html_probe,
    .parse = mctree_html_parse,
#else
    .state = MCTREE_PROVIDER_MISSING,
    .probe = NULL,
    .parse = NULL,
#endif
};
