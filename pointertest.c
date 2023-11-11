#include <stdio.h>

int main(void) {
  char *array1[2];
  char **array2[2];

  array1[0] = "a";
  array2[0] = &array1[0];
  printf("%s\n", *array2[0]);
  array1[0] = "b";
  printf("%s\n", *array2[0]);
return 0;
}
