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
