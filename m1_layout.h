/* m1_layout.h - M1 multitasking memory layout (native + standalone WASM share it).
 *
 * These are the ONLY addresses M1 uses above the frozen R0/S1 substrate.  They
 * are chosen to be disjoint from: the emitted evaluator/collector code
 * (256..~24576), the RV register file + datatype-library scratch + GC state
 * (relocated by M3C to ~24576..25315), the standard DS/RS (16384/24576 down),
 * the shared S1 heap (32768..40000), the loader heap (40000..50600) and the D1
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
 * (~1460 cells).  The arena uses 3200 RS cells per task (>= 2x the repeat-20
 * requirement) and 400 DS cells (>= 40x the observed ~10).
 *
 * The loader-heap boundary moved from 47000 to 50600 (see r0_s1.h): the G1E
 * demo launcher (launcher + shop + guide + merchant-flow) plus the G1A
 * route/event bridge's per-dispatch re-parse overhead outgrew the original
 * 7000-cell loader arena. The task count was lowered from 5 to 4 (the M1/M2
 * tests use at most 3 tasks, so 4 keeps a full task of headroom) and the arena
 * base moved up to 50600, keeping the arena end at 65000 (where the task table
 * lives). This is a memory-limit tradeoff: 65536 cells total.
 *
 * The merchant-flow demo (a real M1 multitasking + dataflow visualisation:
 * mnew-task/yield/task-finish/run-tasks + three competing workers + a router +
 * an SVG/HTML rendering) outgrew that again, so the task count was lowered from
 * 4 to 3 (the M1/M2 tests and the demo use exactly 3 tasks) and the arena base
 * moved up to 54200, giving the loader heap [40000, 54200). The arena still
 * ends at 65000 (task table). This is the last task slot the tests/demo can
 * spare: 3 is the minimum M1_MAX_TASKS the tests require.
 *
 * The tuple-space/dataflow demo + its canvas visualisation outgrew that once
 * more, so the per-task RS was trimmed from 3200 to 3000 cells (still >= 2x the
 * repeat-20 requirement) and the arena base moved up to 54800, giving the
 * loader heap [40000, 54800). The arena still ends at 65000. */
/* The tuple-space experiment (v0.03) needs more concurrent tasks than the
 * native M1 tests/demo (which use 3). The defaults below are unchanged for
 * every existing build; a separate experimental runtime object may override
 * M1_MAX_TASKS / M1_TASK_CELLS / M1_RS_OFF on the compiler command line
 * (the values are guarded so the default build is bit-identical in effect). */
#ifndef M1_TASK_TABLE
#define M1_TASK_TABLE      65000  /* task records                                 */
#endif
#ifndef M1_TASK_REC_SIZE
#define M1_TASK_REC_SIZE   16     /* cells per task record                        */
#endif
#ifndef M1_MAX_TASKS
#define M1_MAX_TASKS       3      /* fixed task count for M1                      */
#endif
#ifndef M1_WRAPPER_DELTA
#define M1_WRAPPER_DELTA   80     /* wrapper block base = task table base + delta */
                                   /* (task records occupy 65000..65048; wrappers
                                    * live at 65080 + slot*16, in the free region) */
#endif

#ifndef M1_ARENA_BASE
#define M1_ARENA_BASE      54800  /* per-task stack arena                         */
#endif
#ifndef M1_TASK_CELLS
#define M1_TASK_CELLS      3400   /* cells per task (400 DS + 3000 RS)            */
#endif
#ifndef M1_DS_OFF
#define M1_DS_OFF          400    /* DS top offset within a task's region         */
#endif
#ifndef M1_RS_OFF
#define M1_RS_OFF          3400   /* RS top offset within a task's region         */
#endif

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
