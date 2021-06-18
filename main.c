/*
 * -----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <webmaster@flippeh.de> wrote this file. As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return. Lukas Niederbremer.
 * -----------------------------------------------------------------------------
 * NBTx modifications by Arnoldo A. Barón.
 * -----------------------------------------------------------------------------
 */

#include "nbtx.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void dump_nbtx(const char* filename);

int main(int argc, char** argv) {
  if (argc > 1) {
    dump_nbtx(argv[1]);
  } else {
    fprintf(stderr, "Usage: %s nbtx_file", argv[0]);
    return 1;
  }

  return 0;
}

void dump_nbtx(const char* filename) {
  assert(errno == NBTX_OK);

  FILE* f = fopen(filename, "rb");
  nbtx_node* root = nbtx_parse_file(f);
  fclose(f);

  if (errno != NBTX_OK) {
    fprintf(stderr, "Parsing error!\n");
    return;
  }

  char* str = nbtx_dump_ascii(root, NBTX_SAME_LINE, NBTX_HEX, 2);
  nbtx_free(root);

  if (str == NULL)
    fprintf(stderr, "Printing error!");

  printf("%s", str);

  free(str);
}
