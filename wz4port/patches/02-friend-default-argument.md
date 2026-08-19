# Patch 02 — friend declaration with a default argument

**File:** `altona_wz4/altona/main/base/graphics.hpp`
**Lines touched:** 1
**Phase:** 1 (toolchain)
**Status:** applied

## Why

```
graphics.hpp:731: error: friend declaration specifying a default argument must be a definition
graphics.cpp:61:  error: friend declaration specifying a default argument must be the only declaration
graphics.cpp:718: error: friend declaration specifying a default argument must be the only declaration
```

C++ forbids a default argument on a friend declaration unless that
declaration is also the definition ([dcl.fct.default]/4). MSVC accepts it;
clang does not.

The knock-on effect is worse than the message suggests: because the friend
declaration is rejected, friendship is never granted, so five further errors
appear in `graphics.cpp:730-740` about `Next`, `IsMemMarkSet`, `Destroy` and
`Data` being private members of `sVertexFormatHandle`. All six errors have
this one cause.

## The change

```cpp
// graphics.hpp:731
- friend void sFlushVertexFormat(sBool flush,void *user=0);
+ friend void sFlushVertexFormat(sBool flush,void *user);
```

## Why this is safe

The `=0` was unused. Checked across the whole of `altona_wz4/`:

- `graphics.hpp:731` — the friend declaration (the only place with a default)
- `graphics.cpp:61`  — a plain declaration, **no** default
- `graphics.cpp:718` — the definition, **no** default
- `graphics.cpp:98`  — `sAddMemMarkCallback(sFlushVertexFormat)`

That last line is the only use, and it takes the function's address rather
than calling it, so a default argument could never have applied. There is no
one-argument call site anywhere in the tree.

## Alternative considered

Moving the default onto `graphics.cpp:61`'s declaration would have preserved
it for callers that include `graphics.cpp`, but nothing includes a `.cpp` and
nothing wants the default. Deleting it is simpler and loses nothing.

## Behaviour

None. This is a declaration-only change to an argument nobody supplied.
