# Large desktop, 4M cells.  PROVISIONAL: GUI MS-1 / M-1 values (see
# desktop-256k.profile); loader = remainder (GUI: 2,086,416 cells).
NAME=desktop-4m
STATUS=provisional
MEM_CELLS=4194304
# GLON-VIEW-CORE-V3 capacity: the code region grows by 1,024 cells (code
# limit 17,152); the main return stack is 7,168 cells (lim/sum recursion
# limit 84 -> 73; GLON-VIEW-CORE-CODECAP.md).  The 64K reference is 16,384.
CODE_CAP=17408
MANAGED_CELLS=1048576
WORKLIST=after-heap
WORKLIST_CELLS=32768
LOADER_CELLS=auto
TAIL_CELLS=0
TASKS=3
TASK_CELLS=3400
TASK_DS_OFF=400
TASK_RS_OFF=3400
GLOBAL_CAP=16384
SYM_CAP=32768
# SITE-1 (GLON-VIEW-CORE-V1): function-site table after the worklist, GUI MS-1/SITE-1 capacity
SITES=after-worklist
SITE_CAP=8192
