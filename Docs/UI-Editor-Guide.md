# rbfx UI Editor User Guide

The rbfx editor ships a built-in visual UI editor (the UI tab) for authoring [RmlUi](https://mikke89.github.io/RmlUiDoc/) documents (`.rml`) together with stylesheets (`.rcss`). It targets a "flow/CSS layout first, absolute positioning on demand" workflow and writes **standard RML/RCSS indistinguishable from hand-written code** — no private formats, no private attributes.

> Design principle: **the source text is the single source of truth**. The canvas is a live projection of that text; saving applies a minimal-diff patch to only the hunks you actually changed, never a whole-file rewrite. You can move freely between the editor and hand-editing at any time; neither destroys the other.

---

## 1. Opening and Creating Documents

- **Open**: double-click any `.rml` in the Resource Browser.
- **Create**: focus a UI tab and click **New** in the toolbar; the save dialog picks the location under the project `Data` folder (a new document must live on disk to be a resource).
- Every open `.rml` owns its own editor tab ("UI", "UI (2)", ..., VS Code style). The first UI tab is a persistent entry point: it shows an empty-state hint when idle and survives its document closing; later instances close together with their document.
- Double-clicking an already-open document focuses its existing instance instead of opening a duplicate.

## 2. Interface Tour

| Area | Description |
|---|---|
| **Toolbar** (top strip) | New / Save / Reload (re-read from disk, discards unsaved edits), (unsaved) marker, Play/Stop preview, name of the edited file |
| **Canvas toolbar** (row above the canvas) | Wrap, Move Up/Down (flow order), Fit, 100%, zoom percentage, Canvas size picker |
| **Canvas** | Live rendered preview of the document; direct picking, dragging and editing |
| **Hierarchy** | Node tree of the document (drag to reparent, context menus) |
| **Inspector** | Property panel of the selected node (sections in chapter 6) |

## 3. Selection and Canvas Interaction

- **Click** selects an element; clicking empty space clears the selection.
- **Ctrl+click** adds/removes from a multi-selection; **drag with the left button** rubber-band selects (a tiny drag degrades to a click).
- **Double-click** a pure-text element (button, text) to edit its text **in place**: Enter commits, Esc reverts.
- **Double-click** a nested template node opens its source file.
- **Right-click** an element for the context menu: Copy / Delete (counts shown for multi-selection), Wrap in Row/Column/Box, Move Up/Down.
- **Middle-drag** pans; **wheel** zooms anchored on the pointer; `F` or the Fit button fits the window.
- **The canvas accepts drops**: drag an image file from the Resource Browser onto the canvas to create an `<img>` (initialized from the texture size); drag a Hierarchy row onto the canvas to move that node.

### Flow dragging (reorder / reparent)

Press and hold an **in-flow** element (not absolutely positioned) and drag: past a ~4 px threshold the drag goes live, with a green indicator line/highlight showing the drop target (insert before/after an element, or drop inside a container); release commits as one undoable step. A release under the threshold is a plain click-select. Esc or right-click cancels mid-drag.

### Absolutely positioned elements and the gizmo

Elements set to `position: absolute` (see Position in chapter 5) get a full gizmo on the canvas:

- **Center handle**: move. While moving, **alignment snapping** is active — the dragged box's left/center/right and top/middle/bottom lines snap to the offset parent's and siblings' corresponding edges and centers (within a 6 px on-screen threshold), with rose-colored guide lines marking the active alignment. The released position is the snapped position.
- **8 edge/corner handles**: resize (writes back width/height).
- **Handle above the top edge**: rotate.
- **Handle outside the lower-right corner**: uniform scale; the handle outside the right edge scales horizontally only.
- With a multi-selection all elements move together (snapping included) and commit as one undo step.
- Esc during a drag cancels and puts the element back.

> **In-flow** elements deliberately have **no** gizmo: their position comes from the container's layout properties — adjust the container's flex settings instead.

## 4. Adding Widgets

Right-click a container in the Hierarchy (or an element on the canvas) and choose **Add Widget**:

- The **filter box** at the top narrows the palette by name/group.
- The grouped list (data-driven, evolves over time):

| Group | Entries |
|---|---|
| Structure | `div`, `form` |
| Content | `img` (src may be empty), Text (`<p>`, joins the document flow) |
| Controls | `button`, `input` (text / checkbox / radio / range / submit), `select` (with two `<option>`s), `textarea`, `progress`, `label` |

- **Arbitrary tag**: the bottom of the palette accepts any tag name — the palette is a shortcut list of high-frequency entries, not a capability boundary; any valid RmlUi tag can be created, rendered, edited and saved.
- Convention: **`p` for text, `div` for layout, `label` for text that belongs with a control**.

Widgets may render transparent or as placeholders in a document without a linked stylesheet (the editor draws a placeholder outline for native controls); link an rcss in the head (chapter 7) for real looks.

## 5. Layout Editing (Inspector → Layout section)

With any element selected, the Inspector **Layout** section offers:

- **Position**: three modes
  - **In flow**: laid out by the container (default).
  - **Anchor** (`position: relative`): stays in flow but becomes the reference for absolutely positioned descendants.
  - **Free** (`position: absolute`): out of flow, draggable on the canvas against the nearest Anchor/Free ancestor.
  - Switching back to In flow removes only the positioning declarations and top/right/bottom/left — **authored sizes are kept**.
- **Z-order** (`z-index`): stacking order; empty means auto (declaration dropped). CSS rule applies: it only takes effect on positioned elements — set Anchor or Free first for HUD layering / modals on top.
- **Anchor 3x3 grid** (absolute elements only): pick which edges the box pins to and whether it stretches with its container; gizmo drags only write top/left, so re-pick a cell here to re-seat on another edge.
- **Top / Right / Bottom / Left**: offsets.
- **Layout mode**: **Row / Column** (writes `display:flex` + `flex-direction`) or plain box (left empty).
- **Gap**: spacing between children (writes both `row-gap` and `column-gap`).
- **Align (main axis)** (`justify-content`) and **Align (cross axis)** (`align-items`).
- **Wrap** (`flex-wrap`) and **Align lines** (`align-content`, enabled only when wrapping).
- **Align (self)** (`align-self`): the element's own alignment as a flex item.
- **Width / Height**: sizes (empty = auto, declaration dropped).

For a fixed-height vertical scrolling list, give the children `flex-shrink: 0` (via Inline Style raw) or they will be compressed.

## 6. Inspector Section Reference

| Section | Contents |
|---|---|
| **Content** | Edits the element's text content (pure-text elements; live preview) |
| **Attributes** | Element attributes (id, class, type, src, ...); resource attributes carry a browse button |
| **Appearance** | background-color / opacity / color / font-size / text-align (inherited rows carry an `*` marker); **Background image** edits a single-image decorator (`image(...)`, path or sprite name, with a resource picker). Mixed decorators (layered gradients etc.) render read-only with a hint to use raw, so nothing you hand-authored gets destroyed |
| **Layout** | See chapter 5 |
| **Inline Style (raw)** | The raw `style` declaration text — anything the structured rows don't cover is written here, with exactly the same authority as hand-written code |
| **Computed** | Read-only final computed values (debugging) |
| **Matched styles** | **Source tracing** for every effective property: inline / rcss rule (shows selector and file path; click jumps to the line in the text editor) / inherited / default. Use it to answer "where does this color come from" |

## 7. Stylesheets and Template Links (head nodes)

Every `<link>` in the document's `<head>` is a **first-class node** in the Hierarchy (🔗 icon):

- **text/rcss links**: select to edit the href or delete in the Inspector; the top row can add new ones (path input + Stylesheet/Template buttons).
- **text/template links**: a template instantiated by the body appears as a `#nested-doc` node (double-click opens the source file; edit href, delete link); uninstantiated template links are `#head-link` nodes like rcss links.
- All link edits are **text-level commands**: fully undoable, and the head region is preserved byte-for-byte on save.

Add links from the editor's head area. Edit the stylesheet itself in a TextEditor tab; saving it hot-refreshes the canvas (chapter 9).

## 8. Copy / Cut / Paste / Undo

- **Ctrl+C / Ctrl+X / Ctrl+V**: the real OS clipboard — it holds standalone RML fragments, so it bridges across documents and editors; pasting renames colliding ids with `-2`/`-3` suffixes.
- **Ctrl+D**: duplicate in place.
- **Delete**: delete (batch for multi-selection, one undo step).
- **Ctrl+Z / Ctrl+Y**: full undo/redo; batch operations are a single step.
- **Alt+Up / Alt+Down**: move up/down within the flow (swap with a sibling).
- Shortcuts belong to the canvas window: they never fire while a text field owns the keyboard.

## 9. Preview, Play and Hot Refresh

- **Play/Stop (toolbar)**: runs the currently edited scene plus this document in the Game View (saves first; aborts if an external-change decision is pending). The running session grabs input; stopping returns focus to the editing tab. A running Game View does **not** auto-reload on file changes.
- **Hot refresh (editing canvas)**: after a `.rcss` / `.rml` file is saved on disk by an external tool (including the editor's own TextEditor tab), **the editing canvas updates itself**:
  - A linked stylesheet/template changed → the canvas re-projects with the new styles; the model is untouched (no undo, no dirty).
  - The open document itself changed while clean → the canvas reloads in place.
  - With unsaved edits there is no auto-reload (the save guard owns the conflict), and a live drag defers the refresh until release.

## 10. Saving Behavior

- Ctrl+S / toolbar Save. Saving is **minimal-diff**: only the hunks that actually changed are rewritten; your indentation, comments and untouched regions survive verbatim; data-binding markers (`{{...}}`), templates and unrecognized standard constructs are passed through untouched — never rewritten, never lost.
- **Overwrite guard**: if the file changed on disk while it was open, saving raises a confirmation dialog (Overwrite / Cancel) — another tool's edits are never silently clobbered.
- The output is standard RML: the RmlUi engine, other tools and hand-written files interoperate seamlessly.

## 11. Keyboard Shortcut Reference

| Key | Action |
|---|---|
| `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Copy / cut / paste (OS clipboard) |
| `Ctrl+D` | Duplicate in place |
| `Delete` | Delete selection |
| `Alt+Up` / `Alt+Down` | Move up / down in flow |
| `F` | Fit view |
| `Esc` | Clear selection; cancel a live drag/marquee/pan gesture |
| Middle-drag | Pan |
| Wheel | Zoom (pointer-anchored) |
| Double-click | Edit text / open nested document |

## 12. Tips and Known Behavior

- Link an rcss in the head of a new document (e.g. `../CoreData/UI/rml.rcss`) — without one, native controls have no looks.
- Image paths use a `/`-rooted path relative to Data, or a sprite name.
- `z-index`, `top/left` and friends only apply to positioned elements — the rows stay visible with a tooltip instead of hiding, so the rule is discoverable.
- `order` / `row-reverse` and other visual-order properties deliberately stay out of the structured panel (visual order diverging from document order); use Inline Style raw.
- The Canvas dropdown next to the zoom percentage switches the preview resolution (presets 1024x768 / 1280x720 / 1920x1080 / 768x1024 or custom W/H); the canvas size is an editor-wide view preference and is never written into the `.rml`.

---

*Chinese edition: [UI-Editor-Guide.zh-CN.md](UI-Editor-Guide.zh-CN.md)*
