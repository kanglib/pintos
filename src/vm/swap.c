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
static struct lock mutex;

void swap_init(void)
{
  size_t swap_cnt = 0;

  if ((swap_disk = disk_get(1, 1)))
    swap_cnt = disk_size(swap_disk) / SLOT_SIZE;
  swap_table = bitmap_create(swap_cnt);
  lock_init(&swap_table_lock);

  sema_init(&sema1, 0);
  sema_init(&sema2, 0);
  lock_init(&mutex);
  thread_create("swap_in_job", PRI_MAX, swap_in_job_thread, NULL);
}

slot_t swap_alloc(void)
{
  slot_t slot;

  lock_acquire(&swap_table_lock);
  slot = bitmap_scan_and_flip(swap_table, 0, 1, false);
  lock_release(&swap_table_lock);
  if (slot == BITMAP_ERROR)
    PANIC("out of swap");
  return slot;
}

void swap_free(slot_t slot)
{
  ASSERT(slot < bitmap_size(swap_table));

  lock_acquire(&swap_table_lock);
  bitmap_reset(swap_table, slot);
  lock_release(&swap_table_lock);
}

void swap_read(slot_t slot, void *frame)
{
  int i;
  for (i = 0; i < SLOT_SIZE; i++)
    disk_read(swap_disk, slot * SLOT_SIZE + i, frame + i * DISK_SECTOR_SIZE);
}

void swap_read_intr(slot_t slot, void *frame)
{
  lock_acquire(&mutex);
  swap_in_job.slot = slot;
  swap_in_job.frame = frame;
  sema_up(&sema1);
  sema_down(&sema2);
  lock_release(&mutex);
}

void swap_write(slot_t slot, const void *frame)
{
  int i;
  for (i = 0; i < SLOT_SIZE; i++)
    disk_write(swap_disk, slot * SLOT_SIZE + i, frame + i * DISK_SECTOR_SIZE);
}

static void swap_in_job_thread(void *aux UNUSED)
{
  for (;;) {
    sema_down(&sema1);
    swap_read(swap_in_job.slot, swap_in_job.frame);
    sema_up(&sema2);
  }
}
