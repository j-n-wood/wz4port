# Patch 03 — Latin-1 to UTF-8 source encoding

**Files:** 30 under `altona_wz4/`
**Bytes changed:** 72 characters re-encoded (+72 bytes total)
**Phase:** 1 (follow-up)
**Status:** applied

## Why

Thirty Altona/wz4 sources were encoded Latin-1. This caused two distinct
problems, one cosmetic and one blocking.

### Blocking: clang rejects Latin-1 inside string literals

Verified:

```
$ clang++ -std=c++11 latin1_lit.cpp
error: illegal character encoding in string literal
```

Comments are tolerated — which is why the phase 1 build succeeded — but
string literals are a hard error. Four files contain Latin-1 bytes inside
literals or as functional data:

| File | What |
|---|---|
| `wz4lib/script.cpp:2004` | `tb.PrintF(L"°%d ",count)` — program output |
| `shadercomp/asc_doc.cpp:305` | `Scan.AddTokens(L"°")` — registers the ASC dot-product operator |
| `wiki/pdf.cpp:131` | `Print(L"%íì¦\"\n")` — PDF output |
| `base/system_win.cpp:612` | `static const sChar symbols[] = L"…"` |
| `examples/_test/basetest/main.cpp:156-157` | `L"…öÄü-"` — case-conversion unit test data |

`script.cpp` is needed in phase 2, so this would have blocked the headless op
runtime.

### Cosmetic: `grep` silently skipped these files

A single invalid UTF-8 byte makes `grep` treat a file as binary and report
nothing — no error, no warning. This produced a wrong finding during the
survey: `sCONFIG_GUID` was reported as appearing only in makefiles when it is
used at `types.hpp:2143`. Every search of this tree needed `-a`, and
forgetting was silent.

## Why UTF-8 rather than transliteration to ASCII

The original request was to flatten the umlauts to plain letters. Inspecting
the bytes first showed that would have broken the build:

```
0xB0 '°' x35   0xE4 'ä' x9    0xDF 'ß' x8    0xFC 'ü' x7
0xF6 'ö' x3    0xB2 '²' x3    0xC4 'Ä' x1    0xD6 'Ö' x1
0xDC 'Ü' x1    0xED 'í' x1    0xEC 'ì' x1    0xA6 '¦' x1    0x80 x1
```

The majority are not German at all. **`°` is the dot-product operator in the
ASC shader language** — it appears as real syntax in ten `.asc` files
(`in_mat0°float4(in_pos,1)`), is registered by `asc_doc.cpp:305`, and is
tokenised as `case 0xb0:` in `script.cpp:2993`. Replacing it with ASCII would
have broken every shader.

UTF-8 conversion preserves the value exactly. Verified:

```
UTF-8 source  L"°"  ->  U+00B0
```

which is precisely the `0xb0` that `script.cpp:2993` matches on. `-fshort-wchar`
does not change this.

## The change

Every file was decoded as Latin-1 and re-encoded as UTF-8. No characters were
added, removed or substituted. Files already valid UTF-8 were left untouched.

## Verification

Each converted file was compared against its git original by decoding the old
bytes as Latin-1 and the new bytes as UTF-8 and requiring the resulting text
to be identical:

```
character-identical: 30
problems: none
```

(The check also flagged `graphics.hpp` and `system_linux.cpp` as differing —
correctly, since those are the deliberate patches 01 and 02.)

Then:

- `grep -c "sChar" base/types.hpp` → **205** (was 0 without `-a`)
- `clang -fsyntax-only script.cpp` → **0** "illegal character encoding" errors
- `clang -fsyntax-only asc_doc.cpp` → **0** ditto
- Clean rebuild → **0 errors**; `ctest` → **1/1 passed**

## Effect on the isolation invariant

The upstream footprint goes from 2 files to 32. The distinction worth keeping
is that patches 01 and 02 change *code*, whereas this one changes *encoding
only* and is provably content-preserving. `git diff` on these 30 files shows
no semantic change.

## Consequence for tooling

`grep -a` is **no longer required** in this tree. `docs/progress.md` gotcha 2
is retired.
