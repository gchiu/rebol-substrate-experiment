#!/usr/bin/env python3
import sys
import b7_verify

VALID = [
    "id: func [x] [ x ] id 1",
    "add: func [x y] [ + x y ] add 1 2",
    "choose: func [x] [ either < x 1 [ 0 ] [ 1 ] ] choose 2",
    "a: func [x] [ b x ] b: func [x] [ + x 1 ] a 1",
]
INVALID = [
    ("missing_top_level", "f: func [x] [ x ]"),
    ("invalid_control_flow", "f: func [x] [ either x [ 1 ] ] f 1"),
    ("invalid_function_reference", "f: func [x] [ y ] f 1"),
    ("argument_count", "f: func [x] [ x ] f"),
    ("unbalanced_delimiter", "f: func [x] [ x f 1"),
    ("unexpected_token", "f: func [x] [ x ] ] f 1"),
    ("empty_block", "f: func [x] [ ] f 1"),
]
fail = 0
for src in VALID:
    err = b7_verify.validate_text(src)
    if err:
        print("FAIL valid:", src, err); fail += 1
for cat, src in INVALID:
    err = b7_verify.validate_text(src)
    if not err or err["category"] != cat:
        print("FAIL invalid category:", cat, err); fail += 1
print(f"B7 validator selftest: {fail} failure(s)")
sys.exit(1 if fail else 0)
