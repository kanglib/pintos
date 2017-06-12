#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

/* Process identifier type. */
typedef int pid_t;

/* Maximum characters in a filename written by readdir(). */
#define READDIR_MAX_LEN 14

void syscall_init (void);

#endif /* userprog/syscall.h */
