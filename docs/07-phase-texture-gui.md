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
that is the whole point of emitting metadata, and it is what makes 34 texture operators (and
later 47 mesh operators) affordable.

Stack: **Dear ImGui + GLFW + OpenGL 3.3 core**, both vendored and pinned — ImGui v1.92.9b,
GLFW 3.5.1, with archive checksums in `wz4port/third_party/VENDORED.md`. GL 3.3 core is
available on macOS (4.1 is the ceiling, which is ample) and everywhere on Linux. No
Metal-specific work.

**No docking**, contrary to this plan's original wording. Docking is still not in a tagged
ImGui release — it lives on the long-running `docking` branch, and
`ImGuiConfigFlags_DockingEnable` does not exist in v1.92.9b. Pinning a release matters more
here than dockable panes, for the same reason sse2neon is pinned: a moving dependency makes a
byte-exact suite meaningless. The editor lays its own panes out, which for a tool with a known
pane arrangement is no loss.

---

## Stages

Each stage is separately demonstrable.

### 5.1 — Application shell — **done**

ImGui + GLFW + GL3 vendored into `wz4port/third_party/`. Window, pane layout, menu bar, and
document loading wired to the phase 3 reader.

**Gate — passed.** `wz4ed` opens a 1280×800 window with a GL 3.3 core context, loads a `.wz4t`
given on the command line, and lists its operators with their geometry. The `In` column shows
the derived input count per operator, so the phase-2 connection derivation is visible in the
editor from the first stage — `atlas` reads 3, `glowrect_ellipse` 1, the generators 0.

An inspector pane shows the selected operator's metadata: class, parameter word and string
counts, array shape, and every parameter with its kind and offset. It agrees with
`wz4gen describe` on `GenBitmap.Bricks` down to the two `Flags` rows that share word 10 — which
is the check that matters, because stages 5.4 and 5.5 are built entirely on that path.

**The editor uses wz4lib as a headless library and brings its own window.** None of Altona's
GUI is compiled. This is the stage that cashes in phases 2 and 3: the document model, the
metadata and the text format were all made usable without a GUI, so the only thing left to
write here was a front end.

#### A GUI needs a way to be looked at, or "it opens" is unverifiable

`wz4ed -frames <n> -shot <file.png>` renders n frames, writes the framebuffer as a PNG, and
exits. `-select <name>` picks an operator at startup so a non-interactive run can exercise the
inspector too — without it, the only pane a screenshot could show is the list, and the metadata
half would go unverified.

That one flag proves a lot at once: GLFW opened a window, a GL 3.3 core context came up, ImGui
built its font atlas and produced draw data, the GL3 backend executed it, the document loaded,
and the panels rendered. `wz4ed_shell` is the ctest, and it needs a graphical session —
configure with `-DWZ4_GUI_TESTS=OFF` for ssh or headless CI.

Not golden-locked: the same command produces 1280×800 or 2560×1600 depending on which display
the window opens on, and font rasterisation is platform-specific. Same reasoning as the `Text`
cases in 4.5.

#### Two collisions between Altona and a modern C++ library

Both were found by building, not by reading, and both are recorded in `architecture.md`:

- **Altona macro-defines `new`** (`base/types.hpp:1763`), which mangles ImGui's placement-new
  declaration into four parse errors inside `imgui.h` that say nothing about the cause. Fixed
  with `push_macro`/`pop_macro` in `editor/imgui_wz4.hpp` — include order alone would work for
  one translation unit but is unenforceable across a growing editor. A46.
- **Altona replaces the global `operator new`/`delete`**, which interposes for every dylib in
  the process, and unregisters its memory handlers before static destructors run. The GUI
  frameworks — the first dylibs this port has linked that own C++ objects — then free through
  the interposed `delete` and Altona correctly reports pointers it does not own, printing four
  `FATAL ERROR` lines on every clean exit. The editor now ends the process at the bottom of
  `sMain` rather than unwinding through it. A47.

`-fshort-wchar` is deliberately **not** applied to ImGui: it is compiled against the system
headers and its `wchar_t` must match the platform's, not Altona's. Every string crossing
between the two goes through an explicit converter (`wUtf8`, `wWide`) rather than being
assigned across — the phase 3 encoding bug is close enough in memory to be worth the ceremony.

#### Not in this stage

File open/save **dialogs**. The document is given on the command line and `File > Reload`
re-reads it, which is enough to work with and to test. A file dialog means either a platform
picker or an in-ImGui browser; it is UI work with no bearing on the model, so it waits until
the canvas can create and modify documents worth saving (5.2 onwards).

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
