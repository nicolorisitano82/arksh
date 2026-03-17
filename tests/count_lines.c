#include <stdio.h>

int main(void) {
  int c;
  int saw_text = 0;
  int line_count = 0;

  while ((c = getchar()) != EOF) {
    saw_text = 1;
    if (c == '\n') {
      line_count++;
    }
  }

  if (saw_text && line_count == 0) {
    line_count = 1;
  }

  printf("%d\n", line_count);
  return 0;
}
