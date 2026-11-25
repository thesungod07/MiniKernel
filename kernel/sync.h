#ifndef SYNC_H
#define SYNC_H

#include "sched.h"

/* Call after sched_init(). Registers the semaphore and mutex syscalls. */
void sync_init(void);

int ksem_create(int initial);       /* counting semaphore */
int kmutex_create(void);            /* mutex with an owner */

/* 1 = priority inheritance on: a mutex owner is boosted to the priority of the
 * most important process waiting for it, and dropped back on unlock. */
void sync_set_inheritance(int on);

#endif
