// Per-level weather roll (core/weather.h + GameWorld::roll_weather):
// deterministic distribution pins, residue boundary mapping, the process-wide
// nonce, and the world-side accessors. Pure headless logic — the roll never
// touches the sim RNG stream and nothing in the sim reads the result.
#include <gtest/gtest.h>

#include <openglad/core/weather.h>
#include <openglad/gameplay/game_world.h>

#include <cstdint>

namespace {

// Every test must leave the process-wide nonce at its deterministic default
// (0) so the binary stays order-independent under --gtest_shuffle.
class WeatherNonceGuard
{
public:
    WeatherNonceGuard()
        : saved_(og::weather_roll_nonce()),
          saved_seq_(og::weather_roll_sequence())
    {
    }
    ~WeatherNonceGuard()
    {
        og::set_weather_roll_nonce(saved_);
        og::set_weather_roll_sequence(saved_seq_);
    }
    WeatherNonceGuard(const WeatherNonceGuard&) = delete;
    WeatherNonceGuard& operator=(const WeatherNonceGuard&) = delete;

private:
    std::uint32_t saved_;
    std::uint32_t saved_seq_;
};

} // namespace

// The residue table is the locked design: 0-4 None (50%), 5-7 Clouds (30%),
// 8-9 Rain (20%).
TEST(Weather, residue_boundaries_map_to_kinds)
{
    static_assert(og::weather_kind_from_residue(0u) == WeatherKind::None);
    static_assert(og::weather_kind_from_residue(4u) == WeatherKind::None);
    static_assert(og::weather_kind_from_residue(5u) == WeatherKind::Clouds);
    static_assert(og::weather_kind_from_residue(7u) == WeatherKind::Clouds);
    static_assert(og::weather_kind_from_residue(8u) == WeatherKind::Rain);
    static_assert(og::weather_kind_from_residue(9u) == WeatherKind::Rain);

    for (std::uint32_t r = 0; r < 10u; ++r)
    {
        const WeatherKind kind = og::weather_kind_from_residue(r);
        if (r < 5u)
            EXPECT_EQ(WeatherKind::None, kind) << "residue " << r;
        else if (r < 8u)
            EXPECT_EQ(WeatherKind::Clouds, kind) << "residue " << r;
        else
            EXPECT_EQ(WeatherKind::Rain, kind) << "residue " << r;
    }
}

// The roll is a pure function of the seed: pin the EXACT distribution over
// seeds 0..999 (deterministic forever — a hash or table change must show up
// here) and check it lands near the designed 50/30/20 split.
TEST(Weather, roll_distribution_over_first_thousand_seeds_is_pinned)
{
    int none = 0, clouds = 0, rain = 0;
    for (std::uint32_t seed = 0; seed < 1000u; ++seed)
    {
        switch (og::roll_weather_kind(seed))
        {
        case WeatherKind::None:
            ++none;
            break;
        case WeatherKind::Clouds:
            ++clouds;
            break;
        case WeatherKind::Rain:
            ++rain;
            break;
        }
    }
    EXPECT_EQ(477, none);
    EXPECT_EQ(308, clouds);
    EXPECT_EQ(215, rain);
    EXPECT_EQ(1000, none + clouds + rain);
}

TEST(Weather, roll_is_deterministic_per_seed)
{
    for (std::uint32_t seed : {0u, 1u, 2u, 7u, 500u, 0xFFFFFFFFu})
        EXPECT_EQ(og::roll_weather_kind(seed), og::roll_weather_kind(seed))
            << "seed " << seed;
}

TEST(Weather, nonce_defaults_to_zero_and_round_trips)
{
    WeatherNonceGuard guard;
    EXPECT_EQ(0u, og::weather_roll_nonce())
        << "test builds must start from the deterministic default";
    og::set_weather_roll_nonce(0xDEADBEEFu);
    EXPECT_EQ(0xDEADBEEFu, og::weather_roll_nonce());
    og::set_weather_roll_nonce(0u);
    EXPECT_EQ(0u, og::weather_roll_nonce());
}

TEST(Weather, world_defaults_to_none_and_accessors_round_trip)
{
    GameWorld world;
    EXPECT_EQ(WeatherKind::None, world.weather());
    world.set_weather(WeatherKind::Rain);
    EXPECT_EQ(WeatherKind::Rain, world.weather());
    world.set_weather(WeatherKind::Clouds);
    EXPECT_EQ(WeatherKind::Clouds, world.weather());
    world.set_weather(WeatherKind::None);
    EXPECT_EQ(WeatherKind::None, world.weather());
}

// Under the default-0 nonce the roll depends on the level id alone: the same
// id reproduces the same kind across worlds and runs. Ids 1/2/7 pin one level
// of each kind (deterministic hash — see the distribution pin above).
TEST(Weather, world_roll_is_deterministic_from_level_id_under_default_nonce)
{
    WeatherNonceGuard guard;
    og::set_weather_roll_nonce(0u);

    GameWorld world;
    world.id = 1;
    og::set_weather_roll_sequence(0u);
    world.roll_weather();
    EXPECT_EQ(WeatherKind::None, world.weather());
    world.id = 2;
    og::set_weather_roll_sequence(0u);
    world.roll_weather();
    EXPECT_EQ(WeatherKind::Clouds, world.weather());
    world.id = 7;
    og::set_weather_roll_sequence(0u);
    world.roll_weather();
    EXPECT_EQ(WeatherKind::Rain, world.weather());

    GameWorld other;
    other.id = 7;
    og::set_weather_roll_sequence(0u);
    other.roll_weather();
    EXPECT_EQ(world.weather(), other.weather())
        << "the same (id, nonce, sequence) must reproduce across worlds";
}

// The nonce re-seasons the roll (level id 2 stops being Clouds under a nonce
// that lands a different residue) without consuming any RNG stream.
TEST(Weather, world_roll_mixes_in_the_process_nonce)
{
    WeatherNonceGuard guard;
    og::set_weather_roll_sequence(0u);
    GameWorld world;
    world.id = 2;

    og::set_weather_roll_nonce(0u);
    og::set_weather_roll_sequence(0u);
    world.roll_weather();
    ASSERT_EQ(WeatherKind::Clouds, world.weather());

    // seed = 2 ^ 5 = 7 -> the pinned Rain level id from the test above.
    og::set_weather_roll_nonce(5u);
    og::set_weather_roll_sequence(0u);
    world.roll_weather();
    EXPECT_EQ(WeatherKind::Rain, world.weather());
}

// The roll never touches the world's sim RNG: parity invariant.
// Within one session the roll SEQUENCE re-seasons every load: retrying a
// level walks fresh outcomes instead of repeating the launch's single roll
// (the bug: a dry level 1 stayed dry across every retry). Exact kinds are
// pinned for level id 1 under the default-0 nonce.
TEST(Weather, world_reroll_walks_fresh_outcomes_within_a_session)
{
    WeatherNonceGuard guard;
    og::set_weather_roll_nonce(0u);
    og::set_weather_roll_sequence(0u);

    GameWorld world;
    world.id = 1;
    world.roll_weather();
    EXPECT_EQ(WeatherKind::None, world.weather()); // seq 0
    world.roll_weather();
    EXPECT_EQ(WeatherKind::Clouds, world.weather()); // seq 1
    world.roll_weather();
    EXPECT_EQ(WeatherKind::None, world.weather()); // seq 2
    world.roll_weather();
    EXPECT_EQ(WeatherKind::Rain, world.weather()); // seq 3
    EXPECT_EQ(4u, og::weather_roll_sequence());

    // Resetting the sequence replays the same walk.
    og::set_weather_roll_sequence(0u);
    world.roll_weather();
    EXPECT_EQ(WeatherKind::None, world.weather());
}

TEST(Weather, world_roll_leaves_sim_rng_state_untouched)
{
    WeatherNonceGuard guard;
    GameWorld world;
    world.id = 7;
    const std::uint32_t rng_before = world.rng_.state_;
    world.roll_weather();
    EXPECT_EQ(rng_before, world.rng_.state_);
}
