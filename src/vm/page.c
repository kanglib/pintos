#include "vm/page.h"
#include <debug.h>
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

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
    struct page *p;
    struct thread *t;

    if ((p = malloc(sizeof(struct page))) == NULL)
      return false;
    p->status = PAGE_PRESENT;
    p->vaddr = upage;
    p->mapping.frame = kpage;
    p->is_writable = writable;

    t = thread_current();
    if (!pagedir_set_page(t->pagedir, upage, kpage, writable)) {
      free(p);
      return false;
    }
    hash_insert(&t->page_table, &p->elem);
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
  free(hash_entry(e, struct page, elem));
}
