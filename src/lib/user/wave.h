#ifndef LIB_USER_WAVE_H
#define LIB_USER_WAVE_H

#include <stdbool.h>

struct wave {
  bool (*play)(struct wave *, unsigned);
  void (*close)(struct wave *);

  int channels;
  int bps;
  int sample_rate;
  unsigned length;
  int pos;

  int fd;
  int offset;
};

bool wave_open(struct wave *wave, const char *file);

#endif /* lib/user/wave.h */
