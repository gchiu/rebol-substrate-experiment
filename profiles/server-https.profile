# Application server with the HTTPS line's capacities (GLON-S1-CLAUDE-D1,
# HTTP-H3E, JSON-J1/J2 on experiment/glon-json-j2): code cap 20,480 (main
# return stack 4,096 -- the minimum), 512 globals, 2,048 symbols, loader
# 500,000.  PROVISIONAL on the core: those values were verified with the
# HTTPS line's task/HTTP/JSON code, which is not yet fully integrated here.
# GLON-C3 (Glon Worlds Native profile): SYM_CAP 1,024 -> 2,048.  The full W5.1
# application measured 687 symbols on C2 and 817 with GLON-VOCAB-1 lexical
# binding (+19%); SYM_CAP sizes host-side tables only, so M[] and the layout
# are unchanged.  CODE_CAP stays 20,480 (17,368 cells measured for the loaded
# W5.1 stack).
NAME=server-https
STATUS=provisional
MEM_CELLS=auto
CODE_CAP=20480
MANAGED_CELLS=1048576
WORKLIST=after-heap
WORKLIST_CELLS=40000
LOADER_CELLS=500000
TAIL_CELLS=1024
TASKS=3
TASK_CELLS=3400
TASK_DS_OFF=400
TASK_RS_OFF=3400
GLOBAL_CAP=512
SYM_CAP=2048
# SITE-1 (GLON-VIEW-CORE-V1): the 64K reference's fixed 640-site slot
SITES=fixed
SITE_CAP=640
