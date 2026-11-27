# The 64K reference (frozen S1 map; D1 3a194a2 / C1 df15c05).  Built from the
# frozen s1.c and the header defaults directly; this file declares the same
# values so that `make profile-check` can prove they are the defaults.
NAME=constrained
STATUS=reference
MEM_CELLS=65536
CODE_CAP=16384
MANAGED_CELLS=7232
WORKLIST=fixed
WORKLIST_CELLS=256
LOADER_CELLS=14800
TAIL_CELLS=0
TASKS=3
TASK_CELLS=3400
TASK_DS_OFF=400
TASK_RS_OFF=3400
GLOBAL_CAP=256
SYM_CAP=512
# SITE-1 (GLON-VIEW-CORE-V1): the 64K reference's fixed 640-site slot
SITES=fixed
SITE_CAP=640
