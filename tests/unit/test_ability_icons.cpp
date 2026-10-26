// The ability icons (docs/ability-select-design.md, "Icons";
// scripts/ability_icons/README.md).
//
// Three things are pinned here:
//   1. every ability a core family can put on a belt names an icon file that
//      exists under pix/icons/ and loads as one 16x16 picture. The walk is
//      over the DESCRIPTOR (special_names / alternate_names, through
//      og::sim::specials_view under both New Specials settings), not over
//      belt::catalog: the catalog drops an alternate whose name repeats, so
//      the cleric's second TURN UNDEAD (slot 3's alternate) would never be
//      checked, and the server still casts (3, alt) from a saved byte. Until
//      the art batches land, this test lists the files still missing;
//   2. every committed icon PNG keeps the palette budget and the structural
//      gates scripts/ability_icons/generate.py enforces on its grid, read
//      back through the game's own loader;
//   3. og::ui::AbilityIcons: a hit, a shared path loaded once, the generic
//      tile for a missing key and for a bad path, nullptr when even the
//      generic tile is missing, a reload when the sprite sources change, and
//      the locked LUT.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/ability_icons.h>
#include <openglad/resources/filesystem.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/our_palette.h>
#include <openglad/resources/packs.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using og::sim::belt::Entry;
using og::ui::AbilityIcon;
using og::ui::AbilityIcons;

constexpr int kIcon = og::ui::kAbilityIconSize;
constexpr int kPixels = kIcon * kIcon;
constexpr std::size_t kPixelCount = static_cast<std::size_t>(kPixels);

bool named(const char* name)
{
    return name != nullptr && std::strcmp(name, kSpecialNameNone) != 0;
}

bool core_family(const FamilyDescriptor* fd)
{
    return fd != nullptr && fd->declared_id != nullptr &&
           std::strncmp(fd->declared_id, "core:", 5) == 0;
}

// "" when `path` loads through read_pixie_file as one 16x16 picture, else why
// not.
std::string load_problem(const char* path)
{
    const PixieData p = read_pixie_file(path);
    if (!p.valid())
        return "does not load";
    if (p.w != kIcon || p.h != kIcon)
        return "is " + std::to_string(p.w) + "x" + std::to_string(p.h) +
               ", not 16x16";
    return {};
}

std::string joined(const std::set<std::string>& items)
{
    std::string out;
    for (const std::string& s : items)
        out += "\n  " + s;
    return out;
}

// The structural gates of scripts/ability_icons/generate.py, on a loaded
// picture. `edge_ok` is the grid's `# edge: ok` waiver.
std::vector<std::string> gate_problems(const unsigned char* px, bool edge_ok)
{
    std::vector<std::string> out;
    int opaque = 0;
    int border = 0;
    bool top = false, bottom = false, left = false, right = false;
    std::set<int> colours;
    const auto& lut = og::ui::locked_icon_lut();
    int interior = 0;
    int far = 0;
    const double fill = og::ui::icon_palette_luma(18);
    for (int y = 0; y < kIcon; ++y) {
        for (int x = 0; x < kIcon; ++x) {
            const int c = px[y * kIcon + x];
            if (c == 0)
                continue;
            ++opaque;
            colours.insert(c);
            const bool edge_x = x == 0 || x == kIcon - 1;
            const bool edge_y = y == 0 || y == kIcon - 1;
            if (edge_x || edge_y)
                ++border;
            top = top || y == 0;
            bottom = bottom || y == kIcon - 1;
            left = left || x == 0;
            right = right || x == kIcon - 1;
            if ((c >= 1 && c <= 15) || (c >= 168 && c <= 191) ||
                (c >= 208 && c <= 231) || c >= 248)
                out.push_back("forbidden index " + std::to_string(c));
            const int g = lut[static_cast<std::size_t>(c)];
            if (g != 16) {
                ++interior;
                if (std::abs(og::ui::icon_palette_luma(g) - fill) >= 30.0)
                    ++far;
            }
        }
    }
    if (opaque > 153)
        out.push_back("opaque " + std::to_string(opaque) + " > 153");
    if (border > 24)
        out.push_back("border " + std::to_string(border) + " > 24");
    if (top && bottom && left && right && !edge_ok)
        out.push_back("touches all four edges");
    if (colours.size() > 16)
        out.push_back(std::to_string(colours.size()) + " colours > 16");
    if (interior > 0 && far * 2 < interior)
        out.push_back("locked contrast " + std::to_string(far) + "/" +
                      std::to_string(interior));
    return out;
}

bool grid_waives_edges(const fs::path& grid)
{
    std::ifstream in(grid);
    std::string line;
    while (std::getline(in, line))
        if (line == "# edge: ok")
            return true;
    return false;
}

// Mounts docs/modding/examples/emberwisp at packs/emberwisp/ (the same way
// test_campaign_packs.cpp does) and puts the registries back on the way out.
class EmberwispMount
{
public:
    EmberwispMount()
        : mounted_(og::resources::mount("docs/modding/examples/emberwisp",
                                        "packs/emberwisp/", 1))
    {
        og::resources::refresh_pack_scripts();
    }
    ~EmberwispMount()
    {
        if (mounted_)
            (void)og::resources::unmount("docs/modding/examples/emberwisp");
        og::resources::refresh_pack_scripts();
    }
    EmberwispMount(const EmberwispMount&) = delete;
    EmberwispMount& operator=(const EmberwispMount&) = delete;
    [[nodiscard]] bool ok() const { return mounted_; }

private:
    bool mounted_;
};

} // namespace

// --- 1. every core ability has its picture ------------------------------------

TEST(AbilityIconFiles, every_core_ability_names_an_icon_that_loads_16x16)
{
    std::set<std::string> undeclared;
    std::set<std::string> missing;
    int abilities = 0;
    for (int id = 0; id < NUM_FAMILY_SLOTS; ++id) {
        const FamilyDescriptor* fd = get_family_descriptor(id);
        if (!core_family(fd))
            continue;
        for (const short new_specials : {short{0}, short{1}}) {
            for (int slot = 1; slot < FD_NUM_SPECIALS; ++slot) {
                for (const bool alt : {false, true}) {
                    const char* name =
                        alt ? og::sim::alternate_name(fd, slot, new_specials)
                            : og::sim::special_name(fd, slot, new_specials);
                    if (!named(name))
                        continue;
                    ++abilities;
                    const char* path = og::ui::declared_icon_path(
                        *fd, Entry{static_cast<std::uint8_t>(slot), alt});
                    if (path == nullptr) {
                        undeclared.insert(std::string(fd->declared_id) + " " +
                                          name + " declares no icon");
                        continue;
                    }
                    const std::string why = load_problem(path);
                    if (!why.empty())
                        missing.insert(std::string(path) + " " + why +
                                       " (" + name + ")");
                }
            }
        }
    }
    // Both settings, 75 belt entries with TURN UNDEAD counted on both cleric
    // lines; a walk that found nothing would pass vacuously.
    EXPECT_GE(abilities, 76);
    EXPECT_TRUE(undeclared.empty())
        << "every specials row and every alternate table names its icon "
           "(packs/core/families/living-*.lua):"
        << joined(undeclared);
    EXPECT_TRUE(missing.empty())
        << missing.size()
        << " ability icon file(s) are missing or do not load as 16x16; draw "
           "them as grids in scripts/ability_icons/grids/ and run "
           "scripts/ability_icons/generate.py:"
        << joined(missing);
}

// The cleric's TURN UNDEAD is one ability on two alternate lines (slots 2 and
// 3): both name the same file. belt::catalog lists it once, which is why the
// walk above reads the descriptor.
TEST(AbilityIconFiles, both_turn_undead_lines_name_the_one_file)
{
    const FamilyDescriptor* cleric = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, cleric->alternate_icons[2]);
    ASSERT_NE(nullptr, cleric->alternate_icons[3]);
    EXPECT_STREQ("icons/turn_undead.png", cleric->alternate_icons[2]);
    EXPECT_STREQ("icons/turn_undead.png", cleric->alternate_icons[3]);
    EXPECT_STREQ("icons/heal.png", cleric->special_icons[1]);
    EXPECT_STREQ("icons/mystic_mace.png", cleric->alternate_icons[1]);
    // A family with no specials declares no icons.
    const FamilyDescriptor* golem = get_family_descriptor(FAMILY_GOLEM);
    ASSERT_NE(nullptr, golem);
    for (int slot = 0; slot < FD_NUM_SPECIALS; ++slot) {
        EXPECT_EQ(nullptr, golem->special_icons[slot]) << "slot " << slot;
        EXPECT_EQ(nullptr, golem->alternate_icons[slot]) << "slot " << slot;
    }
}

// --- 2. every committed icon keeps the palette and the gates ------------------

TEST(AbilityIconFiles, every_committed_icon_keeps_the_palette_and_the_gates)
{
    struct Dir {
        fs::path pngs;
        fs::path grids;
        std::string prefix;  // what read_pixie_file is handed
    };
    const Dir dirs[] = {
        {"pix/icons", "scripts/ability_icons/grids", "icons/"},
        {"docs/modding/examples/emberwisp/sprites",
         "scripts/ability_icons/pack_grids/emberwisp",
         "docs/modding/examples/emberwisp/sprites/"},
    };
    int checked = 0;
    bool saw_missing_tile = false;
    for (const Dir& d : dirs) {
        ASSERT_TRUE(fs::is_directory(d.pngs)) << d.pngs;
        for (const auto& item : fs::directory_iterator(d.pngs)) {
            if (item.path().extension() != ".png")
                continue;
            const std::string stem = item.path().stem().string();
            const fs::path grid = d.grids / (stem + ".txt");
            // A pack's sprites/ directory also holds its walker sheet.
            if (!fs::exists(grid)) {
                EXPECT_NE(d.prefix, "icons/")
                    << item.path() << " has no grid";
                continue;
            }
            const std::string path = d.prefix + item.path().filename().string();
            const PixieData p = read_pixie_file(path.c_str());
            ASSERT_TRUE(p.valid()) << path;
            EXPECT_EQ(kIcon, static_cast<int>(p.w)) << path;
            EXPECT_EQ(kIcon, static_cast<int>(p.h)) << path;
            EXPECT_EQ(1, static_cast<int>(p.frames)) << path << ": no sidecar";
            for (const std::string& why :
                 gate_problems(p.data.get(), grid_waives_edges(grid)))
                ADD_FAILURE() << path << ": " << why;
            saw_missing_tile = saw_missing_tile || stem == "_missing";
            ++checked;
        }
    }
    EXPECT_TRUE(saw_missing_tile) << "pix/icons/_missing.png is the fallback";
    // The generic tile, the four prototype icons and the example pack's.
    EXPECT_GE(checked, 6);
}

// The gate check above is not vacuous: the prototype's flooded HEAL (222
// opaque pixels, 39 on the border) and a picture painted in the cycled fire
// band both fail it.
TEST(AbilityIconFiles, the_gates_refuse_a_filled_tile_and_a_cycled_band)
{
    std::vector<unsigned char> tile(kPixels, 0);
    for (int y = 1; y < kIcon; ++y)
        for (int x = 0; x < kIcon - 1; ++x)
            tile[static_cast<std::size_t>(y * kIcon + x)] = 57;
    const auto filled = gate_problems(tile.data(), false);
    ASSERT_EQ(2u, filled.size());
    EXPECT_EQ("opaque 225 > 153", filled[0]);
    EXPECT_EQ("border 29 > 24", filled[1]);

    std::vector<unsigned char> fire(kPixels, 0);
    fire[7 * kIcon + 7] = 226;
    const auto cycled = gate_problems(fire.data(), false);
    ASSERT_EQ(1u, cycled.size());
    EXPECT_EQ("forbidden index 226", cycled[0]);
}

// --- 3. og::ui::AbilityIcons --------------------------------------------------

TEST(AbilityIconsLut, locked_lut_keeps_outlines_and_lifts_every_colour_off_the_fill)
{
    const auto& lut = og::ui::locked_icon_lut();
    EXPECT_EQ(0, lut[0]);
    EXPECT_EQ(16, lut[16]);
    EXPECT_EQ(22, lut[74]);   // the teleport ring
    EXPECT_EQ(24, lut[40]);   // the red ramp
    EXPECT_EQ(30, lut[239]);  // still fire yellow
    for (int i = 0; i < 256; ++i) {
        const int g = lut[static_cast<std::size_t>(i)];
        EXPECT_TRUE(g == 0 || g == 16 || (g >= 21 && g <= 31))
            << "index " << i << " -> " << g;
        if (i != 0) {
            EXPECT_NE(0, g) << "only index 0 stays transparent";
        }
    }
    // The palette entries the luma is read from: black, the fill, white-ish.
    EXPECT_DOUBLE_EQ(0.0, og::ui::icon_palette_luma(16));
    EXPECT_NEAR(72.0, og::ui::icon_palette_luma(18), 0.5);
}

TEST(AbilityIcons, a_declared_icon_loads_with_its_locked_copy)
{
    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    ASSERT_NE(nullptr, mage);
    EXPECT_STREQ("icons/teleport.png",
                 og::ui::declared_icon_path(*mage, Entry{1, false}));
    EXPECT_STREQ("icons/teleport_marker.png",
                 og::ui::declared_icon_path(*mage, Entry{1, true}));

    AbilityIcons icons;
    const AbilityIcon* teleport = icons.icon(*mage, Entry{1, false});
    ASSERT_NE(nullptr, teleport);
    const PixieData file = read_pixie_file("icons/teleport.png");
    ASSERT_TRUE(file.valid());
    ASSERT_TRUE(teleport->ready.valid());
    ASSERT_TRUE(teleport->locked.valid());
    EXPECT_EQ(1, static_cast<int>(teleport->ready.frames));
    EXPECT_EQ(kIcon, static_cast<int>(teleport->ready.w));
    EXPECT_EQ(kIcon, static_cast<int>(teleport->ready.h));
    EXPECT_NE(file.data.get(), teleport->ready.data.get())
        << "the set owns its pixels";
    const auto& lut = og::ui::locked_icon_lut();
    int ring = 0;
    for (std::size_t i = 0; i < kPixelCount; ++i) {
        EXPECT_EQ(file.data[i], teleport->ready.data[i]) << "pixel " << i;
        EXPECT_EQ(lut[file.data[i]], teleport->locked.data[i]) << "pixel " << i;
        ring += file.data[i] == 74 ? 1 : 0;
    }
    EXPECT_GT(ring, 20) << "the blue ring of mage.png frame 13";
}

TEST(AbilityIcons, a_shared_path_loads_once)
{
    const FamilyDescriptor* mage = get_family_descriptor(FAMILY_MAGE);
    const FamilyDescriptor* archmage = get_family_descriptor(FAMILY_ARCHMAGE);
    ASSERT_NE(nullptr, mage);
    ASSERT_NE(nullptr, archmage);
    AbilityIcons icons;
    const AbilityIcon* a = icons.icon(*mage, Entry{1, false});
    const AbilityIcon* b = icons.icon(*archmage, Entry{1, false});
    ASSERT_NE(nullptr, a);
    EXPECT_EQ(a, b) << "TELEPORT is one file for both classes";
    EXPECT_EQ(1u, icons.load_count());
    EXPECT_EQ(a, icons.icon_at("icons/teleport.png"));
    EXPECT_EQ(1u, icons.load_count());
}

TEST(AbilityIcons, no_icon_key_draws_the_generic_tile)
{
    FamilyDescriptor bare{};
    AbilityIcons icons;
    const AbilityIcon* generic = icons.icon(bare, Entry{1, false});
    ASSERT_NE(nullptr, generic);
    EXPECT_EQ(generic, icons.icon_at(og::ui::kMissingIconPath));
    EXPECT_EQ(generic, icons.icon(bare, Entry{2, true}));
    // An entry outside slots 1..5 names nothing, even where the arrays hold
    // a path: slot 0 is never a special, and slot 6 is past the end (the
    // entry after special_icons[5] would be alternate_icons[0]).
    FamilyDescriptor stray{};
    stray.special_icons[0] = "icons/heal.png";
    stray.alternate_icons[0] = "icons/heal.png";
    EXPECT_EQ(nullptr, og::ui::declared_icon_path(stray, Entry{0, false}));
    EXPECT_EQ(nullptr, og::ui::declared_icon_path(stray, Entry{0, true}));
    EXPECT_EQ(nullptr, og::ui::declared_icon_path(stray, Entry{6, false}));
    EXPECT_EQ(generic, icons.icon(bare, Entry{6, true}));
    EXPECT_EQ(generic, icons.icon_at(""));
    EXPECT_EQ(generic, icons.icon_at(nullptr));
    EXPECT_EQ(1u, icons.load_count()) << "only the generic tile was read";
    const PixieData tile = read_pixie_file(og::ui::kMissingIconPath);
    ASSERT_TRUE(tile.valid());
    for (std::size_t i = 0; i < kPixelCount; ++i)
        ASSERT_EQ(tile.data[i], generic->ready.data[i]) << "pixel " << i;
}

// The generic tile is grey and white, so the locked LUT can only tell its
// two copies apart through its shade: a dark grey the LUT lifts well off
// it. A locked slot that falls back to the generic tile still reads locked
// beside a ready one (and its L<n> label says so too).
//
// RED (planted): the grid's shade back at 27 (it locks to 29, 24 luma up:
// the copies are one picture at 1x). The shade is 13 pixels.
TEST(AbilityIcons, the_generic_tile_locked_reads_apart_from_ready)
{
    AbilityIcons icons;
    const AbilityIcon* generic = icons.icon_at(og::ui::kMissingIconPath);
    ASSERT_NE(nullptr, generic);
    int lifted = 0;
    for (std::size_t i = 0; i < kPixelCount; ++i) {
        const int ready = generic->ready.data[i];
        const int locked = generic->locked.data[i];
        if (ready == 0 || ready == 16)
            continue;
        if (og::ui::icon_palette_luma(locked) -
                og::ui::icon_palette_luma(ready) >= 40.0)
            ++lifted;
    }
    EXPECT_GE(lifted, 12)
        << "the shade must lock at least 40 luma lighter than it is drawn";
}

TEST(AbilityIcons, a_bad_path_draws_the_generic_tile_and_is_read_once)
{
    FamilyDescriptor fd{};
    fd.special_icons[2] = "icons/no_such_ability.png";
    fd.alternate_icons[2] = "mine.png";  // loads, but 9x9: not an icon
    AbilityIcons icons;
    const AbilityIcon* generic = icons.icon_at(og::ui::kMissingIconPath);
    ASSERT_NE(nullptr, generic);
    EXPECT_EQ(generic, icons.icon(fd, Entry{2, false}));
    EXPECT_EQ(generic, icons.icon(fd, Entry{2, true}));
    const unsigned reads = icons.load_count();
    EXPECT_EQ(3u, reads);
    EXPECT_EQ(generic, icons.icon(fd, Entry{2, false}));
    EXPECT_EQ(generic, icons.icon(fd, Entry{2, true}));
    EXPECT_EQ(reads, icons.load_count())
        << "a refused file is remembered, not re-read every frame";
}

TEST(AbilityIcons, no_generic_tile_answers_nullptr)
{
    FamilyDescriptor fd{};
    fd.special_icons[1] = "icons/no_such_ability.png";
    AbilityIcons icons("icons/no_such_generic.png");
    EXPECT_EQ(nullptr, icons.icon(fd, Entry{1, false}));
    EXPECT_EQ(nullptr, icons.icon(fd, Entry{3, false}));
    // A real icon still loads without the generic tile.
    EXPECT_NE(nullptr, icons.icon_at("icons/heal.png"));
}

TEST(AbilityIcons, a_sprite_source_change_reads_the_files_again)
{
    const FamilyDescriptor* cleric = get_family_descriptor(FAMILY_CLERIC);
    ASSERT_NE(nullptr, cleric);
    AbilityIcons icons;
    ASSERT_NE(nullptr, icons.icon(*cleric, Entry{1, false}));
    ASSERT_NE(nullptr, icons.icon(*cleric, Entry{1, false}));
    EXPECT_EQ(1u, icons.load_count());
    og::resources::note_sprite_source_changed();
    const AbilityIcon* again = icons.icon(*cleric, Entry{1, false});
    ASSERT_NE(nullptr, again);
    EXPECT_EQ(2u, icons.load_count()) << "a campaign mount or a sprite-sheet "
                                         "swap re-reads the icon files";
    EXPECT_EQ(again, icons.icon(*cleric, Entry{1, false}));
    EXPECT_EQ(2u, icons.load_count());
}

// A class pack ships its icon in its own sprites/ directory and names it by
// its virtual path, exactly like its `sprite`.
TEST(AbilityIcons, a_pack_icon_resolves_through_the_pack_mount)
{
    EmberwispMount pack;
    ASSERT_TRUE(pack.ok());
    const int family = og::families::resolve_family_string_id(
        Order::Living, "example:emberwisp");
    ASSERT_GE(family, 0);
    const FamilyDescriptor* fd = get_family_descriptor(family);
    ASSERT_NE(nullptr, fd);
    EXPECT_STREQ("packs/emberwisp/sprites/flare_burst.png",
                 og::ui::declared_icon_path(*fd, Entry{1, false}));
    AbilityIcons icons;
    const AbilityIcon* flare = icons.icon(*fd, Entry{1, false});
    ASSERT_NE(nullptr, flare);
    EXPECT_NE(flare, icons.icon_at(og::ui::kMissingIconPath))
        << "the pack's own picture, not the generic tile";
    const PixieData file =
        read_pixie_file("packs/emberwisp/sprites/flare_burst.png");
    ASSERT_TRUE(file.valid());
    for (std::size_t i = 0; i < kPixelCount; ++i)
        ASSERT_EQ(file.data[i], flare->ready.data[i]) << "pixel " << i;
}

// The touch buttons' copy of a picture (og::ui::icon_rgba): index 0 is
// transparent, every other index is its palette entry widened v * 4 (what
// pointb and putdata draw), alpha 255; the locked copy converts through the
// same table; a picture that is not one 16x16 frame is all transparent.
//
// RED (planted): `v * 4` -> `v * 255 / 63` (the fill reads 73, not 72);
// index 0 kept opaque (pixel 0's alpha reads 255).
TEST(AbilityIcons, icon_rgba_is_the_drawn_picture_in_straight_alpha)
{
    std::array<unsigned char, 768> palette{};
    for (int i = 0; i < 768; ++i)
        palette[static_cast<std::size_t>(i)] = our_pal_lookup(i);
    unsigned char raw[kPixelCount] = {};
    raw[1] = 18;   // the slot's fill grey
    raw[2] = 16;   // the outline black
    raw[3] = 40;   // the red ramp
    // PixieData owns the buffer it is handed.
    auto* owned = new unsigned char[kPixelCount];
    std::memcpy(owned, raw, kPixelCount);
    const PixieData picture(1, kIcon, kIcon, owned);
    const auto rgba = og::ui::icon_rgba(picture, palette);
    // Index 0 (pixel 0): nothing at all.
    for (std::size_t c = 0; c < 4; ++c)
        EXPECT_EQ(0, rgba[c]) << "channel " << c;
    // Index 18: (72, 72, 72, 255).
    EXPECT_EQ(72, rgba[4]);
    EXPECT_EQ(72, rgba[5]);
    EXPECT_EQ(72, rgba[6]);
    EXPECT_EQ(255, rgba[7]);
    // Index 16: opaque black, not transparent.
    EXPECT_EQ(0, rgba[8]);
    EXPECT_EQ(255, rgba[11]);
    // Index 40: its palette entry times four.
    for (std::size_t c = 0; c < 3; ++c)
        EXPECT_EQ(our_pal_lookup(40 * 3 + static_cast<int>(c)) * 4,
                  rgba[12 + c]);
    EXPECT_EQ(255, rgba[15]);

    // A real icon and its locked copy: every pixel is the table's.
    AbilityIcons icons;
    const AbilityIcon* teleport = icons.icon_at("icons/teleport.png");
    ASSERT_NE(nullptr, teleport);
    const auto& lut = og::ui::locked_icon_lut();
    const auto ready = og::ui::icon_rgba(teleport->ready, palette);
    const auto locked = og::ui::icon_rgba(teleport->locked, palette);
    for (std::size_t i = 0; i < kPixelCount; ++i) {
        const std::size_t r = teleport->ready.data[i];
        const std::size_t g = lut[r];
        ASSERT_EQ(g, static_cast<std::size_t>(teleport->locked.data[i]));
        ASSERT_EQ(r == 0 ? 0 : 255, ready[i * 4 + 3]) << "pixel " << i;
        ASSERT_EQ(g == 0 ? 0 : 255, locked[i * 4 + 3]) << "pixel " << i;
        if (g != 0) {
            ASSERT_EQ(palette[g * 3] * 4, locked[i * 4]) << "pixel " << i;
        }
    }

    // Not one 16x16 frame: transparent throughout.
    auto* small = new unsigned char[64]();
    small[0] = 18;
    const PixieData wrong(1, 8, 8, small);
    const auto none = og::ui::icon_rgba(wrong, palette);
    for (const unsigned char byte : none)
        ASSERT_EQ(0, byte);
}
