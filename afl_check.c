#include <stdio.h>
#include <string.h>
#include "nbtx.h"

int main(int argc, const char* argv[]) {
  FILE* in = stdin;
  if (argc > 1) {
    in = fopen(argv[1], "rb");
    if (!in) {
      perror("fopen");
      return -1;
    }
  }

  #ifdef __AFL_HAVE_MANUAL_CONTROL
  __AFL_INIT();
  #endif


  #ifdef __AFL_HAVE_MANUAL_CONTROL
  while (__AFL_LOOP(10000)) {
    #endif
    char buf[65536];
    const size_t len = fread(buf, 1, sizeof(buf), in);
    nbtx_free(nbtx_parse(buf, len));
    #ifdef __AFL_HAVE_MANUAL_CONTROL
  }
  #endif
}
