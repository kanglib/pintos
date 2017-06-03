#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/disk.h"

#define FILESYS_CACHE_MAX 64

#define FILESYS_CACHE_P 1
#define FILESYS_CACHE_A 2
#define FILESYS_CACHE_D 4

void cache_init(void);
void cache_read(disk_sector_t sector_idx,
                void *buffer,
                int sector_ofs,
                int chunk_size);
void cache_write(disk_sector_t sector_idx,
                 const void *buffer,
                 int sector_ofs,
                 int chunk_size);
void cache_flush(void);

#endif /* filesys/cache.h */
