# Werkkzeug4 portable generators — overview

## What this is

`fr_public` is the Farbrausch source release: roughly a decade of demoscene tooling, dumped
in 2012 and topped up in 2014. Buried in it is one of the better procedural content
generation systems ever written — Werkkzeug4 — which has only ever run on Windows with
Direct3D.

This project extracts the **generation** half of Werkkzeug4 and makes it run on macOS
(Apple Silicon) and Linux, with a new cross-platform editor built on Dear ImGui.

## Goals, in priority order

1. **Texture generation** — a working GUI tool.
2. **Geometry generation** — the same, for meshes.
3. **Animated geometry** — skeletons, channels, skinning.

Each is independently valuable. Nothing in a later goal is allowed to block an earlier one.

## Non-goals

Explicitly out of scope, and not merely deferred:

- The demo **sequencer** and timeline/clip system.
- The **render graph** (`Wz4Render`, `wz4_demo2`) and all scene/render nodes.
- The **material system** — `wz4_mtrl`, `wz4_mtrl2`, and the modular `wz4_modmtrl*` shader
  generator.
- **Post-processing** (`wz4_ipp`) and the per-demo one-shot effect packs (`fr0xx_*`,
  `tron`, `easter`, `kbfx`, `chaosfx`).
- Audio, video, packfiles, networking, `screens4`.

This is not squeamishness about effort. Those subsystems are where every remaining hard
platform dependency lives — runtime HLSL compilation, the dead NVIDIA Cg toolchain, and
Direct3D feature parity. Dropping them removes the blockers rather than working around them.

## Targets

- **macOS on Apple Silicon** (development host: M2 Pro, macOS 26.2, arm64).
- **Linux x86-64**.

Windows is not a target, but is also not deliberately broken; upstream sources stay
buildable under MSVC because we barely touch them.

## Toolchain

clang 17, CMake, Ninja. FreeType for font-dependent operators. Dear ImGui + GLFW, vendored.
`sse2neon` vendored for the ARM SIMD path.

## Tree layout

```
fr_public/
  docs/                      planning + model documentation (this directory)
  wz4port/                   all new code, parallel to altona_wz4/
    CMakeLists.txt
    compat/                  altona_config.hpp, sse2neon shim, system_osx
    libwz4core/              headless op runtime (doc/build/basic)
    libwz4tex/               texture operators
    libwz4geo/               geometry operators
    tools/opsmeta/           .ops -> metadata JSON
    tools/wz4gen/            headless CLI: render, convert, list, describe
    editor/                  Dear ImGui application
    tests/                   .wz4t cases, golden outputs, runner
    patches/                 minimal tracked upstream diffs, each with rationale
  altona_wz4/                upstream, unchanged except where patches/ says otherwise
```

## Working conventions

- **New code lives in `wz4port/`**, never inside `altona_wz4/`.
- **Upstream sources are touched as little as possible.** Where a change is genuinely
  unavoidable it is small, additive where it can be, and recorded in `wz4port/patches/`
  with a note explaining why an override was not possible. `git status` on `altona_wz4/`
  should never show anything that is not enumerated there.
- **File edits use editor tooling, not shell.** No `sed`, no scripted rewrites, no Python
  file surgery. Shell is for building, running and inspecting.
- **Every stage ends at a review gate.** Work stops at a demonstrable result and direction is
  confirmed before the next stage begins.

## Documents

| Doc | Contents |
|---|---|
| `progress.md` | **Current state — read first when picking the project up.** |
| `00-overview.md` | This document. |
| `architecture.md` | The structure as it stands, and the decision record behind it — including the roads not taken. |
| `01-existing-model.md` | How Werkkzeug4 actually works, with citations. |
| `02-target-model.md` | What we are building, and every deliberate divergence. |
| `03-phase-toolchain.md` | Phase 1 — portable base and host tools. |
| `04-phase-headless-core.md` | Phase 2 — headless op runtime and metadata. |
| `05-phase-text-format.md` | Phase 3 — the `.wz4t` text graph format and CLI. |
| `06-phase-texture.md` | Phase 4 — texture library and test suite. |
| `07-phase-texture-gui.md` | Phase 5 — the ImGui editor. |
| `08-phase-geometry.md` | Phase 6 — geometry. |
| `09-phase-animation.md` | Phase 7 — animated geometry. |

`01` and `02` are the load-bearing ones. Everything else is downstream of them.

Two of these accumulate rather than describing a plan: `progress.md` is
rewritten each stage to say where things stand, and `architecture.md` is
appended to whenever a structural decision is taken or a structural assumption
turns out to be wrong. Superseded entries in `architecture.md` are marked, not
deleted — a decision that was reversed is more useful than one silently
replaced.

## Why this is tractable

The survey that preceded this plan established, by measurement rather than assumption:

- The texture generator (`wz3_bitmap_code.cpp`, 3,578 LOC) touches **no graphics API, no GUI,
  and no Win32**. Its only external dependency is `<emmintrin.h>`.
- The mesh generator (`wz4_mesh.cpp`, 7,406 LOC) confines all renderer and Win32 code to two
  contiguous, banner-delimited regions.
- Across all 45 `.cpp` files in `wz4frlib`, exactly **one** includes `windows.h`, and it is a
  file we do not need.
- Altona's op runtime header has **18** GUI-touching lines; `doc.cpp` has 16 in 4,469;
  `build.cpp` has 1; `script.cpp` has none.
- There is **no inline assembly anywhere in wz4**. (The older werkkzeug3 has 620+ lines of
  MMX in its bitmap code — one of several reasons wz4 is the right starting point.)
- Altona's own core compiles under clang on arm64 today with a single error: a missing
  config header that a documented setup step creates.

The code was written to be ported. It just never was.
