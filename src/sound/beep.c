#include "sound/beep.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "devices/timer.h"

static struct lock mutex;

void beep_init(void)
{
  lock_init(&mutex);
}

void beep_stream(uint16_t *stream, unsigned length)
{
  unsigned i;

  lock_acquire(&mutex);
  for (i = 0; i < length; i++) {
    if (stream[0]) {
      uint16_t divider = 1193182 / stream[0];
      outb(0x43, 0xB6);
      outb(0x42, divider & 0xFF);
      outb(0x42, divider >> 8);
      outb(0x61, inb(0x61) | 3);
    } else {
      outb(0x61, inb(0x61) & ~3);
    }
    timer_sleep(stream[1]);
    stream += 2;
  }
  outb(0x61, inb(0x61) & ~3);
  lock_release(&mutex);
}
