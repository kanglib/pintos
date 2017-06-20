#ifndef SOUND_BEEP_H
#define SOUND_BEEP_H

#include <stdint.h>

void beep_init(void);
void beep_stream(uint16_t *stream, unsigned length);

#endif /* sound/beep.h */
