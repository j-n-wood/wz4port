# The existing model — how Werkkzeug4 works

Reverse-engineered from source. Every rule here is cited; where a rule is subtle the code is
quoted. Paths are relative to the repository root.

Principal sources:

| File | LOC | Role |
|---|---:|---|
| `altona_wz4/altona/main/wz4lib/doc.hpp` / `doc.cpp` | 1.1k / 4,469 | Document model, ops, pages, connection, caching |
| `altona_wz4/altona/main/wz4lib/build.cpp` | 998 | Graph → command list, type checking, conversions |
| `altona_wz4/altona/main/wz4lib/gui.cpp` | 5,889 | Canvas, parameter panel, palette |
| `altona_wz4/altona/main/wz4lib/view.cpp` | 1,191 | Viewport, handles/gizmos |
| `altona_wz4/altona/main/wz4lib/script.cpp` | 4,355 | Embedded scripting language |
| `altona_wz4/altona/tools/wz4ops/` | 3,407 | The `.ops` code generator |
| `altona_wz4/altona/main/wz4lib/werkkzeug4.wire.txt` | 573 | Menus and keybindings, data-driven |

---

## 1. Shape of the system

Werkkzeug4 is a **node-graph content generator**. A document holds pages; a page holds
operators; each operator produces one refcounted object (`wObject`) from zero or more input
objects plus a flat block of parameters. Evaluating a graph means compiling it to a linear
command list and running it.

Three layers, and it is worth keeping them distinct:

1. **Operator definitions** — written in a purpose-built DSL in `.ops` files, compiled by the
   `wz4ops` tool into C++. This is where the algorithms and all editor metadata live.
2. **Document/runtime** — `wDocument`, `wPage`, `wOp`, `wClass`, `wType`, `wExecutive`.
   Structure, evaluation, caching.
3. **Editor** — canvas, parameter panel, viewport. Built on Altona's own widget toolkit.

The editor executable itself is 149 lines (`wz4/werkkzeug4/main.cpp`); it registers operator
modules and opens a window. Everything else is library.

---

## 2. The canvas — the part people remember

### 2.1 There are no wires

The famous Werkkzeug workspace is a grid of rectangular blocks stacked against each other.
**Connections are not stored anywhere.** The entire graph is recomputed from block geometry
after every structural edit, by `wDocument::Connect()` (`doc.cpp:2597`), which calls
`ConnectStack()` per page.

A block stores only its rectangle (`doc.hpp:613-631`):

```cpp
class wStackOp : public wOp {
  sInt PosX; sInt PosY; sInt SizeX; sInt SizeY; sBool Hide;
};
```

Units are **grid cells** on a fixed page of 192 × 128 cells (`doc.hpp:37-38`), rendered at
24 × 16 px per cell (`gui.cpp:4202-4203`). A new operator defaults to 3 × 1 cells.

### 2.2 The connection rule

`wDocument::ConnectStack()`, `doc.cpp:2828-2934`. This is the single most important algorithm
in the system.

```cpp
sSortUp(ops,&wStackOp::PosY);                                  // doc.cpp:2847

sFORALL(ops,op0) {
  sInt j = _i+1;
  while(j<max && ops[j]->PosY <  op0->PosY+op0->SizeY) j++;
  while(j<max && ops[j]->PosY == op0->PosY+op0->SizeY) {       // touching op0's bottom edge
    op1 = ops[j];
    if(op0->PosX < op1->PosX+op1->SizeX && op1->PosX < op0->PosX+op0->SizeX)   // x overlap
      op1->Inputs.AddTail(op0);
    j++;
  }
}
```

Stated plainly:

> **A is an input of B if and only if `A.PosY + A.SizeY == B.PosY` — A's bottom edge exactly
> meets B's top edge — and their horizontal spans overlap.**

Data flows **downward**. Then, `doc.cpp:2891-2904`:

> **B's input list is sorted left-to-right by `PosX`.** Horizontal position determines
> argument order: `in0` is the leftmost block above, `in1` the next, and so on.

Three consequences worth internalising:

- **Adjacency is exact.** A one-cell vertical gap severs the connection completely. There is
  no proximity search, no snapping tolerance. This is what makes the interaction feel like
  stacking physical blocks.
- **Width is semantic.** A 6-cell-wide operator sitting under two 3-cell-wide operators
  consumes both. Widening a consumer is the *only* way to give it more inputs — there are no
  ports to drag onto. (The width is not auto-sized to input count; `CmdAdd` hardcodes
  `width = 3` at `gui.cpp:5170`, with the multi-input heuristic commented out.)
- **Height routes.** `SizeY` matters only through the bottom edge, so a tall block carries a
  value down past intervening rows. This is why `Nop` and `Comment` carry
  `flags = verticalresize` and are the only operators resizable in Y.

Three post-passes finish the job:

- **Hide** (`doc.cpp:2871-2887`) — inputs whose `Hide` flag is set are dropped from consumer
  input lists. The block stays on the canvas, drawn with a red X.
- **Sort** (`doc.cpp:2891-2904`) — the left-to-right ordering above.
- **Bypass** (`doc.cpp:2908-2932`) — a block flagged `Bypass` is spliced out, passing its own
  `in0` through; with no inputs, the slot is removed entirely. Drawn as a vertical red bar.

Two kinds of operator never participate at all: those flagged `comment`, and those whose
`shellswitch` condition is false (`doc.cpp:2834-2845`) — the latter being a compile-time
variant mechanism for shipping several demos from one document.

### 2.3 Tree pages

A page is either a stack page or a tree page (`wPage::IsTree`, `doc.hpp:701`); the two never
mix, and this is asserted (`doc.cpp:2620`, `2625`). Tree pages use indentation instead of
geometry: `ConnectTree()` (`doc.cpp:2792-2826`) walks the flat list maintaining a
`parents[level]` stack, and each operator at level *L* becomes an input of the last operator
at level *L−1*. A classic outline view. The stack page is the interesting one and the one
that matters for this project.

### 2.4 Collision — blocks never push each other

```cpp
sBool wPage::CheckDest(wOp *op0,sInt x,sInt y,sInt w,sInt h,sBool move) {   // doc.cpp:1000-1025
  if(x<0 || x+w>=wPAGEXS) return 0;
  if(y<0 || y+h>=wPAGEYS) return 0;
  if(op0 && op0->Class->Flags & wCF_COMMENT) return 1;        // comments may overlap anything
  ...
  if(r0.IsInside(r1)) return 0;                              // any overlap -> reject
}
```

`CheckMove()` (`doc.cpp:1027-1045`) applies `CheckDest` to every selected block and requires
all to pass. **A move, resize or paste that would overlap anything is rejected wholesale** —
all-or-nothing for the whole selection. Nothing is displaced, nothing is nudged.

### 2.5 Editing operations

All in `class WinStack` (`gui.cpp:4141-5522`). Keybindings come from
`werkkzeug4.wire.txt`, a data-driven menu/binding script.

A **grid cursor** (`CursorX`, `CursorY`) marks where new operators land, drawn as an empty
3 × 1 frame. Clicking sets it; arrow keys move it and select whatever it lands inside.

**Insert** (`CmdAdd`, `gui.cpp:5165-5197`): places a 3 × 1 block at the cursor if
`CheckDest` allows, applies the class's `default` preset if one exists, reconnects, opens it
in the parameter panel, and **advances the cursor down one row** — so repeatedly pressing the
add key builds a stack top-to-bottom. If the cells are occupied the insert is silently
refused.

**Move / resize / duplicate** are one handler, `DragMove(dd,mode)` (`gui.cpp:4999-5068`),
with `mode` 0/1/2. Pixel deltas are rounded to cells; the projected destination is drawn as a
frame while dragging; on release `CheckMove` gates the whole operation, then
`Doc->Connect()` rebuilds the graph. Resize is *grab anywhere with Shift+RMB*, not an edge
handle. Vertical resize requires `wCF_VERTICALRESIZE`.

**Copy/paste** serialise full blocks including geometry. Paste rebases the clipboard set to
the cursor and **pre-validates every block, aborting entirely if any collides**
(`gui.cpp:5223-5293`).

**Selection**: click, shift-click to add, ctrl-click to toggle, rubber-band on empty space.
`Shift+T` flood-fills the connected component. `Select Unconnected` marks everything not
reachable from the store named `root`.

There is **no auto-arrange anywhere**. Layout is entirely manual, which is the point:
geometry *is* the graph.

### 2.6 Block appearance

Fill colour is the output type's colour, so the canvas is colour-coded by data type. On top
of that: selected blocks invert their bevel; errors turn red; unreachable blocks grey out
(optional); skipped `slow` operators grey out; a goto target blinks for 2 s. Three status dots
inside the block mean *shown in a viewport*, *open in the parameter panel*, and *has a cache*.
Obsolete classes get "obsolete" overprinted. Comments are painted last, expanded by 4 px,
tinted from an 8-entry palette, and are click-through.

---

## 3. Types, and automatic conversion

### 3.1 The type system

`wType` (`doc.hpp:268-299`) is a runtime singleton per declared type. Subtyping is nominal
with single inheritance, walking the `Parent` chain (`doc.cpp:867-878`):

> A value of type `O` may feed an input declared as type `T` iff `T` is `O` or an ancestor
> of `O`.

`AnyType` is the root. Types may be `virtual` (abstract bases such as `BitmapBase`,
`MeshBase`). Type flags (`doc.hpp:60-64`):

| Flag | Value | Meaning |
|---|---|---|
| `wTF_NOTAB` | 0x01 | No palette tab of its own; classes land under "misc." |
| `wTF_RENDER3D` | 0x02 | Viewport must run a real 3D pass for this type |
| `wTF_UNCACHE` | 0x04 | Objects are large and participate in LRU cache eviction |

The actual check runs on the flattened node graph after conversion insertion
(`build.cpp:449-474`):

```cpp
if(!node->Inputs[i]->OutType->IsType(op->Class->Inputs[sMin(i,max-1)].Type))
  Error(op,L"input has wrong type");
```

`sMin(i,max-1)` is how varargs work: everything past the declared input count is checked
against the *last* declared input type.

### 3.2 The user never wires type adapters

A class flagged `conversion` is registered into `Doc->Conversions`. When an input's type does
not satisfy the requirement, the builder **silently inserts a conversion operator**
(`build.cpp:559-596`). Selection is a linear scan — **first match in registration order wins,
single hop only, no transitive chaining** (`doc.cpp:1332-1353`). Conversion operators are
invisible synthetic `wOp`s hanging off the source operator, on no page, not serialised, and
garbage-collected after each build.

Two flavours, both in the texture ops:

- `operator GenBitmap MakeWz3Bitmap(BitmapBase) { flags = conversion|hide; }` — fully
  automatic, never shown.
- `operator Texture2D MakeTexture2(GenBitmap) { flags = conversion; tab = GenBitmap;
  parameter { flags Format("default|argb8888|a8|i8|DXT1|..."); } }` — automatic, but also
  placeable by hand when you want to choose the format.

The GUI uses a companion test, `IsTypeOrConversion()` (`doc.cpp:880-889`), to decide which
operators to offer as link targets.

### 3.3 Links and stores — the escape hatch from geometry

Geometric connection is strictly per-page. Crossing pages uses **named stores**: any operator
with a non-empty `Name` becomes a store (`doc.cpp:2658-2669`). Names must be C-style
identifiers; duplicates are an error on both operators; names beginning with `;` are ignored.

`Load` operators and `link` parameters resolve by name through `FindStore()`
(`doc.cpp:3079-3100`), which also supports `storename:extraction.path` for extracting a
sub-object.

Each declared input carries a *method* deciding whether it is filled geometrically or by name
(`doc.hpp:373-378`): always-input, always-link, `both` (a radio in the panel), `choose` (pick
a specific geometric input index), or `anim`. Resolution happens at build time
(`build.cpp:176-215`), with a per-input `Default` operator as fallback when nothing is
connected.

**In practice the texture operators use zero explicit links** — the texture graph is purely
positional. The mesh operators use two (`Material`, `Reference`). This matters: a texture tool
needs no link UI at all.

---

## 4. Operator definitions — the `.ops` DSL

### 4.1 Pipeline

`name.ops` → `wz4ops` → `name.cpp` + `name.hpp`, wired in as an MSBuild custom rule
(`altona/doc/altona.props:36-42`). Generated files are gitignored, so a fresh checkout has
none of them; the tool must run before anything compiles.

Each generated `.cpp` exposes `AddTypes_<name>_ops()` and `AddOps_<name>_ops()`, invoked via
the `sREGOPS` macro (`doc.hpp:51-56`) in two passes — types first, then operators.

Critically: **`.ops` files contain real algorithm code, not just declarations.** Roughly
3,600 lines of the texture and mesh implementations live inside `code { }` blocks in the
`.ops` files, and `type` blocks embed the viewport rendering for each type. They cannot be
bypassed in favour of the `.cpp` files alone.

### 4.2 A worked example

`wz4/wz4frlib/wz4_mesh_ops.ops:315-350`:

```
operator Wz4Mesh Cube()
{
  column = 0;
  shortcut = 'q';
  parameter
  {
    int Tesselate[3] (1..4096)=1;
    float30 Scale (-1024..1024) = 1;
  }
  code
  {
    out->MakeCube(para->Tesselate[0],para->Tesselate[1],para->Tesselate[2]);
    ... out->Transform(mat); out->CalcNormalAndTangents();
  }
}
```

`wz4ops` emits:

- **Header** — `struct Wz4MeshParaCube { sInt Tesselate[3]; sVector30 Scale; };`, laid out at
  fixed 32-bit word offsets with `_padN` fillers, because the editor stores parameters as a
  flat `sU32[]`.
- **Command** — `sBool Wz4MeshCmdCube(wExecutive*, wCommand*)`, which unpacks `para` and
  `in0..inN`, allocates `out` if needed, then pastes the `code { }` block verbatim with
  `#line` directives pointing back into the `.ops` file.
- **GUI** — `Wz4MeshGuiCube(wGridFrameHelper&, wOp*)`, building the parameter panel.
- **Defaults, script bindings, wiki text**, and optional handles/actions/drag handlers.
- **Registration** into `AddOps_wz4_mesh_ops()`, filling a `wClass` with name, output type,
  command pointer, GUI pointer, shortcut, parameter word count and flags.

### 4.3 Signature syntax

```
operator <OutputType> <Name> ["Label"] ( [*] [?|~] Type [= DefType DefOpName], ... )
```

- `*` — varargs; the last input repeats.
- `?` — optional input.
- `~` — weak input: changes do not propagate eagerly.
- `= Type OpName` — a default operator instantiated per operator instance, whose parameters
  are inlined into the panel when the input is unconnected.

### 4.4 Operator metadata

| Keyword | Effect |
|---|---|
| `shortcut = 'c';` | Palette accelerator (uppercase implies Shift) |
| `column = N;` | Palette column 0–30. Defaults are inferred from the signature: 0 inputs → 0, 1 input → 1, more → 2, and 3 if any input type differs from the output type — giving *generator / filter / merge / mix types* |
| `gridcolumns = N;` | Override the 14-column parameter grid |
| `tab = TypeName;` | Which type's palette tab this appears under |
| `new = CClass;` | C++ class to instantiate for the output |
| `extract = "prefix";` | Register as an extraction operator |
| `customed = ClassName;` | Full-window custom editor |
| `code { }` | Execution body. Locals: `para`, `in0..inN`, `out`, `cmd`, `exe` |
| `handles { }` | Viewport gizmos. Locals: `pi`, `op`, `para`, `helper` |
| `drag { }` | Raw drag handler |
| `actions { }` | Button handlers; returning non-zero relays out the panel |
| `helper { }` | Scratch memory surviving between `handles` calls, not serialised |
| `description { }` | Dynamic tooltip text |
| `parameter { }` | The panel definition (§5) |

### 4.5 Class flags

Verified against `doc.hpp:349-371`:

| DSL | Constant | Value | Meaning |
|---|---|---|---|
| (`*` on last input) | `wCF_VARARGS` | 0x000001 | Last input repeats |
| `load` | `wCF_LOAD` | 0x000004 | Load-style body; labelled with its link name |
| `store` | `wCF_STORE` | 0x000008 | Store-style body |
| `hide` | `wCF_HIDE` | 0x000040 | Not shown in the palette |
| `conversion` | `wCF_CONVERSION` | 0x000080 | Auto-insertable type converter |
| `logging` | `wCF_LOGGING` | 0x000100 | Opens the logging overlay while executing |
| `slow` | `wCF_SLOW` | 0x000200 | Graph is cut here unless explicitly requested |
| `blockhandles` | `wCF_BLOCKHANDLES` | 0x000400 | Stop handle recursion |
| `passinput` | `wCF_PASSINPUT` | 0x000800 | Steal input 0's object and mutate in place |
| `passoutput` | `wCF_PASSOUTPUT` | 0x001000 | Allow my output to be stolen |
| `curve` | `wCF_CURVE` | 0x002000 | Animation curve operator |
| `clip` | `wCF_CLIP` | 0x004000 | Animation clip operator |
| `obsolete` | `sCF_OBSOLETE` | 0x008000 | Overprinted "obsolete" |
| `verticalresize` | `wCF_VERTICALRESIZE` | 0x010000 | Resizable in Y |
| `comment` | `wCF_COMMENT` | 0x020000 | Annotation: never connects, overlaps freely |
| `call` | `wCF_CALL` | 0x040000 | Subroutine call |
| `input` | `wCF_INPUT` | 0x080000 | Subroutine argument injection |
| `loop` | `wCF_LOOP` | 0x100000 | Replicate the input N times |
| `endloop` | `wCF_ENDLOOP` | 0x200000 | Terminate loop scope (not implemented) |
| `shellswitch` | `wCF_SHELLSWITCH` | 0x400000 | Exists only if a command-line switch is set |
| `typefrominput` | `wCF_TYPEFROMINPUT` | 0x800000 | Declared `AnyType`; actual type is input 0's |
| `blockchange` | `wCF_BLOCKCHANGE` | 0x1000000 | Dam for change propagation |

Note the graph has **control flow**: `Call`/`Input` implement subroutines with proper scoping
via a call-id that participates in build memoisation, and `Loop` replicates a subtree N times.

---

## 5. The parameter panel

A 14-column grid, assembled per operator by the generated `MakeGui` function
(`WinPara::SetOp`, `gui.cpp:3136-3289`). Above the generated part sit a name field (which is
also the store name), an undo/redo button, a script toggle, and any inlined default-operator
GUIs.

### 5.1 Change propagation contract

Four message levels, and the distinction matters:

| Message | Meaning |
|---|---|
| `ChangeMsg` | Value changed → drop caches, mark document dirty |
| `LayoutMsg` | Value changed *and* the panel must be rebuilt (conditional parameters) |
| `ConnectMsg` | Link or name changed → `Doc->Connect()` first, then change |
| `ConnectLayoutMsg` | Both |

### 5.2 Widget kinds

| DSL | Control |
|---|---|
| `label` / `group` | Row label / full-width bold header |
| `float X (min..max step s[,rstep]) = d;` | Drag-spinner: LMB drag steps, RMB drag uses `rstep`, Home or double-click resets to default, Ctrl+drag moves tied siblings. `logstep` switches to exponential dragging. Text entry always available |
| `float2` / `float30` / `float31` / `float4` | Component spinners wrapped in a tied group, addressed `.x .y .z .w`. `float30` is a direction, `float31` a position |
| `float X[n]` / `int X[n]` | Indexed arrays of the above |
| `int X (min..max step s [hex n])` | Integer spinner, optional hex formatting |
| `bitmask X (n)` | Grid of clickable bits |
| `char X[n]` | Inline fixed-size string field |
| `color X ("rgba")` | One byte field per channel, r/g/b tinted and drag-linked, plus a picker button |
| `flags X ("a\|b:*4c\|d")` | **One integer split across several controls.** `:` separates independent widgets, `*N` shifts by N bits, `\|` separates choices; 2 choices render as a toggle, more as a dropdown |
| `continue flags X (...)` | Another widget on the *same* variable |
| `radio X ("a\|b\|c")` | Row of radio buttons |
| `strobe X ("go")` | One-shot trigger; `wOp::Strobe` is cleared after a successful execution |
| `action "Label" (id)` | Push button calling the class's `actions` handler |
| `string X;` | Single-line or multi-line text box, optional line numbers, optional read-only |
| `filein` / `fileout` | Text field committing on Enter/blur (not per keystroke) plus browse and reload buttons |
| `link X:n [(both\|choose\|anim)]` | Name field plus store browser, page popup, and goto |
| `custom X (Class) lines N` | Bespoke control |
| `padding;` | Reserve parameter words for future use |
| `tie a,b,c;` | Make separately-declared floats drag together under Ctrl |

Prefix modifiers: `nolabel`, `layout`, `continue`, `filter`, `lines N`, `overbox`,
`overlabel`, `anim`, `linenumber`, `narrow`, `static`.

**Animatable parameters are marked only by prefixing the label with `"* "`.** That asterisk is
the entire affordance.

### 5.3 Conditionals

Parameters may be wrapped in `if(expr) { ... }`, which compiles to a real `if` inside the
generated `MakeGui`, so panels genuinely change shape with values. The expression grammar
supports the usual operators, other parameter symbols, `input[n]` (tests whether an input is
connected), and `SomeFlags.choicename`, desugared to a mask/compare using that choice's bit
layout.

### 5.4 Parameter arrays

Declared as `array { ... }`, rendered as a table with a mode dropdown, a clear-all button, a
column-header row, per-row insert/remove buttons, and an optional index column. Long arrays
auto-collapse past a threshold.

When a row is inserted, the generated `SetDefaultsArray` sets each field's default and then
**linearly interpolates every float field between the neighbouring rows**. This is why
inserting a gradient key lands halfway between its neighbours rather than at a default.

### 5.5 Undo

**Per-operator, single-level, parameter panel only.** `wOpData` (`doc.hpp:635-652`) snapshots
one operator's words, strings, link names and array data; the button toggles between undo and
redo for the currently edited operator.

**There is no document-level undo at all** — no undo for insert, delete, move, resize or
paste. The safety nets are autosave (rotating nine `!N_autosave.wz4` files) and numbered
backups written before each save. This is a genuine gap, not a subtlety to preserve.

---

## 6. Evaluation and caching

### 6.1 Dirty propagation

`wDocument::Change()` / `ChangeR()` (`doc.cpp:2946-2990`):

> Editing an operator releases its cached object and, transitively, **every downstream
> operator's cached object**. Upstream is untouched.

`Outputs` is rebuilt on every `Connect()` from both geometric inputs and named links, so
name-based references propagate change correctly. `Connect()` also diffs each operator's new
input list against `OldInputs` and fires `Change` only where the topology actually moved —
that is how dragging one block avoids invalidating the entire document.

Two escape hatches:

- **`blockchange`** — a dam. Downstream stays stale until the user presses *Unblock Change*.
- **weak inputs (`~`)** — changes do not propagate eagerly; the producer is queued and
  recalculated on demand, with its last result kept in `WeakCache`.

### 6.2 Graph → command list

`wBuilder::Parse` walks from the root **upward through inputs**, producing a node DAG,
memoised per operator *per call-id* so shared subtrees evaluate once and subroutines can be
instantiated multiple times with different arguments.

`Optimize` (`build.cpp:514-613`) then:

1. **Removes no-ops** — any node with no command, one input and no script is spliced out.
   `Nop`, `Store`, `Load`, `Group` and `BlockHandles` all vanish here.
2. **Inserts conversions** (§3.2).
3. Counts outputs.
4. Decides cache points.

`Output` linearises the DAG in post-order, and `MakeCommand` **copies** parameter words,
strings and array data into pool memory — so the command list is a self-contained snapshot
and editing cannot corrupt a running calculation.

### 6.3 Caching

Each operator holds `Cache`, `CacheLRU`, `CacheVars`, `WeakCache` (`doc.hpp:570-573`).

**Load side** (`build.cpp:479-512`): a DFS from the root replaces any operator holding a valid
cache with a zero-input node and **stops recursing**. Everything above a cached operator is
never even compiled into commands. That is the whole incremental-recalculation mechanism.

**Store side**: after a command runs successfully its output is retained into the operator's
cache.

**Not cached**: `passoutput` operators with exactly one consumer that are not currently being
viewed or edited. `passoutput` means "my result may be stolen and mutated"; `passinput` means
"hand me input 0's object directly when its refcount is 1". That in-place chain is what makes
long bitmap filter stacks fast, and it is why the two flags appear together throughout the
texture operators.

**Eviction**: only types flagged `uncache` (`GenBitmap`, `Wz4Mesh`) participate. After every
command, while memory exceeds the configured limit, the least-recently-used such cache is
released — preferring ones with no other referent (`doc.cpp:3607-3647`).

### 6.4 When calculation happens

Only when a viewport needs a picture. Operators flagged `slow` cut the graph: the builder
re-roots at the first slow node's input 0 and greys out everything skipped, until the user
explicitly asks for a full calculation.

Two global commands: *Uncalc all Ops* drops every cache for a cold start; *Calc whole
timeline* walks the beat range rendering offscreen to fill every cache and warm up shaders.

---

## 7. Preview

Dispatch is on the **result object's type**, not on the operator (`view.cpp:283-304`):

- `wTF_RENDER3D` decides whether the window runs a real 3D pass.
- `gui = base2d | base3d | mode` (`doc.hpp:66-71`) selects which input binding set is live.
  **Those three are the complete enumeration.** The same window therefore gives you
  pan/zoom/tile/alpha for a bitmap and orbit/dolly/fly/wireframe for a mesh.

Rendering runs a three-phase pass: every type gets `BeginShow`, the object's own type gets
`Show`, every type gets `EndShow` (`doc.cpp:3402-3432`) — which is how multi-pass renderers
accumulate.

A bitmap operator's `Show` copies to an image, sets the destination rect from pan and
`Zoom2D` (**8 = 1:1**), blits, and prints a `W x H` readout. Optional 3×3 tiling and an alpha
view.

### 7.1 Handles

A `handles { }` block runs **every frame** and rebuilds its gizmo list from scratch; identity
across frames is `(operator, id)`. A handle registers a **raw pointer into the operator's
parameter data**, so dragging writes straight through and the parameter panel updates live.

Drag constraint comes from `wHandleMode` (`doc.hpp:74-81`): `wHM_STATIC` (display only),
`wHM_TEX2D` (normalised 2D texture coordinates), `wHM_RAY`, `wHM_PLANE` (XZ plane, with
modifier keys switching to a camera-facing plane or Y-only), `wHM_PLANE_XY`.

Handles from the **entire upstream subtree** draw simultaneously, colour-coded by relationship
to the selected operator, unless a `blockhandles` operator cuts the recursion. A handle may
carry an `arrayline` linking it back to a parameter-array row, so clicking a gizmo scrolls the
panel to its row and vice versa.

---

## 8. Document format

Altona's tagged binary serialiser (`sWriter`/`sReader`), current version 12
(`doc.cpp:3165-3313`). Little-endian; strings are UTF-16. Structure:

```
Header(Werkkzeug4Doc, 12)
  DocOptions                       (v12+ first, so options are readable without the doc)
  ArrayNew(pages)                  + RegisterPtr for every op
  for each page: Name, IsTree, ScrollX, ScrollY
                 stack ops: PosX|PosY|SizeX|SizeY, Bypass|Hide, <wOp>
                 tree ops:  TreeInfo.Level|Flags, <wOp>
  Includes, PageNames
```

Two-phase (pointers registered first, bodies later) so operators can reference each other.

`wOp::Serialize_` is notably robust: operators are identified by **class name plus output type
name**, not by index, so new operators can be added anywhere without breaking files; the
parameter block is written as a word count followed by raw words, with extra words skipped and
missing ones left at their defaults — giving forward and backward compatibility. Unknown
classes load as `UnknownOp` rather than being dropped, so nothing is silently lost.

---

## 9. Notes for a reimplementer

- **`Doc->Connect()` is the single source of truth** and runs after every structural edit. It
  is O(all operators): rebuilds the operator list and store table, clears and recomputes all
  inputs/outputs, resolves links, type-checks, diffs against the previous topology to fire
  change events precisely, and recomputes reachability from `root`.
- `Select`, `Temp`, `CycleCheck`, `BuilderNode` are transient scratch fields reused by several
  algorithms. Do not persist them.
- **Comments are excluded in five separate places** — connection, collision, hit-test
  priority, frame-select, and the minimap. Easy to miss one.
- Deleting an operator must first pull it out of every view and parameter panel or you get
  dangling references; the codebase leans on a mark-and-sweep GC for the rest.
- The whole window layout, menu structure and keybinding set is data-driven from
  `werkkzeug4.wire.txt`. That file is a good inventory of what the tool can actually do.
- Each operator can carry an **embedded script** whose declared sliders become extra panel
  widgets, and which can read *and write* parameters before the body runs. Script outputs flow
  downstream and are cached alongside the object. This is part of the animation system and is
  out of scope for us, but it explains several fields on `wOp`.
