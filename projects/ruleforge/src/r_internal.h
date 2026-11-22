#pragma once

#include <nexus.h>
#include <echo.h>

#include <logos.h>

#include <rf_bindings.h>

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

typedef struct RuleforgeModule RuleforgeModule;

/* A source file of one compilation; the index in RuleforgeCompilation.files is its id in every span. */
typedef struct RuleforgeFile {
  char       *name;   /* path or logical name shown in diagnostics */
  const char *module; /* declared module name, or NULL when the header did not parse */
  byte       *text;   /* NUL-terminated copy */
  uint_large  size;
  uint32      line_count;
  uint32     *line_starts; /* byte offset of every line */
  boolean     embedded;    /* part of the standard library */
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

/*
A list is immutable, but collections.Concat can append to it in amortized constant time: lists made by Concat share one buffer with spare
room, `used` counts what any version has written, and a list whose count equals `used` is the newest version, so it may extend the buffer in
place; older versions still see exactly their own prefix. A script building a list in a loop therefore does not copy it at every step.
*/
struct RuleforgeList {
  uint32          count;
  RuleforgeValue *items;
  uint32          capacity; /* the room in the buffer behind items (0 when the list cannot be extended in place) */
  uint32         *used;     /* the shared count of items written to the buffer, or NULL */
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
  RCT_ENUM,
  RCT_COUNT
} RuleforgeConditionType;

/* A game-time condition declared in the language (`condition name { ... }`): a filter keyword with a value type. */
typedef struct RuleforgeConditionInfo {
  const char            *name;    /* the name scripts write */
  const char            *keyword; /* the filter keyword */
  RuleforgeConditionType type;
  boolean                ordered;     /* supports < <= > >= */
  int64                  order;       /* where the condition stands in an emitted block; lower first */
  uint32                 value_count; /* RCT_ENUM: the named values; their position is their number */
  const char           **values;
  const char            *module;      /* the module that declares it */
  uint32                 declaration; /* its position among all declared conditions: ties in `order` keep it */
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

extern const char *ruleforge_manifest_operator_text_get(RuleforgeOperator operation);
extern boolean     ruleforge_manifest_property_lower(RuleforgeCompilation *compilation, RuleforgeSpan span, const char *name,
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

/* A module declaration, found once when the module is loaded. */
typedef struct RuleforgeDeclaration {
  const char                   *name;
  RfDeclaration                 view;
  boolean                       exported;
  const RuleforgeConditionInfo *condition; /* set for a `condition` declaration */
} RuleforgeDeclaration;

/* A parsed module: its source file and syntax tree, read only through the generated views. */
struct RuleforgeModule {
  uint32                file;
  const char           *name;   /* the declared module name, e.g. filters.currency */
  LParserTree          *tree;   /* owned; destroyed with the compilation */
  RfScript              script; /* the root view of tree */
  uint32                import_count;
  const char          **imports; /* module names, in source order */
  uint32                declaration_count;
  RuleforgeDeclaration *declarations;
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
  RuleforgeModule *module;       /* a module's exports, or NULL */
  boolean          is_color_set; /* a color set's colors instead */
  RfColorSetStmt   color_set;
  RuleforgeModule *owner; /* the module that declares the color set */
} RuleforgeNamespace;

typedef struct RuleforgeEvaluation REval;

struct RuleforgeEvaluation {
  RuleforgeCompilation       *compilation;
  const RuleforgeSettings    *settings;
  RuleforgeModule            *module;
  NexusDataList              *variables; /* RuleforgeVariable, used as a stack */
  uint32                      frame_base;
  uint64                      steps;
  uint32                      depth;
  uint32                      emitted;
  const RuleforgeProvenance  *provenance;
  RuleforgeRuleset           *emit;
  NexusDataList              *cores; /* RuleforgeCondition * pushed by coreConditions */
  RuleforgeTheme             *default_theme;
  const RuleforgeDeclaration *default_schema; /* the themeSchema every theme in use must implement, or NULL */
  uint32                      override_depth;
  boolean                     enum_fallback;
  RuleforgeModule            *entry;
  boolean                     failed;
  boolean                     has_user_call;  /* a call written in a script (not in an embedded library module) is running */
  RuleforgeSpan               user_call_span; /* ... and this is where it is written: library errors are reported there */
  boolean                     returning;
  RuleforgeValue              result;
  NexusDataList              *cache_keys;   /* the declaration nodes whose values are cached */
  NexusDataList              *cache_values; /* RuleforgeValue * */
  NexusDataList              *datasets;     /* RuleforgeDataset * in load order */
};

typedef enum RuleforgeTypeKind {
  RTK_INT,
  RTK_FLOAT,
  RTK_BOOL,
  RTK_STRING,
  RTK_COLOR,
  RTK_STYLE,
  RTK_VOID,
  RTK_RULESET,
  RTK_LIST,
  RTK_DATASET,
  RTK_NAMED,
  RTK_NUMBER,
  RTK_RECORD,
  RTK_ANY
} RuleforgeTypeKind;

/* r_eval.c */
extern RuleforgeTypeKind           ruleforge_eval_type_kind_get(RfType type);
extern void                        ruleforge_eval_initialize(REval *eval, RuleforgeCompilation *compilation, const RuleforgeSettings *settings);
extern void                        ruleforge_eval_release(REval *eval);
extern RuleforgeValue              ruleforge_eval_expression(REval *eval, RfExpr view);
extern boolean                     ruleforge_eval_main_run(REval *eval, RuleforgeRuleset **out_ruleset);
extern void                        ruleforge_eval_fail(REval *eval, const char *code, RuleforgeSpan span, const char *note, const char *format, ...);
extern RuleforgeSpan               ruleforge_eval_span_get(const REval *eval, const LParserTree *tree, const LParserGreenNode *node);
extern const char                 *ruleforge_eval_text_get(REval *eval, const LParserTree *tree, const LParserGreenNode *token);
extern const RuleforgeDeclaration *ruleforge_eval_symbol_find(REval *eval, RuleforgeModule *module, const char *name, RuleforgeModule **out_owner);
extern boolean                     ruleforge_eval_boolean_get(REval *eval, RfExpr view, boolean *out_flag);
extern RuleforgeStyle             *ruleforge_eval_style_get(REval *eval, RfExpr view);
extern const RuleforgeProvenance  *ruleforge_eval_provenance_push(REval *eval, RuleforgeProvenanceKind kind, RuleforgeSpan span, const char *label,
                                                                  RuleforgeRecord *record);
extern boolean                     ruleforge_eval_variable_find(REval *eval, const char *name, boolean parameter, RuleforgeValue *out_value);
extern RuleforgeCondition         *ruleforge_eval_condition_create(REval *eval, const RuleforgeConditionInfo *info, RuleforgeOperator operation,
                                                                   uint32 count);
extern boolean                     ruleforge_eval_type_check(REval *eval, RuleforgeValue *value, RfType type, RuleforgeSpan span, const char *what);
extern const char                 *ruleforge_eval_type_name_get(REval *eval, RfType type);

/* r_color.c */
extern uint32 ruleforge_color_value_get(RfColorLiteral view, boolean *out_has_alpha);
extern uint32 ruleforge_color_alpha_override_get(RfColorOverride view);

/* r_condition.c */
typedef struct RuleforgeDnf {
  boolean          constant;
  boolean          value; /* when constant */
  uint32           count;
  RuleforgeClause *clauses;
} RuleforgeDnf;

/* The name of the value of an enum condition that has this number. */
extern const char *ruleforge_condition_value_name_get(const RuleforgeConditionInfo *info, int64 value);
/* The condition a name stands for in a module: its own declarations, then the exports of its imports. NULL (silently) when there is none. */
extern const RuleforgeConditionInfo *ruleforge_condition_find(REval *eval, const RuleforgeModule *module, const char *name);
/* The names of the conditions a module can use, for "did you mean"; returns how many were written, at most `limit`. */
extern uint32              ruleforge_condition_names_get(const REval *eval, const RuleforgeModule *module, const char **names, uint32 limit);
extern boolean             ruleforge_condition_evaluate(REval *eval, RfExpr view, boolean negate, RuleforgeDnf *out);
extern boolean             ruleforge_condition_scope_build(REval *eval, RfScopeAssignment assignment, RuleforgeCondition **out_condition);
extern RuleforgeCondition *ruleforge_condition_value_make(REval *eval, RuleforgeSpan span, const RuleforgeConditionInfo *info,
                                                          RuleforgeOperator operation, const RuleforgeValue *value);

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
extern RuleforgeDataset *ruleforge_dataset_load(REval *eval, RfDatasetStmt declaration, RuleforgeModule *owner);

/* r_theme.c */
extern RuleforgeTheme *ruleforge_theme_resolve(REval *eval, RfThemeStmt declaration, RuleforgeModule *owner);
extern RuleforgeStyle *ruleforge_theme_style_get(const RuleforgeTheme *theme, const char *key);
extern boolean         ruleforge_theme_adapter_find(REval *eval, const RuleforgeModule *module, const char *name, RfAdapterStmt *out_adapter);
extern RuleforgeTheme *ruleforge_theme_adapt(REval *eval, const RuleforgeTheme *theme, RfAdapterStmt adapter, RuleforgeModule *owner,
                                             RuleforgeSpan span);
extern void            ruleforge_theme_declarations_json_write(RuleforgeCompilation *compilation, NexusStringBuilder *out);
struct RuleforgeTheme {
  const char                 *name;
  const char                 *schema;
  const RuleforgeDeclaration *schema_declaration; /* the themeSchema it implements, or NULL when it implements none */
  uint32                      count;
  const char                **keys;
  RuleforgeStyle            **styles;
};

/* r_stdlib.c */
typedef RuleforgeValue(RuleforgeNativeFunction)(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count);

/* The native code behind an `intrinsic func` of an embedded standard-library module; the parameters are those the module declares. */
typedef struct RuleforgeNative {
  const char              *module;
  const char              *name;
  RuleforgeNativeFunction *function;
} RuleforgeNative;

extern const RuleforgeNative *ruleforge_stdlib_native_find(const char *module, const char *name);

/* The standard-library modules embedded in the engine, in the order of ruleforge_stdlib_module_count_get. */
extern uint32      ruleforge_stdlib_module_count_get(void);
extern const char *ruleforge_stdlib_module_name_get(uint32 index);
extern boolean     ruleforge_stdlib_module_source_get(const char *module, const byte **out_text, uint_large *out_size);

/* r_filter.c: lowering, text emission and the model. */
typedef struct RuleforgeFilter {
  uint32           count;
  RuleforgeBlock **blocks;
} RuleforgeFilter;

extern const char      *ruleforge_filter_action_name_get(const RuleforgeBlock *block);
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

/* One compilation: everything allocated while compiling lives in the arena and is released together. */
struct RuleforgeCompilation {
  RuleforgeRuntime *runtime;
  NexusDataArena    arena;

  NexusDataList    *files;       /* RuleforgeFile * */
  NexusDataList    *diagnostics; /* RuleforgeDiagnostic * */
  NexusDataList    *search;      /* const char * source folders */
  NexusDataList    *modules;     /* RuleforgeModule * in load order: imports before their importers */
  NexusDataHashMap *interned;    /* syntax token -> its NUL-terminated text, made once per token */
  NexusDataHashMap *strings;     /* string literal token -> its unescaped value, made once per token */
  uint32            error_count;

  /* Products of compile (r_compilation.c). */
  RuleforgeRuleset        *result;
  RuleforgeFilter         *filter;
  char                    *filter_text;
  NexusDataList           *datasets;         /* RuleforgeDataset * */
  uint32                   condition_serial; /* the number the next declared condition gets */
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

extern const char *ruleforge_diagnostic_severity_name_get(RuleforgeDiagnosticSeverity severity);

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

extern LParserTree  *ruleforge_syntax_parse(RuleforgeCompilation *compilation, uint32 file);
extern LParserTree  *ruleforge_syntax_text_parse(RuleforgeRuntime *runtime, RuleforgeSyntaxKind kind, const byte *text, uint_large size, char *error,
                                                 uint_large error_size);
extern RuleforgeSpan ruleforge_syntax_span_get(const RuleforgeModule *module, const LParserTree *tree, const LParserGreenNode *node);
extern const char   *ruleforge_syntax_text_get(RuleforgeCompilation *compilation, const LParserTree *tree, const LParserGreenNode *token);
/* The source text a node covers, a copy owned by the compilation. */
extern const char *ruleforge_syntax_source_get(RuleforgeCompilation *compilation, const RuleforgeModule *module, const LParserTree *tree,
                                               const LParserGreenNode *node);
extern void        ruleforge_syntax_tree_json_write(const LParserTree *tree, NexusStringBuilder *out);
extern const char *ruleforge_syntax_name_get(RuleforgeCompilation *compilation, RfQualifiedName name);

/* r_compilation.c */
/* Loads every module of the embedded standard library, as if a script imported them all (the manifest lists them). */
extern boolean ruleforge_compilation_library_load(RuleforgeCompilation *compilation);
/* The documentation comment written directly above a declaration of a module, or "" when there is none. */
extern const char                 *ruleforge_compilation_documentation_get(RuleforgeCompilation *compilation, const RuleforgeModule *module,
                                                                           const RuleforgeDeclaration *declaration);
extern RuleforgeModule            *ruleforge_compilation_module_find(const RuleforgeCompilation *compilation, const char *name);
extern const RuleforgeDeclaration *ruleforge_compilation_declaration_find(const RuleforgeModule *module, const char *name);
