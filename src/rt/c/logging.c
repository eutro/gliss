#include "logging.h"

#ifdef LOG_LEVEL_DYNAMIC
#include <stdlib.h>
#include <stdbool.h>

int gs_get_log_level() {
  static bool init = false;
  static int level = 0;
  if (!init) {
    char *ll = getenv("LOG_LEVEL");
    level = ll ? atoi(ll) : LVLNO_WARN;
  }
  return level;
}

#endif
