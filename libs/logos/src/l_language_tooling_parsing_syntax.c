#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

void logos_language_tooling_parsing_syntax_tree_destroy(LParserTree *tree) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL, "tree must not be NULL");

  if (tree->taken) {
    tree->parser->tree_count--;
  }

  nexus_memory_deallocate(tree->source);
  nexus_data_chunked_list_release(&tree->nodes);
  nexus_data_chunked_list_release(&tree->alternatives);
  nexus_data_chunked_list_release(&tree->tokens);
  nexus_data_slabbed_list_destroy(tree->diagnostics);
  nexus_data_slabbed_list_destroy(tree->expectations);
  nexus_memory_deallocate(tree);
}

const byte *logos_language_tooling_parsing_syntax_tree_source_get(const LParserTree *tree, uint_large *out_size) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL && out_size != NULL, "tree and out_size must not be NULL");

  *out_size = tree->source_size;

  return tree->source;
}

LParserStatus logos_language_tooling_parsing_syntax_tree_status_get(const LParserTree *tree) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL, "tree must not be NULL");

  return tree->status;
}

const LParserGreenNode *logos_language_tooling_parsing_syntax_tree_root_get(const LParserTree *tree) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL, "tree must not be NULL");

  return (const LParserGreenNode *)nexus_data_chunked_list_at(&tree->nodes, tree->root);
}

uint_large logos_language_tooling_parsing_syntax_tree_token_count_get(const LParserTree *tree) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL, "tree must not be NULL");

  return nexus_data_chunked_list_length_get(&tree->tokens);
}

uint_large logos_language_tooling_parsing_syntax_tree_node_count_get(const LParserTree *tree) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL, "tree must not be NULL");

  return nexus_data_chunked_list_length_get(&tree->nodes);
}

uint_large logos_language_tooling_parsing_syntax_tree_alternative_count_get(const LParserTree *tree) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL, "tree must not be NULL");

  return nexus_data_chunked_list_length_get(&tree->alternatives);
}

LLexerError logos_language_tooling_parsing_syntax_tree_token_at(const LParserTree *tree, uint_large index, LLexerToken *out_token) {
  const LParserToken *record;

  NEXUS_PROGRAM_PRECONDITION(tree != NULL && out_token != NULL, "tree and out_token must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(index < nexus_data_chunked_list_length_get(&tree->tokens), "token index must be in bounds");

  record                   = (const LParserToken *)nexus_data_chunked_list_at(&tree->tokens, index);
  out_token->kind          = record->kind;
  out_token->lexeme_offset = record->offset;
  out_token->lexeme_width  = record->width;

  return (LLexerError)record->error;
}

uint_large logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(const LParserTree *tree) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL, "tree must not be NULL");

  return nexus_data_slabbed_list_length_get(tree->diagnostics);
}

const LParserDiagnostic *logos_language_tooling_parsing_syntax_tree_diagnostic_at(const LParserTree *tree, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL, "tree must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(index < nexus_data_slabbed_list_length_get(tree->diagnostics), "diagnostic index must be in bounds");

  return &((const LParsingDiagnostic *)nexus_data_slabbed_list_at(tree->diagnostics, index))->view;
}

const LParserExpectation *logos_language_tooling_parsing_syntax_tree_diagnostic_expectation_at(const LParserTree *tree, uint_large diagnostic_index,
                                                                                               uint_large expectation_index) {
  const LParsingDiagnostic *diagnostic;

  NEXUS_PROGRAM_PRECONDITION(tree != NULL, "tree must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(diagnostic_index < nexus_data_slabbed_list_length_get(tree->diagnostics), "diagnostic index must be in bounds");

  diagnostic = (const LParsingDiagnostic *)nexus_data_slabbed_list_at(tree->diagnostics, diagnostic_index);
  NEXUS_PROGRAM_PRECONDITION(expectation_index < diagnostic->view.expected_count, "expectation index must lie within this diagnostic");

  return (const LParserExpectation *)nexus_data_slabbed_list_at(tree->expectations, diagnostic->expected_begin + expectation_index);
}

/*
Summary: the grammar symbol of a node, or NULL for nodes that carry none.
Description: token and unparsed leaves keep a source offset where other nodes keep their symbol; the sequence, error and
  source nodes and the operator nodes of a Pratt operation have no symbol.
*/
static const LParsingSymbol *l_syntax_node_symbol(const LParserTree *tree, const LParserGreenNode *node) {
  if (node->kind == LPGK_TOKEN || node->kind == LPGK_UNPARSED || node->aux == LPARSER_INDEX_NONE) {
    return NULL;
  }

  return (const LParsingSymbol *)nexus_data_list_at_const(tree->parser->symbols, node->aux);
}

/* Summary: tells whether the node is an operator node of a Pratt operation: an expression without a grammar symbol. */
static boolean l_syntax_node_is_pratt_operator(const LParserGreenNode *node) {
  return node->kind == LPGK_EXPRESSION && node->aux == LPARSER_INDEX_NONE;
}

LParserGreenKind logos_language_tooling_parsing_syntax_green_node_kind_get(const LParserGreenNode *node) {
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "node must not be NULL");

  return (LParserGreenKind)node->kind;
}

uint_large logos_language_tooling_parsing_syntax_green_node_width_get(const LParserGreenNode *node) {
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "node must not be NULL");

  return node->width;
}

const char *logos_language_tooling_parsing_syntax_green_node_label_get(const LParserTree *tree, const LParserGreenNode *node) {
  const LParsingSymbol *symbol;

  NEXUS_PROGRAM_PRECONDITION(node != NULL, "node must not be NULL");

  if (l_syntax_node_is_pratt_operator(node)) {
    return node->payload.label;
  }

  symbol = l_syntax_node_symbol(tree, node);

  return symbol != NULL ? symbol->label : NULL;
}

LContextaOperationKind logos_language_tooling_parsing_syntax_green_node_operation_kind_get(const LParserTree *tree, const LParserGreenNode *node) {
  const LParsingSymbol *symbol;

  NEXUS_PROGRAM_PRECONDITION(node != NULL, "node must not be NULL");

  if (l_syntax_node_is_pratt_operator(node)) {
    return LCOK_PRATT;
  }

  symbol = l_syntax_node_symbol(tree, node);

  return symbol != NULL ? symbol->kind : LCOK_COUNT;
}

LLexerTokenKind logos_language_tooling_parsing_syntax_green_node_token_kind_get(const LParserTree *tree, const LParserGreenNode *node) {
  const LParsingSymbol *symbol;

  NEXUS_PROGRAM_PRECONDITION(node != NULL, "node must not be NULL");

  if (node->kind == LPGK_TOKEN) {
    return node->payload.token_kind;
  }

  symbol = l_syntax_node_symbol(tree, node);

  return symbol != NULL ? symbol->terminal.token_kind : L_LEXER_TOKEN_KIND_ERROR;
}

LLexerError logos_language_tooling_parsing_syntax_green_node_lexical_error_get(const LParserGreenNode *node) {
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "node must not be NULL");

  return (LLexerError)node->lexical_error;
}

uint_large logos_language_tooling_parsing_syntax_green_node_alternative_count_get(const LParserGreenNode *node) {
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "node must not be NULL");

  return node->alternative_count;
}

const byte *logos_language_tooling_parsing_syntax_green_node_text_get(const LParserTree *tree, const LParserGreenNode *node, uint_large *out_size) {
  NEXUS_PROGRAM_PRECONDITION(node != NULL && out_size != NULL, "node and out_size must not be NULL");

  *out_size = node->width;

  if (node->kind != LPGK_TOKEN && node->kind != LPGK_UNPARSED) {
    return NULL;
  }

  return tree->source + node->aux;
}

const LParserGreenAlternative *logos_language_tooling_parsing_syntax_green_node_alternative_first_get(const LParserTree      *tree,
                                                                                                      const LParserGreenNode *node) {
  NEXUS_PROGRAM_PRECONDITION(node != NULL, "node must not be NULL");

  if (node->first_alternative == LPARSER_INDEX_NONE) {
    return NULL;
  }

  return (const LParserGreenAlternative *)nexus_data_chunked_list_at(&tree->alternatives, node->first_alternative);
}

const LParserGreenAlternative *logos_language_tooling_parsing_syntax_alternative_next_get(const LParserTree             *tree,
                                                                                          const LParserGreenAlternative *alternative) {
  NEXUS_PROGRAM_PRECONDITION(alternative != NULL, "alternative must not be NULL");

  if (alternative->next == LPARSER_INDEX_NONE) {
    return NULL;
  }

  return (const LParserGreenAlternative *)nexus_data_chunked_list_at(&tree->alternatives, alternative->next);
}

uint32 logos_language_tooling_parsing_syntax_alternative_child_count_get(const LParserGreenAlternative *alternative) {
  NEXUS_PROGRAM_PRECONDITION(alternative != NULL, "alternative must not be NULL");

  if (alternative->children[0] == LPARSER_INDEX_NONE) {
    return 0;
  }

  return alternative->children[1] == LPARSER_INDEX_NONE ? 1 : 2;
}

const LParserGreenNode *logos_language_tooling_parsing_syntax_alternative_child_get(const LParserTree             *tree,
                                                                                    const LParserGreenAlternative *alternative, uint32 child_index) {
  NEXUS_PROGRAM_PRECONDITION(alternative != NULL, "alternative must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(child_index < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative),
                             "child index must be in bounds");

  return (const LParserGreenNode *)nexus_data_chunked_list_at(&tree->nodes, alternative->children[child_index]);
}

const LParserExpectation *logos_language_tooling_parsing_syntax_green_node_expectation_get(const LParserTree *tree, const LParserGreenNode *node) {
  const LParsingSymbol *symbol;

  NEXUS_PROGRAM_PRECONDITION(node != NULL, "node must not be NULL");

  symbol = l_syntax_node_symbol(tree, node);

  if (symbol == NULL || (symbol->kind != LCOK_TOKEN && symbol->kind != LCOK_SYMBOL)) {
    return NULL;
  }

  return &symbol->terminal;
}

NexusStringFormatResult logos_language_tooling_parsing_syntax_tree_diagnostic_message_write(const LParserTree *tree, uint_large index, char *buffer,
                                                                                            uint_large capacity) {
  const LParserDiagnostic  *diagnostic;
  const LParsingDiagnostic *record;
  const LParserExpectation *expected;
  const char               *action;
  const char               *label;

  NEXUS_PROGRAM_PRECONDITION(tree != NULL && buffer != NULL && capacity > 0, "tree/buffer must exist and capacity must be positive");
  NEXUS_PROGRAM_PRECONDITION(index < nexus_data_slabbed_list_length_get(tree->diagnostics), "diagnostic index must be in bounds");

  record     = (const LParsingDiagnostic *)nexus_data_slabbed_list_at(tree->diagnostics, index);
  diagnostic = &record->view;

  if (diagnostic->kind == LPDK_LIMIT) {
    return nexus_strings_string_format_with_truncation(buffer, capacity, "Parsing stopped at byte %llu: internal resource limit reached",
                                                       (unsigned long long)diagnostic->byte_offset);
  }

  if (diagnostic->kind == LPDK_LEXICAL) {
    switch (diagnostic->lexical_error) {
    case LLE_UNRECOGNIZED:
      label = "unrecognized input";
      break;
    case LLE_TOKEN_TOO_LONG:
      label = "token width limit exceeded";
      break;
    case LLE_CONTEXT_OVERFLOW:
      label = "lexer context stack overflow";
      break;
    case LLE_CONTEXT_UNDERFLOW:
      label = "lexer context stack underflow";
      break;
    case LLE_NONE:
    case LLE_COUNT:
    default:
      label = "lexical error";
      break;
    }

    return nexus_strings_string_format_with_truncation(buffer, capacity, "Lexical error at byte %llu (%llu bytes): %s",
                                                       (unsigned long long)diagnostic->byte_offset, (unsigned long long)diagnostic->byte_width,
                                                       label);
  }

  action = "Unexpected input retained in an error node";

  if (diagnostic->kind == LPDK_MISSING_TOKEN) {
    action = "Missing syntax inserted";
  } else if (diagnostic->kind == LPDK_UNEXPECTED_EOF) {
    action = "Unexpected end of input";
  }

  if (diagnostic->expected_count == 0) {
    return nexus_strings_string_format_with_truncation(buffer, capacity, "%s at byte %llu (token kind %llu, %llu bytes)", action,
                                                       (unsigned long long)diagnostic->byte_offset, (unsigned long long)diagnostic->actual_kind,
                                                       (unsigned long long)diagnostic->byte_width);
  }

  expected = (const LParserExpectation *)nexus_data_slabbed_list_at(tree->expectations, record->expected_begin);
  label    = expected->label;

  if (label != NULL) {
    return nexus_strings_string_format_with_truncation(
        buffer, capacity, "%s at byte %llu: expected %s%s (actual token kind %llu)", action, (unsigned long long)diagnostic->byte_offset, label,
        diagnostic->expected_count > 1 ? " or another listed terminal" : "", (unsigned long long)diagnostic->actual_kind);
  }

  if (expected->byte_terminal) {
    return nexus_strings_string_format_with_truncation(buffer, capacity, "%s at byte %llu: expected a one-byte terminal%s", action,
                                                       (unsigned long long)diagnostic->byte_offset,
                                                       diagnostic->expected_count > 1 ? " or another listed terminal" : "");
  }

  return nexus_strings_string_format_with_truncation(buffer, capacity, "%s at byte %llu: expected token kind %llu%s (actual %llu)", action,
                                                     (unsigned long long)diagnostic->byte_offset, (unsigned long long)expected->token_kind,
                                                     diagnostic->expected_count > 1 ? " or another listed terminal" : "",
                                                     (unsigned long long)diagnostic->actual_kind);
}

/* ---------------------------------------------------------------------------- */
/* DIAGNOSTICS AND EXPECTATIONS                                                 */
/* ---------------------------------------------------------------------------- */

LParserDiagnosticKind logos_language_tooling_parsing_syntax_diagnostic_kind_get(const LParserDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->kind;
}

uint_large logos_language_tooling_parsing_syntax_diagnostic_byte_offset_get(const LParserDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->byte_offset;
}

uint_large logos_language_tooling_parsing_syntax_diagnostic_byte_width_get(const LParserDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->byte_width;
}

LLexerTokenKind logos_language_tooling_parsing_syntax_diagnostic_actual_kind_get(const LParserDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->actual_kind;
}

LLexerError logos_language_tooling_parsing_syntax_diagnostic_lexical_error_get(const LParserDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->lexical_error;
}

uint_large logos_language_tooling_parsing_syntax_diagnostic_expectation_count_get(const LParserDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->expected_count;
}

uint32 logos_language_tooling_parsing_syntax_diagnostic_missing_token_count_get(const LParserDiagnostic *diagnostic) {
  NEXUS_PROGRAM_PRECONDITION(diagnostic != NULL, "diagnostic must not be NULL");

  return diagnostic->missing_token_count;
}

LLexerTokenKind logos_language_tooling_parsing_syntax_expectation_token_kind_get(const LParserExpectation *expectation) {
  NEXUS_PROGRAM_PRECONDITION(expectation != NULL, "expectation must not be NULL");

  return expectation->token_kind;
}

boolean logos_language_tooling_parsing_syntax_expectation_byte_terminal_is(const LParserExpectation *expectation) {
  NEXUS_PROGRAM_PRECONDITION(expectation != NULL, "expectation must not be NULL");

  return expectation->byte_terminal;
}

void logos_language_tooling_parsing_syntax_expectation_symbol_mask_get(const LParserExpectation *expectation, LContextaObservationMask out_mask) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(expectation != NULL && out_mask != NULL, "expectation and out_mask must not be NULL");

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(expectation->symbol_mask); i++) {
    out_mask[i] = expectation->symbol_mask[i];
  }
}

const char *logos_language_tooling_parsing_syntax_expectation_label_get(const LParserExpectation *expectation) {
  NEXUS_PROGRAM_PRECONDITION(expectation != NULL, "expectation must not be NULL");

  return expectation->label;
}

/* ---------------------------------------------------------------------------- */
/* RED OCCURRENCES                                                              */
/* ---------------------------------------------------------------------------- */

void logos_language_tooling_parsing_syntax_red_root_initialize(const LParserTree *tree, LParserRedNode *out_node) {
  NEXUS_PROGRAM_PRECONDITION(tree != NULL && out_node != NULL, "tree and out_node must not be NULL");

  out_node->tree               = tree;
  out_node->parent             = NULL;
  out_node->green              = (const LParserGreenNode *)nexus_data_chunked_list_at(&tree->nodes, tree->root);
  out_node->byte_offset        = 0;
  out_node->parent_alternative = NULL;
  out_node->index_in_parent    = 0;
}

void logos_language_tooling_parsing_syntax_red_child_get(const LParserRedNode *parent, const LParserGreenAlternative *alternative, uint32 child_index,
                                                         LParserRedNode *out_node) {
  const LParserGreenNode *sibling;
  uint_large              offset;
  uint32                  i;

  NEXUS_PROGRAM_PRECONDITION(parent != NULL && alternative != NULL && out_node != NULL && parent != out_node,
                             "parent, alternative and distinct out_node must exist");
  NEXUS_PROGRAM_PRECONDITION(nexus_data_chunked_list_at(&parent->tree->nodes, alternative->parent) == (const void *)parent->green &&
                                 child_index < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative),
                             "alternative must belong to parent and contain the selected child");

  offset = parent->byte_offset;

  for (i = 0; i < child_index; i++) {
    sibling = (const LParserGreenNode *)nexus_data_chunked_list_at(&parent->tree->nodes, alternative->children[i]);
    offset += sibling->width;
  }

  out_node->tree               = parent->tree;
  out_node->parent             = parent;
  out_node->green              = (const LParserGreenNode *)nexus_data_chunked_list_at(&parent->tree->nodes, alternative->children[child_index]);
  out_node->byte_offset        = offset;
  out_node->parent_alternative = alternative;
  out_node->index_in_parent    = child_index;
}

const byte *logos_language_tooling_parsing_syntax_red_text_get(const LParserRedNode *node, uint_large *out_size) {
  const LParsingGreenNode *green;

  NEXUS_PROGRAM_PRECONDITION(node != NULL && out_size != NULL, "node and out_size must not be NULL");

  green = node->green;
  NEXUS_PROGRAM_PRECONDITION(node->byte_offset <= node->tree->source_size && green->width <= node->tree->source_size - node->byte_offset,
                             "red occurrence must lie within its tree's source");

  *out_size = green->width;

  return node->tree->source + node->byte_offset;
}
