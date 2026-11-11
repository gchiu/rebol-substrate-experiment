#!/usr/bin/env python3
"""Regenerate the browser fixtures with Python's independent zlib/gzip.

The compressed bytes are inputs only. The decoder under test is always the
committed Glon DEFLATE implementation in glon.wasm; zlib is the reference
producer and the expected-output oracle.
"""
import base64
import gzip
import hashlib
import json
import os
import zlib


def raw(data):
    c = zlib.compressobj(9, zlib.DEFLATED, -10)
    return c.compress(data) + c.flush()


def zlib_stream(data):
    c = zlib.compressobj(9, zlib.DEFLATED, 10)
    return c.compress(data) + c.flush()


def gzip_stream(data):
    c = zlib.compressobj(9, zlib.DEFLATED, 16 + 10)
    return c.compress(data) + c.flush()


samples = []


def add(sample_id, name, description, data, blob, fmt, chunks=None, text=True):
    open("samples/%s.bin" % sample_id, "wb").write(blob)
    samples.append({
        "id": sample_id, "name": name, "description": description,
        "format": fmt, "encoding": "base64",
        "data": base64.b64encode(blob).decode("ascii"),
        "chunks": chunks or [len(blob)],
        "bytes": len(blob), "output_bytes": len(data),
        "output_sha256": hashlib.sha256(data).hexdigest(),
        "output_text": data.decode("utf-8", "replace") if text else None,
        "expected_base64": base64.b64encode(data).decode("ascii"),
    })


hello = b"Hello, Glon!"
hr = raw(hello)
add("hello", "Hello, Glon!", "A short raw DEFLATE stream.", hello, hr, "raw", [len(hr)])

long_text = (b"Glon is an experimental compact execution language for AI-generated software. "
             b"This text was compressed independently with Python zlib and is decoded by the "
             b"unmodified Glon DEFLATE implementation on the original seven-primitive runtime.\n\n") * 22
lr = raw(long_text)
add("longer", "Longer text", "A larger zlib-wrapped text sample.",
    long_text, zlib_stream(long_text), "zlib", [len(lr)])

stream_text = (b"Streaming means the decoder accepts compressed bytes in fragments, keeps its "
               b"bit and Huffman state, and resumes without reconstructing it. "
               b"This sample is fed in deliberately awkward chunks: ") + bytes(range(32, 127)) + b".\n"
st = raw(stream_text)
pattern = [3, 11, 7, 29, 2, 41, 13, 5, 67, 17, 31, 23, 43, 53, 19, 89, 101, 37, 61, 71]
ch, left, i = [], len(st), 0
while left:
    n = min(pattern[i % len(pattern)], left)
    ch.append(n)
    left -= n
    i += 1
add("streaming", "Streaming example",
    "The same decoder fed in fragments through dz/feed; output is drained with dz/read.",
    stream_text, st, "raw", ch)

binary = bytes(range(256)) + b"\x00\xff\x00Glon\x00"
br = raw(binary)
add("binary", "Binary bytes",
    "Binary output including NUL and every byte value; shown as hex.",
    binary, br, "raw", [len(br)], False)

empty = b""
er = raw(empty)
add("empty", "Empty output",
    "A valid DEFLATE stream whose decompressed result is zero bytes.",
    empty, er, "raw", [len(er)])

gzip_data = (b"This sample uses the gzip container. The browser adapter only removes the "
             b"gzip header and trailer; the DEFLATE payload is decoded by Glon.\n")
gr = raw(gzip_data)
add("gzip", "gzip-wrapped text",
    "A single-member gzip container around raw DEFLATE.",
    gzip_data, gzip_stream(gzip_data), "gzip", [len(gr)])

with open("samples.json", "w") as f:
    json.dump({"samples": samples}, f, indent=2)
    f.write("\n")
