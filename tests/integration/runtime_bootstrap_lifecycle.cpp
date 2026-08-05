#include <openglad/core/test_trace.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace og::runtime::testing
{
int run_runtime_bootstrap_lifecycle_validation(int argc, char* argv[]);
}

namespace
{
std::mutex allbuttons_mutex;

class ScopedTempDirectory
{
public:
    ScopedTempDirectory()
    {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        path_ = std::filesystem::temp_directory_path() /
            ("openglad_runtime_bootstrap_" + std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~ScopedTempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

void push_key_when_intro_is_ready(const std::atomic<bool>& finished)
{
    for (int attempt = 0; attempt < 30000 && !finished.load(); ++attempt)
    {
        if (trace_contains("intro_state", "page ready"))
        {
            SDL_Event event{};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.down = true;
            event.key.key = SDLK_SPACE;
            event.key.scancode = SDL_SCANCODE_SPACE;
            (void)SDL_PushEvent(&event);

            event = {};
            event.type = SDL_EVENT_KEY_UP;
            event.key.down = false;
            event.key.key = SDLK_SPACE;
            event.key.scancode = SDL_SCANCODE_SPACE;
            (void)SDL_PushEvent(&event);
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
} // namespace

std::mutex& get_allbuttons_mutex()
{
    return allbuttons_mutex;
}

int main(int argc, char* argv[])
{
    ScopedTempDirectory test_root;
    SDL_setenv_unsafe("OPENGLAD_CONFIG_DIR", test_root.path().string().c_str(),
                      1);
    SDL_setenv_unsafe("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv_unsafe("SDL_AUDIODRIVER", "dummy", 1);
    SDL_setenv_unsafe("SDL_RENDER_DRIVER", "software", 1);

    trace_clear();
    std::atomic<bool> finished = false;
    std::thread injector(push_key_when_intro_is_ready, std::cref(finished));

    int result = 1;
    try
    {
        result = og::runtime::testing::
            run_runtime_bootstrap_lifecycle_validation(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "runtime bootstrap exception: %s\n",
                     error.what());
    }

    finished.store(true);
    injector.join();
    return result;
}
