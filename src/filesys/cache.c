#include "filesys/cache.h"
#include <string.h>
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "devices/timer.h"

struct line {
  int flags;
  disk_sector_t sector_idx;
  uint8_t buffer[DISK_SECTOR_SIZE];
};

struct read_ahead_job {
  disk_sector_t sector_idx;
  struct list_elem elem;
};

static struct line *cache_load_line(disk_sector_t sector_idx);
static void write_behind_thread(void *aux);
static void read_ahead_thread(void *aux);

static struct line cache[FILESYS_CACHE_MAX];
static int cache_cursor;
static struct lock cache_lock;
static struct list read_ahead_queue;

void cache_init(void)
{
  lock_init(&cache_lock);
  list_init(&read_ahead_queue);
  thread_create("write_behind", PRI_MAX, write_behind_thread, NULL);
  thread_create("read_ahead", PRI_MAX, read_ahead_thread, NULL);
}

void cache_read(disk_sector_t sector_idx,
                void *buffer,
                int sector_ofs,
                int chunk_size)
{
  ASSERT(sector_ofs + chunk_size <= DISK_SECTOR_SIZE);

  lock_acquire(&cache_lock);
  struct line *line = cache_load_line(sector_idx);
  struct read_ahead_job *job = malloc(sizeof(struct read_ahead_job));
  job->sector_idx = sector_idx + 1;
  list_push_front(&read_ahead_queue, &job->elem);
  line->flags |= FILESYS_CACHE_A;
  memcpy(buffer, &line->buffer[sector_ofs], chunk_size);
  lock_release(&cache_lock);
}

void cache_write(disk_sector_t sector_idx,
                 const void *buffer,
                 int sector_ofs,
                 int chunk_size)
{
  ASSERT(sector_ofs + chunk_size <= DISK_SECTOR_SIZE);

  lock_acquire(&cache_lock);
  struct line *line = cache_load_line(sector_idx);
  struct read_ahead_job *job = malloc(sizeof(struct read_ahead_job));
  job->sector_idx = sector_idx + 1;
  list_push_front(&read_ahead_queue, &job->elem);
  line->flags |= FILESYS_CACHE_A | FILESYS_CACHE_D;
  memcpy(&line->buffer[sector_ofs], buffer, chunk_size);
  lock_release(&cache_lock);
}

void cache_flush(void)
{
  struct line *line;
  int i;

  lock_acquire(&cache_lock);
  for (i = 0; i < FILESYS_CACHE_MAX; i++) {
    line = &cache[i];
    if (line->flags & FILESYS_CACHE_P && line->flags & FILESYS_CACHE_D) {
      disk_write(filesys_disk, line->sector_idx, line->buffer);
      line->flags &= ~FILESYS_CACHE_D;
    }
  }
  lock_release(&cache_lock);
}

static struct line *cache_load_line(disk_sector_t sector_idx)
{
  struct line *line;
  int i;

  for (i = 0; i < FILESYS_CACHE_MAX; i++) {
    line = &cache[cache_cursor];
    if (++cache_cursor == FILESYS_CACHE_MAX)
      cache_cursor = 0;
    if (line->flags & FILESYS_CACHE_P && line->sector_idx == sector_idx)
      return line;
  }

  for (i = 0; i < FILESYS_CACHE_MAX; i++) {
    line = &cache[cache_cursor];
    if (++cache_cursor == FILESYS_CACHE_MAX)
      cache_cursor = 0;
    if (~line->flags & FILESYS_CACHE_P)
      goto load;
  }

  for (;;) {
    line = &cache[cache_cursor];
    if (++cache_cursor == FILESYS_CACHE_MAX)
      cache_cursor = 0;
    if (line->flags & FILESYS_CACHE_A)
      line->flags &= ~FILESYS_CACHE_A;
    else
      break;
  }

  if (line->flags & FILESYS_CACHE_D)
    disk_write(filesys_disk, line->sector_idx, line->buffer);

load:
  line->flags = FILESYS_CACHE_P;
  line->sector_idx = sector_idx;
  disk_read(filesys_disk, sector_idx, line->buffer);
  return line;
}

static void write_behind_thread(void *aux UNUSED)
{
  for (;;) {
    timer_sleep(500);
    cache_flush();
  }
}

static void read_ahead_thread(void *aux UNUSED)
{
  for (;;) {
    lock_acquire(&cache_lock);
    if (!list_empty(&read_ahead_queue)) {
      struct list_elem *e = list_pop_back(&read_ahead_queue);
      struct read_ahead_job *job = list_entry(e, struct read_ahead_job, elem);
      cache_load_line(job->sector_idx);
      lock_release(&cache_lock);
      free(job);
    } else {
      lock_release(&cache_lock);
      timer_sleep(1);
    }
  }
}
