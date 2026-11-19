# Glon HTTPS Server — G3: Dynamic Web Applications

**Status:** implemented, locally committed, not pushed or deployed.  
**Branch:** `experiment/glon-https-g3`  
**Worktree:** `/mnt/d/repos/glon-https-g3`  
**Base:** G2 `f854b60` (implementation `c955523`)  
**Frozen S1:** `f90496c26dc45c7a387d8fc8639cd2781507d3d2` (unchanged)

---

## 1. Objective

G3 turns the G2 multi-page HTTPS example into a small interactive application:
a synthetic membership register with list, view, add and edit operations, HTML
forms and equivalent JSON requests. Glon owns routing, request interpretation,
validation, state, status selection, HTML/JSON generation and escaping. C and
CivetWeb remain responsible for networking, TLS, HTTP protocol handling,
bounded transport buffers and request lifecycle.

The demonstration is deliberately small. The 7,232-cell managed heap is the
constraining resource, so the application has an explicit three-record capacity
and an opt-in loader-heap build setting is used for the larger G3 source and
per-request dispatch expressions. No frozen S1 file, parser, evaluator, GC or
M1 scheduler code was changed.

---

## 2. Architecture and data flow

```text
client
  |
  | TLS + HTTP/1.1
  v
CivetWeb (listen, TLS, request line/headers, body read, limits, close)
  |
  v
glon_http.c
  |  read bounded body (--http-max-body, default 2048, hard 4096)
  |  collect bounded request headers
  |  opaque integer id
  v
[ http/dispatch <id> <method> <path> <query> <body-string> <headers-block> ]
  v
persistent Glon session
  |  routing, form/JSON decode, validation, register state, escaping
  v
[ http/take-response ] => [id status content-type headers body] | NONE
  |  body = STRING! or nested block of STRING!/BLOCK! parts
  v
glon_http.c  (validate id; flatten bounded body parts)
  v
CivetWeb -> client
```

The C host contains no membership routes, fields, validation, HTML or JSON.
The application files are ordinary Glon loaded after the shipped HTTP
vocabulary.

---

## 3. Implemented capabilities

- POST and PUT requests with bounded request bodies.
- Request body access (`http/body`).
- Request header access (`http/header`) and Content-Type inspection
  (`http/content-type`).
- URL-encoded form decoding (`application/x-www-form-urlencoded`).
- Flat JSON object decoding for string-valued fields (`application/json`).
- Explicit body cap: default 2,048 bytes, hard 4,096 bytes; the CLI option is
  `--http-max-body N`.
- 64 KiB response body transport cap; response bodies may be nested part
  blocks.
- Status mapping: 200, 201, 400, 404, 405, 409, 413, 415, 422, 500.
- `Allow` headers on 405 responses.
- HTML and JSON output escaping.
- Persistent in-session register state with a fixed three-record block.
- No partial state change on invalid input: parsing and validation complete
  before the single `block-set!` mutation.
- Stale request handles are rejected by every accessor and by
  `http/respond`/`http/respond-with`.

---

## 4. Vocabulary additions and justification

### Host-facing request data

| Addition | Contract | Why |
|---|---|---|
| `http/body request` | `STRING!` request body bytes, empty when absent | POST/form/JSON input is required |
| `http/header request name` | `STRING!` value or `NONE`; case-insensitive name match, first duplicate wins | Content-Type and general header inspection |
| `http/content-type request` | `http/header request "Content-Type"` | Common application check without a special host rule |
| request block | `[id method path query body headers]` | Extends the G2 opaque block; id remains an integer |
| host dispatch source | `[ http/dispatch id method path query body headers ]` | Passes CivetWeb-parsed data into the persistent session |

All accessors share the G1/G2 ownership check. A stale or foreign request raises
`SIN! 'http 'stale-request`; use outside a dispatch raises
`SIN! 'http 'no-request`. Request data is valid only for the current handler
turn; it is not a native pointer and cannot be used after dispatch.

### Response body parts

`http/respond`/`http/respond-with` accept a `STRING!` body or a nested block of
`STRING!`/`BLOCK!` parts. The host flattens the parts in order into one bounded
transport buffer (64 KiB). This is not a new word and does not change the
response status/header contract. It exists because the non-moving 7,232-cell
managed heap cannot reliably hold the repeatedly concatenated intermediate
strings a larger page would create. The application composes pages from small
parts; C only transports them.

### Build setting

`GLON_HTTP_RECLAIM_M1=1` builds the HTTP binary with
`-DR0S1_HEAP_LIMIT=65000`. The HTTP server never creates M1 tasks, so the
otherwise-unused M1 task arena is reclaimed as loader space. This is the
documented `R0S1_HEAP_LIMIT` override, not a change to frozen S1 and not a
change to the 7,232-cell managed heap. It is **opt-in**, used by the G3 target;
the default HTTP build keeps the 54,800 limit and remains valid for G1/G2.

Without this setting, the G3 application source plus the transient dispatch
source (especially request bodies/headers) exhausts the default 14,800-cell
loader heap. This was measured, not assumed. The setting should be reviewed
before becoming a default HTTP configuration.

### Application-level helpers

`examples/https-g3/webparse.glon` contains run-based form/JSON helpers, HTML and
JSON escaping, and string scanning. They are application code, not host
vocabulary:
- `wp/form-check-loop`, `wp/form-scan`, `wp/url-decode-loop`
- `wp/json-find`, `wp/json-pairs`, `wp/json-string-loop`, `wp/json-ws`
- `wp/escape-html`, `wp/escape-json`
- small string helpers (`wp/find-byte`, `wp/match-at?`, `wp/starts-with?`,
  `wp/ci-prefix?`)

The JSON decoder is deliberately restricted to a flat object of string values.
It accepts `\"` and `\\` escapes and rejects nested objects/arrays, numbers,
booleans, null and `\u` escapes. The membership JSON uses string fields only.

---

## 5. Request and application state lifetimes

- CivetWeb owns the socket/TLS/connection and the parsed request line/headers.
- The host reads at most `--http-max-body` bytes into a bounded stack buffer,
  then copies the bytes into an ordinary Glon managed string through the
  dispatch source.
- The request block, body string and header block are rooted in
  `http/current` only for the handler turn. `http/dispatch` sets
  `http/current` to `none` after the handler returns.
- The application register is a global block of three record slots. Each slot
  is a record STRING! or `none`. Add/update builds a new record string and
  assigns it with `block-set!` only after validation and duplicate/capacity
  checks pass.
- `http/response` holds the response `[id status type headers body]` until the
  host calls `http/take-response`; then it is cleared. The host copies the body
  into its bounded transport buffer before the next turn.
- The collector reclaims transient request/response/form/JSON/HTML-part
  objects. The live set after collection is the loaded libraries, application
  closures/globals and the three register slots.

The register capacity is three records (one seeded member plus two additions).
This is an explicit application limit, not a host limit. It is enforced by the
fixed three-slot block; a further add returns 422. The small capacity is a
consequence of the 7,232-cell managed heap and the current non-moving GC.

---

## 6. Error semantics

The application uses the existing `SIN!`/`raise`/`judge` system. Parsing and
validation helpers raise:

```glon
create-sin 'app <http-status> "<message>"
```

Route handlers run the operation inside `judge`; if the result is a SIN!,
`respond/error` uses its integer id as the HTTP status and its string argument
as the body. If the id is not an integer, the response is 500. JSON requests
receive a JSON error object; form/HTML requests receive `text/plain`.

| Situation | Status |
|---|---|
| Valid list/view/add/edit | 200 (add JSON: 201) |
| Empty body | 400 |
| Malformed form percent encoding | 400 |
| Malformed/unsupported JSON shape | 400 |
| Missing/invalid field | 422 |
| Duplicate identifier | 409 |
| Missing record | 404 |
| Unsupported method on a known path | 405 + `Allow` |
| Unsupported content type | 415 |
| Body over `--http-max-body` | 413 (host, before dispatch) |
| Stale request handle/accessor | SIN!, becomes 500 if uncaught |
| Handler SIN! / machine fail-stop | 500; machine fail-stop stops the listener |

Invalid input never reaches the `block-set!` assignment, so rejected requests
do not partially add or update a record.

---

## 7. Complete executable application

Launch both files in this order (webparse first). Both are required.

### `examples/https-g3/webparse.glon`

```glon
;; examples/https-g3/webparse.glon -- small application-level request decoders.
;;
;; Ordinary Glon over the shipped string vocabulary.  This is deliberately not
;; a general web framework: it decodes the flat, string-valued form/JSON shapes
;; the membership application accepts, and escapes values for HTML/JSON output.
;; Malformed input raises SIN! 'app <status> "<message>" (the HTTP status is
;; chosen here only because this file is part of the application).
[
wp/quote: mk-string reduce [ 34 ]
wp/backslash: mk-string reduce [ 92 ]

wp/hex-val: func [b] [
    either = b none [ -1 ] [
        either either >= b 48 [ <= b 57 ] [ 0 ] [
            - b 48
        ] [
            either either >= b 65 [ <= b 70 ] [ 0 ] [
                + - b 65 10
            ] [
                either either >= b 97 [ <= b 102 ] [ 0 ] [
                    + - b 97 10
                ] [
                    -1
                ]
            ]
        ]
    ]
]

wp/find-byte: func [s from ch] [
    either >= from s/length s [ -1 ] [
        either = s/at s from ch [
            from
        ] [
            wp/find-byte s + from 1 ch
        ]
    ]
]
wp/match-at?: func [s i sub] [
    end: + i s/length sub
    either > end s/length s [ 0 ] [
        x: s/span s i end
        str-eq x sub
    ]
]

wp/starts-with?: func [s prefix] [
    either >= s/length s s/length prefix [
        x: s/span s 0 s/length prefix
        str-eq x prefix
    ] [ 0 ]
]
wp/ci-prefix?: func [s prefix] [
    either s [
        either >= s/length s s/length prefix [
            wp/ci-loop s prefix 0
        ] [
            0
        ]
    ] [
        0
    ]
]
wp/ci-loop: func [s prefix i] [
    either >= i s/length prefix [
        1
    ] [
        a: s/at s i
        b: s/at prefix i
        la: either either >= a 65 [ <= a 90 ] [ 0 ] [ + a 32 ] [ a ]
        lb: either either >= b 65 [ <= b 90 ] [ 0 ] [ + b 32 ] [ b ]
        either = la lb [ wp/ci-loop s prefix + i 1 ] [ 0 ]
    ]
]

;; application/x-www-form-urlencoded percent decoding.  '+' becomes space.
wp/url-decode-loop: func [s i out] [
    either >= i s/length s [
        out
    ] [
        b: s/at s i
        either = b 37 [
            h: wp/hex-val s/at s + i 1
            l: wp/hex-val s/at s + i 2
            either either < h 0 [ 1 ] [ < l 0 ] [
                raise create-sin 'app 400 "malformed form encoding"
            ] [
                wp/url-decode-loop s + i 3
                    s/+ out mk-string reduce [ + * h 16 l ]
            ]
        ] [
            wp/url-decode-loop s + i 1
                s/+ out mk-string reduce [ either = b 43 [ 32 ] [ b ] ]
        ]
    ]
]

;; Reject any malformed percent escape anywhere in a form body, including in
;; fields the application does not use.
wp/form-check-loop: func [body i] [
    either >= i s/length body [
        1
    ] [
        either = s/at body i 37 [
            h: wp/hex-val s/at body + i 1
            l: wp/hex-val s/at body + i 2
            either either < h 0 [ 1 ] [ < l 0 ] [
                raise create-sin 'app 400 "malformed form encoding"
            ] [
                wp/form-check-loop body + i 3
            ]
        ] [
            wp/form-check-loop body + i 1
        ]
    ]
]

;; First value for `key`, or NONE.  Keys occur at the start or after '&'.
wp/form-scan: func [body key pos] [
    either >= pos s/length body [
        none
    ] [
        either either wp/match-at? body pos key [
            = s/at body + pos s/length key 61
        ] [ 0 ] [
            start: + + pos s/length key 1
            end: wp/find-byte body start 38
            either < end 0 [ end: s/length body ] [ none ]
            wp/url-decode-loop s/span body start end 0 ""
        ] [
            amp: wp/find-byte body pos 38
            either < amp 0 [ none ] [ wp/form-scan body key + amp 1 ]
        ]
    ]
]

;; Flat JSON object of string values: {"key":"value", ...}.  Keys are decoded
;; strings; duplicate target keys are rejected.  Nested objects/arrays, numbers,
;; booleans, null and \\u escapes are unsupported and rejected.
wp/json-ws: func [s i] [
    either >= i s/length s [
        i
    ] [
        either either = s/at s i 32 [ 1 ] [
            either = s/at s i 9 [ 1 ] [
                either = s/at s i 10 [ 1 ] [ = s/at s i 13 ]
            ]
        ] [
            wp/json-ws s + i 1
        ] [
            i
        ]
    ]
]
wp/json-string-loop: func [s i out] [
    b: s/at s i
    either = b none [
        raise create-sin 'app 400 "unterminated JSON string"
    ] [
        either = b 34 [
            reduce [ out + i 1 ]
        ] [
            either = b 92 [
                e: s/at s + i 1
                either = e 34 [
                    wp/json-string-loop s + i 2 s/+ out wp/quote
                ] [
                    either = e 92 [
                        wp/json-string-loop s + i 2 s/+ out wp/backslash
                    ] [
                        raise create-sin 'app 400 "unsupported JSON escape"
                    ]
                ]
            ] [
                either < b 32 [
                    raise create-sin 'app 400 "control byte in JSON string"
                ] [
                    wp/json-string-loop s + i 1 s/+ out mk-string reduce [ b ]
                ]
            ]
        ]
    ]
]
wp/json-find: func [s target] [
    i: wp/json-ws s 0
    either = s/at s i 123 [
        i: wp/json-ws s + i 1
        either = s/at s i 125 [
            j: wp/json-ws s + i 1
            either = j s/length s [ none ] [
                raise create-sin 'app 400 "trailing JSON data"
            ]
        ] [
            wp/json-pairs s target i none
        ]
    ] [
        raise create-sin 'app 400 "JSON body must be an object"
    ]
]
wp/json-pairs: func [s target i found] [
    either = s/at s i 34 [ none ] [ raise create-sin 'app 400 "expected JSON string" ]
    p: wp/json-string-loop s + i 1 ""
    key: block-at p 0
    i: wp/json-ws s block-at p 1
    either = s/at s i 58 [
        i: wp/json-ws s + i 1
    ] [
        raise create-sin 'app 400 "expected JSON colon"
    ]
    either = s/at s i 34 [ none ] [ raise create-sin 'app 400 "expected JSON string" ]
    v: wp/json-string-loop s + i 1 ""
    val: block-at v 0
    i: wp/json-ws s block-at v 1
    found2: either str-eq key target [
        either found [ raise create-sin 'app 400 "duplicate JSON field" ] [ val ]
    ] [ found ]
    b: s/at s i
    either = b 44 [
        wp/json-pairs s target wp/json-ws s + i 1 found2
    ] [
        either = b 125 [
            j: wp/json-ws s + i 1
            either = j s/length s [ found2 ] [
                raise create-sin 'app 400 "trailing JSON data"
            ]
        ] [
            raise create-sin 'app 400 "expected JSON delimiter"
        ]
    ]
]

wp/html-special?: func [b] [
    either = b 38 [ 1 ] [
        either = b 60 [ 1 ] [
            either = b 62 [ 1 ] [
                either = b 34 [ 1 ] [ either = b 39 [ 1 ] [ 0 ] ]
            ]
        ]
    ]
]
wp/find-html-special: func [s i] [
    either >= i s/length s [ i ] [
        either wp/html-special? s/at s i [ i ] [
            wp/find-html-special s + i 1
        ]
    ]
]
wp/escape-html: func [s] [ wp/escape-html-loop s 0 "" ]
wp/escape-html-loop: func [s i out] [
    j: wp/find-html-special s i
    out2: either > j i [ s/+ out s/span s i j ] [ out ]
    b: s/at s j
    either = b none [ out2 ] [
        piece: either = b 38 [ "&amp;" ] [
            either = b 60 [ "&lt;" ] [
                either = b 62 [ "&gt;" ] [
                    either = b 34 [ "&quot;" ] [ "&#39;" ]
                ]
            ]
        ]
        wp/escape-html-loop s + j 1 s/+ out2 piece
    ]
]

wp/json-special?: func [b] [
    either = b 34 [ 1 ] [ either = b 92 [ 1 ] [ 0 ] ]
]
wp/find-json-special: func [s i] [
    either >= i s/length s [ i ] [
        either wp/json-special? s/at s i [ i ] [
            wp/find-json-special s + i 1
        ]
    ]
]
wp/escape-json: func [s] [ wp/escape-json-loop s 0 "" ]
wp/escape-json-loop: func [s i out] [
    j: wp/find-json-special s i
    out2: either > j i [ s/+ out s/span s i j ] [ out ]
    b: s/at s j
    either = b none [ out2 ] [
        piece: either = b 34 [ s/+ wp/backslash wp/quote ] [ s/+ wp/backslash wp/backslash ]
        wp/escape-json-loop s + j 1 s/+ out2 piece
    ]
]

]
```

### `examples/https-g3/server.glon`

```glon
;; examples/https-g3/server.glon -- synthetic membership register over HTTPS.
;;
;; Requires examples/https-g3/webparse.glon to be loaded first (the Makefile
;; and acceptance commands pass both files in order).  All routes, validation,
;; state, HTML and JSON are Glon.  Synthetic data only; no authentication.
[
block-set!: masm 3 [
    LIT SCRATCH_C ! LIT SCRATCH_B ! DUP LIT 16 MOD SUB LIT SCRATCH_A !
    LIT SCRATCH_C @ LIT SCRATCH_A @ LIT 2 ADD LIT SCRATCH_B @ LIT 16 DIV ADD !
    ARITY 0 EXIT
]
reg: reduce [ "m1|Ada Lovelace|ada@example.test|active;" none none ]

reg/check: func [value pred label] [
    either = value none [
        raise create-sin 'app 422 s/+ label " is required"
    ] [
        either pred value [ 1 ] [ raise create-sin 'app 422 s/+ label " is invalid" ]
    ]
]
reg/text-ok?: func [s] [
    either > s/length s 128 [ 0 ] [
    either > s/length s 0 [
        either < wp/find-byte s 0 124 0 [
            either < wp/find-byte s 0 59 0 [ 1 ] [ 0 ]
        ] [ 0 ]
    ] [ 0 ]
    ]
]
reg/id-ok?: func [s] [ reg/text-ok? s ]
reg/name-ok?: func [s] [ reg/text-ok? s ]
reg/email-ok?: func [s] [
    either reg/text-ok? s [
        at: wp/find-byte s 0 64
        either < at 1 [ 0 ] [
            either >= at - s/length s 1 [ 0 ] [
                idx: wp/find-byte s + at 1 64
                either < idx 0 [ 1 ] [ 0 ]
            ]
        ]
    ] [ 0 ]
]
reg/status-ok?: func [s] [
    either str-eq s "active" [ 1 ] [ either str-eq s "inactive" [ 1 ] [ 0 ] ]
]

reg/field-scan: func [rec idx i start] [
    end: wp/find-byte rec start 124
    either < end 0 [
        either = i idx [ s/span rec start s/length rec ] [ "" ]
    ] [
        either = i idx [
            s/span rec start end
        ] [
            reg/field-scan rec idx + i 1 + end 1
        ]
    ]
]
reg/field: func [rec idx] [ reg/field-scan rec idx 0 0 ]

reg/make: func [id name email status] [
    r: s/+ id "|"
    r: s/+ r name
    r: s/+ r "|"
    r: s/+ r email
    r: s/+ r "|"
    r: s/+ r status
    s/+ r ";"
]

reg/find-loop: func [id i] [
    either >= i block-len reg [ none ] [
        rec: block-at reg i
        either rec [
            rid: reg/field rec 0
            either str-eq rid id [ rec ] [ reg/find-loop id + i 1 ]
        ] [ reg/find-loop id + i 1 ]
    ]
]
reg/find: func [id] [ reg/find-loop id 0 ]
reg/index: func [id i] [
    either >= i block-len reg [ -1 ] [
        rec: block-at reg i
        either rec [
            rid: reg/field rec 0
            either str-eq rid id [ i ] [ reg/index id + i 1 ]
        ] [ reg/index id + i 1 ]
    ]
]
reg/free: func [i] [
    either >= i block-len reg [ -1 ] [
        either block-at reg i [ reg/free + i 1 ] [ i ]
    ]
]

reg/from-fields: func [fields] [
    id: block-at fields 0
    name: block-at fields 1
    email: block-at fields 2
    status: block-at fields 3
    reg/check id :reg/id-ok? "id"
    reg/check name :reg/name-ok? "name"
    reg/check email :reg/email-ok? "email"
    reg/check status :reg/status-ok? "status"
    reg/make id name email status
]

reg/add: func [fields] [
    rec: reg/from-fields fields
    id: reg/field rec 0
    either reg/find id [
        raise create-sin 'app 409 "duplicate id"
    ] [ none ]
    idx: reg/free 0
    either < idx 0 [
        raise create-sin 'app 422 "register capacity reached"
    ] [
        block-set! reg idx rec
        rec
    ]
]

reg/update: func [id fields] [
    old: reg/find id
    either old [
        rec: reg/from-fields fields
        rid: reg/field rec 0
        either str-eq rid id [
            idx: reg/index id 0
            block-set! reg idx rec
            rec
        ] [
            raise create-sin 'app 422 "id in body does not match path"
        ]
    ] [
        raise create-sin 'app 404 "member not found"
    ]
]

req/fields: func [request json] [
    ct: http/content-type request
    body: http/body request
    either > s/length body 0 [
        either json [
            either wp/ci-prefix? ct "application/json" [
                idv: wp/json-find body "id"
                namev: wp/json-find body "name"
                emailv: wp/json-find body "email"
                statusv: wp/json-find body "status"
                reduce [ idv namev emailv statusv ]
            ] [
                raise create-sin 'app 415 "unsupported content type"
            ]
        ] [
            either wp/ci-prefix? ct "application/x-www-form-urlencoded" [
                wp/form-check-loop body 0
                idv: wp/form-scan body "id" 0
                namev: wp/form-scan body "name" 0
                emailv: wp/form-scan body "email" 0
                statusv: wp/form-scan body "status" 0
                reduce [ idv namev emailv statusv ]
            ] [
                raise create-sin 'app 415 "unsupported content type"
            ]
        ]
    ] [
        raise create-sin 'app 400 "empty request body"
    ]
]

html/page: func [title body] [
    reduce [
        "<!doctype html><html><head><meta charset='utf-8'><title>" title
        "</title><style>body{font-family:sans-serif}</style></head><body>" body
        "</body></html>"
    ]
]
html/row: func [rec] [
    id: reg/field rec 0
    eid: wp/escape-html id
    en: wp/escape-html reg/field rec 1
    reduce [
        "<tr><td><a href='/members/" eid "'>" eid "</a></td><td>" en "</td></tr>"
    ]
]
html/list-loop: func [i] [
    either >= i block-len reg [ reduce [ ] ] [
        rec: block-at reg i
        rest: html/list-loop + i 1
        either rec [
            row: html/row rec
            reduce [ row rest ]
        ] [ rest ]
    ]
]
html/list-body: func [] [
    rows: html/list-loop 0
    html/page "Members" reduce [
        "<h1>Members</h1><p><a href='/add'>Add member</a></p><table><tr><th>ID</th><th>Name</th></tr>"
        rows
    ]
]
html/add-page: func [] [
    html/page "Add member" html/form "/" "" "" "" "active" "Add" ""
]
html/detail: func [rec] [
    id: reg/field rec 0
    name: reg/field rec 1
    email: reg/field rec 2
    status: reg/field rec 3
    action: s/+ "/members/" id
    f: html/form action id name email status "Save" " readonly"
    en: wp/escape-html name
    html/page "Member" reduce [ "<h1>" en "</h1>" f ]
]
html/form: func [action id name email status submit readonly] [
    active-sel: either str-eq status "active" [ " selected" ] [ "" ]
    inactive-sel: either str-eq status "inactive" [ " selected" ] [ "" ]
    eid: wp/escape-html id
    en: wp/escape-html name
    ee: wp/escape-html email
    reduce [
        "<form method=post action='" action "'><p>ID <input name=id value='"
        eid "'" readonly "><p>Name <input name=name value='"
        en "'><p>Email <input name=email value='"
        ee "'><p>Status <select name=status><option value=active"
        active-sel ">active</option><option value=inactive" inactive-sel
        ">inactive</option></select><p><button type=submit>" submit "</button></form>"
    ]
]

json/ok: func [rec] [
    id: reg/field rec 0
    name: reg/field rec 1
    eid: wp/escape-json id
    en: wp/escape-json name
    reduce [
        "{" wp/quote "id" wp/quote ":" wp/quote eid wp/quote
        "," wp/quote "name" wp/quote ":" wp/quote en wp/quote "}"
    ]
]

respond/text: func [request status body] [
    http/respond request status "text/plain; charset=utf-8" body
]
respond/html: func [request status body] [
    http/respond request status "text/html; charset=utf-8" body
]
respond/json: func [request status body] [
    http/respond request status "application/json" body
]
respond/405: func [request allow] [
    http/respond-with request 405 "text/plain; charset=utf-8"
        reduce [ "Allow" allow ] "405 Method Not Allowed"
]
respond/error: func [request sin] [
    status: sin-id sin
    either s/raw-int? status [
        ct: http/content-type request
        msg: sin-arg sin
        either wp/ci-prefix? ct "application/json" [
            esc: wp/escape-json-loop msg 0 ""
            respond/json request status reduce [
                "{" wp/quote "error" wp/quote ":" wp/quote esc wp/quote "}"
            ]
        ] [
            respond/text request status msg
        ]
    ] [
        respond/text request 500 "internal application error"
    ]
]

do/html-post: func [request id] [
    result: judge [
        fields: req/fields request 0
        either id [ reg/update id fields ] [ reg/add fields ]
    ]
    either sin? result [ respond/error request result ] [ respond/html request 200 html/detail result ]
]
do/json-post: func [request id] [
    result: judge [
        fields: req/fields request 1
        either id [ reg/update id fields ] [ reg/add fields ]
    ]
    either sin? result [
        respond/error request result
    ] [
        respond/json request either id [ 200 ] [ 201 ] json/ok result
    ]
]

route/html-member: func [request method id] [
    either str-eq method "GET" [
        rec: reg/find id
        either rec [ respond/html request 200 html/detail rec ] [ respond/text request 404 "member not found" ]
    ] [
        either either str-eq method "POST" [ 1 ] [ str-eq method "PUT" ] [
            do/html-post request id
        ] [ respond/405 request "GET, POST, PUT" ]
    ]
]
route/api-member: func [request method id] [
    either either str-eq method "POST" [ 1 ] [ str-eq method "PUT" ] [
        do/json-post request id
    ] [ respond/405 request "POST, PUT" ]
]

handle: func [request] [
    method: http/method request
    path: http/path request
    either str-eq path "/" [
        either str-eq method "GET" [
            respond/html request 200 html/list-body
        ] [
            either str-eq method "POST" [ do/html-post request none ] [ respond/405 request "GET, POST" ]
        ]
    ] [
        either str-eq path "/add" [
            either str-eq method "GET" [ respond/html request 200 html/add-page ] [ respond/405 request "GET" ]
        ] [
        either str-eq path "/api/members" [
            either str-eq method "POST" [ do/json-post request none ] [ respond/405 request "POST" ]
        ] [
            either wp/starts-with? path "/api/members/" [
                route/api-member request method
                    s/span path 13 s/length path
            ] [
                either wp/starts-with? path "/members/" [
                    route/html-member request method
                        s/span path 9 s/length path
                ] [
                    respond/text request 404 "not found"
                ]
            ]
        ]
        ]
    ]
]

http/listen 8443 :handle
]
```

---

## 8. Build, launch and test commands

### Dependencies

```sh
sudo apt-get install build-essential libssl-dev
```

Optional local OpenSSL development tree:

```sh
make glon MODULES=http GLON_HTTP_RECLAIM_M1=1 \
  OPENSSL_CFLAGS="-I/path/usr/include -I/path/usr/include/x86_64-linux-gnu" \
  OPENSSL_LIBS="-L/path/usr/lib/x86_64-linux-gnu -lssl -lcrypto"
```

### Build and launch

```sh
# G1/G2-compatible default HTTP build (no G3 loader extension)
make glon MODULES=http

# Required G3 build: opt into the unused M1 arena as loader space
make glon MODULES=http GLON_HTTP_RECLAIM_M1=1

tests/https-g1/make-cert.sh
./glon --http-cert tests/https-g1/.cert/server.pem \
       examples/https-g3/webparse.glon examples/https-g3/server.glon
```

Optional bounded controls:

```sh
./glon --http-cert tests/https-g1/.cert/server.pem \
       --http-max-body 2048 --http-max-requests 1000 --http-report \
       examples/https-g3/webparse.glon examples/https-g3/server.glon
```

### Browser and curl

```sh
curl -k https://127.0.0.1:8443/
curl -k https://127.0.0.1:8443/members/m1
curl -k https://127.0.0.1:8443/add
curl -k -i -X POST https://127.0.0.1:8443/ \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data 'id=m3&name=Alice&email=alice@example.test&status=active'
curl -k -i -X POST https://127.0.0.1:8443/api/members \
  -H 'Content-Type: application/json' \
  --data '{"id":"m4","name":"Bob","email":"bob@example.test","status":"active"}'
```

Browser URLs:

```text
https://127.0.0.1:8443/
https://127.0.0.1:8443/members/m1
https://127.0.0.1:8443/add
```

### Tests

```sh
make https-g3-test
python3 tests/https-g3/acceptance.py --glon ./glon
python3 tests/https-g3/workloads.py --glon ./glon
python3 tests/https-g3/workloads.py --glon ./glon --extended
```

`https-g3-test` retains G1 and G2, builds the minimal/file/HTTP/combined
configurations, runs G1/G2 under the default and `file,http` builds, and runs
G3 under the opt-in HTTP build. G3 is not run under `file,http` (see §12).

---

## 9. Acceptance results

Configuration: Linux/WSL2 x86_64, loopback, self-signed certificate, bounded
requests, `--http-max-body 2048`, `GLON_HTTP_RECLAIM_M1=1` for G3.

| Requirement | Result |
|---|---|
| List members | PASS |
| View an individual member | PASS |
| Add through an HTML form | PASS |
| Edit through an HTML form | PASS |
| Equivalent JSON add/edit | PASS (201/200) |
| Required-field validation | PASS (422) |
| Duplicate identifier rejection | PASS (409) |
| Missing record | PASS (404) |
| Unsupported method + `Allow` | PASS (405) |
| Unsupported content type | PASS (415) |
| Empty/malformed form/JSON | PASS (400) |
| Oversized request body | PASS (413) |
| Stale request handles/accessors | PASS |
| Client disconnect | PASS; server continues |
| Handler SIN! recovery | PASS (retained G1 test) |
| Continued operation after rejects | PASS |
| HTML escaping of `<script>...` | PASS (`&lt;script&gt;...`) |
| JSON escaping of quote/backslash | PASS; round-trips through `json.loads` |
| No partial state on invalid input | PASS |
| 300 bounded mixed requests | PASS |
| Source edit without recompilation | PASS |
| G1/G2 retained | PASS |
| Minimal build | PASS |
| `MODULES=file,http` build | PASS; G1/G2 suites |
| Frozen S1 | PASS |

Final lines:

```text
HTTPS_G1_ACCEPTANCE PASS
HTTPS_G2_ACCEPTANCE PASS
HTTPS_G3_ACCEPTANCE PASS
```

---

## 10. Measurements

### Executable size

Environment: GCC 11.4, default `-std=c17 -Wall -Wextra -O0 -g`, CivetWeb `-O2`,
OpenSSL 3.0.2 dynamic. Sizes are bytes.

| Build | Unstripped | Stripped |
|---|---:|---:|
| minimal | 185,224 | 104,768 |
| `MODULES=file` | 250,880 | 137,568 |
| `MODULES=http` (default or G3) | 379,840 | 264,512 |
| `MODULES=file,http` + G3 setting | 443,784 | 297,312 |

HTTP adds 159,744 bytes stripped over the minimal CLI (156 KiB). The G3
application source is loaded at run time and is not part of the executable.

### Managed heap and request workloads

Managed heap capacity: **7,232 cells** (unchanged 64K map). Measurements below
are post-collection live cells from the shutdown report.

| Workload | Requests | Peak RSS | Live cells | Live objects | GC | Median latency |
|---|---:|---:|---:|---:|---:|---:|
| A fixed-state, 300 | 314 | 7,988 KiB | 4,288 | 130 | 317 | 110.50 ms |
| A fixed-state, 1,000 | 1,014 | 7,988 KiB | 3,280 | 100 | 1,017 | 126.68 ms |
| A fixed-state, 3,000 | 3,014 | 7,988 KiB | 3,280 | 100 | 3,017 | 113.71 ms |
| B baseline (seed only), 300 | 312 | 7,984 KiB | 3,680 | 113 | 184 | 88.33 ms |
| B grown (+2 records), 300 | 314 | 7,964 KiB | 4,288 | 130 | 317 | 147.79 ms |
| B grown (+2 records), 1,000 | 1,014 | 7,984 KiB | 3,280 | 100 | 1,017 | 130.27 ms |

Interpretation:

- Fixed-state A stabilises: 300 -> 1,000 -> 3,000 all end at 3,280 live cells
  after collection. Transient state is reclaimed; memory is bounded.
- B baseline vs grown at 300 shows a higher live set for the two retained
  member records (4,288 vs 3,680 cells). The 1,000-run converges to the same
  3,280 live set, so this is retained application data plus GC timing, not an
  unbounded leak.
- GC runs roughly once per request (3,017 collections for 3,014 requests in the
  A-3,000 run) because the heap is deliberately tight. No performance
  optimisation was attempted.
- Median latency is the local end-to-end Python client time with a **fresh TLS
  connection per request** (keep-alive is off). It includes TLS handshake,
  Python, HTTP and Glon work; it is not a Glon-only handler measurement.
- Peak RSS is ~7.96–7.99 MiB in all server workloads. The minimal CLI measured
  ~1.8 MiB in G1.

### Request and response limits

| Limit | Value |
|---|---|
| Request body default / hard | 2,048 / 4,096 bytes |
| Request URI / query / method | 2,048 / 2,048 / 32 bytes |
| Request headers passed | 16 (name 64, value 256 bytes) |
| Response body transport | 64 KiB |
| Application register | 3 records (fixed 3-slot block) |
| Keep-alive | off; one response per connection |

---

## 11. Regression

`make https-g3-test` verifies:

- minimal `make glon MODULES=` and `minimal-ok` smoke;
- `MODULES=file`;
- `MODULES=http` default with G1/G2 acceptance;
- `MODULES=http GLON_HTTP_RECLAIM_M1=1` with G3 acceptance;
- `MODULES=file,http GLON_HTTP_RECLAIM_M1=1` with G1/G2 acceptance;
- final `MODULES=http GLON_HTTP_RECLAIM_M1=1` build;
- `./check-frozen-s1.sh`.

G1 and G2 tests remain unchanged and pass.

---

## 12. Known limitations

- **Three-record application capacity.** The fixed block and 7,232-cell heap
  cannot hold a larger register together with the loaded application and
  transient response parts. Further additions return 422. A larger register
  needs a separate approved memory/heap design, not a silent capacity increase.
- **Managed-heap pressure.** The application runs with very frequent GC
  (roughly one collection per request). The non-moving heap is sensitive to
  fragmentation; the block-based register and part-based responses were chosen
  to keep it bounded.
- **Loader-heap extension is opt-in.** G3 requires
  `GLON_HTTP_RECLAIM_M1=1` (loader limit 65,000) because the source plus
  transient dispatch expressions exceed the default loader heap. The default
  HTTP build remains 54,800 and is valid for G1/G2. This capacity decision
  should be reviewed before becoming a general default.
- **G3 is HTTP-only in the combined build.** `MODULES=file,http` builds and
  passes G1/G2, but the file libraries' persistent live set leaves too little
  managed heap for the G3 app. No file-backed persistence is integrated.
- **No file persistence across restarts.** The existing file module is
  event/request based and its queue is not drained inside HTTP handlers, so
  straightforward per-request persistence is not available in G3. Startup-only
  loading would be possible, but mutation persistence would need an explicit
  integration design.
- **Limited JSON support.** Flat string-valued objects only; no numbers,
  booleans, null, arrays, nested objects or `\u` escapes. Only `\"` and `\\`
  escapes are decoded.
- **Minimal validation.** Required/non-empty, length <=128, no `|`/`;`, an
  `@` in email, and `active`/`inactive` status. This is not RFC email
  validation; synthetic data only.
- **No authentication/authorisation, CSRF protection, sessions, rate limiting,
  static files, WebSockets, templates, automatic certificates, HTTP/2/3 or
  public deployment.**
- **One worker.** Handlers are serialised through one Glon evaluator. Network
  concurrency is accepted, but Glon task concurrency is not used.
- **`http/query` is raw** (not URL-decoded); no query-parameter vocabulary.
- **No request-body streaming.** The body is materialised as one managed
  string, so the body cap is also bounded by the managed heap.

---

## 13. Rejected alternatives

| Alternative | Why rejected |
|---|---|
| Routing DSL / framework / middleware | Not needed for four routes; would add vocabulary and hidden state |
| Template engine | HTML is small and application-specific; ordinary string/part composition is clearer |
| General JSON parser/library | No existing JSON vocabulary; a restricted flat decoder is enough for G3 |
| JSON parsing in C | Would move application semantics into the host |
| Per-byte HTML escaping strings | Fragmented the non-moving heap; run-based escaping and part responses are smaller |
| Replacing the whole register string on update | Repeated string replacement caused fragmentation; fixed block slots with `block-set!` are stable |
| Increasing the managed heap | Not approved; the 7,232-cell map is unchanged |
| Running G3 under `file,http` | File libraries leave insufficient managed heap; G3 stays HTTP-only |
| Queuing file writes from the HTTP handler | The file module's request pump is not integrated with the HTTP worker |

---

## 14. Outstanding questions and recommended next experiment

1. **Register capacity and persistence.** A larger register needs either an
   approved managed-heap/compaction design or a storage module with a clear
   request/continuation model. The next experiment should be persistence, not a
   larger in-heap register.
2. **File-backed persistence.** Determine whether a synchronous or
   continuation-based file capability can be integrated with the HTTP worker
   without moving application state into C. This is the natural G4 candidate.
3. **Loader-heap policy.** Decide whether `GLON_HTTP_RECLAIM_M1=1` should be a
   documented HTTP-module default for task-free servers, or remain an explicit
   per-app setting.
4. **Request body streaming.** If larger forms/uploads are needed, decide
   between a bounded streaming API and a larger managed body representation.
5. **GC fragmentation.** The observed sensitivity to small-object
   fragmentation is a runtime/substrate question, not an application
   workaround; investigate only as a separate, approved experiment.

---

## 15. Files changed in G3

| File | Change |
|---|---|
| `glon_http.c`, `glon_http.h` | bounded POST body, request headers, `--http-max-body`, body-part flattening |
| `modules/http.glon` | `http/body`, `http/header`, `http/content-type`; request block extended; response body parts contract |
| `glon.c` | `--http-max-body` option |
| `Makefile` | `GLON_HTTP_RECLAIM_M1`, `https-g3-test` target, config stamp includes the setting |
| `examples/https-g3/webparse.glon` | form/JSON decoding and escaping helpers |
| `examples/https-g3/server.glon` | membership register, routing, HTML/JSON, validation, errors |
| `tests/https-g3/acceptance.py` | G3 acceptance suite |
| `tests/https-g3/inspect.glon` | body/header/content-type/stale-handle test app |
| `tests/https-g3/workloads.py` | bounded A/B workloads and resource measurements |
| `docs/design/GLON-HTTPS-SERVER-G2.md` | G3 wire-contract note |
| `GLON-ARCHITECTURE-MEMORY.md` | G3 decisions and measurements |

Frozen files `s1.c`, `s1.h`, `tests.c`, `adversarial.c` and `claims.c` are
untouched.
