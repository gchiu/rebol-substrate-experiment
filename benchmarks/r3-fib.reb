REBOL [
    title:   "Naive recursive Fibonacci benchmark (evaluation only)"
    purpose: "Match the Glon P1-P5 naive fib workload for a same-PC comparison."
    notes:   "No memoisation/iteration/optimisation. Uses now/precise (ms)."
]

fib: func [n] [
    either n <= 1 [
        n
    ][
        (fib (n - 1)) + (fib (n - 2))
    ]
]

; ---- correctness (must equal 55 / 610 / 6765 / 75025) ----
print ["fib 10 =" fib 10]
print ["fib 15 =" fib 15]
print ["fib 20 =" fib 20]
print ["fib 25 =" fib 25]

; ---- timing helper: elapsed seconds (decimal) for one fib n ----
elapsed: func [n] [
    t0: now/precise
    fib n
    to decimal! difference now/precise t0
]

; ---- warmup ----
fib 25
fib 25

; ---- individual timed runs of fib 25 (7 reps) ----
print "--- fib 25 individual runs (s) ---"
runs: copy []
repeat i 7 [
    r: elapsed 25
    append runs r
    print [i r]
]
sort runs
print ["median" runs/4 "min" first runs "max" last runs]

; ---- batch timings (precision) ----
; per-call = total / reps
batch: func [n reps] [
    t0: now/precise
    loop reps [fib n]
    (to decimal! difference now/precise t0) / reps
]

print "--- batch per-call timings (s) ---"
print ["fib 10 " batch 10 50000]
print ["fib 15 " batch 15 5000]
print ["fib 20 " batch 20 1000]
print ["fib 25 " batch 25 100]
