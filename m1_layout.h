/* m1_layout.h - M1 multitasking memory layout (native + standalone WASM share it).
 *
 * These are the ONLY addresses M1 uses above the frozen R0/S1 substrate.  They
 * are chosen to be disjoint from: the emitted evaluator/collector code
 * (256..~8400), the RV register file (8586..8641), the datatype-library
 * scratch (8642..8645), the standard DS/RS (16384/24576 down), the shared S1
 * heap (32768..40000), the loader heap (40000..47000) and the D1 debugger
 * buffers (8850..8878).  See M1-MULTITASKING-RESULTS.md.
 *
 * This header is NOT part of the frozen substrate; it is M1's own driver-level
 * configuration, shared by the native test driver and the standalone WASM host.
 */
#ifndef M1_LAYOUT_H
#define M1_LAYOUT_H

#include "s1.h"

#define M1_MAIN_ENTRY_CELL 9001   /* seeded by driver: evaluator main entry       */
#define M1_CUR_TASK        9002   /* current task record pointer (raw)            */
#define M1_SCHED_REC       9003   /* scheduler state record (8 cells)             */
#define M1_CURSOR          9011   /* round-robin cursor (slot index)              */
#define M1_STRESS_CNT      9012   /* stress counter (shared, raw)                 */
#define M1_SCRATCH         9015   /* RAW scratch base                             */

#define M1_S0 9015                /* mnew-task: body ptr                          */
#define M1_S1 9016                /* mnew-task: `do` word                         */
#define M1_S2 9017                /* mnew-task: `task-finish` word                */
#define M1_S3 9018                /* mnew-task: slot index i                      */
#define M1_S4 9019                /* mnew-task: record address                    */
#define M1_S5 9020                /* mnew-task: wrapper block ptr                 */
#define M1_S6 9021                /* scheduler: scan record address               */
#define M1_S7 9022                /* scheduler: scan index                        */
#define M1_S8 9023                /* scheduler: scan count                        */
#define M1_S9 9024                /* spin: loop-top address                       */

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
