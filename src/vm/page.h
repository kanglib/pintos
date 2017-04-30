#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "vm/swap.h"

/* Page states. */
enum page_status {
  PAGE_PRESENT, /* Present in memory. */
  PAGE_SWAPPED  /* Swapped into disk. */
};

/* Page table descriptor. */
struct page {
  enum page_status status;  /* Page state. */
  void *vaddr;              /* Virtual address. */
  union _mapping {
    void *frame;            /* Frame if page is present. */
    slot_t slot;            /* Swap slot if page is swapped. */
  } mapping;
  bool is_writable;         /* RO/RW flag. */
  struct hash_elem elem;    /* Hash table element. */
};

bool page_create(void);
void page_destroy(void);
bool page_install(void *upage, void *kpage, bool writable);
struct page *page_lookup(const void *vaddr);
void page_swap_in(struct page *page, void *frame);
void page_swap_out(struct page *page, uint32_t *pagedir, slot_t slot);

#endif /* vm/page.h */
