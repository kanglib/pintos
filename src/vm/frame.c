#include "vm/frame.h"
#include <string.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

static struct frame *frame_lookup(const uintptr_t paddr);
static unsigned frame_hash(const struct hash_elem *f_, void *aux UNUSED);
static bool frame_less(const struct hash_elem *a_,
                       const struct hash_elem *b_,
                       void *aux UNUSED);

static struct hash frame_table;
static struct hash_iterator frame_table_iter;
static struct lock frame_table_lock;
static size_t frame_count;

void frame_init(void)
{
  hash_init(&frame_table, frame_hash, frame_less, NULL);
  for (;;) {
    uintptr_t paddr;
    struct frame *f;

    if ((paddr = (uintptr_t) palloc_get_page(PAL_USER)) == 0)
      break;
    f = malloc(sizeof(struct frame));
    f->status = FRAME_FREE;
    f->paddr = paddr;
    hash_insert(&frame_table, &f->elem);
    frame_count++;
  }

  hash_first(&frame_table_iter, &frame_table);
  hash_next(&frame_table_iter);
  lock_init(&frame_table_lock);
}

void *frame_alloc(bool zero)
{
  size_t i;

  lock_acquire(&frame_table_lock);
  for (i = 0; i < frame_count; i++) {
    struct frame *f = hash_entry(hash_cur(&frame_table_iter),
                                 struct frame,
                                 elem);
    if (!hash_next(&frame_table_iter)) {
      hash_first(&frame_table_iter, &frame_table);
      hash_next(&frame_table_iter);
    }
    if (f->status == FRAME_FREE) {
      f->status = FRAME_USED;
      lock_release(&frame_table_lock);
      if (zero)
        memset((void *) f->paddr, 0, PGSIZE);
      return (void *) f->paddr;
    }
  }

  /* TODO */
  lock_release(&frame_table_lock);
  return NULL;
}

void frame_free(void *frame)
{
  struct frame *f;

  lock_acquire(&frame_table_lock);
  if ((f = frame_lookup((uintptr_t) frame)))
    f->status = FRAME_FREE;
  lock_release(&frame_table_lock);
}

static struct frame *frame_lookup(const uintptr_t paddr)
{
  struct frame f;
  struct hash_elem *e;

  f.paddr = paddr;
  e = hash_find(&frame_table, &f.elem);
  return e ? hash_entry(e, struct frame, elem) : NULL;
}

static unsigned frame_hash(const struct hash_elem *f_, void *aux UNUSED)
{
  struct frame *f = hash_entry(f_, struct frame, elem);
  return hash_bytes(&f->paddr, sizeof(f->paddr));
}

static bool frame_less(const struct hash_elem *a_,
                       const struct hash_elem *b_,
                       void *aux UNUSED)
{
  struct frame *a = hash_entry(a_, struct frame, elem);
  struct frame *b = hash_entry(b_, struct frame, elem);
  return a->paddr < b->paddr;
}
