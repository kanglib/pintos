#include "vm/mmap.h"
#include <debug.h>
#include "threads/malloc.h"
#include "threads/pte.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/syscall.h"
#include "vm/page.h"

static void mmap_remove(struct mmap *mmap);
static struct mmap *mmap_lookup(mapid_t mapid);
static unsigned mmap_hash(const struct hash_elem *m_, void *aux UNUSED);
static bool mmap_less(const struct hash_elem *a_,
                      const struct hash_elem *b_,
                      void *aux UNUSED);
static void mmap_free(struct hash_elem *e, void *aux UNUSED);

bool mmap_create(void)
{
  return hash_init(&thread_current()->mmap_table, mmap_hash, mmap_less, NULL);
}

void mmap_destroy(void)
{
  hash_destroy(&thread_current()->mmap_table, mmap_free);
}

mapid_t mmap_map(struct file *file, void *addr)
{
  struct thread *curr;
  struct mmap *m;
  off_t off;
  void *end;
  void *p;
  uint32_t bytes;

  if (!addr || pg_round_down(addr) != addr)
    return -1;

  lock_acquire(&fs_lock);
  file = file_reopen(file);
  off = file_length(file);
  lock_release(&fs_lock);
  end = pg_round_up(addr + off);
  for (p = addr; p < end; p += PGSIZE)
    if (page_lookup(p))
      return -1;

  if ((m = malloc(sizeof(struct mmap))) == NULL)
    return -1;

  curr = thread_current();
  bytes = off;
  off = 0;
  for (p = addr; p < end; p += PGSIZE) {
    if (!page_map(p, file, off, (bytes >= PGSIZE) ? PGSIZE : bytes, true)) {
      free(m);
      lock_acquire(&fs_lock);
      file_close(file);
      lock_release(&fs_lock);
      curr->exit_code = -1;
      thread_exit();
    }
    off += PGSIZE;
    bytes -= PGSIZE;
  }

  m->mapid = curr->mmap_n++;
  m->file = file;
  m->start = addr;
  m->end = end;
  hash_insert(&curr->mmap_table, &m->elem);
  return m->mapid;
}

void mmap_unmap(mapid_t mapping)
{
  struct mmap *m;

  if ((m = mmap_lookup(mapping))) {
    hash_delete(&thread_current()->mmap_table, &m->elem);
    mmap_remove(m);
  }
}

static void mmap_remove(struct mmap *mmap)
{
  struct thread *curr;
  void *p;

  curr = thread_current();
  for (p = mmap->start; p < mmap->end; p += PGSIZE) {
    struct page *page;

    lock_acquire(&curr->page_table_lock);
    page = page_lookup(p);
    if (page->status == PAGE_PRESENT) {
      if (pagedir_is_dirty(curr->pagedir, p)) {
        lock_acquire(&fs_lock);
        file_seek(page->load_info.file, page->load_info.offset);
        file_write(page->load_info.file,
            page->mapping.frame,
            page->load_info.bytes);
        lock_release(&fs_lock);
      }
      pagedir_clear_page(curr->pagedir, page->vaddr);
    }

    hash_delete(&curr->page_table, &page->elem);
    page_remove(page);
    lock_release(&curr->page_table_lock);
  }

  lock_acquire(&fs_lock);
  file_close(mmap->file);
  lock_release(&fs_lock);
  free(mmap);
}

static struct mmap *mmap_lookup(mapid_t mapid)
{
  struct mmap m;
  struct hash_elem *e;

  m.mapid = mapid;
  e = hash_find(&thread_current()->mmap_table, &m.elem);
  return e ? hash_entry(e, struct mmap, elem) : NULL;
}

static unsigned mmap_hash(const struct hash_elem *m_, void *aux UNUSED)
{
  struct mmap *m = hash_entry(m_, struct mmap, elem);
  return hash_bytes(&m->mapid, sizeof(m->mapid));
}

static bool mmap_less(const struct hash_elem *a_,
                      const struct hash_elem *b_,
                      void *aux UNUSED)
{
  struct mmap *a = hash_entry(a_, struct mmap, elem);
  struct mmap *b = hash_entry(b_, struct mmap, elem);
  return a->mapid < b->mapid;
}

static void mmap_free(struct hash_elem *e, void *aux UNUSED)
{
  mmap_remove(hash_entry(e, struct mmap, elem));
}
