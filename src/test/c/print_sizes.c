/**
 * Copyright (C) 2023 eutro
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>

#include "gc/gc.h"

#define PR(Type) printf("Size of " #Type ": %zu bytes\n", sizeof(Type))
#define PR_O(Type, field) printf("  Offset of " #field ": %zu bytes\n", offsetof(Type, field));

int main() {
  PR(GcAllocator);
  PR(Generation);

  PR(MiniPage);
  PR_O(MiniPage, data);

  PR(LargeObject);
  PR_O(LargeObject, data);

  PR(Trail);

  PR(TrailNode);
  PR_O(TrailNode, count);
}
