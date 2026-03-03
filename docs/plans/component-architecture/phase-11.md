# Phase 11: Define Inter-Component Interfaces

**Part of:** [Component Architecture Plan](README.md)
**Depends on:** [Phase 10](phase-10.md)
**Followed by:** [Phase 12](phase-12.md)
**Key types:** [Target Architecture](target-architecture.md)

---

## Goal

Formalize the boundaries between components with explicit interfaces
so that dependencies are through narrow, well-defined APIs.

## Gameplay → Outside (events — already exists, extend)

Current `EventKind` enum gains new types:

```cpp
ScoreChange,              // a=team, b=points
RequestExitConfirmation,  // a=dest_level; text=prompt
WithdrawToLevel,          // a=dest_level
```

The caller (interface/platform) drains `SimEventLog` after each tick and
handles these.

## Interface → Platform (new callback bridge)

```cpp
// include/openglad/interface/platform_bridge.h
struct PlatformBridge {
    // Rendering
    std::function<void()> present_frame;

    // Audio
    std::function<void(int sound_id)> play_sound;
    std::function<void(const char* music_file)> play_music;
    std::function<void()> stop_music;

    // Render surface management — returns abstract video base, NOT SDL_Surface*
    // Phase 10 splits video into an abstract base (interface) and SDL concrete
    // (platform). This callback returns the abstract base type.
    std::function<video*(int w, int h)> create_surface;
};
```

**Important:** No SDL types in this interface. The whole point of PlatformBridge
is that the interface layer doesn't know about SDL. Platform registers concrete
SDL (or headless no-op) implementations. Interface calls these instead of SDL
functions directly. This replaces the current `LevelDataHooks` pattern with a more general
mechanism.

## Resources API (narrow filesystem interface)

```cpp
// include/openglad/resources/filesystem.h
namespace og::resources {
    bool mount(const char* archive, const char* mountpoint);
    std::vector<uint8_t> read_file(const char* path);
    bool write_file(const char* path, const void* data, size_t len);
    bool exists(const char* path);
    // ... etc
}
```

Implementation in resources/ wraps PhysFS. Other components use this API instead
of PhysFS directly.

## Risk

Low — this is API design. Can be iterated as components are moved.
