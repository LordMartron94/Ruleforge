/*
The platform-unsupported raiser is deliberately independent of Nexus's
assertion and error reporters: either system may need it while its own
platform-specific support is unavailable.
*/
#include "../nexus.h"

#include <stdlib.h>

void _nexus_platform_unsupported_raise(const char *operation, const char *file, uint32 line) {
  const char *operation_description;
  const char *source_file;

  operation_description = operation != NULL ? operation : "unspecified platform-dependent operation";
  source_file           = file != NULL ? file : "<unknown>";

  /* Use the internal panic rather than the wrapper since we provide the source file+line rather than our own. */
  _nexus_program_panic("Nexus unsupported platform operation: %s", source_file, line, operation_description);
  abort();
}
