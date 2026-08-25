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

### 7.1 — Build the animation module — **already done, in stage 6.1**

`wz4_anim.cpp` and the generated `wz4_anim_ops` went into `wz4geo` when the mesh library did, and
`BoneChain` and `ImportSkeleton` have been registered since. The module needed nothing: measured
zero graphics, GUI, Win32, SIMD **and script** dependencies.

**Gate — met** as a side effect of 6.1.

### 7.2 / 7.3 — `AnimateBones`, and animation that moves — **done**

**Zero upstream changes.** The first operator in this project that is not a port lives entirely in
`wz4port/geo/animate_ops.ops`, because `wz4_add_ops` copies a `.ops` into the build tree and runs
the generator there — the source may live anywhere. 151/151 ctest.

#### "keep bones" verified first, and it paid off twice

The plan's top risk was that `Deform`'s `keep bones` flag had never been executed. Verified before
writing anything, and it behaves exactly as the code predicted:

| | bounds | checksum |
|---|---|---|
| `Deform` default — bakes, destroys the rig | x 0..1, bent | `512429fc92000000` |
| `Deform` + keep bones — rig preserved | x −0.5..0.5, **rest pose** | `b8b8f35dea000000` |
| that rig, `BakeAnim(0)` | x 0..1 | **`512429fc92000000`** |

Baking a preserved rig reproduces Deform's own internal bake **bit for bit**. That retired the risk
and handed the phase a correctness oracle for free — it is now the `an_rest_baked == an_ref`
assertion, and it pins the whole `BasePose` × `mata` × weights chain without a locked constant
meaning anything.

#### The operator

`Wz4Mesh AnimateBones(Wz4Mesh)` replaces each joint's `Wz4ChannelConstant` with a
`Wz4ChannelPerFrame` sampling a rotation, translation or scale, with a per-joint phase so a chain
bends progressively. Modules that declare no `type` of their own are ordinary — thirteen upstream
ones do — so it borrows `Wz4Mesh` through its `header` block.

Three decisions worth keeping:

- **`Wz4ChannelPerFrame`, not `Wz4ChannelLinear`.** Linear overrides neither `CopyTo()` nor
  `Serialize()`, and `Wz4Skeleton::CopyFrom` calls `CopyTo()` on every channel — so an ordinary
  edit downstream would hit `sFatal(L"cant copy this kind of channel")`. PerFrame has both, plus a
  construction template at `wz4_mesh.cpp:7393`. This is the correction to "Linear is implemented
  and merely never constructed": it is implemented *incompletely*.
- **The base pose is read via `Channel->Evaluate(0, key)`**, not by assuming the existing channel is
  a Constant. It costs one virtual call and keeps working if that ever changes.
- **A mesh with no rig is an error, said out loud** via `cmd->SetError`. It is almost always a
  `Deform` without `keep bones`, and silently doing nothing looks exactly like an animation that
  will not play.

#### The three pairs

The cases assert relationships, not just numbers — counts and bounds would catch none of them:

| | |
|---|---|
| `an_rig` **==** `an_animated` | the operator replaces channels and moves no vertex |
| `an_baked_t0` **≠** `an_baked_t1` | **the phase gate** — two times, two geometries |
| `an_rest_baked` **==** `an_ref` | the rest-pose identity, exact |

A fourth agreement fell out unarranged: `an_ref`'s checksum equals `t_deform`'s from
`ops_transform.wz4t` — the same bar along the same line, reached through a different case file with
a different key count.

Only the z extent is asserted on the baked cases, and that is the derivation: the animation rotates
about z, so x and y must move and **z cannot**. A rotation leaking into z is the quaternion or
matrix-convention error that otherwise produces plausible-looking output.

#### A false invariant, caught by existing cases

The new rig checks in `geo/mesh_check.cpp` initially fired on every `Text3D` and `Path3D` mesh:
*"196 skinned vertex(es) naming a joint outside 0..-1"*. The meshes were fine — the check was wrong.
An unskinned vertex is **not** identified by a negative `Index[0]`: `wMeshTess::AddVertex` builds
vertices with `sClear`, so `Index[0]` is 0, and 0 is a valid joint number. Upstream's own `Skin`
tests `Index[0] < 0 || Index[0] >= max` against the *joint count*, so on a mesh with no skeleton
every vertex is unskinned by construction.

The recurring lesson, in a new place: **test the same condition the consumer tests.** An invariant
invented alongside its check, rather than read off the code it protects, will disagree with it.

### 7.2 (original plan text) — Skeleton evaluation and skinning

Evaluate a `Wz4Skeleton` at a given time to joint matrices, and apply four-bone skinning to a
`Wz4Mesh`. Both already exist in the source; the work is exposing them headlessly and
confirming the time parameter is threaded correctly now that the sequencer is absent.

Verify `BakeAnim`, which bakes an animation into a mesh — likely the simplest end-to-end path
and therefore the first thing to get working.

**Gate:** a `BoneChain` skeleton driving a skinned mesh produces different vertex positions at
different times, verified by exporting OBJ at two times and diffing.

> **This gate is not reachable as written** — see "What measurement found" below. Every channel a
> registered operator can construct is constant over time, so two times give one answer. Measured:
> `Cube → Deform → BakeAnim` at times 0 and 1 both give checksum `512429fc92000000`. The gate needs
> a decision on option A/B/C before it can be restated.
>
> The half that *is* reachable and worth doing first: the **rest-pose identity check**. Skinning a
> mesh at its bind pose must reproduce the unskinned mesh exactly, which is an exact assertion and
> catches most matrix-convention errors immediately.

### 7.3 — Test cases

Extend the phase 6 harness with time:

- `wz4gen render doc.wz4t --op=X --time=T --out=frame.obj`
- Cases export several frames; each frame gets structural assertions and a golden.
- Assertions worth making: vertex counts stable across time, bounding box moves as expected,
  no NaNs, weights sum to 1, skinned positions equal unskinned at the rest pose.

The rest-pose identity check is the strongest available signal — it is exact, and it catches
most matrix-convention errors immediately.

**Gate:** animated cases pass at multiple times on both platforms.

### 7.4 — Timeline scrubber — **done**

A rigged mesh scrubs in the 3D preview: play/pause, loop, and a `t = 0..1` slider, shown **only
when the mesh actually has a rig** — almost none do, so a permanent scrubber would be a control that
does nothing on nearly every operator in the palette. 152/152 ctest.

#### The structural fix the plan predicted

`wMeshView::Upload` called `Fit()`, so re-uploading each frame would have reset the camera sixty
times a second. Upload and framing are now separate: `Upload` builds buffers, `DrawPane` frames on
operator **change** and on the "fit" button. That split is what makes scrubbing possible at all.

#### Skinning per frame, without destroying the rig

`Wz4MeshVertex::Skin` writes to an out-parameter and reads `v.Pos`, so the mesh is untouched —
unlike `BakeAnim`, which skins in place and then *releases the skeleton*. The rig has to survive
every frame, so `BakeAnim` is exactly the wrong tool here despite being the obvious one.

Three decisions worth keeping:

- **Bounds come from the rest pose, computed once.** An animated mesh's true bounds change every
  frame, and framing the camera or sizing the grid from those would make both jitter as it moves.
- **Normals are skinned too**, which neither `Skin` nor `BakeAnim` does — a baked mesh keeps its
  rest-pose normals. For a *viewer* that is visibly wrong: the shading would not follow the
  deformation. They are blended by the same weights using `sVector30`, so the matrices' translation
  row is ignored, then renormalised.
- **The whole interleaved buffer is rebuilt**, not sub-updated. Positions and normals are
  interleaved, so touching only positions means one strided write per vertex — slower than replacing
  the buffer and considerably easier to get wrong.

#### The gate asserts that scrubbing changes what you SEE

`wz4ed_pose` renders the same operator at two times and requires the screenshots to **differ**.
Nothing weaker establishes a scrubber:

- the report line alone would pass if time were plumbed through and the vertices never re-skinned —
  it prints what the code *believes*;
- one screenshot would pass if the mesh were stuck in its rest pose, since a posed mesh and a rest
  mesh look equally plausible;
- a golden would be a false-failure generator, because the image depends on the display's DPI.

Comparing two images from the **same binary in the same run** sidesteps the DPI problem entirely:
whatever it is, both shots share it, so any difference is the pose. Verified negatively — pointing
both times at the same value fails with the intended message.

### 7.4 (original plan text) — Timeline scrubber

Minimal UI: a time slider with a numeric field, play/pause, loop, and a frame-rate setting.
Drives the preview's time value and nothing else.

Deliberately not built: keyframe editing, curve editing, clips, tracks, beat sync, BPM. Those
are the sequencer.

**Gate:** dragging the scrubber animates the mesh in the 3D preview.

### 7.5 — Skeleton visualisation — **done**

Joints draw as three-axis crosses over the mesh, in the conventional red/green/blue, with a line to
the parent where one exists. A `bones` checkbox next to `wire`/`grid`/`bbox`, offered only on a rig,
and a `-nobones` switch for the gate. 153/153 ctest.

#### The rigs this build makes have no hierarchy at all

Building the overlay measured something the phase had not: **`Deform` produces a flat list of
joints, not a chain.** `Wz4AnimJoint::Init` sets `Parent = -1` (`wz4_anim.cpp:55`) and `Deform`
never assigns it. The only code in the tree that writes `Parent` is the merge remap
(`wz4_mesh.cpp:1030`) and `LoadWz3MinMesh` (`:7388`) — **an import path**, with no assets here.

So hierarchy arrived exactly the way time-varying animation did: with an imported asset, from a
modelling package. That is the same shape as the finding that motivated this whole phase, one level
up, and it means the bone half of the overlay draws nothing on anything this build can currently
produce.

Two consequences, both deliberate:

- **The crosses carry the whole picture**, so they are sized to be *read* (0.20 of the bounding
  radius) rather than merely to be present. The first attempt used 0.06 and rendered three specks.
  The cross also shows **orientation**, which a dot cannot — and orientation is the entire content
  of an `AnimateBones` rotation, whose joints turn in place. A skeleton drawn as points would look
  completely static while the mesh moved.
- **No chain is inferred from the joints' positions.** `Deform`'s joints do lie along a line, so
  connecting them would look right and be a lie: it would draw a hierarchy the data does not have.
  A viewer that invents structure is worse than one that shows none. The parent link is still drawn
  because it costs six lines and is correct the moment an importer lands.

The status line now separates the two — `rig 3 joint(s) 0 bone(s) at t = …` — so "no bones drawn"
reads as the data being flat rather than the overlay being broken.

#### Drawn last, with the depth test off

A rig is *inside* its own geometry, so a depth-tested skeleton is an invisible one on every closed
mesh — which is most of them. The cost is that near and far joints do not occlude each other, and at
this joint count that reads fine.

The bones also get their **own** buffer rather than a third section in `LineVbo`. They are the only
line geometry that moves: grid and box are built once per mesh and uploaded `GL_STATIC_DRAW`, while
the skeleton is rebuilt at every pose. Appending would have meant re-uploading the static geometry
sixty times a second, and would have broken `Draw`'s offset arithmetic, which finds the two existing
sections by counting back 24 vertices from the end.

#### One gate shape, used twice

`editor_pose.cmake` became **`editor_differs.cmake`** when 7.5 needed the same assertion: render
twice, require the images to differ. `wz4ed_pose` varies `-time`; `wz4ed_bones` varies `-nobones`.

The switch is `-nobones`, not `-bones`, because the overlay is **on by default** — a `-bones` switch
would have been a no-op and the gate would have compared a render against itself, passing by
accident. Both gates were verified negatively by pointing the two runs at identical arguments.

### 7.5 (original plan text) — Skeleton visualisation

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

## Open questions — **answered by measurement, before any code**

Taken in the order the plan asks them. Two dissolve; the third turns out to be the wrong
question, and answering it properly reshapes the phase.

### 1. How does a channel get its time once the sequencer is absent? — **it never needed one**

`Wz4Skeleton::Evaluate(sF32 time, sMatrix34 *mata, sMatrix34 *basemat)` takes time as a **plain
argument**. So does every `Wz4Channel::Evaluate`. The caller supplies it; in the original that
caller was the renderer, and for us it is `BakeAnim`'s `Time` parameter or a scrubber.

There is no coupling to unwind. The risk table below called this "the main risk" and it does not
exist.

### 2. Does the skeletal path reach `script.cpp`? — **no, zero references**

`wz4_anim.cpp`, `wz4_anim.hpp` and `wz4_anim_ops.ops` contain **no** occurrence of
`ScriptContext`, `script.hpp` or `sScript`. Counted, not skimmed.

### 3. Is `ImportSkeleton` usable without assets? — the wrong question

The real one is **"what can put a moving skeleton on a mesh?"**, and the answer changes the phase.

---

## What measurement found: skinning works, animation has no source

**Stage 7.1 is already done.** `wz4_anim.cpp` and the generated `wz4_anim_ops` went into `wz4geo`
in stage 6.1, and `BoneChain` and `ImportSkeleton` have been registered and sweeping clean since.

**A skinned mesh *is* constructible procedurally**, which the plan doubted. `Wz4Mesh::Deform`
(`wz4_mesh.cpp:1883`) creates a `Wz4Skeleton` on **both** of its branches, with joints and
per-vertex `Index`/`Weight` — so `Cube → Deform` is a skinned mesh needing no imported asset. The
`Deform` operator has been in the suite since 6.3 without anyone noticing it produces one.

**But nothing procedural can make a joint move.** Every channel a registered operator can build is
a `Wz4ChannelConstant`:

| Channel | Constructed by | Reachable here? |
|---|---|---|
| `Wz4ChannelConstant` | `BoneChain`, `Deform` | **yes** — and constant over time by definition |
| `Wz4ChannelPerFrame` | `LoadWz3MinMesh` (`wz4_mesh.cpp:7393`), XSI import, deserialisation | no — see below |
| `Wz4ChannelSpline` | deserialisation, `chaosmesh_ops.ops` | no |
| `Wz4ChannelLinear` | **nothing, anywhere in the dump** | dead code |
| `Wz4ChannelCat` | **nothing, anywhere in the dump** | dead code |

The three real sources of time-varying channels are all imports or deserialisation:

- **XSI import** — stubbed (patch 11); it constructs materials and textures across 2,142 lines.
- **`LoadWz3MinMesh`**, reachable from the `Import` operator — but **there are no `.wz3`, `.xsi` or
  `.lwo` assets anywhere in the tree**. Searched: zero.
- **Deserialisation** — real and working, but it needs a saved skeleton to load, which requires
  one of the other two to have made it first.

Measured end to end: `Cube → Deform → BakeAnim` at `Time = 0` and `Time = 1` produce **the same
checksum**, `512429fc92000000`, identical to `Deform` alone. Skinning is applied and the bake is an
identity, because the pose does not vary.

### What this means for the phase

**The 7.2 gate as written — "different vertex positions at different times" — is not reachable with
the operators that exist.** That is not a porting problem to solve; it is an absence of content.
Three ways forward, and this is a scope decision rather than a technical one:

| Option | | |
|---|---|---|
| **A** | Scope the phase to what the port can honestly do: skeletons, skinning, `BakeAnim`, the rest-pose identity check, skeleton visualisation and a scrubber that drives a *constant* pose. Honest, small, and leaves the phase gate unmet as originally written. | port only |
| **B** | Write a small **animation channel operator** — a `Wz4Skeleton` filter that installs a time-varying channel (linear or spline) on selected joints, plus an operator to bind a skeleton to a mesh. This is **new functionality**, not a port, but it is small and it makes every other piece testable. | new work |
| **C** | Unstub XSI import. Large — it needs the materials and texture stack, which CLAUDE.md places out of scope — and still needs an asset nobody has. | not viable |

**Recommendation: B**, and it is worth being clear that it is a change of kind. Everything so far
has been "make the existing thing run"; this would be "add the operator Werkkzeug4 never had,
because its animation came from a modelling package". The alternative is a phase that can prove
skinning is wired correctly but can never show anything move.

`Wz4ChannelLinear` already exists, fully implemented, and **has never been constructed by any code
in the dump** — so option B is largely a matter of exposing code that is already there.

## Risks — revised

| Risk | Assessment |
|---|---|
| ~~Skeletal animation is entangled with the script/sequencer layer~~ | **Dissolved.** Zero script references; time is a function argument |
| **No source of time-varying animation in scope** | **The real risk, and it is certain rather than probable.** No assets, XSI stubbed, and every procedural channel is constant. Decides whether the phase is a port or a small feature |
| No usable skinned test assets | **Dissolved.** `Deform` builds a skeleton with weights procedurally |
| Matrix convention errors producing plausible but wrong deformation | Low, and the rest-pose identity check is exact. Still worth having first |
