# Cartographer port

A clean-room 1:1 re-implementation of **Cartographer**, the retail TA:Kingdoms
map editor (`Cartographer.exe`, Cavedog, May 1999). Behaviour and file formats
are reverse-engineered from the retail binary by **static analysis only** — same
rules as the engine (`docs/retail-engine.md`): never copy its code or ship the
binary/its assets; reimplement observed behaviour.

## What it is (from the PE)

MFC + DirectDraw + COMCTL32 GUI app, 4 sections, imagebase 0x400000:
`.text 0x1000`, `.rdata 0xfa000`, `.data 0x11c000`, `.rsrc 0x174000`
(raw 0x135000). MFC document/view: one map document, a scrolling tile canvas,
modal property dialogs. Credits (About dlg): James Loe (programming + design),
Ron Gilbert (design).

## The complete feature map (from resources)

### Main menu (MENU 128)
- **File**: New (Ctrl+N), Open (Ctrl+O), Save (Ctrl+S), Save As (Ctrl+A),
  Recent, Exit.
- **Edit**: Land Lasso (Ctrl+L, #32789), Clear Area (#32796).
- **View**: Toolbar, Status Bar, Triggers (#32791), Grid (#32794).
- **Scenario**: Properties (#32780), Use Only (#32782), Check Map (#32798),
  Resize (#32787), Scripting (#32790).
- **Zoom**: In (`+` #32785), Out (`-` #32786), 100/75/50/25/12.5%
  (#32771-74, #32795).
- **Help**: About (#57664).

### Context menu (MENU 139) — right-click on a placed object
- **Unit**: Delete (#32775), Properties (#32776).
- **Feature**: Delete (#32777).
- **Start Pos**: Delete (#32779).
- **Tree Unit**: Default Properties (#32781).
- **Trigger**: Delete (#32792), Properties (#32793).

### Dialogs (the data model)
- **New** (30721): fresh-map size + world.
- **Scenario** (133): Width, Height "(in Units {512})", World Type combo,
  Load... — the new-map / world setup. 1 Unit = 512 (16.16 world units? = 32px
  block). So map dims are entered in "units", 512 per block.
- **Scenario Properties** (143): Scenario Name, Scenario Description.
- **Resize** (145): Width, Height (in Units).
- **Use Only** (144): a listbox — the buildable-unit restriction set.
- **Unit Properties** (140): Unit Type combo, Unique Name, Player ID, Health%,
  Veteran, X/Y/Z Pos, Armor%, Weapon%, Angle.
- **Default Unit Properties** (142): Unit Type, Veteran, Armor%, Weapon% — the
  "Tree Unit" (feature) default brush.
- **Scenario Scripting** (146): Conditions list + Actions list per rule,
  Add/Remove Condition/Action, Copy/Paste Rule, Copy/Paste Player Rules, Done.
- **Conditions** (147) / **Actions** (148): pick a condition/action type.
- **Rule editor** (150): 5 combo params + 5 spinners ("Rule"/"Keys" group) —
  the condition/action argument editor.
- **Trigger Properties** (151): Trigger Name.
- **About** (100), **Initializing** (131) splash.

## The editable objects

1. **Terrain**: tile mosaic (paint the `.tnt` tile plane) + heightmap +
   the per-cell feature/road/blocker plane (0xFFFB road, 0xFFFC block).
2. **Features** ("Tree Units"): trees/rocks/mana/etc. with a default brush
   (type/veteran/armor/weapon) — stored in the tnt feature plane by name.
3. **Units**: placed with full props (type, name, player, HP%, veteran, XYZ,
   armor%, weapon%, angle) — scenario data (OTA/side file).
4. **Start positions**: numbered spawn points (OTA `specialwhat=StartPosN`).
5. **Triggers**: named rules of Conditions + Actions, per-player rule sets,
   copy/paste — the scenario scripting system (OTA + a script blob).

## File formats to master (RE targets)

- **`.tnt`** WRITE format — we already READ it (`src/tnt/tnt.cpp`): header words
  (dims, tile keys, cols/rows, heights, feature plane, minimap, second large
  minimap at word 12). Need the exact WRITE layout + minimap generation.
- **`.ota`** — scenario/globalheader (TDF); we parse start positions already.
  Need the full field set the editor writes (name, description, units, triggers).
- Tile sections come from the retail install's `terrain/<hexkey>.jpg` +
  `sections.hpi` prefabs — reuse `terrain::Compositor` + the section prefab loader.

## Port architecture (planned)

`src/cartographer/` — an SDL2/C++20 app (`cartographer` target), reusing the
engine's `tnt`, `terrain::Compositor`, `hpi::Vfs`, `client/mapview` (render),
`client/font`, `jpeg`. New pieces: a **TNT writer**, an **OTA scenario writer**,
the **tile/section palette + brush tools**, the **height tools**, the
**feature/unit/start/trigger object model + property dialogs**, and the
**trigger (condition/action) system**. Immediate-mode UI in our own widgets
(we have font + input already), matching Cartographer's panel layout 1:1.

### Build phases
0. Skeleton: window, open a `.tnt`, render terrain (reuse MapView), pan/zoom. ✅ this session
1. TNT writer + New/Save/Save As round-trip (load→save→byte-compare).
2. Tile palette + paint brush; height tools; Grid overlay; zoom levels.
3. Feature/unit/start-pos placement + selection + the property dialogs.
4. Scenario Properties / Resize / Use Only / Check Map.
5. The trigger system (Scripting): condition/action opcode tables (RE), rule
   editor, per-player rules, OTA script write.
6. Land Lasso + Clear Area; Recent files; polish to 1:1.

## RE status
- Resource map: DONE (menus/dialogs/strings extracted).
- Command IDs → handlers: 32771-32798 are the custom commands (disassemble each
  `OnCommand` case). 57600+ are stock MFC File/View.
- Pending deep RE: TNT write bytes, OTA write fields, trigger opcode tables,
  minimap generation, tile-section palette source.
