#include "vm/frame.h"
#include <string.h>
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/page.h"
#include "vm/swap.h"

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
    f->pagedir = NULL;
    f->page = NULL;
    hash_insert(&frame_table, &f->elem);
    frame_count++;
  }

  hash_first(&frame_table_iter, &frame_table);
  hash_next(&frame_table_iter);
  lock_init(&frame_table_lock);
}

void *frame_alloc(bool zero)
{
  struct frame *f = NULL;
  struct thread *curr;
  slot_t slot;
  size_t i;

  lock_acquire(&frame_table_lock);

  for (i = 0; i < frame_count; i++) {
    f = hash_entry(hash_cur(&frame_table_iter), struct frame, elem);
    if (!hash_next(&frame_table_iter)) {
      hash_first(&frame_table_iter, &frame_table);
      hash_next(&frame_table_iter);
    }
    if (f->status == FRAME_FREE) {
      f->status = FRAME_USED;
      goto done;
    }
  }

  for (;;) {
    f = hash_entry(hash_cur(&frame_table_iter), struct frame, elem);
    if (!hash_next(&frame_table_iter)) {
      hash_first(&frame_table_iter, &frame_table);
      hash_next(&frame_table_iter);
    }
    if (pagedir_is_accessed(f->pagedir, f->page->vaddr))
      pagedir_set_accessed(f->pagedir, f->page->vaddr, false);
    else
      break;
  }

  if (f->page->load_info.file && f->page->load_info.bytes) {
    if (pagedir_is_dirty(f->pagedir, f->page->vaddr)) {
      lock_acquire(&fs_lock);
      file_seek(f->page->load_info.file, f->page->load_info.offset);
      file_write(f->page->load_info.file,
                 (void *) f->paddr,
                 f->page->load_info.bytes);
      lock_release(&fs_lock);
    }
    page_drop(f->page, f->pagedir);
    goto done;
  }

  curr = thread_current();
  lock_acquire(&curr->page_table_lock);
  slot = swap_alloc((void *) f->paddr);
  page_swap_out(f->page, f->pagedir, slot);
  lock_release(&curr->page_table_lock);

done:
  lock_release(&frame_table_lock);

  if (zero)
    memset((void *) f->paddr, 0, PGSIZE);
  return (void *) f->paddr;
}

void frame_free(void *frame)
{
  struct frame *f;
  lock_acquire(&frame_table_lock);
  if ((f = frame_lookup((uintptr_t) frame)))
    f->status = FRAME_FREE;
  lock_release(&frame_table_lock);
}

void frame_set_page(void *frame, struct page *page)
{
  struct frame *f;
  lock_acquire(&frame_table_lock);
  if ((f = frame_lookup((uintptr_t) frame))) {
    f->pagedir = thread_current()->pagedir;
    f->page = page;
  }
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
