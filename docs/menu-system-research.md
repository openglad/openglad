# OpenGlad Menu System Replacement Research

Date: 2026-02-12

## 1. Executive Summary (Recommendation)

OpenGlad’s current menu/UI system is a bespoke, pixel-coordinate, event-loop-driven “button array + callback ID” framework tightly coupled to the game’s rendering (`screen`/`video`) and global input state (`keystates`, `allbuttons`, `myscreen`). Replacing it with an off-the-shelf UI framework is feasible, but the real work is not “drawing widgets”; it is bridging OpenGlad’s 320x200/palette-era rendering model, input model (keyboard + joystick mappings + OUYA/touch/web shims), and existing menu-flow semantics (blocking loops returning `EXIT/REDRAW/OK`) into a new UI runtime.

**Recommendation:**

1. **Default path (lowest risk / best ROI): modernize the existing menu system rather than adopting a third-party UI framework.**
   - Add a small internal UI abstraction layer (layout + focus/navigation + rendering primitives + input events) while continuing to render with existing primitives and assets.
   - Convert each screen to use the new abstractions incrementally, keeping compatibility with the existing test interaction model (button IDs and coordinates) during the transition.

2. **If the goal is truly “standard framework” with strong skinning and data-driven menus:** prefer **RmlUi** over ImGui/Nuklear/TGUI.
   - RmlUi (HTML/CSS) is the closest match for skinnable, content-driven game menus.
   - Expect a larger one-time integration cost: renderer + texture/font pipeline + input mapping + build integration + web (Emscripten) considerations.

3. **ImGui/Nuklear** are good for debug/editor UIs and fast prototypes, but they will fight the desired “fantasy game aesthetic” unless you invest significantly in custom styling and assets. They also encourage immediate-mode patterns that don’t map cleanly to OpenGlad’s current “menu is a blocking loop that returns a value” flow.

## 2. Current System Analysis

### 2.1 Code Layout (Two Realities)

This repo currently has two “menu system shapes” depending on branch:

- **`master` (the primary branch in this repo):** menu code is largely monolithic in `src/picker.cpp`, using `src/button.cpp`/`src/button.h`, `src/input.cpp`/`src/input.h`, `src/screen.cpp`/`src/screen.h`.
- **`cpp-modernization-plan` (a refactor branch present in the workspace):** the same system is partially modularized into `src/ui/picker*.cpp` plus modernized headers (`src/input/button.*`, `src/runtime/screen.*`, etc.). This branch is closer to what the prompt explicitly references (`picker.cpp`, `picker_*.cpp`, `picker_input.cpp`, etc.).

This report describes behavior common to both, and calls out notable differences.

### 2.2 Core Concepts

**Buttons are defined as static arrays of `button` structs**.

- A `button` includes:
  - `id` (string used heavily by tests and interaction APIs)
  - `label`
  - `hotkey` (SDL scancode)
  - `x,y,w,h` in **game-space coordinates** (assumes 320x200)
  - `myfun` (callback ID, an integer enum-like value)
  - `arg1` (callback argument)
  - `MenuNav nav` (up/down/left/right indices in the same array)
  - `hidden` / `no_draw`

**At runtime, `init_buttons()` creates `vbutton` objects** (one per `button`) and stores them in a global array `allbuttons`.

- `allbuttons` is a global, fixed-size slot array (`MAX_BUTTONS`, 50).
- `vbutton` owns rendering and hit-testing state (x/y bounds, focus flags, optional pixie-based graphic).
- A “menu screen” generally owns nothing except the static `button[]` array and a local index for highlight.

**Callbacks are dispatched by integer IDs (`myfun`)** via `vbutton::do_call()`, which switches over the callback ID and calls the corresponding free function (`create_team_menu`, `create_hire_menu`, etc.).

### 2.3 Menu Flow and Navigation

**Each menu is a blocking loop** that:

1. Calls `init_buttons(buttons, num_buttons)`.
2. Performs a fade transition (`fadeblack()` patterns vary by menu and branch).
3. Enters a loop while `(retvalue & EXIT)` is false:
   - Poll input (mouse click edge detection, keyboard hotkeys, joystick-based “player keys”).
   - If mouse click: `localbuttons->leftclick()` (which scans `allbuttons` and dispatches).
   - If controller nav: `handle_menu_nav()` moves the highlighted index using `MenuNav` (array indices) and can “activate” a button with `KEY_FIRE`.
   - Redraw the menu using `draw_buttons()`, plus menu-specific extra drawing.

**Navigation is explicit, not spatial.**

- `MenuNav` is stored per-button and points to *array indices* for up/down/left/right.
- That means navigation graphs must be maintained manually whenever buttons are added/removed.

**Return values are overloaded.**

- Menu callbacks often return:
  - `EXIT` to unwind to a previous menu loop
  - `REDRAW` to trigger `init_buttons()` again
  - `OK` or `0` to continue

In the modernization branch this is partially clarified with an enum (`MenuResult`) but the underlying bitmask/flag semantics remain.

### 2.4 Input Handling Model

**Input is handled via a global polling layer (`input.cpp`).**

- `get_input_events(POLL|WAIT)` pumps SDL events and calls `handle_events(event)`.
- Keyboard:
  - Uses `SDL_GetKeyboardState()` and a global `keystates` pointer.
  - “Hotkeys” are implemented by checking `keystates[hotkey]` and then *busy-waiting until key release*.
- Mouse:
  - Maintains a global `MouseState` updated in event handlers.
  - Converts window coords to game coords using viewport globals (`viewport_w/h`, `viewport_offset_x/y`), supporting overscan scaling and web/touch.
- Joystick:
  - A `JoyData` mapping layer maps “player keys” (KEY_UP, KEY_FIRE, etc.) to joystick buttons/axes/hats.
- OUYA:
  - Special-cases OUYA controller events and in some cases fakes key events.
- Touch/web:
  - Touch input can fake keyboard and mouse behavior.
  - Some menu click logic was changed to avoid blocking loops (Emscripten ASYNCIFY constraints), but other parts still contain “wait for release” loops.

### 2.5 Rendering and Coupling to `screen`/SDL

Menu widgets are drawn using the same `screen`/`video` pipeline as the game:

- `vbutton::vdisplay()` draws either:
  - a pixie-based button graphic (`pixieN` from the game’s loader), plus text, or
  - a hand-drawn beveled rectangle using palette indices.

The menu code relies on global `myscreen` and assumes:

- fixed internal resolution (320x200)
- palette-indexed colors
- availability of game loader assets (`level_data.myloader`) for button graphics
- in-menu sound playback via `myscreen->soundp`

This coupling is the single biggest migration constraint for third-party UI frameworks.

### 2.6 Test and Tooling Coupling

The current system has **strong automated-test coupling**:

- Tests enumerate `allbuttons[]` and match by `id` (see `tests/test_interact.h` in the modernization branch).
- Tests use button geometry (center point) and the viewport conversion globals to inject clicks.
- Under `TESTING`, `init_buttons()` uses a mutex to prevent races between the menu loop and injector threads.

Any framework replacement that does not preserve a similar “interactables registry” will require rewriting a significant portion of menu tests.

### 2.7 Pain Points

Structural and maintainability pain points:

- **Hardcoded layout** (pixel coordinates, 320x200 assumptions, no real layout engine).
- **Manual navigation graphs** (`MenuNav` as array indices, fragile under edits).
- **Global mutable state** (`myscreen`, `allbuttons`, `keystates`, viewport globals).
- **Callback dispatch by integer ID** (hard to trace, easy to mis-wire, encourages large switch statements).
- **Menu loops are blocking** and often contain busy-waits on key release.
- **Rendering is tightly coupled** to the game’s loader, palette, and draw primitives.
- **Test coupling** depends on `allbuttons` being populated with stable IDs and geometry.

## 3. SDL2-Friendly UI/Menu Library Options

Below is a comparison of libraries that can plausibly work with an SDL2 C++ game.

### 3.0 Rendering Reality Check (OpenGlad)

OpenGlad’s rendering pipeline (in the refactor branch) uses an `SDL_Renderer` and updates textures from surfaces as part of a custom `Screen` abstraction (see `src/render/sai2x.h`). That matters because:

- Libraries that can render through **SDL_Renderer** can often be layered as an overlay with minimal graphics-API surgery.
- Libraries that require **OpenGL contexts** (or assume OpenGL-first rendering) likely force larger architectural changes (window creation flags, GL context lifetime, mixing GL with SDL_Renderer, etc.).

### 3.1 Comparison Table

| Option | Paradigm | SDL2 Compatibility | Theming / Skinning | Gamepad Nav | License | Maintenance / Community | Fit for OpenGlad |
|---|---|---|---|---|---|---|---|
| Dear ImGui | Immediate mode | Strong; official SDL2 backends incl. SDL_Renderer/OpenGL | Style is code-driven; can be heavily themed but is not “game UI first” | Possible but you implement nav patterns and focus | MIT | Very active, huge community | Easiest integration technically; aesthetic mismatch risk; best for debug/tools |
| Nuklear | Immediate mode, single-header | Works; SDL examples exist; you typically wire your own renderer | Basic theming; limited compared to RmlUi | Possible, but more DIY | Public domain / MIT | Smaller community; stable | Lightweight; still not great for “fantasy” menus without custom drawing |
| TGUI | Retained-mode widgets | SDL backend exists; integration depends on rendering backend | Theme files; widget styling supported | Supported at widget level; gamepad may require mapping | zlib | Active (recent releases) | More “app UI” feel; heavier integration, dependency and rendering constraints |
| NanoGUI | Retained-ish; OpenGL-centric | SDL not primary; typically uses GLFW/OpenGL | Limited; mostly OpenGL/NanoVG style | Not a focus | zlib | Sporadic; more niche | Poor fit unless OpenGlad goes OpenGL-first |
| RmlUi (libRocket successor) | HTML/CSS retained DOM | SDL input is fine; multiple render backends exist (incl. SDL renderer, OpenGL) | Best-in-class skinning via CSS, images, fonts | You can map gamepad to navigation; not automatic “console UI” | MIT | Active community; game UI focus | Best match for skinnable game menus, but highest integration cost |
| SDL2_gfx + custom | Primitives only | Native SDL2 | You build it | You build it | zlib (SDL2_gfx) | Mature but limited scope | Not a replacement; a helper for modernizing the existing system |
| CEGUI (additional option) | Retained-mode widgets | Possible via OpenGL/SDL renderers | Strong but heavy | Possible | (varies by version; generally permissive) | Smaller, older ecosystem | High complexity; likely overkill |

### 3.2 Practical Integration Notes (Per Library)

#### Dear ImGui

- **Best-case integration path for OpenGlad:** use the official SDL2 backend + the `SDL_Renderer` backend (`imgui_impl_sdlrenderer2`). This aligns with OpenGlad’s existing renderer model and avoids requiring a GL context.
- **Menu suitability:** workable for menus, but you will be rebuilding “game menu” conventions (focus rings, transitions, controller-first UX, sound hooks, animations) yourself.
- **Aesthetic risk:** large. ImGui can be styled, but it is fundamentally geared toward tool/debug UI.

#### Nuklear

- **Integration path:** usually you wire Nuklear to SDL2 yourself (event translation + renderer). Nuklear can be extremely lightweight, but you inherit the maintenance burden of the integration layer.
- **Menu suitability:** fine for simple menus; more limited for rich, skinnable fantasy UI.

#### TGUI

- **Integration path:** TGUI’s SDL backend tutorials indicate the SDL backend requires an OpenGL context and an SDL window created with `SDL_WINDOW_OPENGL`.
- **Implication for OpenGlad:** unless OpenGlad is reworked to be OpenGL-first (or you accept mixed rendering complexity), TGUI is not a great fit.

#### NanoGUI

- **Integration path:** OpenGL-first (typically GLFW). SDL2 isn’t the primary target.
- **Implication:** generally a poor match for OpenGlad’s current SDL_Renderer-centric architecture.

#### RmlUi

- **Integration path:** RmlUi provides SDL platform backends and multiple renderer backends, including an SDL renderer backend.
- **Why it stands out for OpenGlad:** it’s one of the few “game UI” libraries that can plausibly fit an SDL2 + SDL_Renderer pipeline while giving you strong skinning.
- **Cost drivers:** building a robust asset pipeline (fonts, images), mapping gamepad navigation, and rethinking menu flow to match a retained DOM.

### 3.3 Notes and Sources (Selected)

- Dear ImGui: MIT license and broad backend support.
  - https://github.com/ocornut/imgui
- Nuklear: single-header immediate-mode GUI; permissive licensing.
  - https://github.com/Immediate-Mode-UI/Nuklear
- TGUI: zlib license; includes SDL backend support.
  - https://github.com/texus/TGUI
  - https://tgui.eu/tutorials/0.9/sdl-backend/
  - https://tgui.eu/tutorials/latest-stable/backend-sdl-opengl3/
- NanoGUI: zlib license; OpenGL-first design.
  - https://github.com/wjakob/nanogui
- RmlUi: MIT license; HTML/CSS UI for games.
  - https://github.com/mikke89/RmlUi
  - https://mikke89.github.io/RmlUiDoc/pages/cpp_manual/integrating.html
- SDL2_gfx: primitive drawing helpers (not a menu framework).
  - https://github.com/ferzkopp/SDL2_gfx

(Where possible, prefer upstream repos for license and maintenance verification; GitHub release cadence is a useful proxy for “alive”.)

## 4. Migration Feasibility Assessment

### 4.1 What Would Need To Change (High-Level)

Regardless of library choice, a “real” replacement requires:

- **A UI runtime loop integration point**
  - Today menus are blocking loops that own input polling and draw directly.
  - A framework UI wants: `begin_frame() -> build/update UI -> render UI -> end_frame()`.

- **A rendering bridge**
  - OpenGlad renders to an internal 320x200 buffer and uses palette-indexed primitives, then scales to window via SDL textures/surfaces.
  - Most UI libraries assume:
    - OpenGL/Vulkan/DirectX immediate rendering, or
    - SDL_Renderer drawing with RGBA textures.

- **An input bridge**
  - Map SDL events to UI library input (mouse, keyboard, text input).
  - Map OpenGlad “player key” abstractions and joystick/touch shims to UI navigation.

- **A resource bridge**
  - Fonts: current menus use built-in bitmap font rendering. Frameworks want TTF or their own font system.
  - Images: OpenGlad assets are pixie/palette; frameworks want RGBA textures.

- **Test bridge**
  - Existing tests depend on `allbuttons[]` and `id` strings.
  - A migration must either:
    - re-implement an equivalent “interactables list” for the new UI, or
    - rewrite menu tests.

### 4.2 Incremental vs All-at-Once

**Incremental migration is possible** if you introduce a “menu host” abstraction:

- Keep the current menu loop structure, but allow a menu to be implemented by either:
  - legacy `button[]` arrays + `vbutton`, or
  - framework-driven UI (ImGui/RmlUi/TGUI) inside the same loop.

However, incremental migration has costs:

- You will run **two UI systems** side-by-side for a while.
- You must maintain consistent transitions (fade, backdrop drawing, audio cues).
- Input routing becomes tricky (who owns text input? who consumes Escape/back?).

**All-at-once migration** reduces long-term complexity but is high risk:

- Every menu must be rebuilt before the game is usable.
- Tests would likely need a coordinated rewrite.

### 4.3 Biggest Risks

- **Rendering mismatch**: palette-indexed / fixed-resolution UI vs RGBA, DPI-scaled UI.
- **Web/Emscripten build**: immediate-mode loops and event pumping must remain compatible.
- **Input capture**: current code uses global keystate polling and busy-waits; frameworks prefer event-driven input.
- **Aesthetic parity**: achieving OpenGlad’s fantasy/retro look inside a framework may be non-trivial.
- **Build/dependency complexity**: adding a new third-party UI library across platforms (Windows/Linux/web).
- **Regression surface area**: menu flows are intertwined with save data, campaign selection, level picker, etc.

### 4.4 Rough Scope Estimates (Order-of-Magnitude)

These are intentionally broad; actual effort depends on platform targets and how much polish is required.

- **ImGui integration for debug menus only**: ~2-5 days.
- **ImGui as the primary game menu UI (polished)**: ~3-8 weeks.
  - Most time goes into look-and-feel (fonts, images, animations), input/nav, and recreating flows.

- **RmlUi integration + a themed main menu + 1-2 submenus**: ~3-6 weeks.
  - Includes renderer and resource pipeline, plus basic document navigation.
- **Full RmlUi migration of all menus**: ~2-4 months.

- **TGUI integration + several menus**: ~4-10 weeks.
  - Dependent on rendering backend fit and theming effort.

- **“Modernize existing system” (see next section)**: ~2-6 weeks for a meaningful improvement pass.

## 5. Custom Modernization Alternative (No External UI Framework)

If the goal is “less painful menus” rather than “adopt a known UI brand”, modernizing the existing system is likely cheaper and safer.

### 5.1 What “Modernization” Could Mean

1. **Introduce a small internal widget/layout layer**
   - Add containers: vertical stack, grid, centered panel.
   - Express button positions in relative/layout terms rather than fixed pixels.

2. **Replace manual `MenuNav` maintenance**
   - Auto-generate navigation graph from layout order (or spatial adjacency).
   - Keep explicit overrides only where necessary.

3. **Decouple UI from globals**
   - Create a `MenuContext` containing:
     - rendering target (still `screen`)
     - input snapshot (mouse state, key pressed, text input)
     - sound hooks
   - Minimize direct use of `myscreen`, `keystates`, and global `allbuttons`.

4. **Normalize menu return semantics**
   - Replace bitmask return values with a small enum/struct: `MenuAction { Exit, Redraw, Push(MenuId), Pop, ... }`.

5. **Preserve test interaction**
   - Keep IDs and expose a registry of interactables to tests.
   - This reduces test churn compared to adopting a third-party library.

### 5.2 Why This Might Be Less Work

- It preserves the existing rendering look and asset formats.
- It avoids a hard dependency on a rendering backend (OpenGL vs SDL_Renderer vs software).
- It retains existing mental model and menu flow while improving maintainability.
- It avoids having to port pixie/palette assets to RGBA textures.

## 6. Recommended Approach (Pragmatic Plan)

### Option A (Recommended): Modernize In-Place

1. Define a minimal internal UI layer (layout + focus + event handling) that renders using existing primitives.
2. Keep compatibility shims so each menu can still expose:
   - button IDs
   - interactable geometry
   - stable navigation
3. Migrate menus incrementally:
   - start with main menu and options
   - then team build/hire/train
   - then save/load/progress

### Option B: Adopt RmlUi for Skinnable Menus

1. Prototype: integrate RmlUi with SDL2 input and a rendering backend compatible with OpenGlad’s present pipeline.
2. Build a minimal “UI texture/font system”:
   - decide whether to generate RGBA textures from pixie assets or author new UI textures.
3. Rebuild one menu end-to-end (main menu) and confirm:
   - theming pipeline
   - focus and controller navigation
   - web build behavior (if required)
4. Only then commit to migrating remaining menus.

### Option C: Adopt ImGui/Nuklear/TGUI

- Use ImGui/Nuklear primarily for debug/editor tooling.
- If used for game menus, expect significant custom skinning and a careful navigation/input design.

## 7. Risks and Unknowns

- Platform support requirements are not explicitly defined here (desktop only vs web vs mobile). This strongly affects the “best” library choice.
- Current menu aesthetic requirements (pixel-perfect retro vs scalable modern UI) will determine whether a framework’s rendering model is acceptable.
- RmlUi/TGUI integration details depend on the desired rendering path (OpenGL vs SDL_Renderer vs software blit into OpenGlad’s buffer).
- Test strategy: keeping current UI tests passing with minimal rewrites is much easier if you keep an `allbuttons`-like interactables registry.
