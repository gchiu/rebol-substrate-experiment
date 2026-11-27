# Server: the G3A HTTPS configuration verified at D1 (https-g3a-test, the CI
# "full" package and the endurance workflow; previously spelled
# MEM_PROFILE=large MEM_LOADER_CELLS=500000).  Declared capacities: code cap
# 16,384 (return stack 8,192), managed heap 1,048,576, worklist 40,000,
# loader 500,000, 3 x 3,400 task stacks, 256 globals, 512 symbols;
# MEM_CELLS derived = 1,633,104.
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
SYM_CAP=512
