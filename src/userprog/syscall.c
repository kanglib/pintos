#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

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

static bool get_user (const void *uaddr, int *data);
static bool get_users (const void *uaddr, int **data, int c);
static bool put_user (const void *uaddr, uint8_t data);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f)
{
  void *esp = f->esp;
  int number;
  int *args = NULL;

  get_user(esp, &number);
  printf("system call! : %d\n", number);
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
  thread_exit();
}

static pid_t
handle_exec(const char *cmd)
{
  tid_t tid;
  printf("EXEC %s\n", cmd);
  tid = process_execute(cmd);

  return tid != -1;
}

static int
handle_wait(pid_t pid)
{
  return 0;
}

static bool
handle_create(const char *file, unsigned initial_size)
{
  return filesys_create(file, initial_size);
}

static bool
handle_remove(const char *file)
{
  return filesys_remove(file);
}

static int
handle_open(const char *name)
{
  struct process *proc = thread_current() -> proc;
  int fd = proc->file_n++;
  struct file *file = filesys_open(name);
  if(file) {
    proc->file[fd] = file;
    return fd;
  } else return -1;
}

static int
handle_filesize(int fd)
{
  struct file *file = thread_current()->proc->file[fd];
  if(file){
    return file_length(file);
  }else{
    return -1;
  }
}

static int
handle_read(int fd, void *buffer, unsigned size)
{
  return 0;
}

static int
handle_write(int fd, const void *buffer, unsigned size)
{
  if(fd == 1) {
    printf("%s", buffer);
  }else{
    printf("WRITE(%d/%d): %s", fd, size, (char *)buffer);
  }
  return 0;
}

static void
handle_seek(int fd, unsigned position)
{
}

static unsigned
handle_tell(int fd)
{
  return 0;
}

static void
handle_close(int fd)
{
  struct file *file = thread_current()->proc->file[fd];
  if(file){
    file_close(file);
  }
}

static void *
valid_addr (uint32_t *pd, const void *uaddr)
{
  if (!uaddr || uaddr >= PHYS_BASE) return NULL;
  else return pagedir_get_page(pd, uaddr);
}

static bool
get_user (const void *uaddr, int *data)
{
  uint32_t *pd = thread_current() -> pagedir;
  void *addr = valid_addr(pd, uaddr);
  if(addr){
    *data = *((int *)addr);
    return true;
  } else {
    process_exit();
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

static bool
put_user (const void *uaddr, uint8_t data) {
  uint32_t *pd = thread_current() -> pagedir;
  void *addr = valid_addr(pd, uaddr);
  if(addr){
    *((uint8_t *)addr) = data;
    return true;
  }else{
    return false;
  }
}
