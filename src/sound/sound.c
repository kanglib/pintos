#include "sound/sound.h"
#include "sound/beep.h"
#include "sound/sb16.h"

bool is_sb16_present;

void sound_init(void)
{
  beep_init();
  is_sb16_present = sb16_init();
}

bool sound_play(int channels,
                int bps,
                int sample_rate,
                const void *stream,
                unsigned length)
{
  if (is_sb16_present)
    return sb16_play(channels, bps, sample_rate, stream, length);
  else
    return false;
}
