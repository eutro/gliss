// modified from https://stackoverflow.com/questions/11813271/embed-resources-eg-shader-code-images-into-executable-library-with-cmake

#include <stdlib.h>
#include <stdio.h>

FILE* open_or_exit(const char *fname, const char *mode) {
  FILE *f = fopen(fname, mode);
  if (f == NULL) {
    perror(fname);
    exit(EXIT_FAILURE);
  }
  return f;
}

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "USAGE: %s {sym} {rsrc} {name}\n\n"
        "  Creates `u8 {name}[]` in {sym}.c from the contents of {rsrc}\n",
        argv[0]);
    return EXIT_FAILURE;
  }

  const char *sym = argv[1];
  FILE *in = open_or_exit(argv[2], "r");
  const char *name = argv[3];

  fseek(in, 0, SEEK_END);
  size_t data_len = ftell(in);
  fseek(in, 0, SEEK_SET);

  char symfile[256];
  snprintf(symfile, sizeof(symfile), "%s.c", sym);

  FILE *out = open_or_exit(symfile, "w");
  fprintf(out, "#include <stdint.h>\n");
  fprintf(out, "#include <stdalign.h>\n");
  fprintf(out, "static const struct {\n");
  fprintf(out, "  alignas(uint32_t) unsigned char data[%zu];\n", data_len);
  fprintf(out, "} d = {{\n");

  unsigned char buf[256];
  size_t nread = 0;
  size_t linecount = 0;
  do {
    nread = fread(buf, 1, sizeof(buf), in);
    size_t i;
    for (i = 0; i < nread; i++) {
      fprintf(out, "0x%02x, ", buf[i]);
      if (++linecount == 16) { fprintf(out, "\n"); linecount = 0; }
    }
  } while (nread > 0);
  if (linecount > 0) fprintf(out, "\n");
  fprintf(out, "}};\n");
  fprintf(out, "const unsigned char *%s = d.data;\n", name);
  fprintf(out, "const uint32_t %s_len = %zu;\n\n", name, data_len);

  fclose(in);
  fclose(out);

  return EXIT_SUCCESS;
}
