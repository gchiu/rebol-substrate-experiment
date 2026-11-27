# Small desktop / embedded, 256K cells.  PROVISIONAL: total memory, managed
# heap, worklist and global/symbol capacities are the GUI line's MS-1 / M-1
# values (OpenViking records gv1b/ms1-memory-map, gv2b-m1-globals; source
# commits c1b3638 / 8685021 not available).  The region order is the core's
# (heap at 32768, worklist after it); the loader takes the remainder, so it is
# larger than the GUI's 120,336 cells.  Code space and stacks as the reference.
NAME=desktop-256k
STATUS=provisional
MEM_CELLS=262144
# GLON-VIEW-CORE-V3 capacity: the code region grows by 1,024 cells (code
# limit 17,152); the main return stack is 7,168 cells (lim/sum recursion
# limit 84 -> 73; GLON-VIEW-CORE-CODECAP.md).  The 64K reference is 16,384.
CODE_CAP=17408
MANAGED_CELLS=65536
WORKLIST=after-heap
WORKLIST_CELLS=2048
LOADER_CELLS=auto
TAIL_CELLS=0
TASKS=3
TASK_CELLS=3400
TASK_DS_OFF=400
TASK_RS_OFF=3400
GLOBAL_CAP=1024
SYM_CAP=2048
# SITE-1 (GLON-VIEW-CORE-V1): function-site table after the worklist, GUI MS-1/SITE-1 capacity
SITES=after-worklist
SITE_CAP=2048
