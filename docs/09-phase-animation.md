# Phase 7 — Animated geometry

**Goal:** skeletal animation of generated meshes — skeletons, animation channels, vertex
skinning, and enough timeline UI to see it move. This is **priority 3**.

Explicitly **not** the demo sequencer.

---

## Scope

| Component | LOC | Notes |
|---|---:|---|
| `wz4frlib/wz4_anim.cpp` / `.hpp` | 632 | `Wz4Skeleton`, `Wz4Channel` |
| `wz4frlib/wz4_anim_ops.ops` | — | 2 types, 2 operators |
| Skinning in `wz4_mesh.cpp` | — | Per-vertex `Index[4]` / `Weight[4]` |
| `BakeAnim` operator | — | In `wz4_mesh_ops.ops` |

Measured dependencies of `wz4_anim.cpp`: **zero** graphics, GUI, Win32 and SIMD. It comes
across untouched.

### What exists

```
type Wz4Channel
type Wz4Skeleton : MeshBase
operator Wz4Skeleton BoneChain()
operator Wz4Skeleton ImportSkeleton()
operator Wz4Mesh     BakeAnim(Wz4Mesh)
```

Plus the skinning attributes already carried on every `Wz4MeshVertex`:
`sS16 Index[4]; sF32 Weight[4]` — four-bone skinning, standard.

`Wz4Skeleton` derives from `MeshBase`, so skeletons flow through the same graph machinery as
meshes with no special handling.

### The boundary with the sequencer

Werkkzeug4 has two distinct animation systems and only one is in scope:

- **Skeletal animation** — `Wz4Skeleton`, `Wz4Channel`, joint transforms over time, vertex
  skinning. **In scope.** It is a property of the geometry.
- **Parameter animation** — the embedded scripting language (`wz4lib/script.cpp`, 4,355 LOC),
  animation curve and clip operators (`curve`/`clip` class flags), the timeline editor, and
  beat/BPM synchronisation. **Out of scope.** It is demo sequencing.

The line is clean because they meet only at the document level. Skeletal animation needs a
time value; where that value comes from is the sequencer's business, and we supply it from a
scrubber.

---

## Stages

### 7.1 — Build the animation module

Add `wz4_anim.cpp` and generated `wz4_anim_ops` to `libwz4geo`. Register the operators.

**Gate:** `BoneChain` constructs a skeleton headlessly and its joint hierarchy is printable.

### 7.2 — Skeleton evaluation and skinning

Evaluate a `Wz4Skeleton` at a given time to joint matrices, and apply four-bone skinning to a
`Wz4Mesh`. Both already exist in the source; the work is exposing them headlessly and
confirming the time parameter is threaded correctly now that the sequencer is absent.

Verify `BakeAnim`, which bakes an animation into a mesh — likely the simplest end-to-end path
and therefore the first thing to get working.

**Gate:** a `BoneChain` skeleton driving a skinned mesh produces different vertex positions at
different times, verified by exporting OBJ at two times and diffing.

### 7.3 — Test cases

Extend the phase 6 harness with time:

- `wz4gen render doc.wz4t --op=X --time=T --out=frame.obj`
- Cases export several frames; each frame gets structural assertions and a golden.
- Assertions worth making: vertex counts stable across time, bounding box moves as expected,
  no NaNs, weights sum to 1, skinned positions equal unskinned at the rest pose.

The rest-pose identity check is the strongest available signal — it is exact, and it catches
most matrix-convention errors immediately.

**Gate:** animated cases pass at multiple times on both platforms.

### 7.4 — Timeline scrubber

Minimal UI: a time slider with a numeric field, play/pause, loop, and a frame-rate setting.
Drives the preview's time value and nothing else.

Deliberately not built: keyframe editing, curve editing, clips, tracks, beat sync, BPM. Those
are the sequencer.

**Gate:** dragging the scrubber animates the mesh in the 3D preview.

### 7.5 — Skeleton visualisation

Draw joints and bones in the 3D preview — small markers with connecting lines, and optional
bone-influence colouring on the mesh. Cheap, and it makes skeleton problems obvious rather
than mysterious.

**Gate — phase gate.** Load or build a skinned mesh with a skeleton, scrub the timeline, see
it animate with the skeleton overlaid, export a frame to OBJ.

---

## Deliverables

- `wz4_anim` in `libwz4geo`
- `--time` support in `wz4gen`
- `wz4port/tests/anim/`
- Timeline scrubber and skeleton visualisation in the editor

## Open questions to resolve in this phase

1. How does `Wz4Channel` obtain its time value once the script/sequencer layer is absent? This
   determines how the scrubber connects, and should be answered before stage 7.2.
2. Is `ImportSkeleton` usable without XSI assets, or is `BoneChain` the only practical source
   of skeletons in our scope?
3. Does anything in the skeletal path reach into `script.cpp`? Phase 2 should already have
   settled whether `script.cpp` is linked at all; confirm here.

## Risks

| Risk | Assessment |
|---|---|
| Skeletal animation is more entangled with the script/sequencer layer than it appears | **The main risk.** Open question 1 should be answered early — before committing to the phase — since it determines whether this is a small phase or a large one |
| No usable skinned test assets | Moderate. `BoneChain` generates skeletons procedurally, so a synthetic case is constructible without imported assets |
| Matrix convention errors producing plausible but wrong deformation | Low, given the rest-pose identity check |
