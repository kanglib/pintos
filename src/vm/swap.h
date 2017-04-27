#ifndef VM_SWAP_H
#define VM_SWAP_H

typedef unsigned int slot_t;

void swap_init(void);
slot_t swap_alloc(void);
void swap_free(slot_t slot);
void swap_read(slot_t slot, void *frame);
void swap_write(slot_t slot, const void *frame);

#endif /* vm/swap.h */
