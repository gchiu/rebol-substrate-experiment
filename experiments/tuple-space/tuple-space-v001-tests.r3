REBOL [
    Title:   "Tuple Space v0.01 - executable law tests"
    Purpose: {
        Exercises storage, matching, ordering, destructive vs non-destructive
        retrieval, copy ownership and fail-fast input behaviour.
    }
]

do to-file join system/script/path %tuple-space-v001.r3

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

; ---- 1. make empty space -------------------------------------------------
s1: make-tuple-space
check "1  make empty space" 0 length? s1/tuples

; ---- 2. rd? empty -> none ------------------------------------------------
check "2  rd? empty -> none" none rd? s1 [job * pending]

; ---- 3. in? empty -> none ------------------------------------------------
check "3  in? empty -> none" none in? s1 [job * pending]

; ---- 4. exact match ------------------------------------------------------
s: make-tuple-space
out s [job 17 pending]
check "4  exact match" [job 17 pending] rd? s [job 17 pending]

; ---- 5. exact mismatch ---------------------------------------------------
check "5  exact mismatch" none rd? s [job 17 running]

; ---- 6. wildcard match ---------------------------------------------------
check "6  wildcard match" [job 17 pending] rd? s [job * pending]

; ---- 7. wildcard mismatch in another field -------------------------------
check "7  wildcard mismatch other field" none rd? s [job * running]

; ---- 8. length mismatch --------------------------------------------------
check "8a length too short" none rd? s [job *]
check "8b length too long"  none rd? s [job * pending extra]

; ---- 9. oldest matching tuple wins ---------------------------------------
s9: make-tuple-space
out s9 [job 1 pending]
out s9 [job 2 pending]
out s9 [job 3 pending]
check "9  oldest match wins" [job 1 pending] rd? s9 [job * pending]

; ---- 10. rd? does not remove ---------------------------------------------
check "10 rd? does not remove" 3 length? s9/tuples

; ---- 11. in? removes exactly one -----------------------------------------
check "11a in? removes oldest" [job 1 pending] in? s9 [job * pending]
check "11b exactly one removed" 2 length? s9/tuples
check "11c next is job 2"      [job 2 pending] rd? s9 [job * pending]

; ---- 12. duplicate tuples are distinct entries ---------------------------
s12: make-tuple-space
out s12 [x 1]
out s12 [x 1]
check "12a duplicates stored" 2 length? s12/tuples
check "12b in? removes one"   [x 1] in? s12 [x 1]
check "12c one remains"       1 length? s12/tuples
check "12d rd? finds remainder" [x 1] rd? s12 [x 1]

; ---- 13. OUT stores a deep copy the caller cannot mutate -----------------
s13: make-tuple-space
t: [job 17 pending]
out s13 t
t/3: 'cancelled
print rejoin ["     caller tuple now " mold t]
check "13 OUT owns its copy" [job 17 pending] rd? s13 [job * pending]

; ---- 14. RD? returns an independent copy ---------------------------------
s14: make-tuple-space
out s14 [job 17 pending]
r14: rd? s14 [job * pending]
r14/2: 999
check "14 RD? copy independent" [job 17 pending] rd? s14 [job * pending]

; ---- 15. IN? returns an independent copy ---------------------------------
s15: make-tuple-space
out s15 [job 17 pending]
out s15 [job 18 pending]
got15: in? s15 [job * pending]
got15/2: 555
check "15 IN? copy independent" [job 18 pending] rd? s15 [job * pending]

; ---- 16. heterogeneous tuple values --------------------------------------
s16: make-tuple-space
h16: [state 200 "car-7" 12.5 [nested x] ok]
out s16 h16
check "16a heterogeneous exact"    h16 rd? s16 [state 200 "car-7" 12.5 [nested x] ok]
check "16b heterogeneous wildcard" h16 rd? s16 [state * * * * *]

; ---- 17. words remain data, never executed -------------------------------
s17: make-tuple-space
out s17 [quit 99]
out s17 [print "SHOULD-NOT-PRINT"]
out s17 [job 17 pending]
check "17a quit tuple stored"  [quit 99] rd? s17 [quit 99]
check "17b print tuple stored" [print "SHOULD-NOT-PRINT"] rd? s17 [print *]
check "17c execution continued" [job 17 pending] rd? s17 [job * pending]

; ---- 18. wildcard works in different positions ---------------------------
s18: make-tuple-space
out s18 [job 17 pending]
check "18a wildcard first"  [job 17 pending] rd? s18 [* 17 pending]
check "18b wildcard middle" [job 17 pending] rd? s18 [job * pending]
check "18c wildcard last"   [job 17 pending] rd? s18 [job 17 *]
check "18d all wildcards"   [job 17 pending] rd? s18 [* * *]

; ---- 19. none is ordinary data; "*" is the only wildcard -----------------
s19: make-tuple-space
out s19 [x none]
out s19 [x 5]
check "19a none matches exactly"  [x none] rd? s19 [x none]
check "19c wildcard matches none" [x none] rd? s19 [x *]
s19b: make-tuple-space
out s19b [x 5]
check "19b none is not wildcard"  none rd? s19b [x none]

s19d: make-tuple-space
out s19d [x *]
check "19d stored * is ordinary data" none  rd? s19d [x 5]
check "19e stored * matches itself"   [x *] rd? s19d [x *]
s19f: make-tuple-space
out s19f [x 5]
check "19f pattern * is wildcard"     [x 5] rd? s19f [x *]

; ---- 20. input / error law: fail fast ------------------------------------
sE: make-tuple-space
check "20a OUT non-block fails"   true error? try [out sE 42]
check "20b RD? non-block fails"   true error? try [rd? sE 42]
check "20c IN? non-block fails"   true error? try [in? sE 42]
check "20d malformed space fails" true error? try [rd? (make object! [kind: 'nope tuples: copy []]) [x]]
check "20e non-object space fails" true error? try [rd? 42 [x]]

; ---- 21. tuple-field equality law (from the R3 equality audit) -----------
s21: make-tuple-space
out s21 [n 1]
check "21a int matches int"            [n 1] rd? s21 [n 1]
check "21b int does not match decimal" none  rd? s21 [n 1.0]

s21c: make-tuple-space
out s21c [n 1.0]
check "21c decimal matches decimal"     [n 1.0] rd? s21c [n 1.0]
check "21d decimal does not match int"  none    rd? s21c [n 1]

s21s: make-tuple-space
out s21s [name "ABC"]
check "21e string case sensitive (equal)" [name "ABC"] rd? s21s [name "ABC"]
check "21f string case sensitive (diff)"  none         rd? s21s [name "abc"]

s21w: make-tuple-space
out s21w [job 1]
check "21g word case-sensitive (same)"  [job 1] rd? s21w [job 1]
check "21g2 word case-sensitive (JOB)"  none    rd? s21w [JOB 1]

ca21: context [slot: 1]
cb21: context [slot: 2]
tupA21: [slot 7]
patB21: [slot 7]
bind tupA21 ca21
bind patB21 cb21
s21b: make-tuple-space
out s21b tupA21
check "21h bindings differ (control)" false     same? (first tupA21) (first patB21)
check "21i binding ignored in match"  [slot 7]  rd? s21b patB21

s21k: make-tuple-space
out s21k [x [1 2]]
check "21j block structural match"    [x [1 2]] rd? s21k [x [1 2]]
check "21k block structural mismatch" none      rd? s21k [x [1 3]]
check "21l nested string case"        none      rd? s21k [x [1 "A"]]
check "21m nested int vs decimal"     none      rd? s21k [x [1 2.0]]

s21n: make-tuple-space
out s21n [x [1 "A"]]
check "21n nested string exact" [x [1 "A"]] rd? s21n [x [1 "A"]]

s21f: make-tuple-space
out s21f [job 1]
check "21o word matches lit-word" [job 1] rd? s21f ['job 1]
check "21p word matches get-word" [job 1] rd? s21f [:job 1]
check "21q word matches set-word" [job 1] rd? s21f [job: 1]
check "21r word vs JOB mismatch"  none    rd? s21f [JOB 1]

s21nb: make-tuple-space
nA21: [x [slot 7]]
nB21: [x [slot 7]]
bind nA21 ca21
bind nB21 cb21
out s21nb nA21
check "21s nested differently-bound words match" [x [slot 7]] rd? s21nb nB21

; ---- summary -------------------------------------------------------------
print rejoin ["--- " results/pass " passed, " results/fail " failed ---"]
if results/fail > 0 [print "RESULT: FAIL" quit/return 1]
print "RESULT: PASS"
quit/return 0
