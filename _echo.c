#define _POSIX_C_SOURCE 200809
#include <stdio.h>

int main(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) printf("%s%c", argv[i], i + 1 < argc ? ' ' : '\n');
}
