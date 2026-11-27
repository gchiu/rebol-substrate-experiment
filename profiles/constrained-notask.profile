# The 64K map without M1 tasks: no task arena and no task table, so the
# loader runs from 40000 to the end of memory (25,536 cells) without sharing
# any cell with task state.  M1_MAX_TASKS is 0: a task cannot be created.
#
# Replaces two D1 overrides that let the loader overlap task state:
#   GLON_HTTP_RECLAIM_M1=1 (-DR0S1_HEAP_LIMIT=65000; G1-G3 HTTPS tests): the
#     loader covered the arena; 25,000 cells -> 25,536 here.
#   KAKA_LIMIT_FLAG (-DR0S1_HEAP_LIMIT=65536; Kaka game, native tests and
#     WASM): the loader also covered the task table at 65000..65536 while the
#     harnesses and the runtime wrote task records there.  Measured (C2):
#     kaka-fighter's loader reaches 65,286, so ending the loader at 65000 is
#     not enough; this profile keeps Kaka's 25,536 cells without overlap.
NAME=constrained-notask
STATUS=verified
MEM_CELLS=65536
CODE_CAP=16384
MANAGED_CELLS=7232
WORKLIST=fixed
WORKLIST_CELLS=256
LOADER_CELLS=auto
TAIL_CELLS=0
TASKS=0
TASK_CELLS=3400
TASK_DS_OFF=400
TASK_RS_OFF=3400
GLOBAL_CAP=256
SYM_CAP=512
