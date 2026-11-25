#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <ucontext.h>
#include "threads.h"

/* ====================== user-level threads ====================== */

#define UT_MAX 16
#define UT_STACK (64 * 1024)

typedef struct
{
    ucontext_t ctx;
    void *stack;
    ut_fn fn;
    void *arg;
    int state;              /* 0 unused, 1 ready, 2 finished */
} ut_t;

static ut_t th[UT_MAX];
static int nth;
static int cur = -1;
static ucontext_t sched_ctx;

static void ut_trampoline(void)
{
    ut_t *t = &th[cur];
    t->fn(t->arg);
    t->state = 2;           /* returning goes back to the scheduler through uc_link */
}

void ut_reset(void)
{
    for (int i = 0; i < nth; i++) { free(th[i].stack); th[i].stack = NULL; th[i].state = 0; }
    nth = 0;
    cur = -1;
}

int ut_create(ut_fn fn, void *arg)
{
    if (nth >= UT_MAX) { return -1; }

    ut_t *t = &th[nth];
    getcontext(&t->ctx);
    t->stack = malloc(UT_STACK);
    t->ctx.uc_stack.ss_sp = t->stack;
    t->ctx.uc_stack.ss_size = UT_STACK;
    t->ctx.uc_link = &sched_ctx;
    makecontext(&t->ctx, ut_trampoline, 0);
    t->fn = fn;
    t->arg = arg;
    t->state = 1;
    return nth++;
}

void ut_yield(void)
{
    if (cur >= 0) { swapcontext(&th[cur].ctx, &sched_ctx); }
}

int ut_self(void) { return cur; }

void ut_run(void)
{
    int live;
    do
    {
        live = 0;
        for (int i = 0; i < nth; i++)
        {
            if (th[i].state != 1) { continue; }
            cur = i;
            swapcontext(&sched_ctx, &th[i].ctx);
            if (th[i].state == 1) { live = 1; }
        }
    } while (live);
    cur = -1;
}

/* ====================== kernel-level threads ====================== */

typedef struct
{
    volatile long *counter;
    int iters;
    pthread_mutex_t *lock;
} kt_arg;

static void *kt_worker(void *p)
{
    kt_arg *a = p;
    for (int i = 0; i < a->iters; i++)
    {
        if (a->lock) { pthread_mutex_lock(a->lock); }
        long v = *a->counter;
        if (!a->lock) { sched_yield(); }        /* widen the race window: read, get preempted, write */
        *a->counter = v + 1;
        if (a->lock) { pthread_mutex_unlock(a->lock); }
    }
    return NULL;
}

long kt_counter_demo(int nthreads, int iters, int use_mutex)
{
    pthread_t t[16];
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    volatile long counter = 0;
    kt_arg a = { &counter, iters, use_mutex ? &lock : NULL };

    if (nthreads > 16) { nthreads = 16; }
    for (int i = 0; i < nthreads; i++) { pthread_create(&t[i], NULL, kt_worker, &a); }
    for (int i = 0; i < nthreads; i++) { pthread_join(t[i], NULL); }
    return counter;
}

/* ====================== thread pool ====================== */

typedef struct { void (*fn)(void *); void *arg; } job_t;

struct tpool
{
    pthread_t *workers;
    int nworkers;
    job_t *queue;
    int cap, head, count;
    int pending;            /* submitted but not yet finished */
    int stop;
    pthread_mutex_t m;
    pthread_cond_t not_empty, not_full, idle;
};

static void *worker_main(void *arg)
{
    tpool_t *p = arg;

    pthread_mutex_lock(&p->m);
    for (;;)
    {
        while (p->count == 0 && !p->stop) { pthread_cond_wait(&p->not_empty, &p->m); }
        if (p->count == 0 && p->stop) { break; }

        job_t j = p->queue[p->head];
        p->head = (p->head + 1) % p->cap;
        p->count--;
        pthread_cond_signal(&p->not_full);
        pthread_mutex_unlock(&p->m);

        j.fn(j.arg);

        pthread_mutex_lock(&p->m);
        p->pending--;
        if (p->pending == 0) { pthread_cond_broadcast(&p->idle); }
    }
    pthread_mutex_unlock(&p->m);
    return NULL;
}

tpool_t *tpool_create(int nworkers, int queue_cap)
{
    tpool_t *p = calloc(1, sizeof *p);
    if (!p) { return NULL; }

    p->nworkers = nworkers;
    p->cap = queue_cap;
    p->workers = calloc((size_t)nworkers, sizeof(pthread_t));
    p->queue = calloc((size_t)queue_cap, sizeof(job_t));
    pthread_mutex_init(&p->m, NULL);
    pthread_cond_init(&p->not_empty, NULL);
    pthread_cond_init(&p->not_full, NULL);
    pthread_cond_init(&p->idle, NULL);

    for (int i = 0; i < nworkers; i++) { pthread_create(&p->workers[i], NULL, worker_main, p); }
    return p;
}

int tpool_submit(tpool_t *p, void (*fn)(void *), void *arg)
{
    pthread_mutex_lock(&p->m);
    while (p->count == p->cap && !p->stop) { pthread_cond_wait(&p->not_full, &p->m); }
    if (p->stop) { pthread_mutex_unlock(&p->m); return -1; }

    p->queue[(p->head + p->count) % p->cap] = (job_t){ fn, arg };
    p->count++;
    p->pending++;
    pthread_cond_signal(&p->not_empty);
    pthread_mutex_unlock(&p->m);
    return 0;
}

void tpool_wait(tpool_t *p)
{
    pthread_mutex_lock(&p->m);
    while (p->pending > 0) { pthread_cond_wait(&p->idle, &p->m); }
    pthread_mutex_unlock(&p->m);
}

void tpool_destroy(tpool_t *p)
{
    pthread_mutex_lock(&p->m);
    p->stop = 1;
    pthread_cond_broadcast(&p->not_empty);
    pthread_cond_broadcast(&p->not_full);
    pthread_mutex_unlock(&p->m);

    for (int i = 0; i < p->nworkers; i++) { pthread_join(p->workers[i], NULL); }

    free(p->workers);
    free(p->queue);
    free(p);
}
