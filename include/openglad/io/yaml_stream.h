/*
 * YAML stream wrapper used by OpenGlad code.
 *
 * This header intentionally does not include any vendored YAML headers.
 * The implementation lives in og_io and includes third_party/yam + libyaml.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace og::io {

// Matches libyaml's handler signatures (kept vendor-free here).
using ReadHandler = int (*)(void* data, unsigned char* buffer, std::size_t size, std::size_t* size_read);
using WriteHandler = int (*)(void* data, unsigned char* buffer, std::size_t size);

enum class YamlParseResult {
    Done = 0,
    Ok,
    Error,
};

enum class YamlEventType {
    None = 0,
    BeginSequence,
    EndSequence,
    BeginMapping,
    EndMapping,
    Alias,
    Scalar,
    Pair,
};

struct YamlEvent {
    YamlEventType type = YamlEventType::None;
    std::string scalar;
    std::string value;
};

class YamlParser {
public:
    YamlParser();
    ~YamlParser();
    YamlParser(const YamlParser&) = delete;
    YamlParser& operator=(const YamlParser&) = delete;
    YamlParser(YamlParser&&) noexcept;
    YamlParser& operator=(YamlParser&&) noexcept;

    void set_input(ReadHandler handler, void* data);
    void close_input();

    YamlParseResult parse_next();
    const YamlEvent& event() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class YamlEmitter {
public:
    YamlEmitter();
    ~YamlEmitter();
    YamlEmitter(const YamlEmitter&) = delete;
    YamlEmitter& operator=(const YamlEmitter&) = delete;
    YamlEmitter(YamlEmitter&&) noexcept;
    YamlEmitter& operator=(YamlEmitter&&) noexcept;

    // Returns false if the output could not be initialized.
    bool set_output(WriteHandler handler, void* data);
    void close_output();

    bool emit_scalar(const char* scalar);
    bool emit_pair(const char* key, const char* value);
    bool emit_begin_mapping();
    bool emit_end_mapping();
    bool emit_begin_sequence();
    bool emit_end_sequence();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace og::io

