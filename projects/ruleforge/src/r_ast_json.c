#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

static void r_ast_json_node_write(const RuleforgeCompilation *compilation, const RuleforgeNode *node, NexusStringBuilder *out) {
  const RuleforgeFile *file;
  uint32               line;
  uint32               column;
  uint32               end_line;
  uint32               end_column;
  uint32               i;

  file = ruleforge_source_get(compilation, node->span.file);
  ruleforge_text_position_get(file, node->span.start, &line, &column);
  ruleforge_text_position_get(file, node->span.end, &end_line, &end_column);

  (void)nexus_strings_string_builder_append_formatted(
      out, "{\"kind\":\"%s\",\"start\":%lu,\"end\":%lu,\"line\":%lu,\"column\":%lu,\"endLine\":%lu,\"endColumn\":%lu",
      ruleforge_ast_kind_name_get(node->kind), (unsigned long)node->span.start, (unsigned long)node->span.end, (unsigned long)line,
      (unsigned long)column, (unsigned long)end_line, (unsigned long)end_column);

  if (node->text != NULL) {
    (void)nexus_strings_string_builder_append(out, ",\"text\":");
    (void)nexus_json_quote_append(out, node->text);
  }

  if (node->text2 != NULL) {
    (void)nexus_strings_string_builder_append(out, ",\"text2\":");
    (void)nexus_json_quote_append(out, node->text2);
  }

  if (node->kind == RNK_INT || node->kind == RNK_BOOL || node->kind == RNK_COLOR || node->kind == RNK_ALPHA_OVERRIDE) {
    (void)nexus_strings_string_builder_append_formatted(out, ",\"value\":%lld", (long long)node->integer);
  } else if (node->kind == RNK_FLOAT) {
    (void)nexus_strings_string_builder_append_formatted(out, ",\"value\":%.17g", (double)node->real);
  } else if (node->kind == RNK_REF) {
    (void)nexus_strings_string_builder_append(out, node->integer == RNK_REF_PARAMETER  ? ",\"ref\":\"parameter\""
                                                   : node->integer == RNK_REF_COMPILER ? ",\"ref\":\"compiler\""
                                                                                       : ",\"ref\":\"plain\"");
  }

  if ((node->flags & RNF_EXPORT) != 0) {
    (void)nexus_strings_string_builder_append(out, ",\"export\":true");
  }

  if ((node->flags & RNF_OVERRIDE) != 0) {
    (void)nexus_strings_string_builder_append(out, ",\"override\":true");
  }

  if ((node->flags & RNF_OPTIONAL) != 0) {
    (void)nexus_strings_string_builder_append(out, ",\"optional\":true");
  }

  if ((node->flags & RNF_MUTABLE) != 0) {
    (void)nexus_strings_string_builder_append(out, ",\"mutable\":true");
  }

  if ((node->flags & RNF_ALPHA) != 0) {
    (void)nexus_strings_string_builder_append(out, ",\"alpha\":true");
  }

  if (node->child_count > 0) {
    (void)nexus_strings_string_builder_append(out, ",\"children\":[");

    for (i = 0; i < node->child_count; i++) {
      if (i > 0) {
        (void)nexus_strings_string_builder_append(out, ",");
      }

      r_ast_json_node_write(compilation, node->children[i], out);
    }

    (void)nexus_strings_string_builder_append(out, "]");
  }

  (void)nexus_strings_string_builder_append(out, "}");
}

void ruleforge_ast_json_write(const RuleforgeCompilation *compilation, const RuleforgeNode *root, NexusStringBuilder *out) {
  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && root != NULL && out != NULL, "ruleforge_ast_json_write: NULL argument");

  r_ast_json_node_write(compilation, root, out);
}
