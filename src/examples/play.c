#include <stdio.h>
#include <syscall.h>
#include <wave.h>

int main(int argc, char *argv[])
{
  bool success = true;
  int i;
  for (i = 1; i < argc; i++) {
    struct wave wave;
    if (!wave_open(&wave, argv[i])) {
      printf("%s: wave_open failed\n", argv[i]);
      success = false;
      continue;
    }
    if (!wave.play(&wave, wave.length)) {
      printf("%s: play failed\n", argv[i]);
      success = false;
      continue;
    }
  }
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
