REBOL [
    Title:   "Tuple Space v0.02 - waiting law tests"
    Purpose: {
        Exercises the deterministic waiter engine: immediate and parked RD/IN,
        OUT service order, head-of-line behaviour, multiple takers, duplicate
        tuples, and copy ownership of queued patterns and delivered values.
        The full v0.01 law suite remains in tuple-space-v001-tests.r3.
    }
]

do to-file join system/script/path %tuple-space-v002.r3

results: make object! [pass: 0 fail: 0]

check: func [label [string!] expected actual][
    either equal? expected actual [
        results/pass: results/pass + 1
        print rejoin ["ok   " label]
    ][
        results/fail: results/fail + 1
        print rejoin ["FAIL " label " | expected " mold expected " | got " mold actual]
    ]
]

; ---- 0. v0.01 regression through the v0.02 build -------------------------
s0: make-tuple-space
out s0 [n 1]
out s0 [name "ABC"]
out s0 [w Job]
check "0a int does not match decimal" none    rd? s0 [n 1.0]
check "0b string case-sensitive"       none    rd? s0 [name "abc"]
check "0c word case-sensitive"         none    rd? s0 [w job]
check "0d word family shares symbol"   [w Job] rd? s0 [w 'Job]
check "0e wildcard"                    [n 1]   rd? s0 [n *]

; ---- 1. RD immediate success ---------------------------------------------
s1: make-tuple-space
out s1 [x 1]
w1: rd-wait s1 [x 1]
check "1a rd-wait immediate done"    true    waiter-done? w1
check "1b rd-wait immediate result"  [x 1]   waiter-result w1
check "1c rd-wait leaves tuple"      1       length? s1/tuples
check "1d immediate is not queued"   0       length? s1/waiters

; ---- 2. IN immediate success ---------------------------------------------
w2: in-wait s1 [x 1]
check "2a in-wait immediate done"    true    waiter-done? w2
check "2b in-wait immediate result"  [x 1]   waiter-result w2
check "2c in-wait consumed tuple"    0       length? s1/tuples

; ---- 3. RD blocks (parks) when absent ------------------------------------
s3: make-tuple-space
w3: rd-wait s3 [x 1]
check "3a rd-wait parked"            false   waiter-done? w3
check "3b parked waiter queued"      1       length? s3/waiters
check "3c parked result is none"     none    waiter-result w3

; ---- 4. IN blocks (parks) when absent ------------------------------------
w4: in-wait s3 [x 2]
check "4a in-wait parked"            false   waiter-done? w4
check "4b two waiters queued"        2       length? s3/waiters

; ---- 5. OUT wakes RD (and leaves the tuple) ------------------------------
out s3 [x 1]
check "5a OUT wakes RD"              true    waiter-done? w3
check "5b woken RD result"           [x 1]   waiter-result w3
check "5c RD leaves the tuple"       1       length? s3/tuples
check "5d unmatched IN still waits"  false   waiter-done? w4

; ---- 6. OUT wakes IN (and consumes exactly one) --------------------------
out s3 [x 2]
check "6a OUT wakes IN"              true    waiter-done? w4
check "6b woken IN result"           [x 2]   waiter-result w4
check "6c IN consumed the tuple"     1       length? s3/tuples

; ---- 7/8. destructive vs non-destructive are already shown in 5c/6c -------

; ---- 9. RD / RD / IN / RD ordering ---------------------------------------
s9: make-tuple-space
w9a: rd-wait s9 [x 1]
w9b: rd-wait s9 [x 1]
w9c: in-wait s9 [x 1]
w9d: rd-wait s9 [x 1]
out s9 [x 1]
check "9a A (RD) served"             [x 1]   waiter-result w9a
check "9b B (RD) served"             [x 1]   waiter-result w9b
check "9c C (IN) served"             [x 1]   waiter-result w9c
check "9d D (RD) remains waiting"    false   waiter-done? w9d
check "9e C consumed the tuple"      0       length? s9/tuples

; ---- 10. IN / RD / RD ordering -------------------------------------------
s10: make-tuple-space
w10a: in-wait s10 [x 1]
w10b: rd-wait s10 [x 1]
w10c: rd-wait s10 [x 1]
out s10 [x 1]
check "10a A (IN) consumes"          [x 1]   waiter-result w10a
check "10b B (RD) remains waiting"   false   waiter-done? w10b
check "10c C (RD) remains waiting"   false   waiter-done? w10c
check "10d space empty"              0       length? s10/tuples

; ---- 11. head-of-line: a nonmatching head does not block -----------------
s11: make-tuple-space
w11a: in-wait s11 [x 2]
w11b: in-wait s11 [x 1]
out s11 [x 1]
check "11a B (matching) completes"   [x 1]   waiter-result w11b
check "11b A (nonmatching) waits"    false   waiter-done? w11a
check "11c space empty"              0       length? s11/tuples

; ---- 12. two takers, two tuples ------------------------------------------
s12: make-tuple-space
w12a: in-wait s12 [job *]
w12b: in-wait s12 [job *]
out s12 [job 1]
out s12 [job 2]
check "12a A gets oldest tuple"      [job 1] waiter-result w12a
check "12b B gets next tuple"        [job 2] waiter-result w12b
check "12c both consumed"            0       length? s12/tuples

; ---- 13. duplicate tuples are distinct -----------------------------------
s13: make-tuple-space
out s13 [x 1]
out s13 [x 1]
w13a: in-wait s13 [x 1]
w13b: in-wait s13 [x 1]
w13c: in-wait s13 [x 1]
check "13a first immediate"          true    waiter-done? w13a
check "13b second immediate"         true    waiter-done? w13b
check "13c third parked"             false   waiter-done? w13c
check "13d duplicates consumed one-by-one" 0 length? s13/tuples

; ---- 14. queued pattern is a deep copy -----------------------------------
s14: make-tuple-space
p14: [x 1]
w14: rd-wait s14 p14
p14/2: 999
out s14 [x 1]
check "14a queued pattern copy still matches" true  waiter-done? w14
check "14b delivered result is original"      [x 1] waiter-result w14

; ---- 15. delivered value is a deep copy ----------------------------------
s15: make-tuple-space
out s15 [x 1]
w15: rd-wait s15 [x 1]
r15: waiter-result w15
r15/2: 999
check "15a RD delivered copy is independent"  [x 1] rd? s15 [x 1]

s15b: make-tuple-space
w15b: in-wait s15b [x 1]
out s15b [x 1]
r15b: waiter-result w15b
r15b/2: 999
check "15b IN delivered copy is independent"  0 length? s15b/tuples

; ---- 16. words remain data, never executed -------------------------------
s16: make-tuple-space
out s16 [quit 99]
w16: rd-wait s16 [quit 99]
check "16a quit word stored as data"  [quit 99] waiter-result w16
w16b: in-wait s16 [quit 99]
check "16b quit word consumed as data" [quit 99] waiter-result w16b
check "16c script still running"       true      true

; ---- 17. deterministic repeated runs -------------------------------------
run-case: func [n /local i ok s a b c d][
    ok: true
    repeat i n [
        s: make-tuple-space
        a: rd-wait s [x 1]
        b: rd-wait s [x 1]
        c: in-wait s [x 1]
        d: rd-wait s [x 1]
        out s [x 1]
        unless all [
            equal? waiter-result a [x 1]
            equal? waiter-result b [x 1]
            equal? waiter-result c [x 1]
            not (waiter-done? d)
            equal? (length? s/tuples) 0
        ][ok: false]
    ]
    ok
]
check "17 deterministic over 5 repeated runs" true run-case 5

; ---- summary -------------------------------------------------------------
print rejoin ["--- " results/pass " passed, " results/fail " failed ---"]
if results/fail > 0 [print "RESULT: FAIL" quit/return 1]
print "RESULT: PASS"
quit/return 0
