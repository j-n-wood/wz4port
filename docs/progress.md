# Progress — session bootstrap

Read this first when picking the project up cold. It records where things
stand, what has been decided and why, and what would otherwise have to be
rediscovered the hard way.

**Last updated:** end of phase 1.
**Status:** Phase 1 complete and verified. Phase 2 not started.

---

## What this project is

Extracting the **procedural generation** half of Farbrausch's Werkkzeug4 from
the `fr_public` code dump and making it run on macOS (Apple Silicon) and
Linux, with a new Dear ImGui editor.

Priority order: **texture generation → geometry generation → animated
geometry**. Sequencer, render graph, materials and post-FX are out of scope —
they are also where every remaining hard platform blocker lives.

Read next, in order:

| Doc | Why |
|---|---|
| `00-overview.md` | Goals, non-goals, tree layout, working conventions |
| `01-existing-model.md` | **How Werkkzeug4 actually works.** The reference document; fully cited |
| `02-target-model.md` | What we are building and every deliberate divergence |
| `03-phase-toolchain.md` … `09-phase-animation.md` | Per-phase plans |

`wz4port/README.md` covers build mechanics and the three non-obvious things
about the build.

---

## Phase status

| Phase | State |
|---|---|
| 0 — Documentation (`docs/00`–`02`) | **Done**, reviewed and approved |
| 1 — Toolchain and portable base | **Done**, gate passed |
| 2 — Headless op runtime + metadata | **Next** |
| 3 — Text graph format + CLI | Not started |
| 4 — Texture library + tests | Not started |
| 5 — Texture GUI | Not started |
| 6 — Geometry | Not started |
| 7 — Animated geometry | Not started |

---

## What builds today

```sh
cmake -S wz4port -B wz4port/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C wz4port/build
ctest --test-dir wz4port/build --output-on-failure
```

Clean build from scratch: **0 errors**. Warnings are expected and benign
(Altona overloads global `operator new` as `inline`, and has a
`if(this)` null check).

| Target | What it is |
|---|---|
| `altona_base` | Altona shell subset — types, math, serialize, system, blank renderer |
| `altona_util` | `scanner` + `scanconfig`, the slice the host tools need |
| `wz4ops` | Upstream's `.ops` code generator, native arm64 |
| `wz4ops_gate` | Regenerates `basic_ops` + `wz3_bitmap_ops` into `build/generated/` |
| `simd_parity` | Verifies all 43 SSE2 intrinsics against scalar models |

`simd_parity`: **70,184 checks, 0 failures** on arm64 via sse2neon.

### Files created so far

```
docs/                          00–09 plus this file
wz4port/
  CMakeLists.txt
  README.md
  compat/
    altona_config.hpp          NOT in altona_wz4/ — see gotcha 1
    include/
      .keep                    must exist; see gotcha 1
      wz4port_posix_compat.h   glibc-isms, force-included
      simd_compat.hpp          SSE2 -> NEON
      linux/joystick.h         stub; macOS has no /dev/input/js*
  patches/
    01-pthread-t-casts.md
    02-friend-default-argument.md
  tests/simd_parity.cpp
  third_party/sse2neon.h       pinned v1.9.1, MIT, 11,222 lines
.gitignore                     new, repo root
.claude/settings.local.json    gitignored tool allowlist
```

---

## Upstream footprint

**32 files.** The isolation invariant is that `git status` on `altona_wz4/`
must never show anything not listed in `wz4port/patches/`.

Two categories, worth keeping distinct:

**Code changes — 2 files, 5 lines.** Both genuine C++ errors under clang, not
portability preferences.

```
 M base/graphics.hpp      (1 line)  patches/02  friend decl with default arg
 M base/system_linux.cpp  (4 lines) patches/01  pthread_t is a pointer on macOS
```

**Encoding only — 30 files, 72 characters.** Latin-1 → UTF-8, verified
character-identical against the git originals; no semantic change.
See `patches/03`. This unblocked `script.cpp` (clang errors on Latin-1 inside
string literals) and retired the `grep -a` requirement.

---

## Gotchas that cost real time

### 1. `altona_config.hpp` lives in `wz4port/compat/`, not in `altona_wz4/`

`base/types.hpp:67` does `#include "../altona_config.hpp"` and upstream
expects you to create that file inside `altona_wz4/altona/`. We don't.

A quoted include that fails to resolve next to its including file is retried
against each `-I` directory **with the relative path intact**, so with
`compat/include` on the path:

```
compat/include/../altona_config.hpp  ==  compat/altona_config.hpp
```

`compat/include/.keep` exists so that directory is always present. Deleting
it breaks the build in a confusing way.

### 2. Source encoding — RESOLVED, no longer a hazard

*Historical note; nothing to do. Retained because it explains patch 03 and a
wrong finding in the survey.*

30 files were Latin-1 encoded. A single invalid UTF-8 byte made `grep` treat
a file as binary and report nothing — silently. That produced a wrong survey
finding (`sCONFIG_GUID` reported as makefile-only when it is used at
`types.hpp:2143`).

Worse, clang **errors** on Latin-1 inside string literals, which would have
blocked `wz4lib/script.cpp` in phase 2.

All 30 files were converted to UTF-8 (`wz4port/patches/03-latin1-to-utf8.md`),
verified character-identical against their git originals. **`grep -a` is no
longer needed.**

Note for anyone tempted to "clean up" the remaining non-ASCII characters:
**`°` is the dot-product operator in the ASC shader language.** It is real
syntax in ten `.asc` files, registered by `shadercomp/asc_doc.cpp:305` and
tokenised as `case 0xb0:` in `wz4lib/script.cpp:2993`. Do not replace it with
ASCII.

### 3. `wz4ops` must be run with a bare filename from the file's own directory

It derives *both* output paths *and* generated function names from the input
path with the extension stripped. `wz4ops a/b/basic_ops.ops` emits
`void AddTypes_a/b/basic_ops(...)`, which does not compile.

`wz4_add_ops()` in CMakeLists.txt copies each `.ops` into
`build/generated/<subdir>/` and runs the tool there with just the filename,
yielding `AddTypes_basic_ops` / `AddOps_basic_ops` — what the `sREGOPS` macro
(`wz4lib/doc.hpp:51-56`) expands to. It also keeps generated output out of
`altona_wz4/`.

### 4. macOS builds as `sPLAT_LINUX`, deliberately

Altona's "LINUX" means "POSIX desktop". 21 guards across the tree spell that
as `(sPLAT_WINDOWS || sPLAT_LINUX)`. Adding a `sPLAT_APPLE` — which the
original plan called for — would mean revisiting every one of them for no
behavioural gain, and the two `pthread_t`-shaped guards in `system.hpp`
(`:370`, `:422`) are already correct for macOS pthreads.

Genuine differences are handled per-file with `__APPLE__` in
`compat/include/wz4port_posix_compat.h` (`lseek64`, `mmap64`, `ftruncate64`,
`stat64`, `strdupa`, `pthread_yield`, `O_LARGEFILE`), force-included into
every TU. `system_linux.cpp` compiles in place, unforked.

### 5. `types.cpp` needs `graphics.cpp`

It references `sRender3DFlush()`, so `graphics.cpp` and `graphics_blank.cpp`
cannot be omitted even for console tools. Discovered at link time after
trying to defer them.

### 6. Altona predates C++11 UDLs

It writes `"..."sTXT(x)` with no separating space, which C++11 parses as a
user-defined literal. Handled with `-Wno-reserved-user-defined-literal`.

---

## Working conventions

Agreed with the user; these are not optional.

- **New code lives in `wz4port/`.** Never inside `altona_wz4/`.
- **Upstream changes are minimal, and every one is recorded** in
  `wz4port/patches/` with rationale. Prefer a shim header or an include-path
  override to a patch; prefer a patch to forking a file.
- **File edits use editor tooling, not shell.** `sed` is denied outright.
  Python is allowed for specific purposes but never for general code editing.
- **Every stage ends at a review gate.** Stop at a demonstrable result,
  `git add` the changes (do **not** commit), and confirm direction before
  starting the next stage.
- **Prefer single shell commands.** Long `&&`/pipe chains trip the permission
  matcher and interrupt the workflow.

---

## Tooling note for a new session

`.claude/settings.local.json` (gitignored) holds a build/inspect allowlist —
`cmake`, `ninja`, `clang++`, `grep`, `git status`/`diff`/`log`, running
binaries from `build/`, and so on. `sed` is denied; `python` is set to ask.

It was written *during* the previous session, after startup, so it never
loaded — the settings watcher only watches directories that existed when the
session began. **A new session will load it normally**, since `.claude/` now
exists. If prompts still appear for allowlisted commands, the file is not
being read; check with `claude --debug`.

Run from the repository root (`/Users/joshwood/code/fr_public`), not from
`altona_wz4/`.

Not allowlisted on purpose: `curl` and other network fetches, `git commit`,
`git push`.

---

## Next: phase 2 — headless op runtime and metadata

Full plan in `docs/04-phase-headless-core.md`. Summary:

**The structural work of the whole project.** Today every operator source
file transitively includes Altona's entire widget toolkit, because
`wz4lib/doc.hpp:18` includes `gui/gui.hpp`. Until that is severed nothing
headless is possible.

The coupling is far lighter than the include graph suggests — measured:

| File | LOC | GUI-touching lines |
|---|---:|---:|
| `wz4lib/doc.hpp` | 1,100 | **18** |
| `wz4lib/doc.cpp` | 4,469 | 16 |
| `wz4lib/build.cpp` | 998 | 1 |
| `wz4lib/basic.cpp` | 980 | 7 |
| `wz4lib/script.cpp` | 4,355 | 0 |

Three declarations account for nearly all of it: `wPaintInfo` (`doc.hpp:113`),
`wGridFrameHelper` (`:304`), `wCustomEditor` (`:326`).

Stages: split `doc.hpp` into `doc_core.hpp` + `doc_gui.hpp` (upstream patch —
the most significant one in the project); build `tools/opsmeta` to emit
operator metadata as JSON; build `libwz4core`.

**Gate:** headless core links; metadata JSON emitted for `basic` and
`wz3_bitmap`; a test program constructs a document, connects two operators by
geometry alone, and prints the derived input lists.

### Decide early in phase 2

1. Can `wz4ops`' parser be reused cleanly by a separate `opsmeta` tool
   (preferred — keeps the metadata path entirely in `wz4port/`), or is
   patching `wz4ops` the pragmatic choice? Read `tools/wz4ops/doc.hpp` and
   assess how separable the parse tree is from the emitter.
2. Does `wExecutive` require `script.cpp`, or can the scripting path be
   excluded entirely?
3. How best to exclude `type` blocks' `Show`/`Paint` externs — a generation
   mode, or a compile-time guard in the emitted code?

### Main risk

The `doc.hpp` split being messier than 18 lines suggests. `wType` and
`wClass` hold *pointers to* GUI-facing functions (`MakeGui`, `Handles`,
`Show`, `Paint`). Forward-declaring the incomplete types should suffice since
nothing headless dereferences them — but prove that early, before building
anything on top of it.

---

## Known open items

- **No reference oracle.** There is no working Werkkzeug4 build to diff
  against, and the bundled `.exe` is x86 Windows. Golden images from phase 4
  will capture *our* behaviour, not the original's — a plausible, stable port
  bug would pass. Mitigations: SSE2-vs-NEON bit parity, structural assertions
  for meshes, and visually diagnostic per-operator cases. Generating true
  reference output on a Windows machine remains worthwhile as a one-off; the
  `.wz4t` text format (phase 3) exists partly to make that cheap.
- **x86-64 side of SIMD parity unverified.** `simd_parity` passes on arm64.
  It should also be run on x86-64 Linux, where it exercises native SSE2 — the
  two must agree.
- **`Text` / `Text3D` / `Path3D` operators** need FreeType (and a tessellator
  for the 3D ones). Deferred; 3 operators out of 84.
