/* r0_s1_layout.h - compile-time memory-layout check (GLON-CORE-C2).
 *
 * Included by r0_s1_runtime.c, so every build of the runtime -- the default
 * constrained 64K reference, a generated profile (scripts/glon-profile.sh),
 * the separate test builds and the WASM hosts -- is checked, however its
 * constants were supplied.  A layout whose regions overlap, or whose
 * capacities cannot be represented safely, does not compile.
 *
 * The map (all addresses in cells), low to high:
 *   [0, 256)                       registers and scratch
 *   [256, R0S1_CODE_LIMIT)         emitted code (+ masm libraries)
 *   [R0S1_CODE_LIMIT, R0S1_DS_INIT) data-stack reserve; the main data stack
 *                                  grows down from R0S1_DS_INIT
 *   [R0S1_DS_INIT, R0S1_RS_INIT)   main return stack (grows down)
 *   [24576, 32768)                 fixed runtime-state cells (RV_*, GC_*, M1_*,
 *                                  M3 tables, G1 buffers, site/escape table)
 *   [32768, R0S1_HEAP_BASE)        managed heap (GC_HEAP_BASE..GC_HEAP_LIMIT)
 *   [GC_WORKLIST, +GC_WORKLIST_CAP) GC worklist: either the fixed 256-entry
 *                                  slot at 25057, or directly after the heap
 *   [R0S1_LOAD_BASE, R0S1_HEAP_LIMIT) loader heap
 *   [M1_ARENA_BASE, M1_TASK_TABLE) M1 task stacks (none when M1_MAX_TASKS is 0)
 *   [M1_TASK_TABLE, +536)          task records and wrapper blocks
 * The frozen S1 fixes code base 256, RS top 24576 and heap base 32768. */
#ifndef R0S1_LAYOUT_H
#define R0S1_LAYOUT_H

#include "r0_s1.h"
#include "m1_layout.h"

#ifdef GLON_PROFILE_MEM_CELLS
#define R0S1_MEM_CELLS GLON_PROFILE_MEM_CELLS   /* generated profile */
#else
#define R0S1_MEM_CELLS ((long)MEM_CELLS)        /* frozen s1.h: 65536 */
#endif
#define R0S1_CODE_BASE         256L
#define R0S1_FIXED_STATE_BASE  24576L
/* The task table region: 536 cells (records + wrapper blocks) when the
 * profile has M1 tasks, none when it has none (M1_MAX_TASKS == 0). */
#define R0S1_TASK_TABLE_CELLS  (M1_MAX_TASKS > 0 ? 536L : 0L)

/* Minimum main return stack.  4,096 cells is the smallest main return stack
 * any verified configuration has run with (the HTTPS server line's
 * code cap 20,480; GLON-S1-CLAUDE-D1).  One closure activation reserves at
 * most 96 cells, so this allows about 40 nested activations. */
#define R0S1_RS_MIN            4096L

/* ---- code, stacks and the fixed state (GLON-S1-CLAUDE-D1 73fcc44) -------- */
_Static_assert(R0S1_RS_INIT == 24576L,
               "layout: the main return-stack top is fixed by the frozen S1 at 24576");
_Static_assert(R0S1_RS_INIT <= R0S1_FIXED_STATE_BASE,
               "layout: the main return stack must not overlap the fixed runtime-state cells at 24576");
_Static_assert(GC_META >= R0S1_RS_INIT && RV_BASE >= R0S1_RS_INIT && M1_MAIN_ENTRY_CELL >= R0S1_RS_INIT,
               "layout: a fixed runtime-state cell lies inside the main return stack");
_Static_assert(R0S1_DS_INIT >= 16384L,
               "layout: the code space must not be smaller than the 64K reference (code cap 16384)");
_Static_assert(R0S1_RS_INIT - R0S1_DS_INIT >= R0S1_RS_MIN,
               "layout: the main return stack is below its 4096-cell minimum (code cap too high)");
_Static_assert(R0S1_CODE_LIMIT > R0S1_CODE_BASE,
               "layout: no code space");

/* ---- fixed-state region ---------------------------------------------------- */
_Static_assert(GC_HEAP_BASE == 32768L,
               "layout: the managed heap starts at the frozen S1 HEAP_BASE 32768");
_Static_assert(G1_OUT_DATA + G1_OUT_CAP <= G1_VIS && G1_VIS_DATA + G1_VIS_CAP <= SITE_PARENT_BASE
               && RV_ESC_W < GC_HEAP_BASE,
               "layout: the fixed runtime-state buffers overlap or reach the managed heap");

/* ---- managed heap, worklist, loader ---------------------------------------- */
_Static_assert(R0S1_HEAP_BASE > GC_HEAP_BASE,
               "layout: the managed heap is empty");
_Static_assert((GC_WORKLIST == 25057L && GC_WORKLIST_CAP <= 256L)
               || (GC_WORKLIST >= GC_HEAP_LIMIT && GC_WORKLIST + GC_WORKLIST_CAP <= R0S1_LOAD_BASE),
               "layout: the GC worklist is neither the fixed 256-entry slot nor between the heap and the loader");
_Static_assert(R0S1_LOAD_BASE >= R0S1_HEAP_BASE,
               "layout: the loader heap overlaps the managed heap");
_Static_assert(R0S1_HEAP_LIMIT > R0S1_LOAD_BASE,
               "layout: the loader heap is empty");

/* ---- M1 task arena and table ----------------------------------------------- */
/* No exception: the loader never shares cells with the task arena or table.
 * A profile without tasks declares M1_MAX_TASKS 0 (no arena, no table). */
#ifdef R0S1_ARENA_RECLAIMED
#error "R0S1_ARENA_RECLAIMED is not supported: declare a profile with TASKS=0 instead"
#endif
_Static_assert(M1_MAX_TASKS >= 0, "layout: negative task count");
_Static_assert(R0S1_HEAP_LIMIT <= M1_ARENA_BASE,
               "layout: the loader heap overlaps the M1 task arena or task table");
_Static_assert(M1_ARENA_BASE + (long)M1_MAX_TASKS * M1_TASK_CELLS <= M1_TASK_TABLE,
               "layout: the M1 task stacks overlap the task table");
_Static_assert(M1_MAX_TASKS == 0 || (M1_DS_OFF > 0 && M1_DS_OFF < M1_RS_OFF && M1_RS_OFF <= M1_TASK_CELLS),
               "layout: a task's data and return stacks do not fit its slot");
_Static_assert(M1_MAX_TASKS == 0
               || ((long)M1_MAX_TASKS * M1_TASK_REC_SIZE <= M1_WRAPPER_DELTA
                   && M1_WRAPPER_DELTA + (long)M1_MAX_TASKS * 16 <= R0S1_TASK_TABLE_CELLS),
               "layout: task records and wrapper blocks do not fit the 536-cell task table region");
_Static_assert(M1_TASK_TABLE + R0S1_TASK_TABLE_CELLS <= R0S1_MEM_CELLS,
               "layout: the regions end beyond MEM_CELLS");

#endif /* R0S1_LAYOUT_H */
