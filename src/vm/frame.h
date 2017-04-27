#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Frame table descriptor. */
struct frame {
  uintptr_t paddr_base;
};

void frame_init(size_t page_cnt);
void *falloc_get_frame(bool zero);
void falloc_free_frame(void *frame);

#endif /* vm/frame.h */
