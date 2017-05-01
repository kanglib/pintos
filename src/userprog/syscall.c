#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include <list.h>
#include <string.h>
#include "devices/input.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/pte.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#ifdef VM
#include "vm/frame.h"
#include "vm/page.h"
#endif

/* Maximum number of system call arguments. */
#define MAX_ARGS 3

static void syscall_handler (struct intr_frame *);

static void handle_exit(int status) NO_RETURN;
static pid_t handle_exec(const char *cmd_line);
static int handle_wait(pid_t pid);
static bool handle_create(const char *file, unsigned initial_size);
static bool handle_remove(const char *file);
static int handle_open(const char *file);
static int handle_filesize(int fd);
static int handle_read(int fd, void *buffer, unsigned size, const void *esp);
static int handle_write(int fd, const void *buffer, unsigned size);
static void handle_seek(int fd, unsigned position);
static unsigned handle_tell(int fd);
static void handle_close(int fd);

static bool is_valid_vaddr(const void *vaddr, unsigned size);
static bool is_writable_vaddr(const void *vaddr, unsigned size);
static void read_args(const long *esp, long *args, int count);
#ifdef VM
static void grow_stack(const void *vaddr, unsigned size, const void *esp);
#endif

/* External lock for base file system. */
struct lock fs_lock;

void
syscall_init (void) 
{
  lock_init(&fs_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f)
{
  long args[MAX_ARGS];

  if (!is_valid_vaddr(f->esp, sizeof(int)))
    handle_exit(-1);

  switch (*(int *) f->esp) {
  case SYS_HALT:
    power_off();
    NOT_REACHED();
  case SYS_EXIT:
    read_args(f->esp, args, 1);
    handle_exit(args[0]);
    NOT_REACHED();
  case SYS_EXEC:
    read_args(f->esp, args, 1);
    f->eax = handle_exec((char *) args[0]);
    break;
  case SYS_WAIT:
    read_args(f->esp, args, 1);
    f->eax = handle_wait(args[0]);
    break;
  case SYS_CREATE:
    read_args(f->esp, args, 2);
    f->eax = handle_create((char *) args[0], args[1]);
    break;
  case SYS_REMOVE:
    read_args(f->esp, args, 1);
    f->eax = handle_remove((char *) args[0]);
    break;
  case SYS_OPEN:
    read_args(f->esp, args, 1);
    f->eax = handle_open((char *) args[0]);
    break;
  case SYS_FILESIZE:
    read_args(f->esp, args, 1);
    f->eax = handle_filesize(args[0]);
    break;
  case SYS_READ:
    read_args(f->esp, args, 3);
    f->eax = handle_read(args[0], (void *) args[1], args[2], f->esp);
    break;
  case SYS_WRITE:
    read_args(f->esp, args, 3);
    f->eax = handle_write(args[0], (void *) args[1], args[2]);
    break;
  case SYS_SEEK:
    read_args(f->esp, args, 2);
    handle_seek(args[0], args[1]);
    break;
  case SYS_TELL:
    read_args(f->esp, args, 1);
    f->eax = handle_tell(args[0]);
    break;
  case SYS_CLOSE:
    read_args(f->esp, args, 1);
    handle_close(args[0]);
    break;
  }
}

static void handle_exit(int status)
{
  thread_current()->exit_code = status;
  thread_exit();
}

static pid_t handle_exec(const char *cmd)
{
  tid_t tid;

  if (!is_valid_vaddr(cmd, sizeof(char *)))
    handle_exit(-1);

  if ((tid = process_execute(cmd)) == TID_ERROR)
    return -1;
  thread_yield();
  return tid;
}

static int handle_wait(pid_t pid)
{
  return process_wait(pid);
}

static bool handle_create(const char *file, unsigned initial_size)
{
  bool success;

  if (!is_valid_vaddr(file, sizeof(char *)))
    handle_exit(-1);

  lock_acquire(&fs_lock);
  success = filesys_create(file, initial_size);
  lock_release(&fs_lock);
  return success;
}

static bool handle_remove(const char *file)
{
  bool success;

  if (!is_valid_vaddr(file, sizeof(char *)))
    handle_exit(-1);

  lock_acquire(&fs_lock);
  success = filesys_remove(file);
  lock_release(&fs_lock);
  return success;
}

static int handle_open(const char *file)
{
  struct file *f;

  if (!is_valid_vaddr(file, sizeof(char *)))
    handle_exit(-1);

  lock_acquire(&fs_lock);
  f = filesys_open(file);
  lock_release(&fs_lock);
  if (f) {
    struct thread *t = thread_current();
    int fd = t->file_n++;
    if (fd >= t->file_alloc_n) {
      t->file_alloc_n *= 2;
      t->file = realloc(t->file, t->file_alloc_n * sizeof(struct file *));
    }
    t->file[fd] = f;
    return fd;
  } else {
    return -1;
  }
}

static int handle_filesize(int fd)
{
  struct thread *t;
  struct file *f;

  t = thread_current();
  if (fd >= t->file_n)
    handle_exit(-1);

  if ((f = t->file[fd])) {
    off_t off;
    lock_acquire(&fs_lock);
    off = file_length(f);
    lock_release(&fs_lock);
    return off;
  } else {
    return -1;
  }
}

static int handle_read(int fd, void *buffer, unsigned size, const void *esp)
{
  struct thread *t;
  struct file *f;

  t = thread_current();
#ifdef VM
  if (fd >= t->file_n)
    handle_exit(-1);
  grow_stack(buffer, size, esp);
#else
  (void) esp;
  if (fd >= t->file_n || !is_valid_vaddr(buffer, size))
    handle_exit(-1);
  if (!is_writable_vaddr(buffer, size))
    handle_exit(-1);
#endif

  if (fd == 0) {
    uint8_t *buf;
    unsigned i;

    buf = buffer;
    for (i = 0; i < size; i++)
      buf[i] = input_getc();
    return size;
  } else if ((f = t->file[fd])) {
    off_t off;
    lock_acquire(&fs_lock);
    off = file_read(f, buffer, size);
    lock_release(&fs_lock);
    return off;
  } else {
    handle_exit(-1);
  }
}

static int handle_write(int fd, const void *buffer, unsigned size)
{
  struct thread *t;
  struct file *f;

  t = thread_current();
  if (fd >= t->file_n || !is_valid_vaddr(buffer, size))
    handle_exit(-1);

  if (fd == 1) {
    return printf("%s", (char *) buffer);
  } else if ((f = t->file[fd])) {
    off_t off;
    lock_acquire(&fs_lock);
    off = file_write(f, buffer, size);
    lock_release(&fs_lock);
    return off;
  } else {
    handle_exit(-1);
  }
}

static void handle_seek(int fd, unsigned position)
{
  struct thread *t;
  struct file *f;

  t = thread_current();
  if (fd >= t->file_n)
    handle_exit(-1);

  if ((f = t->file[fd])) {
    lock_acquire(&fs_lock);
    file_seek(f, position);
    lock_release(&fs_lock);
  } else {
    handle_exit(-1);
  }
}

static unsigned handle_tell(int fd)
{
  struct thread *t;
  struct file *f;

  t = thread_current();
  if (fd >= t->file_n)
    handle_exit(-1);

  if ((f = t->file[fd])) {
    off_t off;
    lock_acquire(&fs_lock);
    off = file_tell(f);
    lock_release(&fs_lock);
    return off;
  } else {
    handle_exit(-1);
  }
}

static void handle_close(int fd)
{
  struct thread *t;
  struct file *f;

  t = thread_current();
  if (fd >= t->file_n)
    handle_exit(-1);

  if ((f = t->file[fd])) {
    lock_acquire(&fs_lock);
    file_close(f);
    lock_release(&fs_lock);
    t->file[fd] = NULL;
  } else {
    handle_exit(-1);
  }
}

static bool is_valid_vaddr(const void *vaddr, unsigned size)
{
  uint32_t *pd;
  void *start;
  void *end;
  void *p;

  if (!vaddr || !is_user_vaddr(vaddr + size - 1))
    return false;

  pd = thread_current()->pagedir;
  start = pg_round_down(vaddr);
  end = pg_round_up(vaddr + size);
  for (p = start; p < end; p += PGSIZE)
    if (!pagedir_get_page(pd, p))
      return false;
  return true;
}

static bool is_writable_vaddr(const void *vaddr, unsigned size)
{
  uint32_t *pd;
  void *start;
  void *end;
  void *p;

  if (!vaddr || !is_user_vaddr(vaddr + size - 1))
    return false;

  pd = thread_current()->pagedir;
  start = pg_round_down(vaddr);
  end = pg_round_up(vaddr + size);
  for (p = start; p < end; p += PGSIZE) {
    uint32_t *pt = pde_get_pt(pd[pd_no(vaddr)]);
    if (~pt[pt_no(vaddr)] & PTE_W)
      return false;
  }
  return true;
}

static void read_args(const long *esp, long *args, int count)
{
  int i;

  if (!is_valid_vaddr(esp + 1, count * sizeof(long)))
    handle_exit(-1);

  for (i = 0; i < count; i++)
    args[i] = esp[i + 1];
}

#ifdef VM
static void grow_stack(const void *vaddr, unsigned size, const void *esp)
{
  if (is_user_vaddr(vaddr + size - 1) && vaddr >= esp
      && vaddr >= PHYS_BASE - USER_STACK_LIMIT) {
    void *start;
    void *end;
    void *p;

    start = pg_round_down(vaddr);
    end = pg_round_up(vaddr + size);
    for (p = start; p < end; p += PGSIZE) {
      uint32_t *pd = thread_current()->pagedir;
      if (pagedir_get_page(pd, p)) {
        uint32_t *pt = pde_get_pt(pd[pd_no(vaddr)]);
        if (~pt[pt_no(vaddr)] & PTE_W)
          handle_exit(-1);
      } else {
        page_install(p, frame_alloc(true), true);
      }
    }
  } else {
    if (!is_valid_vaddr(vaddr, size) || !is_writable_vaddr(vaddr, size))
      handle_exit(-1);
  }
}
#endif
