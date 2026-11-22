#include <nexus.h>

#include "../ruleforge.h"

#ifndef RULEFORGE_VERSION
#  define RULEFORGE_VERSION "0.0.0-dev"
#endif

const char *ruleforge_version_get(void) {
  return RULEFORGE_VERSION;
}
