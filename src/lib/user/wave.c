#include <wave.h>
#include <syscall.h>

static bool wave_play(struct wave *wave, unsigned length);
static void wave_close(struct wave *wave);

bool wave_open(struct wave *wave, const char *file)
{
  unsigned buffer;

  if ((wave->fd = open(file)) == -1)
    return false;
  unsigned size = filesize(wave->fd);
  if (size < 36)
    return false;

  read(wave->fd, &buffer, 4);
  if (buffer != 0x46464952)             /* "RIFF" */
    return false;
  read(wave->fd, &buffer, 4);
  if (buffer != size - 8)
    return false;
  read(wave->fd, &buffer, 4);
  if (buffer != 0x45564157)             /* "WAVE" */
    return false;

  read(wave->fd, &buffer, 4);
  if (buffer != 0x20746D66)             /* "fmt " */
    return false;
  read(wave->fd, &buffer, 4);
  if (buffer != 16)
    return false;
  read(wave->fd, &buffer, 2);
  if ((short) buffer != 1)
    return false;
  read(wave->fd, &buffer, 2);
  wave->channels = (short) buffer;
  read(wave->fd, &buffer, 4);
  wave->sample_rate = buffer;
  seek(wave->fd, 34);
  read(wave->fd, &buffer, 2);
  wave->bps = (short) buffer;

  seek(wave->fd, 36);
  while (tell(wave->fd) < size) {
    if (read(wave->fd, &buffer, 4) != 4)
      return false;
    unsigned cid = buffer;
    if (read(wave->fd, &buffer, 4) != 4)
      return false;
    if (cid != 0x61746164) {            /* "data" */
      seek(wave->fd, tell(wave->fd) + buffer);
      continue;
    } else {
      wave->play = wave_play;
      wave->close = wave_close;

      wave->length = buffer * 8 / wave->bps / wave->channels;
      wave->pos = 0;
      wave->offset = buffer;
      return true;
    }
  }
  return false;
}

static int8_t buffer[256 * 1024];

static bool wave_play(struct wave *wave, unsigned length)
{
  unsigned size = length * wave->bps / 8 * wave->channels;
  if (size > 256 * 1024)
    return false;
  read(wave->fd, buffer, size);
  return play(wave->channels, wave->bps, wave->sample_rate, buffer, length);
}

static void wave_close(struct wave *wave)
{
  close(wave->fd);
}
