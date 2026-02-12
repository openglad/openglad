# Modernization Audit Round 2 (Fresh Pass)

Branch: `cpp-modernization-plan`  
Scope: entire repo (all tracked `*.c/*.cc/*.cpp/*.h/*.hpp`, including `util/`, `tests/`, and vendored `src/external/`)  
Rule of engagement: audit only (no code changes in this round).

## Findings

### 1. Raw Pointers / Ownership / Manual Lifetime

1. **Global `localbuttons` + special-cased `allbuttons[0]` lifetime is fragile**
   - File/line: `src/picker.cpp:109`, `src/picker.cpp:672`, `src/button.cpp:488`
   - Current:
     ```cpp
     // src/picker.cpp
     vbutton * localbuttons; //global so we can delete the buttons anywhere
     ...
     if(localbuttons != nullptr)
         delete localbuttons; //we'll make a new set
     ```
     ```cpp
     // src/button.cpp
     for (i=1; i < MAX_BUTTONS; i++) // skip # 0!
     {
         if (allbuttons[i])
             delete allbuttons[i];
         allbuttons[i] = nullptr;
     }
     ...
     allbuttons[i] = new vbutton(...);
     return allbuttons[0];
     ```
   - Suggested improvement:
     - Make `init_buttons()` own and delete all button instances consistently (including index 0), or replace `allbuttons[]` with `std::array<std::unique_ptr<vbutton>, MAX_BUTTONS>` and return a non-owning pointer/reference to the “primary” button.
     - Remove `localbuttons` as an owning raw pointer; make it a non-owning alias or eliminate entirely.
   - Priority: **high** (lifetime coupling is error-prone; easy to introduce UAF/dangling `allbuttons[0]`).

2. **`ctx().prefs` is deleted manually (raw owning pointer)**
   - File/line: `src/picker.cpp:643`
   - Current:
     ```cpp
     if (ctx().prefs)
     {
         delete ctx().prefs;
         ctx().prefs = nullptr;
     }
     ```
   - Suggested improvement:
     - Convert `ctx().prefs` to `std::unique_ptr<options>` (or whatever the concrete type is) so ownership is explicit and deletion cannot be skipped/doubled.
   - Priority: **high** (manual delete + global-ish state; easy to regress).

3. **Manual `new[]` for grid resize can be replaced with safe ownership**
   - File/line: `src/level_data.cpp:531`
   - Current:
     ```cpp
     int size = width*height;
     unsigned char* new_grid = new unsigned char[size];
     ...
     grid.free();
     grid.data.reset(new_grid);
     ```
   - Suggested improvement:
     - Use `auto new_grid = std::make_unique<unsigned char[]>(size);` and move/swap into `grid.data`.
     - Optionally replace repeated `rand()%4` with a small helper returning `PIX_GRASS{1..4}`.
   - Priority: **medium** (current code is mostly safe, but still manual allocation and exception-unsafety).

4. **`CampaignEntry` owns `icon` via raw pointer; also has a null-deref hazard**
   - File/line: `src/campaign_picker.cpp:160`, `src/campaign_picker.cpp:208`
   - Current:
     ```cpp
     if(icondata.valid())
         icon = new pixie(icondata);
     ...
     icon->drawMix(x, y, myscreen->viewob[0].get());
     ```
   - Suggested improvement:
     - Make `icon` a `std::unique_ptr<pixie>` (or store by value if feasible).
     - Guard in `draw()` when `icon == nullptr` and draw a placeholder or skip drawing. (Right now a campaign without `icon.pix` can crash.)
   - Priority: **high** (potential crash; ownership can be simplified).

5. **`pick_campaign()` stores heap objects in `std::vector<CampaignEntry*>`**
   - File/line: `src/campaign_picker.cpp:298`, `src/campaign_picker.cpp:310`, `src/campaign_picker.cpp:610`
   - Current:
     ```cpp
     std::vector<CampaignEntry*> entries;
     ...
     entries.push_back(new CampaignEntry(cid, num_completed));
     ...
     for(auto* entry : entries) { delete entry; }
     ```
   - Suggested improvement:
     - Use `std::vector<std::unique_ptr<CampaignEntry>>` and keep `CampaignEntry*` non-owning views only where needed (e.g., `result = entries[idx].get()`).
   - Priority: **high** (manual delete in multiple branches; easier to leak on early returns).

6. **`pick_level()` manages `BrowserEntry* entries[]` via manual new/delete and shifting**
   - File/line: `src/level_picker.cpp:288`, `src/level_picker.cpp:422`, `src/level_picker.cpp:634`
   - Current:
     ```cpp
     BrowserEntry* entries[NUM_BROWSE_RADARS];
     ...
     entries[i] = new BrowserEntry(...);
     ...
     delete entries[0];
     ...
     for(int i = 0; i < NUM_BROWSE_RADARS; i++) { delete entries[i]; }
     ```
   - Suggested improvement:
     - Use `std::array<std::unique_ptr<BrowserEntry>, NUM_BROWSE_RADARS>`.
     - Encapsulate the “shift up/down” logic in a small helper to reduce pointer juggling.
   - Priority: **high** (complex lifetime logic; high regression surface).

7. **Tests still use raw `new`/`delete` in many places**
   - File/line: examples include `tests/test_save_load_team.cpp:63`, `tests/test_view_lifecycle.cpp:41`
   - Current:
     ```cpp
     guy* soldier = new guy(FAMILY_SOLDIER);
     ...
     viewscreen* vs = new viewscreen(0, 0, 320, 200, 0);
     ```
   - Suggested improvement:
     - Prefer `std::unique_ptr` (or stack objects) in tests; it improves readability and reduces leak noise under sanitizers.
   - Priority: **low** (tests are short-lived, but it helps sanitizer signal quality).

### 2. File I/O Robustness / Error Handling Gaps

8. **Scenario loading uses unchecked `SDL_RWread()` extensively (v2-v5 loaders)**
   - File/line: `src/level_data.cpp:665`, `src/level_data.cpp:673`, `src/level_data.cpp:680`
   - Current:
     ```cpp
     SDL_RWread(infile, newgrid, 8, 1);
     ...
     SDL_RWread(infile, &listsize, 2, 1);
     ...
     SDL_RWread(infile, &temporder, 1, 1);
     ```
   - Suggested improvement:
     - Use an “exact read” helper everywhere (there is already `rw_read_exact()` in other modules like `src/save_data.cpp`).
     - Propagate typed errors (or at least return failure) instead of continuing with partially read data.
   - Priority: **high** (corrupt/truncated files can lead to undefined behavior / uninitialized values).

9. **`READ_OR_RETURN` macro checks `SDL_RWread()` incorrectly**
   - File/line: `src/level_data.cpp:1057`
   - Current:
     ```cpp
     #define READ_OR_RETURN(ctx, ptr, size, n) \
     do{ \
         if(!SDL_RWread(ctx, ptr, size, n)) \
         { \
             Log("Read error: {}\n", SDL_GetError()); \
             return 0; \
         } \
     } while(0)
     ```
   - Suggested improvement:
     - Fix the semantics to `SDL_RWread(...) == n` (not just non-zero), otherwise short reads for `n > 1` are silently accepted.
     - Prefer a real function (`bool rw_read_exact(SDL_RWops*, ...)`) to avoid macro footguns.
   - Priority: **high** (this is a correctness bug, not just style).

10. **Scenario object count (`listsize`) lacks sanity checks**
   - File/line: `src/level_data.cpp:673`
   - Current:
     ```cpp
     SDL_RWread(infile, &listsize, 2, 1);
     for (i=0; i < listsize; i++) { ... }
     ```
   - Suggested improvement:
     - Validate `listsize` against reasonable limits (e.g., maximum objects that can fit into map bounds, or a hard cap).
     - If possible, validate against remaining file length (SDL_RWops size/seek support varies, but you can still bound reads defensively).
   - Priority: **high** (malformed files can cause long loops, OOM, or inconsistent state).

11. **File deletion ignores return values and errors**
   - File/line: `src/io.cpp:403`, `src/io.cpp:406`, `src/io.cpp:417`
   - Current:
     ```cpp
     remove(path.c_str());
     ...
     remove(path.c_str());
     ```
   - Suggested improvement:
     - Use `std::filesystem::remove(path, ec)` (or check `remove()` return) and log/report failures via existing typed error enums.
   - Priority: **medium** (silent failures can produce confusing UI states).

12. **`list_files()` uses raw `char**` enumeration; could be wrapped for safety**
   - File/line: `src/io.cpp:208`
   - Current:
     ```cpp
     char** files = PHYSFS_enumerateFiles(dirname.c_str());
     ...
     PHYSFS_freeList(files);
     ```
   - Suggested improvement:
     - Wrap the returned list in an RAII type (custom deleter) to guarantee `PHYSFS_freeList()` on early returns/exceptions.
     - Consider returning `std::vector<std::string>` rather than `std::list`, since you sort anyway.
   - Priority: **low** (current code is correct, but RAII reduces future risk).

### 3. C-Style Strings / Parsing / Cast Safety

13. **`atoi()` is used broadly (no error reporting, treats invalid input as 0)**
   - File/line: `src/campaign_picker.cpp:41`, `src/io.cpp:354`, `src/level_picker.cpp:156`, `src/picker.cpp:1263`
   - Current:
     ```cpp
     int toInt(const std::string& s) { return atoi(s.c_str()); }
     ...
     result.push_back(atoi(e->c_str()));
     ...
     return atoi(n);
     ```
   - Suggested improvement:
     - Use `std::from_chars` and handle invalid/overflow cases (return `std::optional<int>` or a typed error).
     - This matters for user-provided strings (“Enter Level ID”) and for parsing filenames.
   - Priority: **medium** (prevents silent “0” behavior and reduces surprising UI flows).

14. **`isalpha(*c)` and `tolower(c)` are used without `unsigned char` casts**
   - File/line: `src/level_picker.cpp:140`, `src/picker.cpp:1269`, `src/util.cpp:149`
   - Current:
     ```cpp
     if(!gotNum && isalpha(*e)) ...
     while(isalpha(*n)) { ... }
     str[i] = tolower(str[i]);
     ```
   - Suggested improvement:
     - Use `std::isalpha(static_cast<unsigned char>(ch))` and `std::tolower(static_cast<unsigned char>(ch))`.
     - Without this, chars with the high bit set can trigger undefined behavior depending on platform/locale.
   - Priority: **medium** (correctness + portability; low cost to fix).

15. **Legacy `char*`-returning text input API uses static buffers and fixed size**
   - File/line: `src/text.cpp:443`
   - Current:
     ```cpp
     static char editstring[100], firststring[100];
     ...
     if ( temptext != nullptr &&
           (current_length + short(strlen(temptext)) < maxlength) )
     {
         ...
         editstring[current_length] = c;
     }
     ```
   - Suggested improvement:
     - Clamp `maxlength` to `sizeof(editstring)-1` (today `maxlength` can exceed 99; the check is against `maxlength`, not the buffer size).
     - Long term: make `input_string()` return `std::optional<std::string>` directly (you already added `input_string_value()` and `input_string_ex_value()`).
   - Priority: **high** (potential buffer overflow if callers pass `maxlength > 99`).

16. **Dead/commented C string code inside `glad.cpp` should not be resurrected as-is**
   - File/line: `src/glad.cpp:861`
   - Current:
     ```cpp
     /*
       ...
       case ACT_RANDOM: strcpy(message, "CHARGE"); break;
       case ACT_GUARD: strcpy(message, "GUARD"); break;
       ...
     */
     ```
   - Suggested improvement:
     - If this block is re-enabled in the future, rewrite it using the current `std::string message` (it will not compile or will be wrong as-is).
     - Consider deleting the block entirely if it’s truly obsolete; otherwise mark it clearly as “historical” to avoid confusion.
   - Priority: **low** (currently commented out; risk is future reintroduction).

### 4. Thread Safety / Global State / Hidden Coupling

17. **`mounted_campaign` is a writable global (`static std::string`)**
   - File/line: `src/io.cpp:222`
   - Current:
     ```cpp
     static std::string mounted_campaign;
     ```
   - Suggested improvement:
     - Move this into a context object (e.g., `GameContext`, `SaveData`, or a dedicated campaign manager) so ownership and mutation points are explicit.
     - If background threads can touch it (tests with injector threads), add synchronization or enforce single-thread access.
   - Priority: **medium** (hidden shared state makes tests and refactors harder).

18. **Tests use background threads to drive menus; `allbuttons[]` updates are mostly unsynchronized**
   - File/line: `src/button.cpp:485`, `src/picker.cpp:705`
   - Current:
     ```cpp
     // src/button.cpp (only init is guarded, and only in TESTING)
     SDL_LockMutex(get_allbuttons_mutex());
     ...
     SDL_UnlockMutex(get_allbuttons_mutex());
     ```
     ```cpp
     // src/picker.cpp
     allbuttons[2]->label = buttons[2].label;
     ```
   - Suggested improvement:
     - Use an RAII lock wrapper for the test mutex and document which operations must hold it.
     - Consider making `allbuttons` fully owned by a single-threaded UI “model” and have tests interact through a synchronized API (instead of poking globals).
   - Priority: **medium** (reduces flaky tests and hard-to-debug races).

### 5. SDL / Event Loop Modernization

19. **Busy-wait loops with `SDL_Delay(1)` are widespread in menu flows**
   - File/line: `src/level_picker.cpp:404`, `src/campaign_picker.cpp:462`
   - Current:
     ```cpp
     while(mymouse.left)
     {
         SDL_Delay(1);
         get_input_events(POLL);
     }
     ```
   - Suggested improvement:
     - Prefer `SDL_WaitEventTimeout()` or a centralized “pump with timeout” that yields efficiently.
     - This is especially relevant on web builds and battery-powered devices.
   - Priority: **medium** (performance/UX; lowers CPU burn).

20. **Manual lock/unlock for SDL mutex in `init_buttons()`**
   - File/line: `src/button.cpp:485`
   - Current:
     ```cpp
     SDL_LockMutex(get_allbuttons_mutex());
     ...
     SDL_UnlockMutex(get_allbuttons_mutex());
     ```
   - Suggested improvement:
     - Wrap `SDL_mutex*` in a small RAII guard (`struct SdlMutexLock { ... }`) to ensure unlock on early returns.
   - Priority: **low** (only in TESTING today, but still a good hardening).

### 6. Data / Configuration Loading & Validation

21. **Campaign YAML parsing assumes required fields exist and are valid**
   - File/line: `src/campaign_picker.cpp:138`
   - Current:
     ```cpp
     if(std::string(yam.event.scalar) == "version")
         version = toInt(yam.event.value);
     ...
     else if(std::string(yam.event.scalar) == "first_level")
         first_level = toInt(yam.event.value);
     ```
   - Suggested improvement:
     - Validate presence/range of required keys (`id`, `first_level`, `version`).
     - Prefer a typed parse result (already used elsewhere) instead of silently defaulting to 0 on `atoi()` failures.
   - Priority: **medium** (prevents weird campaign picker behavior and potential crashes).

22. **Scenario title/grid strings are manually packed into fixed-size buffers**
   - File/line: `src/level_data.cpp:1485`, `src/level_data.cpp:1489`
   - Current:
     ```cpp
     snprintf(temp_grid, 9, "%s", this->grid_file.c_str());  // max 8 chars
     snprintf(scentitle, sizeof(scentitle), "%s", this->title.c_str());
     ```
   - Suggested improvement:
     - Centralize serialization/deserialization with explicit truncation rules and validations (e.g., ensure `grid_file` is <= 8 chars *before* write).
     - Consider a small `struct ScenarioHeader { ... }` with explicit field sizes and helper (de)serialization.
   - Priority: **low** (current code uses `snprintf`, but format constraints should be explicit and tested).

### 7. Dead Code / TODOs / Repo Hygiene

23. **Known global-leak TODO for text glyph pixies**
   - File/line: `src/text.cpp:25`, `src/text.cpp:49`
   - Current:
     ```cpp
     static PixieData letters1;
     static PixieData letters_big;
     ...
     // TODO: Free letters somewhere better (it's a global leak for now, so no hurry)
     ```
   - Suggested improvement:
     - Move `letters1/letters_big` ownership into a singleton with explicit `init()`/`shutdown()` called from `io_init()`/`io_exit()` (or equivalent), or store them in a `std::optional<PixieData>` and free them at program end deterministically.
   - Priority: **low** (leak is intentional, but it’s still technical debt).

24. **`LevelData::delete_objects()` contains a “naughty things” FIXME around obmap cleanup**
   - File/line: `src/level_data.cpp:615`
   - Current:
     ```cpp
     // FIXME: Freeing them here does naughty things!
     ```
   - Suggested improvement:
     - Treat this as an ownership/invariant smell: define who owns walkers vs. who indexes them.
     - Add invariant checks/tests around obmap lifecycle (especially for editor flows and scenario reload).
   - Priority: **medium** (comment suggests known edge-case crashes or corruption).

25. **Repo root is generating lots of untracked artifacts (`io_tmp_*.txt`, `core`, build dirs)**
   - File/line: N/A (observed via `git status`)
   - Current:
     - Many untracked files like `io_tmp_*.txt`, `build*/`, and a `core` artifact.
   - Suggested improvement:
     - Add/update `.gitignore` entries for test/runtime temp outputs (or redirect tests to write under `build-test/`).
   - Priority: **low** (hygiene; reduces accidental commits and keeps working tree clean).

### 8. `util/` Tools (Barely Modernized, Still C-Style)

26. **`util/pixieread.c` has classic unsafe patterns (buffer overflow, wrong `argc` type)**
   - File/line: `util/pixieread.c:6`
   - Current:
     ```c
     int main(char argc, char **argv)
     {
         char filename[40];
         ...
         strcpy(filename,argv[1]);
     }
     ```
   - Suggested improvement:
     - Fix `main` signature to `int main(int argc, char** argv)`.
     - Replace `strcpy` with `snprintf` (or just use `argv[1]` directly).
     - If these tools matter long-term, consider porting them to C++ with `std::vector<uint8_t>` and RAII.
   - Priority: **high** (straight buffer overflow opportunity in a shipped source tree).
   - Nostalgic comments: preserve `//useful for debugging shit` as-is if keeping the tool.

27. **`util/pixedit.c` uses `sprintf`**
   - File/line: `util/pixedit.c:94`
   - Current:
     ```c
     sprintf(buffer, "Frame %i at %ix", frame, mult);
     ```
   - Suggested improvement:
     - Use `snprintf(buffer, sizeof(buffer), ...)`.
   - Priority: **medium**.

28. **`util/pixconvert.cpp` uses `malloc/free` and has FIXME for error handling**
   - File/line: `util/pixconvert.cpp:206`, `util/pixconvert.cpp:135`
   - Current:
     ```cpp
     return 0;  // FIXME: Handle errors better
     ...
     data = (unsigned char *)malloc(size);
     ...
     free(data);
     ```
   - Suggested improvement:
     - Use `std::vector<unsigned char>` or `std::unique_ptr<unsigned char[]>`.
     - Convert remaining C APIs to `std::filesystem` + robust error reporting, consistent with the main game’s typed IO errors.
   - Priority: **medium**.

### 9. Vendored `src/external/` Libraries

29. **Vendored libs contain many insecure legacy patterns (e.g., `sprintf`, `vsprintf`, C-casts)**
   - File/line: examples: `src/external/physfs/zlib123/gzio.c:613`, `src/external/libzip/zip_close.c:603`
   - Current:
     - Multiple uses of `sprintf` / `vsprintf` and extensive C-style casting, which is normal for upstream C code but increases audit burden.
   - Suggested improvement:
     - Prefer upgrading vendored libraries to known-good upstream versions (and track their versions explicitly).
     - If upgrading is not feasible, consider isolating them behind wrappers and compiling with warnings/sanitizers tuned to reduce noise while still catching real issues.
   - Priority: **medium** (security posture + maintenance; not a “refactor in place” recommendation).

