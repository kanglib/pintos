#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "devices/disk.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");

  inode_init ();
  free_map_init ();
  cache_init();

  if (format) 
    do_format ();

  free_map_open ();

  struct thread *curr = thread_current();
  curr->pwd = dir_open_root();
  inode_inc_pwd_cnt(dir_get_inode(curr->pwd));
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_flush();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
  if (*name == '\0')
    return NULL;
  if (!strcmp(name, "/") || !strcmp(name, ".") || !strcmp(name, ".."))
    return NULL;

  struct dir *dir;
  char file_name[NAME_MAX + 1] = "";
  dir_open_path(name, &dir, file_name);

  disk_sector_t inode_sector = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add(dir, file_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct thread *curr = thread_current();
  if (*name == '\0')
    return NULL;
  if (!strcmp(name, "/"))
    return file_open(inode_open(ROOT_DIR_SECTOR));
  if (!strcmp(name, "."))
    return file_open(inode_reopen(dir_get_inode(curr->pwd)));
  if (!strcmp(name, ".."))
    return file_open(inode_open(inode_get_parent(dir_get_inode(curr->pwd))));

  struct dir *dir;
  char file_name[NAME_MAX + 1] = "";
  bool force_dir = dir_open_path(name, &dir, file_name);

  struct inode *inode = NULL;
  if (dir != NULL)
    dir_lookup(dir, file_name, &inode);
  dir_close (dir);

  if (force_dir && inode_get_type(inode) != FILE_TYPE_DIR) {
    inode_close(inode);
    return NULL;
  }

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  if (*name == '\0')
    return NULL;
  if (!strcmp(name, "/") || !strcmp(name, ".") || !strcmp(name, ".."))
    return NULL;

  struct dir *dir;
  char file_name[NAME_MAX + 1] = "";
  dir_open_path(name, &dir, file_name);

  bool success = dir != NULL && dir_remove(dir, file_name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
