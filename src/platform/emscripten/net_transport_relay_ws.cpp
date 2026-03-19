#include <openglad/platform/net_transport_relay_ws.h>

#include "net_transport_emscripten_ws_detail.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <deque>
#include <format>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace og::sim {
namespace {

constexpr unsigned short kWebSocketReadyStateOpen = 1;
constexpr std::uint8_t kRelaySendToPeerTag = 1u;
constexpr std::uint8_t kRelayReceiveFromPeerTag = 2u;
constexpr std::uint8_t kRelayBroadcastTag = 3u;
constexpr std::size_t kRelayPeerHeaderSize = 5u;

std::string trim_copy(std::string_view text)
{
    const auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    const auto begin =
        std::find_if(text.begin(), text.end(), not_space);
    const auto end =
        std::find_if(text.rbegin(), text.rend(), not_space).base();
    if (begin >= end)
        return {};
    return std::string(begin, end);
}

std::optional<std::size_t> find_json_field_value(
    std::string_view text,
    std::string_view key)
{
    const std::string needle = std::format("\"{}\"", key);
    const std::size_t key_pos = text.find(needle);
    if (key_pos == std::string_view::npos)
        return std::nullopt;

    std::size_t colon_pos = text.find(':', key_pos + needle.size());
    if (colon_pos == std::string_view::npos)
        return std::nullopt;
    ++colon_pos;

    while (colon_pos < text.size() &&
           std::isspace(static_cast<unsigned char>(text[colon_pos])))
    {
        ++colon_pos;
    }
    if (colon_pos >= text.size())
        return std::nullopt;
    return colon_pos;
}

std::optional<std::string> extract_json_string_field(
    std::string_view text,
    std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value() || text[*value_pos] != '"')
        return std::nullopt;

    std::string result;
    bool escape = false;
    for (std::size_t index = *value_pos + 1; index < text.size(); ++index)
    {
        const char ch = text[index];
        if (escape)
        {
            result.push_back(ch);
            escape = false;
            continue;
        }
        if (ch == '\\')
        {
            escape = true;
            continue;
        }
        if (ch == '"')
            return result;
        result.push_back(ch);
    }

    return std::nullopt;
}

std::optional<std::uint32_t> extract_json_u32_field(
    std::string_view text,
    std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value())
        return std::nullopt;

    std::size_t end = *value_pos;
    while (end < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[end])))
    {
        ++end;
    }
    if (end == *value_pos)
        return std::nullopt;

    std::uint32_t value = 0;
    const auto [ptr, ec] = std::from_chars(
        text.data() + *value_pos,
        text.data() + end,
        value);
    if (ec != std::errc{} || ptr != text.data() + end)
        return std::nullopt;
    return value;
}

std::optional<bool> extract_json_bool_field(std::string_view text,
                                            std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value())
        return std::nullopt;

    const std::string_view rest = text.substr(*value_pos);
    if (rest.rfind("true", 0) == 0)
        return true;
    if (rest.rfind("false", 0) == 0)
        return false;
    return std::nullopt;
}

std::vector<PeerId> extract_json_u32_array_field(std::string_view text,
                                                 std::string_view key)
{
    const auto value_pos = find_json_field_value(text, key);
    if (!value_pos.has_value() || text[*value_pos] != '[')
        return {};

    std::vector<PeerId> result;
    std::size_t cursor = *value_pos + 1;
    while (cursor < text.size())
    {
        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])))
        {
            ++cursor;
        }

        if (cursor >= text.size() || text[cursor] == ']')
            break;

        const std::size_t start = cursor;
        while (cursor < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[cursor])))
        {
            ++cursor;
        }
        if (cursor == start)
            break;

        std::uint32_t value = 0;
        const auto [ptr, ec] = std::from_chars(
            text.data() + start,
            text.data() + cursor,
            value);
        if (ec == std::errc{} && ptr == text.data() + cursor)
            result.push_back(value);

        while (cursor < text.size() &&
               std::isspace(static_cast<unsigned char>(text[cursor])))
        {
            ++cursor;
        }
        if (cursor < text.size() && text[cursor] == ',')
            ++cursor;
    }

    return result;
}

void append_u32_le(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

std::optional<std::uint32_t> read_u32_le(std::span<const std::uint8_t> bytes,
                                         std::size_t offset)
{
    if (offset + sizeof(std::uint32_t) > bytes.size())
        return std::nullopt;

    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::vector<std::uint8_t> encode_targeted_relay_payload(
    PeerId peer_id,
    const std::uint8_t* data,
    std::size_t len)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(kRelayPeerHeaderSize + len);
    payload.push_back(kRelaySendToPeerTag);
    append_u32_le(payload, peer_id);
    if (data != nullptr && len != 0)
        payload.insert(payload.end(), data, data + len);
    return payload;
}

std::vector<std::uint8_t> encode_broadcast_relay_payload(
    const std::uint8_t* data,
    std::size_t len)
{
    std::vector<std::uint8_t> payload;
    payload.reserve(1u + len);
    payload.push_back(kRelayBroadcastTag);
    if (data != nullptr && len != 0)
        payload.insert(payload.end(), data, data + len);
    return payload;
}

std::optional<ReceivedMessage> decode_incoming_relay_payload(
    std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < kRelayPeerHeaderSize ||
        bytes[0] != kRelayReceiveFromPeerTag)
    {
        return std::nullopt;
    }

    const auto peer_id = read_u32_le(bytes, 1u);
    if (!peer_id.has_value() || *peer_id == 0)
        return std::nullopt;

    ReceivedMessage message;
    message.peer_id = *peer_id;
    message.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kRelayPeerHeaderSize),
                        bytes.end());
    return message;
}

} // namespace

struct RelayWebSocketTransport::Impl
{
    using WebSocketHandle = detail::WebSocketHandle;

    enum class QueueEntryKind : std::uint8_t {
        Connect,
        TextMessage,
        BinaryMessage,
        Disconnect,
    };

    struct QueueEntry {
        QueueEntryKind kind = QueueEntryKind::BinaryMessage;
        WebSocketHandle socket = 0;
        std::string text;
        std::vector<std::uint8_t> payload;
    };

    Impl(std::string url_in, Options options_in)
        : url(trim_copy(url_in))
        , options(std::move(options_in))
    {
        if (url.empty())
            throw std::invalid_argument("RelayWebSocketTransport URL must not be empty");
    }

    ~Impl()
    {
        connected = false;
        started = false;
        clear_queue();
        dispose_socket();
    }

    void accept_connections()
    {
        if (started)
            return;

        const detail::EmscriptenWebSocketApi& api =
            detail::emscripten_websocket_api();
        if (api.is_supported == nullptr || api.is_supported() == detail::kFalse)
        {
            throw std::runtime_error(
                "RelayWebSocketTransport requires browser WebSocket support");
        }

        clear_queue();
        connected = false;
        reset_peer_state();

        detail::WebSocketCreateAttributes create_attributes{};
        api.init_create_attributes(&create_attributes);
        create_attributes.url = url.c_str();
        create_attributes.protocols =
            options.protocols.empty() ? nullptr : options.protocols.c_str();

        const WebSocketHandle new_socket = api.create(&create_attributes);
        if (new_socket <= 0)
        {
            throw std::runtime_error(std::format(
                "RelayWebSocketTransport failed to open {}: {}",
                url,
                new_socket));
        }

        socket = new_socket;
        try
        {
            const auto set_or_throw =
                [](std::string_view label, detail::EmscriptenResult result) {
                    if (result != detail::kResultSuccess)
                    {
                        throw std::runtime_error(std::format(
                            "RelayWebSocketTransport {} failed: {}",
                            label,
                            result));
                    }
                };

            set_or_throw("setting onopen callback",
                         api.set_onopen(socket, this, &Impl::on_open));
            set_or_throw("setting onmessage callback",
                         api.set_onmessage(socket, this, &Impl::on_message));
            set_or_throw("setting onerror callback",
                         api.set_onerror(socket, this, &Impl::on_error));
            set_or_throw("setting onclose callback",
                         api.set_onclose(socket, this, &Impl::on_close));
        }
        catch (...)
        {
            dispose_socket();
            clear_queue();
            throw;
        }

        started = true;
    }

    void send(PeerId peer_id, const std::uint8_t* data, std::size_t len)
    {
        if (data == nullptr && len != 0)
        {
            throw std::runtime_error(std::format(
                "RelayWebSocketTransport peer {} send buffer is null",
                peer_id));
        }
        if (len > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error(std::format(
                "RelayWebSocketTransport peer {} payload is too large: {}",
                peer_id,
                len));
        }
        if (peer_id == 0 || !connected || socket <= 0 ||
            blocked_peers.contains(peer_id) || !remote_peers.contains(peer_id))
        {
            return;
        }

        const detail::EmscriptenWebSocketApi& api =
            detail::emscripten_websocket_api();
        unsigned short ready_state = 0;
        const detail::EmscriptenResult ready_state_result =
            api.get_ready_state(socket, &ready_state);
        if (ready_state_result != detail::kResultSuccess ||
            ready_state != kWebSocketReadyStateOpen)
        {
            enqueue_disconnect(socket);
            request_close_socket(socket);
            return;
        }

        const std::vector<std::uint8_t> payload =
            encode_targeted_relay_payload(peer_id, data, len);
        std::uint8_t empty_payload = 0;
        void* bytes = payload.empty() ? static_cast<void*>(&empty_payload)
                                      : const_cast<std::uint8_t*>(payload.data());
        const detail::EmscriptenResult send_result = api.send_binary(
            socket, bytes, static_cast<std::uint32_t>(payload.size()));
        if (send_result != detail::kResultSuccess)
        {
            enqueue_disconnect(socket);
            request_close_socket(socket);
        }
    }

    void broadcast(const std::uint8_t* data, std::size_t len)
    {
        if (data == nullptr && len != 0)
        {
            throw std::runtime_error(
                "RelayWebSocketTransport broadcast buffer is null");
        }
        if (len > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error(std::format(
                "RelayWebSocketTransport broadcast payload is too large: {}",
                len));
        }
        if (!connected || socket <= 0 || remote_peers.empty())
            return;

        const detail::EmscriptenWebSocketApi& api =
            detail::emscripten_websocket_api();
        unsigned short ready_state = 0;
        const detail::EmscriptenResult ready_state_result =
            api.get_ready_state(socket, &ready_state);
        if (ready_state_result != detail::kResultSuccess ||
            ready_state != kWebSocketReadyStateOpen)
        {
            enqueue_disconnect(socket);
            request_close_socket(socket);
            return;
        }

        const std::vector<std::uint8_t> payload =
            encode_broadcast_relay_payload(data, len);
        std::uint8_t empty_payload = 0;
        void* bytes = payload.empty() ? static_cast<void*>(&empty_payload)
                                      : const_cast<std::uint8_t*>(payload.data());
        const detail::EmscriptenResult send_result = api.send_binary(
            socket, bytes, static_cast<std::uint32_t>(payload.size()));
        if (send_result != detail::kResultSuccess)
        {
            enqueue_disconnect(socket);
            request_close_socket(socket);
        }
    }

    std::vector<ReceivedMessage> poll()
    {
        std::deque<QueueEntry> queued_entries;
        {
            std::unique_lock<std::mutex> lock(queue_mutex, std::try_to_lock);
            if (!lock.owns_lock() || queue.empty())
                return {};
            queued_entries.swap(queue);
        }

        std::vector<ReceivedMessage> received;
        bool should_dispose_socket = false;
        for (auto& entry : queued_entries)
        {
            if (entry.socket != socket)
                continue;

            switch (entry.kind)
            {
            case QueueEntryKind::Connect:
                connected = true;
                reset_peer_state();
                break;

            case QueueEntryKind::TextMessage:
                if (connected)
                    process_control_message(entry.text);
                break;

            case QueueEntryKind::BinaryMessage:
                if (!connected)
                    break;
                if (auto decoded =
                        decode_incoming_relay_payload(entry.payload))
                {
                    if (!blocked_peers.contains(decoded->peer_id))
                        received.push_back(std::move(*decoded));
                }
                break;

            case QueueEntryKind::Disconnect:
                connected = false;
                started = false;
                reset_peer_state();
                should_dispose_socket = true;
                break;
            }
        }

        if (should_dispose_socket)
            dispose_socket();

        return received;
    }

    void disconnect(PeerId peer_id)
    {
        if (peer_id == 0)
            return;

        remote_peers.erase(peer_id);
        blocked_peers.insert(peer_id);
        if (host_peer_id_.has_value() && *host_peer_id_ == peer_id)
            host_peer_id_.reset();
    }

    std::vector<PeerId> connected_peers() const
    {
        std::vector<PeerId> peers(remote_peers.begin(), remote_peers.end());
        std::sort(peers.begin(), peers.end());
        return peers;
    }

    std::optional<PeerId> local_peer_id() const noexcept
    {
        return local_peer_id_;
    }

    std::optional<PeerId> host_peer_id() const noexcept
    {
        return host_peer_id_;
    }

private:
    static detail::EmscriptenBool on_open(
        int,
        const detail::WebSocketOpenEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return detail::kFalse;

        static_cast<Impl*>(user_data)->handle_connect(websocket_event->socket);
        return detail::kTrue;
    }

    static detail::EmscriptenBool on_message(
        int,
        const detail::WebSocketMessageEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return detail::kFalse;

        static_cast<Impl*>(user_data)->handle_message(websocket_event);
        return detail::kTrue;
    }

    static detail::EmscriptenBool on_error(
        int,
        const detail::WebSocketErrorEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return detail::kFalse;

        static_cast<Impl*>(user_data)->handle_disconnect(websocket_event->socket);
        return detail::kTrue;
    }

    static detail::EmscriptenBool on_close(
        int,
        const detail::WebSocketCloseEvent* websocket_event,
        void* user_data)
    {
        if (websocket_event == nullptr || user_data == nullptr)
            return detail::kFalse;

        static_cast<Impl*>(user_data)->handle_disconnect(websocket_event->socket);
        return detail::kTrue;
    }

    void handle_connect(WebSocketHandle socket_handle)
    {
        if (socket_handle != socket)
            return;

        QueueEntry entry;
        entry.kind = QueueEntryKind::Connect;
        entry.socket = socket_handle;
        enqueue(std::move(entry));
    }

    void handle_message(const detail::WebSocketMessageEvent* websocket_event)
    {
        if (websocket_event == nullptr || websocket_event->socket != socket)
            return;

        QueueEntry entry;
        entry.socket = websocket_event->socket;
        if (websocket_event->numBytes != 0)
        {
            if (websocket_event->data == nullptr)
                return;

            entry.payload.assign(websocket_event->data,
                                 websocket_event->data +
                                     websocket_event->numBytes);
        }

        if (websocket_event->isText == detail::kTrue)
        {
            entry.kind = QueueEntryKind::TextMessage;
            entry.text.assign(entry.payload.begin(), entry.payload.end());
            entry.payload.clear();
        }
        else
        {
            entry.kind = QueueEntryKind::BinaryMessage;
        }

        enqueue(std::move(entry));
    }

    void handle_disconnect(WebSocketHandle socket_handle)
    {
        if (socket_handle != socket)
            return;
        enqueue_disconnect(socket_handle);
    }

    void process_control_message(const std::string& text)
    {
        const std::optional<std::string> type =
            extract_json_string_field(text, "type");
        if (!type.has_value())
            return;

        if (*type == "joined")
        {
            local_peer_id_ = extract_json_u32_field(text, "peer_id");
            if (const auto host = extract_json_u32_field(text, "host");
                host.has_value() && *host != 0)
            {
                host_peer_id_ = *host;
            }
            return;
        }

        if (*type == "peer_list")
        {
            std::unordered_set<PeerId> next_peers;
            for (const PeerId peer_id : extract_json_u32_array_field(text, "peers"))
            {
                if (peer_id == 0 || blocked_peers.contains(peer_id))
                    continue;
                if (local_peer_id_.has_value() && *local_peer_id_ == peer_id)
                    continue;
                next_peers.insert(peer_id);
            }
            remote_peers = std::move(next_peers);
            if (const auto host = extract_json_u32_field(text, "host");
                host.has_value() && *host != 0)
            {
                host_peer_id_ = *host;
                if ((!local_peer_id_.has_value() || *local_peer_id_ != *host) &&
                    !blocked_peers.contains(*host))
                {
                    remote_peers.insert(*host);
                }
            }
            return;
        }

        if (*type == "peer_joined")
        {
            const auto peer_id = extract_json_u32_field(text, "peer_id");
            if (!peer_id.has_value() || *peer_id == 0 ||
                blocked_peers.contains(*peer_id))
            {
                return;
            }

            if (!local_peer_id_.has_value() || *local_peer_id_ != *peer_id)
                remote_peers.insert(*peer_id);

            if (const auto is_host =
                    extract_json_bool_field(text, "is_host");
                is_host.value_or(false))
            {
                host_peer_id_ = *peer_id;
            }
            return;
        }

        if (*type == "peer_left")
        {
            const auto peer_id = extract_json_u32_field(text, "peer_id");
            if (!peer_id.has_value())
                return;
            remote_peers.erase(*peer_id);
            if (host_peer_id_.has_value() && *host_peer_id_ == *peer_id)
                host_peer_id_.reset();
            return;
        }

        if (*type == "host_changed")
        {
            const auto peer_id = extract_json_u32_field(text, "new_host");
            if (!peer_id.has_value() || *peer_id == 0)
                return;
            host_peer_id_ = *peer_id;
            if ((!local_peer_id_.has_value() || *local_peer_id_ != *peer_id) &&
                !blocked_peers.contains(*peer_id))
            {
                remote_peers.insert(*peer_id);
            }
        }
    }

    void reset_peer_state()
    {
        local_peer_id_.reset();
        host_peer_id_.reset();
        remote_peers.clear();
        blocked_peers.clear();
    }

    void enqueue_disconnect(WebSocketHandle socket_handle)
    {
        QueueEntry entry;
        entry.kind = QueueEntryKind::Disconnect;
        entry.socket = socket_handle;
        enqueue(std::move(entry));
    }

    void request_close_socket(WebSocketHandle socket_handle) const noexcept
    {
        if (socket_handle <= 0)
            return;

        const detail::EmscriptenWebSocketApi& api =
            detail::emscripten_websocket_api();
        if (api.close != nullptr)
            (void)api.close(socket_handle, 1000, nullptr);
    }

    void dispose_socket() noexcept
    {
        if (socket <= 0)
            return;

        const WebSocketHandle retiring_socket = socket;
        socket = 0;
        request_close_socket(retiring_socket);

        const detail::EmscriptenWebSocketApi& api =
            detail::emscripten_websocket_api();
        if (api.destroy != nullptr)
            (void)api.destroy(retiring_socket);
    }

    void clear_queue()
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        queue.clear();
    }

    void enqueue(QueueEntry entry)
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        queue.push_back(std::move(entry));
    }

    std::string url;
    Options options;
    WebSocketHandle socket = 0;
    bool started = false;
    bool connected = false;

    std::mutex queue_mutex;
    std::deque<QueueEntry> queue;

    std::optional<PeerId> local_peer_id_;
    std::optional<PeerId> host_peer_id_;
    std::unordered_set<PeerId> remote_peers;
    std::unordered_set<PeerId> blocked_peers;
};

RelayWebSocketTransport::RelayWebSocketTransport(std::string url)
    : RelayWebSocketTransport(std::move(url), Options{})
{
}

RelayWebSocketTransport::RelayWebSocketTransport(std::string url,
                                                 Options options)
    : impl_(std::make_unique<Impl>(std::move(url), std::move(options)))
{
}

RelayWebSocketTransport::~RelayWebSocketTransport() = default;

void RelayWebSocketTransport::send(PeerId peer_id,
                                   const std::uint8_t* data,
                                   std::size_t len)
{
    impl_->send(peer_id, data, len);
}

void RelayWebSocketTransport::broadcast(const std::uint8_t* data,
                                        std::size_t len)
{
    impl_->broadcast(data, len);
}

std::vector<ReceivedMessage> RelayWebSocketTransport::poll()
{
    return impl_->poll();
}

void RelayWebSocketTransport::accept_connections()
{
    impl_->accept_connections();
}

void RelayWebSocketTransport::disconnect(PeerId peer_id)
{
    impl_->disconnect(peer_id);
}

std::vector<PeerId> RelayWebSocketTransport::connected_peers() const
{
    return impl_->connected_peers();
}

std::optional<PeerId> RelayWebSocketTransport::local_peer_id() const noexcept
{
    return impl_->local_peer_id();
}

std::optional<PeerId> RelayWebSocketTransport::host_peer_id() const noexcept
{
    return impl_->host_peer_id();
}

} // namespace og::sim
