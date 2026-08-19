# Phase 5 — Texture GUI

**Goal:** a working cross-platform texture editor. This completes **priority 1**.

Reproduces the Werkkzeug stacking canvas faithfully, generates the entire parameter panel and
operator palette from metadata, and adds the document-level undo the original never had.

---

## Approach

**A custom canvas, not a node-editor library.** The Werkkzeug model has no stored links — an
operator's inputs are the blocks touching it from above, ordered by horizontal position. A
node-editor library's data model is links-first, so using one would mean synthesising links on
load and reconstructing a legal grid layout on save. Lossy, fiddly, and more work than drawing
rectangles.

The canvas is ImGui `DrawList` calls over a 24 × 16 px cell grid, with `ConnectStack`
reimplemented exactly as documented in `01-existing-model.md` §2.2.

**The panel and palette are generated from the phase 2 metadata.** No per-operator UI code —
that is the whole point of emitting metadata, and it is what makes 37 texture operators (and
later 47 mesh operators) affordable.

Stack: **Dear ImGui + GLFW + OpenGL 3.3 core**, all vendored. GL 3.3 core is available on
macOS (4.1 is the ceiling, which is ample) and everywhere on Linux. No Metal-specific work.

---

## Stages

Each stage is separately demonstrable.

### 5.1 — Application shell

ImGui + GLFW + GL3 vendored into `wz4port/third_party/`, building on both platforms. Window,
docking layout, menu bar, file open/save wired to the phase 3 readers and writers.

**Gate:** the application opens, loads a `.wz4t` file, and lists its operators in a panel.

### 5.2 — Grid canvas

Draw blocks as rectangles filled with their output type's colour. Pan and zoom the page.
Selection: click, shift-click to add, ctrl-click to toggle, rubber-band on empty space.
Move, resize and duplicate by drag, with the projected destination framed while dragging.

Collision via `CheckDest`/`CheckMove` semantics: **all-or-nothing, nothing is displaced**.

Block decoration per `01-existing-model.md` §2.6: inverted bevel when selected, red when in
error, the three status dots, `Hide` drawn with a red X, `Bypass` with a vertical red bar,
comments painted last and click-through.

**Gate:** a document's layout renders recognisably; blocks can be moved and resized within the
original's collision rules.

### 5.3 — Connection derivation

Implement `ConnectStack` exactly: exact bottom-to-top edge adjacency, horizontal span overlap,
left-to-right input ordering, then the `Hide`, sort and `Bypass` passes.

Draw derived connections as subtle guides — the original draws none, but a faint indication of
which blocks feed which helps while learning the model. Off by default; a view toggle.

**Gate:** connections derived in the editor match those derived by the headless library for
the same document, verified by comparing against `wz4gen list`. Moving a block one cell breaks
the connection exactly as the rule predicts.

### 5.4 — Operator palette

Built from metadata: the type tabs, then classes grouped into columns 0–30 under their
type's column headers (`generator`, `filter`, `merge`, `mix types`, `any type`), with keyboard
shortcuts. Respects the `hide` flag.

Insertion follows the original: place at the cursor if `CheckDest` allows, apply defaults,
**advance the cursor down one row** so repeated insertion builds a stack.

**Gate:** every registered operator is reachable from the palette and can be inserted.

### 5.5 — Parameter panel

Generated entirely from metadata. Every widget kind in `01-existing-model.md` §5.2:

- Drag-spinners for `float`/`int` with min/max/step, right-drag step, double-click reset, and
  logarithmic mode.
- Tied vector components (`float2`/`float30`/`float31`/`float4`) dragging together.
- `flags` — one integer split across several dropdowns/toggles by shift and mask.
- `radio`, `bitmask`, `color` with per-channel fields and a picker, `strobe`, `action`
  buttons, strings, `filein`/`fileout` with browse.
- Parameter arrays as a table with insert/remove, and **new rows interpolated between their
  neighbours** — the behaviour that makes gradient editing feel right.
- Conditional visibility, evaluated from the expression trees in the metadata.

Change propagation follows the original's four-level contract: value change, value change plus
relayout, connection change, and both.

**Gate:** every parameter of every texture operator is editable, and edits reach the
generator. Spot-check against the `.ops` sources.

### 5.6 — Preview

A 2D preview pane: pan, zoom with **8 = 1:1** as in the original, 3×3 tiling toggle, alpha
view toggle, and the `W x H` readout. Upload the `GenBitmap` result as a texture.

Recalculation follows the original's caching: editing an operator invalidates it and
everything downstream, and evaluation stops at any still-valid cache.

**Gate:** editing a parameter updates the preview promptly on a non-trivial graph.

### 5.7 — Document-level undo

The original has only per-operator, single-level, panel-scoped undo — no undo for insert,
delete, move, resize or paste. We fix this.

Approach: command-pattern undo stack over document mutations. Every canvas and panel edit
becomes an undoable command. Coalesce drag operations into one entry, and parameter
drag-edits into one entry per gesture.

**Gate — phase gate.** Insert, move, resize, delete, paste and parameter edits all undo and
redo correctly. Full editing session on a real document without losing work.

---

## Deliverables

- `wz4port/editor/` — application, canvas, panel, palette, preview, undo
- `wz4port/third_party/` — ImGui, GLFW
- A texture editor running on macOS arm64 and Linux x86-64

## Deliberately not included

Tree pages, the store browser, custom editors, the wiki, presets, autosave and backups,
handles/gizmos, and the embedded scripting language. All are documented in
`01-existing-model.md` and can be added later; none are needed for texture work, and the
texture operators declare no handles.

## Risks

| Risk | Assessment |
|---|---|
| Metadata insufficient to build some widget faithfully | Moderate. Surfaces here but the fix is in phase 2's emitter. Mitigated by fixing the schema against the complete widget inventory rather than incrementally |
| Grid canvas interaction feels worse than the original | Moderate and subjective. Mitigated by following the documented rules exactly rather than improvising, and by reviewing feel at stage 5.3 |
| Conditional-visibility evaluation diverges from the compiled original | Low-moderate. Small grammar; worth a targeted test comparing panel shape against the `.ops` conditions |
| Preview recalculation too slow to feel interactive | Low. The caching model is designed for exactly this, and `passinput`/`passoutput` in-place mutation is preserved |
