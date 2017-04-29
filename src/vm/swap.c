#include "vm/swap.h"
#include <bitmap.h>
#include <stddef.h>
#include "devices/disk.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define SLOT_SIZE ((PGSIZE) / (DISK_SECTOR_SIZE))

static struct disk *swap_disk;
static struct bitmap *swap_table;
static struct lock swap_table_lock;

void swap_init(void)
{
  size_t swap_cnt = 0;

  if ((swap_disk = disk_get(1, 1)))
    swap_cnt = disk_size(swap_disk) / SLOT_SIZE;
  swap_table = bitmap_create(swap_cnt);
  lock_init(&swap_table_lock);
}

slot_t swap_alloc(void)
{
  slot_t slot;

  lock_acquire(&swap_table_lock);
  slot = bitmap_scan_and_flip(swap_table, 0, 1, false);
  if (slot == BITMAP_ERROR)
    PANIC("swap_alloc: out of swap");
  lock_release(&swap_table_lock);
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

  lock_acquire(&swap_table_lock);
  for (i = 0; i < SLOT_SIZE; i++)
    disk_read(swap_disk, slot * SLOT_SIZE + i, frame);
  lock_release(&swap_table_lock);
}

void swap_read_intr(slot_t slot, void *frame)
{
  /*TODO*/
  swap_read(slot, frame);
}

void swap_write(slot_t slot, const void *frame)
{
  int i;

  lock_acquire(&swap_table_lock);
  for (i = 0; i < SLOT_SIZE; i++)
    disk_write(swap_disk, slot * SLOT_SIZE + i, frame);
  lock_release(&swap_table_lock);
}
