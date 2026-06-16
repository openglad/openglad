/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * og::io YAML stream wrapper implementation.
 *
 * This translation unit is the boundary for vendored YAML libraries.
 */

#include <openglad/resources/yaml_stream.h>

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
    // Thread the og::io::ReadHandler and user data through libyaml's single
    // void* so we never have to reinterpret_cast between function-pointer types.
    ReadHandler read_handler = nullptr;
    void* read_data = nullptr;

    // Trampoline whose type matches Yam_Read_Handler (yaml_read_handler_t)
    // exactly; forwards to the stored og::io::ReadHandler.
    static int read_trampoline(void* data, unsigned char* buffer, std::size_t size, std::size_t* size_read)
    {
        auto* self = static_cast<Impl*>(data);
        return self->read_handler(self->read_data, buffer, size, size_read);
    }
};

YamlParser::YamlParser() : impl_(std::make_unique<Impl>()) {}
YamlParser::~YamlParser() = default;
YamlParser::YamlParser(YamlParser&&) noexcept = default;
YamlParser& YamlParser::operator=(YamlParser&&) noexcept = default;

void YamlParser::set_input(ReadHandler handler, void* data)
{
    impl_->read_handler = handler;
    impl_->read_data = data;
    impl_->yam.set_input(&Impl::read_trampoline, impl_.get());
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
    // Thread the og::io::WriteHandler and user data through libyaml's single
    // void* so we never have to reinterpret_cast between function-pointer types.
    WriteHandler write_handler = nullptr;
    void* write_data = nullptr;

    // Trampoline whose type matches Yam_Write_Handler (yaml_write_handler_t)
    // exactly; forwards to the stored og::io::WriteHandler.
    static int write_trampoline(void* data, unsigned char* buffer, std::size_t size)
    {
        auto* self = static_cast<Impl*>(data);
        return self->write_handler(self->write_data, buffer, size);
    }
};

YamlEmitter::YamlEmitter() : impl_(std::make_unique<Impl>()) {}
YamlEmitter::~YamlEmitter() = default;
YamlEmitter::YamlEmitter(YamlEmitter&&) noexcept = default;
YamlEmitter& YamlEmitter::operator=(YamlEmitter&&) noexcept = default;

bool YamlEmitter::set_output(WriteHandler handler, void* data)
{
    impl_->write_handler = handler;
    impl_->write_data = data;
    return impl_->yam.set_output(&Impl::write_trampoline, impl_.get());
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

