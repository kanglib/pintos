#include "tone.h"
#include <syscall.h>

uint16_t score[] = {
  0,        39,
  TONE_G4,  13,
  TONE_A4,  130,
  TONE_G4,  13,
  TONE_A4,  13,

  TONE_B4,  26,
  TONE_C5,  26,
  TONE_B4,  13,
  TONE_C5,  13,
  TONE_B4,  13,
  TONE_G4,  13,
  TONE_B4,  26,
  TONE_C5,  26,
  TONE_B4,  13,
  TONE_G4,  39,

  TONE_A4,  156,
  0,        26,
  TONE_E4,  13,
  TONE_G4,  13,

  TONE_A4,  104,
  TONE_GS4, 78,
  0,        26,

  TONE_A4,  39,
  TONE_A4,  13,
  TONE_E5,  130,
  TONE_D5,  13,
  TONE_E5,  13,

  TONE_F5,  26,
  TONE_E5,  13,
  TONE_F5,  13,
  TONE_G5,  26,
  TONE_F5,  13,
  TONE_G5,  13,
  TONE_C6,  26,
  TONE_B5,  26,
  TONE_A5,  26,
  TONE_G5,  26,

  TONE_A5,  208,

  TONE_A5,  78,
  TONE_GS5, 13,
  TONE_A5,  13,
  TONE_B5,  52,
  TONE_E5,  52,

  TONE_E5,  104,
  TONE_D5,  78,
  TONE_E5,  13,
  TONE_D5,  13,

  TONE_C5,  104,
  TONE_B4,  52,
  TONE_E4,  52,

  TONE_A4,  52,
  TONE_F4,  52,
  TONE_A4,  52,
  TONE_C5,  26,
  TONE_B4,  13,
  TONE_A4,  13,

  TONE_B4,  78,
  TONE_A4,  13,
  TONE_GS4, 13,
  TONE_A4,  52,
  TONE_GS4, 26,
  TONE_E4,  26,

  TONE_A4,  208,
};

int main(void)
{
  beep(score, sizeof(score) / 4);
  return EXIT_SUCCESS;
}
