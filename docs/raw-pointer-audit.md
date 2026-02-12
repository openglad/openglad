# Raw Pointer Audit (src/)

- Date: 2026-02-12
- Scope: every `*.c`, `*.cc`, `*.cpp`, `*.h`, `*.hh`, `*.hpp` under `src/` (247 files total: 82 project-owned + 165 vendored in `src/external`).
- Method: automated lexical scan (comments/strings stripped) + manual spot-checking of high-risk ownership paths.

## Executive Summary

- Total findings: **4465**
- Ownership pointer findings: **329** (internal 201, external 128)
- Observation pointer findings: **967** (internal only)
- Container pointer findings (C-style arrays): **245** (internal only)
- Callback pointer findings: **1**
- C API boundary findings: **2923** (internal 20, external 2903)

Ownership and manual lifetime management remain concentrated in a small set of gameplay/UI files (`picker.cpp`, `screen.cpp`, `level_data.cpp`, `gloader.cpp`) plus third-party C libraries in `src/external` where raw pointers are expected.

## Note on Prior Modernization Commit `6532a1f`

- Commit exists: `6532a1f Modernize walker creation path and align picker pointer ownership tests`
- Containment check: (not contained in current local branches)
- Interpretation: this repository state (based on `master`) does not currently include that commit; equivalent areas (notably level-data object creation/destruction) still show raw ownership patterns in this audit.

## Priority Ranking (Most Dangerous First)

- **P0** `src/picker.cpp` (44 ownership lines: 187, 189, 191, 193, 202, 207, 233, 243, 246-247, 249, 733, 846, 1027, 1100, 1200, 1640, 1677, 1840, 1880, 1929, 2164, 2298, 2394, 2496, 2557, 3049, 3054, 3099, 3132-3133, 3242, 3422, 3453, 3474, 3561, 3587, 3710, 4371, 4373, 4375, 4377, 4384, 4387) - Many owning raw pointers (`myscreen`, `current_guy`, backdrop pixies, dynamic button lists) with cross-function deletes; highest regression/leak/dangling risk.
- **P0** `src/screen.cpp` (25 ownership lines: 151, 239, 250, 254-255, 259-261, 265-268, 283, 326, 330-331, 335-337, 341-344, 827, 840) - Owning `viewob[]`/`soundp` plus in-loop `delete ob` patterns; high use-after-free risk under iteration order changes.
- **P0** `src/level_data.cpp` (23 ownership lines: 47, 102, 301, 303, 310, 335, 337, 346, 357-358, 455, 488, 541, 553, 565, 577, 583, 589, 595, 1311-1312, 1332, 1342) - `myloader`, `myobmap`, `grid.data`, and multi-list walker deletes are manual and duplicated across load/reset paths.
- **P1** `src/gloader.cpp` (21 ownership lines: 329, 341, 344, 346, 348-351, 778, 780-785, 815, 817, 819, 821, 823, 1133) - `new[]/delete[]` for core stat arrays and polymorphic walker construction via raw pointers.
- **P1** `src/level_editor.cpp` (9 ownership lines: 978, 1087-1088, 1471, 2048, 3070, 3111, 3150, 3277) - Manual deletion of campaign/level/object brush entities during complex interaction loops.
- **P1** `src/save_data.cpp` (6 ownership lines: 57, 80, 185, 280, 430, 443) - Team list entries are heap-owned raw pointers with repeated cleanup paths.
- **P2** `src/external/*` (128 ownership lines across vendored files) - High raw-pointer density is expected; treat as C API boundaries and avoid broad in-place rewrites unless vendoring strategy changes.

## Recommended Modernization Approach

### Ownership pointers -> `std::unique_ptr` / `std::shared_ptr`
- Default to `std::unique_ptr` for sole ownership (`screen` sub-objects, loader-owned arrays, menu-owned objects).
- Replace container-of-owning-raw-pointer patterns with `std::vector<std::unique_ptr<T>>` (or `std::list<std::unique_ptr<T>>` where stable iterators are required).
- Use `std::shared_ptr` only where true shared lifetime is required; otherwise keep non-owning raw observer pointers.
- Estimated effort: **Large (3-5 weeks)** for project-owned code due to lifetime coupling across gameplay loops.

### Container pointers / C arrays -> `std::vector` / `std::array` / `std::string`
- Replace fixed global buffers and stack arrays where practical with STL containers.
- Convert `new[]/delete[]` arrays to `std::vector<T>` and pass spans/views where needed.
- Estimated effort: **Medium (1-2 weeks)**.

### Callback pointers -> `std::function`
- Replace the one remaining raw function pointer callback in `button.h` with `std::function<Sint32(Sint32)>` (or small callable wrapper if allocation-sensitive).
- Estimated effort: **Small (0.5 day)**.

### Observation pointers (non-owning)
- Keep raw pointers where they are observer-only (`walker*`, `screen*`, SDL handles), but document ownership source and nullability contract.
- Optionally introduce `observer_ptr` style aliases/comments for readability.
- Estimated effort: **Medium (2-4 days)** for annotation pass.

### C API boundaries
- Keep raw pointers at SDL/PhysFS/libyaml/libzip/micropather boundaries; wrap boundary calls in narrow RAII helpers where useful.
- Estimated effort: **Small-to-medium (2-5 days)** for wrapper-only hardening.

## File-by-File Breakdown (Project-Owned `src/` minus `src/external`)

| File | Ownership | Container | Callback | Observation | C API | Total |
|---|---:|---:|---:|---:|---:|---:|
| `src/OuyaController.cpp` | 0 | 41 | 0 | 8 | 2 | 51 |
| `src/OuyaController.h` | 0 | 3 | 0 | 0 | 0 | 3 |
| `src/base.h` | 0 | 1 | 0 | 12 | 0 | 13 |
| `src/button.cpp` | 4 | 1 | 0 | 7 | 0 | 12 |
| `src/button.h` | 0 | 0 | 1 | 13 | 0 | 14 |
| `src/campaign_picker.cpp` | 6 | 2 | 0 | 7 | 0 | 15 |
| `src/campaign_picker.h` | 0 | 0 | 0 | 1 | 0 | 1 |
| `src/colors.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/effect.cpp` | 0 | 0 | 0 | 13 | 0 | 13 |
| `src/effect.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/game.cpp` | 0 | 2 | 0 | 8 | 0 | 10 |
| `src/glad.cpp` | 2 | 5 | 0 | 31 | 0 | 38 |
| `src/gloader.cpp` | 21 | 1 | 0 | 11 | 0 | 33 |
| `src/gloader.h` | 0 | 1 | 0 | 10 | 0 | 11 |
| `src/gparser.cpp` | 0 | 1 | 0 | 2 | 0 | 3 |
| `src/gparser.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/graph.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/graphlib.cpp` | 1 | 0 | 0 | 3 | 0 | 4 |
| `src/guy.cpp` | 4 | 4 | 0 | 7 | 0 | 15 |
| `src/guy.h` | 0 | 1 | 0 | 3 | 0 | 4 |
| `src/help.cpp` | 2 | 3 | 0 | 13 | 0 | 18 |
| `src/input.cpp` | 3 | 8 | 0 | 13 | 0 | 24 |
| `src/input.h` | 0 | 2 | 0 | 3 | 0 | 5 |
| `src/intro.cpp` | 18 | 2 | 0 | 4 | 0 | 24 |
| `src/io.cpp` | 5 | 11 | 0 | 27 | 2 | 45 |
| `src/io.h` | 0 | 0 | 0 | 5 | 2 | 7 |
| `src/level_data.cpp` | 23 | 30 | 0 | 37 | 0 | 90 |
| `src/level_data.h` | 0 | 1 | 0 | 9 | 0 | 10 |
| `src/level_editor.cpp` | 9 | 19 | 0 | 77 | 0 | 105 |
| `src/level_picker.cpp` | 8 | 5 | 0 | 11 | 0 | 24 |
| `src/level_picker.h` | 0 | 0 | 0 | 3 | 0 | 3 |
| `src/living.cpp` | 0 | 0 | 0 | 8 | 0 | 8 |
| `src/living.h` | 0 | 0 | 0 | 3 | 0 | 3 |
| `src/obmap.cpp` | 0 | 2 | 0 | 7 | 0 | 9 |
| `src/obmap.h` | 0 | 0 | 0 | 4 | 0 | 4 |
| `src/pal32.cpp` | 0 | 4 | 0 | 7 | 0 | 11 |
| `src/pal32.h` | 0 | 0 | 0 | 7 | 0 | 7 |
| `src/palettes.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/picker.cpp` | 44 | 27 | 0 | 132 | 0 | 203 |
| `src/picker.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/pixdefs.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/pixie.cpp` | 0 | 0 | 0 | 6 | 0 | 6 |
| `src/pixie.h` | 0 | 0 | 0 | 6 | 0 | 6 |
| `src/pixie_data.cpp` | 2 | 0 | 0 | 1 | 0 | 3 |
| `src/pixie_data.h` | 1 | 0 | 0 | 1 | 0 | 2 |
| `src/pixien.cpp` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/pixien.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/radar.cpp` | 3 | 0 | 0 | 7 | 0 | 10 |
| `src/radar.h` | 0 | 0 | 0 | 6 | 0 | 6 |
| `src/results_screen.cpp` | 1 | 7 | 0 | 18 | 0 | 26 |
| `src/results_screen.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/sai2x.cpp` | 0 | 1 | 0 | 17 | 0 | 18 |
| `src/sai2x.h` | 0 | 0 | 0 | 8 | 0 | 8 |
| `src/save_data.cpp` | 6 | 16 | 0 | 4 | 0 | 26 |
| `src/save_data.h` | 0 | 3 | 0 | 1 | 0 | 4 |
| `src/screen.cpp` | 25 | 6 | 0 | 43 | 0 | 74 |
| `src/screen.h` | 0 | 2 | 0 | 18 | 0 | 20 |
| `src/smooth.cpp` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/smooth.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/sound.cpp` | 0 | 0 | 0 | 4 | 0 | 4 |
| `src/soundob.h` | 0 | 2 | 0 | 2 | 0 | 4 |
| `src/sounds.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/stats.cpp` | 0 | 2 | 0 | 10 | 0 | 12 |
| `src/stats.h` | 0 | 1 | 0 | 3 | 0 | 4 |
| `src/test_trace.cpp` | 0 | 0 | 0 | 2 | 0 | 2 |
| `src/test_trace.h` | 0 | 1 | 0 | 3 | 0 | 4 |
| `src/text.cpp` | 0 | 3 | 0 | 37 | 0 | 40 |
| `src/text.h` | 0 | 0 | 0 | 25 | 0 | 25 |
| `src/treasure.cpp` | 0 | 4 | 0 | 7 | 0 | 11 |
| `src/treasure.h` | 0 | 0 | 0 | 2 | 0 | 2 |
| `src/util.cpp` | 0 | 0 | 0 | 5 | 1 | 6 |
| `src/util.h` | 0 | 0 | 0 | 3 | 0 | 3 |
| `src/version.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/video.cpp` | 6 | 3 | 0 | 43 | 0 | 52 |
| `src/video.h` | 0 | 0 | 0 | 6 | 0 | 6 |
| `src/view.cpp` | 2 | 10 | 0 | 47 | 0 | 59 |
| `src/view.h` | 0 | 4 | 0 | 9 | 0 | 13 |
| `src/view_sizes.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/walker.cpp` | 5 | 2 | 0 | 84 | 12 | 103 |
| `src/walker.h` | 0 | 0 | 0 | 28 | 1 | 29 |
| `src/weap.cpp` | 0 | 1 | 0 | 5 | 0 | 6 |
| `src/weap.h` | 0 | 0 | 0 | 0 | 0 | 0 |

### Project-Owned Line References

#### `src/OuyaController.cpp`
- Totals: ownership 0, container 41, callback 0, observation 8, c-api 2
- Container lines: 33, 35, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 57, 60, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 102, 104, 106, 108, 110, 112, 115, 123, 125, 127, 129, 131, 133, 320
- Observation lines: 152, 157, 162, 167, 187, 393, 401, 409
- C API lines: 335, 353

#### `src/OuyaController.h`
- Totals: ownership 0, container 3, callback 0, observation 0, c-api 0
- Container lines: 24-25, 55

#### `src/base.h`
- Totals: ownership 0, container 1, callback 0, observation 12, c-api 0
- Container lines: 84
- Observation lines: 87, 102-105, 230, 337, 344-345, 348, 377, 390

#### `src/button.cpp`
- Totals: ownership 4, container 1, callback 0, observation 7, c-api 0
- Ownership lines: 213, 231, 490, 496
- Container lines: 509
- Observation lines: 21, 23, 322, 352, 475, 478, 520

#### `src/button.h`
- Totals: ownership 0, container 0, callback 1, observation 13, c-api 0
- Callback lines: 130
- Observation lines: 47, 112, 114, 133-134, 138, 146, 148, 150, 153, 161, 172, 182

#### `src/campaign_picker.cpp`
- Totals: ownership 6, container 2, callback 0, observation 7, c-api 0
- Ownership lines: 141, 153, 295, 469, 480, 597
- Container lines: 167, 196
- Observation lines: 30-31, 38, 85, 102, 272, 275

#### `src/campaign_picker.h`
- Totals: ownership 0, container 0, callback 0, observation 1, c-api 0
- Observation lines: 35

#### `src/colors.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/effect.cpp`
- Totals: ownership 0, container 0, callback 0, observation 13, c-api 0
- Observation lines: 47, 152, 163, 267, 278, 373, 439, 561, 571, 575, 592, 612, 668

#### `src/effect.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/game.cpp`
- Totals: ownership 0, container 2, callback 0, observation 8, c-api 0
- Container lines: 27, 53
- Observation lines: 22, 24, 28-29, 64, 122, 143, 164

#### `src/glad.cpp`
- Totals: ownership 2, container 5, callback 0, observation 31, c-api 0
- Ownership lines: 209-210
- Container lines: 719, 721, 727, 737, 747
- Observation lines: 26, 63-64, 66-67, 69-74, 76, 81-82, 85, 88, 92, 200, 372, 428, 435, 446, 453, 463, 468, 473, 499, 522, 649, 716, 731

#### `src/gloader.cpp`
- Totals: ownership 21, container 1, callback 0, observation 11, c-api 0
- Ownership lines: 329, 341, 344, 346, 348-351, 778, 780-785, 815, 817, 819, 821, 823, 1133
- Container lines: 808
- Observation lines: 27, 345, 347, 774, 788, 797, 799, 801, 839, 1123, 1125

#### `src/gloader.h`
- Totals: ownership 0, container 1, callback 0, observation 10, c-api 0
- Container lines: 38
- Observation lines: 29-33, 35-36, 39-41

#### `src/gparser.cpp`
- Totals: ownership 0, container 1, callback 0, observation 2, c-api 0
- Container lines: 138
- Observation lines: 81, 142

#### `src/gparser.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/graph.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/graphlib.cpp`
- Totals: ownership 1, container 0, callback 0, observation 3, c-api 0
- Ownership lines: 67
- Observation lines: 43, 53, 79

#### `src/guy.cpp`
- Totals: ownership 4, container 4, callback 0, observation 7, c-api 0
- Ownership lines: 137, 176, 586, 602
- Container lines: 194, 216, 240, 266
- Observation lines: 28, 503, 505, 584, 587, 600, 603

#### `src/guy.h`
- Totals: ownership 0, container 1, callback 0, observation 3, c-api 0
- Container lines: 49
- Observation lines: 34-35, 47

#### `src/help.cpp`
- Totals: ownership 2, container 3, callback 0, observation 13, c-api 0
- Ownership lines: 52, 364
- Container lines: 39, 445, 782
- Observation lines: 45, 47, 76, 205, 350, 354, 374, 434, 437, 508, 512-513, 553

#### `src/input.cpp`
- Totals: ownership 3, container 8, callback 0, observation 13, c-api 0
- Ownership lines: 394, 865, 1447
- Container lines: 95, 101, 149, 1298, 1301, 1303, 1361, 1416
- Observation lines: 45, 67, 99, 268, 270, 474-475, 523-524, 562-563, 756, 905

#### `src/input.h`
- Totals: ownership 0, container 2, callback 0, observation 3, c-api 0
- Container lines: 167-168
- Observation lines: 206, 213, 269

#### `src/intro.cpp`
- Totals: ownership 18, container 2, callback 0, observation 4, c-api 0
- Ownership lines: 67, 71, 82, 84, 92-93, 129, 132, 137, 140, 145, 148, 153, 156, 184, 187, 192, 195
- Container lines: 36, 50
- Observation lines: 40, 45-47

#### `src/io.cpp`
- Totals: ownership 5, container 11, callback 0, observation 27, c-api 2
- Ownership lines: 399, 421, 719-721
- Container lines: 87, 129, 348, 363, 643-644, 733, 761, 948, 951, 963
- Observation lines: 61, 70, 147, 149, 180, 185, 187, 193, 203-204, 390, 403, 460, 591-592, 638, 688, 690, 725, 727, 754, 784, 825, 871, 895, 945, 966
- C API lines: 59, 68

#### `src/io.h`
- Totals: ownership 0, container 0, callback 0, observation 5, c-api 2
- Observation lines: 26, 34-37
- C API lines: 60-61

#### `src/level_data.cpp`
- Totals: ownership 23, container 30, callback 0, observation 37, c-api 0
- Ownership lines: 47, 102, 301, 303, 310, 335, 337, 346, 357-358, 455, 488, 541, 553, 565, 577, 583, 589, 595, 1311-1312, 1332, 1342
- Container lines: 129, 207, 632, 636, 716, 720-721, 818, 822-823, 825, 927, 931, 933, 935, 1066, 1070, 1073, 1076, 1078, 1193, 1280, 1286, 1409, 1411-1412, 1418, 1420-1421, 1423
- Observation lines: 60, 126, 204, 285, 369, 375, 387, 389, 399, 401, 408, 538, 550, 562, 626, 635, 709, 719, 811, 821, 920, 930, 1033, 1058, 1069, 1227, 1241, 1279, 1367, 1377, 1397, 1410, 1504, 1536, 1568, 1633, 1653

#### `src/level_data.h`
- Totals: ownership 0, container 1, callback 0, observation 9, c-api 0
- Container lines: 98
- Observation lines: 54, 86, 94, 99, 108-111, 121

#### `src/level_editor.cpp`
- Totals: ownership 9, container 19, callback 0, observation 77, c-api 0
- Ownership lines: 978, 1087-1088, 1471, 2048, 3070, 3111, 3150, 3277
- Container lines: 1151, 1568, 1603, 1619, 1701, 1728, 1753, 2602, 2679, 2690, 2713, 2720, 2733, 2767, 2812, 2835, 2863, 2914, 2929
- Observation lines: 69-70, 79-81, 85-86, 91-92, 94, 97, 247, 290, 311, 332, 659, 731, 772, 801, 807, 825, 844, 857-858, 946-947, 952, 964, 967, 1170, 1269, 1310, 1330, 1355, 1378, 1394, 1407, 1429, 1451, 1467, 1578, 1636, 1662, 1709, 1715, 1720, 1937, 1975, 1996, 2077, 2111, 2117, 2135, 2169, 2180, 2184, 2194, 2204, 3043, 3082, 3133, 3237, 3267, 3269-3270, 3282, 3291, 3298, 3305, 3396, 3398, 3930, 4021, 4205, 4209, 4222, 4235

#### `src/level_picker.cpp`
- Totals: ownership 8, container 5, callback 0, observation 11, c-api 0
- Ownership lines: 316, 431, 440, 454, 463, 510, 513, 644
- Container lines: 167, 173, 252, 270, 575
- Observation lines: 41, 49, 52, 66, 89, 176, 180, 183, 239, 290, 297

#### `src/level_picker.h`
- Totals: ownership 0, container 0, callback 0, observation 3, c-api 0
- Observation lines: 25, 28-29

#### `src/living.cpp`
- Totals: ownership 0, container 0, callback 0, observation 8, c-api 0
- Observation lines: 395, 494, 507, 517, 519, 734-736

#### `src/living.h`
- Totals: ownership 0, container 0, callback 0, observation 3, c-api 0
- Observation lines: 32, 34, 37

#### `src/obmap.cpp`
- Totals: ownership 0, container 2, callback 0, observation 7, c-api 0
- Container lines: 232, 246
- Observation lines: 24, 110, 138, 166, 200, 239, 260

#### `src/obmap.h`
- Totals: ownership 0, container 0, callback 0, observation 4, c-api 0
- Observation lines: 31-34

#### `src/pal32.cpp`
- Totals: ownership 0, container 4, callback 0, observation 7, c-api 0
- Container lines: 31-32, 35, 265
- Observation lines: 45, 79, 90, 122, 139, 167, 206

#### `src/pal32.h`
- Totals: ownership 0, container 0, callback 0, observation 7, c-api 0
- Observation lines: 26-30, 32, 34

#### `src/palettes.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/picker.cpp`
- Totals: ownership 44, container 27, callback 0, observation 132, c-api 0
- Ownership lines: 187, 189, 191, 193, 202, 207, 233, 243, 246-247, 249, 733, 846, 1027, 1100, 1200, 1640, 1677, 1840, 1880, 1929, 2164, 2298, 2394, 2496, 2557, 3049, 3054, 3099, 3132-3133, 3242, 3422, 3453, 3474, 3561, 3587, 3710, 4371, 4373, 4375, 4377, 4384, 4387
- Container lines: 70-71, 94, 131, 140, 146, 533, 745, 1135, 1328, 1347, 1386, 2156, 2158, 2549, 2551, 2999, 3203, 3324, 3341, 3356, 3358-3359, 4249, 4269, 4298, 4320
- Observation lines: 48-50, 62, 66-67, 74, 91-92, 97, 99, 168, 215, 484, 552, 616, 626, 629, 631, 729, 848, 930, 974, 1033, 1102, 1353, 1576, 1646, 1658, 1755, 1758-1759, 1764, 1767-1768, 1773, 1776-1777, 1782, 1785-1786, 1791, 1794, 1802-1803, 1805-1806, 1809-1810, 1812-1813, 1816-1817, 1820, 1825, 1886, 1900, 1904, 2068, 2075, 2080, 2085, 2090, 2095-2096, 2098-2099, 2102-2103, 2105-2106, 2109-2110, 2113, 2118, 2123, 2128, 2131, 2134, 2136, 2166, 2205, 2228, 2265, 2300, 2364, 2396, 2459, 2498, 2559, 2599, 2726, 2781, 2783, 2855, 2861, 2866, 2872, 2878, 2887, 2894, 2900, 2907, 2914, 2918, 2924, 2929, 2934, 2940, 2983, 2985, 2997, 3000, 3070, 3108, 3149, 3170, 3187, 3218, 3260, 3353, 3355, 3514, 3527, 3595, 3690, 3699, 3712, 4177, 4182, 4395, 4459

#### `src/picker.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/pixdefs.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/pixie.cpp`
- Totals: ownership 0, container 0, callback 0, observation 6, c-api 0
- Observation lines: 78, 96, 102, 133, 139, 177

#### `src/pixie.h`
- Totals: ownership 0, container 0, callback 0, observation 6, c-api 0
- Observation lines: 32-35, 45, 51

#### `src/pixie_data.cpp`
- Totals: ownership 2, container 0, callback 0, observation 1, c-api 0
- Ownership lines: 43, 48
- Observation lines: 26

#### `src/pixie_data.h`
- Totals: ownership 1, container 0, callback 0, observation 1, c-api 0
- Ownership lines: 36
- Observation lines: 31

#### `src/pixien.cpp`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/pixien.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/radar.cpp`
- Totals: ownership 3, container 0, callback 0, observation 7, c-api 0
- Ownership lines: 107-108, 119
- Observation lines: 41, 55, 129, 175, 196, 275, 373

#### `src/radar.h`
- Totals: ownership 0, container 0, callback 0, observation 6, c-api 0
- Observation lines: 28, 31, 41, 43, 45-46

#### `src/results_screen.cpp`
- Totals: ownership 1, container 7, callback 0, observation 18, c-api 0
- Ownership lines: 267
- Container lines: 37, 51, 354, 570, 589, 597-598
- Observation lines: 15-16, 19, 24, 78-79, 100, 103, 129, 241, 243, 264, 272, 299, 323, 396, 530, 680

#### `src/results_screen.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/sai2x.cpp`
- Totals: ownership 0, container 1, callback 0, observation 17, c-api 0
- Container lines: 495
- Observation lines: 65-66, 83-84, 257-258, 275-276, 488, 507, 617, 634, 747, 765-766, 812-813

#### `src/sai2x.h`
- Totals: ownership 0, container 0, callback 0, observation 8, c-api 0
- Observation lines: 19-20, 23, 26, 29, 31, 36, 46

#### `src/save_data.cpp`
- Totals: ownership 6, container 16, callback 0, observation 4, c-api 0
- Ownership lines: 57, 80, 185, 280, 430, 443
- Container lines: 94, 96, 98-100, 110-111, 362, 380, 457, 459-460, 462, 465, 474, 667
- Observation lines: 95, 439, 458, 601

#### `src/save_data.h`
- Totals: ownership 0, container 3, callback 0, observation 1, c-api 0
- Container lines: 42, 44, 46
- Observation lines: 49

#### `src/screen.cpp`
- Totals: ownership 25, container 6, callback 0, observation 43, c-api 0
- Ownership lines: 151, 239, 250, 254-255, 259-261, 265-268, 283, 326, 330-331, 335-337, 341-344, 827, 840
- Container lines: 665, 909, 1070-1071, 1073, 1379
- Observation lines: 53-57, 368, 585, 592, 678, 726, 742, 763, 776, 791, 824, 837, 877, 949, 989, 1014, 1018, 1037, 1062, 1067, 1069, 1112, 1117, 1158, 1161, 1170, 1186, 1201, 1215, 1217, 1226, 1241, 1251, 1270, 1280, 1296, 1306, 1338, 1355

#### `src/screen.h`
- Totals: ownership 0, container 2, callback 0, observation 18, c-api 0
- Container lines: 98-99
- Observation lines: 47-50, 53, 61-62, 64-69, 71, 73-74, 101, 103

#### `src/smooth.cpp`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/smooth.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/sound.cpp`
- Totals: ownership 0, container 0, callback 0, observation 4, c-api 0
- Observation lines: 36, 130, 132, 145

#### `src/soundob.h`
- Totals: ownership 0, container 2, callback 0, observation 2, c-api 0
- Container lines: 62-63
- Observation lines: 55-56

#### `src/sounds.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/stats.cpp`
- Totals: ownership 0, container 2, callback 0, observation 10, c-api 0
- Container lines: 433, 609
- Observation lines: 29, 200, 204, 427, 432, 605, 618, 967, 1069, 1103

#### `src/stats.h`
- Totals: ownership 0, container 1, callback 0, observation 3, c-api 0
- Container lines: 81
- Observation lines: 67-68, 105

#### `src/test_trace.cpp`
- Totals: ownership 0, container 0, callback 0, observation 2, c-api 0
- Observation lines: 10, 19

#### `src/test_trace.h`
- Totals: ownership 0, container 1, callback 0, observation 3, c-api 0
- Container lines: 24
- Observation lines: 18-19, 23

#### `src/text.cpp`
- Totals: ownership 0, container 3, callback 0, observation 37, c-api 0
- Container lines: 83, 440, 584
- Observation lines: 26, 34, 52, 72, 84, 103, 124, 144, 164, 186, 197, 236, 259-260, 274, 288, 297, 306, 319, 331-332, 344, 364, 374, 381, 387, 393, 398, 401, 411, 414, 425, 435, 443, 574, 579, 587

#### `src/text.h`
- Totals: ownership 0, container 0, callback 0, observation 25, c-api 0
- Observation lines: 32-50, 56-59, 61-62

#### `src/treasure.cpp`
- Totals: ownership 0, container 4, callback 0, observation 7, c-api 0
- Container lines: 60, 63, 189, 228
- Observation lines: 36, 56, 62, 201, 318, 333, 349

#### `src/treasure.h`
- Totals: ownership 0, container 0, callback 0, observation 2, c-api 0
- Observation lines: 32-33

#### `src/util.cpp`
- Totals: ownership 0, container 0, callback 0, observation 5, c-api 1
- Observation lines: 101, 109, 117, 158, 166
- C API lines: 64

#### `src/util.h`
- Totals: ownership 0, container 0, callback 0, observation 3, c-api 0
- Observation lines: 33-35

#### `src/version.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/video.cpp`
- Totals: ownership 6, container 3, callback 0, observation 43, c-api 0
- Ownership lines: 99, 105, 1916-1917, 1951-1952
- Container lines: 727-728, 1784
- Observation lines: 33, 284, 342, 406, 413, 460, 464, 469, 489, 504, 659, 761, 781, 799, 831, 853, 1007, 1106-1107, 1174-1175, 1267-1268, 1339-1340, 1413-1414, 1710, 1715, 1728, 1770, 1792, 1830, 1833, 1838-1839, 1842, 1859-1861, 1915, 1919, 1964

#### `src/video.h`
- Totals: ownership 0, container 0, callback 0, observation 6, c-api 0
- Observation lines: 58, 81, 123, 130-131, 153

#### `src/view.cpp`
- Totals: ownership 2, container 10, callback 0, observation 47, c-api 0
- Ownership lines: 180, 196
- Container lines: 127, 370, 374-376, 1334, 1453, 1473, 2166, 2227
- Observation lines: 125, 130, 215-216, 251, 253, 255, 258, 269, 274-275, 310, 312, 314, 317, 379-380, 396, 414, 433, 518, 533, 559, 574, 625, 653, 669, 704, 718, 741, 885, 901, 919, 938, 1079, 1116, 1121, 1129, 1137, 1361, 1376, 1930, 1965, 1981, 1985, 2164, 2167

#### `src/view.h`
- Totals: ownership 0, container 4, callback 0, observation 9, c-api 0
- Container lines: 72, 110-111, 113
- Observation lines: 68-69, 87, 91, 96, 116-117, 123, 127

#### `src/view_sizes.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

#### `src/walker.cpp`
- Totals: ownership 5, container 2, callback 0, observation 84, c-api 12
- Ownership lines: 49, 200, 204, 2221, 4402
- Container lines: 1875, 2359
- Observation lines: 43, 297, 332, 337, 363, 365, 369, 371, 375, 377, 381, 383, 684, 686, 873, 933, 986, 988, 999, 1015, 1217, 1381, 1431, 1499, 1683, 1690, 1704, 1711, 1754, 1761, 1773, 1783, 1801, 1840, 1870, 1872-1873, 1880, 2158, 2160, 2162, 2260, 2262, 2349-2351, 2483, 2513, 2841, 2950, 2989, 3030, 3112, 3154, 3384, 3539, 3577, 3768, 3845, 3985, 4062, 4095, 4178, 4206, 4368, 4374, 4376, 4495, 4605, 4633, 4650, 4664, 4690, 4696-4698, 4706, 4719, 4727, 4739, 4745-4746, 4813-4814
- C API lines: 1376, 1386-1388, 1391, 1401, 1446, 1463-1464, 1476-1477, 1509

#### `src/walker.h`
- Totals: ownership 0, container 0, callback 0, observation 28, c-api 1
- Observation lines: 41-43, 48-49, 56-57, 68, 75-76, 80, 84, 86, 88-89, 92, 126-132, 152, 167, 180-182
- C API lines: 154

#### `src/weap.cpp`
- Totals: ownership 0, container 1, callback 0, observation 5, c-api 0
- Container lines: 37
- Observation lines: 134, 264, 266, 283, 285

#### `src/weap.h`
- Totals: ownership 0, container 0, callback 0, observation 0, c-api 0

## File-by-File Breakdown (Vendored `src/external`)

| File | Ownership | Container | Callback | Observation | C API | Total |
|---|---:|---:|---:|---:|---:|---:|
| `src/external/libyaml/include/yaml.h` | 0 | 0 | 0 | 0 | 159 | 159 |
| `src/external/libyaml/src/api.c` | 3 | 0 | 0 | 0 | 114 | 117 |
| `src/external/libyaml/src/config.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/libyaml/src/dumper.c` | 0 | 0 | 0 | 0 | 36 | 36 |
| `src/external/libyaml/src/emitter.c` | 0 | 0 | 0 | 0 | 147 | 147 |
| `src/external/libyaml/src/loader.c` | 0 | 0 | 0 | 0 | 40 | 40 |
| `src/external/libyaml/src/parser.c` | 0 | 0 | 0 | 0 | 109 | 109 |
| `src/external/libyaml/src/reader.c` | 0 | 0 | 0 | 0 | 8 | 8 |
| `src/external/libyaml/src/scanner.c` | 0 | 0 | 0 | 0 | 97 | 97 |
| `src/external/libyaml/src/writer.c` | 0 | 0 | 0 | 0 | 4 | 4 |
| `src/external/libyaml/src/yaml_private.h` | 0 | 0 | 0 | 0 | 21 | 21 |
| `src/external/libzip/config.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/libzip/mkstemp.c` | 0 | 0 | 0 | 0 | 3 | 3 |
| `src/external/libzip/zip.h` | 0 | 0 | 0 | 0 | 5 | 5 |
| `src/external/libzip/zip_add.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_add_dir.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_add_entry.c` | 1 | 0 | 0 | 0 | 1 | 2 |
| `src/external/libzip/zip_close.c` | 14 | 0 | 0 | 0 | 19 | 33 |
| `src/external/libzip/zip_delete.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_dir_add.c` | 3 | 0 | 0 | 0 | 2 | 5 |
| `src/external/libzip/zip_dirent.c` | 16 | 0 | 0 | 0 | 36 | 52 |
| `src/external/libzip/zip_discard.c` | 5 | 0 | 0 | 0 | 1 | 6 |
| `src/external/libzip/zip_entry.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_err_str.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_error.c` | 1 | 0 | 0 | 0 | 7 | 8 |
| `src/external/libzip/zip_error_clear.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_error_get.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_error_get_sys_type.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_error_strerror.c` | 1 | 0 | 0 | 0 | 4 | 5 |
| `src/external/libzip/zip_error_to_str.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_extra_field.c` | 6 | 0 | 0 | 0 | 14 | 20 |
| `src/external/libzip/zip_extra_field_api.c` | 0 | 0 | 0 | 0 | 9 | 9 |
| `src/external/libzip/zip_fclose.c` | 1 | 0 | 0 | 0 | 1 | 2 |
| `src/external/libzip/zip_fdopen.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_file_add.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_file_error_clear.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_file_error_get.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_file_get_comment.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_file_get_offset.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_file_rename.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_file_replace.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_file_set_comment.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_file_strerror.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_filerange_crc.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_fopen.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_fopen_encrypted.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_fopen_index.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_fopen_index_encrypted.c` | 3 | 0 | 0 | 0 | 5 | 8 |
| `src/external/libzip/zip_fread.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_get_archive_comment.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_get_archive_flag.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_get_compression_implementation.c` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/libzip/zip_get_encryption_implementation.c` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/libzip/zip_get_file_comment.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_get_name.c` | 0 | 0 | 0 | 0 | 3 | 3 |
| `src/external/libzip/zip_get_num_entries.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_get_num_files.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_memdup.c` | 1 | 0 | 0 | 0 | 3 | 4 |
| `src/external/libzip/zip_name_locate.c` | 0 | 0 | 0 | 0 | 4 | 4 |
| `src/external/libzip/zip_new.c` | 1 | 0 | 0 | 0 | 1 | 2 |
| `src/external/libzip/zip_open.c` | 5 | 0 | 0 | 0 | 21 | 26 |
| `src/external/libzip/zip_rename.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_replace.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_set_archive_comment.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_set_archive_flag.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_set_default_password.c` | 1 | 0 | 0 | 0 | 1 | 2 |
| `src/external/libzip/zip_set_file_comment.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_set_file_compression.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_set_name.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_source_buffer.c` | 4 | 0 | 0 | 0 | 6 | 10 |
| `src/external/libzip/zip_source_close.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_source_crc.c` | 2 | 0 | 0 | 0 | 4 | 6 |
| `src/external/libzip/zip_source_deflate.c` | 3 | 0 | 0 | 0 | 14 | 17 |
| `src/external/libzip/zip_source_error.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_source_file.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_source_filep.c` | 5 | 0 | 0 | 0 | 8 | 13 |
| `src/external/libzip/zip_source_free.c` | 1 | 0 | 0 | 0 | 1 | 2 |
| `src/external/libzip/zip_source_function.c` | 1 | 0 | 0 | 0 | 2 | 3 |
| `src/external/libzip/zip_source_layered.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_source_open.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_source_pkware.c` | 2 | 0 | 0 | 0 | 9 | 11 |
| `src/external/libzip/zip_source_pop.c` | 1 | 0 | 0 | 0 | 1 | 2 |
| `src/external/libzip/zip_source_read.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_source_stat.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_source_window.c` | 2 | 0 | 0 | 0 | 5 | 7 |
| `src/external/libzip/zip_source_zip.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_source_zip_new.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_stat.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_stat_index.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_stat_init.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_strerror.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_string.c` | 6 | 0 | 0 | 0 | 8 | 14 |
| `src/external/libzip/zip_unchange.c` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/libzip/zip_unchange_all.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_unchange_archive.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_unchange_data.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/libzip/zip_utf-8.c` | 1 | 0 | 0 | 0 | 8 | 9 |
| `src/external/libzip/zipconf.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/libzip/zipint.h` | 0 | 0 | 0 | 0 | 25 | 25 |
| `src/external/micropather/micropather.cpp` | 7 | 0 | 0 | 0 | 59 | 66 |
| `src/external/micropather/micropather.h` | 0 | 0 | 0 | 0 | 36 | 36 |
| `src/external/physfs/archivers/dir.c` | 0 | 0 | 0 | 0 | 40 | 40 |
| `src/external/physfs/archivers/grp.c` | 0 | 0 | 0 | 0 | 62 | 62 |
| `src/external/physfs/archivers/hog.c` | 0 | 0 | 0 | 0 | 61 | 61 |
| `src/external/physfs/archivers/lzma.c` | 0 | 0 | 0 | 0 | 81 | 81 |
| `src/external/physfs/archivers/mvl.c` | 0 | 0 | 0 | 0 | 61 | 61 |
| `src/external/physfs/archivers/qpak.c` | 0 | 0 | 0 | 0 | 74 | 74 |
| `src/external/physfs/archivers/wad.c` | 0 | 0 | 0 | 0 | 64 | 64 |
| `src/external/physfs/archivers/zip.c` | 0 | 0 | 0 | 0 | 111 | 111 |
| `src/external/physfs/extras/abs-file.h` | 0 | 0 | 0 | 0 | 5 | 5 |
| `src/external/physfs/extras/globbing.c` | 1 | 0 | 0 | 0 | 10 | 11 |
| `src/external/physfs/extras/globbing.h` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/physfs/extras/ignorecase.c` | 0 | 0 | 0 | 0 | 11 | 11 |
| `src/external/physfs/extras/ignorecase.h` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/physfs/extras/physfs_rb/physfs/physfsrwops.c` | 0 | 0 | 0 | 0 | 15 | 15 |
| `src/external/physfs/extras/physfs_rb/physfs/physfsrwops.h` | 0 | 0 | 0 | 0 | 4 | 4 |
| `src/external/physfs/extras/physfs_rb/physfs/rb_physfs.c` | 0 | 0 | 0 | 0 | 17 | 17 |
| `src/external/physfs/extras/physfs_rb/physfs/rb_physfs.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/physfs/extras/physfs_rb/physfs/rb_physfs_file.c` | 3 | 0 | 0 | 0 | 12 | 15 |
| `src/external/physfs/extras/physfs_rb/physfs/rb_physfs_file.h` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/physfs/extras/physfs_rb/physfs/rb_sdl_rwops.c` | 3 | 0 | 0 | 0 | 10 | 13 |
| `src/external/physfs/extras/physfs_rb/physfs/rb_sdl_rwops.h` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/physfs/extras/physfshttpd.c` | 5 | 0 | 0 | 0 | 12 | 17 |
| `src/external/physfs/extras/physfsrwops.c` | 0 | 0 | 0 | 0 | 17 | 17 |
| `src/external/physfs/extras/physfsrwops.h` | 0 | 0 | 0 | 0 | 4 | 4 |
| `src/external/physfs/extras/physfsunpack.c` | 2 | 0 | 0 | 0 | 10 | 12 |
| `src/external/physfs/extras/selfextract.c` | 0 | 0 | 0 | 0 | 4 | 4 |
| `src/external/physfs/physfs.c` | 3 | 0 | 0 | 0 | 240 | 243 |
| `src/external/physfs/physfs.h` | 0 | 0 | 0 | 0 | 80 | 80 |
| `src/external/physfs/physfs_byteorder.c` | 0 | 0 | 0 | 0 | 24 | 24 |
| `src/external/physfs/physfs_casefolding.h` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/physfs/physfs_internal.h` | 3 | 0 | 0 | 0 | 73 | 76 |
| `src/external/physfs/physfs_platforms.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/physfs/physfs_unicode.c` | 0 | 0 | 0 | 0 | 18 | 18 |
| `src/external/physfs/platform/beos.cpp` | 2 | 0 | 0 | 0 | 21 | 23 |
| `src/external/physfs/platform/macosx.c` | 0 | 0 | 0 | 0 | 29 | 29 |
| `src/external/physfs/platform/os2.c` | 0 | 0 | 0 | 0 | 62 | 62 |
| `src/external/physfs/platform/pocketpc.c` | 0 | 0 | 0 | 0 | 66 | 66 |
| `src/external/physfs/platform/posix.c` | 0 | 0 | 0 | 0 | 43 | 43 |
| `src/external/physfs/platform/unix.c` | 0 | 0 | 0 | 0 | 43 | 43 |
| `src/external/physfs/platform/windows.c` | 0 | 0 | 0 | 0 | 115 | 115 |
| `src/external/physfs/zlib123/adler32.c` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/physfs/zlib123/compress.c` | 0 | 0 | 0 | 0 | 6 | 6 |
| `src/external/physfs/zlib123/crc32.c` | 0 | 0 | 0 | 0 | 5 | 5 |
| `src/external/physfs/zlib123/crc32.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/physfs/zlib123/deflate.c` | 0 | 0 | 0 | 0 | 41 | 41 |
| `src/external/physfs/zlib123/deflate.h` | 0 | 0 | 0 | 0 | 17 | 17 |
| `src/external/physfs/zlib123/gzio.c` | 2 | 0 | 0 | 0 | 53 | 55 |
| `src/external/physfs/zlib123/infback.c` | 0 | 0 | 0 | 0 | 5 | 5 |
| `src/external/physfs/zlib123/inffast.c` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/physfs/zlib123/inffast.h` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/physfs/zlib123/inffixed.h` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/physfs/zlib123/inflate.c` | 0 | 0 | 0 | 0 | 8 | 8 |
| `src/external/physfs/zlib123/inflate.h` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/physfs/zlib123/inftrees.c` | 0 | 0 | 0 | 0 | 0 | 0 |
| `src/external/physfs/zlib123/inftrees.h` | 0 | 0 | 0 | 0 | 1 | 1 |
| `src/external/physfs/zlib123/trees.c` | 0 | 0 | 0 | 0 | 59 | 59 |
| `src/external/physfs/zlib123/trees.h` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/physfs/zlib123/uncompr.c` | 0 | 0 | 0 | 0 | 3 | 3 |
| `src/external/physfs/zlib123/zconf.h` | 0 | 0 | 0 | 0 | 2 | 2 |
| `src/external/physfs/zlib123/zlib.h` | 0 | 0 | 0 | 0 | 30 | 30 |
| `src/external/physfs/zlib123/zutil.c` | 3 | 0 | 0 | 0 | 9 | 12 |
| `src/external/physfs/zlib123/zutil.h` | 0 | 0 | 0 | 0 | 7 | 7 |
| `src/external/yam/yam.cpp` | 4 | 0 | 0 | 0 | 7 | 11 |
| `src/external/yam/yam.h` | 0 | 0 | 0 | 0 | 11 | 11 |

### Vendored Line References

Vendored libraries are classified as C API boundaries by default. Ownership lines below indicate explicit allocation/free operations inside those libraries.

#### `src/external/libyaml/include/yaml.h`
- Totals: ownership 0, c-api 159
- C API lines: 68, 91, 93, 284, 290, 296, 298, 304, 322, 324, 343, 401, 406, 408, 424, 430, 432, 434, 448, 450, 460, 462, 488, 500, 522-525, 541, 553, 578-580, 601-602, 614, 633-634, 646, 655, 723, 731, 743, 745, 747, 758, 760, 762, 783, 785, 787, 791, 796, 798, 832-835, 845, 860, 879, 896-897, 913-914, 929-930, 943, 958, 986-987, 1067, 1091, 1099, 1113, 1116, 1131, 1140, 1142, 1144, 1146, 1194, 1196, 1198, 1200, 1212, 1214, 1216, 1228, 1230, 1232, 1247, 1249, 1251, 1260, 1262, 1264, 1270, 1272, 1274, 1289, 1291, 1293, 1297, 1317, 1326, 1341, 1355, 1367-1368, 1378, 1402, 1426, 1451, 1476, 1535, 1547, 1550, 1561, 1565, 1571, 1573, 1575, 1577, 1618, 1620, 1622, 1631, 1633, 1635, 1637, 1643, 1645, 1647, 1653, 1655, 1657, 1689, 1699, 1703, 1711, 1756, 1776, 1785, 1803, 1817, 1829-1830, 1840, 1851, 1861, 1871, 1881, 1891, 1908, 1921, 1934, 1951, 1962

#### `src/external/libyaml/src/api.c`
- Totals: ownership 3, c-api 114
- Ownership lines: 33, 43, 53
- C API lines: 19, 30, 40-41, 51, 61, 74-75, 77, 96-97, 118, 120, 136, 141, 171, 214, 243-244, 246, 269-270, 272, 283, 303, 320-321, 336, 349, 386, 414, 416, 440, 442, 451, 472, 489-490, 505, 518, 530, 542, 554, 566, 578, 620, 622-623, 663, 680, 696-699, 706, 708-710, 727, 775, 791, 794, 815-817, 822-824, 869-870, 874-875, 908, 924-925, 929-930, 963, 979, 981, 1030-1033, 1040-1042, 1044, 1046-1048, 1068, 1117, 1122, 1164, 1179, 1194-1195, 1202-1203, 1244-1245, 1251, 1253-1255, 1289-1290, 1296, 1298-1300, 1334, 1362

#### `src/external/libyaml/src/config.h`
- Totals: ownership 0, c-api 0

#### `src/external/libyaml/src/dumper.c`
- Totals: ownership 0, c-api 36
- C API lines: 9, 12, 15, 22, 29, 32, 40, 43, 46-47, 50-51, 54-55, 62, 86, 112, 167, 207, 209-211, 248, 250, 264, 266, 268, 301, 316-317, 339-340, 347, 369-370, 377

#### `src/external/libyaml/src/emitter.c`
- Totals: ownership 0, c-api 147
- C API lines: 69, 76, 79, 82, 86, 94, 97-98, 101-102, 105-106, 109-110, 113-114, 117-118, 121-122, 125-126, 129-130, 133-134, 137, 141, 144, 147, 150, 157, 160, 163, 166, 169, 176, 179, 182, 189, 193, 197-198, 201-202, 205-206, 209-210, 217, 220, 223-224, 228-229, 232-233, 236-237, 240-241, 244-245, 248-249, 252, 256-257, 260-261, 268, 281, 309, 313, 364, 367, 403, 424, 495-496, 551-552, 561, 677-678, 691-692, 729-730, 778-779, 841-842, 866-867, 900-901, 945-946, 970, 1005, 1019, 1042, 1066, 1090, 1100, 1114, 1128, 1130, 1179, 1241, 1259, 1294, 1333, 1349, 1400-1401, 1436-1437, 1441, 1479-1480, 1679-1680, 1754, 1766, 1786-1787, 1812-1813, 1829-1830, 1850-1851, 1898-1899, 1958-1959, 2021-2022, 2177, 2180-2181, 2235-2236, 2274-2275

#### `src/external/libyaml/src/loader.c`
- Totals: ownership 0, c-api 40
- C API lines: 9, 16-17, 20-22, 30, 38, 45, 48, 51, 54, 57, 60, 67, 119-120, 134-136, 152, 165, 201, 225, 229, 260, 262-263, 283, 287, 320, 325-327, 330, 377, 382-384, 388

#### `src/external/libyaml/src/parser.c`
- Totals: ownership 0, c-api 109
- C API lines: 68, 75-76, 79-81, 88, 91, 94, 98, 101, 104, 108-109, 112-113, 116-117, 120-121, 124-125, 128-129, 132-133, 136-137, 140-141, 144-145, 152-153, 156-159, 162, 170, 196-197, 207-209, 226, 313, 315, 342, 345-346, 348-349, 440, 442, 470, 472, 529, 532-536, 601, 688, 726-727, 729, 785-786, 788, 835-836, 838, 901-902, 904, 951-952, 954, 1013-1014, 1016, 1043-1044, 1046, 1074-1075, 1077, 1103-1104, 1106, 1174-1175, 1177, 1210, 1213, 1233-1236, 1243-1244, 1246-1248, 1250, 1341, 1344

#### `src/external/libyaml/src/reader.c`
- Totals: ownership 0, c-api 8
- C API lines: 9, 13, 16, 19, 26, 51, 94, 142

#### `src/external/libyaml/src/scanner.c`
- Totals: ownership 0, c-api 97
- C API lines: 574, 581, 589, 592, 599, 602, 605, 608, 611, 618, 622, 629, 632, 635, 638, 642, 646, 650, 653, 656, 659, 662, 665, 668, 671, 674, 681, 684, 687, 691, 695, 699, 703, 707, 710, 714-715, 718, 722, 726-727, 731, 735, 742, 782, 800, 822, 860, 1056, 1058, 1097, 1141, 1143, 1168, 1189, 1209, 1257, 1290, 1328, 1366, 1402, 1447, 1490, 1533, 1568, 1629, 1682, 1686, 1764, 1794, 1825, 1856, 1887, 1918, 1994, 1997, 1999, 2110, 2161, 2208, 2255, 2258-2259, 2319, 2385, 2387-2388, 2464, 2500, 2567-2568, 2659, 2728, 2950-2951, 3012, 3380

#### `src/external/libyaml/src/writer.c`
- Totals: ownership 0, c-api 4
- C API lines: 9, 12, 19, 32

#### `src/external/libyaml/src/yaml_private.h`
- Totals: ownership 0, c-api 21
- C API lines: 13, 16-17, 20, 30, 37, 96-98, 102-103, 107-108, 402, 405, 424-425, 451-452, 463-464

#### `src/external/libzip/config.h`
- Totals: ownership 0, c-api 0

#### `src/external/libzip/mkstemp.c`
- Totals: ownership 0, c-api 3
- C API lines: 59, 71, 78

#### `src/external/libzip/zip.h`
- Totals: ownership 0, c-api 5
- C API lines: 207, 224, 260, 280, 284

#### `src/external/libzip/zip_add.c`
- Totals: ownership 0, c-api 1
- C API lines: 49

#### `src/external/libzip/zip_add_dir.c`
- Totals: ownership 0, c-api 1
- C API lines: 44

#### `src/external/libzip/zip_add_entry.c`
- Totals: ownership 1, c-api 1
- Ownership lines: 52
- C API lines: 45

#### `src/external/libzip/zip_close.c`
- Totals: ownership 14, c-api 19
- Ownership lines: 113, 120, 133, 143, 150, 245, 250, 257, 269, 283, 599, 606, 615, 623
- C API lines: 64-65, 70, 74-75, 291, 455, 457, 490, 492, 524, 529, 565, 592, 594, 596, 642, 644-645

#### `src/external/libzip/zip_delete.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_dir_add.c`
- Totals: ownership 3, c-api 2
- Ownership lines: 67, 77, 83
- C API lines: 46, 50

#### `src/external/libzip/zip_dirent.c`
- Totals: ownership 16, c-api 36
- Ownership lines: 62, 64, 84, 106, 113, 115, 215, 251, 299, 434, 437, 802, 814, 897, 909, 942
- C API lines: 53, 70, 101, 132, 211, 232, 245, 257, 283, 327, 429, 462, 513, 518, 541, 576, 636, 641, 789, 791-792, 821, 849, 862, 875, 890, 892, 919, 933, 935, 949, 960, 975, 986, 999, 1016

#### `src/external/libzip/zip_discard.c`
- Totals: ownership 5, c-api 1
- Ownership lines: 55, 60, 67, 77, 79
- C API lines: 47

#### `src/external/libzip/zip_entry.c`
- Totals: ownership 0, c-api 2
- C API lines: 39, 49

#### `src/external/libzip/zip_err_str.c`
- Totals: ownership 0, c-api 1
- C API lines: 10

#### `src/external/libzip/zip_error.c`
- Totals: ownership 1, c-api 7
- Ownership lines: 66
- C API lines: 43, 55, 64, 73, 88, 98, 109

#### `src/external/libzip/zip_error_clear.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_error_get.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_error_get_sys_type.c`
- Totals: ownership 0, c-api 1
- C API lines: 46

#### `src/external/libzip/zip_error_strerror.c`
- Totals: ownership 1, c-api 4
- Ownership lines: 78
- C API lines: 46, 48-49, 80

#### `src/external/libzip/zip_error_to_str.c`
- Totals: ownership 0, c-api 2
- C API lines: 46, 48

#### `src/external/libzip/zip_extra_field.c`
- Totals: ownership 6, c-api 14
- Ownership lines: 117-118, 196, 205, 369, 372
- C API lines: 45, 72, 111, 126, 128, 156, 192, 218, 221, 261, 291, 307, 322, 356

#### `src/external/libzip/zip_extra_field_api.c`
- Totals: ownership 0, c-api 9
- C API lines: 41, 70, 99, 101, 146, 168, 197, 226, 330

#### `src/external/libzip/zip_fclose.c`
- Totals: ownership 1, c-api 1
- Ownership lines: 63
- C API lines: 43

#### `src/external/libzip/zip_fdopen.c`
- Totals: ownership 0, c-api 2
- C API lines: 41, 44

#### `src/external/libzip/zip_file_add.c`
- Totals: ownership 0, c-api 1
- C API lines: 47

#### `src/external/libzip/zip_file_error_clear.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_file_error_get.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_file_get_comment.c`
- Totals: ownership 0, c-api 2
- C API lines: 42, 46

#### `src/external/libzip/zip_file_get_offset.c`
- Totals: ownership 0, c-api 1
- C API lines: 54

#### `src/external/libzip/zip_file_rename.c`
- Totals: ownership 0, c-api 2
- C API lines: 43, 45

#### `src/external/libzip/zip_file_replace.c`
- Totals: ownership 0, c-api 2
- C API lines: 41, 60

#### `src/external/libzip/zip_file_set_comment.c`
- Totals: ownership 0, c-api 2
- C API lines: 43-44

#### `src/external/libzip/zip_file_strerror.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_filerange_crc.c`
- Totals: ownership 0, c-api 2
- C API lines: 45, 48

#### `src/external/libzip/zip_fopen.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_fopen_encrypted.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_fopen_index.c`
- Totals: ownership 0, c-api 1
- C API lines: 45

#### `src/external/libzip/zip_fopen_index_encrypted.c`
- Totals: ownership 3, c-api 5
- Ownership lines: 79, 87, 91
- C API lines: 42, 47-48, 75, 88

#### `src/external/libzip/zip_fread.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_get_archive_comment.c`
- Totals: ownership 0, c-api 2
- C API lines: 43, 47

#### `src/external/libzip/zip_get_archive_flag.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_get_compression_implementation.c`
- Totals: ownership 0, c-api 0

#### `src/external/libzip/zip_get_encryption_implementation.c`
- Totals: ownership 0, c-api 0

#### `src/external/libzip/zip_get_file_comment.c`
- Totals: ownership 0, c-api 2
- C API lines: 42, 45

#### `src/external/libzip/zip_get_name.c`
- Totals: ownership 0, c-api 3
- C API lines: 43, 51, 54

#### `src/external/libzip/zip_get_num_entries.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_get_num_files.c`
- Totals: ownership 0, c-api 1
- C API lines: 42

#### `src/external/libzip/zip_memdup.c`
- Totals: ownership 1, c-api 3
- Ownership lines: 49
- C API lines: 41-42, 44

#### `src/external/libzip/zip_name_locate.c`
- Totals: ownership 0, c-api 4
- C API lines: 46, 54, 56-57

#### `src/external/libzip/zip_new.c`
- Totals: ownership 1, c-api 1
- Ownership lines: 51
- C API lines: 47

#### `src/external/libzip/zip_open.c`
- Totals: ownership 5, c-api 21
- Ownership lines: 118, 170, 510, 520, 560
- C API lines: 64, 66, 83, 105, 107, 126, 178, 201, 310, 370, 373-374, 405, 434, 461, 491, 574, 593, 648, 653-654

#### `src/external/libzip/zip_rename.c`
- Totals: ownership 0, c-api 1
- C API lines: 44

#### `src/external/libzip/zip_replace.c`
- Totals: ownership 0, c-api 1
- C API lines: 42

#### `src/external/libzip/zip_set_archive_comment.c`
- Totals: ownership 0, c-api 1
- C API lines: 43

#### `src/external/libzip/zip_set_archive_flag.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_set_default_password.c`
- Totals: ownership 1, c-api 1
- Ownership lines: 50
- C API lines: 44

#### `src/external/libzip/zip_set_file_comment.c`
- Totals: ownership 0, c-api 1
- C API lines: 44

#### `src/external/libzip/zip_set_file_compression.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_set_name.c`
- Totals: ownership 0, c-api 1
- C API lines: 44

#### `src/external/libzip/zip_source_buffer.c`
- Totals: ownership 4, c-api 6
- Ownership lines: 65, 76, 150, 153
- C API lines: 42, 47, 52, 86, 89, 138

#### `src/external/libzip/zip_source_close.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_source_crc.c`
- Totals: ownership 2, c-api 4
- Ownership lines: 64, 159
- C API lines: 44, 49, 55, 81

#### `src/external/libzip/zip_source_deflate.c`
- Totals: ownership 3, c-api 14
- Ownership lines: 75, 374, 391
- C API lines: 42, 47, 52, 54-55, 57, 64, 103-104, 177-178, 246, 317, 388

#### `src/external/libzip/zip_source_error.c`
- Totals: ownership 0, c-api 2
- C API lines: 41, 43

#### `src/external/libzip/zip_source_file.c`
- Totals: ownership 0, c-api 1
- C API lines: 44

#### `src/external/libzip/zip_source_filep.c`
- Totals: ownership 5, c-api 8
- Ownership lines: 91, 100, 114, 237, 240
- C API lines: 45-46, 53, 56, 62, 79, 124, 127

#### `src/external/libzip/zip_source_free.c`
- Totals: ownership 1, c-api 1
- Ownership lines: 58
- C API lines: 43

#### `src/external/libzip/zip_source_function.c`
- Totals: ownership 1, c-api 2
- Ownership lines: 66
- C API lines: 43, 62

#### `src/external/libzip/zip_source_layered.c`
- Totals: ownership 0, c-api 2
- C API lines: 43-44

#### `src/external/libzip/zip_source_open.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_source_pkware.c`
- Totals: ownership 2, c-api 9
- Ownership lines: 79, 233
- C API lines: 42, 44, 57, 64, 102, 134, 136, 174, 231

#### `src/external/libzip/zip_source_pop.c`
- Totals: ownership 1, c-api 1
- Ownership lines: 59
- C API lines: 43

#### `src/external/libzip/zip_source_read.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_source_stat.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_source_window.c`
- Totals: ownership 2, c-api 5
- Ownership lines: 63, 143
- C API lines: 45, 48, 54, 78, 84

#### `src/external/libzip/zip_source_zip.c`
- Totals: ownership 0, c-api 1
- C API lines: 44

#### `src/external/libzip/zip_source_zip_new.c`
- Totals: ownership 0, c-api 1
- C API lines: 43

#### `src/external/libzip/zip_stat.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_stat_index.c`
- Totals: ownership 0, c-api 2
- C API lines: 41, 44

#### `src/external/libzip/zip_stat_init.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_strerror.c`
- Totals: ownership 0, c-api 1
- C API lines: 41

#### `src/external/libzip/zip_string.c`
- Totals: ownership 6, c-api 8
- Ownership lines: 80-82, 159, 164-165
- C API lines: 44, 59, 75, 88, 90, 125, 136, 190

#### `src/external/libzip/zip_unchange.c`
- Totals: ownership 0, c-api 2
- C API lines: 43, 51

#### `src/external/libzip/zip_unchange_all.c`
- Totals: ownership 0, c-api 1
- C API lines: 43

#### `src/external/libzip/zip_unchange_archive.c`
- Totals: ownership 0, c-api 1
- C API lines: 43

#### `src/external/libzip/zip_unchange_data.c`
- Totals: ownership 0, c-api 1
- C API lines: 39

#### `src/external/libzip/zip_utf-8.c`
- Totals: ownership 1, c-api 8
- Ownership lines: 241
- C API lines: 42, 120, 123, 197, 224-225, 227-228

#### `src/external/libzip/zipconf.h`
- Totals: ownership 0, c-api 0

#### `src/external/libzip/zipint.h`
- Totals: ownership 0, c-api 25
- C API lines: 179, 185-186, 199, 205, 257, 263-264, 271, 285, 344, 355, 374, 377, 387, 392, 429, 447, 449, 452, 460, 477, 483, 485, 491

#### `src/external/micropather/micropather.cpp`
- Totals: ownership 7, c-api 59
- Ownership lines: 216, 225, 236-238, 276, 300
- C API lines: 48, 59-61, 69-71, 75, 90, 106, 109, 128, 147, 164, 167, 181, 197, 245, 272, 274, 298, 311, 363, 368, 373, 383, 386, 404, 408, 431, 434, 446, 467, 469, 475, 534, 565-566, 569, 584, 590, 621, 628, 630, 641, 662, 674, 698, 701-703, 738, 775, 780, 794-795, 797, 804, 822

#### `src/external/micropather/micropather.h`
- Totals: ownership 0, c-api 36
- C API lines: 89, 119, 127, 134, 142, 155, 158, 171, 175, 182-183, 193, 203, 245-246, 249, 252, 256, 265, 270-271, 274, 277-279, 281-283, 285, 336, 348, 359, 373, 379, 381, 391

#### `src/external/physfs/archivers/dir.c`
- Totals: ownership 0, c-api 40
- C API lines: 17, 26, 35, 41, 47, 53, 59, 72, 79, 81-82, 102, 104, 106, 116, 118, 128, 130, 135, 142, 144, 149, 156-158, 160, 165, 172-174, 176-177, 198, 204, 210, 216, 218, 228, 230, 240

#### `src/external/physfs/archivers/grp.c`
- Totals: ownership 0, c-api 62
- C API lines: 39, 46, 49, 54-55, 60, 62, 69, 72-73, 83, 89, 96, 98-99, 104, 110, 112-113, 126, 128, 133, 135, 142-143, 145, 179, 181, 192, 196, 204, 209-210, 218, 220, 223-224, 268, 271, 301, 303, 308-309, 319, 321-322, 352, 358, 365, 372-374, 376, 380, 387, 389-391, 414, 420, 426, 432

#### `src/external/physfs/archivers/hog.c`
- Totals: ownership 0, c-api 61
- C API lines: 47, 57, 60, 68-69, 74, 76, 83, 86-87, 97, 103, 110, 112-113, 118, 124, 126-127, 140, 142, 147, 149, 156-157, 159, 216, 218, 229, 233, 241, 246-247, 255, 257, 259, 308, 311, 340, 342, 347-348, 358, 360-361, 391, 397, 404, 411-413, 415, 419, 426, 428-430, 453, 459, 465, 471

#### `src/external/physfs/archivers/lzma.c`
- Totals: ownership 0, c-api 81
- C API lines: 27, 43, 45, 57, 68, 77-79, 87, 93, 108-109, 111, 130-131, 133, 146, 148, 158, 174, 176-177, 186, 188, 196, 199-200, 210, 212, 223, 225, 242, 263, 282, 325, 328, 379, 386, 388, 393, 395, 400, 402, 413, 415, 420, 422, 440, 442-443, 464, 467, 545-546, 548, 559, 561, 565-566, 583-584, 603, 605, 610-612, 614-615, 626, 628-629, 637, 643, 645-646, 659, 665, 671, 673, 687, 693

#### `src/external/physfs/archivers/mvl.c`
- Totals: ownership 0, c-api 61
- C API lines: 42, 49, 52, 57-58, 63, 65, 72, 75-76, 86, 92, 99, 101-102, 107, 113, 115-116, 129, 131, 136, 138, 145-146, 148, 182, 184, 195, 199, 207, 212-213, 221, 223, 226, 266, 269, 297, 299, 304-305, 315, 317-318, 348, 354, 361, 368-370, 372, 376, 383, 385-387, 410, 416, 422, 428

#### `src/external/physfs/archivers/qpak.c`
- Totals: ownership 0, c-api 74
- C API lines: 53, 60, 63, 68-69, 77, 79, 86, 89-90, 100, 106, 113, 115-116, 121, 127, 129-130, 143, 145, 150, 152, 159-160, 205, 207, 218, 222, 230, 235-236, 244, 246, 248, 293, 295, 329, 337, 386-387, 389, 400, 402, 404, 419-420, 422, 435, 448, 450, 455, 475, 496, 499-500, 505, 507, 509, 519, 526-528, 531, 533, 536, 543, 545-547, 572, 578, 584, 590

#### `src/external/physfs/archivers/wad.c`
- Totals: ownership 0, c-api 64
- C API lines: 57, 64, 68, 73-74, 79, 81, 88, 91-92, 102, 108, 115, 117-118, 123, 129, 131-132, 145, 147, 152, 154, 161-162, 164, 203, 205, 216, 220, 228, 233-234, 242, 244, 247, 293, 296, 325, 327, 329-330, 333-334, 361, 363, 385, 391, 393, 396, 421, 428-430, 432, 436, 443, 445-447, 470, 476, 482, 488

#### `src/external/physfs/archivers/zip.c`
- Totals: ownership 0, c-api 111
- C API lines: 64, 82, 84, 92-93, 96, 120, 135, 144, 170, 180, 192, 201, 204-205, 269, 275, 282, 284, 289, 295, 297-299, 336, 352, 354, 359, 361, 375, 377-378, 463, 467, 495, 500, 514, 516, 521, 543, 559, 580, 585, 595, 597-598, 654, 663, 665, 685, 687, 709, 748, 785, 862, 870, 915, 974, 978, 986, 991-992, 1000, 1025-1027, 1092, 1094-1095, 1112, 1114-1115, 1151, 1159, 1208-1209, 1211, 1222, 1224, 1226, 1241, 1249-1250, 1258, 1267, 1270-1271, 1276-1278, 1281-1282, 1293, 1295, 1297, 1309, 1324, 1327, 1334, 1337, 1358, 1360-1363, 1402, 1408, 1414, 1416, 1423, 1429

#### `src/external/physfs/extras/abs-file.h`
- Totals: ownership 0, c-api 5
- C API lines: 99, 101, 110, 121-122

#### `src/external/physfs/extras/globbing.c`
- Totals: ownership 1, c-api 10
- Ownership lines: 105
- C API lines: 31, 35-36, 91, 94-96, 116, 119-120

#### `src/external/physfs/extras/globbing.h`
- Totals: ownership 0, c-api 2
- C API lines: 78-79

#### `src/external/physfs/extras/ignorecase.c`
- Totals: ownership 0, c-api 11
- C API lines: 32, 49, 51-53, 88, 91-92, 116, 119-120

#### `src/external/physfs/extras/ignorecase.h`
- Totals: ownership 0, c-api 1
- C API lines: 78

#### `src/external/physfs/extras/physfs_rb/physfs/physfsrwops.c`
- Totals: ownership 0, c-api 15
- C API lines: 25, 27, 99, 101, 113, 115, 124, 126, 138, 140, 161, 163, 173, 179, 185

#### `src/external/physfs/extras/physfs_rb/physfs/physfsrwops.h`
- Totals: ownership 0, c-api 4
- C API lines: 42, 54, 66, 78

#### `src/external/physfs/extras/physfs_rb/physfs/rb_physfs.c`
- Totals: ownership 0, c-api 17
- C API lines: 49, 66, 69, 92, 133-134, 151, 165, 179, 233-234, 289, 303-304, 372, 383, 394

#### `src/external/physfs/extras/physfs_rb/physfs/rb_physfs.h`
- Totals: ownership 0, c-api 0

#### `src/external/physfs/extras/physfs_rb/physfs/rb_physfs_file.c`
- Totals: ownership 3, c-api 12
- Ownership lines: 68, 75, 80
- C API lines: 20, 36, 60, 62, 79, 92, 112, 134, 156, 176, 199-200

#### `src/external/physfs/extras/physfs_rb/physfs/rb_physfs_file.h`
- Totals: ownership 0, c-api 1
- C API lines: 13

#### `src/external/physfs/extras/physfs_rb/physfs/rb_sdl_rwops.c`
- Totals: ownership 3, c-api 10
- Ownership lines: 110, 117, 122
- C API lines: 19, 37, 49-50, 63, 81, 102, 104, 121, 135

#### `src/external/physfs/extras/physfs_rb/physfs/rb_sdl_rwops.h`
- Totals: ownership 0, c-api 1
- C API lines: 13

#### `src/external/physfs/extras/physfshttpd.c`
- Totals: ownership 5, c-api 12
- Ownership lines: 145-146, 154, 160, 163
- C API lines: 67, 76, 78-79, 106, 108-111, 151, 173, 226

#### `src/external/physfs/extras/physfsrwops.c`
- Totals: ownership 0, c-api 17
- C API lines: 26, 28, 36, 38, 96, 98, 110, 112, 121, 123, 135, 137, 161, 163, 173, 179, 185

#### `src/external/physfs/extras/physfsrwops.h`
- Totals: ownership 0, c-api 4
- C API lines: 43, 55, 67, 79

#### `src/external/physfs/extras/physfsunpack.c`
- Totals: ownership 2, c-api 10
- Ownership lines: 104, 136
- C API lines: 11, 13, 28, 37, 40-41, 51, 66, 100, 141

#### `src/external/physfs/extras/selfextract.c`
- Totals: ownership 0, c-api 4
- C API lines: 37, 55-56, 59

#### `src/external/physfs/physfs.c`
- Totals: ownership 3, c-api 240
- Ownership lines: 2157, 2165, 2172
- C API lines: 22-25, 32, 34-36, 46, 48, 71, 97, 128-134, 138-139, 150, 152, 155, 157-159, 183, 196-198, 218-220, 256-258, 269, 271-272, 299, 301, 314, 333, 335, 348-349, 361, 372, 374-375, 391-392, 394, 397, 417, 419-421, 462, 464, 509, 538-539, 542-543, 589, 591, 607, 609, 613, 620-622, 632, 639, 641-642, 660, 662-664, 682, 733, 735, 778, 780-781, 803-804, 863, 869, 871-872, 879, 885, 891, 897, 903, 909, 911, 922, 947, 949-951, 988, 994, 996-998, 1025, 1031, 1033, 1039, 1050, 1052, 1064, 1067, 1069, 1079-1080, 1083-1085, 1087, 1134-1135, 1145-1146, 1148, 1181-1183, 1185, 1187-1189, 1271, 1273, 1275-1276, 1339, 1341-1343, 1383, 1386, 1399, 1402, 1417, 1420, 1433, 1435-1436, 1445, 1449, 1466-1468, 1498, 1501-1503, 1534, 1549, 1551, 1554-1555, 1557, 1573, 1575, 1578, 1589, 1596, 1613, 1619, 1622, 1636, 1641, 1660, 1664, 1679, 1685, 1699, 1703, 1720, 1726, 1740, 1742, 1744, 1753-1755, 1797, 1803, 1809, 1811-1812, 1823-1824, 1835, 1872, 1874-1875, 1882, 1907, 1909, 1929, 1939, 1973, 1976, 1988, 1992, 1995, 1997-1998, 2008, 2011, 2023, 2025, 2035, 2037, 2046, 2048, 2070, 2072, 2077, 2079, 2113, 2125, 2127, 2142, 2153, 2161, 2169, 2190, 2198, 2209, 2213

#### `src/external/physfs/physfs.h`
- Totals: ownership 0, c-api 80
- C API lines: 343, 378-381, 464, 487, 563, 754, 771, 791, 880-882, 910, 943, 971, 1012, 1032, 1052, 1072, 1088, 1115, 1141, 1166, 1187, 1206-1207, 1224-1225, 1244, 1257, 1274, 1293, 1338, 1357, 1540, 1557, 1573, 1590, 1606, 1623, 1639, 1656, 1675, 1694, 1713, 1732, 1747, 1762, 1777, 1792, 1807, 1822, 1837, 1852, 1870, 1888, 1906, 1924, 1983-1987, 2018, 2061, 2085, 2112, 2146-2147, 2181, 2217, 2258, 2260, 2281, 2303, 2329, 2355, 2382

#### `src/external/physfs/physfs_byteorder.c`
- Totals: ownership 0, c-api 24
- C API lines: 95, 105, 115, 125, 135, 145, 155, 165, 175, 185, 195, 205, 216, 224, 232, 240, 248, 256, 264, 272, 280, 288, 296, 304

#### `src/external/physfs/physfs_casefolding.h`
- Totals: ownership 0, c-api 1
- C API lines: 1755

#### `src/external/physfs/physfs_internal.h`
- Totals: ownership 3, c-api 73
- Ownership lines: 80-82
- C API lines: 69, 76, 716, 738, 750, 760-761, 764-765, 772, 784, 794, 806, 822, 836, 849, 858, 870, 878, 895, 905, 911, 916, 923, 930, 937, 948, 964-966, 1007-1009, 1061, 1067, 1073, 1079, 1105, 1143, 1160, 1178, 1192, 1205, 1219, 1231, 1243, 1253, 1262, 1274, 1283, 1291, 1298, 1305, 1313, 1322, 1330, 1335, 1343, 1364-1366, 1377, 1380-1381, 1391, 1404, 1412, 1426, 1436, 1444, 1461, 1473, 1487

#### `src/external/physfs/physfs_platforms.h`
- Totals: ownership 0, c-api 0

#### `src/external/physfs/physfs_unicode.c`
- Totals: ownership 0, c-api 18
- C API lines: 38, 40, 194, 212, 234, 236, 323, 328, 334, 353, 359, 363-364, 386, 395, 409, 424, 444

#### `src/external/physfs/platform/beos.cpp`
- Totals: ownership 2, c-api 21
- Ownership lines: 226, 232
- C API lines: 59, 82, 84, 99, 111, 143, 160, 175, 191, 193, 200, 202, 206, 209, 211, 218, 224, 230, 236, 242, 248

#### `src/external/physfs/platform/macosx.c`
- Totals: ownership 0, c-api 29
- C API lines: 32, 39, 45, 51-52, 111, 165, 167, 179-180, 191, 195, 204, 220, 229-230, 294, 300, 317, 348, 355, 362, 368, 379, 381, 385, 394, 401, 410

#### `src/external/physfs/platform/os2.c`
- Totals: ownership 0, c-api 62
- C API lines: 34, 37, 96-97, 112, 114-115, 128, 170, 174, 219, 236, 255, 270, 279, 281, 288, 294, 300, 308, 314, 324-326, 331-332, 353, 356-357, 359, 394, 396, 427, 429-430, 440, 446, 461, 465, 480, 484, 509, 513, 529, 536, 552, 559, 572, 582, 592, 605, 611, 617, 626, 652, 662, 666, 670, 674, 680, 687, 694

#### `src/external/physfs/platform/pocketpc.c`
- Totals: ownership 0, c-api 66
- C API lines: 28-29, 38, 40-41, 80, 84-86, 139, 145, 151, 157, 164, 166, 170, 173, 184, 190, 193, 204-206, 211-212, 233, 236, 244, 247-248, 252-253, 283, 299, 305, 307, 313, 316, 327, 330-331, 353, 359, 365, 367, 373, 385, 394, 410, 419, 435, 459, 483, 506, 526, 528, 536, 545, 547, 573, 575, 579, 585, 591, 597, 604

#### `src/external/physfs/platform/posix.c`
- Totals: ownership 0, c-api 43
- C API lines: 33, 36, 38-39, 52, 56, 70, 74, 88, 90, 97, 99, 119, 127, 135, 143-145, 150, 171, 174-175, 177, 180, 216, 240, 250, 254, 280, 284, 290, 296, 302, 319, 324, 336, 354, 373, 382, 390, 398, 407, 414

#### `src/external/physfs/platform/unix.c`
- Totals: ownership 0, c-api 43
- C API lines: 64, 70, 94, 96, 128, 136, 148, 151-153, 168, 202, 206, 210, 234, 236-237, 253, 261, 278, 287, 289-290, 302, 310-311, 343, 351-355, 366, 368, 372, 375, 386, 390, 392, 403, 405, 419, 421

#### `src/external/physfs/platform/windows.c`
- Totals: ownership 0, c-api 115
- C API lines: 57, 65, 67, 70, 83, 85, 89, 110, 118-128, 130, 132, 134, 149, 160, 162, 174, 186, 196, 212, 224, 235, 250, 266, 325, 335, 337-339, 367, 371, 376, 378, 403, 468, 491, 510, 514, 525, 534, 537, 541, 554, 556, 563, 565, 580, 599, 632, 644-646, 651-652, 673, 676-677, 684-686, 715, 735, 758, 779, 781-782, 805, 811-812, 829, 897, 929, 931, 967, 985, 988-989, 1016, 1022, 1028, 1030, 1037, 1048, 1057, 1072, 1081, 1096, 1134, 1159, 1183, 1203, 1205, 1213, 1238, 1255, 1257, 1261, 1267, 1273, 1279, 1336, 1347, 1356, 1383-1384, 1399

#### `src/external/physfs/zlib123/adler32.c`
- Totals: ownership 0, c-api 1
- C API lines: 59

#### `src/external/physfs/zlib123/compress.c`
- Totals: ownership 0, c-api 6
- C API lines: 23-25, 63-65

#### `src/external/physfs/zlib123/crc32.c`
- Totals: ownership 0, c-api 5
- C API lines: 68, 70, 159, 184, 232

#### `src/external/physfs/zlib123/crc32.h`
- Totals: ownership 0, c-api 0

#### `src/external/physfs/zlib123/deflate.c`
- Totals: ownership 0, c-api 41
- C API lines: 73, 76-78, 80, 82-83, 85, 89, 91, 94, 97, 207, 224, 227, 231, 317, 320, 360, 421, 461, 493, 519, 557, 901-903, 933-934, 958, 987, 1028, 1042, 1176, 1233, 1267, 1391, 1449, 1555, 1684, 1691

#### `src/external/physfs/zlib123/deflate.h`
- Totals: ownership 0, c-api 17
- C API lines: 81, 83, 97, 99, 113, 128, 134, 200, 203, 210, 214, 238, 282-284, 286-287

#### `src/external/physfs/zlib123/gzio.c`
- Totals: ownership 2, c-api 53
- Ownership lines: 43-44
- C API lines: 35, 60-62, 64-65, 76, 78-82, 94-95, 101-104, 209-210, 221, 223, 239, 262, 289, 356, 399-401, 519, 540, 542, 545, 564, 603, 605, 640, 644, 695, 711, 750, 772, 860, 894, 911, 921, 936, 956, 987, 989, 991-992, 1020

#### `src/external/physfs/zlib123/infback.c`
- Totals: ownership 0, c-api 5
- C API lines: 19, 32, 78-79, 84

#### `src/external/physfs/zlib123/inffast.c`
- Totals: ownership 0, c-api 0

#### `src/external/physfs/zlib123/inffast.h`
- Totals: ownership 0, c-api 0

#### `src/external/physfs/zlib123/inffixed.h`
- Totals: ownership 0, c-api 2
- C API lines: 10, 87

#### `src/external/physfs/zlib123/inflate.c`
- Totals: ownership 0, c-api 8
- C API lines: 95, 100, 147, 189, 210-211, 216, 1171

#### `src/external/physfs/zlib123/inflate.h`
- Totals: ownership 0, c-api 1
- C API lines: 114

#### `src/external/physfs/zlib123/inftrees.c`
- Totals: ownership 0, c-api 0

#### `src/external/physfs/zlib123/inftrees.h`
- Totals: ownership 0, c-api 1
- C API lines: 53

#### `src/external/physfs/zlib123/trees.c`
- Totals: ownership 0, c-api 59
- C API lines: 102, 108, 122-123, 143-151, 153-155, 157-159, 190, 193, 247, 332, 383, 412, 456-457, 491-492, 494, 496-497, 578, 580, 582, 620-621, 623-624, 708-709, 753-754, 804, 839, 868-869, 893, 922-923, 1023, 1073-1075, 1127, 1162, 1179, 1198-1199

#### `src/external/physfs/zlib123/trees.h`
- Totals: ownership 0, c-api 2
- C API lines: 73, 102

#### `src/external/physfs/zlib123/uncompr.c`
- Totals: ownership 0, c-api 3
- C API lines: 27-29

#### `src/external/physfs/zlib123/zconf.h`
- Totals: ownership 0, c-api 2
- C API lines: 280, 284

#### `src/external/physfs/zlib123/zlib.h`
- Totals: ownership 0, c-api 30
- C API lines: 77-78, 83, 87, 91, 114, 117, 119, 539, 737, 877-878, 1009-1010, 1024-1025, 1047-1048, 1068, 1085, 1122, 1135, 1142, 1236, 1260, 1285, 1318, 1320, 1326, 1329

#### `src/external/physfs/zlib123/zutil.c`
- Totals: ownership 3, c-api 9
- Ownership lines: 306-307, 314
- C API lines: 14, 27, 123, 133, 150-151, 161-162, 174

#### `src/external/physfs/zlib123/zutil.h`
- Totals: ownership 0, c-api 7
- C API lines: 53, 94-95, 235-237, 244

#### `src/external/yam/yam.cpp`
- Totals: ownership 4, c-api 7
- Ownership lines: 43, 46, 232, 250
- C API lines: 50, 58, 63, 91, 112, 270, 289

#### `src/external/yam/yam.h`
- Totals: ownership 0, c-api 11
- C API lines: 28-29, 37-39, 44-45, 48-49, 63, 71

## Files With No Detected Raw Pointer Patterns

0 files:
