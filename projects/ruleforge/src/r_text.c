#include <nexus.h>

#include "./r_internal.h"

void ruleforge_text_position_get(const RuleforgeFile *file, uint32 offset, uint32 *out_line, uint32 *out_column) {
  uint32 low;
  uint32 high;
  uint32 middle;

  NEXUS_PROGRAM_PRECONDITION(file != NULL, "ruleforge_text_position_get: NULL file");
  NEXUS_PROGRAM_PRECONDITION(out_line != NULL && out_column != NULL, "ruleforge_text_position_get: NULL output");

  low  = 0;
  high = file->line_count;

  /* The last line whose start is at or before the offset; lines are 1-based in the output. */
  while (high - low > 1) {
    middle = low + ((high - low) / 2);

    if (file->line_starts[middle] <= offset) {
      low = middle;
    } else {
      high = middle;
    }
  }

  *out_line   = low + 1;
  *out_column = (offset - file->line_starts[low]) + 1;
}
