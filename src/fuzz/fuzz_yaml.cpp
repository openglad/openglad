// Fuzz harness for YAML configuration parsing.
//
// OpenGlad uses YAML (via libyaml/yam) for configuration files
// (openglad.yaml) and campaign metadata (campaign.yaml). The
// YamlParser class wraps libyaml and exposes a pull-style event API.
//
// This harness feeds fuzzer-generated data through the YamlParser
// to exercise libyaml's parsing logic and the event-processing
// loop used in gparser.cpp and level_data.cpp.

#include <SDL2/SDL.h>
#include <cstddef>
#include <cstdint>
#include <string>

#include <openglad/io/yaml_stream.h>

// Matches the rwops_read_handler signature used in OpenGlad
static int mem_read_handler(void *data, unsigned char *buffer, size_t size, size_t *size_read)
{
    SDL_RWops *rw = static_cast<SDL_RWops *>(data);
    *size_read = SDL_RWread(rw, buffer, 1, size);
    return 1;
}

static void fuzz_parse_yaml(const uint8_t *data, size_t size)
{
    SDL_RWops *rw = SDL_RWFromConstMem(data, static_cast<int>(size));
    if (!rw)
        return;

    og::io::YamlParser yaml;
    yaml.set_input(mem_read_handler, rw);

    // Drain events, mimicking the pattern in gparser.cpp.
    // Cap iterations to avoid hangs on pathological input.
    static constexpr int kMaxEvents = 10000;
    int events = 0;
    og::io::YamlParseResult result;
    while ((result = yaml.parse_next()) == og::io::YamlParseResult::Ok)
    {
        // Access event fields to exercise string copying
        const og::io::YamlEvent &ev = yaml.event();
        (void)ev.type;
        (void)ev.scalar;
        (void)ev.value;

        if (++events >= kMaxEvents)
            break;
    }

    yaml.close_input();
    SDL_RWclose(rw);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    // Limit input size to keep execution fast; YAML parsing is O(n) but
    // constant factors are high with sanitizer instrumentation.
    if (size == 0 || size > 4096)
        return 0;

    fuzz_parse_yaml(data, size);
    return 0;
}
