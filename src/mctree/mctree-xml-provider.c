/*
   XML provider for mctree.

   Copyright (C) 2026
   Free Software Foundation, Inc.

   This file is part of the Midnight Commander.
 */

#include <config.h>

#ifdef HAVE_MCTREE_LIBXML2
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
#endif

/*** public variables ****************************************************************************/

const mctree_provider_t mctree_xml_provider = {
    .content_type = MCTREE_CONTENT_XML,
    .name = "libxml2",
#ifdef HAVE_MCTREE_LIBXML2
    .state = MCTREE_PROVIDER_ENABLED,
    .enabled = TRUE,
    .probe = mctree_xml_probe,
    .parse = mctree_xml_parse,
#else
    .state = MCTREE_PROVIDER_MISSING,
    .enabled = FALSE,
    .probe = NULL,
    .parse = NULL,
#endif
};
