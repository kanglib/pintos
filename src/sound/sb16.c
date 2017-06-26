#include "sound/sb16.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"

static uint16_t base_address;

#define PORT_MXA (base_address + 0x4) /* Mixer Address Port */
#define PORT_MXD (base_address + 0x5) /* Mixer Data Port */
#define PORT_RST (base_address + 0x6) /* Reset */
#define PORT_RD  (base_address + 0xA) /* Read Data */
#define PORT_WR  (base_address + 0xC) /* Write Command/Data */
#define PORT_WRS (base_address + 0xC) /* Write-Buffer Status */
#define PORT_RDS (base_address + 0xE) /* Read-Buffer Status */

static int load_dma_buffer(void);
static void interrupt(struct intr_frame *f);

static bool dsp_reset(void);
static uint8_t dsp_read(void);
static void dsp_write(uint8_t data);

/* Unfortunately, 8237 DMA can use only 24-bit addresses. */
static int8_t dma_buffer_unaligned[16384];
static uintptr_t dma_buffer;
static int dma_buffer_plane;

static int dsp_bps;
static const void *dsp_stream;
static unsigned dsp_length;
static volatile bool dsp_is_playing;
static struct lock mutex;

bool sb16_init(void)
{
  /* Manual probing for ISA bus. */
  for (base_address = 0x220; base_address <= 0x280; base_address += 0x20) {
    if (!dsp_reset())
      continue;

    /* Get DSP version number */
    dsp_write(0xE1);
    uint8_t major = dsp_read(); dsp_read();
    if (major != 4)
      continue;

    printf("sound: detected Sound Blaster 16, ISA bus %xh\n", base_address);
    break;
  }
  if (base_address > 0x280)
    return false;

  outb(PORT_MXA, 0x80);
  outb(PORT_MXD, 0x02);
  outb(PORT_MXA, 0x81);
  outb(PORT_MXD, 0x22);

  dma_buffer = vtop(dma_buffer_unaligned);
  if ((dma_buffer >> 16) != ((dma_buffer + 8191) >> 16))
    dma_buffer += 8192;
  lock_init(&mutex);

  intr_register_ext(0x25, interrupt, "SB16 ISA Sound Card");
  return true;
}

bool sb16_play(int channels,
               int bps,
               int sample_rate,
               const void *stream,
               unsigned length)
{
  if (channels < 1 || channels > 2)
    return false;
  if (bps != 8 && bps != 16)
    return false;
  if (sample_rate < 5000 || sample_rate > 44100)
    return false;

  lock_acquire(&mutex);

  dsp_bps = bps;
  dsp_stream = stream;
  dsp_length = length * bps / 8 * channels;
  dsp_is_playing = true;

  dma_buffer_plane = 0;
  load_dma_buffer();
  load_dma_buffer();

  if (bps == 16) {
    outb(0xD4, 0x05);
    outb(0xD6, 0x59);
    outb(0xD8, 1);
    outb(0xC4, dma_buffer >> 1);
    outb(0xC4, dma_buffer >> 9);
    outb(0x8B, dma_buffer >> 16);
    outb(0xC6, 0xFF);
    outb(0xC6, 0x0F);
    outb(0xD4, 0x01);
  } else {
    outb(0x0A, 0x05);
    outb(0x0B, 0x59);
    outb(0x0C, 1);
    outb(0x02, dma_buffer);
    outb(0x02, dma_buffer >> 8);
    outb(0x83, dma_buffer >> 16);
    outb(0x03, 0xFF);
    outb(0x03, 0x1F);
    outb(0x0A, 0x01);
  }

  dsp_write(0x41);
  dsp_write(sample_rate >> 8);
  dsp_write(sample_rate);

  if (bps == 16) {
    dsp_write(0xB6);
    dsp_write((channels == 2) ? 0x30 : 0x10);
    dsp_write(0xFF);
    dsp_write(0x07);
  } else {
    dsp_write(0xC6);
    dsp_write((channels == 2) ? 0x20 : 0x00);
    dsp_write(0xFF);
    dsp_write(0x0F);
  }

  /* Busy wait... */
  while (dsp_is_playing) ;

  lock_release(&mutex);
  return true;
}

static int load_dma_buffer(void)
{
  void *buffer = ptov(dma_buffer) + dma_buffer_plane * 4096;
  if (dsp_length >= 4096) {
    memcpy(buffer, dsp_stream, 4096);
    dma_buffer_plane = !dma_buffer_plane;
    dsp_stream += 4096;
    dsp_length -= 4096;
    return 0;
  } else if (dsp_length > 0) {
    memcpy(buffer, dsp_stream, dsp_length);
    memset(buffer + dsp_length, 0, 4096 - dsp_length);
    dsp_length = 0;
    return 1;
  } else {
    return 2;
  }
}

static void interrupt(struct intr_frame *f UNUSED)
{
  outb(PORT_MXA, 0x82);
  uint8_t status = inb(PORT_MXD);
  if (status & 1) {
    int phase = load_dma_buffer();
    if (phase == 1)
      dsp_write(0xDA);
    else if (phase == 2)
      dsp_is_playing = false;
    inb(base_address + 0xE);
  } else if (status & 2) {
    int phase = load_dma_buffer();
    if (phase == 1)
      dsp_write(0xD9);
    else if (phase == 2)
      dsp_is_playing = false;
    inb(base_address + 0xF);
  } else if (status & 4) {
    inb(base_address + 0x100);
  }
}

static bool dsp_reset(void)
{
  int i;

  outb(PORT_RST, 1);
  timer_msleep(3);
  outb(PORT_RST, 0);
  for (i = 0; i < 65536; i++)
    if (dsp_read() == 0xAA)
      return true;
  return false;
}

static uint8_t dsp_read(void)
{
  while (~inb(PORT_RDS) & 0x80) ;
  return inb(PORT_RD);
}

static void dsp_write(uint8_t data)
{
  while (inb(PORT_WRS) & 0x80) ;
  outb(PORT_WR, data);
}
