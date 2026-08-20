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

### 5.2 — Grid canvas — **done**

Draw blocks as rectangles filled with their output type's colour. Pan and zoom the page.
Selection: click, shift-click to add, ctrl-click to toggle, rubber-band on empty space.
Move and resize by drag, with the projected destination framed while dragging.

Collision via `CheckDest`/`CheckMove` semantics: **all-or-nothing, nothing is displaced**.

Block decoration per `01-existing-model.md` §2.6: inverted bevel when selected, red when in
error, `Hide` drawn with a red X, `Bypass` with a vertical red bar, comments painted last and
click-through.

**Gate — passed**, in two halves, because a canvas has two kinds of claim and only one of them
is visible.

*It renders recognisably* — verified by screenshot on `ops_gen.wz4t` and `ops_merge.wz4t`. The
layout matches the documents cell for cell: the two-operator stacks touch, `atlas` spans its
three inputs at 9 cells wide, and the twelve `Merge` groups sit where the file puts them.

*Blocks move and resize within the original's rules* — verified by `canvas_rules`, 30 checks
with no window. **The canvas does not reimplement the collision rules**: it calls
`wPage::CheckDest` and `wPage::CheckMove` directly, and they read `wOp::Select`, so the editor
keeps no parallel selection that could drift from what the document format enforces. The test
therefore drives exactly the code a drag drives.

The checks that matter most are the ones about the model rather than the geometry:

- **A legal move rewires the graph.** Sliding a block clear of its consumer's horizontal span
  drops it as an input; sliding it back restores it. Moving a block up one row until its top
  edge meets another's bottom edge creates an input. That is the whole point of this canvas —
  a drag is a structural edit, not a cosmetic one.
- **All-or-nothing really is all-or-nothing.** A two-block selection is refused entirely when
  either member would collide or leave the page, and the same delta is then shown to be legal
  for the innocent member alone — which is what proves the refusal was about the other block.
- **A refused move displaces nothing.** Checked explicitly, because "nothing is nudged" is the
  property that makes geometry trustworthy as a graph.
- **`move=1` versus `move=0`.** With the flag set, a selected block may slide onto cells another
  selected block is vacating; without it, the same move is refused.

#### Two of my own assertions were wrong before the code was

Worth recording because of how the second one hid:

- A "the same move is refused with `move=0`" check moved both blocks four rows down into empty
  space, where the two modes agree. The assertion was **vacuous**, and it failed. Fixed by
  choosing a delta that actually lands one selected block on another.
- A check that moving a block *down* would break its adjacency ignored that its consumer sits
  directly below, so `CheckMove` refused the move — correctly. The assertion failed, but the
  three assertions *after* it still passed, because the test helper applied the move anyway and
  they were then measuring the wrong geometry. `ApplyMove` now re-checks and refuses to apply
  an illegal move, so that class of mistake cannot recur.

#### Deliberate departures

- **Fit-on-load.** Landing at 1:1 on the top-left of a 192 × 128 cell page shows a corner of a
  wide graph with no hint the rest exists. `View > Fit page` (Home) frames everything, and a
  document is fitted when it loads. Never magnifies past 1:1.
- **Rubber-band selects touched blocks, not enclosed ones.** A 3-wide block in a dense stack is
  hard to fully enclose without catching its neighbours.
- **Middle-drag pans, wheel zooms about the cursor.** The original uses scrollbars.
- **Connection guides** are a `View` toggle, off by default. The original draws none — geometry
  is meant to be self-evident — but a faint hint of what feeds what helps while learning it.
- **Duplicate-by-drag is not implemented.** The original folds it into the same handler as move
  and resize as `mode 2`. It needs operator cloning, which belongs with insert and paste in 5.4.
- **Two of the three status dots are absent**, deliberately: "shown in a viewport" and "open in
  the parameter panel" have nothing to report until 5.6 and 5.5 exist. The cache dot is drawn.
  Adding the other two now would mean inventing state to display.

### 5.3 — Connection derivation — **done**

~~Implement `ConnectStack` exactly~~ — **the editor implements none of it.** It calls
`wDocument::Connect()`, the same function every other tool in this port calls. That is the
whole reason phases 2 to 4 made the runtime headless: there was nothing left to reimplement,
and reimplementing it would only have created a second thing that could be wrong.

So the gate as originally written — "connections derived in the editor match those derived by
the headless library" — is true *by construction*. What this stage actually delivered is the
part that was missing: making that checkable, and making the derivation visible and editable.

**Gate — passed.**

#### The three post-passes were untested, and they are not cosmetic

`core_connect` covered adjacency and `canvas_rules` covered what a drag does to it, but Hide,
Sort and Bypass had no coverage at all, and each one changes which operator feeds which.
`connect_passes` is 27 checks over all three:

- **Hide** drops the block from its consumers' input lists while leaving it on the page, and a
  hidden block still derives its own inputs.
- **Bypass** splices the block out, passing its own `in0` through — and *removes the slot
  entirely* when the bypassed block has no inputs, which is a different outcome from passing
  nothing through.
- **Sort** is left-to-right by `PosX`, and it runs *after* Hide for a reason worth knowing: the
  Hide pass removes with `RemAt`, which does not preserve order. Hiding the **middle** of three
  inputs is the case that would expose it, so that is the case the test uses.
- **Bypass runs after Sort and never re-sorts**, so substitution could in principle leave a
  list out of order. It cannot, and the reason is geometric: a bypassed block's own input must
  overlap that block's horizontal span, while its sibling slots lie outside it, so the
  substitute always sorts into the slot it replaces. Asserted rather than assumed — I started
  to write a test claiming the opposite and had to work out why it was unwritable.
- **Comments never participate**, in either direction.

#### `wz4gen list` could not read `.wz4t`

The gate said "verified by comparing against `wz4gen list`", and it turned out `list` called
`Doc->Load` unconditionally — the *binary* reader. The tool could not read the text format this
project invented. Fixed by dispatching on the extension, as `convert` already did.

`list -inputs` now prints the derived graph per operator, in slot order, with each input's `x`
position and any `[hidden]` / `[bypass]` marker. On `ops_gen.wz4t` it reports
`atlas … <- Flat@x8, Flat@x11, Flat@x14`, which is exactly what the editor's inspector shows.
`connect_inputs` pins that line, because the *ordering* of those three is the part of the rule a
reimplementation is most likely to get wrong.

#### Hide and Bypass are now editable, and the inspector shows the derivation

Checkboxes in the inspector and `H` / `B` on the whole selection, guarded against firing while a
text field has focus. Both reconnect. The inspector lists the derived inputs beside them, so
toggling Bypass and watching `in0` change from the bypassed block to *its* input is a two-second
demonstration of the rule.

Edits are collected into one `Connect()` per frame rather than reconnecting at the point of
each edit.

#### Connection guides: the original draws none, and trying to draw them shows why

This is a **contact** model, so a connection has no length. The two blocks share an edge, and a
line from one centre to the other has both endpoints *on that edge* — invisible, hidden under
the borders. I implemented centre-to-centre wires first and they rendered nothing at all on
either a vertical stack or `atlas`'s three inputs. Drawing wires here is a category error, and
that is the real reason the original has none.

What is worth drawing is the **contact patch**: the span of the shared edge where the two blocks
overlap. That is precisely what makes the connection exist and precisely what a sideways drag
destroys, so highlighting it answers "why is this connected, and how much room is there before
it stops being" — which no wire could. `atlas` shows as three distinct segments along its top
edge, one per input.

Still a `View` toggle, off by default, `-guides` to start with it on.

### 5.4 — Operator palette — **done**

~~Built from metadata~~ — **built from the live class registry** (`Doc->Types`, `Doc->Classes`).
A palette's job is to offer what can be inserted, and only a *registered* class can be, so
driving it from the registry makes an unofferable entry impossible by construction. Driving it
from metadata would let the palette and the runtime disagree, which is the class of bug metadata
was introduced to prevent elsewhere. The registry also carries everything the layout needs —
`wClass::Column`, `Shortcut`, `TabType`, and `wType::ColumnHeaders`. The plan's actual point,
"no per-operator UI code", holds either way: there is none.

Insertion follows the original: place at the cursor if `CheckDest` allows, apply defaults,
**advance the cursor down one row** so repeated insertion builds a stack.

**Gate — passed.** The palette shows all 34 `GenBitmap` operators grouped under the type's own
column headers — `generator`, `filters`, `special`, `samplers`, read from the registry rather
than invented — with each class's shortcut key shown beside it, plus a filter box.

`palette_insert` is the gate proper: it walks the whole registry and **inserts every offerable
class for real**, then reconnects. 67 of 74 offered, 67 inserted and connected. Reachability is
three flag tests and could be eyeballed; insertability is the half that can break — a class
whose `SetDefaults` crashed would look fine in a list and fail on click.

It links `editor/edit_ops.cpp`, the same `wInsertOp` the palette calls. Insert and delete were
factored out of the GUI for exactly that reason: copying twenty lines into the test would have
tested the copy. Same reasoning as the canvas calling `wPage::CheckMove` rather than
reimplementing it.

The test also pins the two behaviours that make the palette usable rather than merely present:
**repeated insertion at an advancing cursor builds a connected chain** — which is the entire
reason the original advances the cursor by one row — and **an insert with no room is refused and
displaces nothing.**

#### The 7 exclusions are named, not counted

`ConvertSceneNode`, `MakeCubeTex`, `MakeTexture`, `MakeTexture2` and `MakeWz3Bitmap` are
conversions, which the editor inserts automatically to bridge a type mismatch; offering them by
hand invites graphs that cannot be reasoned about. `Dummy` and our own `UnknownOp` placeholder
carry `wCF_HIDE`, which `doc_core.hpp` documents as literally "hide in op palette".

The test prints each one with the flag that excluded it. An exclusion list that is only a number
cannot be audited, and "the palette is missing an operator" is the complaint this test exists to
answer.

#### `wType::Order` exists for tab ordering and no type sets it

`doc_core.hpp` documents it as "sorting order, set to 1..9 to assign type keyboard shortcuts
1..9" — exactly the field wanted. It is 0 for every type in both registered modules, so sorting
by it changed nothing, which is how I found out.

In registration order the bar opened on `AnyType`'s seventeen structural operators — `Call`,
`Dummy`, `EndLoop`, `InjectGlobals` — and scrolled `GenBitmap`'s thirty-four off the end, which
is backwards for a texture editor. `Order` is still honoured where set; the tie-break is
insertable-class count, descending. **That tie-break is a judgement, not upstream behaviour**,
and it is there because the alternative was leaving the useful tab hidden.

#### Also in this stage, and not in its brief

**Delete** (`Del`/`Backspace`, and `wDeleteSelection`). An editor that can only add is not usable
enough to test a palette with — you would restart to undo a mistake. §2.5 lists it as one of the
basic operations.

**Class shortcut keys.** One unmodified key inserts one operator at the cursor. `H` and `B` are
checked first and so are unavailable as class shortcuts; upstream resolves that collision through
a data-driven binding file (`werkkzeug4.wire.txt`) this port does not read. The palette's click
path reaches every operator regardless, so nothing is unreachable.

**Not** done: copy/paste, and duplicate-by-drag (still deferred from 5.2). Both need clipboard
serialisation of full blocks including geometry, which is a piece of work in its own right and
belongs with undo in 5.7.

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
