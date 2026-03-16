#include <openglad/gameplay/input_state_net.h>

#include <openglad/gameplay/input_action.h>

namespace og::sim {
namespace {

constexpr std::uint8_t kQuitRequestedTrue = 0x01;
constexpr std::uint8_t kQuitRequestedMask = 0x01;
constexpr std::uint8_t kTimerWaitShift = 1;
constexpr std::uint8_t kTimerWaitMask = 0x3e;
constexpr std::size_t kInputTickOffset = kTransportHeaderSize;
constexpr std::size_t kInputMetadataOffset =
    kInputTickOffset + sizeof(std::uint32_t);
constexpr std::size_t kPlayerBitsOffset = kInputMetadataOffset + 1;

static_assert(MAX_PLAYERS == 4);
static_assert(NUM_INPUT_KEYS == 16);
static_assert(kInputActionCount == NUM_INPUT_KEYS);

std::uint32_t pack_player_bits(const PlayerInput& input)
{
    std::uint32_t bits = 0;
    for (int key = 0; key < NUM_INPUT_KEYS; ++key)
    {
        if (input.held[key])
        {
            bits |= (1u << key);
        }
        if (input.pressed[key])
        {
            bits |= (1u << (NUM_INPUT_KEYS + key));
        }
    }
    return bits;
}

PlayerInput unpack_player_bits(std::uint32_t bits)
{
    PlayerInput input{};
    for (int key = 0; key < NUM_INPUT_KEYS; ++key)
    {
        input.held[key] = (bits & (1u << key)) != 0;
        input.pressed[key] = (bits & (1u << (NUM_INPUT_KEYS + key))) != 0;
    }
    return input;
}

void write_u32_le(std::array<std::uint8_t, kSerializedInputMessageSize>& bytes,
                  std::size_t offset,
                  std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

std::uint32_t read_u32_le(std::span<const std::uint8_t> bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::uint8_t encode_input_metadata(const InputState& input)
{
    std::uint8_t metadata = input.quit_requested ? kQuitRequestedTrue : 0U;
    if (input.timer_wait_request >= 0 && input.timer_wait_request <= 20)
    {
        const std::uint8_t encoded_timer_wait = static_cast<std::uint8_t>(
            input.timer_wait_request + 1);
        metadata |= static_cast<std::uint8_t>(
            (encoded_timer_wait << kTimerWaitShift) & kTimerWaitMask);
    }
    return metadata;
}

std::int8_t decode_timer_wait_request(std::uint8_t metadata)
{
    const std::uint8_t encoded_timer_wait =
        static_cast<std::uint8_t>((metadata & kTimerWaitMask) >> kTimerWaitShift);
    return encoded_timer_wait == 0
        ? kNoTimerWaitRequest
        : static_cast<std::int8_t>(encoded_timer_wait - 1);
}

} // namespace

std::array<std::uint8_t, kSerializedInputMessageSize>
serialize_input(const InputState& input)
{
    return serialize_input(0, input);
}

std::array<std::uint8_t, kSerializedInputMessageSize>
serialize_input(std::uint32_t tick, const InputState& input)
{
    std::array<std::uint8_t, kSerializedInputMessageSize> bytes{};
    write_transport_header(bytes, kInputMessageType,
                           static_cast<std::uint16_t>(kSerializedInputPayloadSize));
    write_u32_le(bytes, kInputTickOffset, tick);
    bytes[kInputMetadataOffset] = encode_input_metadata(input);

    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        const std::size_t offset =
            kPlayerBitsOffset + (static_cast<std::size_t>(player) * 4);
        write_u32_le(bytes, offset, pack_player_bits(input.players[player]));
    }

    return bytes;
}

std::optional<InputStateMessage> deserialize_input_message(
    std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != kSerializedInputMessageSize)
    {
        return std::nullopt;
    }

    TransportEnvelope envelope;
    if (!decode_transport_envelope(bytes, envelope) ||
        envelope.message_type != kInputMessageType ||
        envelope.payload_length != kSerializedInputPayloadSize)
    {
        return std::nullopt;
    }

    InputStateMessage message;
    message.tick = read_u32_le(bytes, kInputTickOffset);
    message.input.quit_requested =
        (bytes[kInputMetadataOffset] & kQuitRequestedMask) != 0;
    message.input.timer_wait_request =
        decode_timer_wait_request(bytes[kInputMetadataOffset]);

    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        const std::size_t offset =
            kPlayerBitsOffset + (static_cast<std::size_t>(player) * 4);
        message.input.players[player] =
            unpack_player_bits(read_u32_le(bytes, offset));
    }

    return message;
}

std::optional<InputState> deserialize_input(std::span<const std::uint8_t> bytes)
{
    const std::optional<InputStateMessage> message =
        deserialize_input_message(bytes);
    if (!message.has_value())
        return std::nullopt;
    return message->input;
}

} // namespace og::sim
