#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stddef.h>

typedef size_t slot_t;

void swap_init(void);
slot_t swap_alloc(const void *frame);
void swap_free(slot_t slot, void *frame);

#endif /* vm/swap.h */
