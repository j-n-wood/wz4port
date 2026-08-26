# Phase 8 — glTF 2.0 export

## Why

Phases 1–7 met all three goals in `00-overview.md`: textures, geometry, and animated geometry run
headlessly on macOS arm64 with an ImGui editor. What is missing is a way to get the results **out**
in a format anything modern will open.

The only exporter today is upstream's `SaveOBJ` (`wz4_mesh_obj.cpp:187`), and it drops most of what
`Wz4MeshVertex` carries: the second UV set, tangents, cluster grouping, and any rig. It writes five
decimals, so it cannot see a change below `1e-5`. glTF 2.0 carries all of that natively and is what
every current viewer, engine and DCC tool reads.

**Scope: geometry only.** Positions, normals, tangents, both UV sets, indices, cluster structure.
Skinning and animation are deferred — phase 7 established that no operator in this build produces a
rig with either hierarchy or motion (A63), so there is nothing to export yet. The writer reserves
the shape for `skins` and `animations` so they slot in later without a redesign.

## The dependency decision

**fx/gltf and nlohmann/json were considered and are not used.** Four findings decided it.

1. **Both halves of the JSON problem already exist in this tree, in separate implementations.**
   `wz4port/wz4t/json.hpp` is a complete JSON *reader* (`wJsonDoc`), already linked into `wz4gen`,
   and it exports `wFormatFloat`/`wParseFloat` — which exist precisely because Altona's `%f` and
   `sScanner::ScanFloat` are not correctly rounded (A18, A34). `wz4port/tools/opsmeta/json.hpp` is a
   deterministic JSON *writer* (`wJsonWriter`), built for output that "has to diff cleanly".
2. **We only ever write.** fx/gltf's value is concentrated in parsing and validation — the side we
   do not need.
3. **No network access.** `curl` is not allowlisted, so both libraries would have to be vendored by
   hand. fx/gltf also throws unconditionally with no `JSON_NOEXCEPTION`-style escape, and Altona
   builds `-fno-exceptions`; it would need its own CMake target outside `altona_flags` plus an
   exception firewall at the seam.
4. **The strongest reason.** `tests/mesh_obj.cpp` is a real test rather than a self-consistent one
   *because `LoadOBJ` and `SaveOBJ` share no code*. Using the in-tree reader as the oracle for the
   in-tree writer reproduces exactly that relationship. Vendoring one library for both directions
   would forfeit it.

## 8.1 — Promote the JSON writer, then the glTF writer — **done**

The move was verified the way the plan specified: all **34 metadata files deleted and regenerated
from scratch**, and the checksum over the set is unchanged at `e87685cd…`. Byte-identical, so the
relocation changed nothing.

`wz4geochk` now links `wz4t` for `json_write.hpp`. That is a heavier dependency than a JSON writer
warrants, and the alternative was worse: compiling `json_write.cpp` into both targets gives a
duplicate definition in `wz4gen`, which links both.

**Move** `tools/opsmeta/json.{hpp,cpp}` → `wz4t/json_write.{hpp,cpp}`, beside the reader. `opsmeta`
compiles that file by path and does **not** link `wz4t` (`CMakeLists.txt:160`), so this is a pure
relocation. **The meta JSON output must be byte-identical afterwards** — that is the check that the
move changed nothing.

**New `geo/gltf_write.hpp` / `.cpp`** in `wz4geochk` — ours, kept out of `wz4geo` for the isolation
reason that target's comment already gives. Following the `wz4t_write.cpp:435` split: a pure
in-memory formatter plus a thin file wrapper, so the formatter is testable without a disk.

### The five conversions that must be got right

1. **Handedness.** Wz4 is left-handed Y-up; glTF is right-handed Y-up. Negate `z` on positions,
   normals and tangents, **reverse triangle winding**, and flip `BiSign` (the tangent's `w`). Baked
   into the data, not expressed as a negative-scale root node, which would break normals.
2. **Quads.** There is no `Wz4Mesh::Triangulate` — quad splitting exists only inline in
   `ChargeSolid` (`wz4_mesh.cpp:4716`) as a fan `(0, i-1, i)`. Reproduce that fan so winding and
   normals agree with what the renderer shows, then reverse it for (1).
3. **Matrices.** `sMatrix34` is **row-vector** (`v * M`), `i/j/k` basis rows, `l` translation; glTF
   is column-vector, column-major. Transposing a row-major matrix and storing it column-major are
   the same operation, so the code looks like a no-op — say so, or a reviewer will "fix" it.
   *(Reached only once skins land.)*
4. **`min`/`max` on POSITION is required by the spec** and must describe the **exported** data — not
   `CalcBBox`, which includes unused vertices and pre-conversion coordinates.
5. **Vertex colours do not exist.** `WZ4MESH_LOWMEM` is unconditionally `1` (`wz4_mesh.hpp:11`), so
   `Color0`/`Color1` are compiled out. No `COLOR_0`. Both UV sets and `TANGENT` *are* present, and
   `Tangent`+`BiSign` is already glTF's `VEC4` layout exactly.

### Structure

One buffer; one shared set of vertex accessors; **one primitive per cluster**, each with its own
index accessor, preserving the structure OBJ discards. One scene, one node, one default PBR material
— the material system is out of scope and `Wz4Mtrl` is only forward-declared headless.

**The mesh is not mutated.** `MergeVertices()` would drop unused vertices, but `ReportMesh` receives
an object borrowed from the document cache, and mutating that broke the next frame once already
(A58). Unused vertices are exported and reported, not silently removed.

`.glb` is a 12-byte header + JSON chunk padded with spaces + BIN chunk padded with zeros, all to 4
bytes; `buffers[0]` carries no `uri`. glTF is little-endian — assert it, do not assume it. The
`asset.generator` string is fixed, with no version or timestamp, so goldens do not drift.

## 8.2 — CLI — **done**

```
cube_wide: Wz4Mesh.Transform
  24 vertices, 6 faces (0 tri, 6 quad), 1 clusters
  min -2 -0.5 -0.5
  max 2 0.5 0.5
  checksum e340d418a0000000
  gltf 24 vertices, 12 triangles, 1 primitive(s)
  wrote gltf/cube_wide.gltf (2333 bytes)
```

Unused vertices and degenerate faces are reported when present, because both are kept rather than
silently repaired.

## 8.2 (original plan text) — CLI

`ReportMesh` (`tools/wz4gen/main.cpp:983`) already dispatches on the output extension and rejects
anything but `.obj`. Add `.gltf`/`.glb` branches there rather than a new subcommand — the whole
golden harness then comes along for free. Keep the existing habits: make the output directory, and
**read the file back after writing**, because a writer's success return is not evidence (A39).

## 8.3 — Tests — **done**

160/160 ctest: `gltf_roundtrip` plus six goldens. Four things came out of building it.

### A symmetric mesh cannot detect a missing mirror

The conversion is a z-mirror plus a winding reversal. A cube is symmetric in z, so **omitting the
conversion entirely** — no negation, no reversal — passes every check on a cube while producing a
file that opens mirrored in every viewer. The two halves cancel in the checks and not in the result.

So the geometry case translates the cube **+1 in z** first and requires the exported range to be the
source's negated and swapped:

```
source z range 0.50000..1.50000
exported z range -1.5000..-0.50000
```

That assertion is vacuous on a symmetric mesh and decisive on this one.

### Both handedness checks are load-bearing — measured, not assumed

The gate was verified by breaking the writer twice:

| deliberate bug | inward | disagreeing with NORMAL |
|---|---|---|
| winding not reversed | **12 of 12** | 12 |
| positions mirrored, normals not | 0 | **4 of 12** |

The second is the interesting row. The winding check alone reports a clean mesh; only the
normal-agreement check sees it, and only on the four faces whose normals have a z component. Either
check alone would have shipped one of these bugs.

### The buffer half of the golden catches what the JSON cannot

Verified by corrupting four bytes of `p_dual.bin` and leaving the JSON alone: the JSON comparison
passes and the buffer comparison fails with *"the structure is unchanged and the coordinates are
not"*. A JSON-only golden would pass a writer that emitted the right accessors over the wrong
vertices. This is A43 inverted twice over — there a PNG golden was blind to the low 8 bits and
needed a checksum beside it; here the JSON is blind to every coordinate and needs the buffer beside
it.

### The negative cases

Three, because a checker that accepted everything would pass every positive assertion above: an
index one past the end (the exact off-by-one a 1-based writer makes), an accessor overrunning its
bufferView, and a buffer whose declared length disagrees with the file. Each is a minimal valid glTF
differing in exactly one way, and the valid one is checked to be **accepted** first — otherwise the
rejections prove only that the checker dislikes hand-written files.

## 8.3 (original plan text) — Tests

**`tests/gltf_roundtrip.cpp`** — the oracle, parsing our output with `wJsonDoc`, which shares no code
with the writer:

- every accessor's `byteOffset + count*stride` fits its `bufferView`; every `bufferView` fits the
  buffer; `buffer.byteLength` equals the actual `.bin` size; alignment rules hold
- every index is `< POSITION` count
- POSITION `min`/`max` equal the values recomputed from the `.bin`
- **the handedness gate**: rebuild each triangle from the buffer, take its geometric normal from the
  winding, and require it to point *away* from the centroid on a closed convex mesh, and to agree
  with the exported `NORMAL`. This is the one check that decides whether the Z-negation and the
  winding reversal compose correctly. A symmetric mesh will not reveal it — use `cube_wide` too.
- **a negative case**, mirroring `mesh_obj.cpp`'s broken-OBJ test: a hand-written `.gltf` with an
  index one past the end must be **rejected**. Without it none of the positive assertions mean
  anything, because a checker that accepted everything would pass them all.

**Goldens** — `tests/geo/golden/gltf/`, with `golden_gltf.cmake` and `lock_gltf_goldens.cmake` copied
from the OBJ pair. Same rules: a missing golden is a hard error, never an auto-create; re-locking is
a separate hand-run target, never run by ctest. `.gltf` compared as text so a failure is diffable;
`.bin` byte-exact.

**Optional Khronos validator.** CMake probes for a `gltf-validator` binary; present, it checks every
export for spec conformance; absent, the test skips. Not installed here and there is no network, so
it stays dormant until `npm i -g gltf-validator` — but it is the only check that verifies
conformance rather than self-consistency.

## 8.4 — Editor

`wz4ed`'s File menu holds only Reload and Quit (`editor/main.cpp:545`); there is no export of any
kind. Add **File → Export glTF** for the selected operator, reporting the path and byte count.
ImGui has no file dialog and one is not in scope.

Gate it with an `-export <path>` switch driving the same code path, for the reason `-wire`, `-time`
and `-nobones` all exist: a screenshot of a menu item is not evidence that the menu item works.

## Deferred, and stated so

`skins` and `animations`; per-cluster materials and textures; Draco or quantisation; `KHR_*`
extensions; `Wz4ChunkPhysics` (no glTF equivalent). The writer reserves the shape for the first two.

## Verification

- **8.1** — rebuild the metadata and require `git diff` on `meta/` to be **empty**.
- **8.2** — `wz4gen render tests/geo/gen.wz4t -op cube_wide -out out.gltf` reports the same counts
  and bounds as the OBJ path, and writes both files.
- **8.3** — `gltf_roundtrip` passes including the negative case; goldens match; validator passes or
  skips.
- **8.4** — `wz4ed tests/geo/gen.wz4t -select cube -export out.glb` writes a file the checker accepts.
- **Look at the output** before locking any golden. The goldens record what the writer does, not that
  it is right; 6.3b's OBJ review is the precedent.

Upstream footprint: **zero**. Everything lands in `wz4port/`.
