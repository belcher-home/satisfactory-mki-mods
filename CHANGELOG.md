# Changelog

## MKI Tools 0.4.0 and MKI Roads 0.8.2 (2026-10-29)

MKI Bridge stays 0.4.6.

- **MKI Tools 0.4.0.** Requires only SML.
  - **Settings under Options > MKI Tools.** The settings moved from Mods > MKI Tools to their own tab in the game's Options menu, grouped (Player, Items & Inventory, Building, Equipment, World) and used like the game's own settings: checkboxes and sliders, a description for the hovered row, search, Apply and Reset to Default. Values set before carry over. The Inventory Slots row shows the inventory's size and adds 10 slots per press.
  - **Area dismantle mode:** after Terrain, a dismantle gun mode that takes everything in a cube of 16 to 96 m around the aimed building or point, belts and their supports included, and leaves the floor under an aimed machine.
  - **Foundation area zoop:** foundations zoop as a rectangle as well as a line, up to the game's own 10 by 10.
  - **Dismantle unused supports:** dismantling a belt, lift, pipe or hypertube also takes the poles, mounts and supports it leaves holding nothing, outlined red first and refunded (option, on by default).
  - **Elevation and length readouts:** the player's elevation under the compass and, while building, the hologram's elevation and the length of belts, pipes, rails, hypertubes, roads and power lines (option, on by default).
  - **Fill Depot from inventory:** items in the inventory that the Dimensional Depot lists go into the depot's upload queue, up to its limit (option, off by default). The depot panel lists its items A-Z.
  - **Object scanner range bonus:** up to three times the scanner's range, its beeps scaled to match.
  - **Terrain mode breaks nobelisk rocks**, for limestone.
  - **Movement:** the movement speed bonus counts sideways only, everywhere; a Flight mode switch for flying by a double jump, which moves at the ground's walking and sprinting speeds (Blade Runners included). Every numeric option runs to 20, and collecting nearby items reaches 100 m.
  - **Fixes:** the chainsaw's pickup effect and hand mining that ends mid-cycle no longer crash the game; a double jump while gliding no longer starts flight.
- **MKI Roads 0.8.2.** A Lane Transition piece from 4 lanes to 2; Shift keeps a road level while placing; lane switches on the 4-lane truck depots; a Double Street Light for the road's centre line. Fixed: road paths stayed red where a later road piece joined, and dismantling a truck station on a depot left its legs behind.

## MKI Tools 0.3.0, MKI Roads 0.8.1 and MKI Bridge 0.4.6 (2026-10-22)

- **MKI Tools 0.3.0 (new).** Requires only SML; MKI Roads and MKI Bridge do not need it.
  - **Terrain dismantle mode:** a third dismantle gun mode that removes level rocks, arches and cliffs nothing else removes. Hold to remove. Removed rocks are hidden without collision, never deleted; they stay removed across saves and streaming, and give 50 to 500 limestone by size.
  - **Auto pickup:** hold E where E has nothing to use, to turn the game's own auto pickup on or off.
  - **Options under Mods > MKI Tools:**
    - the chainsaw cuts boulders, for limestone;
    - a chainsaw clearance radius of 1x to 10x (5 to 50 m), which also clears grass and boulders in the area;
    - hand crafting and hand mining multipliers (1x to 10x the work per Craft Bench tick and the ore per swing);
    - health regeneration speed (1x to 10x);
    - a movement speed bonus (up to +100%, slides keep the game's speed);
    - an Inventory slots button (+10 per press, up to 500 per save, never shrinks);
    - collect nearby items (berries, nuts, bacon agaric, ground items, power slugs, somersloops, mercer spheres, dismantle and death crates, within 5 to 50 m);
    - short conveyor lifts anywhere (2 m, on by default);
    - vegetation regrowth (off by default; cut plants grow back at random out of sight, more likely with time).
- **MKI Roads 0.8.1.** The HUB milestone is called MKI Roads and unlocks in Tier 3 for 400 Concrete, 100 Reinforced Iron Plate and 50 Modular Frame. Road costs are unchanged.
- **MKI Bridge 0.4.6.** World identity: the column probe can report the actor it hits, `GET /v1/world/actor` and `POST /v1/world/static-meshes`. Terrain routes call MKI Tools by reflection and never require it: `/v1/terrain/removed-rocks`, `remove-rock` and `restore-rock`.

## MKI Roads 0.8.0 and MKI Bridge 0.4.5 (2026-10-19)

The first public release, and the first version of this repository. The mods were renamed for MidKnight Industries: AI RoadPack is now **MKI Roads** (`MKIRoads`) and Satisfactory AI Bridge is **MKI Bridge** (`MKIBridge`), with their classes, assets and menus (MKI Packs, MKI Patterns). Both mods moved here from the project's private development repository, under GPL-3.0.

- **MKI Roads 0.8.0.** Spline roads of 2 or 4 lanes laid like rail track; 2-, 3- and 4-way intersections, a turnaround, single and double truck depots, and concrete road supports; North American Road lane markings drawn per road; automatic vanilla vehicle paths; vanilla street lights, barriers, railings, fences, walkways, catwalks, foundations and truck stations snap to roads. Intersections, turnarounds and depots are built of real 8 m foundation squares, so vanilla foundations, walls, walkways, attachments (pipe and conveyor supports, floor holes, outlets), poles and road supports snap to them as to any foundation, while each piece still dismantles and repaints as one. Road pieces lock and nudge like vanilla holograms, Ctrl locks them to the world grid, they snap to each other and to foundation edges within 2 m and turn in quarter turns there, and they are refused where they would pass through other buildings. Road supports also go under vanilla foundations, their base set by aim and nudge. Costed as asphalt 8x4 foundations. Windows client only. Saves made with AI RoadPack keep their roads: the module redirects every old asset, Blueprint class and native class, struct and enum name to the new one before a save loads.
- **MKI Bridge 0.4.5.** Requires MKI Roads `^0.8.0` (it required `^0.7.0`, which SemVer's 0.x rule reads as excluding 0.8.0). On its first start it copies its token, scopes, audit log and receipts from `Saved/SatisfactoryAIBridge` to `Saved/MKIBridge`, so clients keep working. Its build-gun runs can lock and nudge the hologram.
