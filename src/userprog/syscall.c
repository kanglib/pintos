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
#include "filesys/filesys.h"

//#define ARG(n) (get_user(esp+(n)*4))
#define GET_ARG(n) get_users(esp, &args, n)


static void syscall_handler (struct intr_frame *);

static bool handle_write (int, const void *, unsigned);
static bool handle_exec (const char *);
static bool handle_create (const char *name, unsigned initial_size);
static bool handle_remove (const char *name);
static int handle_open (const char *name);
static int handle_filesize (int fd);
static void handle_close (int fd);

static bool get_user (const void *uaddr, int *data);
static bool get_users (const void *uaddr, int **data, int c);
static bool put_user (const void *uaddr, uint8_t data);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{

  printf ("system call! : ");
  void *esp = f->esp;
  int number, i;
  int *args = NULL;

  get_user(esp, &number);
  printf ("%d\n", number);
  //printf("esp=%x\n", esp);
 // hex_dump(PHYS_BASE-200, PHYS_BASE-200, 200, true);


  switch(number){
    case SYS_EXIT:
     // process_exit();
      thread_exit();
      break;
    case SYS_HALT:
      power_off();
      NOT_REACHED();
      break;
    case SYS_WRITE:
      get_users(esp, &args, 3);
     // handle_write(ARG(1), ARG(2), ARG(3));
      handle_write(args[0], args[1], args[2]);

      break;
    case SYS_EXEC:
      get_users(esp, &args, 1);
      f->eax = handle_exec(args[0]);
      break;
    case SYS_WAIT:

      break;
    case SYS_CREATE:
      get_users(esp, &args, 2);
      f->eax = handle_create(args[0], args[1]);
      break;
    case SYS_REMOVE:
      get_users(esp, &args, 1);
      f->eax = handle_remove(args[0]);
      break;
    case SYS_OPEN:
      get_users(esp, &args, 1);
      f->eax = handle_open(args[0]);
      break;
    case SYS_FILESIZE:
      GET_ARG(1);
      f->eax = handle_filesize(args[0]);
    case SYS_READ:
    case SYS_SEEK:
    case SYS_TELL:
    case SYS_CLOSE:
      GET_ARG(1);
      handle_close(args[0]);
      break;
    default:
      break;
  }

  if(args) free(args);


 // thread_exit ();
}

static void *
valid_addr (uint32_t *pd, const void *uaddr)
{
  if (!uaddr || uaddr >= PHYS_BASE) return NULL;
  else return pagedir_get_page(pd, uaddr);
}


static bool
handle_write (int fd, const void *buffer, unsigned size) {
  if(fd == 1) {
    printf((char *)buffer);
  }else{
    printf("WRITE(%d/%d): %s", fd, size, (char *)buffer);
  }
  return true;
}

static bool
handle_exec (const char *cmd) {
  tid_t tid;
  printf("EXEC %s\n", cmd);
  tid = process_execute(cmd);

  return tid != -1;
}

static bool
handle_create (const char *name, unsigned initial_size) {
  return filesys_create(name, initial_size);
}

static bool
handle_remove (const char *name) {
  return filesys_remove (name);
}

static int
handle_open (const char *name) {
  struct process *proc = thread_current() -> proc;
  int fd = proc->file_n++;
  struct file *file = filesys_open(name);
  if(file) {
    proc->file[fd] = file;
    return fd;
  } else return -1;
}

static int
handle_filesize (int fd) {
  struct file *file = thread_current()->proc->file[fd];
  if(file){
    return file_length(file);
  }else{
    return -1;
  }
}

static void
handle_close (int fd) {
  struct file *file = thread_current()->proc->file[fd];
  if(file){
    file_close(file);
  }
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
  }else{
    return false;
  }
}

