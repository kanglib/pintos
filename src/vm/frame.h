#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>

/* Frame states. */
enum frame_status {
  FRAME_FREE,
  FRAME_USED
};

/* Frame table descriptor. */
struct frame {
  enum frame_status status; /* Frame state. */
  uintptr_t paddr;          /* Physical address. */
  struct page *page;        /* Page if frame is used. */
  uint32_t *pagedir;        /* Page directory if frame is used. */
  struct hash_elem elem;    /* Hash table element. */
};

void frame_init(void);
void *frame_alloc(bool zero);
void frame_free(void *frame);
void frame_set_page(void *frame, struct page *page);

#endif /* vm/frame.h */
