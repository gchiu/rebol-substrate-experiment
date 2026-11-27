# Server: the G3A HTTPS configuration verified at D1 (https-g3a-test, the CI
# "full" package and the endurance workflow; previously spelled
# MEM_PROFILE=large MEM_LOADER_CELLS=500000).  Declared capacities: code cap
# 16,384 (return stack 8,192), managed heap 1,048,576, worklist 40,000,
# loader 500,000, 3 x 3,400 task stacks, 256 globals, 1,024 symbols;
# MEM_CELLS derived = 1,633,104.
#
# SYM_CAP 512 -> 1,024 (GLON-VIEW-CORE-V5, measured): GLON-VOCAB-1 lexical
# binding interns each distinct function-local name once more, decorated, so
# the G3A application needs 638 symbols on the integrated core (420 + server
# on C2) and stopped with symbol_table_full at 512.  SYM_CAP sizes only
# host-side tables (the interner's spelling pointers and three bitmaps); the
# M[] layout, GLOBAL_CAP and the hash radix are unchanged.  1,024 is the
# HTTPS line's own server value (MEM_SYM_CAP=1024).
NAME=server
STATUS=verified
MEM_CELLS=auto
CODE_CAP=16384
MANAGED_CELLS=1048576
WORKLIST=after-heap
WORKLIST_CELLS=40000
LOADER_CELLS=500000
TAIL_CELLS=1024
TASKS=3
TASK_CELLS=3400
TASK_DS_OFF=400
TASK_RS_OFF=3400
GLOBAL_CAP=256
SYM_CAP=1024
# SITE-1 (GLON-VIEW-CORE-V1): the 64K reference's fixed 640-site slot
SITES=fixed
SITE_CAP=640
