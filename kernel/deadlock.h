#ifndef DEADLOCK_H
#define DEADLOCK_H

#include <stdbool.h>
#include "sched.h"

/* ================= part 1: the algorithms, on plain matrices ================= */

#define DL_MAXP 8
#define DL_MAXR 6

typedef struct
{
    int np, nr;
    int available[DL_MAXR];
    int max[DL_MAXP][DL_MAXR];          /* declared maximum claim */
    int alloc[DL_MAXP][DL_MAXR];        /* currently held */
    int request[DL_MAXP][DL_MAXR];      /* currently waiting for (used by detection) */
} dl_state_t;

/* Banker's safety algorithm. seq (optional) receives a safe sequence. */
bool dl_is_safe(const dl_state_t *s, int seq[]);

/* Banker's resource-request algorithm. Returns 0 if granted (state updated),
 * -1 if the request exceeds the declared maximum, -2 if not available now,
 * -3 if granting it would leave the system in an unsafe state. */
int dl_request(dl_state_t *s, int p, const int req[]);

/* Detection for multiple-instance resources. Returns the number of deadlocked
 * processes and lists them in deadlocked[]. */
int dl_detect(const dl_state_t *s, int deadlocked[]);

/* Wait-for graph: edge i -> j when i waits for a resource that j holds. */
void dl_build_wfg(const dl_state_t *s, int adj[DL_MAXP][DL_MAXP]);
int  dl_wfg_cycle(int n, const int adj[DL_MAXP][DL_MAXP], int cycle[]);   /* cycle length, 0 if none */

/* Recovery by termination: abort the cheapest deadlocked process, repeat until
 * the deadlock is gone. Returns how many were aborted (listed in victims[]). */
int dl_recover_terminate(dl_state_t *s, int victims[]);

/* Prevention helpers */
bool dl_ordered_ok(int highest_held, int requested);          /* break circular wait */
int  dl_request_all_or_none(dl_state_t *s, int p, const int req[]);   /* break hold-and-wait */

/* ================= part 2: a resource manager inside the kernel ================= */

enum { RM_NONE, RM_AVOID, RM_ORDERED };

/* Call after sched_init(). mode: RM_NONE just grants or blocks, RM_AVOID runs the
 * Banker's algorithm on every request, RM_ORDERED rejects out-of-order requests. */
void rm_init(int mode, int nres, const int totals[]);
void rm_set_max(int pid, int rid, int n);          /* declared claim, needed for RM_AVOID */
void rm_enable_monitor(uint64_t interval);         /* periodically detect and recover */
int  rm_detect(int pids[]);                        /* deadlocked process ids right now */
int  rm_deadlocks_found(void);

#endif
