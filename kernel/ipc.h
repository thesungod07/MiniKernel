#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include "sched.h"

/* Call after sched_init(). Clears every IPC object and registers the syscalls. */
void ipc_init(void);

/* kernel-side creation; processes then refer to the objects by id */
int pipe_create(void);
int shm_create(int words);
int msgq_create(void);

uint32_t shm_peek(int seg, int index);      /* for tests and demos */

#endif
