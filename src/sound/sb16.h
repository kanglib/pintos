#ifndef SOUND_SB16_H
#define SOUND_SB16_H

#include <stdbool.h>

bool sb16_init(void);
bool sb16_play(int channels,
               int bps,
               int sample_rate,
               const void *stream,
               unsigned length);

#endif /* sound/sb16.h */
