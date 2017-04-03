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
#include "devices/input.h"
#include "lib/string.h"


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

static void *valid_addr (uint32_t *pd, const void *uaddr);
static void *valid_uaddr (const void *uaddr);


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
  if(!valid_uaddr(esp)) handle_exit(-1);


  get_user(esp, &number);
  //printf("system call! : %d\n", number);
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
  thread_current()->proc->status = status; 
  thread_exit();
}

static pid_t
handle_exec(const char *cmd)
{
  tid_t tid;
  if(!cmd || !valid_uaddr(cmd))
    handle_exit(-1);
  
  char *name;
  int i, j=0;
  while(cmd[j++]==' ');
  i = j - 1;
  while(cmd[j++]!='\0') if(cmd[j-1] == ' ') break;
  name = malloc(sizeof(char)*(j-i+1));
  strlcpy(name, cmd+i, j-i);

  if(!filesys_open(name)) {
    free(name);
    return -1;
  }
  
  free(name);
  tid = process_execute(cmd);

  thread_yield();

  return tid;
}

static int
handle_wait(pid_t pid)
{
  return 0;
}

static bool
handle_create(const char *file, unsigned initial_size)
{
  if(file && valid_uaddr(file))
    return filesys_create(file, initial_size);
  else handle_exit(-1);
}

static bool
handle_remove(const char *file)
{
  if(file && valid_uaddr(file))
    return filesys_remove(file);
  else handle_exit(-1);
}

static int
handle_open(const char *name)
{
  struct process *proc = thread_current() -> proc;
  int fd = proc->file_n++;
  struct file *file;
  if(!name || !valid_uaddr(name))
    handle_exit(-1);

  file = filesys_open(name);
  if(file) {
    proc->file[fd] = file;
    return fd;
  } else return -1;
}

static int
handle_filesize(int fd)
{
  struct process *proc = thread_current()->proc;
  struct file *file;
  if(fd >= proc->file_n)
    handle_exit(-1);

  file = proc->file[fd];
  if(file){
    return file_length(file);
  }else{
    return -1;
  }
}

static int
handle_read(int fd, void *buffer, unsigned size)
{
  struct process *proc = thread_current()->proc;
  struct file *file;
  uint8_t *buf = buffer;

  if(fd >= proc->file_n || !buffer || !valid_uaddr(buffer) || !valid_uaddr(buffer+size-1)) handle_exit(-1);

  if(fd == 0) {
    unsigned i;
    for(i=0; i<size; i++){
      buf[i] = input_getc();
    }
    return size;
  }else{
    file = proc->file[fd];
    if(file){
      return file_read(file, buffer, size);
    }else{
      handle_exit(-1);
    }
  }
  return 0; // NOT REACHED
}

static int
handle_write(int fd, const void *buffer, unsigned size)
{
  struct process *proc = thread_current()->proc;
  struct file *file;

  if(fd >= proc->file_n || !buffer || !valid_uaddr(buffer) || !valid_uaddr(buffer+size-1)) handle_exit(-1);

  if(fd == 1) {
    return printf("%s", (char *)buffer);
  }else{
    file = proc->file[fd];
    if(file){
      return file_write(file, buffer, size);
    }else{
      handle_exit(-1);
    }
  }
  return 0; //NOT REACHED
}

static void
handle_seek(int fd, unsigned position)
{
  struct process *proc = thread_current()->proc;
  struct file *file;
  if(fd >= proc->file_n)
    handle_exit(-1);
  
  file = proc->file[fd];

  if(file){
    file_seek(file, position);
  }else handle_exit(-1);
}

static unsigned
handle_tell(int fd)
{
  struct process *proc = thread_current()->proc;
  struct file *file;
  if(fd >= proc->file_n)
    handle_exit(-1);
  
  file = proc->file[fd];

  if(file){
    return file_tell(file);
  }else handle_exit(-1);
  return 0;
}

static void
handle_close(int fd)
{
  struct process *proc = thread_current()->proc;
  struct file *file;
  if(fd >= proc->file_n)
    handle_exit(-1);
  
  file = proc->file[fd];

  if(file){
    file_close(file);
    proc->file[fd] = NULL;
  }
}

static void *
valid_addr (uint32_t *pd, const void *uaddr)
{
  if (!uaddr || uaddr >= PHYS_BASE) return NULL;
  else return pagedir_get_page(pd, uaddr);
}

static void *
valid_uaddr (const void *uaddr)
{
  uint32_t *pd = thread_current() -> pagedir;
  return valid_addr(pd, uaddr);
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
