#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/pte.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "lib/string.h"
#include "lib/kernel/list.h"

#define GET_ARG(n) get_users(esp, &args, n)

static void syscall_handler (struct intr_frame *);

static void handle_exit(int status) NO_RETURN;
static pid_t handle_exec(const char *cmd_line);
static int handle_wait(pid_t pid);
static bool handle_create(const char *file, unsigned initial_size);
static bool handle_remove(const char *file);
static int handle_open(const char *file);
static int handle_filesize(int fd);
static int handle_read(int fd, void *buffer, unsigned size);
static int handle_write(int fd, const void *buffer, unsigned size);
static void handle_seek(int fd, unsigned position);
static unsigned handle_tell(int fd);
static void handle_close(int fd);

static void *valid_uaddr(const void *vaddr);
static bool is_writable_vaddr(const void *vaddr);
static bool get_user (const void *uaddr, int *data);
static bool get_users (const void *uaddr, int **data, int c);

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
  void *esp = f->esp;
  int number;
  int *args = NULL;

  if (!valid_uaddr(esp))
    handle_exit(-1);

  get_user(esp, &number);
  switch (number) {
    case SYS_HALT:
      power_off();
      NOT_REACHED();
    case SYS_EXIT:
      GET_ARG(1);
      handle_exit(args[0]);
      NOT_REACHED();
    case SYS_EXEC:
      GET_ARG(1);
      f->eax = handle_exec((char *) args[0]);
      break;
    case SYS_WAIT:
      GET_ARG(1);
      f->eax = handle_wait(args[0]);
      break;
    case SYS_CREATE:
      GET_ARG(2);
      f->eax = handle_create((char *) args[0], args[1]);
      break;
    case SYS_REMOVE:
      GET_ARG(1);
      f->eax = handle_remove((char *) args[0]);
      break;
    case SYS_OPEN:
      GET_ARG(1);
      f->eax = handle_open((char *) args[0]);
      break;
    case SYS_FILESIZE:
      GET_ARG(1);
      f->eax = handle_filesize(args[0]);
      break;
    case SYS_READ:
      GET_ARG(3);
      f->eax = handle_read(args[0], (void *) args[1], args[2]);
      break;
    case SYS_WRITE:
      GET_ARG(3);
      f->eax = handle_write(args[0], (void *) args[1], args[2]);
      break;
    case SYS_SEEK:
      GET_ARG(2);
      handle_seek(args[0], args[1]);
      break;
    case SYS_TELL:
      GET_ARG(1);
      f->eax = handle_tell(args[0]);
      break;
    case SYS_CLOSE:
      GET_ARG(1);
      handle_close(args[0]);
      break;
  }
  if (args)
    free(args);
}

static void
handle_exit(int status)
{
  thread_current()->exit_code = status;
  thread_exit();
}

static pid_t
handle_exec(const char *cmd)
{
  tid_t tid;
  if(!cmd || !valid_uaddr(cmd))
    handle_exit(-1);

  if ((tid = process_execute(cmd)) == TID_ERROR)
    return -1;
  thread_yield();
  return tid;
}

static int
handle_wait(pid_t pid)
{
  return process_wait(pid);
}

static bool
handle_create(const char *file, unsigned initial_size)
{
  if (file && valid_uaddr(file)) {
    bool success;
    lock_acquire(&fs_lock);
    success = filesys_create(file, initial_size);
    lock_release(&fs_lock);
    return success;
  } else {
    handle_exit(-1);
  }
}

static bool
handle_remove(const char *file)
{
  if (file && valid_uaddr(file)) {
    bool success;
    lock_acquire(&fs_lock);
    success = filesys_remove(file);
    lock_release(&fs_lock);
    return success;
  } else {
    handle_exit(-1);
  }
}

static int
handle_open(const char *file)
{
  struct file *f;

  if (!file || !valid_uaddr(file))
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

static int
handle_filesize(int fd)
{
  struct thread *t = thread_current();
  struct file *file;
  if(fd >= t->file_n)
    handle_exit(-1);

  file = t->file[fd];
  if(file){
    off_t off;
    lock_acquire(&fs_lock);
    off = file_length(file);
    lock_release(&fs_lock);
    return off;
  }else{
    return -1;
  }
}

static int
handle_read(int fd, void *buffer, unsigned size)
{
  struct thread *t = thread_current();
  struct file *file;
  uint8_t *buf = buffer;

  if (fd >= t->file_n || !buffer || !valid_uaddr(buffer)
      || !valid_uaddr(buffer + size - 1))
    handle_exit(-1);
  if (!is_writable_vaddr(buffer) || !is_writable_vaddr(buffer + size - 1))
    handle_exit(-1);

  if(fd == 0) {
    unsigned i;
    for(i=0; i<size; i++){
      buf[i] = input_getc();
    }
    return size;
  }else{
    file = t->file[fd];
    if(file){
      off_t off;
      lock_acquire(&fs_lock);
      off = file_read(file, buffer, size);
      lock_release(&fs_lock);
      return off;
    }else{
      handle_exit(-1);
    }
  }
  NOT_REACHED();
}

static int
handle_write(int fd, const void *buffer, unsigned size)
{
  struct thread *t = thread_current();
  struct file *file;

  if (fd >= t->file_n || !buffer || !valid_uaddr(buffer)
      || !valid_uaddr(buffer + size - 1))
    handle_exit(-1);

  if(fd == 1) {
    return printf("%s", (char *)buffer);
  }else{
    file = t->file[fd];
    if(file){
      off_t off;
      lock_acquire(&fs_lock);
      off = file_write(file, buffer, size);
      lock_release(&fs_lock);
      return off;
    }else{
      handle_exit(-1);
    }
  }
  NOT_REACHED();
}

static void
handle_seek(int fd, unsigned position)
{
  struct thread *t = thread_current();
  struct file *file;
  if (fd >= t->file_n)
    handle_exit(-1);

  file = t->file[fd];

  if(file){
    lock_acquire(&fs_lock);
    file_seek(file, position);
    lock_release(&fs_lock);
  }else handle_exit(-1);
}

static unsigned
handle_tell(int fd)
{
  struct thread *t = thread_current();
  struct file *file;
  if (fd >= t->file_n)
    handle_exit(-1);

  file = t->file[fd];

  if(file){
    off_t off;
    lock_acquire(&fs_lock);
    off = file_tell(file);
    lock_release(&fs_lock);
    return off;
  }else handle_exit(-1);
  return 0;
}

static void
handle_close(int fd)
{
  struct thread *t = thread_current();
  struct file *file;
  if (fd >= t->file_n)
    handle_exit(-1);

  file = t->file[fd];

  if(file){
    lock_acquire(&fs_lock);
    file_close(file);
    lock_release(&fs_lock);
    t->file[fd] = NULL;
  }
}

static void *valid_uaddr(const void *vaddr)
{
  if (vaddr && is_user_vaddr(vaddr))
    return pagedir_get_page(thread_current()->pagedir, vaddr);
  else
    return NULL;
}

static bool is_writable_vaddr(const void *vaddr)
{
  uint32_t *pd = thread_current()->pagedir;
  uint32_t *pt = pde_get_pt(pd[pd_no(vaddr)]);
  return pt[pt_no(vaddr)] & PTE_W;
}

static bool
get_user (const void *uaddr, int *data)
{
  void *addr = valid_uaddr(uaddr);
  if(addr){
    *data = *((int *)addr);
    return true;
  } else {
    handle_exit(-1);
    return false;
  }
}

static bool
get_users (const void *uaddr, int **data, int c)
{
  *data = malloc(sizeof(int) * c);
  int i;
  for(i=1; i<=c; i++){
    if(!get_user(uaddr+i*4, &(*data)[i-1]))
      return false;
  }
  return true;
}
