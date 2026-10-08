/* The New Specials sprites (scripts/generate_special_art.py ->
 * pix/{mine,banner,bonewall,ember}.png + .json): asset conformance.
 *
 * Every file must load through read_pixie_file, the loader the game uses
 * (indexed 8-bit, a 256-entry palette within +-1 of our.pal, frames stacked
 * by the sidecar, every dimension <= 255). Palette budgets pin the cycled-band
 * and team-band rules: nothing touches the WATER band 208-223, only the ember's
 * flame may use the cycled fire band 224-231 (do_cycle IS the flicker), and
 * only the mine's light and the banner's cloth use the team band 248-255
 * (walkputbuffer repaints it in the owner's team colour). Each sprite is cut
 * from art the game already ships; the reuse rows below compare the shared
 * pixels with their source, so a regenerated sprite that drifts away from the
 * game's own look fails here. The pixel hashes pin the committed bytes the
 * generator writes (rerun it with --check to compare without writing).
 */
#include <openglad/resources/og_file.h>
#include <openglad/gameplay/pixie_data.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

namespace {

struct ArtRow
{
    const char* file;
    int w;
    int h;      // one frame
    int frames;
    bool fire;  // may use the cycled fire band 224-231
    bool team;  // may use the team band 248-255
    std::uint32_t pixel_hash; // FNV-1a over every frame's pixels
};

// Pinned from the committed art (python3 scripts/generate_special_art.py).
constexpr ArtRow kArt[] = {
    {"mine.png", 9, 9, 2, false, true, 0xcc1f1262u},
    {"banner.png", 12, 22, 4, false, true, 0x8cae22d3u},
    {"bonewall.png", 16, 16, 2, false, false, 0x1db153b3u},
    {"ember.png", 7, 7, 2, true, false, 0x79b6d946u},
};

std::uint32_t fnv1a(const unsigned char* p, std::size_t n)
{
    std::uint32_t h = 0x811c9dc5u;
    for (std::size_t i = 0; i < n; ++i)
    {
        h ^= p[i];
        h *= 0x01000193u;
    }
    return h;
}

int at(const PixieData& p, int frame, int x, int y)
{
    return p.data[static_cast<std::size_t>((frame * p.h + y) * p.w + x)];
}

bool in(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

} // namespace

TEST(SpecialArt, every_sprite_loads_with_its_frames_and_size)
{
    for (const ArtRow& row : kArt)
    {
        const PixieData p = read_pixie_file(row.file);
        ASSERT_TRUE(p.valid()) << "pix/" << row.file
                               << " must parse through read_pixie_file";
        EXPECT_EQ(row.w, static_cast<int>(p.w)) << row.file;
        EXPECT_EQ(row.h, static_cast<int>(p.h)) << row.file << " frame height";
        EXPECT_EQ(row.frames, static_cast<int>(p.frames))
            << row.file << ": the sidecar's frame count";
        const std::size_t n = static_cast<std::size_t>(p.w) * p.h * p.frames;
        EXPECT_EQ(row.pixel_hash, fnv1a(p.data.get(), n))
            << row.file << ": the committed pixels moved; rerun "
               "scripts/generate_special_art.py and repin";
    }
}

TEST(SpecialArt, palette_budgets_pin_the_cycled_and_team_bands)
{
    for (const ArtRow& row : kArt)
    {
        const PixieData p = read_pixie_file(row.file);
        ASSERT_TRUE(p.valid()) << row.file;
        std::set<std::string> frames_seen;
        int fire = 0;
        int team = 0;
        for (int f = 0; f < p.frames; ++f)
        {
            int transparent = 0;
            int foreground = 0;
            std::string bytes;
            for (int y = 0; y < p.h; ++y)
                for (int x = 0; x < p.w; ++x)
                {
                    const int v = at(p, f, x, y);
                    bytes.push_back(static_cast<char>(v));
                    if (v == 0)
                    {
                        ++transparent;
                        continue;
                    }
                    ++foreground;
                    EXPECT_FALSE(in(v, 208, 223))
                        << row.file << " frame " << f
                        << ": the WATER band flashes (index " << v << ")";
                    if (in(v, 224, 231))
                        ++fire;
                    if (v >= 248)
                        ++team;
                }
            EXPECT_GE(transparent, 1)
                << row.file << " frame " << f << " must be a cut-out";
            EXPECT_GE(foreground, 1) << row.file << " frame " << f << " is empty";
            EXPECT_TRUE(frames_seen.insert(bytes).second)
                << row.file << " frame " << f << " repeats an earlier frame";
        }
        if (row.fire)
            EXPECT_GE(fire, 1) << row.file << ": the flame must flicker";
        else
            EXPECT_EQ(0, fire)
                << row.file << ": only the ember may use the fire band";
        if (row.team)
            EXPECT_GE(team, 1) << row.file << ": must carry the team colour";
        else
            EXPECT_EQ(0, team)
                << row.file << ": only the mine and the banner are team-coloured";
    }
}

// The mine is the dark bomb (bomb1.png frame 10, body at cols 3..9, rows
// 4..10) set one pixel in: every body pixel outside the middle 3x3 is the
// bomb's own, except the bomb's fuse ember, which is painted out.
TEST(SpecialArt, mine_is_the_dark_bomb_with_a_team_light)
{
    const PixieData mine = read_pixie_file("mine.png");
    const PixieData bomb = read_pixie_file("bomb1.png");
    ASSERT_TRUE(mine.valid() && bomb.valid());
    ASSERT_EQ(13, static_cast<int>(bomb.w));
    int shared = 0;
    for (int f = 0; f < mine.frames; ++f)
        for (int y = 0; y < 7; ++y)
            for (int x = 0; x < 7; ++x)
            {
                const int b = at(bomb, 10, x + 3, y + 4);
                const int m = at(mine, f, x + 1, y + 1);
                if (b == 0 || in(b, 224, 231))
                    continue;
                if (in(x + 1, 3, 5) && in(y + 1, 3, 5))
                    continue; // the light
                EXPECT_EQ(b, m) << "mine frame " << f << " at " << x + 1
                                << "," << y + 1;
                ++shared;
            }
    EXPECT_EQ(2 * 28, shared) << "the bomb's body outside the light carries over";
    // The light blinks: team band in the middle, brighter on frame 1.
    EXPECT_GE(at(mine, 0, 4, 4), 248);
    EXPECT_GE(at(mine, 1, 4, 4), 248);
    EXPECT_GT(at(mine, 0, 4, 4), at(mine, 1, 4, 4));
}

// The banner's finial is the skeleton's skull (skeleton.png frame 0, cols
// 5..9, rows 0..4), on every frame.
TEST(SpecialArt, banner_wears_the_skeletons_skull)
{
    const PixieData banner = read_pixie_file("banner.png");
    const PixieData skel = read_pixie_file("skeleton.png");
    ASSERT_TRUE(banner.valid() && skel.valid());
    for (int f = 0; f < banner.frames; ++f)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                EXPECT_EQ(at(skel, 0, x + 5, y), at(banner, f, x, y))
                    << "banner frame " << f << " skull pixel " << x << ","
                    << y;
}

// The bone wall's posts are the thrown bone's upright frame (bone1.png
// frame 0, cols 2..6) at x=1, 6 and 11: the bottom bone of each post is a
// verbatim copy. Its bars are the sideways frame (bone1.png frame 2, rows
// 2..5) lashed post to post from x=2 and x=7, upper and lower: wherever a
// bar's middle shows between two shafts, it is the bone's own pixel.
TEST(SpecialArt, bone_wall_is_built_from_the_thrown_bone)
{
    const PixieData wall = read_pixie_file("bonewall.png");
    const PixieData bone = read_pixie_file("bone1.png");
    ASSERT_TRUE(wall.valid() && bone.valid());
    constexpr int kPosts[] = {1, 6, 11};
    int post_pixels = 0;
    for (int f = 0; f < wall.frames; ++f)
        for (const int post_x : kPosts)
            for (int y = 0; y < 7; ++y)
                for (int x = 0; x < 5; ++x)
                {
                    const int b = at(bone, 0, x + 2, y);
                    if (b == 0)
                        continue;
                    const int w = at(wall, f, post_x + x, y + 7);
                    // The cracked frame darkens a few pixels to grey 17.
                    if (f == 1 && w == 17)
                        continue;
                    EXPECT_EQ(b, w) << "wall frame " << f << " post " << post_x
                                    << " at " << x << "," << y;
                    ++post_pixels;
                }
    // 18 opaque pixels in the upright bone, 3 posts, 2 frames, less the
    // three the cracked frame darkens on the left post.
    EXPECT_EQ(2 * 3 * 18 - 3, post_pixels);

    // The bars on the whole wall. A column under a post's shaft (post_x + 1,
    // post_x + 2) belongs to the post, so only the open span is compared.
    auto under_a_shaft = [&](int x) {
        for (const int post_x : kPosts)
            if (x == post_x + 1 || x == post_x + 2)
                return true;
        return false;
    };
    int bar_pixels = 0;
    for (const int bar_x : {2, 7})
        for (const int bar_y : {2, 8})
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 7; ++x)
                {
                    const int b = at(bone, 2, x, y + 2);
                    if (b == 0 || under_a_shaft(bar_x + x))
                        continue;
                    EXPECT_EQ(b, at(wall, 0, bar_x + x, bar_y + y))
                        << "bar at " << bar_x << "," << bar_y << " pixel "
                        << x << "," << y;
                    ++bar_pixels;
                }
    // Each bar shows three columns of its two-row middle between the posts.
    EXPECT_EQ(4 * 3 * 2, bar_pixels);
}

// Like every obstacle the game ships (tree, door, boulder, tent, tower,
// bone pile), the bone wall paints no ground shadow of its own: the engine
// draws that. And the wall stands inside its outer posts, centred in the
// frame, so no bar end sticks out past a post into thin air.
TEST(SpecialArt, bone_wall_has_no_painted_shadow_and_no_loose_bar_end)
{
    const PixieData wall = read_pixie_file("bonewall.png");
    ASSERT_TRUE(wall.valid());
    ASSERT_EQ(16, static_cast<int>(wall.w));
    for (int f = 0; f < wall.frames; ++f)
        for (int y = wall.h - 2; y < wall.h; ++y)
            for (int x = 0; x < wall.w; ++x)
            {
                const int v = at(wall, f, x, y);
                EXPECT_FALSE(v == 16 || v == 17)
                    << "wall frame " << f << " paints a shadow at " << x << ","
                    << y;
            }
    int left = wall.w;
    int right = -1;
    for (int y = 0; y < wall.h; ++y)
        for (int x = 0; x < wall.w; ++x)
            if (at(wall, 0, x, y) != 0)
            {
                left = std::min(left, x);
                right = std::max(right, x);
            }
    // The outer posts' top knobs are 4 wide, at x=1 and x=11.
    EXPECT_EQ(1, left) << "the wall starts at the left post";
    EXPECT_EQ(14, right) << "the wall ends at the right post";
}

// The ember's flame is the meteor bolt's (meteor.png frame 0, cols 2..5,
// rows 1..6), standing on the charred spot.
TEST(SpecialArt, ember_burns_with_the_meteors_flame)
{
    const PixieData ember = read_pixie_file("ember.png");
    const PixieData meteor = read_pixie_file("meteor.png");
    ASSERT_TRUE(ember.valid() && meteor.valid());
    int shared = 0;
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 4; ++x)
        {
            const int m = at(meteor, 0, x + 2, y + 1);
            if (m == 0)
                continue;
            EXPECT_EQ(m, at(ember, 0, x + 1, y)) << "ember flame " << x << ","
                                                 << y;
            ++shared;
        }
    EXPECT_GE(shared, 15);
}
