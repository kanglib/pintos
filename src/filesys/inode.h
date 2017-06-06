#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "filesys/file.h"
#include "devices/disk.h"

struct bitmap;

void inode_init (void);
bool inode_create (disk_sector_t, off_t);
struct inode *inode_open (disk_sector_t);
struct inode *inode_reopen (struct inode *);
disk_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
enum file_type inode_get_type(const struct inode *inode);
void inode_set_type(struct inode *inode, enum file_type type);
disk_sector_t inode_get_parent(const struct inode *child);
void inode_set_parent(struct inode *child, const struct inode *parent);
bool inode_is_open(struct inode *inode);
int inode_get_pwd_cnt(struct inode *inode);
void inode_inc_pwd_cnt(struct inode *inode);
void inode_dec_pwd_cnt(struct inode *inode);

#endif /* filesys/inode.h */
