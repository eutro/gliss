#pragma once

#include "bytecode/image.h"

Err *gs_run_raw_image(u32 size, const u8 *buf);

Err *gs_run_image(Image *img);
