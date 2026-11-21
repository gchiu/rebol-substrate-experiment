#include <stdio.h>
int run_r0_s1_m1_tests(void);
int main(void) {
    int failures = run_r0_s1_m1_tests();
    return failures != 0;
}
