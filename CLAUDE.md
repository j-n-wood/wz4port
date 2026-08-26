# fr_public — Werkkzeug4 portable generators

Extracting the procedural **texture** and **geometry** generation from
Farbrausch's Werkkzeug4 and making it run on macOS (Apple Silicon) and Linux,
with a new Dear ImGui editor.

`altona_wz4/`, `werkkzeug3/`, `v2/`, `ktg/` etc. are the upstream 2012/2014
code dump. All new work lives in `wz4port/`.

## Read first

**`docs/progress.md`** — current state, what's done, what's next, and the
gotchas. Then `docs/architecture.md` (**why the structure is shaped like this**,
and what was tried and rejected), `docs/00-overview.md`,
`docs/01-existing-model.md` (how Werkkzeug4 works — the reference document),
`docs/02-target-model.md`. Per-phase plans are `docs/03`–`docs/10`.

`docs/editor.md` is the **user** guide to `wz4ed` — panes, shortcuts, switches
and glTF export. It documents behaviour, not decisions; keep it in step with the
editor, and with `wz4ed -help`, whenever a control or switch changes.

## Working process

- **Work in phases, one stage at a time.** Each stage ends at a demonstrable
  result.
- **At the end of each phase or stage: `git add` the changes and stop for
  review.** Do not commit. Do not start the next stage until the user has
  reviewed and given direction.
- Phase plans are written to `docs/` **before** the code for that phase.
- When implementation contradicts a plan, update the plan document — the
  docs are the plan of record, not a historical record.
- **`docs/architecture.md` is the exception: it accumulates.** Add an entry
  whenever a structural decision is taken, an alternative is rejected, or a
  structural assumption turns out wrong. Mark superseded entries rather than
  deleting them.

## Rules

- **New code goes in `wz4port/`.** Never inside `altona_wz4/`.
- **Upstream changes are minimal and every one is documented** in
  `wz4port/patches/` with rationale. Prefer a shim header or an include-path
  override to a patch; prefer a patch to forking a file.
  Invariant: `git status` on `altona_wz4/` shows nothing not listed there.
- **Edit files with editor tooling, not shell.** `sed` is denied. **Do not use
  Python to read or write code** — not for editing, and not for verification
  either. Use `git diff`, `diff` and `grep` to check a refactor.
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
- **`wz4ops` must be run with a bare filename from the file's own directory,
  and switches go after it.** It derives generated *function names* from the
  input path, so `wz4ops a/b/x_ops.ops` emits `AddTypes_a/b/x_ops`, which will
  not compile. And Altona's shell parser eats the token after a `-switch`, so
  `wz4ops -headless x.ops` prints the usage text — write `x.ops -headless`.
  Both handled by `wz4_add_ops()` in `wz4port/CMakeLists.txt`.
- **`altona_config.hpp` lives in `wz4port/compat/`**, found via an include-path
  trick so it never enters `altona_wz4/`. `compat/include/.keep` is
  load-bearing. See `wz4port/README.md`.
- **macOS builds as `sPLAT_LINUX` deliberately** — Altona's "LINUX" means
  "POSIX desktop", and 21 guards spell it that way.
- **`error: attempt to use a poisoned identifier` is the `headless_core_gate`
  tripwire doing its job.** `wz4port/tests/gui_poison.h` poisons `sWindow`,
  `sGui_` and `sSimpleMaterial` so `wz4lib/doc_core.hpp` can never reacquire a
  GUI dependency. Fix the include, do not weaken the poison. `gui/theme.hpp`,
  `gui/treeinfo.hpp`, `gui/palette.hpp` and `gui/guicolor.hpp` are ours and are
  allowed, as is `base/windows.hpp` (it never names `sWindow`).

## Out of scope

Sequencer, render graph, materials, post-FX, audio, video, packfiles,
networking. These are where every remaining hard platform blocker lives
(runtime HLSL compilation, the dead NVIDIA Cg toolchain, D3D feature parity).
