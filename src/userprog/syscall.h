#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* Process identifier type. */
typedef int pid_t;

void syscall_init (void);

extern struct lock fs_lock;

#endif /* userprog/syscall.h */
