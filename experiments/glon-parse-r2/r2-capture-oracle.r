REBOL []
out: func [label result value] [print rejoin [label "=" mold result " cap=" mold value]]
x: "SENTINEL"
out "I1" parse "abc" [copy x "a"] x
x: "SENTINEL"
out "I2" parse "abc" [copy x "z"] x
x: "SENTINEL"
out "I3" parse "abc" [copy x "a" "z"] x
x: "SENTINEL"
out "I4" parse "abc" [copy x "a" "z" | "abc"] x
x: "SENTINEL"
out "I5" parse "ab" [copy x "a" copy x "b"] x
x: "SENTINEL"
out "I6" parse "ab" [copy x ["a" "b"]] x
x: "SENTINEL"
out "I7" parse "aaa" [copy x some "a"] x
x: "SENTINEL"
out "I8" parse "" [copy x any "a"] x
x: "SENTINEL"
out "I9" parse "" [copy x opt "a"] x
x: "SENTINEL"
out "I10" parse "" [copy x ""] x
x: "SENTINEL"
out "I11" parse "ABC" [copy x "a"] x
x: "SENTINEL"
out "I12" parse "aaa" [copy x 2 "a"] x
x: "SENTINEL"
out "I13" parse "aaa" [copy x 1 3 "a"] x
x: "SENTINEL"
out "I14" parse "a" [copy x skip] x
x: "SENTINEL"
out "I15" parse "abc" [copy x ["a" | "b"] "bc"] x
x: "SENTINEL"
probe-set: does [parse "abc" [set x "a"]]
out "I16" either error? e: try [probe-set] ["error"] ["no-error"] x
quit
