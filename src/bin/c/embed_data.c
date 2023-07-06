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

#include <stdlib.h>
#include <stdio.h>

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <out-name> <in-name> <var-name>\n", argv[0]);
    return 1;
  }

  FILE *in = fopen(argv[2], "r");
  if (!in) {
    perror(argv[2]);
    return 1;
  }
  FILE *out = fopen(argv[1], "w");
  if (!out) {
    perror(argv[1]);
    return 1;
  }
  char *varName = argv[3];

  fseek(in, 0, SEEK_END);
  size_t dataLen = ftell(in);
  fseek(in, 0, SEEK_SET);

  fprintf(out, "#include <stdint.h>\n");
  fprintf(out, "#include <stdalign.h>\n");
  fprintf(out, "static const struct {\n");
  fprintf(out, "  alignas(uint32_t) unsigned char data[%zu];\n", dataLen);
  fprintf(out, "} d = {{");

  unsigned char buf[256];
  unsigned countOnLine = 0;
  while (!feof(in)) {
    size_t read = fread(buf, 1, sizeof(buf), in);
    if (ferror(in)) {
      perror("error reading");
    }
    if (read == 0) break;
    for (unsigned i = 0; i < read; ++i) {
      if (countOnLine == 0) fprintf(out, "\n ");
      fprintf(out, " 0x%02x,", buf[i]);
      countOnLine = (countOnLine + 1) % 16;
    }
  }
  fprintf(out, "\n}};\n");
  fprintf(out, "const unsigned char *%s = d.data;\n", varName);
  fprintf(out, "const uint32_t %s_len = %zu;\n\n", varName, dataLen);

  fclose(in);
  fclose(out);

  return 0;
}

FILE* open_or_exit(const char *fname, const char *mode) {
  FILE *f = fopen(fname, mode);
  if (f == NULL) {
    perror(fname);
    exit(EXIT_FAILURE);
  }
  return f;
}
