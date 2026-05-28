/** \file mongo_wizard_priv.h
 *  \brief MongoDB plugin: filter-builder wizard internals shared across the
 *         parser, JSON generator, and UI translation units.
 *
 *  Not a public interface; only the wizard's own mongo_wizard_*.c files include
 *  it. The rule list (s_rules) is the shared data model: the parser fills it,
 *  the JSON generator reads it, and the UI owns its lifetime.
 */

#ifndef MC_PANEL_MONGO_WIZARD_PRIV_H
#define MC_PANEL_MONGO_WIZARD_PRIV_H

#include <bson/bson.h>

#include "lib/global.h"

/*** typedefs(not structures) and defined constants *********************************************/

typedef enum
{
    WIZ_OP_EQ = 0,
    WIZ_OP_NE,
    WIZ_OP_GT,
    WIZ_OP_GTE,
    WIZ_OP_LT,
    WIZ_OP_LTE,
    WIZ_OP_IN,
    WIZ_OP_NIN,
    WIZ_OP_REGEX,
    WIZ_OP_LIKE,
    WIZ_OP_EXISTS,
    WIZ_OP_COUNT
} wiz_op_t;

typedef enum
{
    WIZ_AND = 0, /* chain with the next row inside one $and group */
    WIZ_OR,      /* close the group; the next row starts a new $or member */
    WIZ_END,     /* close the group (terminator); same grouping as OR */
    WIZ_LOGIC_COUNT
} wiz_logic_t;

/*** structures declarations (and typedefs of structures) ***************************************/

typedef struct
{
    char *field;
    wiz_op_t op;
    char *value;
    char *options;
    wiz_logic_t logic; /* how this row joins the next */
} wiz_rule_t;

/*** global variables defined in .c file ********************************************************/

/* The shared rule list (wiz_rule_t; invariant: len >= 1). Owned by the UI
   module (mongo_wizard.c), which allocates and frees it. */
extern GArray *s_rules;

/*** declarations of public functions ***********************************************************/

/* JSON layer (mongo_wizard_json.c). */
char *wiz_json_quote (const char *s);
gboolean wiz_is_number (const char *v);
gboolean wiz_has_edge_space (const char *s);
char *wiz_bson_value_json (bson_iter_t *it);
char *wiz_picker_scalar_json (const char *raw);
char *wiz_picker_value_text (const char *raw);
char *wiz_generate (void);

/* Parser layer (mongo_wizard_parse.c). */
int wiz_import_filter (const char *json);
int wiz_import_doc_fields (const char *json);

#endif
