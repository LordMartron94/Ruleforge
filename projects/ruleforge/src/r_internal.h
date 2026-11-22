#pragma once

#include <nexus.h>
#include <echo.h>

#include <logos.h>

#include "../ruleforge.h"

/* ---------------------------------------------------------------------------- */
/* RUNTIME                                                                      */
/* ---------------------------------------------------------------------------- */

struct RuleforgeRuntime {
  EchoRuntime  *echo;
  EchoSystem   *log_system;
  EchoContext  *log_context;
  LogosRuntime *logos;

  /* The compiled grammars (Ruleforge and the formats it reads), built on first use and kept for the runtime's lifetime. */
  LGenesisCompiler *grammar_compiler;
  LGenesisEngine   *grammar_engines[4];
};

/* ---------------------------------------------------------------------------- */
/* DIAGNOSTICS                                                                  */
/* ---------------------------------------------------------------------------- */

/* A source file of one compilation; the index in RuleforgeCompilation.files is its id in every span. */
typedef struct RuleforgeFile {
  char      *name;   /* path or logical name shown in diagnostics */
  char      *module; /* declared module name, or NULL when the header did not parse */
  byte      *text;   /* NUL-terminated copy */
  uint_large size;
  uint32     line_count;
  uint32    *line_starts; /* byte offset of every line */
  boolean    embedded;    /* part of the standard library */
} RuleforgeFile;

typedef struct RuleforgeSpan {
  uint32 file;
  uint32 start;
  uint32 end;
} RuleforgeSpan;

/* Summary: builds a span; trivial and header-defined per CONVENTIONS.md § Inlining. */
static NEXUS_MAYBE_UNUSED RuleforgeSpan ruleforge_span_make(uint32 file, uint32 start, uint32 end) {
  RuleforgeSpan span;

  span.file  = file;
  span.start = start;
  span.end   = end;

  return span;
}

typedef struct RuleforgeDiagnostic {
  RuleforgeDiagnosticSeverity severity;
  const char                 *code;
  char                       *message;
  RuleforgeSpan               span;
  char                       *note; /* optional extra line, or NULL */
} RuleforgeDiagnostic;

/* ---------------------------------------------------------------------------- */
/* ABSTRACT SYNTAX TREE                                                         */
/* ---------------------------------------------------------------------------- */

/*
The abstract syntax tree is one node type with a kind, so the JSON interchange form, the checker and the evaluator all
walk the same structure. The child layout of every kind is documented next to its enumerator.
*/
typedef enum RuleforgeNodeKind {
  RNK_MODULE,          /* text = module name; children: RNK_VERSION, RNK_IMPORT*, declarations */
  RNK_VERSION,         /* text = "1.0.0" */
  RNK_IMPORT,          /* text = qualified module name */
  RNK_SCHEMA,          /* text = name; flag EXPORT; children: RNK_FIELD* */
  RNK_FIELD,           /* text = name; child: RNK_TYPE */
  RNK_DATASET,         /* text = name; flag EXPORT; children: RNK_TYPE (the schema), RNK_STRING (the logical source) */
  RNK_THEME_SCHEMA,    /* text = name; flag EXPORT; children: RNK_KEY* */
  RNK_KEY,             /* text = style key */
  RNK_COLOR_SET,       /* text = name; flag EXPORT; children: RNK_COLOR_DEF* */
  RNK_COLOR_DEF,       /* text = name; child: expression */
  RNK_THEME,           /* text = name; flag EXPORT; children: [RNK_EXTENDS] [RNK_IMPLEMENTS] RNK_THEME_STYLE* */
  RNK_EXTENDS,         /* text = qualified name */
  RNK_IMPLEMENTS,      /* text = qualified name */
  RNK_THEME_STYLE,     /* text = key; flag OVERRIDE; children: RNK_THEME_STATE* */
  RNK_THEME_STATE,     /* text = "show" or "hide"; children: RNK_STYLE_PROPERTY* */
  RNK_STYLE_PROPERTY,  /* text = property; children: expression+ [RNK_ALPHA_OVERRIDE] */
  RNK_ALPHA_OVERRIDE,  /* integer = alpha 0..255 */
  RNK_TEMPLATE,        /* text = name; flag EXPORT; children: RNK_PARAMS, rule body items */
  RNK_FUNCTION,        /* text = name; flag EXPORT; children: RNK_PARAMS, [RNK_TYPE return], RNK_BLOCK */
  RNK_RULESET,         /* text = name; flag EXPORT; children: [RNK_DESCRIPTION], [RNK_CORE_CONDITIONS], statements */
  RNK_DESCRIPTION,     /* text = description */
  RNK_CORE_CONDITIONS, /* children: expression* (game conditions added to every rule of the ruleset) */
  RNK_CONFIGURATION,   /* children: RNK_CONFIG_ASSIGN*, RNK_ADAPTER* */
  RNK_CONFIG_ASSIGN,   /* text = key; child: expression */
  RNK_ADAPTER,         /* text = name; child[0]: RNK_TYPE (target schema), then RNK_ADAPTER_MAP* */
  RNK_ADAPTER_MAP,     /* text = target field; text2 = source field */
  RNK_PARAMS,          /* children: RNK_PARAM* */
  RNK_PARAM,           /* text = name; child: RNK_TYPE */
  RNK_TYPE,            /* text = base name ("int", "list", "dataset", "a.b"); flag OPTIONAL; child: element RNK_TYPE for list/dataset */
  RNK_BLOCK,           /* children: statements */
  RNK_LET,             /* text = name; flag MUTABLE for var; children: [RNK_TYPE], expression */
  RNK_ASSIGN,          /* text = name; child: expression */
  RNK_IF,              /* children: condition, RNK_BLOCK, [RNK_BLOCK or RNK_IF] */
  RNK_FOR,             /* text = variable; children: iterable, [RNK_WHERE], RNK_BLOCK */
  RNK_WHERE,           /* child: expression */
  RNK_RETURN,          /* child: [expression] */
  RNK_INCLUDE,         /* children: target (RNK_CALL or RNK_REF), [RNK_WITH] */
  RNK_WITH,            /* children: RNK_WITH_BINDING* */
  RNK_WITH_BINDING,    /* text = name; child: expression */
  RNK_EMIT,            /* child: RNK_RULE */
  RNK_RULE,            /* text = name or NULL; either one RNK_CALL child (template use) or rule body items */
  RNK_NATIVE,          /* text = native filter text */
  RNK_SCOPE,           /* children: RNK_SCOPE_ASSIGN* */
  RNK_SCOPE_ASSIGN,    /* text = condition name; child: expression */
  RNK_BASELINE,        /* children: commands */
  RNK_VARIANTS,        /* children: RNK_WHEN* */
  RNK_WHEN,            /* children: condition expression, commands */
  RNK_ACTION,          /* text = "show" or "hide" */
  RNK_STYLE,           /* child: expression */
  RNK_INHERIT,         /* text = "action" */
  RNK_INT,             /* integer */
  RNK_FLOAT,           /* real */
  RNK_STRING,          /* text = unescaped value */
  RNK_BOOL,            /* integer = 0 or 1 */
  RNK_NONE,
  RNK_COLOR,  /* integer = 0xRRGGBBAA, flag ALPHA when an alpha was written */
  RNK_LIST,   /* children: RNK_ARG* */
  RNK_CALL,   /* children: RNK_REF (the callee), RNK_ARG* */
  RNK_ARG,    /* text = argument name or NULL; child: expression */
  RNK_REF,    /* text = dotted name; integer = RNK_REF_PARAMETER / RNK_REF_COMPILER / RNK_REF_PLAIN */
  RNK_INDEX,  /* children: base, index */
  RNK_UNARY,  /* text = "not" or "-"; child: operand */
  RNK_BINARY, /* text = operator; children: left, right */
  RNK_COUNT
} RuleforgeNodeKind;

#define RNF_EXPORT   1U
#define RNF_OVERRIDE 2U
#define RNF_OPTIONAL 4U
#define RNF_MUTABLE  8U
#define RNF_ALPHA    16U

#define RNK_REF_PLAIN     0
#define RNK_REF_PARAMETER 1
#define RNK_REF_COMPILER  2

typedef struct RuleforgeNode RuleforgeNode;

struct RuleforgeNode {
  RuleforgeNodeKind kind;
  RuleforgeSpan     span;
  uint32            flags;
  const char       *text;
  const char       *text2;
  int64             integer;
  f_real            real;
  uint32            child_count;
  RuleforgeNode   **children;
};

typedef struct RuleforgeModule RuleforgeModule;

/* ---------------------------------------------------------------------------- */
/* VALUES                                                                       */
/* ---------------------------------------------------------------------------- */

typedef enum RuleforgeValueKind {
  RVK_NONE,
  RVK_INT,
  RVK_FLOAT,
  RVK_BOOL,
  RVK_STRING,
  RVK_COLOR,
  RVK_STYLE,
  RVK_RULESET,
  RVK_LIST,
  RVK_DATASET,
  RVK_RECORD,
  RVK_THEME,
  RVK_MODULE,
  RVK_COUNT
} RuleforgeValueKind;

typedef struct RuleforgeList    RuleforgeList;
typedef struct RuleforgeRecord  RuleforgeRecord;
typedef struct RuleforgeStyle   RuleforgeStyle;
typedef struct RuleforgeRuleset RuleforgeRuleset;
typedef struct RuleforgeTheme   RuleforgeTheme;
typedef struct RuleforgeDataset RuleforgeDataset;

/* A compile-time value. Small and passed by value; everything it points to lives in the compilation's arena. */
typedef struct RuleforgeValue {
  RuleforgeValueKind kind;
  boolean            is_identifier; /* RVK_STRING from an unquoted enumerator such as Red or Circle */

  union {
    int64             integer;
    f_real            real;
    boolean           flag;
    const char       *string;
    uint32            color; /* 0xRRGGBBAA */
    RuleforgeList    *list;
    RuleforgeDataset *dataset;
    RuleforgeRecord  *record;
    RuleforgeStyle   *style;
    RuleforgeRuleset *ruleset;
    RuleforgeTheme   *theme;
    void             *module;
  } as;
} RuleforgeValue;

struct RuleforgeList {
  uint32          count;
  RuleforgeValue *items;
};

/* A record: a schema instance from a dataset, or a computed row from a standard library function. */
struct RuleforgeRecord {
  uint32          count;
  const char    **names;
  RuleforgeValue *values;
  uint32          dataset; /* index into the compilation's datasets, or 0xFFFFFFFF */
  uint32          row;     /* the row of that dataset, 0-based */
};

struct RuleforgeDataset {
  const char       *name;   /* the declared name */
  const char       *source; /* the logical source it reads, e.g. "economy.currency" */
  const char       *schema;
  uint32            count;
  RuleforgeRecord **rows;
  uint64            hash;  /* of the normalized snapshot */
  uint32            index; /* position in the compilation's dataset list */
};

/* ---------------------------------------------------------------------------- */
/* STYLES, CONDITIONS AND RULES                                                 */
/* ---------------------------------------------------------------------------- */

/* One style property as the game writes it, resolved and validated against the manifest. */
typedef struct RuleforgeStyleProperty {
  const char *name;    /* the Ruleforge name, e.g. Background */
  const char *keyword; /* the filter keyword, e.g. SetBackgroundColor */
  char       *text;    /* the arguments as the filter writes them, e.g. "255 215 0" */
} RuleforgeStyleProperty;

struct RuleforgeStyle {
  const char             *theme; /* the theme the style came from, for the trace */
  const char             *key;
  uint32                  show_count;
  RuleforgeStyleProperty *show;
  uint32                  hide_count;
  RuleforgeStyleProperty *hide;
};

typedef enum RuleforgeOperator {
  ROP_EQ,
  ROP_NE,
  ROP_LT,
  ROP_LE,
  ROP_GT,
  ROP_GE
} RuleforgeOperator;

typedef enum RuleforgeConditionType {
  RCT_INTEGER,
  RCT_STRING,
  RCT_BOOLEAN,
  RCT_RARITY,
  RCT_COUNT
} RuleforgeConditionType;

/* A game-time condition the manifest knows: a filter keyword with a value type. */
typedef struct RuleforgeConditionInfo {
  const char            *name;    /* Ruleforge name, camelCase */
  const char            *keyword; /* filter keyword */
  RuleforgeConditionType type;
  boolean                ordered; /* supports < <= > >= */
} RuleforgeConditionInfo;

/* One condition of a block: keyword operator values. Integer and rarity values are in `numbers`, strings in `strings`. */
typedef struct RuleforgeCondition {
  const RuleforgeConditionInfo *info;
  RuleforgeOperator             op;
  uint32                        count;
  int64                        *numbers;
  const char                  **strings;
} RuleforgeCondition;

/* A conjunction of conditions. */
typedef struct RuleforgeClause {
  uint32               count;
  RuleforgeCondition **conditions;
} RuleforgeClause;

/* Where a generated rule came from: a chain of provenance records, newest first. */
typedef enum RuleforgeProvenanceKind {
  RPK_DECLARATION, /* the rule's own declaration */
  RPK_INCLUDE,
  RPK_CALL,
  RPK_ROW,
  RPK_TEMPLATE,
  RPK_TRANSFORM,
  RPK_COUNT
} RuleforgeProvenanceKind;

typedef struct RuleforgeProvenance RuleforgeProvenance;

struct RuleforgeProvenance {
  RuleforgeProvenanceKind    kind;
  RuleforgeSpan              span;
  const char                *label;  /* function, ruleset, template, transformation or dataset name */
  RuleforgeRecord           *record; /* RPK_ROW */
  const RuleforgeProvenance *parent;
};

typedef struct RuleforgeVariant {
  RuleforgeSpan    span;
  boolean          terminal;     /* its compile-time condition was true: nothing after it can match */
  uint32           clause_count; /* the game-time condition in disjunctive normal form; 0 clauses with terminal means always */
  RuleforgeClause *clauses;
  boolean          has_action;
  boolean          inherit_action;
  boolean          show;
  RuleforgeStyle  *style; /* NULL inherits the baseline's */
} RuleforgeVariant;

/* An immutable generated rule: scope, baseline and variants, with its provenance. */
typedef struct RuleforgeRule {
  const char                *name;
  RuleforgeSpan              span;
  RuleforgeClause            scope;
  boolean                    has_action;
  boolean                    show;
  RuleforgeStyle            *style;
  uint32                     variant_count;
  RuleforgeVariant          *variants;
  const char                *native; /* a raw filter block instead of a structured rule, or NULL */
  const RuleforgeProvenance *provenance;
  uint32                     shadowed; /* variants and baseline dropped because an earlier variant always matched */
} RuleforgeRule;

struct RuleforgeRuleset {
  const char     *name;
  uint32          count;
  uint32          capacity;
  RuleforgeRule **rules;
};

/* A lowered filter block: what the game evaluates, first match wins. */
typedef struct RuleforgeBlock {
  uint32                id;
  boolean               show;
  boolean               has_continue;
  uint32                condition_count;
  RuleforgeCondition  **conditions;
  const RuleforgeStyle *style;
  const char           *native;
  const RuleforgeRule  *rule;
  const char           *role;       /* "baseline", "variant 2" ... */
  uint32                first_line; /* in the emitted filter, 1-based */
  uint32                last_line;
} RuleforgeBlock;

/* ---------------------------------------------------------------------------- */
/* MANIFEST (r_manifest.c)                                                      */
/* ---------------------------------------------------------------------------- */

extern const RuleforgeConditionInfo *ruleforge_manifest_condition_find(const char *name);
extern uint32                        ruleforge_manifest_condition_count_get(void);
extern const RuleforgeConditionInfo *ruleforge_manifest_condition_at(uint32 index);
extern int32                         ruleforge_manifest_rarity_parse(const char *text);
extern const char                   *ruleforge_manifest_rarity_name_get(int64 rarity);
extern const char                   *ruleforge_manifest_operator_text_get(RuleforgeOperator op);
extern boolean                       ruleforge_manifest_property_lower(RuleforgeCompilation *compilation, RuleforgeSpan span, const char *name,
                                                                       const RuleforgeValue *arguments, uint32 count, int32 alpha_override,
                                                                       RuleforgeStyleProperty *out_property);

/* ---------------------------------------------------------------------------- */
/* VALUE HELPERS                                                                */
/* ---------------------------------------------------------------------------- */

static NEXUS_MAYBE_UNUSED RuleforgeValue ruleforge_value_none(void) {
  RuleforgeValue value;

  value.kind          = RVK_NONE;
  value.is_identifier = FALSE;
  value.as.integer    = 0;

  return value;
}

static NEXUS_MAYBE_UNUSED RuleforgeValue ruleforge_value_integer(int64 integer) {
  RuleforgeValue value;

  value.kind          = RVK_INT;
  value.is_identifier = FALSE;
  value.as.integer    = integer;

  return value;
}

static NEXUS_MAYBE_UNUSED RuleforgeValue ruleforge_value_real(f_real real) {
  RuleforgeValue value;

  value.kind          = RVK_FLOAT;
  value.is_identifier = FALSE;
  value.as.real       = real;

  return value;
}

static NEXUS_MAYBE_UNUSED RuleforgeValue ruleforge_value_flag(boolean flag) {
  RuleforgeValue value;

  value.kind          = RVK_BOOL;
  value.is_identifier = FALSE;
  value.as.flag       = flag;

  return value;
}

static NEXUS_MAYBE_UNUSED RuleforgeValue ruleforge_value_string(const char *string) {
  RuleforgeValue value;

  value.kind          = RVK_STRING;
  value.is_identifier = FALSE;
  value.as.string     = string;

  return value;
}

static NEXUS_MAYBE_UNUSED RuleforgeValue ruleforge_value_color(uint32 color) {
  RuleforgeValue value;

  value.kind          = RVK_COLOR;
  value.is_identifier = FALSE;
  value.as.color      = color;

  return value;
}

static NEXUS_MAYBE_UNUSED RuleforgeValue ruleforge_value_list(RuleforgeList *list) {
  RuleforgeValue value;

  value.kind          = RVK_LIST;
  value.is_identifier = FALSE;
  value.as.list       = list;

  return value;
}

static NEXUS_MAYBE_UNUSED RuleforgeValue ruleforge_value_ruleset(RuleforgeRuleset *ruleset) {
  RuleforgeValue value;

  value.kind          = RVK_RULESET;
  value.is_identifier = FALSE;
  value.as.ruleset    = ruleset;

  return value;
}

/* r_value.c */
extern const char           *ruleforge_value_type_name_get(RuleforgeValueKind kind);
extern boolean               ruleforge_value_equals(const RuleforgeValue *left, const RuleforgeValue *right);
extern boolean               ruleforge_value_number_get(const RuleforgeValue *value, f_real *out_number);
extern void                  ruleforge_value_text_write(const RuleforgeValue *value, NexusStringBuilder *out);
extern void                  ruleforge_value_json_write(const RuleforgeValue *value, NexusStringBuilder *out);
extern RuleforgeList        *ruleforge_value_list_create(RuleforgeCompilation *compilation, uint32 count);
extern RuleforgeRecord      *ruleforge_value_record_create(RuleforgeCompilation *compilation, uint32 count);
extern const RuleforgeValue *ruleforge_value_record_field_get(const RuleforgeRecord *record, const char *name);
extern void                  ruleforge_value_ruleset_rule_add(RuleforgeCompilation *compilation, RuleforgeRuleset *ruleset, RuleforgeRule *rule);
extern RuleforgeRuleset     *ruleforge_value_ruleset_create(RuleforgeCompilation *compilation, const char *name);

/* ---------------------------------------------------------------------------- */
/* SETTINGS                                                                     */
/* ---------------------------------------------------------------------------- */

typedef struct RuleforgeSettingValue {
  char          *key;
  RuleforgeValue value;
} RuleforgeSettingValue;

typedef struct RuleforgeSettingDataset {
  char *source;
  char *provider; /* "csv" or "json" */
  char *path;
  char *root; /* json: the member holding the array, or empty */
  char *delimiter;
} RuleforgeSettingDataset;

struct RuleforgeSettings {
  NexusDataArena arena;
  NexusDataList *values;   /* RuleforgeSettingValue * */
  NexusDataList *datasets; /* RuleforgeSettingDataset * */
  char          *filter;   /* the output name */
  uint64         step_limit;
  uint32         rule_limit;
  uint32         depth_limit;
};

/* ---------------------------------------------------------------------------- */
/* EVALUATION                                                                   */
/* ---------------------------------------------------------------------------- */

typedef struct RuleforgeVariable {
  const char    *name;
  RuleforgeValue value;
  boolean        mutable_flag;
  boolean        parameter;
} RuleforgeVariable;

typedef struct RuleforgeNamespace {
  RuleforgeModule     *module; /* a module's exports, or NULL */
  const RuleforgeNode *node;   /* a color set, or NULL */
  RuleforgeModule     *owner;  /* the module that declares node */
} RuleforgeNamespace;

typedef struct RuleforgeEvaluation REval;

struct RuleforgeEvaluation {
  RuleforgeCompilation      *compilation;
  const RuleforgeSettings   *settings;
  RuleforgeModule           *module;
  NexusDataList             *variables; /* RuleforgeVariable, used as a stack */
  uint32                     frame_base;
  uint64                     steps;
  uint32                     depth;
  uint32                     emitted;
  const RuleforgeProvenance *provenance;
  RuleforgeRuleset          *emit;
  NexusDataList             *cores; /* RuleforgeCondition * pushed by coreConditions */
  RuleforgeTheme            *default_theme;
  uint32                     override_depth;
  boolean                    enum_fallback;
  RuleforgeModule           *entry;
  boolean                    failed;
  boolean                    returning;
  RuleforgeValue             result;
  NexusDataList             *cache_keys;   /* const RuleforgeNode * */
  NexusDataList             *cache_values; /* RuleforgeValue * */
  NexusDataList             *datasets;     /* RuleforgeDataset * in load order */
};

/* r_eval.c */
extern void                       ruleforge_eval_initialize(REval *eval, RuleforgeCompilation *compilation, const RuleforgeSettings *settings);
extern void                       ruleforge_eval_release(REval *eval);
extern RuleforgeValue             ruleforge_eval_expression(REval *eval, const RuleforgeNode *node);
extern boolean                    ruleforge_eval_main_run(REval *eval, RuleforgeRuleset **out_ruleset);
extern void                       ruleforge_eval_fail(REval *eval, const char *code, RuleforgeSpan span, const char *note, const char *format, ...);
extern const RuleforgeNode       *ruleforge_eval_symbol_find(REval *eval, RuleforgeModule *module, const char *name, RuleforgeModule **out_owner);
extern boolean                    ruleforge_eval_reference_resolve(REval *eval, const RuleforgeNode *node, RuleforgeValue *out_value);
extern boolean                    ruleforge_eval_boolean_get(REval *eval, const RuleforgeNode *node, boolean *out_flag);
extern RuleforgeStyle            *ruleforge_eval_style_get(REval *eval, const RuleforgeNode *node);
extern const RuleforgeProvenance *ruleforge_eval_provenance_push(REval *eval, RuleforgeProvenanceKind kind, RuleforgeSpan span, const char *label,
                                                                 RuleforgeRecord *record);
extern boolean                    ruleforge_eval_variable_find(REval *eval, const char *name, boolean parameter, RuleforgeValue *out_value);
extern RuleforgeCondition *ruleforge_eval_condition_create(REval *eval, const RuleforgeConditionInfo *info, RuleforgeOperator op, uint32 count);

/* r_condition.c */
typedef struct RuleforgeDnf {
  boolean          constant;
  boolean          value; /* when constant */
  uint32           count;
  RuleforgeClause *clauses;
} RuleforgeDnf;

extern boolean ruleforge_condition_evaluate(REval *eval, const RuleforgeNode *node, boolean negate, RuleforgeDnf *out);
extern boolean ruleforge_condition_scope_build(REval *eval, const RuleforgeNode *assign, RuleforgeCondition **out_condition);
extern boolean ruleforge_condition_game_name_is(REval *eval, const RuleforgeNode *node);

/* r_json.c: a JSON reader that keeps fractions, which Nexus's does not. */
typedef enum RuleforgeJsonKind {
  RJK_NULL,
  RJK_BOOL,
  RJK_NUMBER,
  RJK_STRING,
  RJK_ARRAY,
  RJK_OBJECT
} RuleforgeJsonKind;

typedef struct RuleforgeJson RuleforgeJson;

struct RuleforgeJson {
  RuleforgeJsonKind kind;
  boolean           flag;
  boolean           integral; /* a number written without fraction or exponent */
  int64             integer;
  f_real            real;
  const char       *text;
  uint32            count;
  RuleforgeJson   **items;
  const char      **keys;
};

extern RuleforgeJson       *ruleforge_json_parse(RuleforgeRuntime *runtime, NexusDataArena *arena, const char *text, uint_large size, char *error,
                                                 uint_large error_size);
extern const RuleforgeJson *ruleforge_json_member_get(const RuleforgeJson *object, const char *key);

/* r_csv.c: records of cells, read through the Logos CSV grammar. */
typedef struct RuleforgeCsv {
  uint32        row_count;
  uint32       *column_counts; /* per row */
  const char ***cells;         /* [row][column] */
} RuleforgeCsv;

extern RuleforgeCsv *ruleforge_csv_parse(RuleforgeRuntime *runtime, NexusDataArena *arena, const char *text, uint_large size, char *error,
                                         uint_large error_size);

/* r_toml.c: scalar entries of a settings file, read through the TOML grammar. */
typedef struct RuleforgeTomlEntry {
  uint32             path_count; /* table segments, then key segments */
  const char       **path;
  RuleforgeValueKind kind; /* RVK_STRING, RVK_INT, RVK_FLOAT or RVK_BOOL */
  const char        *text; /* the decoded string, or the number/boolean as written */
} RuleforgeTomlEntry;

typedef struct RuleforgeToml {
  uint32               count;
  RuleforgeTomlEntry **entries;
} RuleforgeToml;

extern RuleforgeToml *ruleforge_toml_parse(RuleforgeRuntime *runtime, NexusDataArena *arena, const char *text, uint_large size, char *error,
                                           uint_large error_size);

/* r_dataset.c */
extern RuleforgeDataset *ruleforge_dataset_load(REval *eval, const RuleforgeNode *declaration, RuleforgeModule *owner);

/* r_theme.c */
extern RuleforgeTheme *ruleforge_theme_resolve(REval *eval, const RuleforgeNode *declaration, RuleforgeModule *owner);
extern RuleforgeStyle *ruleforge_theme_style_get(const RuleforgeTheme *theme, const char *key);
struct RuleforgeTheme {
  const char      *name;
  const char      *schema;
  uint32           count;
  const char     **keys;
  RuleforgeStyle **styles;
};

/* r_stdlib.c */
extern void ruleforge_stdlib_json_write(NexusStringBuilder *out);
typedef RuleforgeValue(RuleforgeNativeFunction)(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count);

typedef struct RuleforgeNative {
  const char              *module;
  const char              *name;
  const char              *parameters; /* space-separated parameter names, e.g. "data field count" */
  uint32                   minimum;    /* the number of required parameters */
  RuleforgeNativeFunction *function;
} RuleforgeNative;

extern const RuleforgeNative *ruleforge_stdlib_native_find(const char *module, const char *name);
extern boolean                ruleforge_stdlib_module_exists(const char *module);

/* r_filter.c: lowering, text emission and the model. */
typedef struct RuleforgeFilter {
  uint32           count;
  RuleforgeBlock **blocks;
} RuleforgeFilter;

extern RuleforgeFilter *ruleforge_filter_lower(RuleforgeCompilation *compilation, const RuleforgeRuleset *ruleset);
extern char            *ruleforge_filter_text_write(RuleforgeCompilation *compilation, RuleforgeFilter *filter, const char *name, uint64 hash);
extern void             ruleforge_filter_model_write(const RuleforgeCompilation *compilation, NexusStringBuilder *out);
extern void             ruleforge_filter_condition_json_write(const RuleforgeCondition *condition, NexusStringBuilder *out);
extern void             ruleforge_filter_style_json_write(const RuleforgeStyle *style, boolean show, NexusStringBuilder *out);

extern boolean ruleforge_simulate_json_write(const RuleforgeCompilation *compilation, const char *items_json, NexusStringBuilder *out);

/* r_compilation.c (private parts) */
extern uint64 ruleforge_compilation_hash_get(const RuleforgeCompilation *compilation);

/* ---------------------------------------------------------------------------- */
/* COMPILATION                                                                  */
/* ---------------------------------------------------------------------------- */

/* A parsed module: its source file and syntax tree. */
struct RuleforgeModule {
  uint32         file;
  RuleforgeNode *ast;
};

/* One compilation: everything allocated while compiling lives in the arena and is released together. */
struct RuleforgeCompilation {
  RuleforgeRuntime *runtime;
  NexusDataArena    arena;

  NexusDataList *files;       /* RuleforgeFile * */
  NexusDataList *diagnostics; /* RuleforgeDiagnostic * */
  NexusDataList *search;      /* const char * source folders */
  NexusDataList *modules;     /* RuleforgeModule * in load order: imports before their importers */
  uint32         error_count;

  /* Products of compile (r_compilation.c). */
  RuleforgeRuleset        *result;
  RuleforgeFilter         *filter;
  char                    *filter_text;
  NexusDataList           *datasets; /* RuleforgeDataset * */
  const RuleforgeSettings *settings;
  uint64                   filter_hash; /* of the sources, snapshots and settings the filter came from */
};

/* ---------------------------------------------------------------------------- */
/* INTERNAL FUNCTIONS (one system per file; see CONVENTIONS.md § Naming)        */
/* ---------------------------------------------------------------------------- */

/* r_text.c: line/column and escaping helpers shared by the writers. */
extern void ruleforge_text_position_get(const RuleforgeFile *file, uint32 offset, uint32 *out_line, uint32 *out_column);

/* r_diagnostic.c */
extern void ruleforge_diagnostic_report(RuleforgeCompilation *compilation, RuleforgeDiagnosticSeverity severity, const char *code, RuleforgeSpan span,
                                        const char *note, const char *format, ...);

/* r_source.c */
extern uint32         ruleforge_source_add(RuleforgeCompilation *compilation, const char *name, const char *module, const byte *text, uint_large size,
                                           boolean embedded);
extern RuleforgeFile *ruleforge_source_get(const RuleforgeCompilation *compilation, uint32 file);
extern boolean        ruleforge_source_file_read(const char *path, byte **out_bytes, uint_large *out_size);

/* r_syntax.c */
typedef enum RuleforgeSyntaxKind {
  RSK_RULEFORGE,
  RSK_JSON,
  RSK_CSV,
  RSK_TOML,
  RSK_COUNT
} RuleforgeSyntaxKind;

extern LParserTree *ruleforge_syntax_parse(RuleforgeCompilation *compilation, uint32 file);
extern LParserTree *ruleforge_syntax_text_parse(RuleforgeRuntime *runtime, RuleforgeSyntaxKind kind, const byte *text, uint_large size, char *error,
                                                uint_large error_size);

/* r_ast.c */
extern RuleforgeNode *ruleforge_ast_build(RuleforgeCompilation *compilation, uint32 file, const LParserTree *tree);
extern const char    *ruleforge_ast_kind_name_get(RuleforgeNodeKind kind);
extern RuleforgeNode *ruleforge_ast_node_create(RuleforgeCompilation *compilation, RuleforgeNodeKind kind, RuleforgeSpan span);
extern void           ruleforge_ast_child_add(RuleforgeCompilation *compilation, RuleforgeNode *parent, RuleforgeNode *child);

/* r_compilation.c */
extern RuleforgeModule *ruleforge_compilation_module_find(const RuleforgeCompilation *compilation, const char *name);

/* r_ast_json.c */
extern void ruleforge_ast_json_write(const RuleforgeCompilation *compilation, const RuleforgeNode *root, NexusStringBuilder *out);
