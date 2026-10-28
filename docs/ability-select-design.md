# New Ability Select

How the game puts each hero's abilities on keys: three SPEC keys, a second
row of three under SHIFTER, an ABILITIES screen that chooses what sits
where, a new default keyboard and a CHANGELOG! page that announces it.
This document describes what shipped, for whoever maintains it next. The
section letters (A.5, E.1 and so on) are the ones the code comments cite.

The short version:

- With **New Ability Select** on (the default), SPEC 1, SPEC 2 and SPEC 3
  each cast one ability. Holding SHIFTER gives three more. Six slots, one
  press each, no cycling.
- **TRAIN > ABILITIES** puts any ability the hero has on any slot. Every
  class has a default layout; the Archmage's seventh spell, MIND CONTROL,
  starts on the bench (the abilities that are not on a key).
- Every ability has a **picture** (section I). In play the HUD shows the
  three pictures on the keys, not their names; the ABILITIES screen shows
  each picture with its name beside it, which is where a player learns
  what each picture means.
- One new default keyboard for every player (section C.2). A returning
  player's saved keys move to it once; a key the player chose stays.
- A **CHANGELOG!** page shows once, the first time the player presses
  BEGIN NEW GAME or CONTINUE after the update, with a picture of the new
  keyboard.
- Turning the setting off (**Game Settings > UI FX > New Ability Select**)
  brings back the old rules on the new keys: SPEC 1 casts the selected
  special, SPEC 2 changes it, SHIFTER casts the alternate. The game the
  server plays is then byte for byte the old one.

The setting changes only what a key press means on this machine. The
server never reads a layout or the setting, and bots never read either.

---

## A. The model

### A.1 Words

| Word | Meaning |
|---|---|
| ability | One castable thing of a class: a special slot's primary, or that slot's alternate. Players see "ability"; prose about mages may say "spell". |
| entry | How an ability is named in code: `(special slot 1..5, alternate yes/no)`. |
| catalog | The entries a class has this session, in slot order `(1,0) (1,1) (2,0) ... (5,1)`, keeping only named entries the New Specials setting leaves in play, and dropping an entry whose name repeats an earlier one. Entries the hero's level has not opened stay in the catalog. |
| belt | Six slots: 0, 1, 2 are SPEC 1/2/3; 3, 4, 5 are SPEC 1/2/3 with SHIFTER held. Each holds one entry or nothing. |
| bench | Every catalog entry not on the belt. Worked out each time, never stored. |
| locked | An entry whose special slot the hero's level has not opened. Shown grey with the level it opens at. |
| hidden | A New Specials entry while that setting is off. Not in the catalog. |

The cleric's TURN UNDEAD is the alternate of both RAISE UNDEAD and RAISE
GHOST (`packs/core/families/living-05-cleric.lua`; both call the one
`do_turn_undead`). It is listed once, under RAISE UNDEAD, because the second
copy would be the same ability with a different price.

The model lives in `include/openglad/gameplay/families/belt.h` and
`src/gameplay/belt.cpp` (SDL-free, no RNG, no side effects). Every fact
about a class comes through `og::sim::specials_view`
(`include/openglad/gameplay/families/specials_view.h`: `special_name`,
`alternate_name`, `special_in_play`, `alternate_in_play`, `slot_open`,
`slot_open_level`), so the belt sees what the HUD, the old cycle key and
the bots see.

### A.2 The byte at rest

Each hero (`guy::belt`, `include/openglad/gameplay/guy.h`) stores six
bytes, one per belt slot:

| Value | Meaning |
|---|---|
| `0x00` | DEFAULT: whatever the class default puts in this slot. An untouched hero is six zeros. |
| `0xFF` | EMPTY: the player cleared this slot. |
| bits 0-2 = special slot 1..5, bit 3 = alternate, bits 4-6 zero | That entry. `0x09` is slot 1's alternate. |
| bit 7 set (`0x80`) | Kept for item abilities (issue #177). Nothing writes it today; it reads as an empty slot. |
| anything else | Junk (slot 0, 6 or 7 with other bits, or bits 4-6 set). Read as DEFAULT. |

One function applies the table, `normalize_stored`. The save reader runs
every byte through it, and `resolve` runs it again on whatever it is given,
so a junk byte from a file and a junk byte from another machine both end up
as the class default.

### A.3 The byte on the input wire

`PlayerInput::belt_cast` (`include/openglad/gameplay/input_state.h`), one
byte per local seat:

| Value | Meaning |
|---|---|
| `0` | The classic cast: the walker's current slot, with the physical Shift key choosing the alternate. A seat with the setting off always sends 0. |
| `slot | alt << 3` | Cast this entry. Only means something while Special is pressed or held. |
| `0x07` | Nothing on this key. The server says NO ABILITY THERE and casts nothing. |
| any other nonzero value | Malformed. Refused exactly like `0x07`. |

The rule is simple on purpose: 0 is classic, and every other value goes
through the server's check (C.7).

### A.4 Resolving a layout

`og::sim::belt::resolve(family, level, new_specials, bytes)` turns six
stored bytes into six slots a player sees. Per slot: normalise the byte;
DEFAULT takes that slot of the class default (A.5); EMPTY and an item byte
are empty; an explicit entry is that entry when it is in the catalog, and
empty otherwise. Then a filled slot is **Locked** below the level its
special slot opens at, else **Castable**.

Two consequences worth knowing:

- An explicit byte is never rewritten. A thief who put MINE on a key and
  then turned New Specials off sees that key empty; turning New Specials
  back on brings MINE back.
- `resolve` asks the catalog, but the server's check (`validate_cast`)
  does not. So the cleric's second TURN UNDEAD, `(3, alt)` = `0x0B`, reads
  as an empty slot on every screen and is never sent by a client, but the
  server would still cast it from a crafted byte, at RAISE GHOST's price.
  That is harmless (the player owns that ability anyway) and is how
  `belt.h` documents the two functions.

A walker with no hero record behind it (a summoned ally or raised skeleton
reached through Switch Character, a possessed host, a cross-controlled bot)
uses the class default: `bytes_for(const walker&)` returns
`w.myguy ? w.myguy->belt : default_bytes()`.

### A.5 The default layout, every core class

The rule is "pair-first", and it depends on the class and the New Specials
setting only, never on level. Walking the catalog in slot order: a primary
takes the lowest free key (1, 2, 3); its alternate takes SHIFTER + the same
key when that is free, else the lowest free SHIFTER key; a primary with no
plain key left takes the lowest free SHIFTER key; anything left over goes
on the bench. This keeps the old habit, "Shift is the alternate", true for
every class it can.

With New Specials on (the default):

| Class | 1 | 2 | 3 | SHIFTER+1 | SHIFTER+2 | SHIFTER+3 | Bench |
|---|---|---|---|---|---|---|---|
| Soldier | CHARGE | BOOMERANG | WHIRLWIND | DISARM | – | – | – |
| Elf | ROCKS | BOUNCING ROCKS | LOTS OF ROCKS | MEGA ROCKS | – | – | – |
| Archer | FIRE ARROWS | BARRAGE | EXPLODING BOLT | – | – | – | – |
| Mage | TELEPORT | WARP SPACE | FREEZE TIME | TELEPORT MARKER | ENERGY WAVE | HEARTBURST | – |
| Skeleton | TUNNEL | DIG IN | BONE WALL | REASSEMBLE | LEGION | BONE STORM | – |
| Cleric | HEAL | RAISE UNDEAD | RAISE GHOST | MYSTIC MACE | TURN UNDEAD | RESURRECT | – |
| Elemental | STARBURST | IMMOLATE | METEOR RAIN | REKINDLE | SUPERNOVA | – | – |
| Faerie | BLINK | GLIMMER | HASTEN | SWAP | WISH | HASTE SELF | – |
| Slime / Small Slime / Medium Slime | SPLIT / GROW / GROW | – | – | – | – | – | – |
| Thief | DROP BOMB | CLOAK | TAUNT ENEMY | MINE | POISON CLOUD | CHARM OPPONENT | – |
| Ghost | SCARE | SIPHON | POSSESS | WAIL | PHASE | – | – |
| Druid | GROW TREE | SUMMON FAERIE | REVEAL | PROTECTION | – | – | – |
| Orc | HOWL | EAT CORPSE | – | – | – | – | – |
| Orc Captain | HOWL | EAT CORPSE | HOOK BLADE | WAR BANNER | WARBAND | KNIFE FAN | – |
| Barbarian | HURL BOULDER | EXPLODING BOULDER | – | – | – | – | – |
| Archmage | TELEPORT | HEARTBURST | SUMMON IMAGE | TELEPORT MARKER | CHAIN LIGHTNING | SUMMON ELEMENTAL | MIND CONTROL |
| Golem, Giant Skeleton, Tower | – | – | – | – | – | – | – |

Two walks spelled out. Mage: TELEPORT takes 1 and its alternate TELEPORT
MARKER takes SHIFTER+1; WARP SPACE takes 2; FREEZE TIME takes 3; ENERGY
WAVE finds no plain key left and takes SHIFTER+2; HEARTBURST takes
SHIFTER+3. Cleric: HEAL 1 with MYSTIC MACE on SHIFTER+1, RAISE UNDEAD 2
with TURN UNDEAD on SHIFTER+2, RAISE GHOST 3 (its copy of TURN UNDEAD is
dropped, A.1), RESURRECT SHIFTER+3.

Four alternates do not sit on SHIFTER + their primary's key. The
skeleton's LEGION, the elemental's SUPERNOVA and the orc captain's WARBAND
belong to a primary that itself landed on SHIFTER+1 (REASSEMBLE, REKINDLE,
WAR BANNER), so they take the next free SHIFTER key, SHIFTER+2. The
fourth is the cleric's dropped second TURN UNDEAD. The class help's
ability lines are generated from this same catalog (`og::ui` in
`src/interface/ui/picker_common.cpp`, composed into HELP > Classes by
`src/interface/ui/help.cpp`); `cfg/help_classes.txt` keeps only each class's
prose, so the help names every second form where the catalog puts it.

With New Specials off the New Specials entries leave the catalog and the
rule runs again: Skeleton 1 TUNNEL; Elemental 1 STARBURST; Faerie and Orc
Captain nothing; Ghost 1 SCARE; Thief 1 DROP BOMB, 2 CLOAK, 3 TAUNT ENEMY,
SHIFTER+1 POISON CLOUD, SHIFTER+3 CHARM OPPONENT (POISON CLOUD moves up
because MINE is gone). Every other class is as above.

Both tables are pinned for every class by `tests/unit/test_belt.cpp`
(`default_layout_matches_the_table`, og_unit_kits).

A locked ability still sits on its key. A level-1 mage's HUD already shows
FREEZE TIME on key 3, greyed with "L7"; it lights up at level 7 with no
edit. Only the Archmage benches anything by default.

### A.6 Promotion, level-up, New Specials, class packs

- **Promotion** (Mage to Archmage, Orc to Orc Captain) resets the layout to
  six zeros, the new class's default: `og::ui::promote_guy` calls
  `reset_belt_for_family_change` (`src/interface/ui/picker_common.cpp`).
- **Level-up** moves nothing. The results screen
  (`src/interface/ui/results_screen.cpp`) now announces slot 5 too (the
  mage's HEARTBURST was never announced before), and adds
  ` (ON THE BENCH)` when the ability that just opened is not on a key.
- **New Specials toggled**: explicit bytes naming hidden entries read empty
  and come back with the setting (A.4). DEFAULTS on the ABILITIES screen
  writes six zeros.
- **Class packs**: see `docs/modding/api-reference.md`, "Abilities on keys".
  A pack may declare up to ten entries (five slots and their alternates);
  six go on keys and the rest wait on the bench.

### A.7 The accepted drift

After a belt cast, a walker's `current_special` and `shifter_down` mean
"the entry cast last" rather than "where the cycle key left it". If that
hero is later handed to the AI, `statistics::hit_response`
(`src/gameplay/stats.cpp`) answers a hit with that slot, exactly as it
answers today with whatever the cycle key left. It depends on what the
player did either way and no parity scenario can see it.

### A.8 The place left for item abilities (#177)

Not built. Every belt cast goes through one server function (C.7) that
takes an entry. An item ability would add a kind to `Entry`, use the
reserved `0x80` byte at rest and a reserved value on the wire, and call the
item's hook instead of `set_current_special`. The HUD, the ABILITIES model
and the resolver deal only in resolved slots and entries, so they would
not change shape.

---

## B. The setting

- **Where**: Game Settings > UI FX, fourth row, label `New Ability Select`
  (`src/interface/ui/menu_screen_specs.cpp`, `kUiFxRows`). The UI FX
  column was widened to x 100, w 120 so the 18-letter label fits.
- **cfg**: `effects/new_ability_select`, default `on`
  (`src/resources/gparser.cpp`, and the shipped `cfg/openglad.yaml`).
- **Per machine, never on the wire.** It is a UI FX row, not a Gameplay FX
  rule: every UI FX row is `toggle_effect("effects", key)` and stays on the
  machine. The server needs no setting (C.6), so a host never flips a
  joiner's keyboard. A host with it on and a joiner with it off play the
  same match without trouble: each sends its own kind of input and draws its
  own HUD.
- **One accessor**: `og::input::ability_select_on()`
  (`include/openglad/interface/input.h`), backed by a cached flag on
  `InputHardwareState` that the controls loader, the UI FX toggle
  (`ButtonAction::ToggleNewAbilitySelect`, `src/interface/ui/button.cpp`)
  and RESTORE SETTINGS refresh. The per-frame resolver reads the cache,
  never cfg.
- **Off is the old game.** The client sends `belt_cast = 0` with today's
  bits, the SPEC 2 key sends the old change-special action, and the server
  takes its unchanged path. The HUD draws the old `SPC:` row, the binding
  panels show the seven old rows, REMAP asks the old prompts, HELP shows the
  old table, all on the new keys.
- **Terminals**: the text client's and the curses client's settings pages
  ask `New ability select: on/off`, stored in the same cfg key.
- **Tests** run with the setting on (the shipped default):
  `tests/integration/integration_main.cpp` sets it and re-sets it before
  every test. A test that pins the old output takes `ScopedAbilitySelect`
  (`tests/test_ability_select_scope.h`).

---

## C. Keys and the wire

### C.1 Key slots

`include/openglad/interface/input.h`:

| Slot | Name | Meaning |
|---|---|---|
| 9 | `KEY_SPECIAL` | SPEC 1 (setting off: SPECIAL) |
| 11 | `KEY_SPECIAL_SWITCH` | Reserved, like slot 14: the retired change-special key. Never bound, never shown. |
| 13 | `KEY_SHIFTER` | SHIFTER: the belt's second row (setting off: the alternate) |
| 17 | `KEY_SPEC2` | SPEC 2 (setting off: change special) |
| 18 | `KEY_SPEC3` | SPEC 3 (setting off: unused) |

`NUM_KEYS` is 19. Slots 16, 17 and 18 never travel: the client folds SPEC
1/2/3 and SHIFTER into the one Special action plus `belt_cast`, so the
input wire still carries 16 actions. One physical key, one slot, two
meanings by setting.

### C.2 The default keyboard

One factory table for both settings: `kDefaultFourDirKeys` and
`kDefaultEightDirKeys` in `src/interface/input/input_state.cpp` (the 2002
row comments are kept on their rows). The tables from before this change
stay beside them as `kLegacyFourDirKeys` / `kLegacyEightDirKeys`, used
only by the migration (C.5) and its tests.

4-direction (the approved picture, "Proposal 1"):

| | P1 | P2 | P3 | P4 |
|---|---|---|---|---|
| Up Right Down Left | W D S A | arrows | I L K J | T H G F |
| Fire | Left Ctrl (web: Z) | Right Shift | Space | V |
| SPEC 1 / 2 / 3 | 1 2 3 | [ ] \ | 8 9 0 | 4 5 6 |
| Shifter | Left Shift | / | M | B |
| Yell | E | Enter | O | Y |
| Look up (hold) | Q | Right Ctrl (web: Delete) | U | R |
| Switch character | `` ` `` | ' | - | N |
| Cheat | F5 | F6 | F7 | F8 |

Look up sits left of walk-up and Yell right of it for P1, P3 and P4 (Q W E,
U I O, R T Y).

8-direction keeps each player's diagonal ring and moves what it collides
with. Rows that differ from 4-direction:

| | P1 (W E D C X Z A Q) | P2 | P3 (I O L . , M J U) | P4 (T Y H N B V F R) |
|---|---|---|---|---|
| Fire | Left Ctrl (web: S) | Right Shift | Space | G |
| Switch character | `` ` `` | ' | - | = |
| Yell | S (web: Tab) | Enter | K | 7 |
| Shifter | Left Shift | / | ; | Left Alt |
| Look up (hold) | Tab (web: unbound) | Right Ctrl (web: Delete) | P | unbound |

No key is bound by two players in either mode, native or web. The sweeps
in `tests/integration/test_input_keybinds.cpp` and
`tests/integration/test_input_latch.cpp` check every player, both modes
and every key, with no exemptions.

**Web player 1** (`include/openglad/interface/web_control_defaults.h`):
the browser owns Ctrl+W, so web P1's FIRE stays off Ctrl (Z, or S in
8-direction), and in 8-direction YELL moves to Tab and LOOK UP starts
unbound. `browser_safe_default_key` substitutes these at the read sites.

**Web player 2** (#391): on web P2's LOOK UP is Delete in both modes, not
Right Ctrl (`kWebP2LookupKey`, substituted by the same
`browser_safe_default_key`). With it no web default binds Ctrl at all, so no
two players on one keyboard can form a Ctrl chord (Ctrl+1/2/3 tab switching,
Ctrl+W) out of the factory keys. Mac laptops have no forward-Delete key: P2
presses fn + delete there (the key labelled "delete" is Backspace, the web
PAUSE key). `web/help.html` and the KEYMAP legend say so.

**Modifier chords across seats.** `tests/unit/test_web_control_defaults.cpp`
walks every seat's modifier keys against every other bound key, including
the PAUSE key (Esc native, Backspace web), in every mode combination, web and
native, and fails on any chord a page cannot intercept (Ctrl+W/T/N/Q/Tab,
Ctrl+PgUp/PgDn, Ctrl+F4, Ctrl+Shift+W/T/N/Q/P/Tab, Alt+F4,
Ctrl/Alt+Backspace). The Alt chords that remain with P4's 8-direction
SHIFTER on Left Alt (Alt+Tab with web P1's 8-direction YELL, Alt+Space,
Alt+\`, Alt+F5-F8, Shift+Alt, Alt+Backspace) are listed there by name as the
known set, so a new one fails; Left Alt stays a default (PR #256). The page
prevents the default on every key it hands to the game, and
`tests/e2e/wasm-game.spec.js` ("Modifier chords across seats") checks that
P2's Delete reaches the game and looks up, that Alt with P4's keys keeps its
default prevented, and that a player who rebinds to Ctrl still gets 1/2/3
with the default prevented. Playwright's synthetic keys never trigger
browser shortcuts, so the unit walk is the real guard.

**Native keyboard grab.** With two or more of this machine's seats on the
keyboard, a running mission grabs the keyboard
(`og::input_native::set_mission_keyboard_grab`, rule
`want_keyboard_grab(in_mission, keyboard_seats)` in
`include/openglad/interface/native_input.h`), so OS shortcuts such as
Alt+Tab or the Super key do not steal a player's keys, and Alt+Tab does not
minimise a fullscreen window while grabbed. One keyboard seat never grabs.
The grab is released before the PAUSED menu opens, at mission end and on
entry to any menu screen, so Esc is always the way out. On GNOME Wayland the
first grab shows a one-time "wants to inhibit shortcuts" dialog. On stock
macOS the grab does nothing, and Ctrl+arrows (P1 FIRE on Left Ctrl, P2 on
the arrows) can still open Mission Control (a follow-up issue). The web
build never grabs.

### C.3 Gamepads, touch, terminals

- **Gamepads** (`src/interface/input/input.cpp`): button 0 is SPEC 1,
  1 FIRE, **2 SPEC 2**, 3 YELL, 4 SHIFTER, 5 SWITCH, **6 SPEC 3** (on pads
  with seven or more buttons). With the setting off button 2 changes the
  special, as before.
- **Touch overlay** (`web/shell.html`, `src/platform/emscripten/web_touch_bridge.cpp`):
  three buttons beside FIRE. With the setting on, each button is the
  first player's slot picture itself, a rounded square with the slot's
  label (`1`, `S1`, `L7`) as a small badge in its corner: a red edge when
  the magic is short, a white edge while the key is held, dimmed when the
  slot is locked, turned off by the level, or empty (an empty slot shows
  only its label). The ALT button latches SHIFTER, so ALT then 1/2/3 casts
  the second row, and the buttons switch to the second row's pictures at
  once. While the buttons are on, the first player's pane draws no strip;
  turning them off brings it back. With the setting off they read `SPC` and
  `SP+` and the third hides, as before.
  The game hands the page what to show: once per HUD frame it reads the
  first player's three slots with the same code the strip uses
  (`resolve_belt_cells` in `src/interface/score_panel.cpp`) and passes
  them, with the exact picture pixels, through
  `og::ui::publish_belt_touch` (`include/openglad/interface/belt_touch.h`)
  to the bridge, which sends the page an `openglad-belt` event only when
  something changed, and each picture's pixels only once. The page never
  works out a slot's state itself. Native builds have no listener, so they
  do none of this.
- **Text client**: it has no in-game keys. Its belt is the ABILITIES page
  (E.2).
- **Curses client**: see `docs/ncurses-client.md`, "New Ability Select".

### C.5 Moving saved keys to the new keyboard

The game writes the whole controls block back to cfg on every start, so
every returning player has the old factory keys saved. They move once.

- **The marker**: `controls/default_keys_version` (integer;
  `kDefaultKeysVersion = 2` in `include/openglad/interface/input_mappings.h`;
  1 = New Ability Select, 2 = web P2 LOOK UP on Delete).
  It is not in the shipped cfg and has no default, so a cfg without it is
  migrated.
- **The rule**, in one pure function, `og::input::migrate_seat_keys`
  (`src/interface/input/input_mappings.cpp`), per seat and mode, against
  the whole old table at once:
  1. **Classify** every slot. A stored value equal to the old factory value
     for that seat, mode and slot (or, on web for player 1, the old browser
     value) is *factory*; anything else is *custom*. A custom old
     change-special key (slot 11, read straight from cfg because slot 11 is
     no longer loaded) becomes the seat's custom SPEC 2.
  2. **Assign** together: every factory slot takes the new factory value;
     every custom slot keeps its key.
  3. **Collide**: a factory slot whose new key is already a custom key of
     this seat, or of any other seat in either mode, is left unbound. The
     player's choice wins; the binding panel shows `--` and REMAP binds it.
  4. **Stamp** the marker and save. On web the save is followed by
     `sync_filesystem()`, so the marker reaches browser storage; a lost
     marker would move a later deliberate rebind back.
- Doing it slot by slot does not work, because the old and new tables hand
  keys around inside one seat (P2's Right Shift moves from SHIFTER to FIRE,
  P4's B from LOOK UP to SHIFTER, P4's G from YELL to FIRE).
- Results: a pristine four-player cfg reaches exactly the C.2 tables in
  both modes. A player who had bound YELL to `2` keeps it and gets SPEC 2
  unbound. A custom change-special key becomes SPEC 2.
- **Saved mappings** (the named mapping library): each entry is migrated
  by the same function. A factory-named entry (WASD, ARROWS, IJKL, TFGH)
  gets the new defaults for slots it does not store; a CUSTOM entry takes
  its old slot-11 key as SPEC 2 and leaves SPEC 3 unbound. Mapping names
  still come from the four movement keys, which did not move.
- The migration runs inside `load_player_control_settings_from_cfg`
  (`src/interface/input/input_state.cpp`) before any REMAP or mapping save
  can write a stale entry. The old web-only migration and its
  `controls/web_default_keys_version` counter are retired; old cfgs keep
  that key, unread.
- **Version 2** (#391): `migrate_seat_keys` takes the version the keys were
  stamped with. Version 0 runs the rule above. Version 1 reads the New
  Ability Select tables as the old factory ones; they equal today's except
  web P2 LOOK UP, which was Right Ctrl (`kV1WebP2LookupKey`). Step 3 is
  checked only for slots whose key actually moves, so a native 1 -> 2 run
  changes nothing but the stamp, and on web P2's LOOK UP moves to Delete, or
  is left unbound when Delete is some seat's own key. The mapping library
  migrates each entry from its own `mapping{K}_default_keys_version`.

### C.6 How a press reaches the server: the client resolves, the wire carries the entry

The client turns a key press into an entry; the server checks it and
casts. The server never reads a layout.

- **The resolver**: `og::sim::belt::fold_belt_keys`
  (`include/openglad/gameplay/belt_input.h`, `src/gameplay/belt_input.cpp`),
  shared by the SDL client, the curses client and the touch overlay. Rules:
  1. The SPEC key that goes down becomes the active key. Its entry is read
     once, at the press, from the belt slot for that key and the SHIFTER
     state at that moment, and held. Letting go of or pressing SHIFTER
     during the hold does not change the running ability.
  2. Pressing another SPEC key during a hold is a fresh press, never a
     continued hold, so one tap is still one cast (issue #228).
  3. When the active key is let go while another SPEC key is still down,
     that key becomes active as a fresh press.
  4. An empty slot sends `0x07`; a locked slot sends its entry, and the
     server says which level opens it. Every refusal message comes from one
     place, the server.
- **Where it runs** (`src/interface/sdl_context_services.cpp`): with the
  setting on and the seat driving a hero, the belt is
  `resolve(...)` over the seat's own hero in the display world
  (`bytes_for`), and the fold writes Special and `belt_cast` and forces the
  change-special action off. With the setting on and no hero (menus, a dead
  seat), the classic fold with `belt_cast = 0`. With the setting off, the
  old fold: slot 9 is Special, slot 17 is change special.
- **Every place that merges inputs keeps the byte**: the render-frame latch
  (`src/platform/sdl/game_loop.cpp`: a nonzero byte replaces the pending
  one, so a tap released between two ticks keeps its entry), the server's
  late and exact-tick inputs and the activity check
  (`src/gameplay/game_server.cpp`), the in-process transport's
  round-trip check (`src/gameplay/net_transport_inprocess.cpp`), replay
  frames (`src/gameplay/replay.cpp`), and `InputState::clear()`.

### C.7 The server's check

`sim_arm_belt_entry` (`src/gameplay/sim_input_handler.cpp`), on every tick
with Special pressed or held and a nonzero `belt_cast`, calls
`og::sim::belt::validate_cast` with the hero's live class, level and the
New Specials setting:

- malformed, `0x07`, or an entry that is not named or not in play: says
  **NO ABILITY THERE** (on the press only), casts nothing;
- an entry the level has not opened: says **LEVEL N NEEDED**, casts
  nothing;
- otherwise: sets the walker's slot and alternate from the entry and casts
  through the unchanged cast path. The physical Shift key means nothing to
  a belt cast.

`belt_cast = 0` runs the old code exactly, including the 2002 line that
sets `shifter_down` from the Shift key. A refused entry never falls back to
the stale `current_special`. Left untouched on purpose: the hidden-hero
branch (a dug-in skeleton answers any SPEC key with its pinned slot), the
cycle block, the frozen branch, `walker::special`, the costs, the bots and
every pack. A client that lies about its layout can at most cast an ability
its hero already has open.

### C.10 Versions

Protocol 21 to **22**, world snapshot 17 to **18**, replay 23 to **24**,
save file (GTL) 20 to **21**, all in one commit. The input payload grows
from 21 to 25 bytes (one `belt_cast` per local seat). See
`docs/ARCHITECTURE.md`, the "Protocol v22" paragraph.

### C.11 Parity

The goldens did not move. The files the simulation reads for a cast
(`walker_specials.cpp`, `specials_view.cpp`, `living.cpp`, `stats.cpp`,
every pack) are unchanged, and the parity runner never calls
`sim_process_player_input`, where the new code sits. One mutation pin in
`tests/parity/scenario_table.h` (`kMut_snapshot_dirty`) moved by six lines
because the guy-snapshot serializer above it grew; it was re-pointed and
mirrored to the old game's table with no capture (the ledger entry is in
`tests/parity/golden/DRIFT_LEDGER.md`).

---

## D. Persistence

- **The hero**: `guy::belt`, six bytes (A.2). The copy constructor copies
  it; `statscopy` deliberately does not, so TRAIN and renaming never touch a
  layout.
- **The save file** (`src/resources/save_data.cpp`, GTL 21): the six bytes
  live at hero record offset +52..+57, the bytes that used to be reserved
  zeros. An untouched hero is byte-identical on disk. A version-20 save
  loads with the class default.
- **The wire**: `LobbyCharacterData`, `InitialSetupGuyData` and
  `GuySnapshot` each carry the six bytes, appended last. Because the belt
  rides every session, none of the places that rebuild a hero from a wire
  struct can drop it (the picker's lobby client, the network lobby client,
  the curses client, the dedicated server, the mission roster, the Base
  Camp display hero, and `collect_initial_setup_guys`).
- An ABILITIES edit changes the hero's lobby data, so it clears this
  machine's READY the way training does, and a joiner's edit reaches the
  host's roster and comes back intact.
- **Validation**: bytes mean nothing until `resolve` reads them, and the
  server checks every cast on its own, so a crafted layout gains nothing.

---

## E. Screens

Every label and every edit rule comes from one model in
`og::ui` (`include/openglad/interface/ui/picker_common.h`:
`ability_page`, `ability_place`, `ability_swap`, `ability_clear`,
`ability_reset`, `ability_status_line`, `belt_hud_line`,
`binding_panel_action_rows`). The SDL screen, the text page and the curses
page draw from it. Placing an ability that is already on a key swaps the
two instead (one ability, one slot). A locked ability can be placed.

### E.1 ABILITIES (SDL)

- **Door**: a 21st row on TRAIN, `ABILITIES`, in the gap between ACCEPT
  and SELL. With the setting off it shows dimmed, so players can still find
  it (question K.8). Dimmed is the menu's one rule for an inert row: the
  darker face every dimmed door uses (`vbutton::vdisplay`).
- **HIRE's door**: `ABILITIES` on HIRE's bottom row (BACK | ABILITIES |
  the team picker, shown with more than one player | HIRE ME, one gap
  between each; solo, HIRE ME takes the team picker's place). HIRE ME is
  the tall face, 58x28, beside 20-high BACK, ABILITIES and team picker
  (110 wide, to fit "HIRING FOR TEAM 4"); the rewire moves it every frame
  as the team picker shows or hides. It opens the
  TRAIN screen's DETAILS.. page (`create_recruit_detail_menu`) for the
  class HIRE shows, as the level-1 recruit HIRE ME would hire: no PROMOTE,
  nobody hired. BACK returns to HIRE on the same class. DETAILS exists in
  both settings, so the door is lit in both, and the HIRE box carries the
  class's description only (`og::ui::hire_description`).
- **Screen**: `MenuScreenId::Abilities`, `src/interface/ui/abilities_screen.cpp`,
  layout constants in `src/interface/ui/picker_sdl_defs.h`. Title and
  "Class, level N" at the top; the six slots in two columns (1 | S1, 2 |
  S2, 3 | S3); the bench below (four abilities a page; a fifth or more
  pages, which only a class pack can reach, since no core class benches
  more than four); a status line; BACK and DEFAULTS.
- **Faces**: every slot and bench row is a 20-pixel face carrying the
  ability's picture in the same dark slot the HUD draws, with its name
  beside it (section I.5). A locked ability shows the grey copy of its
  picture and its name in the grey ink, with the level it opens at; an
  empty slot shows the bare slot and `--`. The selected face has a red
  ring inside its edge, drawn over the slot so it shows all the way
  round. The name starts four pixels after the slot; twenty letters fit on
  a line beside it; a longer row (a level-1 Barbarian's `2 EXPLODING
  BOULDER L4`) breaks at its last space onto a second line inside the face.
- **Use**: click a slot to select it, then click a bench ability to put it
  there (the old one goes to the bench), or another slot to swap the two.
  Click the selected slot again, or right-click any slot, to empty it.
  DEFAULTS puts the class default back. Keyboard: arrows, Enter or FIRE,
  Esc for BACK.
- **Commit**: the screen edits a copy and BACK writes it to the hero once:
  one roster push and one autosave a visit. A hero this machine does not own
  opens read-only. A host's GO while the screen is open starts the match
  and drops the edit.

### E.2 Text client

`equip N` on the roster (`src/platform/text/text_picker.cpp`) prints the
page and takes `set <1|2|3|s1|s2|s3> <bench#>`, `swap <a> <b>`,
`clear <slot>` and `default`; a blank line saves. It refuses in words when
the setting is off or the hero cannot train.

### E.3 Curses client

`e` on a roster row (`src/platform/curses/curses_picker_client.cpp`) opens
the page: pick a slot, then an ability or another slot; `c` empties the
highlighted slot; Done or Esc saves. In a mission, Esc then `a` opens the
same page for the body this player drives (section E.6).

### E.4 The in-game HUD

`src/interface/score_panel.cpp`. With the setting off the old `SPC:` row
is drawn, unchanged, and no picture is ever loaded. With it on, every pane
shows a row of three slots, one for each SPEC key, with the key's label
under each slot (`1`, `2`, `3`). While SHIFTER is held the same three slots
show the SHIFTER row and are labelled `S1`, `S2`, `S3`. No ability name is
written in play.

- The slot is the same everywhere: 18 pixels square, a dark grey fill with
  a black frame, the 16-pixel picture inside. With the grey OVERLAY box on,
  the slots sit on the box; with it off (the shipped look) they sit
  straight on the floor. Either way the picture is on the same dark ground.
- The slots are see-through, so the game shows under them: the dark fill
  at 44 % (only where the picture is empty, never under it), the picture
  and the black frame at 75 %. The red and white frames and the key labels
  stay solid, because a half-strength red over grass is brown and
  half-strength labels cannot be read. On average a slot is a little over
  half see-through. This is the same for every number of players. (With
  the grey OVERLAY box on, the box is solid, so the slots show the box
  through them.)
- **Split screen draws the strip at half size.** With two, three or four
  players on one screen, in every pane of every layout, the strip is drawn
  half as wide and half as tall, with the same pictures pixel for pixel:
  the strip's pixels become the screen's own, smaller than the game's. At
  the default 640x400 window one picture pixel is one screen pixel, at
  1280x800 two. The key letters shrink with it (five screen pixels tall at
  640x400). One player keeps the full-size strip, and so does every player
  of a network game, who each have their own screen. A window smaller than
  640x400 would have fewer screen pixels than the pictures have, so there
  the strip stays full size too.
- What a slot says (section I.4): ready, the picture as drawn; not enough
  magic, a red frame and a red key label; the key held down, a white frame;
  locked, the grey copy of the picture labelled with the level it opens at
  (`L7`); empty, the bare slot; specials turned off by the level, the grey
  copy.
- The strip sits where the text rows were: above the score and XP lines in
  one- and two-seat panes (and the tall first pane of three), at the bottom
  of four-seat panes (and the other two of three). The frozen-time and
  seat-timer cells sit above it. At half size it takes 29 by 13 canvas
  pixels, so the 100-pixel columns of a three-seat inset layout and the
  104-line two-seat inset carry it too.
- It never covers the message feed or the radar. A pane too small even for
  the half strip (the 72- and 80-line insets) falls back to the old text
  rows, cut to their key labels when they must be.
- On a phone or tablet with the touch buttons on, the 1, 2 and 3 buttons
  are the slot pictures themselves (the key in a corner, the state on the
  button's edge), so the first player's pane draws no strip. Turning the
  touch buttons off brings the strip back. Other players on the same
  screen (with gamepads) keep theirs.

The curses status line shows `1:TELEPORT 2:WARP SPACE 3:FREEZE TIME`
(names cut to 10) and the `S1:` row while SHIFTER is held; the text and
curses clients have no pictures.

### E.5 Binding panels, REMAP, help, DETAILS

- **Binding panels** (the seat's player screen and the PAUSED menu): with
  the setting on, eight rows (FIRE, SPEC 1, SPEC 2, SPEC 3, SHIFTER, YELL,
  LOOK UP, CHAR SW); off, the old seven, with the change-special row reading
  slot 17.
- **REMAP** (`src/interface/ui/picker.cpp`, `remap_prompts`): on, 12
  prompts in 4-direction (UP, RIGHT, DOWN, LEFT, FIRE, SPEC 1, SPEC 2,
  SPEC 3, YELL, SWITCH CHARACTER, SHIFTER, LOOK UP) and 16 in 8-direction;
  off, the old 11 and 15, with SPECIAL SWITCH binding slot 17. The button
  reads **REMAP ALL** since #358.
- **One key** (#358): every line of a binding panel is a key cell; a click
  (or Enter) asks for that key alone, with the same prompt label as REMAP
  ALL. A key already on another slot of the same seat in the same mode
  leaves that slot (it shows `--`); other seats keep their keys
  (`og::input::bind_player_key`). The key that opened the prompt and a held
  key's auto-repeat never answer it. Both paths save through
  `save_player_keys_after_remap`.
- **Prompt summary** (#383): the control summary under a REMAP prompt wraps
  at 48 characters per line, breaking after a `/`, and long key names have
  short forms (PrtSc, ScrLk, NumLk, Ins, Del, PgUp, PgDn, `KP x`).
- **HELP > Controls** (`src/interface/ui/help.cpp`): a key table for each
  setting.
- **HELP > Classes**: a paragraph explaining SHIFTER under the belt. Each
  class's ability lines are generated from the live catalog (with the
  setting on the second forms read `Also:`, off `SHIFTER:`);
  `cfg/help_classes.txt` holds only the prose around them.
- **DETAILS** (`og::ui::detail_page`): with the setting on the alternate's
  line reads `Also: <name>` instead of `Shift: <name>`.

### E.6 ABILITIES and KEYMAP from the PAUSED menu (#376, #388)

- **Doors**: the pause player screen's bottom band carries ABILITIES and
  KEYMAP (left of REMOVE PLAYER); the Base Camp seat screen carries KEYMAP
  beside TEAM. ABILITIES is dimmed with the setting off or when the seat has
  no living body. KEYMAP is hidden for a phone touch seat.
- **Whose layout**: the body the seat drives now (after a Switch or a
  possession, that body), not the roster hero. A body with no saved layout
  opens read-only with `THIS BODY HAS NO SAVED LAYOUT.`
- **Commit**: the screen edits a copy. BACK sends one BeltChange (protocol
  23, see `docs/ARCHITECTURE.md`) only if the layout changed and the same
  body is still driven; if control moves to another body while it is open,
  the screen closes and nothing is sent. The seat's held SPEC latch is
  cleared, so a key held through the pause casts nothing until it is
  pressed again. The server applies the change while paused; RESUME shows it
  on the HUD.
- **KEYMAP**: the keyboard picture (F.5) of every local keyboard seat, this
  seat first.
- **Curses**: Esc opens `[r]esume [a]bilities [k]eys [q]uit mission`. `a`
  opens the ABILITIES page for the driven body (same rules; the edit is sent
  only if the same body is still driven when the page closes), `k` a KEYS
  page listing the binding panel rows. The session keeps advancing and the
  pause is re-asked every 20 s while either page is open.
- **Text client**: unchanged (no pause menu).

---

## F. The CHANGELOG! page and the keyboard picture

### F.1 Shape

`include/openglad/interface/ui/changelog_screen.h` holds a list of entries,
each an id, a title, up to six lines of at most 48 characters, and an
optional picture. Entry 1 is this change:

```
The controls have changed! Up to three specials
can now be invoked directly, and up to 3 more
via the Shifter key. The mappings below are the
default mappings. You can restore the old
controls in Game Settings -> UI FX.
```

with the keyboard picture under it. Later announcements add entries with
higher ids.

### F.2 When and where

- **Marker**: cfg `ui/changelog_seen`, the highest entry id shown. Not in
  the shipped cfg, so a new install sees the page once too (question K.3).
  After the page, the marker is set, cfg is saved and, on web, flushed to
  browser storage (`sync_filesystem()`), so a reload does not show it again.
  It is never keyed on the version string, which changes every commit.
- **When**: not at startup. The game starts on the main menu as it always
  did. The first time the player presses **BEGIN NEW GAME** or
  **CONTINUE**, the main menu fades out, the page fades in, and OK fades it
  out into the screen that button opens (the company name for BEGIN NEW
  GAME, base camp for CONTINUE), exactly as if the page had not been
  there. Each edge fades once: no double fade, no extra black. Only those
  two main-menu buttons show it; other ways into base camp (LOAD, a
  host's GO, coming back from a level) do not.
- **Where**: `SdlPickerClient::present_menu` (`src/interface/ui/picker.cpp`)
  calls `picker_show_pending_changelog()` after the main menu exits on one
  of those two clicks. The web build runs the same picker, so the page
  shows at the same point there. The marker is the only "already shown"
  state; there is no per-process flag.
- **Screen**: `MenuScreenId::Changelog`, engine-hosted, with one OK button
  (Return, Escape and Space close it too; Backspace on web). The Return
  that pressed the main-menu button, if still held, does not close it: the
  page waits until Return, Escape and Space have all been seen up before
  any of them counts, and the key that closes it does not reach the next
  screen.

### F.3 Never in the way of a test or a headless run

The demo and every non-interactive session skip it. Under `TESTING` it only
writes a trace unless a test forces it on with its own flag. The web tests
set `window.__opengladSkipChangelogForTests` before the page loads (the
older `window.__opengladSkipIntroForTests` skips it too); one web test,
`tests/e2e/wasm-changelog.spec.js`, boots without it, checks the page is not
up on the main menu, clicks CONTINUE, clicks OK, reloads and checks the
page stays away, reading `window.__opengladChangelogState` (`none`, `open`
or `seen`).
`openglad_text`, `openglad_server` and `openglad_curses` never reach the
SDL picker.

### F.4 The keyboard picture

- **Drawn by**: one renderer, `og::ui::keyboard_picture`
  (`include/openglad/interface/ui/keyboard_picture.h`,
  `src/interface/ui/keyboard_picture.cpp`). It owns the layout (a US
  keyboard: the six main rows, the navigation block and the arrows), the cap
  geometry, the player colours, the hit test (`cap_at`) and the drawing. It
  is SDL-free and fills a 1560x550 RGBA image from a set of seats and their
  keys; a cap two players use is drawn split in both colours and reported,
  and a key with no cap (the numpad, media keys) is reported as off the
  picture. Action names come from `og::ui::binding_panel_action_rows`, like
  the binding panels. The CHANGELOG! page draws it from the factory keys
  (`keyboard_picture::factory_input(web)`); the KEYMAP screen draws it from
  the live keys (F.5).
- **Labels**: DejaVu Sans, the look of the approved picture, without the
  game loading a TrueType font. `scripts/keyboard_picture/generate_glyphs.py`
  (run inside `nix develop`, which carries Pillow and the fonts) bakes every
  glyph the picture prints, at the few sizes it prints them, into the
  committed atlas `pix/keyboard/glyphs.png` and its table
  `src/interface/ui/keyboard_glyphs.inc`; the renderer composes the text
  from them at run time.
- **Kept true**: the `check_keyboard_glyphs` build check
  (`scripts/check_keyboard_glyphs.sh`) fails unless the atlas and the
  generator hash to what the table records; inside `nix develop` it also
  redraws the atlas and table and compares them (CI lanes have no Pillow, so
  they run the hash part only). `tests/unit/test_keyboard_map_asset.cpp`
  checks the renderer: every factory key of every seat, web and native, is
  on a cap and no cap is shared, each bound cap's colour, the action labels
  for both settings, and the `cap_at` round trip.
- **On screen**: smooth-scaled with `video::draw_rgba_scaled` into the band
  under the text (the `kChangelogBand*` constants in `changelog_screen.h`),
  which has the picture's own shape and sits inside the panel's frame lines.
  If that drawing cannot happen (a tiny window) the band says
  `Keyboard picture: see HELP > Controls`. At 640x400 the picture is soft
  but readable (question K.4); the six lines carry the rule in words either
  way.
- `og::io::read_rgba_png` loads the atlas; the old PNG pictures and the
  `scripts/keyboard_map/` pipeline are gone.

### F.5 KEYMAP

`og::ui::run_keymap_screen(seat, host)` (`include/openglad/interface/ui/keymap_screen.h`,
`src/interface/ui/keymap_screen.cpp`) shows the keyboard picture of all of
this machine's keyboard seats, from their live keys, so players can see who
has which key and spot a clash. The title names each seat's key set; under
the picture a legend lists a seat that plays on a pad, a key two players
share and a key with no cap, and on web, when a seat uses Delete, says
`DEL ON A MAC: FN + DELETE`. BACK (Esc) closes it; it sits where the
ABILITIES screen's BACK sits (x=12, y=176, 50x16, clear of the frame). One draw body sits behind
two specs: the Base Camp seat screen's (`MenuScreenId::Keymap`, follows the
host's GO) and the pause player screen's (an overlay that keeps the paused
session pumped). Its tests are in `tests/integration/test_keymap_screen.cpp`.

---

## G. Tests

The main ones, by area (each was shown to fail with the behaviour it pins
broken, then restored):

- Model: `tests/unit/test_belt.cpp` (og_unit_kits): the catalog, both
  default tables for every class, every byte value, resolve, bench, the
  server check.
- Keys: `tests/unit/test_belt_input.cpp` (the fold rules),
  `tests/integration/test_input_keybinds.cpp` (every cell of both tables,
  the collision sweeps, the migration through the loader),
  `tests/unit/test_input_mappings.cpp` (`migrate_seat_keys` against literal
  tables), `tests/integration/test_input_joystick.cpp`.
- Wire and server: `tests/unit/test_input_state_net.cpp`,
  `tests/integration/test_sim_input_handler.cpp`, the game-server and in-process
  transport tests, `og_test_parity`.
- Persistence: `tests/integration/test_save_data_versions.cpp`,
  `tests/integration/test_belt_persistence.cpp`, the lobby and server
  tests.
- Screens (G.5): `tests/integration/test_abilities_flow.cpp` (the
  `og_test_abilities` group), `tests/integration/test_glad_hud.cpp`, the menu
  layout and options tests, the text and curses client tests,
  `tests/e2e/wasm-touch.spec.js` (the touch buttons' pictures, ALT, the
  red/white/dim looks, the first player's strip gone while they are on),
  `tests/e2e/wasm-game.spec.js` (a desktop that never turns the buttons on
  keeps its strip, and it comes back when they are turned off), and the
  CHANGELOG! flow and web test.
- Pictures: `tests/unit/test_ability_icons.cpp` (every core ability names a
  picture that loads, the palette and the drawing rules, the grey copy,
  the cache and the generic tile); `scripts/check_ability_icons.sh` (the
  grids and the committed pictures agree, at build time); the ABILITIES
  flow tests read the picture back off each face, pixel for pixel, ready
  and locked.

## H. Pictures for review

`scripts/media/capture_ability_select.sh` makes every still a reviewer
needs (the HUD at 1, 2 and 4 seats on and off, the HUD as the window
shows it, over grass with the radar, at 1, 2, 3 and 4 seats and at
640x400, 1280x800, 1920x1080 and 480x300, read back from the screen
because the half-size strip is drawn there and not on the 320x200 canvas,
a two-seat game with the radar, the ABILITIES screen, the
UI FX row, the CHANGELOG! page at 640x400, 1280x800 and 1920x1080, and the
pictures' own sheets from section I.7) into
`build/media/ability-select/`, and checks them at the end with
`scripts/media/verify_media.py --set ability-select`. Pictures go to the
openglad/openglad-screenshots repository, never this one.

---

## I. Icons

Every ability has a small picture. With New Ability Select on, the HUD
shows the pictures on the keys instead of the names, and the ABILITIES
screen shows each picture next to its name. The technical details of the
format live in `scripts/ability_icons/README.md`; this section says what
was decided and why.

### I.1 The picture

- **16 by 16 pixels**, one frame, drawn in the game's own palette. That is
  the size of a character in the game (a footman, a mage, an orc), so a
  picture reads as "one thing" at the game's scale, and a row of three
  fits every pane, including a four-player split screen.
- **Colours**: only the steady colours of the palette. Never the bands
  the game animates (the picture would flicker), never the team colours
  (a picture shows the ability, not the player's team: a borrowed class
  sprite is drawn in the red of team one, as the title screen shows it),
  never the menu greys.
- **Drawing rules**: light from the top left; the picture never paints
  its own background or border (the slot gives both); one main shape and
  at most one small mark beside it; no more than 16 colours.
- **Show what the ability really does, from the sprite that does it**:
  TELEPORT is the mage inside his tightening blue ring, cut from his own
  sprite sheet; HEARTBURST is the game's own blast; a picture that has no
  sprite of its own (HEAL, HOWL, FREEZE TIME) uses the class's sprite plus
  one plain sign the player already knows (the green of heal numbers,
  sound arcs, an hourglass).

### I.2 How a picture is made and kept honest

Each picture is a text grid, one character per pixel, in
`scripts/ability_icons/grids/<name>.txt`, compiled by
`scripts/ability_icons/generate.py` into `pix/icons/<name>.png`. A grid can
be read and reviewed in a diff, and a picture cut from a sprite starts as
that frame printed as a grid (`--dump`), so it cannot drift into an
invented shape.

The generator refuses a picture that breaks a rule, and the build runs it
(`scripts/check_ability_icons.sh`): too many solid pixels (a filled tile
is a button, not a picture), too much on its border, touching all four
edges, more than 16 colours, a forbidden colour, a grey copy that would
melt into the slot, or a committed PNG that is not its grid.

### I.3 Where the pictures live, and class packs

- The core pictures are `pix/icons/<name>.png`, named after the ability
  (`teleport.png`, `chain_lightning.png`). Abilities with the same name
  share a file. They install, preload on the web and can be re-skinned
  like every other picture under `pix/`.
- A specials row names its picture with one key, `icon = "..."`, and so
  does its `alternate` table, read exactly like a family's `sprite`. A
  class pack ships its own pictures in its `sprites/` folder and names
  them by their full path (`packs/<pack>/sprites/<name>.png`); the
  example pack `docs/modding/examples/emberwisp` does this. Multiplayer
  already sends a pack's whole folder, pictures included.
- The game never reads `icon` while playing: it changes nothing a server
  decides, no replay, no save and no parity result.

### I.4 What a slot shows

A slot is 18 pixels square: a dark grey fill, a black frame, the picture
inside. It is the same slot on the HUD and on the ABILITIES screen, so
every picture is designed against one ground.

| The ability is | The picture | The frame | The key label (HUD) |
|---|---|---|---|
| Ready | as drawn | black | the key |
| Being pressed | as drawn | white | the key |
| Short of magic | as drawn | red | the key, in red |
| Locked | its grey copy | black | the level it opens at, `L7` |
| An empty slot | none | black | the key, dim |
| Turned off by the level | its grey copy | black | the key, dim |

The grey copy is worked out once per picture: black stays black, every
other colour becomes a grey of the same brightness, always light enough to
stand out from the slot's fill. Pressed wins over short of magic; a
locked, empty or turned-off slot never flashes.

An ability with no picture, or whose file will not load, shows the
generic tile, a grey square with a question mark (`pix/icons/_missing.png`);
the slot still shows its key and state, so the ability stays usable and
its name is on the ABILITIES screen. If even the generic tile is missing,
the slot is drawn empty. Never a crash, never a blank HUD. The tests read
which one a slot drew from its trace (the HUD's `belt_cell`, the ABILITIES
face's `abilities_icon`): `icon=` is always the path the ability names
(`none` on the HUD and `(none)` on a face when it names none), and `drawn=`
is the file whose pixels show, the declared one, `icons/_missing.png`, or
`none` when nothing is drawn.

The generic tile is grey and white, so its locked copy differs from the
ready one only in its shade: a dark grey (21) that the grey copy lifts to a
light one (26), so a locked tile reads flat beside a ready one; the key
label's `L7` says the rest.

Pictures are loaded the first time they are needed and kept, in one set
for the whole game that the HUD and the ABILITIES screen share, so a
picture either has shown is never read a second time. A campaign or a
sprite sheet that replaces them makes the game read them again; the set
goes when the game closes. With the setting off none is ever loaded.

### I.5 The ABILITIES screen

Every slot and bench row is a face 20 pixels tall carrying the slot (the
HUD's, as above) at its left and the name beside it. The page shows the
whole belt at once (six faces) and four bench faces a page, so the screen
reads as an inventory: pictures you can pick up and put on a key. A
locked face has the grey copy and its name in the grey ink. Twenty
letters fit on a line beside the slot. A longer row (a level-1
Barbarian's `2 EXPLODING BOULDER L4`) breaks at its last space onto a
second line inside the face; the model's budget of 24 letters always fits
in two lines, and the layout test pins that. The
labels the tests read (`3 SUMMON IMAGE`, `>3 SUMMON IMAGE` while selected)
are unchanged; the `>` is not drawn, the red ring inside the face marks the
selection.

The screen draws each face itself (the rows are hit zones the menu does
not draw), so a click never flashes the bare centred label over the
picture.

### I.6 Not changed

- **The CHANGELOG! page**: none of its six lines mentions the text rows,
  so each stays true; its picture band is smooth-scaled, so no pixel
  picture goes there.
- **DETAILS, the binding panels, REMAP, HELP**: they show keys or prose.
- **The web's touch buttons** now carry the pictures (sections C.3 and E.4): the
  game hands the page the exact pixels it would draw for the first
  player's three slots, and that pane draws no strip while the buttons are
  on.
- **The text and curses clients** keep the names.

### I.7 How a set of pictures is accepted

1. `generate.py --check` passes: every rule above.
2. The artist reads the sheets `generate.py --sheet` writes (each picture
   in the slot, ready and locked, on grass, on the grey box and on an
   ABILITIES face, at 1x, 2x and 4x, with its numbers under it) and notes
   what each one shows and what was redrawn after reading.
3. Someone else names every picture from a shuffled sheet with no names; a
   picture they cannot name, or name as another ability, goes back.
4. Every picture said to be cut from a sprite is compared with that frame.
5. The pictures are read again in the real HUD and ABILITIES captures
   (`scripts/media/capture_ability_select.sh`, whose `icons` step also
   writes the sheets) before the stills are published.

---

## K. Questions for the maintainer, with the answers taken

1. **Archmage bench**: MIND CONTROL, the last to open (level 10), starts on
   the bench. The other choice was TELEPORT MARKER.
2. **One default keyboard for both settings**: with the setting off the
   old roles sit on the new keys. A second table that brings back Left Alt
   and Tab when off would split RESET, the mapping library and the
   migration in two.
3. **New installs see CHANGELOG! once too.**
4. **Small windows** show the keyboard picture softer rather than split in
   halves.
5. **8-direction odd keys** as in C.2 (P4: FIRE G, SWITCH =, YELL 7,
   SHIFTER Left Alt, LOOK UP unbound; P1: LOOK UP Tab).
6. **No ABILITIES from the PAUSED menu** mid-mission: an edit there would
   need a new client-to-server message. Left for a follow-up.
   Superseded by #376: the BeltChange message carries it (section E.6).
7. **The compact HUD cuts names** to 12 letters in four-seat panes rather
   than adding short names to every pack. Superseded: the HUD now shows
   pictures, not names (section I); the cut is kept only for the panes too
   small even for the half-size pictures, which fall back to the text rows.
8. **The TRAIN door shows dimmed with the setting off**, not hidden.
9. **Web P2's look-up stays on Right Ctrl** (C.2, chords across seats).
   Superseded by #391: web P2's look-up is Delete (C.2, "Web player 2").
