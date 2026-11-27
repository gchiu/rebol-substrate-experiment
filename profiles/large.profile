# D1's MEM_PROFILE=large defaults, unchanged (json-test, m1-large-test):
# 1,048,576-cell managed heap, 40,000-entry worklist, 200,000-cell loader,
# 3 x 3,400 task stacks, 1,024-cell tail; MEM_CELLS derived = 1,333,104.
NAME=large
STATUS=verified
MEM_CELLS=auto
CODE_CAP=16384
MANAGED_CELLS=1048576
WORKLIST=after-heap
WORKLIST_CELLS=40000
LOADER_CELLS=200000
TAIL_CELLS=1024
TASKS=3
TASK_CELLS=3400
TASK_DS_OFF=400
TASK_RS_OFF=3400
GLOBAL_CAP=256
SYM_CAP=512
# SITE-1 (GLON-VIEW-CORE-V1): the 64K reference's fixed 640-site slot
SITES=fixed
SITE_CAP=640
