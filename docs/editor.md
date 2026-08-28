# `wz4ed` — the editor

A user guide. Everything here is about *using* the editor; why it is built the way it is lives in
`architecture.md` and the phase documents.

---

## Running it

```sh
wz4ed [<document.wz4t>] [-meta <dir>] [switches]
```

The editor needs **operator metadata** — the JSON that `opsmeta` generates from the `.ops` sources —
before it can do anything. Without it there is no palette, no inspector, and documents will not
load: `cannot load a document without metadata`. It looks in `meta/` relative to the working
directory unless `-meta` says otherwise, so the usual invocation is from the build directory:

```sh
cd wz4port/build
./wz4ed ../tests/geo/gen.wz4t -meta meta
```

**Switches go *after* the filename.** Altona's command-line parser treats the token following a
`-switch` as that switch's first parameter, so `wz4ed -meta meta doc.wz4t` reads the document name as
an argument to `-meta` and then has no document.

| Switch | Effect |
|---|---|
| `-meta <dir>` | where to find the operator metadata (default `meta`) |
| `-select <name>` | select an operator by its store name at startup |
| `-export <path>` | export the selected operator as glTF and exit — see below |
| `-shot <file.png>` | render two frames, write the last as a PNG, and exit |
| `-frames <n>` | render *n* frames and exit |
| `-time <pct>` | set the animation time, 0–100, mapping to *t* = 0…1 |
| `-wire`, `-bbox` | start with wireframe / the bounding box on |
| `-nobones` | start with the skeleton overlay **off** (it is on by default) |
| `-guides` | start with connection guides on |
| `-help`, `-h` | usage |

Runs that use `-shot`, `-frames` or `-export` are **non-interactive**: the window is created hidden
and the macOS menu bar is suppressed, so they do not steal keyboard focus from whatever you are
doing. Interactive runs are unaffected.

---

## The layout

Four panes, left to right: **Palette**, **Canvas**, **Preview** below it, and **Inspector**.

### Palette

Every operator that can be inserted, grouped into a tab per output type (`wz4 Mesh`, `wz3 Bitmap`,
…) and into columns within a tab. The count on each tab is the number *insertable* in that tab, which
is smaller than the number of registered classes.

Type in the **filter** box to narrow by name or label. A filter matching nothing says so rather than
showing an empty palette. **Click an operator to insert it** at the canvas cursor.

### Canvas — the graph

Operators are blocks on a grid, coloured by output type.

**Connections are never drawn or dragged. They come from the geometry**: an operator reads whatever
sits directly above it. This is the single most important thing to know about the editor — to
connect two operators, put one under the other. The default block width is 3 cells, so two stacks
placed 4 apart cannot touch. Turn on **View → Connection guides** to see what is currently
connecting to what.

| Input | Action |
|---|---|
| Left click | select, and move the cursor here — the cursor is where the next insert lands |
| Left drag on a block | move the selection |
| Left drag on empty space | rubber-band select |
| Shift + left click | add to the selection |
| Ctrl + left click | toggle one block |
| **Shift + right drag** | resize a block |
| Middle drag | pan |
| Wheel | zoom (0.3×–3×) |
| `Home` | fit the page |

While dragging, the destination the blocks would land on is framed, and an illegal drop is refused
rather than silently discarded.

**Keyboard** (only when no text field has focus):

| Key | Action |
|---|---|
| `H` | toggle Hide on every selected block |
| `B` | toggle Bypass on every selected block |
| `Delete` / `Backspace` | delete the selection |
| a letter | insert the operator bound to that shortcut, at the cursor |

`H` and `B` win over the single-letter insert shortcuts, so the two operators whose shortcut is `h`
or `b` are reachable only from the palette. Everything remains reachable.

### Preview

The pane routes on the **selected operator's output type**: a `Wz4Mesh` gets the 3D viewer, anything
else gets the bitmap preview.

**Bitmap preview:** `1:1`, `-`/`+` zoom, and a `tile` checkbox. Wheel zooms, left drag pans.

**Mesh viewer:** left drag orbits, wheel dollies, and the toolbar has `fit`, `wire`, `grid`, `bbox`.

Two controls appear **only when the mesh carries a skeleton**, which almost none do — every
generator produces an unrigged mesh, and `Deform` destroys the rig it builds unless its *keep bones*
flag is set:

- a **`bones`** checkbox — joints draw as red/green/blue axis crosses, with a line to the parent
  where one exists. On rigs this build can produce there are no parent links at all (see below), so
  you will see crosses and no bones. That is the data, not a fault.
- a **timeline**: `play`/`pause`, `loop`, and a `t = 0…1` slider. The mesh is re-skinned on the CPU
  every frame, normals included.

### Operator list

Off by default; turn it on with **View → Operator list**. It holds the **page selector** — the only
way to move between pages of a multi-page document — and a table of every operator on the current
page with the geometry that defines its connections, sorted the way `Connect()` reads them: down the
page, then left to right. That is the evaluation order, so the table reads in the order things
happen.

### Inspector

The selected operator's parameters, generated from the metadata: name, `Hide`/`Bypass`, its inputs,
and one row per parameter. Edits mark the document dirty and re-evaluate the graph.

### Edit and View menus

`Ctrl+Z` / `Ctrl+Shift+Z` (or `Ctrl+Y`) undo and redo, labelled with what they will undo — the menu
reads *Undo move*, *Redo delete*, and so on. `Ctrl+R` reloads the document from disk, `Ctrl+Q` quits.
View toggles the operator list and connection guides, and offers fit-to-page (`Home`) and a 1:1 view
reset.

Note there is **no Save and no Open**. The editor reads a `.wz4t` given on the command line and does
not write it back; changes live only in the session, and Reload discards them without asking.

---

## Exporting glTF

**File → Export glTF** writes the **currently selected operator** as a glTF 2.0 file.

### Conditions

The menu item is greyed out unless **both** hold:

- the selected operator's output type is **`Wz4Mesh`**. Textures are not exportable this way — use
  the `Export` operator or `wz4gen` for those.
- a **document is loaded from a file**, because the export path is derived from the document's
  location.

If the item is greyed out, select a mesh operator on the canvas first. Attempting the export
another way (the `-export` switch) reports `select an operator first` or
`<class> is not a mesh — glTF export is for Wz4Mesh` and exits non-zero.

### Where it writes

There is **no file dialog** — ImGui has none and one is out of scope. The file goes **beside the
document**, named after the operator:

- the operator's **name** if it has one (the `name` field in the inspector), otherwise its class
  name;
- extension `.glb`.

So `Cube` named `cube_wide` in `tests/geo/gen.wz4t` becomes `tests/geo/cube_wide.glb`. The status
line at the bottom of the window reports exactly what happened:

```
exported 24 vertices, 12 triangles to .../cube_wide.glb (3820 bytes)
```

**An existing file is overwritten without a prompt.** Rename the operator if you want both.

### What comes out

| Written | Notes |
|---|---|
| positions, normals | |
| tangents | as glTF's `VEC4`, xyz plus the handedness sign |
| two UV sets | `TEXCOORD_0` and `TEXCOORD_1` — OBJ carries only the first. **No V flip**: Wz4's v runs downward from the top of the image, the same as glTF, verified through `Select`'s bitmap sampling, `GenBitmap::CopyTo` and `sImage::SavePNG`, none of which flips a row |
| indices | quads are triangulated with the same fan the renderer uses |
| cluster structure | one glTF *primitive* per cluster; OBJ discards this entirely |
| one default material | grey, non-metallic, double-sided |

| **Not** written | Why |
|---|---|
| skins and animations | nothing in this build produces a rig with hierarchy or motion, so there is nothing to export. The scrubber's pose is a *view*, not exported data |
| materials and textures | the material system is out of scope for this port |
| vertex colours | they do not exist in this build (`WZ4MESH_LOWMEM`) |

**Unused vertices are kept, not removed**, and are reported when present. Degenerate faces are kept
too — both are legal in glTF, and silently repairing a mesh would make the export disagree with what
the viewer shows.

**Coordinates are converted.** Werkkzeug4 is left-handed and glTF is right-handed, so z is negated
and triangle winding is reversed on the way out. The exported bounds are therefore the *negated and
swapped* source bounds in z — a mesh spanning `0.5…1.5` exports as `-1.5…-0.5`. That is correct, not
a bug.

### From the command line instead

The editor's export is a convenience wrapper. `wz4gen` does the same thing headlessly, with a report
of counts and bounds, and chooses the container by extension:

```sh
wz4gen render doc.wz4t -op <storename> -meta meta -out mesh.gltf   # JSON + mesh.bin
wz4gen render doc.wz4t -op <storename> -meta meta -out mesh.glb    # single file
wz4gen render doc.wz4t -op <storename> -meta meta -out mesh.obj    # Wavefront OBJ
```

`.gltf` writes a **`.bin` sidecar beside it**, named after the `.gltf` and referenced by a relative
uri — the pair must be kept together and moved together. `.glb` is one self-contained file and is
the better choice for handing to someone else; the editor's menu writes `.glb` for that reason.

### Example files to look at

```sh
cmake --build wz4port/build --target gltf_samples
```

writes 18 `.glb` files to `wz4port/build/gltf-samples/` — one per case, covering the primitives,
FreeType glyph outlines (`g_text3d`), an extruded path, subdivision, dual, facette, extrusion, and
two baked poses of the same rig (`an_baked_t0` / `an_baked_t1`) so the animation path has something
to show.

**Start with `uv_cube.glb` if you are checking the export is right.** It is the one
sample built to be *read* rather than measured: the word "Up" on a dark ground,
low and left, wrapped round a cube whose UVs run 0..4 — one full tile per face, four
times around, with no seam. Every automated check in the suite verifies structure;
none can see which way up the image landed, because that is a property of what a
viewer draws. Noise textures like `mm_moved.glb`'s prove a texture arrived and
nothing about how.

| what you see | what it means |
|---|---|
| "Up" upright, once per face, four times around | correct |
| upside down | V was flipped on export |
| mirrored | U was, or the handedness conversion is wrong |
| smeared across three faces | the sampler is CLAMP, not REPEAT |
| sideways | the UV set is transposed |
| one stretched tile | the 0..4 range was normalised somewhere it should not be |

**GLB, not `.gltf`, and deliberately so.** A `.gltf` is JSON plus a `.bin` sidecar that must travel
with it, which drag-and-drop and upload-based viewers will not accept. A `.glb` is one
self-contained file. The goldens under `wz4port/tests/geo/golden/gltf/` stay `.gltf` + `.bin` for the
opposite reason — a failure there should be a readable diff, not a byte offset.

Thirteen of the fifteen have **zero** triangles disagreeing with their stored normal. The two that
remain are `an_baked_t0` and `an_baked_t1`, and **the exporter is not the reason**: `BakeAnim` skins
the positions and leaves the **rest-pose normals** untouched, so the shading does not follow the
deformed surface. Measured — `an_rest_baked` has all 68 of its triangles disagreeing, and its
unbaked reference has none. The editor's 3D preview blends normals itself, which is why it looks
right there and wrong in an exported file.

`g_text3d` used to belong in that list, with 26 of 188 triangles inside-out and z-fighting across
every glyph counter. That was a real defect in the tessellation seam, found by looking at exactly
these files, and it is fixed — see `10-phase-gltf.md`.

### Verifying an export

There is no glTF *reader* in this tree, so a bad file will not be caught by re-importing it. Two
things you can do:

```sh
gltf_roundtrip <dir> -check mesh.glb            # structural validation, either container
gltf_roundtrip <dir> -check mesh.glb -convex    # ...plus the outward-winding check
npm i -g gltf-validator                         # then the goldens check conformance too
```

`gltf_roundtrip -check` validates buffer and accessor containment, index ranges, alignment, declared
bounds against the actual data, and the raw bytes (no BOM, no NUL, and for GLB a 4-aligned JSON chunk
padded with spaces). It also reports how many triangles disagree with their stored normal.

**`-convex` is opt-in for a reason.** The "every face winds outward from the centroid" check only
means anything on a closed *convex* mesh. A torus has ten inward-facing triangles by construction,
and a grid, a disc or an extruded path are not closed at all — applying it unconditionally made seven
of the fifteen samples look broken when nothing was wrong with any of them.

The official Khronos validator is optional and not installed by default; when present, the build
finds it and the glTF golden tests use it.

---

## Known limitations

- **No Save.** Documents are read-only in the editor.
- **No file dialog** anywhere, and no File → Open: a document comes from the command line, and
  exports go to a derived path.
- **The page selector is in the Operator list window**, not the canvas, so on a multi-page document
  it is not obvious how to reach page two. `-select <name>` finds an operator on any page and
  switches to it.
- **Rigs have no hierarchy.** `Deform` builds a flat list of joints and never sets a parent — in
  Werkkzeug4 hierarchy, like animation, only ever arrived with an imported asset, and this port has
  no importer for those formats. The skeleton overlay draws what is actually there rather than
  inferring a chain from the joints' positions.
- **`Dual` discards UVs** — it rebuilds the mesh from face centres and leaves every vertex at
  (0,0). Every other topology and transform operator checked preserves them. There is also no
  operator that *creates* UVs, so a mesh that loses them cannot get them back.
- **The second UV set is always empty.** `TEXCOORD_1` is exported because `Wz4MeshVertex` carries
  `U1`/`V1`, but no generator writes it.
