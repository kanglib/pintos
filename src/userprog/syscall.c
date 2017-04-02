#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/init.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "filesys/filesys.h"

//#define ARG(n) (get_user(esp+(n)*4))


static void syscall_handler (struct intr_frame *);

static bool handle_write (int, const void *, unsigned);
static bool handle_exec (const char *);
static bool handle_create (const char *name, unsigned initial_size);
static bool handle_remove (const char *name);

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
      handle_exec(args[0]);
      break;
    case SYS_WAIT:
    
      break;
    case SYS_CREATE:
      get_users(esp, &args, 2);
      handle_create(args[0], args[1]);
      break;
    case SYS_REMOVE:
      get_users(esp, &args, 1);
      handle_remove(args[0]);
      break;
    case SYS_OPEN:
    case SYS_FILESIZE:
    case SYS_READ:
    case SYS_SEEK:
    case SYS_TELL:
    case SYS_CLOSE:
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

