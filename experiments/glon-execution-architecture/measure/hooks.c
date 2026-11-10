/* -finstrument-functions hooks: for each emitted cell, the chain of C
 * functions that emitted it, innermost first (scratch measurement only) */
typedef long cell;
extern cell asm_here(void);
#define MEMC 65536
#define DEPTH 6
void *meas_chain[MEMC][DEPTH];
void *meas_owner[MEMC];
static cell stk_start[4096]; static int sp_;
void __cyg_profile_func_enter(void *fn, void *site) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *fn, void *site) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *fn, void *site) { (void)fn; (void)site; if (sp_ < 4096) stk_start[sp_] = asm_here(); sp_++; }
void __cyg_profile_func_exit(void *fn, void *site) {
    (void)site; sp_--;
    if (sp_ >= 4096) return;
    cell e = asm_here();
    for (cell c = stk_start[sp_]; c < e && c < MEMC; c++) {
        if (c < 0) continue;
        if (!meas_owner[c]) meas_owner[c] = fn;
        for (int d = 0; d < DEPTH; d++) { if (!meas_chain[c][d]) { meas_chain[c][d] = fn; break; } if (meas_chain[c][d] == fn) break; }
    }
}
