# F2 allocation trace — the failed 320-cell allocation

## Reproduction

From the committed F1 baseline (`08c5816`):

```
cd experiments/glon-deflate
GLON_F1_DIAG=1 build/dzhost -L ../.. -c 128 -r 100 \
  -o /tmp/f1.out build/fixtures/multi-memlevel1.deflate
```

The run fails after 4,047 of 9,000 bytes. The F1 diagnostic reports:

```
request_extent = 320
request_kind   = 6 (GC_KIND_STRING)
largest_free   = 304
free_cells     = 2,944 in 19 blocks
live_cells     = 4,288
scratchF       = 288
scratchA       = 36,556   ; string payload 36,544, length 287
scratchB       = 34,668   ; string payload 34,656, length 1
```

The diagnostic reads the failing `s/raw-cat` scratch cells and the two input
strings' bytes:

```
scratchA bytes: 10 10 10 10 10 10 10 10 10 14 ... 15 15 14 13 15 13 13 13
scratchB bytes: 14
```

Those are not compressed or decompressed output bytes. They are DEFLATE
**code lengths stored as length + 16** (the decoder's LENGTH BYTES convention).
The A string is a 287-byte dynamic-header code-length sequence; B is the next
one-byte code length (byte `0x14` = 20 = length 4 + 16).

## Exact chain

1. `dz/dynamic` reads the dynamic block's `HLIT`, `HDIST` and `HCLEN` and builds
   the code-length code. It then accumulates the literal/distance code lengths
   in `dz-ls` one symbol or run at a time.
2. In the F1 source this is `inflate.glon:335` (at `08c5816`):

   ```glon
   dz-ls: s/+ dz-ls mk-string reduce [ + 16 dz-sy ]
   ```

   The other run branches at lines 340, 343 and 345 are the same
   concatenation mechanism with a repeated byte string.
3. `s/+` is `s/raw-cat`, `demo/shop/strings.glon:30`. It computes the requested
   payload as `(lena + lenb + 16) / 16 * 16`. Here `lena + lenb = 287 + 1 = 288`,
   so it requests a 304-cell payload plus the 16-cell managed header:
   **320 cells**.
4. The allocator has 2,944 free cells but no contiguous block larger than 304.
   The request fails before `s/raw-cat` can write the length cell or copy the
   bytes.

The allocation is therefore **not the output buffer**. It is a temporary
Huffman-header code-length array. It has nothing to do with the bytes that the
decoder has already produced (the 4,047-byte prefix) or with the LZ77 copy.

## Data and lifetime

| Item | Value |
|---|---|
| Object kind | `GC_KIND_STRING` (6) |
| Bytes held | 287 code lengths, then one more length |
| Encoding | one byte per symbol, value = code length + 16 |
| Requested extent | 320 cells (304-byte payload + 16-cell header) |
| Current API | contiguous managed STRING!, capacity one cell per byte |
| Live from | start of `dz/dynamic` |
| Live until | `hf/build` has consumed the lengths; then the binding is dead |
| Necessary for decoding? | No. The lengths are needed, but a contiguous managed string is not. |
| Necessary for the API? | Yes for the old `s/+`/`s/length`/`s/at` representation only. |

The final code-length sequence in this header is 288 bytes (`dz-tot = 270 +
18 = 288`). The old path grows through 287 bytes and tries to make 288 with a
new 320-cell concatenation. A single fixed buffer of the final size avoids the
growth, but the old code could not allocate one at that point either because
the largest free extent was 304.

## What this rules out

- The failed allocation is not decompressed output, compressed input, the LZ77
  window, or a Huffman symbol table.
- It is not a coalescing defect: F1's heap dump shows no adjacent free blocks.
- It is not algorithmically necessary: `hf/build` needs a sequence of code
  lengths and their length, and can read them from a bounded buffer with an
  explicit length.

The F1 report's inference that the request came from the string library is
confirmed here; F2 supplies the exact dynamic-header caller, the input bytes,
and the data type.
