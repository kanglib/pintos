#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "vm/swap.h"
#include "filesys/off_t.h"

/* Page states. */
enum page_status {
  PAGE_PRESENT, /* Present in memory. */
  PAGE_SWAPPED, /* Swapped into disk. */
  PAGE_LOADING  /* Loading file. */
};

/* Limit of user stack size. */
#define USER_STACK_LIMIT (8192 * 1024)

/* Page table descriptor. */
struct page {
  enum page_status status;  /* Page state. */
  void *vaddr;              /* Virtual address. */
  union _mapping {          /* Mapping information. */
    void *frame;            /* Frame if page is present. */
    slot_t slot;            /* Swap slot if page is swapped. */
  } mapping;
  struct _load_info {       /* Load information. */
    struct file *file;      /* Loading file. */
    off_t offset;           /* File offset. */
    uint32_t bytes;         /* Bytes to read. */
  } load_info;
  bool is_writable;         /* RO/RW flag. */
  struct hash_elem elem;    /* Hash table element. */
};

bool page_create(void);
void page_destroy(void);
bool page_install(void *upage, void *kpage, bool writable);
void page_remove(struct page *page);
struct page *page_lookup(const void *vaddr);
void page_swap_in(struct page *page, void *frame);
void page_swap_out(struct page *page, uint32_t *pagedir, slot_t slot);
bool page_map(void *upage,
              struct file *file,
              off_t offset,
              uint32_t bytes,
              bool writable);
void page_drop(struct page *page, uint32_t *pagedir);

extern struct lock page_global_lock;

#endif /* vm/page.h */
