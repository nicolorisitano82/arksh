#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(int argc, char **argv) {
  int seconds = 1;

  if (argc > 1) {
    seconds = atoi(argv[1]);
    if (seconds < 0) {
      seconds = 0;
    }
  }

#ifdef _WIN32
  Sleep((DWORD) seconds * 1000U);
#else
  sleep((unsigned int) seconds);
#endif

  return 0;
}
