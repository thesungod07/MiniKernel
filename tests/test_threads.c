#include <stdio.h>
#include <pthread.h>
#include "../kernel/threads.h"

static int fails;
#define CHECK(cond, msg) do { if (cond) printf("  ok    %s\n", msg); \
    else { printf("  FAIL  %s\n", msg); fails++; } } while (0)

static int order[32], norder;

static void ut_body(void *arg)
{
    int id = *(int *)arg;
    for (int i = 0; i < 3; i++)
    {
        order[norder++] = id;
        ut_yield();
    }
}

static long sum;
static pthread_mutex_t sum_lock = PTHREAD_MUTEX_INITIALIZER;
static void add_job(void *arg)
{
    long v = (long)arg;
    pthread_mutex_lock(&sum_lock);
    sum += v;
    pthread_mutex_unlock(&sum_lock);
}

int main(void)
{
    printf("user-level threads (many-to-one): cooperative round robin\n");
    {
        int ids[3] = { 0, 1, 2 };
        ut_reset();
        for (int i = 0; i < 3; i++) ut_create(ut_body, &ids[i]);
        ut_run();
        int expect[9] = { 0, 1, 2, 0, 1, 2, 0, 1, 2 };
        int ok = (norder == 9);
        for (int i = 0; ok && i < 9; i++) if (order[i] != expect[i]) ok = 0;
        CHECK(ok, "threads interleaved 0 1 2 0 1 2 0 1 2 and all finished");
        ut_reset();
    }

    printf("kernel-level threads (one-to-one): shared counter\n");
    {
        long locked = kt_counter_demo(4, 20000, 1);
        long racy = kt_counter_demo(4, 20000, 0);
        printf("        with mutex %ld, without mutex %ld (expected 80000)\n", locked, racy);
        CHECK(locked == 80000, "with the mutex the result is exact");
        CHECK(racy < 80000, "without it, updates are lost (a race)");
    }

    printf("thread pool: 4 workers, 100 jobs\n");
    {
        tpool_t *p = tpool_create(4, 8);
        sum = 0;
        for (long i = 0; i < 100; i++) tpool_submit(p, add_job, (void *)i);
        tpool_wait(p);
        CHECK(sum == 4950, "all jobs ran exactly once (sum 0..99 = 4950)");
        tpool_destroy(p);
    }

    printf(fails ? "\n%d FAILED\n" : "\nall ok\n", fails);
    return fails != 0;
}
