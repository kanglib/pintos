#include "vm/page.h"
#include <debug.h>
#include "threads/malloc.h"
#include "threads/pte.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"

static unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED);
static bool page_less(const struct hash_elem *a_,
                      const struct hash_elem *b_,
                      void *aux UNUSED);
static void page_free(struct hash_elem *e, void *aux UNUSED);

struct lock page_global_lock;

bool page_create(void)
{
  return hash_init(&thread_current()->page_table, page_hash, page_less, NULL);
}

void page_destroy(void)
{
  lock_acquire(&page_global_lock);
  hash_destroy(&thread_current()->page_table, page_free);
  lock_release(&page_global_lock);
}

bool page_install(void *upage, void *kpage, bool writable)
{
  if (!page_lookup(upage)) {
    struct page *p;
    struct thread *t;

    if ((p = malloc(sizeof(struct page))) == NULL)
      return false;
    p->status = PAGE_PRESENT;
    p->vaddr = upage;
    p->mapping.frame = kpage;
    p->load_info.file = NULL;
    p->is_writable = writable;

    t = thread_current();
    if (!pagedir_set_page(t->pagedir, upage, kpage, writable)) {
      free(p);
      return false;
    }
    frame_set_page(kpage, p);
    hash_insert(&t->page_table, &p->elem);
    return true;
  }
  return false;
}

void page_remove(struct page *page)
{
  if (page->status == PAGE_PRESENT)
    frame_free(page->mapping.frame);
  else if (page->status == PAGE_SWAPPED)
    swap_free(page->mapping.slot);
  free(page);
}

struct page *page_lookup(const void *vaddr)
{
  struct page p;
  struct hash_elem *e;

  p.vaddr = (void *) vaddr;
  e = hash_find(&thread_current()->page_table, &p.elem);
  return e ? hash_entry(e, struct page, elem) : NULL;
}

void page_swap_in(struct page *page, void *frame)
{
  page->status = PAGE_PRESENT;
  page->mapping.frame = frame;

  pagedir_set_page(thread_current()->pagedir,
                   page->vaddr,
                   frame,
                   page->is_writable);
  frame_set_page(frame, page);
}

void page_swap_out(struct page *page, uint32_t *pagedir, slot_t slot)
{
  page->status = PAGE_SWAPPED;
  page->mapping.slot = slot;
  pagedir_clear_page(pagedir, page->vaddr);
}

bool page_map(void *upage,
              struct file *file,
              off_t offset,
              uint32_t bytes,
              bool writable)
{
  if (!page_lookup(upage)) {
    struct page *p;
    if ((p = malloc(sizeof(struct page))) == NULL)
      return false;
    p->status = PAGE_LOADING;
    p->vaddr = upage;
    p->load_info.file = file;
    p->load_info.offset = offset;
    p->load_info.bytes = bytes;
    p->is_writable = writable;
    hash_insert(&thread_current()->page_table, &p->elem);
    return true;
  }
  return false;
}

void page_drop(struct page *page, uint32_t *pagedir)
{
  page->status = PAGE_LOADING;
  pagedir_clear_page(pagedir, page->vaddr);
}

static unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED)
{
  struct page *p = hash_entry(p_, struct page, elem);
  return hash_bytes(&p->vaddr, sizeof(p->vaddr));
}

static bool page_less(const struct hash_elem *a_,
                      const struct hash_elem *b_,
                      void *aux UNUSED)
{
  struct page *a = hash_entry(a_, struct page, elem);
  struct page *b = hash_entry(b_, struct page, elem);
  return a->vaddr < b->vaddr;
}

static void page_free(struct hash_elem *e, void *aux UNUSED)
{
  page_remove(hash_entry(e, struct page, elem));
}
