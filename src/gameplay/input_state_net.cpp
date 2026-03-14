#include <openglad/gameplay/input_state_net.h>

#include <openglad/gameplay/input_action.h>

namespace og::sim {
namespace {

constexpr std::uint8_t kQuitRequestedMask = 0x01;
constexpr int kVersionShift = 1;

static_assert(MAX_PLAYERS == 4);
static_assert(NUM_INPUT_KEYS == 16);
static_assert(kInputActionCount == NUM_INPUT_KEYS);
static_assert(kInputStateProtocolVersion < (1u << (8 - kVersionShift)));

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

void write_u32_le(std::array<std::uint8_t, kSerializedInputStateSize>& bytes,
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

} // namespace

std::array<std::uint8_t, kSerializedInputStateSize>
serialize_input(const InputState& input)
{
    std::array<std::uint8_t, kSerializedInputStateSize> bytes{};
    bytes[0] = static_cast<std::uint8_t>(kInputStateProtocolVersion << kVersionShift);
    if (input.quit_requested)
    {
        bytes[0] |= kQuitRequestedMask;
    }

    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        const std::size_t offset = 1 + (static_cast<std::size_t>(player) * 4);
        write_u32_le(bytes, offset, pack_player_bits(input.players[player]));
    }

    return bytes;
}

std::optional<InputState> deserialize_input(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != kSerializedInputStateSize)
    {
        return std::nullopt;
    }

    const std::uint8_t header = bytes[0];
    if ((header >> kVersionShift) != kInputStateProtocolVersion)
    {
        return std::nullopt;
    }

    InputState input{};
    input.quit_requested = (header & kQuitRequestedMask) != 0;

    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        const std::size_t offset = 1 + (static_cast<std::size_t>(player) * 4);
        input.players[player] = unpack_player_bits(read_u32_le(bytes, offset));
    }

    return input;
}

} // namespace og::sim
