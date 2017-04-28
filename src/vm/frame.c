#include "vm/frame.h"
#include <string.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/swap.h"

static struct frame *frame_table;
static size_t frame_cnt;
static int frame_cursor;
static struct lock frame_table_lock;

void frame_init(size_t page_cnt)
{
  size_t i;

  frame_cnt = page_cnt;
  frame_table = malloc(frame_cnt * sizeof(struct frame));
  for (i = 0; i < frame_cnt; i++)
    frame_table[i].paddr_base = (uintptr_t) palloc_get_page(PAL_USER);
  lock_init(&frame_table_lock);

  swap_init();
}

void *falloc_get_frame(bool zero)
{
  size_t i;

  lock_acquire(&frame_table_lock);
  for (i = 0; i < frame_cnt; i++) {
    uintptr_t *frame = &frame_table[frame_cursor].paddr_base;
    if (~*frame & 1) {
      *frame |= 1;
      lock_release(&frame_table_lock);
      if (zero)
        memset((void *) (*frame & ~1), 0, PGSIZE);
      return (void *) (*frame & ~1);
    }
    frame_cursor = (frame_cursor + 1) % frame_cnt;
  }
  lock_release(&frame_table_lock);
  return NULL;
}

void falloc_free_frame(void *frame)
{
  size_t i;

  lock_acquire(&frame_table_lock);
  for (i = 0; i < frame_cnt; i++) {
    if ((frame_table[i].paddr_base & ~1) == (uintptr_t) frame) {
      frame_table[i].paddr_base &= ~1;
      break;
    }
  }
  lock_release(&frame_table_lock);
}
