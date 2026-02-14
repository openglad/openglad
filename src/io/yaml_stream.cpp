/*
 * og::io YAML stream wrapper implementation.
 *
 * This translation unit is the boundary for vendored YAML libraries.
 */

#include <openglad/io/yaml_stream.h>

#include "yam.h" // third_party/yam (wraps libyaml)

#include <utility>

namespace og::io {

namespace {
YamlEventType to_event_type(Yam::EventType t)
{
    switch (t)
    {
        case Yam::NONE: return YamlEventType::None;
        case Yam::BEGIN_SEQUENCE: return YamlEventType::BeginSequence;
        case Yam::END_SEQUENCE: return YamlEventType::EndSequence;
        case Yam::BEGIN_MAPPING: return YamlEventType::BeginMapping;
        case Yam::END_MAPPING: return YamlEventType::EndMapping;
        case Yam::ALIAS: return YamlEventType::Alias;
        case Yam::SCALAR: return YamlEventType::Scalar;
        case Yam::PAIR: return YamlEventType::Pair;
    }
    return YamlEventType::None;
}

YamlParseResult to_parse_result(Yam::ParseResultEnum r)
{
    switch (r)
    {
        case Yam::DONE: return YamlParseResult::Done;
        case Yam::OK: return YamlParseResult::Ok;
        case Yam::ERROR: return YamlParseResult::Error;
    }
    return YamlParseResult::Error;
}

std::string safe_copy(const char* s)
{
    return s ? std::string(s) : std::string();
}
} // namespace

struct YamlParser::Impl {
    Yam yam;
    YamlEvent current;
};

YamlParser::YamlParser() : impl_(std::make_unique<Impl>()) {}
YamlParser::~YamlParser() = default;
YamlParser::YamlParser(YamlParser&&) noexcept = default;
YamlParser& YamlParser::operator=(YamlParser&&) noexcept = default;

void YamlParser::set_input(ReadHandler handler, void* data)
{
    impl_->yam.set_input(reinterpret_cast<Yam_Read_Handler*>(handler), data);
}

void YamlParser::close_input()
{
    impl_->yam.close_input();
}

YamlParseResult YamlParser::parse_next()
{
    const auto r = impl_->yam.parse_next();
    impl_->current.type = to_event_type(impl_->yam.event.type);
    impl_->current.scalar = safe_copy(impl_->yam.event.scalar);
    impl_->current.value = safe_copy(impl_->yam.event.value);
    return to_parse_result(r);
}

const YamlEvent& YamlParser::event() const
{
    return impl_->current;
}

struct YamlEmitter::Impl {
    Yam yam;
};

YamlEmitter::YamlEmitter() : impl_(std::make_unique<Impl>()) {}
YamlEmitter::~YamlEmitter() = default;
YamlEmitter::YamlEmitter(YamlEmitter&&) noexcept = default;
YamlEmitter& YamlEmitter::operator=(YamlEmitter&&) noexcept = default;

bool YamlEmitter::set_output(WriteHandler handler, void* data)
{
    return impl_->yam.set_output(reinterpret_cast<Yam_Write_Handler*>(handler), data);
}

void YamlEmitter::close_output()
{
    impl_->yam.close_output();
}

bool YamlEmitter::emit_scalar(const char* scalar)
{
    return impl_->yam.emit_scalar(scalar);
}

bool YamlEmitter::emit_pair(const char* key, const char* value)
{
    return impl_->yam.emit_pair(key, value);
}

bool YamlEmitter::emit_begin_mapping()
{
    return impl_->yam.emit_begin_mapping();
}

bool YamlEmitter::emit_end_mapping()
{
    return impl_->yam.emit_end_mapping();
}

bool YamlEmitter::emit_begin_sequence()
{
    return impl_->yam.emit_begin_sequence();
}

bool YamlEmitter::emit_end_sequence()
{
    return impl_->yam.emit_end_sequence();
}

} // namespace og::io

