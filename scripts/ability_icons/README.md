# Ability icons

Every ability has a 16x16 picture. With New Ability Select on, the HUD draws
it in a slot above the ability's key, and TRAIN > ABILITIES draws it beside
the ability's name. The pictures are drawn here as text grids and compiled
into the PNGs the game loads.

```
scripts/ability_icons/
  generate.py              grids -> PNGs; --check; --sheet; --dump; --self-test
  grids/<name>.txt         one core icon per file
  pack_grids/<pack>/       a class pack's icons (PACK_GRIDS in generate.py
                           names where each pack's PNGs go)
pix/icons/<name>.png       the committed core icons (16x16, no sidecar)
pix/icons/_missing.png     the generic tile for an ability with no icon
```

The file name is the ability's name in lower case with spaces turned into
underscores: `teleport.png`, `teleport_marker.png`, `chain_lightning.png`.
Abilities with the same name share one file (the mage and the archmage both
say `icon = "icons/teleport.png"`).

## Using an icon

A specials row names its picture with `icon`, and so does its `alternate`
table, resolved like a family's `sprite`:

```lua
{ id = "teleport", name = "TELEPORT", mp_cost = 10,
  alternate = { name = "TELEPORT MARKER", icon = "icons/teleport_marker.png" },
  cast = teleport, icon = "icons/teleport.png" },
```

A pack puts its PNG in its own `sprites/` directory and names it by its
virtual path, `icon = "packs/<pack_id>/sprites/<name>.png"`
(`docs/modding/examples/emberwisp` does this for FLARE BURST). An ability
with no `icon`, or whose file does not load as one 16x16 picture, draws
`_missing.png`.

## The grid format

```
# TELEPORT (mage, archmage): the mage inside the tightening blue ring, cut from mage.png frame 13
# legend: one character = one palette index (pix/openglad.gpl)
k = 74
a = 42
b = 45
c = 16

................
......kkkkk.....
.....k.....k....
(16 rows of 16 characters in all)
```

- Lines starting with `#` are comments. The first one names the ability and
  says what the picture shows and what it is cut from (the sheet and the
  frame).
- `X = NNN` lines are the legend: one character for one palette index. A
  legend letter is never `#`, `.`, `=` or a space.
- Every other non-blank line is a row: exactly 16 rows of exactly 16
  characters. `.` is always index 0, transparent.
- `# edge: ok` waives the four-edges gate for that grid (a picture that must
  fill the tile, such as SUPERNOVA). The contact sheet prints the waiver.

## Palette rules

The PNG carries the full engine palette (`pix/openglad.gpl`); index 0 is
transparent.

| Use | Indices |
|---|---|
| Outline, shadow | 16 (black), 17 |
| Greys, bone, steel, stone | 16-31 |
| Red | 40-47 (32-39 is the dull red) |
| Green (heal numbers 56) | 48-63 |
| Blue (the teleport ring is 74; stands in for the water band) | 64-79 |
| Yellow | 80-95 |
| Magenta, violet | 96-111, 192-207 |
| Cyan | 112-127 |
| Wood, leather | 128-143 |
| Pink | 144-159 |
| Dark green | 160-167 |
| Fire, still | 232-239 |
| Skin | 240-247 |

Never used: 1-15 (the menu greys), 168-191 (made at run time), 208-231
(rotated every frame: an icon painted there would flicker; fire goes to
232-239, water blues to 64-79) and 248-255 (the team band; a sprite cut from
a team-coloured sheet has its band fixed to the red ramp, `40 + (255 - p)`,
which `--dump` does for you).

## The gates

`generate.py` refuses a grid, and the build fails, when:

| Gate | Limit |
|---|---|
| Opaque pixels | more than 153 of 256: a filled tile is a button, not a picture |
| Opaque border pixels | more than 24 of the 60: the edge belongs to the slot |
| Edges touched | all four, unless `# edge: ok` |
| Colours | more than 16 non-zero indices |
| Forbidden bands | any of 1-15, 168-191, 208-231, 248-255 |
| Locked contrast | fewer than half of the locked copy's interior pixels at least 30 luma from the slot fill (grey 18) |

The locked copy is what a slot shows for an ability the hero's level has not
opened: index 0 stays transparent, 16 stays black, and every other index
becomes the grey of its brightness on 21..31 (`og::ui::locked_icon_lut`).

## Drawing

1. Show what the ability does, from the sprite that does it: start from the
   frame the ability spawns or the class plays, with
   `python3 scripts/ability_icons/generate.py --dump pix/mage.png 13 > grids/teleport.txt`
   (the team band comes out red, the cycled fire band on its still copy;
   `--at X,Y` places the frame), then edit by hand and say what you changed
   in the header.
2. Light from the top left. A sprite cut as it is keeps its own edge; add a
   black outline only where the picture is lost on the dark slot. Drawn
   signs (a cross, a burst) carry one; sparks and specks do not.
3. Keep to a 14x14 area, one clear pixel each side, unless a ray or a speed
   line needs the edge. One main shape and at most one small mark.
4. Never paint a background or a border: the slot (a dark grey 18 cell with a
   black frame) supplies both.

## Commands

```
python3 scripts/ability_icons/generate.py                    # write every PNG, print each structure line
python3 scripts/ability_icons/generate.py --check            # what the build runs; writes nothing
python3 scripts/ability_icons/generate.py --sheet build/media/ability-select [--names heal,charge]
python3 scripts/ability_icons/generate.py --dump pix/mage.png 13 [--at 0,-1]
python3 scripts/ability_icons/generate.py --self-test        # every gate on a grid built to break it
```

`--sheet` writes the acceptance sheets:

- `icons-contact.png`: every icon at 4x with its name and structure line
  (`opaque 134/256  border 5/60  edges LR  colours 5 ...`) and OK or the gate
  it fails;
- `icons-cells.png` (2x; `-1x` and `-4x` beside it): per icon, the slot ready
  and locked on grass (the HUD with no box), on the box grey (the HUD with
  the box), and the ABILITIES face ready and locked. This is the sheet a
  batch is accepted on;
- `icons-floors.png` (2x; `-1x` beside it): the bare icon on grass, stone,
  dark stone and cobbles, ready and locked;
- `icons-blind.png` with `icons-blind-key.txt`: the slots on grass in a
  shuffled order with no names, for a second reader to name each one.

The build runs `scripts/check_ability_icons.sh` (`--check`) before
`og_unit_data`, and `tests/unit/test_ability_icons.cpp` loads every icon
through the game's own loader and checks the same gates.
