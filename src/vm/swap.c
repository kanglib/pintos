#include "vm/swap.h"
#include <bitmap.h>
#include "devices/disk.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

/* Size of a swap slot in disk sectors. */
#define SLOT_SIZE (PGSIZE / DISK_SECTOR_SIZE)

static struct disk *swap_disk;
static struct bitmap *swap_table;
static struct lock swap_lock;

void swap_init(void)
{
  size_t swap_cnt = 0;
  if ((swap_disk = disk_get(1, 1)))
    swap_cnt = disk_size(swap_disk) / SLOT_SIZE;
  swap_table = bitmap_create(swap_cnt);
  lock_init(&swap_lock);
}

slot_t swap_alloc(const void *frame)
{
  slot_t slot;
  int i;

  lock_acquire(&swap_lock);
  if ((slot = bitmap_scan_and_flip(swap_table, 0, 1, false)) == BITMAP_ERROR)
    PANIC("out of swap");
  for (i = 0; i < SLOT_SIZE; i++)
    disk_write(swap_disk, slot * SLOT_SIZE + i, frame + i * DISK_SECTOR_SIZE);
  lock_release(&swap_lock);
  return slot;
}

void swap_free(slot_t slot, void *frame)
{
  ASSERT(slot < bitmap_size(swap_table));

  lock_acquire(&swap_lock);
  bitmap_reset(swap_table, slot);
  if (frame) {
    int i;
    for (i = 0; i < SLOT_SIZE; i++)
      disk_read(swap_disk, slot * SLOT_SIZE + i, frame + i * DISK_SECTOR_SIZE);
  }
  lock_release(&swap_lock);
}
