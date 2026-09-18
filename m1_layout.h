/* m1_layout.h - M1 multitasking memory layout (native + standalone WASM share it).
 *
 * These are the ONLY addresses M1 uses above the frozen R0/S1 substrate.  They
 * are chosen to be disjoint from: the emitted evaluator/collector code
 * (256..~24576), the RV register file + datatype-library scratch + GC state
 * (relocated by M3C to ~24576..25315), the standard DS/RS (16384/24576 down),
 * the shared S1 heap (32768..40000), the loader heap (40000..47000) and the D1
 * debugger buffers.  See M1-MULTITASKING-RESULTS.md.
 *
 * This header is NOT part of the frozen substrate; it is M1's own driver-level
 * configuration, shared by the native test driver and the standalone WASM host.
 */
#ifndef M1_LAYOUT_H
#define M1_LAYOUT_H

#include "s1.h"

#define M1_MAIN_ENTRY_CELL 25010   /* seeded by driver: evaluator main entry       */
#define M1_CUR_TASK        25011   /* current task record pointer (raw)            */
#define M1_SCHED_REC       25012   /* scheduler state record (8 cells)             */
#define M1_CURSOR          25020   /* round-robin cursor (slot index)              */
#define M1_STRESS_CNT      25021   /* stress counter (shared, raw)                 */
#define M1_SCRATCH         25024   /* RAW scratch base                             */

#define M1_S0 25024                /* mnew-task: body ptr                          */
#define M1_S1 25025                /* mnew-task: `do` word                         */
#define M1_S2 25026                /* mnew-task: `task-finish` word                */
#define M1_S3 25027                /* mnew-task: slot index i                      */
#define M1_S4 25028                /* mnew-task: record address                    */
#define M1_S5 25029                /* mnew-task: wrapper block ptr                 */
#define M1_S6 25030                /* scheduler: scan record address               */
#define M1_S7 25031                /* scheduler: scan index                        */
#define M1_S8 25032                /* scheduler: scan count                        */
#define M1_S9 25033                /* spin: loop-top address                       */

/* FIB-OPT-P3: the activation footprint moved from ~16 cells (frame only) to
 * ~73 cells (frame + 48-cell context + 16-align padding) per invocation, so the
 * old 800-cell task RS overflowed well before the repeat-20 stress depth
 * (~1460 cells).  The arena is enlarged to [47000, 65000) and the task count
 * deliberately lowered from 8 to 5 so each task gets 3200 RS cells (>= 2x the
 * repeat-20 requirement) and 400 DS cells (>= 40x the observed ~10).  This is a
 * memory-limit tradeoff: 65536 cells total and the fixed loader-heap boundary at
 * 47000 leave at most ~18.5k cells above the arena; 8 tasks x 3200 RS would not
 * fit.  5 tasks still gives headroom over the <=3 tasks the tests use. */
#define M1_TASK_TABLE      65000  /* task records                                 */
#define M1_TASK_REC_SIZE   16     /* cells per task record                        */
#define M1_MAX_TASKS       5      /* fixed task count for M1                      */
#define M1_WRAPPER_DELTA   80     /* wrapper block base = task table base + delta */
                                   /* (task records occupy 65000..65080; wrappers
                                    * live at 65080 + slot*16, in the free region) */

#define M1_ARENA_BASE      47000  /* per-task stack arena                         */
#define M1_TASK_CELLS      3600   /* cells per task (400 DS + 3200 RS)            */
#define M1_DS_OFF          400    /* DS top offset within a task's region         */
#define M1_RS_OFF          3600   /* RS top offset within a task's region         */

/* task record field offsets */
#define TREC_IP    0
#define TREC_SP    1
#define TREC_RP    2
#define TREC_CUR   3
#define TREC_END   4
#define TREC_CTX   5
#define TREC_BLK   6
#define TREC_FRAME 7
#define TREC_STATE 8

/* task states */
#define TASK_RUNNABLE 0
#define TASK_FINISHED 1
#define TASK_EMPTY    2

#endif /* M1_LAYOUT_H */
