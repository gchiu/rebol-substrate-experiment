<!-- Generated from demo/shop/primer.txt by demo/shop/build-primer.py. Edit primer.txt, then rerun the script. -->

# Glon in 5 minutes

*Saturnine · A Glonbook*

Glon is a small Rebol-family language that runs on a tiny frozen machine (S1), natively and as WebAssembly in the browser. This primer teaches enough Glon to read and change simple programs.

This primer is **instructional**. The normative rules are in [GLON-ALPHA-LAWS.md](GLON-ALPHA-LAWS.md); where the two differ, the laws win.

Every example here is real: the live primer runs each one through the actual Glon WebAssembly runtime, and the native test suite checks every result shown below. The result of an example is shown after `=>`: it is the value of the program's last expression. A result starting `** uncaught` means the program was stopped by a raised SIN! that nothing caught.

Live, editable version: <https://gchiu.github.io/rebol-substrate-experiment/shop/primer.html>

## 1. Values and words

A program is a sequence of values. Numbers are integers. A **word** names a value; a **set-word** (a word with a trailing colon) gives it one.

```
x: 5
+ x 3
;; => 8
```

Glon calls are **prefix**: the function comes first and takes a fixed number of arguments. There is no operator precedence and there are no parentheses; each function simply takes the next complete expressions it needs. `+ 1 * 2 3` reads as `+ 1 (* 2 3)`.

```
+ 1 * 2 3
;; => 7
```

A word with a leading quote (`'tea`) is a word used as data, not looked up.

```
drink: 'tea
drink
;; => tea
```

## 2. Blocks are data

Square brackets make a **block**. A block is just data (a list of values) until something evaluates it. `do` evaluates a block.

```
[ + 2 3 ]
;; => [ + 2 3 ]
```

```
do [ + 2 3 ]
;; => 5
```

## 3. Functions

`func [params] [body]` makes a function. Calling it needs no parentheses or commas: the function takes as many following values as it has parameters.

```
double: func [x] [
    * x 2
]
double 21
;; => 42
```

```
area: func [w h] [ * w h ]
area 3 4
;; => 12
```

Calls nest by arity: `double double 5` is `double (double 5)`.

```
double: func [x] [ * x 2 ]
double double 5
;; => 20
```

## 4. Conditionals

`either condition [then] [else]` evaluates one of two blocks. Comparisons are prefix too. `none` and `0` are false; every other value is true (there are no `true`/`false` words; use `1` and `0`).

```
n: 7
either > n 5 [ 'big ] [ 'small ]
;; => big
```

```
either 0 [ 'yes ] [ 'no ]
;; => no
```

## 5. CASE

`case` takes ONE block of pairs: a **condition block** followed by an **action block**. Conditions are tried in order; the first true one runs its action and `case` returns the action's result. `[1]` is a catch-all condition. With no match, `case` returns `none`. (`case` is ordinary Glon from `case.glon`, loaded as a library.)

```
size: func [n] [
    case [
        [< n 10]  [ 'small ]
        [< n 100] [ 'medium ]
        [1]       [ 'large ]
    ]
]
size 50
;; => medium
```

## 6. Closures and lexical scope

A function remembers the context it was made in. A function returned from another function is a **closure**: it keeps its parameters alive.

```
make-adder: func [n] [
    func [x] [ + x n ]
]
add10: make-adder 10
add10 5
;; => 15
```

A set-word inside a function updates the nearest existing binding of that word; if there is none, it creates a local of the current call.

```
counter: func [] [
    n: 0
    func [] [ n: + n 1 ]
]
c: counter
c c c
;; => 3
```

**The lexical fence.** A plain block that refers to a function's locals depends on that one call. It may be used while the call is running, but it may not escape it. To take behaviour out of a call, make a closure.

Legal: the block `[ + x 1 ]` is evaluated inside the call that owns `x`.

```
f: func [x] [ do [ + x 1 ] ]
f 7
;; => 8
```

Illegal: `do [ [x] ]` returns the inner block `[x]` out of `f`, but that block still refers to `f`'s `x`. The runtime refuses, at the moment of escape, by raising a SIN! of type `escape`, id `return`. Its last field, shown here as `<site>`, is the block's internal lexical site id: an implementation number (the live page shows the real one).

```
f: func [x] [ do [ [x] ] ]
f 7
;; => ** uncaught #[SIN! escape return <site>]
```

The legal way to take `x` out is a closure (`does` makes a function with no parameters):

```
f: func [x] [ does [ x ] ]
g: f 7
g
;; => 7
```

## 7. SIN!, raise and judge

A **SIN!** is a first-class value describing a failure: a type, an id and an argument. A SIN! is inert data until it is raised: you can store it, pass it and inspect it freely.

```
s: create-sin 'demo 'oops 7
s
;; => #[SIN! demo oops 7]
```

`raise` starts propagation: it abandons the current work and unwinds to the nearest `judge`. `judge block` runs the block; if something inside raises, `judge` returns the SIN! as ordinary inert data.

```
s: create-sin 'demo 'oops 7
judge [
    raise s
]
;; => #[SIN! demo oops 7]
```

Inspect a SIN! with `sin?`, `sin-type`, `sin-id` and `sin-arg`:

```
e: judge [ raise create-sin 'demo 'oops 7 ]
sin-id e
;; => oops
```

After `judge` returns, execution simply continues:

```
x: 1
s: judge [
    raise create-sin 'demo 'oops 7
]
x: + x 1
x
;; => 2
```

With no `judge`, a raised SIN! stops the program:

```
raise create-sin 'demo 'oops 7
;; => ** uncaught #[SIN! demo oops 7]
```

Runtime checks raise SIN!s too. The lexical fence above can be judged like any other raise:

```
f: func [x] [ do [ [x] ] ]
e: judge [ f 7 ]
sin-id e
;; => return
```

## 8. Tasks

Glon tasks are cooperative: `spawn block` creates a task, `yield` hands control to the next task, and `run-tasks` runs them until all finish. These words come from the task library shipped with the demos (`demos/tuple-space.glon`), loaded for the examples below. Two tasks interleave their digits:

```
trace: 0
spawn [ trace: + * trace 10 1  yield  trace: + * trace 10 1 ]
spawn [ trace: + * trace 10 2  yield  trace: + * trace 10 2 ]
run-tasks
trace
;; => 1212
```

A task body obeys the lexical fence: a block that depends on the spawning call is rejected with `'escape 'transport` **before** the task is created.

```
go: func [x] [ spawn [ + x 1 ] ]
e: judge [ go 5 ]
sin-id e
;; => transport
```

## 9. Coming from Rebol or Red

Glon looks familiar, which is exactly why these differences matter:

| In Rebol / Red | In Glon today |
|---|---|
| `1 + 2 * 3` (infix) | prefix only: `+ 1 * 2 3`; comparisons too: `> n 0` |
| `( ... )` groups an expression | no parentheses; fixed arity decides grouping |
| `0` is true; `true` / `false` | `0` and `none` are false; no `true` / `false` words (use `1` / `0`) |
| `case [cond [action] ...]`, `case/all` | `case [ [cond] [action] ... ]`: conditions are blocks; no `/all` |
| `foreach`, `each`, `while`, `repeat`, `loop` | no loop words; use recursion |
| `switch` | none; use `case` |
| `type?` | none |
| `try`, `attempt`, `error!`, `catch` / `throw` | `SIN!`, `create-sin`, `raise`, `judge` |
| an unset word is a catchable error | an unset word halts the run (a machine fail-stop, not a SIN!) |
| `func` set-words are global (Rebol); `function` collects locals (Red) | a set-word updates the nearest existing binding, else makes a local |
| a bound block can travel anywhere | a block that uses a call's locals may not outlive that call; use a closure |
| `[1] = [1]` is true | `=` is identity for blocks and strings (`= [1] [1]` is `0`); `str-eq` compares strings |
| threads, ports | cooperative tasks: `spawn`, `yield`, `run-tasks` (library words) |
| `print` shows any value | `print` shows integers only |

## 10. Next

- The first substantial Glon program is the live traffic simulator: <https://gchiu.github.io/rebol-substrate-experiment/shop/traffic.html>. Its IDM, Newell, OVM/FVDM and NaSch car-following models are written in Glon and run through WebAssembly in the browser.
- The normative rules: [GLON-ALPHA-LAWS.md](GLON-ALPHA-LAWS.md).
