#include <stdio.h>

int run_tests(void);
int run_adversarial_tests(void);
int run_claim_tests(void);
int run_r0_tests(void);
int run_r0_s1_tests(void);
int run_r0_s1_debug_tests(void);
int run_r0_s1_m1_tests(void);
int run_r0_s1_m2_tests(void);
int run_r0_s1_m3_tests(void);
int run_r0_s1_m3b_tests(void);
int run_r0_s1_m3c_tests(void);
int run_r0_s1_m3d_tests(void);
int run_r0_s1_nested_closure_tests(void);
int run_r0_s1_p4_tests(void);
int run_r0_s1_p5_tests(void);
int run_r0_s1_g1a_tests(void);
int run_r0_s1_g1b_tests(void);
int run_r0_s1_g1c_tests(void);
int run_r0_s1_g1d_tests(void);
int run_r0_s1_g1e_tests(void);
int run_r0_s1_lambda_tests(void);
int run_r0_s1_masm_tests(void);
int run_r0_s1_gc_safepoint_tests(void);
int run_r0_s1_invoke_tests(void);
int run_r0_s1_reduce_tests(void);
int run_r0_s1_parse_tests(void);
int run_r0_s1_equality_tests(void);
int run_r0_s1_bound_tests(void);
int run_r0_s1_case_tests(void);
int run_r0_s1_traffic_tests(void);
int run_r0_s1_newell_tests(void);
int run_r0_s1_ovm_tests(void);
int run_r0_s1_nasch_tests(void);

int main(void) {
    int failures = run_tests();
    failures += run_adversarial_tests();
    failures += run_claim_tests();
    failures += run_r0_tests();
    failures += run_r0_s1_tests();
    failures += run_r0_s1_debug_tests();
    failures += run_r0_s1_m1_tests();
    failures += run_r0_s1_m2_tests();
    failures += run_r0_s1_m3_tests();
    failures += run_r0_s1_m3b_tests();
    failures += run_r0_s1_m3c_tests();
    failures += run_r0_s1_m3d_tests();
    failures += run_r0_s1_nested_closure_tests();
    failures += run_r0_s1_p4_tests();
    failures += run_r0_s1_p5_tests();
    failures += run_r0_s1_g1a_tests();
    failures += run_r0_s1_g1b_tests();
    failures += run_r0_s1_g1c_tests();
    failures += run_r0_s1_g1d_tests();
    failures += run_r0_s1_g1e_tests();
    failures += run_r0_s1_lambda_tests();
    failures += run_r0_s1_masm_tests();
    failures += run_r0_s1_gc_safepoint_tests();
    failures += run_r0_s1_invoke_tests();
    failures += run_r0_s1_reduce_tests();
    failures += run_r0_s1_parse_tests();
    failures += run_r0_s1_equality_tests();
    failures += run_r0_s1_bound_tests();
    failures += run_r0_s1_case_tests();
    failures += run_r0_s1_traffic_tests();
    failures += run_r0_s1_newell_tests();
    failures += run_r0_s1_ovm_tests();
    failures += run_r0_s1_nasch_tests();
    if (failures == 0) {
        printf("all tests passed\n");
        return 0;
    }
    printf("%d test(s) failed\n", failures);
    return 1;
}
