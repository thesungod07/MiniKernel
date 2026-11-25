#ifndef THREADS_H
#define THREADS_H

/* Thread models, built on the host so they can be run and measured directly.
 * Link with -pthread. */

/* ---- user-level threads: many-to-one ----
 * Every thread lives inside one kernel-visible entity. The library switches
 * between them itself (ucontext), so a switch costs no system call, but a
 * blocking call would block all of them. Scheduling is cooperative round robin. */
typedef void (*ut_fn)(void *arg);
void ut_reset(void);
int  ut_create(ut_fn fn, void *arg);     /* returns the thread id */
void ut_yield(void);
int  ut_self(void);
void ut_run(void);                       /* run until every thread has finished */

/* ---- kernel-level threads: one-to-one (POSIX threads) ----
 * Several threads increment one counter. Without the mutex the result is
 * lower than nthreads * iters (a race); with it the result is exact. */
long kt_counter_demo(int nthreads, int iters, int use_mutex);

/* ---- thread pool ---- */
typedef struct tpool tpool_t;
tpool_t *tpool_create(int nworkers, int queue_cap);
int  tpool_submit(tpool_t *p, void (*fn)(void *), void *arg);
void tpool_wait(tpool_t *p);             /* block until every submitted job has finished */
void tpool_destroy(tpool_t *p);

#endif
