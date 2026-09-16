#ifndef _TEST_FRAME_CAPTURE_H__
#define _TEST_FRAME_CAPTURE_H__

// The presented-frame capture handshake, shared by the injector suites.
//
// It was written for tests/integration/test_campaign_zone_ui.cpp and hoisted
// here verbatim when the menu-capture scenes needed the same rules: one
// implementation of the rule, not five (the maintainer principle from PR
// #245). The commentary below is the ruling text and travels with the code.
// What changed in the move: the names (ZoneShotResult -> CapturedFrame,
// g_zone_shots -> g_captured_frames, capture_zone_frame ->
// capture_presented_frame, verify_zone_shots -> verify_captured_frames), the
// output directory is now the CALLER's (each suite resolves its own env var),
// and the frozen 320x200x3 bytes are kept on the result so a caller can
// assert on the pixels itself through take_captured_frames().

#include <gtest/gtest.h>

#include <openglad/interface/screen.h>
#include <openglad/platform/game_session.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdio>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// Presenter pause handshake (TESTING; the uxshots capture seam). Defined in
// src/interface/screen.cpp.
extern std::atomic_bool g_test_present_pause_requested;
extern std::atomic_bool g_test_present_paused;

// The pixel source. The frozen frame is read back through the live session's
// screen, which is what the presenter just showed.
inline screen* capture_frame_screen()
{
    return og::runtime::current_session->myscreen_;
}

// One capture's outcome. Recorded on the INJECTOR thread and asserted on
// the main thread by verify_captured_frames: a gtest failure raised from an
// injector that then dies mid-flow takes its own message with it, and the
// capture flows already report their observations this way.
struct CapturedFrame {
    std::string name;
    std::string path;       // "" = no output dir, nothing to write
    bool captured = false;  // the presenter handshake froze a real frame
    bool written = false;   // the PPM exists on disk afterwards
    std::size_t nonblack = 0;
    // The frozen frame itself, 320*200*3 bytes, RGB row-major. Empty when
    // the handshake never froze anything.
    std::vector<Uint8> rgb;
};

inline std::mutex g_captured_frame_mutex;
inline std::vector<CapturedFrame> g_captured_frames;

// The nonblank bar the uxshots probe uses (test_uxshots_probe.cpp): a
// settled 320x200 menu frame inks far more than this, a black or
// half-cleared one far less.
inline constexpr std::size_t kCapturedFrameMinNonblackPixels = 1000;

// Visual-verification capture (the uxshots PresentedFramePause handshake,
// minimal form): freeze the settled 320x200 frame, count its ink, and dump
// it as a PPM when the caller asked for a file. The frame is read back
// either way -- "the capture produced a blank screen" is a real failure
// whether or not anyone asked for the file. Runs on the injector thread.
//
// output_dir is the caller's: nullptr or "" means capture but write nothing.
inline void capture_presented_frame(const char* name, const char* output_dir)
{
    CapturedFrame result;
    result.name = name;
    const bool want_file = output_dir != nullptr && output_dir[0] != '\0';
    if (want_file) {
        std::error_code error;
        std::filesystem::create_directories(output_dir, error);
        if (!error)
            result.path = std::string(output_dir) + "/" + name + ".ppm";
    }

    auto record = [&result] {
        const std::lock_guard<std::mutex> lock(g_captured_frame_mutex);
        g_captured_frames.push_back(std::move(result));
    };

    bool expected = false;
    if (!g_test_present_pause_requested.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        record();
        return;
    }
    const Uint64 deadline = SDL_GetTicks() + 30000;
    while (!g_test_present_paused.load(std::memory_order_acquire)) {
        if (SDL_GetTicks() >= deadline) {
            g_test_present_pause_requested.store(false,
                                                 std::memory_order_release);
            record();
            return;
        }
        SDL_Delay(1);
    }

    result.rgb.reserve(320 * 200 * 3);
    screen* scr = capture_frame_screen();
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 320; ++x) {
            Uint8 r = 0, g = 0, b = 0;
            scr->get_pixel(x, y, &r, &g, &b);
            if (r != 0 || g != 0 || b != 0)
                ++result.nonblack;
            result.rgb.push_back(r);
            result.rgb.push_back(g);
            result.rgb.push_back(b);
        }
    }
    g_test_present_pause_requested.store(false, std::memory_order_release);
    result.captured = true;

    if (!result.path.empty()) {
        FILE* f = fopen(result.path.c_str(), "wb");
        if (f != nullptr) {
            fprintf(f, "P6\n320 200\n255\n");
            fwrite(result.rgb.data(), sizeof(Uint8), result.rgb.size(), f);
            fclose(f);
            std::error_code exists_error;
            result.written =
                std::filesystem::exists(result.path, exists_error) &&
                !exists_error;
            fprintf(stderr, "  [uxshot] wrote %s\n", result.path.c_str());
        }
    }
    record();
}

// Hand the ledger to the caller, emptied. For a flow that wants to assert on
// the frozen bytes itself (an ink band, a colour at a coordinate) instead of
// -- or before -- the standard verification below.
inline std::vector<CapturedFrame> take_captured_frames()
{
    std::vector<CapturedFrame> frames;
    const std::lock_guard<std::mutex> lock(g_captured_frame_mutex);
    frames.swap(g_captured_frames);
    return frames;
}

// Main-thread verification of every frame the finished flow recorded, and
// the reason the capture seam has teeth: a re-capture run that quietly
// stopped producing stills (or started producing black ones) now fails the
// test instead of leaving the media script nothing to convert. Clears the
// ledger, so each flow only ever answers for its own captures.
inline void verify_captured_frames(const char* flow, std::size_t expected)
{
    const std::vector<CapturedFrame> frames = take_captured_frames();
    EXPECT_EQ(expected, frames.size())
        << flow << ": the flow did not reach every capture point";
    for (const CapturedFrame& frame : frames) {
        EXPECT_TRUE(frame.captured)
            << flow << ": " << frame.name << " never froze a presented frame";
        EXPECT_GE(frame.nonblack, kCapturedFrameMinNonblackPixels)
            << flow << ": " << frame.name << " is blank (" << frame.nonblack
            << " nonblack pixels)";
        if (frame.path.empty())
            continue;
        EXPECT_TRUE(frame.written)
            << flow << ": " << frame.name << " produced no file at "
            << frame.path;
    }
}

#endif  // _TEST_FRAME_CAPTURE_H__
