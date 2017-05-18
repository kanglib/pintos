#include "vm/swap.h"
#include <bitmap.h>
#include <stddef.h>
#include "devices/disk.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

/* Size of a swap slot in disk sectors. */
#define SLOT_SIZE (PGSIZE / DISK_SECTOR_SIZE)

static void swap_in_job_thread(void *aux);

static struct disk *swap_disk;
static struct bitmap *swap_table;
static struct lock swap_table_lock;

static struct _swap_in_job {
  slot_t slot;
  void *frame;
} swap_in_job;

static struct semaphore sema1;
static struct semaphore sema2;

void swap_init(void)
{
  size_t swap_cnt = 0;

  if ((swap_disk = disk_get(1, 1)))
    swap_cnt = disk_size(swap_disk) / SLOT_SIZE;
  swap_table = bitmap_create(swap_cnt);
  lock_init(&swap_table_lock);

  sema_init(&sema1, 0);
  sema_init(&sema2, 0);
  thread_create("swap_in_job", PRI_MAX, swap_in_job_thread, NULL);
}

slot_t swap_alloc(const void *frame)
{
  slot_t slot;
  int i;

  lock_acquire(&swap_table_lock);
  if ((slot = bitmap_scan_and_flip(swap_table, 0, 1, false)) == BITMAP_ERROR)
    PANIC("out of swap");
  for (i = 0; i < SLOT_SIZE; i++)
    disk_write(swap_disk, slot * SLOT_SIZE + i, frame + i * DISK_SECTOR_SIZE);
  lock_release(&swap_table_lock);
  return slot;
}

void swap_free(slot_t slot, void *frame)
{
  ASSERT(slot < bitmap_size(swap_table));

  lock_acquire(&swap_table_lock);
  bitmap_reset(swap_table, slot);
  if (frame) {
    swap_in_job.slot = slot;
    swap_in_job.frame = frame;
    sema_up(&sema1);
    sema_down(&sema2);
  }
  lock_release(&swap_table_lock);
}

static void swap_in_job_thread(void *aux UNUSED)
{
  for (;;) {
    int i;
    sema_down(&sema1);
    for (i = 0; i < SLOT_SIZE; i++)
      disk_read(swap_disk,
                swap_in_job.slot * SLOT_SIZE + i,
                swap_in_job.frame + i * DISK_SECTOR_SIZE);
    sema_up(&sema2);
  }
}
