# fr_public — Werkkzeug4 portable generators

Extracting the procedural **texture** and **geometry** generation from
Farbrausch's Werkkzeug4 and making it run on macOS (Apple Silicon) and Linux,
with a new Dear ImGui editor.

`altona_wz4/`, `werkkzeug3/`, `v2/`, `ktg/` etc. are the upstream 2012/2014
code dump. All new work lives in `wz4port/`.

## Read first

**`docs/progress.md`** — current state, what's done, what's next, and the
gotchas. Then `docs/00-overview.md`, `docs/01-existing-model.md` (how
Werkkzeug4 works — the reference document), `docs/02-target-model.md`.
Per-phase plans are `docs/03`–`docs/09`.

## Working process

- **Work in phases, one stage at a time.** Each stage ends at a demonstrable
  result.
- **At the end of each phase or stage: `git add` the changes and stop for
  review.** Do not commit. Do not start the next stage until the user has
  reviewed and given direction.
- Phase plans are written to `docs/` **before** the code for that phase.
- When implementation contradicts a plan, update the plan document — the
  docs are the plan of record, not a historical record.

## Rules

- **New code goes in `wz4port/`.** Never inside `altona_wz4/`.
- **Upstream changes are minimal and every one is documented** in
  `wz4port/patches/` with rationale. Prefer a shim header or an include-path
  override to a patch; prefer a patch to forking a file.
  Invariant: `git status` on `altona_wz4/` shows nothing not listed there.
- **Edit files with editor tooling, not shell.** `sed` is denied. Python is
  fine for inspection or a bounded, verifiable transform — never for general
  code editing.
- **Prefer single shell commands.** Long `&&`/pipe chains trip the permission
  matcher and interrupt the workflow.
- **Verify, don't assume.** This codebase has rewarded measurement repeatedly
  and punished inference. Compile it, run it, diff it.

## Build

```sh
cmake -S wz4port -B wz4port/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja -C wz4port/build
ctest --test-dir wz4port/build --output-on-failure
```

Clean build must be 0 errors. Warnings from Altona are expected.

## Landmines

- **`°` is the dot-product operator in the ASC shader language** — real syntax
  in ten `.asc` files, registered at `shadercomp/asc_doc.cpp:305`, tokenised
  as `case 0xb0:` in `wz4lib/script.cpp:2993`. Do not "clean up" non-ASCII
  characters in this tree.
- **`wz4ops` must be run with a bare filename from the file's own directory.**
  It derives generated *function names* from the input path, so
  `wz4ops a/b/x_ops.ops` emits `AddTypes_a/b/x_ops`, which will not compile.
  Handled by `wz4_add_ops()` in `wz4port/CMakeLists.txt`.
- **`altona_config.hpp` lives in `wz4port/compat/`**, found via an include-path
  trick so it never enters `altona_wz4/`. `compat/include/.keep` is
  load-bearing. See `wz4port/README.md`.
- **macOS builds as `sPLAT_LINUX` deliberately** — Altona's "LINUX" means
  "POSIX desktop", and 21 guards spell it that way.

## Out of scope

Sequencer, render graph, materials, post-FX, audio, video, packfiles,
networking. These are where every remaining hard platform blocker lives
(runtime HLSL compilation, the dead NVIDIA Cg toolchain, D3D feature parity).
