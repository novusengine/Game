# Luau API Index

This is a navigation guide for the reusable Luau API. It answers three questions:

1. What reusable module already exists?
2. What problem does it own?
3. When should it be used instead of implementing the behavior locally?

It is intentionally not a method-by-method specification. The exported types in each linked module are the source of truth for exact signatures and options.

## Script loading and module conventions

- Only `.luau` files under `API/` are placed in the `require` bytecode cache.
- Require API modules with `@src/`, omit `.luau`, and use forward slashes:

  ```luau
  local Button = require("@src/API/UI/Button");
  ```

- `Bootstrap/` scripts are startup entry scripts. They are loaded before ordinary scripts and should not be treated as require modules.
- `UI/` scripts are runtime entry scripts. They compose reusable behavior from `API/` modules.
- `Editor/` scripts are developer-mode entry scripts. They are not loaded in normal user mode.
- Reusable behavior needed by multiple entry scripts belongs under `API/`, not `UI/` or `Editor/`.
- Native globals and userdata contracts such as `UI`, `Widget`, `Panel`, `TextWidget`, `Unit`, `Container`, `Spell`, and the developer-only `SpellEditor` are declared in [`Types.def`](Types.def). Their C++ implementations live under `Source/Game-Lib/Game-Lib/Scripting`. The `Spell` global exposes targeting metadata and uses `CastAtPosition` for ground-position spells.

### UI conventions worth remembering

- Text widgets currently support ASCII only. UTF-8 input, editing, measurement, and rendering are not yet supported consistently and remain ongoing work; do not assume any text-facing API is Unicode-safe.
- Positions, sizes, anchors, relative points, clip bounds, and texture-coordinate pairs use `vec` values where the API exposes a vector contract.
- Compound widgets commonly expose a `root` widget and use [`RootedWidget`](API/UI/RootedWidget.luau) to forward ordinary widget methods.
- Panel interaction states reset the live panel data to its registered base template before applying hover/click templates. If a property must persist in every state, put it in the base/state templates rather than applying it only once at runtime.
- `Panel:SetAlpha(0)` makes the fill transparent but does not remove a native border. Set the border size explicitly when an internal scaffolding panel must be completely invisible.
- Prefer [`FrameManager`](API/UI/FrameManager.luau) for named windows and [`PopupLayer`](API/UI/PopupLayer.luau) for temporary click-outside popups.

## Quick choice guide

| Need | Start with |
| --- | --- |
| Styled clickable control | `@src/API/UI/Button` |
| Text entry | `@src/API/UI/InputBox` |
| Boolean, numeric, or choice control | `Checkbox`, `Slider`/`DragInput`, or `Dropdown` |
| Named window with escape/conflict behavior | `FrameManager` |
| Confirm/cancel blocking dialog | `Modal` |
| Context menu or dropdown popup | `ContextMenu` and `PopupLayer` |
| Scrollable content | `ScrollBox` |
| Very large fixed-height list | `VirtualList` |
| Automatic row/column positioning | `VerticalLayout`, `HorizontalLayout`, or `GridLayout` |
| Mutually exclusive state without UI ownership | `SelectionGroup` |
| Generic icon/action cell | `Slot` plus a payload (`ItemAction`, `SpellAction`, or `MacroAction`) |
| Inventory item cell and container movement | `ItemSlot`, `Bag`, and `DragContext` |
| Player inventory lookup/notifications | `PlayerContainers` |
| Editor tool window | `API/Editor/UI/Window` plus `API/Editor/Registry` |

## Core APIs

| Require path | Purpose and main capabilities |
| --- | --- |
| `@src/API/OptionsContext` | Hierarchical runtime options model: categories, groups, sections, typed option kinds, defaults, reset, lookup, and versioned value-change subscriptions. Use it when a setting must be shared between UI and gameplay code. [Source](API/OptionsContext.luau) |
| `@src/API/Input/Input` | High-level input action and binding API. Creates keyboard/mouse/wheel bindings; registers actions and contexts; connects callbacks; queries action and mouse-capture state; detects conflicts; mutates, captures, formats, resets, and saves bindings. Prefer it over direct raw-key handling. [Source](API/Input/Input.luau) |

## General UI APIs

| Require path | Purpose and main capabilities |
| --- | --- |
| `@src/API/UI/AtlasPanel` | Panel backed by a uniform texture atlas. Selects a 1-based atlas cell and forwards common panel/input operations. Use for sprite sheets whose cells share a regular grid. [Source](API/UI/AtlasPanel.luau) |
| `@src/API/UI/Button` | Standard styled button wrapper with text alignment, colors/alpha for base-hover-highlight-disabled states, optional border overrides, activation callbacks, and forwarded widget operations. [Source](API/UI/Button.luau) |
| `@src/API/UI/Checkbox` | Boolean control composed from background and fill panels. Supports checked state, vetoable value changes, and mouse callbacks. [Source](API/UI/Checkbox.luau) |
| `@src/API/UI/Collapsible` | Expandable header and body section with measured total height and toggle callbacks. Use for inspector/editor sections that must restack when collapsed. [Source](API/UI/Collapsible.luau) |
| `@src/API/UI/ContextMenu` | Dynamic menu with items, dividers, disabled/selected states, cursor opening, and `OpenBelow` anchoring. Uses `PopupLayer` for outside-click dismissal. [Source](API/UI/ContextMenu.luau) |
| `@src/API/UI/Draggable` | Attaches drag movement to a target widget, optionally through a separate handle and min/max bounds. Use for simple local-position dragging; use a dedicated handle for windows. [Source](API/UI/Draggable.luau) |
| `@src/API/UI/DragInput` | Developer numeric editor supporting horizontal scrub and typed input, with min/max, speed, format, and callbacks. `NewVec3` composes three fields for vectors. [Source](API/UI/DragInput.luau) |
| `@src/API/UI/Dropdown` | Button-backed single-choice control using a context menu. Supports dynamic options, placeholders, value changes, disabled options, and optional notification/veto behavior. [Source](API/UI/Dropdown.luau) |
| `@src/API/UI/GridLayout` | Uniform-cell row-major or column-major grid with configurable rows/columns/cell size, spacing, padding, cell alignment, grid alignment, sizing modes, and child options. [Source](API/UI/GridLayout.luau) |
| `@src/API/UI/HorizontalLayout` | Convenience wrapper over `LinearLayout` with the X axis selected. Use for left-to-right or right-to-left automatic layout. [Source](API/UI/HorizontalLayout.luau) |
| `@src/API/UI/InputBox` | Editable ASCII-only text control with focus, cursor movement, selection, clipboard operations, placeholder, secure/numeric/multiline modes, submit/tab/focus/change callbacks, and horizontal text clipping. UTF-8 support remains ongoing work. [Source](API/UI/InputBox.luau) |
| `@src/API/UI/LayoutCommon` | Internal implementation shared by `LinearLayout` and `GridLayout`: child proxies, invalidation, option merging, and base layout machinery. Do not use directly for ordinary UI composition. [Source](API/UI/LayoutCommon.luau) |
| `@src/API/UI/LinearLayout` | Axis-configurable automatic layout with padding, spacing, fixed/flex children, weights, min/max constraints, alignment, reverse order, aspect ratios, and fixed/shrink/auto container sizing. Usually require `HorizontalLayout` or `VerticalLayout` instead. [Source](API/UI/LinearLayout.luau) |
| `@src/API/UI/Modal` | Blocking dialog shell registered with `FrameManager`. Supports confirm/cancel dialogs with dynamic per-open text and callbacks, optional action buttons, custom styling, exposed dialog content, escape cancellation, and single-modal conflict handling. [Source](API/UI/Modal.luau) |
| `@src/API/UI/PopupLayer` | Owns the single active temporary popup and its dismissal shields. Supports an input-passthrough region and hover-outside closing. Usually consumed through `ContextMenu` or `Dropdown`. [Source](API/UI/PopupLayer.luau) |
| `@src/API/UI/ProgressBar` | Non-interactive normalized progress display with configurable background/fill templates and inset. `SetProgress` clamps to `[0, 1]`. [Source](API/UI/ProgressBar.luau) |
| `@src/API/UI/RootedWidget` | Attaches a forwarding metatable so a compound table with `.root` can be used like the underlying widget while retaining its own API. Foundation for most compound controls. [Source](API/UI/RootedWidget.luau) |
| `@src/API/UI/ScrollBox` | Clipped scrolling viewport with a public content panel, optional horizontal/vertical scrollbars, top- or bottom-filled content, content sizing, offsets, callbacks, and programmatic start/end scrolling. [Source](API/UI/ScrollBox.luau) |
| `@src/API/UI/SelectionGroup` | Generic single-selection state with previous/current values, optional veto callback, notification control, clear, and callback replacement. It owns selection state but no widgets. [Source](API/UI/SelectionGroup.luau) |
| `@src/API/UI/Slider` | Interactive numeric slider with min/max/step, value/progress conversion, configurable track/fill geometry, and value-change callback. [Source](API/UI/Slider.luau) |
| `@src/API/UI/Slot` | Generic icon/action cell. A payload supplies icon, availability, activation, and optional tooltip methods; the slot owns visual refresh, left-click activation, and hover tooltip dispatch. [Source](API/UI/Slot.luau) |
| `@src/API/UI/Templates` | Registers the shared panel and text templates used by standard controls. Bootstrap initializes it; ordinary modules should consume registered template names rather than calling it again. [Source](API/UI/Templates.luau) |
| `@src/API/UI/TextUtil` | Case-insensitive containment plus character- and rendered-width-based truncation helpers. Use for filters and compact labels. [Source](API/UI/TextUtil.luau) |
| `@src/API/UI/Tooltip` | Lightweight generic tooltip with show/hide, clear, and vertically stacked text lines. Use `GameTooltip` for item/spell presentation. [Source](API/UI/Tooltip.luau) |
| `@src/API/UI/VerticalLayout` | Convenience wrapper over `LinearLayout` with the Y axis selected. Use for top-down or bottom-up automatic layout. [Source](API/UI/VerticalLayout.luau) |
| `@src/API/UI/VirtualList` | Fixed-row-height list backed by `ScrollBox` that recycles a viewport-sized row pool. Use for large lists; `bindRow` must fully refresh every visual and handler-visible value. [Source](API/UI/VirtualList.luau) |

## Game APIs

| Require path | Purpose and main capabilities |
| --- | --- |
| `@src/API/Game/FrameManager` | Central lifecycle and stacking policy for named frames. Handles open/close/toggle, escape dismissal, open order, and conflicts among `ManagedPanel`, `Auxiliary`, `WidePanel`, `Fullscreen`, `Modal`, and `System` roles. [Source](API/Game/FrameManager.luau) |
| `@src/API/Game/EditMode` | Core edit mode state machine: enter/exit, element registration, category filtering, per-element toggle, overlay lifecycle, and canvas management for user UI customization. [Source](API/Game/EditMode.luau) |
| `@src/API/Game/EditModeDefaults` | Edit mode category and element ID constants, default definitions with positions and toggle states, and category color palette. Import to use typed IDs when registering editable elements. [Source](API/Game/EditModeDefaults.luau) |
| `@src/API/Game/EditModeOverlay` | Per-element edit mode overlay with colored highlight, header label, reset button, and full-body dragging. Created and destroyed by `EditMode` during enter/exit. [Source](API/Game/EditModeOverlay.luau) |
| `@src/API/Game/EditModeProfile` | Profile CRUD, layout snapshot/apply, JSON serialization, and UI code import/export (base64-encoded). Manages the default profile lifecycle and profile switching. [Source](API/Game/EditModeProfile.luau) |
| `@src/API/Game/ChatListener` | Ordered chat listener chain. A listener can consume processing and independently decide whether the original message is blocked, with optional registration/consumption diagnostics. [Source](API/Game/ChatListener.luau) |
| `@src/API/Game/PlayerContainers` | Process-wide registry for equipment, equipped bags, bank, and mail containers. Provides indexed access, replacement/removal, and change subscriptions with unsubscribe functions. [Source](API/Game/PlayerContainers.luau) |
| `@src/API/Game/ItemUtil` | Item-domain constants and mappings for equipment slots/types, categories, rarity, binding, weapon styles, stats, effect prefixes, display text, colors, and empty-slot textures. Use instead of duplicating item enum logic. [Source](API/Game/ItemUtil.luau) |

## Game UI APIs

| Require path | Purpose and main capabilities |
| --- | --- |
| `@src/API/Game/UI/ActionBar` | Configurable action-slot grid with key binding labels, input activation, payload assignment, configurable empty-slot texture/color/opacity, action-reference dragging, and inventory-item drops converted to `ItemAction`. Unbound slots default to opaque `texture/ui/icons/itemslots/slot_empty.png`. [Source](API/Game/UI/ActionBar.luau) |
| `@src/API/Game/UI/Backpack` | Standalone equipped-bag strip and combined backpack frame. Attaches equipment/storage `Bag` containers, maps every displayed slot back to its real container/local index, dynamically lays out capacity, and toggles through `FrameManager` or the rebindable `Toggle Backpack` input action (`B` by default). Layout constants live in `Backpack.Config` and currently require reload after edits. [Source](API/Game/UI/Backpack.luau) |
| `@src/API/Game/UI/Bag` | Inventory container model plus optional default UI. Owns item/count data, free/locked slots, fit/use callbacks, add/remove/swap handling, and cursor commits. Custom `setupCallback` lets another UI, such as `Backpack`, own its item-slot widgets. [Source](API/Game/UI/Bag.luau) |
| `@src/API/Game/UI/DragContext` | Singleton held-operation state and cursor visual for inventory moves and generic action references. Begins/commits/cancels inventory swaps, transfers action payloads, and clears held state. Bootstrap initializes it. [Source](API/Game/UI/DragContext.luau) |
| `@src/API/Game/UI/GameTooltip` | Singleton item/spell tooltip with typed item/spell population, custom single/double lines, minimum width, visibility controls, and current-item queries. Bootstrap initializes it. [Source](API/Game/UI/GameTooltip.luau) |
| `@src/API/Game/UI/ItemAction` | `Slot.Payload` adapter for an item ID. Resolves its icon, searches player containers for availability, uses the matching item, and delegates item tooltip display. [Source](API/Game/UI/ItemAction.luau) |
| `@src/API/Game/UI/ItemSlot` | Inventory/equipment item widget bound to a container and local slot index. Handles icon/default texture, tooltip, use/drag commits, optional size, and textureless selected border. [Source](API/Game/UI/ItemSlot.luau) |
| `@src/API/Game/UI/MacroAction` | `Slot.Payload` adapter for caller-owned macro IDs, icons, and activation callbacks. [Source](API/Game/UI/MacroAction.luau) |
| `@src/API/Game/UI/Nameplate` | World or standalone unit nameplate with unit positioning, name/reaction presentation, health display, visibility/focus controls, and destruction. [Source](API/Game/UI/Nameplate.luau) |
| `@src/API/Game/UI/SpellAction` | `Slot.Payload` adapter for spell IDs. Resolves spell icons, casts by ID, and delegates spell tooltip display. Availability is currently unconditional because known/usable queries are not exposed. [Source](API/Game/UI/SpellAction.luau) |
| `@src/API/Game/UI/UnitFrame` | Full unit-frame component with identity/level, health and resource bars, smoothed damage/healing trails, reaction/targeting presentation, visibility/input controls, and options-backed timing behavior. [Source](API/Game/UI/UnitFrame.luau) |
| `@src/API/Game/UI/UnitPresentation` | Shared unit color helpers: faction-reaction palette selection, health-gradient calculation, and clamped color interpolation. [Source](API/Game/UI/UnitPresentation.luau) |

## Editor APIs

These modules are requireable everywhere, but their usual consumers are developer-only entry scripts under `Editor/`.

| Require path | Purpose and main capabilities |
| --- | --- |
| `@src/API/Editor/Registry` | Registry of named editor windows implementing `Show`, `Hide`, and `IsVisible`. Supports registration, removal, lookup, toggling, visibility queries, and listing. [Source](API/Editor/Registry.luau) |
| `@src/API/Editor/Selection` | Shared editor entity-selection state. Owns the single native selection callback and fans changes out to multiple Luau listeners. [Source](API/Editor/Selection.luau) |
| `@src/API/Editor/Terrain` | Typed wrapper over the developer-only native terrain editing session, including sculpting, texture-layer painting and picking, stroke transactions, undo/redo, and explicit Pact-staging save. [Source](API/Editor/Terrain.luau) |
| `@src/API/Editor/TerrainBrushes` | Registry and lifecycle contract for Luau-scripted terrain brushes. Brushes compose native bulk operations without running the per-vertex workload in Luau. [Source](API/Editor/TerrainBrushes.luau) |
| `@src/API/Editor/TerrainBrushes/Flatten` | Built-in flatten brush module. Captures its target height at stroke start and delegates the bulk operation to native terrain editing. [Source](API/Editor/TerrainBrushes/Flatten.luau) |
| `@src/API/Editor/TerrainBrushes/RaiseLower` | Built-in raise/lower brush module with Shift inversion. [Source](API/Editor/TerrainBrushes/RaiseLower.luau) |
| `@src/API/Editor/TerrainBrushes/Smooth` | Built-in smoothing brush module backed by the native smoothing operation. [Source](API/Editor/TerrainBrushes/Smooth.luau) |
| `@src/API/Editor/TerrainBrushes/TexturePaint` | Built-in scriptable texture-paint brush with target opacity and Shift erase behavior. [Source](API/Editor/TerrainBrushes/TexturePaint.luau) |
| `@src/API/Editor/UI/MenuBar` | Editor menu bar composed from buttons and context menus. Supports multiple menus, hover switching, sizing options, and explicit close. [Source](API/Editor/UI/MenuBar.luau) |
| `@src/API/Editor/UI/Theme` | Shared editor colors, alpha, border, and text-template values. Use to keep editor tools visually consistent. [Source](API/Editor/UI/Theme.luau) |
| `@src/API/Editor/UI/Window` | Standard editor window shell with themed background/border, title bar, close button, and title-bar dragging. Callers own show/hide registration and content layout. [Source](API/Editor/UI/Window.luau) |

## Bootstrap

### `Bootstrap/Init.luau`

Automatically performs the reusable UI startup sequence:

1. Registers shared templates through `API/UI/Templates`.
2. Initializes the global inventory/action `DragContext`.
3. Initializes the singleton `GameTooltip`.

Do not repeat these initialization calls in ordinary entry scripts. [Source](Bootstrap/Init.luau)

## Common implementation recipes

### Managed window

Use `FrameManager` when a window must participate in escape handling and window-role conflicts:

```luau
local FrameManager = require("@src/API/Game/FrameManager");

FrameManager.Register("QuestLog", {
    root = rootPanel,
    role = "ManagedPanel",
});

FrameManager.Toggle("QuestLog");
```

Use `Auxiliary` for a frame that may coexist with normal managed panels. Use `Modal` through the `Modal` API when input behind the frame must be blocked.

### Draggable window with a dedicated header

```luau
local Draggable = require("@src/API/UI/Draggable");

local root = canvas:NewPanel(position, size, layer, "DialogBox");
local header = root:NewPanel(vec.New(0, 0), vec.New(size.x, 24), 1i, "DebugDarkGrey");
header:SetAnchor(vec.New(0, 1));
header:SetRelativePoint(vec.New(0, 1));
Draggable.Attach(root, header);
```

Do not attach window dragging to the entire content background when rows, empty-space clicks, or held-item cancellation need distinct behavior.

### Scrollable list

Use `ScrollBox` for modest lists whose rows can all exist at once:

```luau
local ScrollBox = require("@src/API/UI/ScrollBox");

local list = ScrollBox.New(parent, position, size, layer, {
    panelTemplate = "DebugDarkGrey",
    contentPanelTemplate = "DebugDarkGrey",
    contentSizeX = size.x,
    contentSizeY = size.y,
    verticalScrollBar = true,
    scrollBarWidth = 10,
    alpha = 0,
});

local row = list.content:NewPanel(vec.New(0, 0), vec.New(size.x - 10, 18), 1i, "DebugDarkGrey");
list:SetContentHeight(totalRows * 18);
```

Use `VirtualList` when item count is large or unbounded. Its `createRow` builds only the pool; its `bindRow` must set the complete visual state for whichever item is currently assigned.

### Automatic layout

```luau
local VerticalLayout = require("@src/API/UI/VerticalLayout");

local layout = VerticalLayout.New(parent, position, size, layer, {
    spacing = 6,
    padding = 8,
    heightMode = "shrink",
    defaultChildOpts = { modeX = "flex" },
});

layout:NewText("Header", vec.New(0, 0), 0i, "DefaultButtonText");
layout:NewPanel(vec.New(0, 0), vec.New(200, 32), 0i, "DialogBox");
```

Use `GridLayout` for uniform cells. `stretch` container sizing is currently reserved and intentionally errors; use `fixed`, `shrink`, or `auto`.

### Context menu or dropdown

Use `ContextMenu` for commands and `Dropdown` for selecting a value. Both already own click-outside dismissal through `PopupLayer`; do not add another full-screen shield.

### Action slot payload

`Slot` does not know about items, spells, or macros. Give it a payload implementing the shared contract:

```luau
local Slot = require("@src/API/UI/Slot");
local SpellAction = require("@src/API/Game/UI/SpellAction");

local slot = Slot.New(parent, position, vec.New(40, 40), layer);
slot:SetPayload(SpellAction.New(spellID));
```

Use `ActionBar` when the slots also need registered input actions, binding labels, and drag/drop reassignment.

### Inventory UI

- `Bag` owns authoritative local slot data and container operations.
- `ItemSlot` owns one interactive slot visual and retains its actual container/local index.
- `PlayerContainers` discovers the player's active containers.
- `DragContext` owns the one operation currently held by the cursor.
- `Backpack` projects several equipped `Bag` containers into one combined frame without changing container indices.

Preserve the real `Bag` and local slot index on every displayed `ItemSlot`; do not flatten them into synthetic network slot indices.

### Editor tool

Create the shell with `API/Editor/UI/Window`, put repeated sections in `Collapsible`, and register show/hide behavior with `API/Editor/Registry`. Use `API/Editor/Selection` instead of taking ownership of the native single selection callback.

### Edit Mode element registration

Register UI elements with `EditMode` so they participate in edit mode. Elements become draggable and gain highlight overlays when the user enters edit mode from the Game Menu.

```luau
local EditMode = require("@src/API/Game/EditMode");
local Defaults = require("@src/API/Game/EditModeDefaults");

EditMode.Register({
    id = Defaults.ElementIDs.PlayerUnitFrame,
    category = Defaults.CategoryIDs.UnitFrames,
    displayName = "Player Frame",
    defaultPos = vec.New(770, 500),
    getPos = function() return frame.border:GetPos() end,
    setPos = function(pos) frame.border:SetPos(pos) end,
    isEnabled = function() return frame:IsEnabled() end,
    setEnabled = function(enabled) frame:SetEnabled(enabled) end,
});
```

Use `EditModeDefaults.CategoryIDs` and `EditModeDefaults.ElementIDs` for typed ID constants. Categories are auto-discovered from registrations; add new categories to `EditModeDefaults.Categories` when introducing new element groups.

## Maintaining this index

Update this document in the same change whenever a module under `API/` is added, removed, renamed, or materially changes purpose or capabilities. Keep entries concise and link to source rather than copying complete type definitions.
