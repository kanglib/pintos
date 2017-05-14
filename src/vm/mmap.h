#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <hash.h>
#include "filesys/file.h"

/* Map region identifier type. */
typedef int mapid_t;

/* Map region descriptor. */
struct mmap {
  mapid_t mapid;          /* Map region identifier. */
  struct file *file;      /* Memory-mapped file. */
  void *start;            /* Starting address. */
  void *end;              /* Ending address. */
  struct hash_elem elem;  /* Hash table element. */
};

bool mmap_create(void);
void mmap_destroy(void);
mapid_t mmap_map(struct file *file, void *addr);
void mmap_unmap(mapid_t mapping);

#endif /* vm/mmap.h */
