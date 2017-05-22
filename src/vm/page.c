#include "vm/page.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

struct lock page_global_lock;

static unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED);
static bool page_less(const struct hash_elem *a_,
                      const struct hash_elem *b_,
                      void *aux UNUSED);
static void page_free(struct hash_elem *e, void *aux UNUSED);

bool page_create(void)
{
  return hash_init(&thread_current()->page_table, page_hash, page_less, NULL);
}

void page_destroy(void)
{
  hash_destroy(&thread_current()->page_table, page_free);
}

bool page_install(void *upage, void *kpage, bool writable)
{
  if (!page_lookup(upage)) {
    struct thread *curr;
    struct page *p;

    curr = thread_current();
    if ((p = malloc(sizeof(struct page))) == NULL)
      return false;
    p->status = PAGE_PRESENT;
    p->vaddr = upage;
    p->mapping.frame = kpage;
    p->load_info.file = NULL;
    p->is_writable = writable;
    hash_insert(&curr->page_table, &p->elem);

    if (!pagedir_set_page(curr->pagedir, upage, kpage, writable)) {
      hash_delete(&curr->page_table, &p->elem);
      free(p);
      return false;
    }
    frame_set_page(kpage, p);
    return true;
  }
  return false;
}

void page_remove(struct page *page)
{
  if (page->status == PAGE_PRESENT)
    frame_free(page->mapping.frame);
  else if (page->status == PAGE_SWAPPED)
    swap_free(page->mapping.slot, NULL);
  free(page);
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

bool page_map(void *upage,
              struct file *file,
              off_t offset,
              uint32_t bytes,
              bool writable)
{

  if (!page_lookup(upage)) {
    struct thread *curr;
    struct page *p;

    curr = thread_current();
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

struct page *page_lookup(const void *vaddr)
{
  struct page p;
  struct hash_elem *e;

  p.vaddr = (void *) vaddr;
  e = hash_find(&thread_current()->page_table, &p.elem);
  return e ? hash_entry(e, struct page, elem) : NULL;
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
