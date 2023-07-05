#include "driver.h"

extern const u8 *gs_main_data;
extern const u32 gs_main_data_len;

Err *gs_main() {
  return gs_run_raw_image(gs_main_data_len, gs_main_data);
}
