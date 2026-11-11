#include "b12_machine.h"
extern cell M[MEM_CELLS];
#include <stdio.h>
int main(void) {
    M[256] = B3_OP_TO_R;
    M[257] = OP_HALT;
    b3_bad_opcode = 0;
    s1_reset();
    s1_run(256);
    printf("nine native >R guard: %s\n", b3_bad_opcode ? "REJECTED" : "NOT REJECTED");
    return b3_bad_opcode ? 0 : 1;
}
