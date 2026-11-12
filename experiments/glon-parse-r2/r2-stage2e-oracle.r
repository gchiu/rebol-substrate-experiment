REBOL []
out: func [l r v] [print rejoin [l "=" mold r " cap=" mold v]]
out "J1" parse "" [none] none
out "J2" parse "a" [none] none
out "J3" parse "" [none end] none
out "J4" parse "a" [none "a"] none
out "J5" parse "" ["a" | none] none
out "J6" parse "a" ["a" | none] none
out "J7" parse "b" ["a" | none] none
x: "S"
out "J8" parse "" [copy x none] x
x: "S"
out "J9" parse "a" [copy x none] x
x: "S"
out "J10" parse "" [copy x ["a" | none]] x
x: "S"
out "J11" parse "a" [copy x ["a" | none]] x
out "J12" parse "a" [some none] none
out "J14" parse "" [any none] none
probe-ahead: does [parse "a" [ahead "a"]]
out "AHEAD" either error? e: try [probe-ahead] ["error"] ["no-error"] none
probe-not: does [parse "a" [not "a"]]
out "NOT" either error? e: try [probe-not] ["error"] ["no-error"] none
quit
