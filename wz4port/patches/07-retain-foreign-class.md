# Patch 07 — remember what an operator was before we replaced it

**Files:** 2 (`wz4lib/doc_core.hpp`, `wz4lib/doc.cpp`)
**Phase:** 3 (text format and CLI), before stage 3.1
**Status:** applied
**Wider context:** `docs/architecture.md` A26

## Why

Altona handles an operator class it does not recognise by substituting
`UnknownOp` at read time (`doc.cpp:1854-1863`). That is good behaviour, and it
is the reason a document full of out-of-scope operators still loads with its
texture subgraphs intact.

But the substitution left no record. On write, `Serialize_` emitted
`classname = Class->Name` — the *substituted* name. So a build that did not
know every module would silently rewrite every unrecognised operator as
`UnknownOp`, and the document was damaged permanently.

That matters twice over here:

1. **`wz4gen` writes documents.** Phase 3 exists to convert between `.wz4` and a
   text format. Every one of the six bundled documents contains classes we have
   not registered, so without this every conversion would be destructive.
2. **The substitution was unmeasurable.** With the original name gone, there was
   no way to ask "which modules would I have to register to load this?" — a
   question phases 4 and 6 need answered.

## The change

```cpp
// doc_core.hpp, in wOp
wDocName ForeignClass;
wDocName ForeignType;
```

Set when — and only when — `FindClass` fails and `UnknownOp` is substituted.
Written back in place of `UnknownOp`'s own name. Copied by `wOp::CopyFrom` so
they survive clipboard and undo. Not serialised themselves: they are recovered
from the file on every read.

Three sites, about fifteen lines including comments.

## What this does NOT fix

**The parameters of an unrecognised operator are still lost.** The reader
discards them, in four places, because `UnknownOp` declares no storage:

| `doc.cpp` | What is discarded |
|---|---|
| `:1881` | parameter words — `Skip((words-ParaWords)*4)` with `ParaWords == 0` |
| `:1898-1900` | string parameters — read into a `discard` buffer |
| `:1913` | link names and select modes — read, then dropped |
| `:1963-1976` | array data — entries allocated at `ArrayCount == 0` |

Carrying all of that through would mean retaining a raw record per foreign
operator and replaying it on write. It is achievable, and it is deliberately
**not** done: it would be building fidelity for content this port has decided
not to support — render graph, materials, effects. The phase 3 gate already
scopes the round-trip guarantee to "the subgraphs whose classes we have
registered".

So the guarantee this patch establishes is precise: **identity and geometry
survive; parameter content does not.** A resaved document is not byte-identical
to its input, and the difference is measurable — reloading a resaved
`example.wz4` reports 4,481 unknown-class reads where the original reported
4,687, because the 206 default-operator instances belonging to foreign
operators are not written either.

## Verified

`wz4gen identity <doc> <scratch>` loads, saves, reloads, and compares the class
identity tally. It is a `ctest` case for all six bundled documents.

On `example.wz4`: **4,899 operators and 221 distinct class identities preserved
across a save/reload**, 207 of which this build cannot load.

Checked independently, through a different code path, rather than trusting the
one test: `wz4gen list` on the *resaved* file reports the same 207 unregistered
classes with the same counts — `Wz4Mesh.Cube` 268, `Wz4Render.Render` 244,
`GenBitmap.GlowRect` 144, and so on. The original names are genuinely in the
written bytes.

Worth noting about the test's shape: its "before" side reads the untouched
original, so it is ground truth. Had the write-back been broken, the reloaded
file would have collapsed 207 identities into one and the test would have failed
loudly rather than passed vacuously.

## What it immediately bought

The question "how much content is actually reachable once we register the
texture module?" was unanswerable before this patch and is now a one-liner.
Across the six bundled documents, **817 `GenBitmap` operators** — the phase 4
target — and 1,424 `Wz4Mesh` for phase 6. `example.wz4` alone:

| Output type | Operators | Status |
|---|---:|---|
| `Wz4Mesh` | 1,424 | phase 6 |
| `Wz4Render` | 1,299 | out of scope |
| `GenBitmap` | 624 | **phase 4** |
| `ModShader` / `ModMtrl` / `ModShaderSampler` / `SimpleMtrl` | 779 | out of scope |
| `Wz4Particles`, `Sph*`, `Wz4BSP`, … | ~250 | out of scope |

## Behaviour

For a build that recognises every class in a document: none — both fields stay
empty and `Serialize_` takes the same path as before. For a build that does not:
documents survive a load/save instead of being silently damaged.
