#ifndef SOUND_SOUND_H
#define SOUND_SOUND_H

#include <stdbool.h>

void sound_init(void);
bool sound_play(int channels,
                int bps,
                int sample_rate,
                const void *stream,
                unsigned length);

#endif /* sound/sound.h */
