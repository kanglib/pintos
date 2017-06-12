#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    enum file_type type;                /* File type. */
    off_t length;                       /* File size in bytes. */
    disk_sector_t parent;               /* Parent directory pointer. */
    unsigned magic;                     /* Magic number. */
    disk_sector_t pointers[14];         /* Block pointers. */
    uint32_t unused[110];               /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    int pwd_cnt;                        /* 0: remove ok, >0: deny remove. */
    struct lock mutex;                  /* Synchronization. */
  };

#define TABLE_SIZE (DISK_SECTOR_SIZE / (int) sizeof(disk_sector_t))

/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode_length(inode)) {
    disk_sector_t sector_idx;
    int index = pos / DISK_SECTOR_SIZE;
    if (index < 12) {
      cache_read(inode->sector,
                 &sector_idx,
                 offsetof(struct inode_disk, pointers[index]),
                 sizeof(disk_sector_t));
      if (!sector_idx)
        return -1;
    } else if (index < 12 + TABLE_SIZE) {
      cache_read(inode->sector,
                 &sector_idx,
                 offsetof(struct inode_disk, pointers[12]),
                 sizeof(disk_sector_t));
      if (!sector_idx)
        return -1;
      cache_read(sector_idx,
                 &sector_idx,
                 (index - 12) * sizeof(disk_sector_t),
                 sizeof(disk_sector_t));
      if (!sector_idx)
        return -1;
    } else if (index < 12 + TABLE_SIZE + TABLE_SIZE * TABLE_SIZE) {
      cache_read(inode->sector,
                 &sector_idx,
                 offsetof(struct inode_disk, pointers[13]),
                 sizeof(disk_sector_t));
      if (!sector_idx)
        return -1;
      cache_read(sector_idx,
                 &sector_idx,
                 (index - 12 - TABLE_SIZE) / TABLE_SIZE * sizeof(disk_sector_t),
                 sizeof(disk_sector_t));
      if (!sector_idx)
        return -1;
      cache_read(sector_idx,
                 &sector_idx,
                 (index - 12 - TABLE_SIZE) % TABLE_SIZE * sizeof(disk_sector_t),
                 sizeof(disk_sector_t));
      if (!sector_idx)
        return -1;
    } else {
      return -1;
    }
    return sector_idx;
  } else {
    return -1;
  }
}

static bool extend_one_block(struct inode *inode, off_t incr)
{
  disk_sector_t sector_idx;
  disk_sector_t table1;
  disk_sector_t table2;

  int index = (inode_length(inode) + DISK_SECTOR_SIZE - 1) / DISK_SECTOR_SIZE;
  if (index < 12) {
    if (!free_map_allocate(1, &sector_idx))
      return false;
    cache_write(inode->sector,
                &sector_idx,
                offsetof(struct inode_disk, pointers[index]),
                sizeof(disk_sector_t));
  } else if (index < 12 + TABLE_SIZE) {
    cache_read(inode->sector,
               &table1,
               offsetof(struct inode_disk, pointers[12]),
               sizeof(disk_sector_t));
    if (!table1) {
      if (!free_map_allocate(1, &table1))
        return false;
      cache_write(inode->sector,
                  &table1,
                  offsetof(struct inode_disk, pointers[12]),
                  sizeof(disk_sector_t));
    }
    if (!free_map_allocate(1, &sector_idx))
      return false;
    cache_write(table1,
                &sector_idx,
                (index - 12) * sizeof(disk_sector_t),
                sizeof(disk_sector_t));
  } else if (index < 12 + TABLE_SIZE + TABLE_SIZE * TABLE_SIZE) {
    cache_read(inode->sector,
               &table2,
               offsetof(struct inode_disk, pointers[13]),
               sizeof(disk_sector_t));
    if (!table2) {
      if (!free_map_allocate(1, &table2))
        return false;
      cache_write(inode->sector,
                  &table2,
                  offsetof(struct inode_disk, pointers[13]),
                  sizeof(disk_sector_t));
    }
    cache_read(table2,
               &table1,
               (index - 12 - TABLE_SIZE) / TABLE_SIZE * sizeof(disk_sector_t),
               sizeof(disk_sector_t));
    if (!table1) {
      if (!free_map_allocate(1, &table1))
        return false;
      cache_write(table2,
                  &table1,
                  (index - 12 - TABLE_SIZE) / TABLE_SIZE * sizeof(disk_sector_t),
                  sizeof(disk_sector_t));
    }
    if (!free_map_allocate(1, &sector_idx))
      return false;
    cache_write(table1,
                &sector_idx,
                (index - 12 - TABLE_SIZE) % TABLE_SIZE * sizeof(disk_sector_t),
                sizeof(disk_sector_t));
  } else {
    return false;
  }

  off_t new_length = inode_length(inode) + incr;
  cache_write(inode->sector,
              &new_length,
              offsetof(struct inode_disk, length),
              sizeof(off_t));

  static char zeros[DISK_SECTOR_SIZE];
  cache_write(sector_idx, zeros, 0, DISK_SECTOR_SIZE);
  return true;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

static struct lock open_inodes_mutex;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  lock_init(&open_inodes_mutex);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (disk_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->magic = INODE_MAGIC;
      cache_write(sector, disk_inode, 0, DISK_SECTOR_SIZE);
      struct inode *inode = inode_open(sector);
      success = true;
      size_t i;
      for (i = 0; i < sectors; i++) {
        off_t incr = (length >= DISK_SECTOR_SIZE) ? DISK_SECTOR_SIZE : length;
        if (!extend_one_block(inode, incr)) {
          inode_remove(inode);
          success = false;
          break;
        }
        length -= incr;
      }
      inode_close(inode);
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  lock_acquire(&open_inodes_mutex);
  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          lock_release(&open_inodes_mutex);
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL) {
    lock_release(&open_inodes_mutex);
    return NULL;
  }

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  lock_release(&open_inodes_mutex);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  inode->pwd_cnt = 0;
  lock_init(&inode->mutex);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
    bool flag = lock_held_by_current_thread(&inode->mutex);
    if (!flag)
      lock_acquire(&inode->mutex);
    inode->open_cnt++;
    if (!flag)
      lock_release(&inode->mutex);
  }
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          disk_sector_t sector_idx;
          disk_sector_t table1;
          disk_sector_t table2;

          cache_read(inode->sector,
                     &table2,
                     offsetof(struct inode_disk, pointers[13]),
                     sizeof(disk_sector_t));
          if (table2) {
            int i;
            for (i = 0; i < TABLE_SIZE; i++) {
              cache_read(table2,
                         &table1,
                         i * sizeof(disk_sector_t),
                         sizeof(disk_sector_t));
              if (table1) {
                int j;
                for (j = 0; j < TABLE_SIZE; j++) {
                  cache_read(table1,
                             &sector_idx,
                             j * sizeof(disk_sector_t),
                             sizeof(disk_sector_t));
                  if (sector_idx)
                    free_map_release(sector_idx, 1);
                  else
                    break;
                }
                free_map_release(table1, 1);
              } else {
                break;
              }
            }
            free_map_release(table2, 1);
          }

          cache_read(inode->sector,
                     &table1,
                     offsetof(struct inode_disk, pointers[12]),
                     sizeof(disk_sector_t));
          if (table1) {
            int i;
            for (i = 0; i < TABLE_SIZE; i++) {
              cache_read(table1,
                         &sector_idx,
                         i * sizeof(disk_sector_t),
                         sizeof(disk_sector_t));
              if (sector_idx)
                free_map_release(sector_idx, 1);
              else
                break;
            }
            free_map_release(table1, 1);
          }

          int i;
          for (i = 0; i < 12; i++) {
            cache_read(inode->sector,
                       &sector_idx,
                       offsetof(struct inode_disk, pointers[i]),
                       sizeof(disk_sector_t));
            if (sector_idx)
              free_map_release(sector_idx, 1);
            else
              break;
          }
          free_map_release (inode->sector, 1);
        }

      free (inode); 
      inode = NULL;
    }
  if (inode && !flag)
    lock_release(&inode->mutex);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  inode->removed = true;
  if (!flag)
    lock_release(&inode->mutex);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  if (!flag)
    lock_release(&inode->mutex);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  if (inode->deny_write_cnt) {
    if (!flag)
      lock_release(&inode->mutex);
    return 0;
  }

  while (size > 0) 
    {
      off_t length = inode_length(inode);

      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = length - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0) {
        if (size > 0) {
          off_t left = offset + size - length;
          off_t slack = DISK_SECTOR_SIZE - length % DISK_SECTOR_SIZE;
          if (slack != DISK_SECTOR_SIZE) {
            off_t new_length = length + ((left >= slack) ? slack : left);
            cache_write(inode->sector,
                        &new_length,
                        offsetof(struct inode_disk, length),
                        sizeof(off_t));
          } else {
            while (left > 0) {
              off_t incr = (left >= DISK_SECTOR_SIZE) ? DISK_SECTOR_SIZE : left;
              if (!extend_one_block(inode, incr))
                return bytes_written;
              left -= incr;
            }
          }
          continue;
        } else {
          break;
        }
      }

      cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  if (!flag)
    lock_release(&inode->mutex);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  if (!flag)
    lock_release(&inode->mutex);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
  if (!flag)
    lock_release(&inode->mutex);
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  off_t length;
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire((void *) &inode->mutex);
  cache_read(inode->sector,
             &length,
             offsetof(struct inode_disk, length),
             sizeof(off_t));
  if (!flag)
    lock_release((void *) &inode->mutex);
  return length;
}

enum file_type inode_get_type(struct inode *inode)
{
  enum file_type type;
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  cache_read(inode->sector,
             &type,
             offsetof(struct inode_disk, type),
             sizeof(enum file_type));
  if (!flag)
    lock_release(&inode->mutex);
  return type;
}

void inode_set_type(struct inode *inode, enum file_type type)
{
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  cache_write(inode->sector,
              &type,
              offsetof(struct inode_disk, type),
              sizeof(enum file_type));
  if (!flag)
    lock_release(&inode->mutex);
}

disk_sector_t inode_get_parent(struct inode *child)
{
  disk_sector_t parent;
  bool flag = lock_held_by_current_thread(&child->mutex);
  if (!flag)
    lock_acquire(&child->mutex);
  cache_read(child->sector,
             &parent,
             offsetof(struct inode_disk, parent),
             sizeof(disk_sector_t));
  if (!flag)
    lock_release(&child->mutex);
  return parent;
}

void inode_set_parent(struct inode *child, struct inode *parent)
{
  disk_sector_t pointer = parent->sector;
  bool flag = lock_held_by_current_thread(&child->mutex);
  if (!flag)
    lock_acquire(&child->mutex);
  cache_write(child->sector,
              &pointer,
              offsetof(struct inode_disk, parent),
              sizeof(disk_sector_t));
  if (!flag)
    lock_release(&child->mutex);
}

bool inode_is_open(struct inode *inode)
{
  int open_cnt;
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  open_cnt = inode->open_cnt;
  if (!flag)
    lock_release(&inode->mutex);
  return open_cnt != 1;
}

int inode_get_pwd_cnt(struct inode *inode)
{
  int pwd_cnt;
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  pwd_cnt = inode->pwd_cnt;
  if (!flag)
    lock_release(&inode->mutex);
  return pwd_cnt;
}

void inode_inc_pwd_cnt(struct inode *inode)
{
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  inode->pwd_cnt++;
  if (!flag)
    lock_release(&inode->mutex);
}

void inode_dec_pwd_cnt(struct inode *inode)
{
  bool flag = lock_held_by_current_thread(&inode->mutex);
  if (!flag)
    lock_acquire(&inode->mutex);
  inode->pwd_cnt--;
  if (!flag)
    lock_release(&inode->mutex);
}
