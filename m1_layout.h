/* m1_layout.h - M1 multitasking memory layout (native + standalone WASM share it).
 *
 * These are the ONLY addresses M1 uses above the frozen R0/S1 substrate.  They
 * are chosen to be disjoint from: the frozen evaluator code (256..~4300), the
 * RV register file (8192..8261), the standard DS/RS (16384/24576 down), the
 * shared S1 heap (32768..40000), the loader heap (40000..47000) and the D1
 * debugger buffers (6000..6028).  See M1-MULTITASKING-RESULTS.md.
 *
 * This header is NOT part of the frozen substrate; it is M1's own driver-level
 * configuration, shared by the native test driver and the standalone WASM host.
 */
#ifndef M1_LAYOUT_H
#define M1_LAYOUT_H

#include "s1.h"

#define M1_MAIN_ENTRY_CELL 8262   /* seeded by driver: evaluator main entry       */
#define M1_CUR_TASK        8263   /* current task record pointer (raw)            */
#define M1_SCHED_REC       8264   /* scheduler state record (8 cells)             */
#define M1_CURSOR          8272   /* round-robin cursor (slot index)              */
#define M1_STRESS_CNT      8273   /* stress counter (shared, raw)                 */
#define M1_SCRATCH         8276   /* RAW scratch base                             */

#define M1_S0 8276                /* mnew-task: body ptr                          */
#define M1_S1 8277                /* mnew-task: `do` word                         */
#define M1_S2 8278                /* mnew-task: `task-finish` word                */
#define M1_S3 8279                /* mnew-task: slot index i                      */
#define M1_S4 8280                /* mnew-task: record address                    */
#define M1_S5 8281                /* mnew-task: wrapper block ptr                 */
#define M1_S6 8282                /* scheduler: scan record address               */
#define M1_S7 8283                /* scheduler: scan index                        */
#define M1_S8 8284                /* scheduler: scan count                        */
#define M1_S9 8285                /* spin: loop-top address                       */

#define M1_TASK_TABLE      60000  /* task records                                 */
#define M1_TASK_REC_SIZE   16     /* cells per task record                        */
#define M1_MAX_TASKS       8      /* fixed task count for M1                      */
#define M1_WRAPPER_DELTA   128    /* wrapper block base = task table base + delta */
                                   /* (task records occupy 60000..60128; wrappers
                                    * live at 60128 + slot*16, in the free region) */

#define M1_ARENA_BASE      47000  /* per-task stack arena                         */
#define M1_TASK_CELLS      1600   /* cells per task (800 DS + 800 RS)             */
#define M1_DS_OFF          800    /* DS top offset within a task's region         */
#define M1_RS_OFF          1600   /* RS top offset within a task's region         */

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
