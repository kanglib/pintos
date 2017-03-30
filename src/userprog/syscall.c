#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "threads/init.h"

#define ARG(n) (get_user(esp+(n)*4))

static void syscall_handler (struct intr_frame *);

static bool handle_write (int, const void *, unsigned);

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
  int number = get_user(esp);
  printf ("%d\n", number);
  hex_dump(0, esp, 16, true);

  switch(number){
    case SYS_EXIT:
      process_exit();
      break;
    case SYS_HALT:
      power_off();
      NOT_REACHED();
      break;
    case SYS_WRITE:
      handle_write(ARG(1), ARG(2), ARG(3));
      break;
  }


  //thread_exit ();
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
  }
}

static int
get_user (const void *uaddr)
{
  uint32_t *pd = thread_current() -> pagedir;
  void *addr = valid_addr(pd, uaddr);
  if(addr){
    return *((int *)addr);
  } else {
    process_exit();
    return -1;
  }
}
//
////static bool
////put_user (const void *uaddr, uint8_t data) {
////  uint32_t *pd = thread_current() -> pagedir;
////  void *addr = valid_addr(pd, uaddr);
////  if(addr){
////    *((uint8_t)addr) = data;
////  }else{
////    return false;
////  }
////}
//
