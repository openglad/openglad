/* Shared picker business logic implementation.
 *
 * Extracted from picker.cpp, picker_team_build.cpp, and text_picker.cpp
 * so both SDL and text clients call the same code.
 */

#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/campaign_state_providers.h> // G4 my_team fallback
#include <openglad/resources/save_data.h>
#include <openglad/resources/io_common.h>
#include <openglad/core/campaign_ids.h>
#include <openglad/core/constants.h>
#include <openglad/core/text_wrap.h>
#include <openglad/core/util.h>
#include <openglad/core/scale_mode.h>
#include <optional>
#include <openglad/core/tower_constants.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/irandom.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string_view>

// Defined in entities/guy.cpp
std::uint32_t calculate_exp(std::int32_t level);

// Legacy global used by entity code (living.cpp, walker.cpp) via
// extern std::int32_t difficulty_level[].  Centralised here so both
// the SDL and headless clients share a single definition.
// Difficulty settings .. in percent, so 100 == normal
extern const std::int32_t difficulty_level[DIFFICULTY_SETTINGS] = {50, 100, 200};

namespace og::ui {

// --- Constants ---

const char* const kDifficultyNames[DIFFICULTY_SETTINGS] = {
    "Skirmish",
    "Battle",
    "Slaughter",
};

// --- Campaign browser geometry ---

namespace {

// Small-font metrics used by every campaign-browser text row.
constexpr int kGlyphAdvance = 6;
constexpr int kGlyphHeight = 8;

} // namespace

bool picker_rects_overlap(const PickerRect& a, const PickerRect& b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

CampaignPickerLayout campaign_picker_layout()
{
    constexpr int kScreenW = 320;
    constexpr int kScreenH = 200;
    // The screen's two-column grid: the list pane on the left, the detail
    // pane on the right; every rect derives from these edges.
    constexpr int kListX = 8;
    constexpr int kListW = 120;
    constexpr int kDetailX = 136;
    constexpr int kDetailW = 176;

    CampaignPickerLayout layout;

    layout.header_y = 4;
    layout.list_rows = 6;
    layout.list_row_h = 10;
    layout.list_row_pitch = 12;
    layout.list = PickerRect{kListX, 14, kListW,
                             layout.list_row_pitch * (layout.list_rows - 1) +
                                 layout.list_row_h};
    // A marker column (6px) leads each row; the label uses the rest.
    layout.list_label_max_chars = (kListW - 2 * kGlyphAdvance) / kGlyphAdvance;

    // Page controls sit directly under the list with the "N of M" readout
    // centered between them.
    const int pager_y = layout.list.y + layout.list.h + 6;
    layout.prev = PickerRect{kListX, pager_y, 30, 10};
    layout.next = PickerRect{kListX + kListW - 30, pager_y, 30, 10};

    layout.choose = PickerRect{kScreenW / 2 + 20, kScreenH - 15, 30, 10};
    layout.cancel = PickerRect{kScreenW / 2 - 38 - 20, kScreenH - 15, 38, 10};

    // DELETE and RESET share one cell (the browser shows exactly one of them).
    layout.delete_button = PickerRect{kScreenW - 50, 10, 38, 10};
    layout.reset_button = layout.delete_button;

    // ENTER ID sits directly under DELETE/RESET, right edges flush.
    layout.id_button = PickerRect{
        layout.delete_button.x + layout.delete_button.w - 52,
        layout.delete_button.y + layout.delete_button.h + 2,
        52,
        10};

    // Detail pane: title, version row, icon, power/completion/authors lines,
    // then the description box with the MORE control under its right edge.
    layout.detail = PickerRect{kDetailX, 36, kDetailW, 140};
    layout.title_center_x = kDetailX + kDetailW / 2;
    layout.title_y = layout.detail.y;
    layout.icon = PickerRect{layout.title_center_x - 16, 56, 32, 32};
    layout.desc_box = PickerRect{kDetailX + 4, 126, kDetailW - 8, 44};
    layout.desc_rows = 4;
    layout.desc_max_chars = (layout.desc_box.w - 10) / kGlyphAdvance;
    layout.more_button = PickerRect{
        kDetailX + kDetailW - 40, layout.desc_box.y + layout.desc_box.h + 2,
        40, 10};

    // The title is centered in the detail pane, so a title of L glyphs spans
    // center +/- 3L; the pane edges cap the half-width. Computed rather than
    // written down so the budget follows the pane if it ever moves again.
    const int right_room =
        layout.detail.x + layout.detail.w - layout.title_center_x;
    const int left_room = layout.title_center_x - layout.detail.x;
    const int half_room = right_room < left_room ? right_room : left_room;
    layout.title_max_chars = (2 * half_room) / kGlyphAdvance;
    if (layout.title_max_chars < 0)
        layout.title_max_chars = 0;
    return layout;
}

PickerRect campaign_picker_row_rect(int row)
{
    const CampaignPickerLayout layout = campaign_picker_layout();
    if (row < 0)
        row = 0;
    if (row >= layout.list_rows)
        row = layout.list_rows - 1;
    return PickerRect{layout.list.x, layout.list.y + layout.list_row_pitch * row,
                      layout.list.w, layout.list_row_h};
}

PickerRect campaign_title_rect(int chars)
{
    const CampaignPickerLayout layout = campaign_picker_layout();
    if (chars < 0)
        chars = 0;
    const int width = chars * kGlyphAdvance;
    return PickerRect{layout.title_center_x - chars * (kGlyphAdvance / 2),
                      layout.title_y, width, kGlyphHeight};
}

std::string fit_text_to_chars(std::string_view text, int budget)
{
    if (budget <= 0)
        return std::string();
    if (text.size() <= static_cast<std::size_t>(budget))
        return std::string(text);
    if (budget <= 3)
        return std::string(text.substr(0, static_cast<std::size_t>(budget)));
    return std::string(text.substr(0, static_cast<std::size_t>(budget) - 3)) + "...";
}

std::string fit_campaign_title(std::string_view title)
{
    return fit_text_to_chars(title, campaign_picker_layout().title_max_chars);
}

std::string fit_campaign_row_label(std::string_view title)
{
    return fit_text_to_chars(title,
                             campaign_picker_layout().list_label_max_chars);
}

int campaign_list_clamp_offset(int offset, int total, int rows)
{
    if (rows <= 0 || total <= rows)
        return 0;
    if (offset < 0)
        return 0;
    if (offset > total - rows)
        return total - rows;
    return offset;
}

int campaign_list_offset_for_cursor(int cursor, int offset, int total, int rows)
{
    offset = campaign_list_clamp_offset(offset, total, rows);
    if (rows <= 0 || total <= 0)
        return 0;
    if (cursor < offset)
        offset = cursor;
    else if (cursor >= offset + rows)
        offset = cursor - rows + 1;
    return campaign_list_clamp_offset(offset, total, rows);
}

int campaign_list_page_step(int offset, int total, int rows, int direction)
{
    const int step = direction < 0 ? -rows : rows;
    return campaign_list_clamp_offset(offset + step, total, rows);
}

int campaign_list_clamp_cursor(int cursor, int offset, int total, int rows)
{
    if (total <= 0)
        return 0;
    offset = campaign_list_clamp_offset(offset, total, rows);
    const int visible = std::min(rows, total - offset);
    const int last = offset + (visible > 0 ? visible : 1) - 1;
    return std::clamp(cursor, offset, last);
}

std::string format_campaign_position_label(int cursor, int total)
{
    if (total <= 0)
        return "0 of 0";
    return std::format("{} of {}", std::clamp(cursor, 0, total - 1) + 1, total);
}

bool campaign_description_overflows(const std::string& description)
{
    const CampaignPickerLayout layout = campaign_picker_layout();
    return static_cast<int>(
               og::core::wrap_text(description, layout.desc_max_chars,
                                   og::core::WrapMode::Paragraphs)
                   .size()) > layout.desc_rows;
}

std::array<CampaignPickerNavLinks, kCampaignPickerButtonCount>
campaign_picker_nav(const CampaignPickerVisibility& visibility)
{
    std::array<CampaignPickerNavLinks, kCampaignPickerButtonCount> nav{};

    const int rows =
        std::clamp(visibility.visible_rows, 0, kCampaignPickerRowCount);
    const int top_right = visibility.delete_hidden ? kCampaignPickerResetIndex
                                                   : kCampaignPickerDeleteIndex;
    const int first_row = rows > 0 ? kCampaignPickerRowBaseIndex : -1;
    const int last_row = rows > 0 ? kCampaignPickerRowBaseIndex + rows - 1 : -1;
    // Leaving the list downward lands on the pagers when a pager shows,
    // otherwise straight on CANCEL (always visible).
    const int below_list = !visibility.prev_hidden ? kCampaignPickerPrevIndex
        : (!visibility.next_hidden ? kCampaignPickerNextIndex
                                   : kCampaignPickerCancelIndex);
    // The bottom of the left stack, as seen from CANCEL going up.
    const int left_stack_bottom = !visibility.prev_hidden
        ? kCampaignPickerPrevIndex
        : (!visibility.next_hidden
               ? kCampaignPickerNextIndex
               : (last_row >= 0 ? last_row : kCampaignPickerIdIndex));

    nav[kCampaignPickerPrevIndex] = {
        .up = last_row >= 0 ? last_row : kCampaignPickerIdIndex,
        .down = kCampaignPickerCancelIndex,
        .right = visibility.next_hidden ? -1 : kCampaignPickerNextIndex};
    nav[kCampaignPickerNextIndex] = {
        .up = last_row >= 0 ? last_row : kCampaignPickerIdIndex,
        .down = kCampaignPickerCancelIndex,
        .left = visibility.prev_hidden ? -1 : kCampaignPickerPrevIndex};
    nav[kCampaignPickerChooseIndex] = {
        .up = visibility.more_hidden ? kCampaignPickerIdIndex
                                     : kCampaignPickerMoreIndex,
        .left = kCampaignPickerCancelIndex};
    nav[kCampaignPickerCancelIndex] = {
        .up = left_stack_bottom,
        .right = visibility.choose_hidden ? -1 : kCampaignPickerChooseIndex};

    // DELETE / RESET share the top-right cell and drop onto ENTER ID, which
    // sits directly below them; leftward they enter the top of the list.
    nav[kCampaignPickerDeleteIndex] = {.down = kCampaignPickerIdIndex,
                                       .left = first_row};
    nav[kCampaignPickerResetIndex] = {.down = kCampaignPickerIdIndex,
                                      .left = first_row};
    nav[kCampaignPickerIdIndex] = {
        .up = top_right,
        .down = !visibility.more_hidden ? kCampaignPickerMoreIndex
            : (visibility.choose_hidden ? kCampaignPickerCancelIndex
                                        : kCampaignPickerChooseIndex),
        .left = first_row};
    nav[kCampaignPickerMoreIndex] = {
        .up = kCampaignPickerIdIndex,
        .down = visibility.choose_hidden ? kCampaignPickerCancelIndex
                                         : kCampaignPickerChooseIndex,
        .left = last_row};

    // List rows: up/down walk the visible rows; the top row exits to
    // DELETE/RESET and the bottom visible row to the pager band; rightward
    // every row reaches the detail-side stack via ENTER ID. Rows past
    // visible_rows are hidden, so their links are never followed.
    for (int i = 0; i < kCampaignPickerRowCount; ++i)
    {
        const int index = kCampaignPickerRowBaseIndex + i;
        nav[static_cast<std::size_t>(index)] = {
            .up = i == 0 ? top_right : index - 1,
            .down = (i >= rows - 1) ? below_list : index + 1,
            .right = kCampaignPickerIdIndex};
    }
    return nav;
}

bool apply_campaign_selection(SaveData& save, const std::string& campaign_id,
                              int first_level)
{
    const std::string previous_campaign = save.current_campaign;
    const int level = load_campaign(campaign_id, save.current_levels, first_level);
    if (level < 0)
    {
        // Mirrors do_set_scen_level's rollback: keep the save untouched and
        // put the previous campaign's package back so level data stays
        // loadable.
        (void)load_campaign(previous_campaign, save.current_levels,
                            static_cast<int>(save.scen_num));
        return false;
    }
    // A campaign switch abandons any replay excursion in flight: the arm's
    // origin is a cursor in the PREVIOUS campaign, and restoring it after a
    // foreign replay would plant an unearned cursor there (#207).
    save.clear_replay_arm();
    save.current_campaign = campaign_id;
    save.scen_num = static_cast<short>(level);
    return true;
}

// --- Level browser geometry ---

LevelPickerLayout level_picker_layout()
{
    constexpr int kScreenW = 320;
    constexpr int kScreenH = 200;
    // Left column: the three preview rows. Right column: everything else,
    // sharing kRightX as its left edge.
    constexpr int kRowX = 10;
    constexpr int kRightX = 174;

    LevelPickerLayout layout;
    layout.row_x = kRowX;
    layout.row0_y = 4;
    layout.row_count = 3;
    layout.radar_dy = 9;
    layout.radar_max_w = 60;  // RADAR_X/RADAR_Y viewport clamps (radar.cpp)
    layout.radar_max_h = 44;
    // Title line (8) + radar (44) + frame and breathing room; three rows at
    // this pitch end at row0_y + 2*pitch + radar_dy + radar_max_h + 2 = 183,
    // fully on the 200px screen.
    layout.row_pitch = 62;

    layout.stats_x = kRowX + layout.radar_max_w + 6;
    layout.stats_max_chars = (kRightX - layout.stats_x - 2) / kGlyphAdvance;
    layout.title_max_chars = 16;
    layout.status_x = kRowX + (layout.title_max_chars + 3 + 1) * kGlyphAdvance;

    layout.delete_button = PickerRect{kScreenW - 50, 10, 38, 10};
    layout.id_button = PickerRect{layout.delete_button.x - 52 - 10, 10, 52, 10};
    layout.prev = PickerRect{kRightX, 22, 30, 10};
    layout.army_x = layout.prev.x + layout.prev.w + 6;
    layout.army_y = layout.prev.y + 2;
    layout.desc_box = PickerRect{kRightX, 36, kScreenW - 8 - kRightX, 112};
    layout.desc_max_chars = (layout.desc_box.w - 4) / kGlyphAdvance;
    layout.next = PickerRect{
        kRightX, layout.desc_box.y + layout.desc_box.h + 4, 30, 10};
    layout.choose = PickerRect{kScreenW - 50, kScreenH - 30, 30, 10};
    layout.cancel = PickerRect{kScreenW - 100, kScreenH - 30, 38, 10};
    return layout;
}

int level_picker_row_y(int row)
{
    const LevelPickerLayout layout = level_picker_layout();
    return layout.row0_y + layout.row_pitch * row;
}

const char* level_row_status_label(bool cleared, bool current)
{
    if (cleared)
        return "CLEARED";
    if (current)
        return "CURRENT";
    return "";
}

// --- Family display helpers ---

const char* family_display_name(int family)
{
    const auto* fd = get_family_descriptor(family);
    if (fd)
        return fd->name;
    return "BEAST";
}

const char* family_short_name(short family)
{
    const auto* fd = get_family_descriptor(family);
    if (fd)
        return fd->short_name ? fd->short_name : fd->name;
    return "BEAST";
}

// --- Cost calculations ---

int family_hiring_base_cost(int family)
{
    const FamilyDescriptor* fd = get_family_descriptor(family);
    return fd ? static_cast<int>(fd->hiring_cost) : 0;
}

std::uint32_t calculate_hire_cost(const guy& recruit)
{
    const auto* fd = get_family_descriptor(recruit.family);
    if (!fd)
        return 0;

    std::int32_t temp = fd->hiring_cost;

    // Stat upgrade costs above base (clamped to 0 if below base)
    auto stat_delta = [](short stat, std::int32_t base) -> std::int32_t {
        std::int32_t d = static_cast<std::int32_t>(stat) - base;
        return d > 0 ? d : 0;
    };

    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.strength, fd->base_stats[StatAxis::Strength]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Strength]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.dexterity, fd->base_stats[StatAxis::Dexterity]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Dexterity]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.constitution, fd->base_stats[StatAxis::Constitution]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Constitution]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.intelligence, fd->base_stats[StatAxis::Intelligence]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Intelligence]);
    temp += static_cast<std::int32_t>(
        std::pow(stat_delta(recruit.armor, fd->base_stats[StatAxis::Armor]), kStatCostExponent)
        * fd->stat_costs[StatAxis::Armor]);

    // Level cost
    short effective_level = std::max(recruit.level, static_cast<short>(fd->base_stats[StatAxis::Level]));
    std::int32_t exp_cost = static_cast<std::int32_t>(calculate_exp(effective_level));
    if (exp_cost < 0) // overflow
        return 0;
    temp += exp_cost;

    if (temp < 0)
        return 0;

    return static_cast<std::uint32_t>(temp);
}

std::uint32_t calculate_train_cost(const guy& current, const guy& original)
{
    const auto* fd = get_family_descriptor(current.family);
    if (!fd)
        return 0;

    std::int32_t temp = 0;

    // Use effective stats: current stats clamped to not go below original
    auto effective = [](short cur, short orig) -> short {
        return cur > orig ? cur : orig;
    };

    short eff_str = effective(current.strength, original.strength);
    short eff_dex = effective(current.dexterity, original.dexterity);
    short eff_con = effective(current.constitution, original.constitution);
    short eff_int = effective(current.intelligence, original.intelligence);
    short eff_arm = effective(current.armor, original.armor);
    short eff_lvl = std::max(current.level, original.level);

    // Level XP cost
    if (calculate_exp(eff_lvl) > original.exp)
        temp += static_cast<std::int32_t>(calculate_exp(eff_lvl) - original.exp);

    // Stat costs only if not upgrading level
    if (eff_lvl <= original.level) {
        auto stat_cost = [&](short stat, short old_stat, int idx) -> std::int32_t {
            std::int32_t base = fd->base_stats[idx];
            std::int32_t new_delta = std::max(static_cast<std::int32_t>(stat) - base,
                                              std::int32_t{0});
            std::int32_t old_delta = std::max(static_cast<std::int32_t>(old_stat) - base,
                                              std::int32_t{0});
            std::int32_t new_cost = static_cast<std::int32_t>(
                std::pow(new_delta, kStatCostExponent) * fd->stat_costs[idx]);
            std::int32_t old_cost = static_cast<std::int32_t>(
                std::pow(old_delta, kStatCostExponent) * fd->stat_costs[idx]);
            return new_cost - old_cost;
        };

        temp += stat_cost(eff_str, original.strength, 0);
        temp += stat_cost(eff_dex, original.dexterity, 1);
        temp += stat_cost(eff_con, original.constitution, 2);
        temp += stat_cost(eff_int, original.intelligence, 3);
        temp += stat_cost(eff_arm, original.armor, 4);
    }

    if (temp < 0)
        return 0;

    return static_cast<std::uint32_t>(temp);
}

std::uint32_t calculate_sell_value(const guy& member)
{
    // query_heart_value() is the death-drop valuation. It is historically
    // non-const, so query a copy while keeping this helper pure.
    guy valued(member);
    const std::int32_t heart_value = valued.query_heart_value();
    if (heart_value <= 0)
        return 0;

    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(heart_value) * 3u / 4u);
}

// --- Name generation ---

namespace {

bool has_name_in_save(const char* name, const SaveData& save)
{
    for (int i = 0; i < save.team_size && i < MAX_TEAM_SIZE; i++) {
        if (save.team_list[static_cast<std::size_t>(i)] && save.team_list[static_cast<std::size_t>(i)]->name == name)
            return true;
    }
    return false;
}

} // anonymous namespace

// Recruit names come from the family descriptor's pool (`names` in the
// declaration), so a class pack's family names its own recruits with no
// engine change. Families that deliberately share a pool — archmage with
// mage, the orc captain with the orcs, all three slimes — do so by shipping
// the same list, which is how the switch this replaced behaved.
//
// A family with no pool of its own borrows the soldier pool, the fallback the
// old `default:` branch used; the core pack leaves the three unhireable
// families (golem, giant skeleton, tower) poolless on purpose.
const char* get_random_name(unsigned char family)
{
    const FamilyDescriptor* fd = get_family_descriptor(family);
    if (fd == nullptr || fd->name_pool == nullptr || fd->name_pool_size <= 0)
        fd = get_family_descriptor(FAMILY_SOLDIER);
    if (fd == nullptr || fd->name_pool == nullptr || fd->name_pool_size <= 0)
        return "Nameless";  // no pool at all: not reachable with a core pack
    return fd->name_pool[std::rand() % fd->name_pool_size];
}

std::string get_unique_name(unsigned char family, const SaveData& save)
{
    const char* result = get_random_name(family);

    // Try a few times to get a unique name
    int i = 0;
    while (has_name_in_save(result, save) && i < 10) {
        result = get_random_name(family);
        i++;
    }

    // Still a duplicate? Append a number.
    if (has_name_in_save(result, save)) {
        i = 2;
        std::string numbered;
        do {
            numbered = std::format("{}{}", result, i);
            i++;
        } while (has_name_in_save(numbered.c_str(), save));
        return numbered;
    }

    return std::string(result);
}

// --- Company name generation (design §2.2) ---

namespace {

// Word banks for "<ADJ> <NOUN> <GROUP>". Budget contract (asserted by the
// unit pins over company_name_banks()): adjectives <= 5 chars, nouns <= 6,
// groups <= 5, so the longest combination is 5 + 1 + 6 + 1 + 5 = 18
// = kCompanyNameMaxLen — the generator can never overflow and never
// truncates (the cap is load-bearing: §2.2 promises the name fits every
// later surface untrimmed).
constexpr const char* kCompanyAdjectives[] = {
    "IRON", "GREY", "BLACK", "WHITE", "ASHEN", "AMBER", "IVORY", "STONE",
    "THORN", "EMBER", "RIVER", "NORTH", "SABLE", "FROST", "STORM", "GOLD",
};

constexpr const char* kCompanyNouns[] = {
    "KETTLE", "RAVEN", "FALCON", "WOLF", "LION", "DRAGON", "HAMMER",
    "SHIELD", "DAGGER", "BANNER", "VIPER", "BADGER", "WYVERN", "LANCE",
    "CROW", "ANVIL",
};

constexpr const char* kCompanyGroups[] = {
    "BAND", "CREW", "HOST", "GUILD", "ORDER", "LODGE", "TROOP", "WATCH",
    "PACK", "CADRE",
};

const char* pick_bank_word(IRandom& rng, std::span<const char* const> bank)
{
    return bank[rng.next(static_cast<std::uint32_t>(bank.size()))];
}

} // namespace

CompanyNameBanks company_name_banks()
{
    return CompanyNameBanks{
        std::span<const char* const>(kCompanyAdjectives),
        std::span<const char* const>(kCompanyNouns),
        std::span<const char* const>(kCompanyGroups),
    };
}

std::string generate_company_name(IRandom& rng)
{
    // Exactly three draws, adjective->noun->group, so the output for a given
    // rng state is pinned (determinism is part of the API contract).
    const char* adjective = pick_bank_word(rng, company_name_banks().adjectives);
    const char* noun = pick_bank_word(rng, company_name_banks().nouns);
    const char* group = pick_bank_word(rng, company_name_banks().groups);
    return std::format("{} {} {}", adjective, noun, group);
}

// --- Team queries ---

int count_family_members(int family, const SaveData& save)
{
    int counter = 0;
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (save.team_list[static_cast<std::size_t>(i)] && save.team_list[static_cast<std::size_t>(i)]->family == family)
            counter++;
    }
    return counter;
}

// --- Team operations ---

int add_recruit_to_team(SaveData& save, std::unique_ptr<guy> recruit, int team_num)
{
    if (save.team_size >= MAX_TEAM_SIZE)
        return -1;

    recruit->teamnum = static_cast<short>(team_num);

    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        // found an empty slot
        if (!save.team_list[static_cast<std::size_t>(i)]) {
            save.team_list[static_cast<std::size_t>(i)] = std::move(recruit);
            save.team_size++;
            return i;
        }
    }

    return -1;
}

std::unique_ptr<guy> create_recruit(int family, int team_num, const SaveData& save)
{
    auto recruit = std::make_unique<guy>(family);
    recruit->teamnum = static_cast<short>(team_num);
    recruit->name = get_unique_name(static_cast<unsigned char>(family), save);
    return recruit;
}

void reset_for_new_game(SaveData& save)
{
    save.reset();
    save.totalcash = kNewGameStartingGold;
}

void ensure_team_populated(SaveData& save, const std::vector<int>& families, int team_num)
{
    if (save.team_size > 0)
        return;

    for (size_t i = 0; i < families.size() && save.team_size < MAX_TEAM_SIZE; ++i) {
        auto recruit = create_recruit(families[i], team_num, save);
        add_recruit_to_team(save, std::move(recruit), team_num);
    }

    // Fallback: if families was empty or all failed, add a soldier.
    if (save.team_size == 0) {
        auto recruit = create_recruit(FAMILY_SOLDIER, team_num, save);
        add_recruit_to_team(save, std::move(recruit), team_num);
    }
}

// --- Derived stats ---

DerivedStats compute_derived_stats(const guy& g,
    float base_hp, float base_damage, float base_stepsize, float base_fire_freq)
{
    DerivedStats ds;
    ds.hp = std::ceil(base_hp + g.get_hp_bonus());
    ds.mp = std::ceil(g.get_mp_bonus());
    ds.atk = base_damage + g.get_damage_bonus();
    ds.def = g.get_armor_bonus();
    ds.spd = base_stepsize + g.get_speed_bonus();
    // The 10.0f/fire_frequency is somewhat arbitrary, but it makes for good comparison info.
    float freq = base_fire_freq - g.get_fire_frequency_bonus();
    if (freq < 1.0f)
        freq = 1.0f;
    ds.atk_spd = 10.0f / freq;
    return ds;
}

// --- Difficulty ---

int cycle_difficulty(int current)
{
    return (current + 1) % DIFFICULTY_SETTINGS;
}

int difficulty_percent(int difficulty)
{
    const int normalized =
        ((difficulty % DIFFICULTY_SETTINGS) + DIFFICULTY_SETTINGS) % DIFFICULTY_SETTINGS;
    return static_cast<int>(difficulty_level[normalized]);
}

// --- Allied mode ---

void toggle_allied_mode(SaveData& save)
{
    save.allied_mode = static_cast<short>((save.allied_mode + 1) % 2);
}

bool is_allied_mode(const SaveData& save)
{
    return save.allied_mode != 0;
}

// --- CTF match settings ---

// The TEAMS pair (cycle_ctf_team_count / format_ctf_teams_label) is GONE
// (amendment A3): dropping an authored team is the LINEUP band's own
// BOTS: OFF, and nothing reads ctf_team_count as a control any more. The
// save/wire field keeps its place — every authority home snaps it to 0 —
// but no menu offers it and no label speaks for it.
void cycle_ctf_capture_limit(SaveData& save)
{
    switch (save.ctf_capture_limit)
    {
        case 0: save.ctf_capture_limit = 1; break;
        case 1: save.ctf_capture_limit = 3; break;
        case 3: save.ctf_capture_limit = 5; break;
        case 5: save.ctf_capture_limit = 10; break;
        default: save.ctf_capture_limit = 0; break;
    }
}

bool is_versus_campaign(const SaveData& save)
{
    return og::data::campaign_matchup(save.current_campaign) == "versus";
}

std::uint8_t ctf_authored_team_mask_for_loaded_level(
    const SaveData& save,
    const GameWorld& world,
    std::string_view mounted_campaign)
{
    if (!is_versus_campaign(save) ||
        mounted_campaign != save.current_campaign ||
        world.id != save.scen_num ||
        (world.type & GameWorld::TYPE_SCRIPTED) == 0)
    {
        return 0;
    }
    return og::sim::authored_team_mask(world);
}

// --- Difficulty submenu match rules ---

void cycle_respawn_mode(SaveData& save)
{
    switch (save.respawn_mode)
    {
        case og::sim::kRespawnModeOff:
            save.respawn_mode = og::sim::kRespawnModeHeroes;
            break;
        case og::sim::kRespawnModeHeroes:
            save.respawn_mode = og::sim::kRespawnModeEveryone;
            break;
        case og::sim::kRespawnModeEveryone:
            save.respawn_mode = og::sim::kRespawnModeTeamOneHeroes;
            break;
        case og::sim::kRespawnModeTeamOneHeroes:
        default:
            save.respawn_mode = og::sim::kRespawnModeOff;
            break;
    }
}

void cycle_respawn_delay(SaveData& save)
{
    switch (save.ctf_respawn_ticks)
    {
        case 0: save.ctf_respawn_ticks = 60; break;
        case 60: save.ctf_respawn_ticks = 360; break;
        default: save.ctf_respawn_ticks = 0; break;
    }
}

void toggle_permadeath(SaveData& save)
{
    save.keep_fallen_heroes =
        static_cast<short>(save.keep_fallen_heroes != 0 ? 0 : 1);
}

void cycle_generator_rate(SaveData& save)
{
    switch (save.generator_rate)
    {
        case 0: save.generator_rate = 50; break;
        case 50: save.generator_rate = 200; break;
        default: save.generator_rate = 0; break;
    }
}

void toggle_infinite_gold(SaveData& save)
{
    save.infinite_gold = static_cast<short>(save.infinite_gold != 0 ? 0 : 1);
}

bool gold_is_infinite(const SaveData& save) noexcept
{
    return save.infinite_gold != 0;
}

bool can_afford(const SaveData& save, int team, std::uint32_t cost) noexcept
{
    if (gold_is_infinite(save))
        return true;
    const int wallet_team = std::clamp(team, 0, MAX_PLAYERS - 1);
    return cost <= save.m_totalcash[static_cast<std::size_t>(wallet_team)];
}

std::string format_wallet_amount(const SaveData& save, int team)
{
    if (gold_is_infinite(save))
        return "INF";
    const int wallet_team = std::clamp(team, 0, MAX_PLAYERS - 1);
    return std::format("{}",
                       static_cast<unsigned>(
                           save.m_totalcash[static_cast<std::size_t>(wallet_team)]));
}

// --- Scripted campaign picker wallet (issue #206) ---

// The acting team mirrors og::data::make_campaign_providers (the Base Camp
// gold-label rule): the lowest team present on the roster, my_team fallback,
// clamped to [0,3]. In a networked lobby the autosave merge persists only
// owned teams' wallets, so this rule keeps every scripted debit durable.
static int campaign_acting_team(const SaveData& save)
{
    int team = MAX_PLAYERS;
    for (const auto& member : save.team_list)
    {
        if (member != nullptr && member->teamnum >= 0 &&
            member->teamnum < MAX_PLAYERS)
        {
            team = std::min(team, static_cast<int>(member->teamnum));
        }
    }
    if (team == MAX_PLAYERS)
    {
        team = (save.my_team >= 0 && save.my_team < MAX_PLAYERS)
            ? save.my_team
            : 0;
    }
    return team;
}

bool campaign_picker_can_afford(const SaveData& save, int cost)
{
    if (cost <= 0)
        return true;
    return can_afford(save, campaign_acting_team(save),
                      static_cast<std::uint32_t>(cost));
}

void campaign_picker_debit(SaveData& save, int cost)
{
    // Infinite gold makes the purchase FREE (the hire/train free_purchase
    // rule): the wallet is never written, so no autosave can bake a cheat
    // balance into the .gtl file.
    if (cost <= 0 || gold_is_infinite(save))
        return;
    std::uint32_t& wallet =
        save.m_totalcash[static_cast<std::size_t>(campaign_acting_team(save))];
    wallet -= std::min(wallet, static_cast<std::uint32_t>(cost));
}

void campaign_picker_refund(SaveData& save, int cost)
{
    // The exact inverse of campaign_picker_debit, for the one case a debit
    // must not stick: an action row served by no registered picker_action.
    // Under infinite gold the debit wrote nothing, so neither does this.
    if (cost <= 0 || gold_is_infinite(save))
        return;
    std::uint32_t& wallet =
        save.m_totalcash[static_cast<std::size_t>(campaign_acting_team(save))];
    const std::uint32_t amount = static_cast<std::uint32_t>(cost);
    wallet = wallet > UINT32_MAX - amount ? UINT32_MAX : wallet + amount;
}

// --- GRAPHICS FX depth selector (cfg effects/depth_fx) ---

// Out-of-set values (including the empty string an absent key reads as)
// count as the default, fog — the same rule depth_fx_mode_from_setting
// applies in the renderer, so the label always names what is drawn.
static std::string normalize_depth_fx_value(const std::string& value)
{
    if (value == "off" || value == "tint" || value == "haze" || value == "mist")
        return value;
    return "fog";
}

std::string cycle_depth_fx(const std::string& current)
{
    const std::string value = normalize_depth_fx_value(current);
    if (value == "fog")
        return "haze";
    if (value == "haze")
        return "mist";
    if (value == "mist")
        return "tint";
    if (value == "tint")
        return "off";
    return "fog"; // off wraps back to the default
}

std::string format_depth_fx_label(const std::string& value)
{
    const std::string v = normalize_depth_fx_value(value);
    if (v == "off")
        return "Depth: Off";
    if (v == "tint")
        return "Depth: Tint";
    if (v == "haze")
        return "Depth: Haze";
    if (v == "mist")
        return "Depth: Mist";
    return "Depth: Fog";
}

bool depth_fx_is_active(const std::string& value)
{
    return normalize_depth_fx_value(value) != "off";
}

// --- GAME SETTINGS speed selector (cfg gameplay/timer_wait) ---

// The stored value is the sim's per-tick wait (GameWorld::timer_wait, 0..20
// as the host-request clamp in game_server.cpp allows); the player-facing
// number is the inverted 1..11 the retired in-game options menu showed.
int parse_timer_wait(const std::string& value)
{
    const std::optional<int> parsed = parse_int_strict(value);
    if (!parsed)
        return og::sim::DEFAULT_TIMER_WAIT;
    return std::clamp(*parsed, kTimerWaitFastest, kTimerWaitSlowest);
}

int game_speed_from_timer_wait(int timer_wait)
{
    const int wait = std::clamp(timer_wait, kTimerWaitFastest, kTimerWaitSlowest);
    return (kTimerWaitSlowest - wait) / 2 + 1;
}

int timer_wait_from_game_speed(int speed)
{
    const int step = std::clamp(speed, kGameSpeedMin, kGameSpeedMax);
    return kTimerWaitSlowest - (step - 1) * 2;
}

// One click = one step faster, wrapping from the fastest back to the
// slowest. An odd stored wait (nothing writes one today, but a hand-edited
// cfg can) normalizes onto the even lap at the first click.
std::string cycle_game_speed(const std::string& current)
{
    const int speed = game_speed_from_timer_wait(parse_timer_wait(current));
    const int next = speed >= kGameSpeedMax ? kGameSpeedMin : speed + 1;
    return std::to_string(timer_wait_from_game_speed(next));
}

std::string format_game_speed_label(const std::string& value)
{
    return "SPEED: " +
           std::to_string(game_speed_from_timer_wait(parse_timer_wait(value)));
}

// --- DISPLAY brightness (cfg graphics/brightness) ---

// Gamma steps as adjust_palette() consumes them: each step is a 10%
// multiplier plus a flat offset on every palette component. The range is
// clamped because the transform saturates — beyond about +5 every color has
// hit 63 (white) and beyond -5 everything is black, so further steps would
// only give the -/+ pair dead travel.
int parse_brightness_steps(const std::string& value)
{
    const std::optional<int> parsed = parse_int_strict(value);
    if (!parsed)
        return 0;
    return std::clamp(*parsed, kBrightnessStepMin, kBrightnessStepMax);
}

std::string adjust_brightness_steps(const std::string& current, int direction)
{
    const int step = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
    return std::to_string(std::clamp(parse_brightness_steps(current) + step,
                                     kBrightnessStepMin, kBrightnessStepMax));
}

std::string format_brightness_label(int steps)
{
    const int clamped =
        std::clamp(steps, kBrightnessStepMin, kBrightnessStepMax);
    return clamped > 0 ? "Brightness: +" + std::to_string(clamped)
                       : "Brightness: " + std::to_string(clamped);
}

// --- DISPLAY zoom and smoothing selectors ---

// Zoom selector (cfg graphics/zoom): each click zooms out one step and wraps
// at the deepest step that fits the current window's resource budget.
std::string cycle_zoom(const std::string& current, int minimum_steps)
{
    const int steps = og::parse_zoom_steps(current);
    const int minimum = std::clamp(minimum_steps, 1, og::kZoomStepsMax);
    const int next = steps <= minimum ? og::kZoomStepsMax : steps - 1;
    return next == og::kZoomStepsMax ? std::string("1.0")
                                     : "0." + std::to_string(next);
}

std::string format_zoom_label(const std::string& value)
{
    const int steps = og::parse_zoom_steps(value);
    if (steps == og::kZoomStepsMax)
        return "Zoom: 1.0x";
    return "Zoom: 0." + std::to_string(steps) + "x";
}

// Smoothing selector (cfg graphics/smoothing): the world-canvas-only present
// filter. off -> sai -> eagle -> off.
std::string effective_smoothing_setting(const std::string& value,
                                        const std::string& legacy_render)
{
    if (value.empty() && (legacy_render == "sai" || legacy_render == "eagle"))
        return legacy_render;
    return value;
}

std::string cycle_smoothing(const std::string& current)
{
    const og::WorldScaleMode mode = og::parse_smoothing_setting(current);
    if (mode == og::WorldScaleMode::Sai)
        return "eagle";
    if (mode == og::WorldScaleMode::Eagle)
        return "off";
    return "sai";
}

std::string format_smoothing_label(const std::string& value, bool supported)
{
    switch (og::parse_smoothing_setting(value))
    {
    case og::WorldScaleMode::Sai:
        return supported ? "Smooth: SAI" : "Smooth: SAI N/A";
    case og::WorldScaleMode::Eagle:
        return supported ? "Smooth: Eagle" : "Smooth: Eagle N/A";
    default:
        return "Smooth: Off";
    }
}

DisplayMode parse_display_mode(const std::string& value)
{
    // Legacy boolean cfg: "on" meant the borderless desktop fullscreen that
    // SDL_WINDOW_FULLSCREEN(_DESKTOP) provided.
    if (value == "on" || value == "borderless")
        return DisplayMode::Borderless;
    if (value == "exclusive" || value == "fullscreen")
        return DisplayMode::Exclusive;
    return DisplayMode::Windowed; // "off", absent, or anything unrecognized
}

std::string display_mode_cfg_value(DisplayMode mode)
{
    switch (mode)
    {
    case DisplayMode::Borderless:
        return "borderless";
    case DisplayMode::Exclusive:
        return "exclusive";
    case DisplayMode::Windowed:
    default:
        return "off";
    }
}

DisplayMode next_display_mode(DisplayMode mode)
{
    switch (mode)
    {
    case DisplayMode::Windowed:
        return DisplayMode::Borderless;
    case DisplayMode::Borderless:
        return DisplayMode::Exclusive;
    case DisplayMode::Exclusive:
    default:
        return DisplayMode::Windowed;
    }
}

std::string format_display_mode_label(const std::string& value)
{
    switch (parse_display_mode(value))
    {
    case DisplayMode::Borderless:
        return "Mode: Borderless";
    case DisplayMode::Exclusive:
        return "Mode: Fullscreen";
    case DisplayMode::Windowed:
    default:
        return "Mode: Windowed";
    }
}

std::vector<std::pair<int, int>> fallback_resolutions(std::pair<int, int> desktop)
{
    if (desktop.first < 640 || desktop.second < 400)
    {
        // No usable desktop information: the 16:10 multiples of the classic
        // 320x200 canvas.
        return {{640, 400}, {960, 600}, {1280, 800}, {1600, 1000}, {1920, 1200}};
    }
    // The desktop itself plus aspect-preserving fractions of it, so every
    // offered window size matches the user's own display shape.
    std::vector<std::pair<int, int>> out;
    for (const int num : {4, 3, 2})
    {
        // 4/4 (native), 3/4, 2/4 of the desktop, rounded to even.
        const int w = (desktop.first * num / 4) & ~1;
        const int h = (desktop.second * num / 4) & ~1;
        if (w >= 640 && h >= 400)
            out.emplace_back(w, h);
    }
    if (out.empty())
        out.emplace_back(desktop);
    return out;
}

std::vector<std::pair<int, int>> build_resolution_choices(
    const std::vector<std::pair<int, int>>& display_modes,
    std::pair<int, int> desktop,
    std::pair<int, int> current,
    DisplayMode mode)
{
    std::vector<std::pair<int, int>> out = display_modes;
    if (mode != DisplayMode::Exclusive)
    {
        const bool desktop_is_usable =
            desktop.first >= 640 && desktop.second >= 400;
        if (desktop_is_usable &&
            std::find(out.begin(), out.end(), desktop) == out.end())
        {
            out.push_back(desktop);
        }

        if (out.size() < 2)
            out = fallback_resolutions(desktop);

        // A hand-edited/windowed size is useful on the window-size lap.
        if (std::find(out.begin(), out.end(), current) == out.end())
            out.push_back(current);
    }

    std::sort(out.begin(), out.end(), std::greater<>());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::pair<int, int> preferred_exclusive_resolution(
    const std::vector<std::pair<int, int>>& display_modes,
    std::pair<int, int> desktop)
{
    const auto choices = build_resolution_choices(
        display_modes, desktop, {0, 0}, DisplayMode::Exclusive);
    const auto desktop_mode = std::find(choices.begin(), choices.end(), desktop);
    if (desktop.first >= 640 && desktop.second >= 400 &&
        desktop_mode != choices.end())
    {
        return *desktop_mode;
    }
    return choices.empty() ? std::pair<int, int>{0, 0} : choices.front();
}

std::pair<int, int> parse_resolution(const std::string& width, const std::string& height)
{
    const std::optional<int> w = parse_int_strict(width);
    const std::optional<int> h = parse_int_strict(height);
    if (!w || !h || *w < 320 || *h < 200)
        return {640, 400}; // the video_init boot default
    return {*w, *h};
}

std::pair<int, int> next_resolution(const std::vector<std::pair<int, int>>& list,
                                    const std::string& width, const std::string& height)
{
    if (list.empty())
        return parse_resolution(width, height);
    const std::pair<int, int> current = parse_resolution(width, height);
    for (std::size_t i = 0; i < list.size(); ++i)
    {
        if (list[i] == current)
            return list[(i + 1) % list.size()];
    }
    return list.front(); // hand-edited size re-enters the list
}

std::string format_resolution_label(const std::string& width, const std::string& height)
{
    const std::pair<int, int> r = parse_resolution(width, height);
    return "Res: " + std::to_string(r.first) + "x" + std::to_string(r.second);
}

// --- Team choice helpers (local seats) ---

bool team_has_members(const SaveData& save, short team)
{
    for (const auto& member : save.team_list)
    {
        if (member && member->teamnum == team)
            return true;
    }
    return false;
}

bool set_preferred_team(SaveData& save, short team)
{
    if (team < 0 || team >= MAX_PLAYERS)
        return false;
    if (!team_has_members(save, team))
        return false;
    save.my_team = team;
    return true;
}

bool set_guy_team(SaveData& save, int slot_index, short team)
{
    if (team < 0 || team >= 4)
        return false;
    if (slot_index < 0 || slot_index >= MAX_TEAM_SIZE)
        return false;
    guy* member = save.team_list[static_cast<std::size_t>(slot_index)].get();
    if (member == nullptr)
        return false;
    member->teamnum = team;
    return true;
}

short cycle_guy_team(SaveData& save, int slot_index, int dir)
{
    if (slot_index < 0 || slot_index >= MAX_TEAM_SIZE)
        return -1;
    const guy* member = save.team_list[static_cast<std::size_t>(slot_index)].get();
    if (member == nullptr)
        return -1;
    // The wrap is this helper's own rule; the WRITE is the one setter every
    // team-assignment surface shares (docs/lineup-design.md §5).
    const int next = ((member->teamnum + dir) % 4 + 4) % 4;
    if (!set_guy_team(save, slot_index, static_cast<short>(next)))
        return -1;
    return static_cast<short>(next);
}

std::vector<short> derive_local_seat_teams(const SaveData& save)
{
    std::vector<short> teams;
    teams.reserve(MAX_PLAYERS);
    for (const auto& member : save.team_list)
    {
        if (!member || !member->deployed)
            continue;
        const short team = member->teamnum;
        if (team <= 0 || team >= MAX_PLAYERS)
            continue;
        if (std::find(teams.begin(), teams.end(), team) == teams.end())
            teams.push_back(team);
    }

    const short preferred = save.my_team;
    const bool preferred_has_deployed = std::any_of(
        save.team_list.begin(), save.team_list.end(),
        [preferred](const std::unique_ptr<guy>& member) {
            return member != nullptr && member->deployed &&
                member->teamnum == preferred;
        });
    if (preferred >= 0 && preferred < MAX_PLAYERS && preferred_has_deployed)
    {
        teams.erase(std::remove(teams.begin(), teams.end(), preferred),
                    teams.end());
        teams.insert(teams.begin(), preferred);
    }
    return teams;
}

std::vector<short> derive_local_gameplay_seat_teams(const SaveData& save)
{
    const int required_players = std::clamp<int>(save.numplayers, 0, MAX_PLAYERS);
    if (required_players == 0)
        return {};

    std::vector<short> teams = derive_local_seat_teams(save);
    if (static_cast<int>(teams.size()) > required_players)
        teams.resize(static_cast<std::size_t>(required_players));

    for (short candidate = 0;
         static_cast<int>(teams.size()) < required_players &&
             candidate < MAX_PLAYERS;
         ++candidate)
    {
        if (std::find(teams.begin(), teams.end(), candidate) == teams.end())
            teams.push_back(candidate);
    }

    if (teams.empty())
        teams.push_back(0);
    if (save.allied_mode != 0)
        teams.assign(static_cast<std::size_t>(required_players), teams.front());
    return teams;
}

int first_local_seat_team(const SaveData& save)
{
    const std::vector<short> teams = derive_local_gameplay_seat_teams(save);
    if (teams.empty())
        return og::data::campaign_my_team_fallback(save);
    return teams.front();
}

bool local_seat_teams_have_controls(const SaveData& save,
                                    std::span<const short> seat_teams)
{
    std::array<int, MAX_PLAYERS> available{};
    for (const auto& member : save.team_list)
    {
        if (member == nullptr || !member->deployed || member->teamnum < 0 ||
            member->teamnum >= MAX_PLAYERS)
        {
            continue;
        }
        ++available[static_cast<std::size_t>(member->teamnum)];
    }

    for (const short team : seat_teams)
    {
        if (team < 0 || team >= MAX_PLAYERS)
            return false;
        int& remaining = available[static_cast<std::size_t>(team)];
        if (remaining <= 0)
            return false;
        --remaining;
    }
    return true;
}

og::data::CompanyAutosaveContext company_autosave_context(
    const SaveData& save, bool networked_lobby_active)
{
    og::data::CompanyAutosaveContext context;
    context.networked_lobby = networked_lobby_active;
    if (!networked_lobby_active)
        return context;

    // Owned wallets = every m_totalcash index this machine's mutations spend
    // from. In a networked lobby the in-memory roster remains private to this
    // machine and each in-range teamnum remains a combat/wallet color;
    // my_team covers hiring into an empty roster. Team 0 is a real wallet
    // index here — unlike derive_local_seat_teams, which is about SEATS.
    if (save.my_team >= 0 && save.my_team < SCORE_TEAM_COUNT)
        context.owned_teams[static_cast<std::size_t>(save.my_team)] = true;
    for (const auto& member : save.team_list)
    {
        if (!member)
            continue;
        const short team = member->teamnum;
        if (team >= 0 && team < SCORE_TEAM_COUNT)
            context.owned_teams[static_cast<std::size_t>(team)] = true;
    }
    return context;
}

SaveDataIoError company_autosave_after_mutation(SaveData& save,
                                                bool networked_lobby_active,
                                                int additional_owned_team)
{
    og::data::CompanyAutosaveContext context =
        company_autosave_context(save, networked_lobby_active);
    if (networked_lobby_active && additional_owned_team >= 0 &&
        additional_owned_team < SCORE_TEAM_COUNT)
    {
        context.owned_teams[static_cast<std::size_t>(
            additional_owned_team)] = true;
    }
    return og::data::company_autosave(
        save,
        og::data::CompanyAutosaveKind::BaseCampMutation,
        context);
}

std::vector<int> collect_base_camp_slots(const SaveData& save)
{
    std::vector<int> slots;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (save.team_list[static_cast<std::size_t>(i)])
            slots.push_back(i);
    }
    return slots;
}

int count_deployed_members(const SaveData& save)
{
    int deployed = 0;
    for (const auto& member : save.team_list) {
        if (member && member->deployed)
            ++deployed;
    }
    return deployed;
}

bool toggle_deploy_slot(SaveData& save, int slot)
{
    if (slot < 0 || slot >= MAX_TEAM_SIZE || !save.team_list[static_cast<std::size_t>(slot)])
        return false;
    guy& member = *save.team_list[static_cast<std::size_t>(slot)];
    member.deployed = !member.deployed;
    return member.deployed;
}

int move_team_member_up(SaveData& save, int slot)
{
    if (slot <= 0 || slot >= MAX_TEAM_SIZE || !save.team_list[static_cast<std::size_t>(slot)])
        return -1;

    for (int previous = slot - 1; previous >= 0; --previous) {
        if (!save.team_list[static_cast<std::size_t>(previous)])
            continue;
        std::swap(save.team_list[static_cast<std::size_t>(previous)], save.team_list[static_cast<std::size_t>(slot)]);
        return previous;
    }
    return -1;
}

namespace {

std::string clip_chars(std::string value, std::size_t max_chars)
{
    if (value.size() > max_chars)
        value.resize(max_chars);
    return value;
}

} // namespace

std::string clip_with_ellipsis(std::string value, std::size_t max_chars)
{
    if (value.size() <= max_chars)
        return value;
    constexpr std::size_t kMarker = 2;  // ".."
    if (max_chars <= kMarker + 1)
        return clip_chars(std::move(value), max_chars);
    const std::size_t body = max_chars - kMarker;
    // Prefer a whole-word cut, but only while it keeps two thirds of the
    // room: "SOUTH OF TALWOOD FOREST" in 17 would otherwise collapse to
    // "SOUTH OF..", throwing away a word that nearly fits. Below that
    // threshold the marker alone carries the honesty and the cut goes
    // mid-word ("SOUTH OF TALWOO..").
    const std::size_t space = value.find_last_of(' ', body);
    if (space != std::string::npos && space * 3 >= body * 2)
        value.resize(space);
    else
        value.resize(body);
    while (!value.empty() && value.back() == ' ')
        value.pop_back();
    return value + "..";
}

BaseCampRowText format_base_camp_row(const guy& member)
{
    BaseCampRowText row;
    row.name = clip_chars(member.name, 12);
    std::string cls = family_display_name(member.family);
    for (char& c : cls)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    row.cls = clip_chars(std::move(cls), 9);
    // §9.9 graft (b): fixed-width left-padded numerics so the digit columns
    // right-align down the page (the clip budgets are unchanged).
    row.level = clip_chars(std::format("{:>2}", member.level), 3);
    row.exp = clip_chars(std::format("{:>6}", member.exp), 6);
    return row;
}

unsigned char base_camp_family_ramp_start(short family)
{
    // The exact View Team formula from master. Each result begins the
    // family's 16-entry palette ramp.
    return static_cast<unsigned char>(((family + 1) << 4) & 255);
}

std::string format_base_camp_gold_label(const SaveData& save)
{
    // The lobby may remap my_team to a shared seat/control team without
    // changing this company's combat colors or wallets. Prefer the first
    // wallet actually represented by the private
    // roster; only an empty company falls back to the seat team.
    int team = MAX_PLAYERS;
    for (const auto& member : save.team_list)
    {
        if (member != nullptr && member->teamnum >= 0 &&
            member->teamnum < MAX_PLAYERS)
        {
            team = std::min(team, static_cast<int>(member->teamnum));
        }
    }
    if (team == MAX_PLAYERS)
    {
        team = (save.my_team >= 0 && save.my_team < MAX_PLAYERS)
            ? save.my_team
            : 0;
    }
    return clip_chars(
        std::format("GOLD {}", format_wallet_amount(save, team)), 11);
}

std::string format_base_camp_scen_line(const SaveData& save,
                                       std::string_view level_title)
{
    const std::string dep_part =
        std::format("  DEP {}/{}", count_deployed_members(save),
                    static_cast<int>(collect_base_camp_slots(save).size()));
    const std::string prefix = std::format("SCEN {}: ", save.scen_num);
    // The conservative line-B budget: the solo header renders under every
    // composition, including the ones that show HIRE beside it.
    const std::size_t budget =
        static_cast<std::size_t>(kBaseCampLineBCharsHireVisible);
    const std::size_t scen_budget =
        dep_part.size() < budget ? budget - dep_part.size() : 0;
    // The level's NAME is the one piece of story on this screen, so the
    // title takes the ellipsis cut ("SCEN 1: THE RASPBERRY..") instead of
    // stopping mid-word; the SCEN id itself never loses digits.
    if (prefix.size() >= scen_budget)
        return clip_chars(prefix, scen_budget) + dep_part;
    return prefix +
        clip_with_ellipsis(std::string(level_title),
                           scen_budget - prefix.size()) +
        dep_part;
}

std::vector<BaseCampDisplaySlot> collect_base_camp_display_slots(
    const SaveData& save,
    const std::vector<og::sim::LobbyPlayer>& players,
    const std::vector<std::uint8_t>& local_player_indices,
    bool networked)
{
    std::vector<BaseCampDisplaySlot> slots;

    // Own rows first (§2.5 MP roster rule), read from the PRIVATE save so
    // toggles/train edits show the same frame. Solo: this is the whole list.
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (!save.team_list[static_cast<std::size_t>(i)])
            continue;
        BaseCampDisplaySlot slot;
        slot.owned = true;
        slot.save_slot = i;
        slot.deployed = save.team_list[static_cast<std::size_t>(i)]->deployed;
        if (networked)
            slot.company = save.save_name;
        slots.push_back(std::move(slot));
    }
    if (!networked)
        return slots;

    // Foreign rows: every other machine's replicated slots, ordered by
    // owner player index then declared slot order (stable across polls).
    std::vector<const og::sim::LobbyPlayer*> foreign;
    for (const og::sim::LobbyPlayer& player : players) {
        if (std::find(local_player_indices.begin(), local_player_indices.end(),
                      player.player_index) != local_player_indices.end())
        {
            continue;
        }
        foreign.push_back(&player);
    }
    std::stable_sort(foreign.begin(), foreign.end(),
                     [](const og::sim::LobbyPlayer* a,
                        const og::sim::LobbyPlayer* b) {
                         return a->player_index < b->player_index;
                     });
    for (const og::sim::LobbyPlayer* player : foreign) {
        for (const og::sim::LobbyCharacterSlot& wire_slot :
             player->character_slots)
        {
            BaseCampDisplaySlot slot;
            slot.owned = false;
            slot.owner_player_index = player->player_index;
            slot.deployed = wire_slot.deployed;
            slot.company = player->company;
            slot.character = wire_slot.character;
            slots.push_back(std::move(slot));
        }
    }
    return slots;
}

BaseCampDeployCounts count_base_camp_display_deploys(
    const std::vector<BaseCampDisplaySlot>& slots, const SaveData& save)
{
    BaseCampDeployCounts counts;
    for (const BaseCampDisplaySlot& slot : slots) {
        ++counts.total;
        bool deployed = slot.deployed;
        if (slot.owned && slot.save_slot >= 0 &&
            slot.save_slot < MAX_TEAM_SIZE &&
            save.team_list[static_cast<std::size_t>(slot.save_slot)])
        {
            deployed = save.team_list[static_cast<std::size_t>(slot.save_slot)]->deployed;
        }
        if (deployed)
            ++counts.deployed;
    }
    return counts;
}

namespace {

std::uint64_t lobby_machine_group_key(
    const og::sim::LobbyPlayer& player,
    std::size_t ordinal) noexcept
{
    if (player.machine_id != og::sim::kInvalidLobbyMachineId)
        return player.machine_id;
    // A missing authority grant must never make unrelated seats look like
    // one machine. This path exists for defensive/test-shaped states only;
    // v9 servers stamp every accepted seat.
    return (std::uint64_t{1} << 32) | ordinal;
}

} // namespace

BaseCampReadyCounts count_base_camp_ready_machines(
    const std::vector<og::sim::LobbyPlayer>& players)
{
    struct MachineState {
        bool is_host = false;
        bool all_ready = true;
    };
    std::map<std::uint64_t, MachineState> machines;
    for (std::size_t index = 0; index < players.size(); ++index) {
        const og::sim::LobbyPlayer& player = players[index];
        MachineState& machine =
            machines[lobby_machine_group_key(player, index)];
        machine.is_host = machine.is_host || player.is_host;
        machine.all_ready = machine.all_ready && player.ready;
    }
    BaseCampReadyCounts counts;
    for (const auto& [key, machine] : machines) {
        (void)key;
        if (machine.is_host)
            continue;
        ++counts.machines;
        if (machine.all_ready)
            ++counts.ready;
    }
    return counts;
}

BaseCampSessionCensus count_base_camp_session_census(
    const std::vector<og::sim::LobbyPlayer>& players)
{
    std::set<std::uint64_t> machines;
    for (std::size_t index = 0; index < players.size(); ++index)
        machines.insert(lobby_machine_group_key(players[index], index));
    BaseCampSessionCensus census;
    census.machines = static_cast<int>(machines.size());
    census.players = static_cast<int>(players.size());
    return census;
}

std::string format_base_camp_session_status(
    bool is_host,
    std::string_view room_code,
    const std::vector<og::sim::LobbyPlayer>& players,
    int max_chars)
{
    const std::size_t budget =
        max_chars > 0 ? static_cast<std::size_t>(max_chars) : 0u;
    // Room codes display-clip at 12 (relay codes are 9-char "GLAD-XXXX";
    // the NETWORKING screen keeps the authoritative full form).
    const std::string room = clip_chars(std::string(room_code), 12);
    const BaseCampSessionCensus census =
        count_base_camp_session_census(players);
    // PLAYERS FIRST. The rail shows this machine's seats only, so the count
    // of everyone in the lobby has nowhere else to live; machines are the
    // second-order fact (how many rooms the noise is coming from). The
    // joiner's line drops "HOST: <company>" — the host's company already
    // sits on every one of its roster rows, and the census does not.
    const std::string head = is_host ? "HOSTING" : "JOINED";
    const std::string room_part = room.empty()
        ? head
        : std::format("{} {}", is_host ? "HOSTING" : "IN", room);
    // One is one. A solo host reading "1 PLAYERS / 1 MACHINES" is the line
    // telling him it cannot count, and the singular is always the SHORTER
    // spelling — it can never push a rung off its band.
    const auto counted = [](int value, std::string_view one,
                            std::string_view many) {
        return std::format("{} {}", value, value == 1 ? one : many);
    };
    // The ladder, widest first: each rung is a whole spelling that either
    // fits or falls to the next — never a byte cut of the one above.
    // "PCS" is the everyday word for the thing "MACHINES" names precisely;
    // at 1-digit plural counts the two rungs land exactly on the 41 and 34
    // bands, and the singular forms sit a word inside them.
    const std::array<std::string, 3> rungs{
        std::format("{} / {}", counted(census.players, "PLAYER", "PLAYERS"),
                    counted(census.machines, "MACHINE", "MACHINES")),
        std::format("{}/{}", counted(census.players, "PLAYER", "PLAYERS"),
                    counted(census.machines, "PC", "PCS")),
        std::format("{}P/{}M", census.players, census.machines)};
    for (const std::string& rung : rungs) {
        std::string status = std::format("{}: {}", room_part, rung);
        if (status.size() <= budget)
            return status;
    }
    // A pathological room code can still over-run: the census is the half
    // that changes, so the room code (authoritative on the NETWORKING
    // screen) is the half that goes.
    return clip_chars(std::format("{}: {}", head, rungs[2]), budget);
}

BaseCampLineB compose_base_camp_line_b(
    const std::optional<std::string>& alert,
    bool is_host,
    std::string_view room_code,
    const std::vector<og::sim::LobbyPlayer>& players,
    int max_chars)
{
    // Degraded links outrank the healthy session status: the alert takes
    // the §2.5 line-B slot (and the ORANGE color) until the link heals.
    // Alert text is transport prose (a pack-install failure can be long),
    // so it takes the same band budget.
    if (alert.has_value()) {
        return {.text = clip_chars(*alert,
                                   max_chars > 0
                                       ? static_cast<std::size_t>(max_chars)
                                       : 0u),
                .alert = true};
    }
    return {.text = format_base_camp_session_status(is_host, room_code,
                                                    players, max_chars),
            .alert = false};
}

ReadyGoPresentation format_ready_go_button(bool networked,
                                           bool is_host,
                                           bool my_ready,
                                           bool all_other_machines_ready,
                                           int global_deployed,
                                           int own_deployed,
                                           bool cross_control,
                                           bool spectator)
{
    ReadyGoPresentation p;
    if (!networked) {
        // States 1-2: solo/local multi never consults ready (the local
        // LobbyServer is exempt) and keeps the plain grey face byte-identical.
        p.state = global_deployed > 0 ? ReadyGoState::LocalGo
                                      : ReadyGoState::LocalGoNoDeploy;
        p.label = "GO";
        p.face_color = kReadyGoFaceGrey;
        if (p.state == ReadyGoState::LocalGoNoDeploy)
            p.caption = "DEPLOY AT LEAST ONE";
        return p;
    }
    if (is_host) {
        // States 3-4. Rule 3 outranks rule 4 (the server's start_allowed
        // order): report unready machines before the global-deploy gate.
        p.label = "GO";
        if (!all_other_machines_ready) {
            p.state = ReadyGoState::HostGated;
            p.face_color = kReadyGoFaceGated;
            p.caption = "WAITING FOR OTHERS";
        } else if (global_deployed <= 0) {
            p.state = ReadyGoState::HostGated;
            p.face_color = kReadyGoFaceGated;
            p.caption = "NO ONE IS DEPLOYED";
        } else {
            p.state = ReadyGoState::HostGo;
            p.face_color = kReadyGoFaceGo;
        }
        return p;
    }
    // States 5-6 (joiner). Label = the action, color = the state.
    if (my_ready) {
        p.state = ReadyGoState::ClientReady;
        p.label = "UNREADY";
        p.face_color = kReadyGoFaceGo;
        return p;
    }
    p.state = ReadyGoState::ClientUnready;
    p.label = "READY";
    p.face_color = kReadyGoFaceUnready;
    // Client ready gate: cross-control OFF + own roster brought characters
    // + none deployed => the click popups instead of readying. The spectator
    // formatter shape and active seats with empty rosters have no deploy
    // minimum [NET-R9]; Base Camp separately hides READY for a true zero-seat
    // client, which is exempt from the server gate. Cross-control ON also
    // removes the minimum (bring 0, play a friend's characters).
    if (!cross_control && !spectator && own_deployed <= 0)
        p.caption = "DEPLOY AT LEAST ONE";
    return p;
}

std::string format_go_blockers(
    const std::vector<og::sim::LobbyPlayer>& players)
{
    struct MachineState {
        bool is_host = false;
        bool all_ready = true;
        std::string company;
        std::string display_name;
    };
    // Insertion order keeps the popup stable across identical lobby states.
    std::vector<std::pair<std::uint64_t, MachineState>> machines;
    for (std::size_t index = 0; index < players.size(); ++index) {
        const og::sim::LobbyPlayer& player = players[index];
        const std::uint64_t key = lobby_machine_group_key(player, index);
        auto it = std::find_if(machines.begin(), machines.end(),
                               [&key](const auto& entry) {
                                   return entry.first == key;
                               });
        if (it == machines.end())
            it = machines.insert(machines.end(), {key, MachineState{}});
        it->second.is_host = it->second.is_host || player.is_host;
        it->second.all_ready = it->second.all_ready && player.ready;
        if (it->second.company.empty())
            it->second.company = player.company;
        if (it->second.display_name.empty())
            it->second.display_name = player.name;
    }
    std::vector<std::string> blockers;
    for (const auto& [key, machine] : machines) {
        (void)key;
        if (machine.is_host || machine.all_ready)
            continue;
        blockers.push_back(clip_chars(
            machine.company.empty() ? machine.display_name : machine.company,
            26));
    }
    std::string body;
    constexpr std::size_t kMaxLines = 4;
    for (std::size_t i = 0; i < blockers.size() && i < kMaxLines; ++i) {
        if (!body.empty())
            body += '\n';
        body += blockers[i];
    }
    if (blockers.size() > kMaxLines)
        body += std::format("\nAND {} MORE", blockers.size() - kMaxLines);
    return body;
}

std::string format_cross_control_label(bool cross_control_enabled)
{
    return cross_control_enabled ? "CTRL: ALL" : "CTRL: OWN";
}

BaseCampNetRowText format_base_camp_net_row(std::string_view name,
                                            std::string_view company,
                                            int level)
{
    BaseCampNetRowText row;
    row.name = clip_chars(std::string(name), 10);
    row.company = clip_chars(std::string(company), 16);
    // §9.9 graft (b): the level left-pads to 2 like the solo shape.
    row.level = clip_chars(std::format("{:>2}", level), 3);
    return row;
}

std::unique_ptr<guy> make_base_camp_display_guy(
    const og::sim::LobbyCharacterData& character)
{
    auto result = std::make_unique<guy>(character.family);
    result->name = character.name;
    result->family = static_cast<char>(character.family);
    result->strength = character.strength;
    result->dexterity = character.dexterity;
    result->constitution = character.constitution;
    result->intelligence = character.intelligence;
    result->armor = character.armor;
    result->exp = character.exp;
    result->level = character.level;
    result->teamnum = character.teamnum;
    return result;
}

std::string format_team_row_label(short team,
                                  int hero_count,
                                  bool is_ctf,
                                  bool authored,
                                  bool has_humans,
                                  std::string_view seat_tag)
{
    std::string label = std::format("{} TEAM", og::sim::team_color_name(team));
    if (!seat_tag.empty())
    {
        label += ' ';
        label += seat_tag;
    }
    label += ' ';
    if (is_ctf && !authored)
        label += "NOT ON MAP";
    else if (is_ctf && authored && !has_humans && hero_count == 0)
        label += "BOTS";
    else
        label += std::format("{} HEROES", hero_count);
    return label;
}

// --- Campaign ordering ---

void order_campaigns_for_select(std::list<std::string>& campaign_ids)
{
    // The shipped shelf order: the classics lead (gladiator, then tryxian),
    // the two original story campaigns follow, then the multiplayer
    // packages, then the tower (single-player mode package, owner-placed
    // after the MP block), with the concept playground trailing. Campaigns
    // not on the shelf (user-made packages) keep their incoming enumeration
    // order and follow every shelved id.
    static constexpr std::string_view kShelf[] = {
        og::kDefaultCampaignId, // gladiator
        "tryxian",
        "westlands",
        "longseason",
        "modes",   // Multiplayer Game Modes (versus)
        og::kTowerCampaignId,   // tower (The Endless Tower)
        "imaginations", // the community dream-log (kid-submitted levels)
        "concept",
    };
    auto anchor = campaign_ids.begin();
    for (const std::string_view id : kShelf)
    {
        const auto it = std::find(anchor, campaign_ids.end(), id);
        if (it == campaign_ids.end())
            continue;
        if (it == anchor)
        {
            ++anchor;
            continue;
        }
        campaign_ids.splice(anchor, campaign_ids, it);
    }
}

void filter_campaigns_for_networked_lobby(std::list<std::string>& campaign_ids,
                                          bool networked_session)
{
    if (!networked_session)
        return; // local shelves keep every campaign (tower is local-only)
    // v1 keys on the tower id directly; the
    // documented upgrade is a yaml-driven campaign_mode(id) accessor once a
    // second mode campaign exists. The prepare_launch veto and the
    // LobbyServer sanitize backstop enforce the same rule below the UI.
    campaign_ids.remove_if([](const std::string& id)
                           { return id == og::kTowerCampaignId; });
}

std::vector<std::string> format_campaign_select_labels(
    const std::vector<std::string>& campaign_ids)
{
    std::map<std::string, int> title_counts;
    for (const std::string& id : campaign_ids)
        ++title_counts[og::data::campaign_display_title(id)];

    std::vector<std::string> labels;
    labels.reserve(campaign_ids.size());
    for (const std::string& id : campaign_ids)
    {
        std::string label = og::data::campaign_display_title(id);
        // A fallback label already IS the id — appending it again would
        // just stutter.
        if (title_counts[label] > 1 && label != id)
            label += " [" + id + "]";
        labels.push_back(std::move(label));
    }
    return labels;
}

bool sync_campaign_mount_to_save(const SaveData& save)
{
    const std::string mounted = get_mounted_campaign();
    if (mounted == save.current_campaign)
        return true;
    if (mount_campaign_package_with_error(save.current_campaign) ==
        CampaignPackageIoError::None)
        return true;
    // Missing/corrupt package (a joiner can legitimately lack the host's
    // campaign): a failed mount leaves NOTHING mounted, so put the previous
    // package back to keep some level data loadable.
    (void)mount_campaign_package_with_error(mounted);
    return false;
}

// --- Player count ---

void set_player_count(SaveData& save, int count)
{
    save.numplayers = static_cast<unsigned char>(count);
}

bool is_spectator_mode(const SaveData& save)
{
    return save.numplayers == 0;
}

// --- Label formatting ---

std::string format_difficulty_label(int difficulty)
{
    const int normalized =
        ((difficulty % DIFFICULTY_SETTINGS) + DIFFICULTY_SETTINGS) % DIFFICULTY_SETTINGS;
    return std::format("Difficulty: {}", kDifficultyNames[normalized]);
}

std::string format_allied_mode_label(const SaveData& save)
{
    return is_allied_mode(save) ? "SEATS: TOGETHER" : "SEATS: SPLIT";
}

std::string format_ctf_score_label(const SaveData& save)
{
    // 80px face = 12 chars; "SCORE: MAP" is 10, "SCORE: 10" 9.
    if (save.ctf_capture_limit <= 0)
        return "SCORE: MAP";
    return std::format("SCORE: {}", save.ctf_capture_limit);
}

std::string format_respawn_mode_label(const SaveData& save)
{
    if (save.respawn_mode <= og::sim::kRespawnModeOff)
        return "Respawns: Off";
    if (save.respawn_mode == og::sim::kRespawnModeHeroes)
        return "Respawns: Heroes";
    if (save.respawn_mode == og::sim::kRespawnModeTeamOneHeroes)
        return "Respawns: Team 1 Heroes";
    return "Respawns: Everyone";
}

std::string format_respawn_delay_label(const SaveData& save)
{
    // Anything outside the cycle set (including the 0 map-default sentinel)
    // reads as the default delay.
    if (save.ctf_respawn_ticks == 60)
        return "Spawn Delay: Fast";
    if (save.ctf_respawn_ticks == 360)
        return "Spawn Delay: Slow";
    return "Spawn Delay: Normal";
}

std::string format_permadeath_label(const SaveData& save)
{
    return save.keep_fallen_heroes != 0 ? "Permadeath: Off" : "Permadeath: On";
}

std::string format_generator_rate_label(const SaveData& save)
{
    if (save.generator_rate == 50)
        return "Generators: Calm";
    if (save.generator_rate == 200)
        return "Generators: Frenzy";
    return "Generators: Normal";
}

std::string format_infinite_gold_label(const SaveData& save)
{
    return gold_is_infinite(save) ? "Infinite Gold: On" : "Infinite Gold: Off";
}

// --- Company screens: label formatters (design §2.2/§2.3) ---

namespace {

std::string clip_to(std::string text, std::size_t max_len)
{
    if (text.size() > max_len)
        text.resize(max_len);
    return text;
}

} // namespace

// "YYYY-MM-DD" in UTC (deterministic — never the machine's timezone), or ""
// for never-played (<= 0) and out-of-calendar values (a 4-digit year keeps
// the §2.3 10-char date column budget honest). Public: the cloud-save
// confirm prompts (#155) reuse the same date rendering.
std::string format_played_date_utc(std::int64_t unix_s)
{
    // 9999-12-31T23:59:59Z. Values beyond this would overflow the int day
    // count inside year_month_day (wrapping to a bogus in-range date), so
    // bound the input BEFORE converting.
    constexpr std::int64_t kMaxFourDigitYearUnixS = 253402300799;
    if (unix_s <= 0 || unix_s > kMaxFourDigitYearUnixS)
        return {};
    const std::chrono::sys_seconds when{std::chrono::seconds{unix_s}};
    const std::chrono::year_month_day ymd{
        std::chrono::floor<std::chrono::days>(when)};
    if (!ymd.ok())
        return {};
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(ymd.year()),
                       static_cast<unsigned>(ymd.month()),
                       static_cast<unsigned>(ymd.day()));
}

std::string format_cloud_passphrase_status(bool key_set)
{
    return key_set ? "PASSPHRASE: SET" : "PASSPHRASE: NOT SET";
}

std::string format_company_list_title(int count)
{
    return std::format("COMPANIES ({})", count);
}

CompanyRowText format_company_row(const og::data::CompanyInfo& info)
{
    CompanyRowText row;
    row.corrupt = !info.valid;
    // Corrupt headers may still have parsed a display name before failing;
    // fall back to the slot so the row always identifies its file.
    row.name = clip_to(
        !info.display_name.empty() ? info.display_name : info.slot,
        kCompanyNameMaxLen);
    if (row.corrupt)
    {
        row.roster = "--";
        row.played = "CORRUPT";
        return row;
    }
    row.roster = std::format("{:2}", std::clamp(info.roster_size, 0, 99));
    row.played = format_played_date_utc(info.last_played_unix_s);
    return row;
}

std::string format_company_row_line(const og::data::CompanyInfo& info,
                                    bool active)
{
    const CompanyRowText row = format_company_row(info);
    std::string line = std::format("{} {:<18} {:>2}", active ? '*' : ' ',
                                   row.name, row.roster);
    if (!row.played.empty()) {
        line += ' ';
        line += row.played;
    }
    return line;
}

// --- Backups sub-view: label formatters (design §2.4) ---

namespace {

// "MM-DD HH:MM" in UTC (deterministic — never the machine's timezone; the
// §2.3 date-column precedent), or "" for unstamped (<= 0) and
// out-of-calendar values.
std::string format_saved_datetime_utc(std::int64_t unix_s)
{
    // 9999-12-31T23:59:59Z, the same bound the §2.3 date formatter applies
    // BEFORE converting (larger values overflow year_month_day's day count).
    constexpr std::int64_t kMaxFourDigitYearUnixS = 253402300799;
    if (unix_s <= 0 || unix_s > kMaxFourDigitYearUnixS)
        return {};
    const std::chrono::sys_seconds when{std::chrono::seconds{unix_s}};
    const auto day = std::chrono::floor<std::chrono::days>(when);
    const std::chrono::year_month_day ymd{day};
    if (!ymd.ok())
        return {};
    const std::chrono::hh_mm_ss<std::chrono::seconds> tod{when - day};
    return std::format("{:02}-{:02} {:02}:{:02}",
                       static_cast<unsigned>(ymd.month()),
                       static_cast<unsigned>(ymd.day()),
                       tod.hours().count(), tod.minutes().count());
}

} // namespace

std::string format_backup_list_title(const std::string& company_name,
                                     int count)
{
    return std::format("BACKUPS: {} ({}/{})",
                       clip_to(company_name, kCompanyNameMaxLen), count,
                       og::data::kCompanyBackupRetention);
}

BackupRowText format_backup_row(const og::data::CompanyBackupInfo& info)
{
    BackupRowText row;
    row.corrupt = !info.header.valid;
    if (row.corrupt)
    {
        // §2.4 corrupt-backup marking: the row stays listed (and clickable —
        // the restore API's step-0 validation is the guard), but identifies
        // itself as damage instead of level context it never parsed.
        row.level = "CORRUPT";
        row.saved = "--";
        return row;
    }
    const int scen = info.header.scen_num;
    row.level = std::format("L{}", scen);
    // Level title off the MOUNTED package only (the level_display_guarded
    // mount-match rule): a mismatched mount would caption the row with
    // another campaign's title.
    if (get_mounted_campaign() == info.header.campaign_id)
    {
        std::string title = og::data::scenario_display_name(scen);
        const std::string prefix = std::format("{}. ", scen);
        if (title.starts_with(prefix))
            title.erase(0, prefix.size());
        // The missing-scenario fallback title is "Level N" — pure noise
        // next to "L<nn>", so the bare tag stands alone instead.
        if (title != std::format("Level {}", scen))
        {
            row.level += ' ';
            row.level += clip_to(std::move(title), 14);
        }
    }
    row.saved = format_saved_datetime_utc(info.header.last_played_unix_s);
    return row;
}

std::string format_backup_row_line(const og::data::CompanyBackupInfo& info)
{
    const BackupRowText row = format_backup_row(info);
    if (row.saved.empty())
        return row.level;
    return std::format("{:<20} {}", row.level, row.saved);
}

const char* company_restore_error_string(og::data::CompanyRestoreError error)
{
    switch (error)
    {
    case og::data::CompanyRestoreError::None:
        return "";
    case og::data::CompanyRestoreError::InvalidBackup:
        return "BACKUP FILE DAMAGED";
    case og::data::CompanyRestoreError::PreRestoreBackupFailed:
        return "COULD NOT BACK UP CURRENT STATE";
    case og::data::CompanyRestoreError::CopyFailed:
        return "COPY FAILED - COMPANY UNCHANGED";
    case og::data::CompanyRestoreError::ReloadFailed:
        return "RELOAD FAILED - REWIND UNDONE";
    case og::data::CompanyRestoreError::RestampFailed:
        return "REWOUND, BUT THE TIMESTAMP WRITE FAILED";
    }
    return "";
}

ContinueResult open_company_slot(SaveData& save, const std::string& slot,
                                 SaveDataIoError* io_error)
{
    // Never SILENTLY switch to a corrupt company (§2.1/§2.3): validate the
    // header first and keep the currently loaded save on damage — only an
    // explicit restore-from-backup or delete acts on a corrupt file.
    const std::optional<og::data::CompanyInfo> header =
        og::data::read_company_header(slot);
    if (!header || !header->valid)
        return ContinueResult::Corrupt;

    const std::string previous = og::data::active_company_slot();
    (void)og::data::set_active_company_slot(slot);
    const SaveDataIoError io = save.load_with_error(slot);
    if (io != SaveDataIoError::None) {
        if (io_error != nullptr)
            *io_error = io;
        // The header validated but the body failed (torn file, missing
        // campaign package). Restore the previous slot and best-effort
        // reload it so autosaves keep targeting the company that is really
        // open instead of writing the old save into the broken slot.
        (void)og::data::set_active_company_slot(previous);
        (void)save.load_with_error(previous);
        return ContinueResult::LoadFailed;
    }
    return ContinueResult::Opened;
}

ContinueResult open_most_recent_company(SaveData& save,
                                        SaveDataIoError* io_error)
{
    // §2.1: CONTINUE opens the most-recent company (WP2 startup selection).
    const std::string slot = og::data::select_startup_company();
    if (slot.empty())
        return ContinueResult::NoCompany;  // CONTINUE is gated hidden anyway.
    return open_company_slot(save, slot, io_error);
}

// --- Team family extraction ---

std::vector<int> collect_team_families(const SaveData& save)
{
    std::vector<int> families;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        if (save.team_list[static_cast<std::size_t>(i)])
            families.push_back(static_cast<int>(save.team_list[static_cast<std::size_t>(i)]->family));
    }
    return families;
}

// --- Team initialization ---

void initialize_starting_team(SaveData& save, const std::vector<int>& families, int team_num)
{
    if (save.team_size > 0)
        return;

    save.m_totalcash[team_num] = kNewGameStartingGold;
    save.totalcash = kNewGameStartingGold;

    ensure_team_populated(save, families, team_num);
}

// --- Save/Load error strings ---

const char* save_error_string(SaveDataIoError error)
{
    switch (error) {
    case SaveDataIoError::None: return "none";
    case SaveDataIoError::OpenReadFailed: return "open_read_failed";
    case SaveDataIoError::OpenWriteFailed: return "open_write_failed";
    case SaveDataIoError::ReadFailed: return "read_failed";
    case SaveDataIoError::WriteFailed: return "write_failed";
    case SaveDataIoError::InvalidHeader: return "invalid_header";
    case SaveDataIoError::UnsupportedVersion: return "unsupported_version";
    case SaveDataIoError::CampaignLoadFailed: return "campaign_load_failed";
    }
    return "unknown";
}

// --- Stats copy utility ---

void statscopy(guy* dest, const guy* source)
{
    dest->family = source->family;
    dest->strength = source->strength;
    dest->dexterity = source->dexterity;
    dest->constitution = source->constitution;
    dest->intelligence = source->intelligence;
    dest->level = source->level;
    dest->armor = source->armor;
    dest->exp = source->exp;
    dest->kills = source->kills;
    dest->level_kills = source->level_kills;
    dest->total_damage = source->total_damage;
    dest->total_hits = source->total_hits;
    dest->total_shots = source->total_shots;
    dest->teamnum = source->teamnum;

    dest->scen_damage = source->scen_damage;
    dest->scen_kills = source->scen_kills;
    dest->scen_damage_taken = source->scen_damage_taken;
    dest->scen_min_hp = source->scen_min_hp;
    dest->scen_shots = source->scen_shots;
    dest->scen_hits = source->scen_hits;

    dest->name = source->name;
}

// --- HireSession ---

HireSession::HireSession(SaveData& save, int team_num)
    : save_(save), team_num_(team_num)
{
    make_recruit();
}

void HireSession::next_family()
{
    current_type_ = (current_type_ + 1) % static_cast<int>(kAllowableGuys.size());
    make_recruit();
}

void HireSession::prev_family()
{
    current_type_ = (current_type_ - 1 + static_cast<int>(kAllowableGuys.size()))
                    % static_cast<int>(kAllowableGuys.size());
    make_recruit();
}

int HireSession::hire()
{
    if (!recruit_ || team_full())
        return -1;

    // team_num_ comes from a save-loaded guy::teamnum and is not clamped on the
    // SDL hire path; clamp at the index sites so a corrupt save cannot drive an
    // out-of-bounds read/write of the 4-element m_totalcash array.
    const int cash_team = std::clamp(team_num_, 0, static_cast<int>(SCORE_TEAM_COUNT) - 1);

    // Infinite gold makes the purchase FREE: the affordability reject (and
    // the cost==0 overflow reject it doubles as) is skipped and the wallet is
    // never written. Nothing is inflated, so turning the setting back off
    // restores exactly the pre-toggle economy — and no company autosave can
    // bake a cheat balance into the .gtl file.
    const bool free_purchase = gold_is_infinite(save_);

    std::uint32_t cost = current_cost();
    if (!free_purchase)
    {
        if (cost == 0 || cost > save_.m_totalcash[cash_team])
            return -1;

        save_.m_totalcash[cash_team] -= cost;
    }

    int newfamily = recruit_->family;
    recruit_->teamnum = static_cast<short>(team_num_);
    // Ensure we have the right exp for our level
    recruit_->exp = calculate_exp(recruit_->level);

    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (!save_.team_list[static_cast<std::size_t>(i)]) {
            // Save a copy of the hired recruit's stats for the next recruit
            auto next = std::make_unique<guy>(newfamily);
            statscopy(next.get(), recruit_.get());
            next->name = get_unique_name(static_cast<unsigned char>(newfamily), save_);

            save_.team_list[static_cast<std::size_t>(i)] = std::move(recruit_);
            save_.team_size++;

            // Next recruit starts with same stats as the one just hired
            recruit_ = std::move(next);

            return i;
        }
    }

    return -1;
}

void HireSession::rename_hired(int slot, const std::string& name)
{
    if (slot >= 0 && slot < MAX_TEAM_SIZE && save_.team_list[static_cast<std::size_t>(slot)])
        save_.team_list[static_cast<std::size_t>(slot)]->name = name;
}

const guy* HireSession::current_recruit() const
{
    return recruit_.get();
}

std::uint32_t HireSession::current_cost() const
{
    if (!recruit_)
        return 0;
    return calculate_hire_cost(*recruit_);
}

int HireSession::family_index() const
{
    return current_type_;
}

int HireSession::team_num() const
{
    return team_num_;
}

bool HireSession::team_full() const
{
    return save_.team_size >= MAX_TEAM_SIZE;
}

void HireSession::make_recruit()
{
    int family = kAllowableGuys[static_cast<std::size_t>(current_type_)];
    recruit_ = create_recruit(family, team_num_, save_);

    // Clamp stats up to family base values
    const auto* fd = get_family_descriptor(family);
    if (fd) {
        if (recruit_->strength < fd->base_stats[StatAxis::Strength])
            recruit_->strength = static_cast<short>(fd->base_stats[StatAxis::Strength]);
        if (recruit_->dexterity < fd->base_stats[StatAxis::Dexterity])
            recruit_->dexterity = static_cast<short>(fd->base_stats[StatAxis::Dexterity]);
        if (recruit_->constitution < fd->base_stats[StatAxis::Constitution])
            recruit_->constitution = static_cast<short>(fd->base_stats[StatAxis::Constitution]);
        if (recruit_->intelligence < fd->base_stats[StatAxis::Intelligence])
            recruit_->intelligence = static_cast<short>(fd->base_stats[StatAxis::Intelligence]);
        if (recruit_->armor < fd->base_stats[StatAxis::Armor])
            recruit_->armor = static_cast<short>(fd->base_stats[StatAxis::Armor]);
        if (recruit_->level < fd->base_stats[StatAxis::Level])
            recruit_->upgrade_to_level(static_cast<short>(fd->base_stats[StatAxis::Level]));
    }
}

// --- TrainSession ---

TrainSession::TrainSession(SaveData& save)
    : save_(save)
{
    if (save_.team_size < 1)
        return;

    // Find first editable team slot. Network lobbies render remote players in
    // the shared save view, but only the local player's slots may be trained.
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (save_.team_list[static_cast<std::size_t>(i)] && picker_lobby_save_slot_editable(i)) {
            edit_slot_ = i;
            break;
        }
    }

    select_current_slot();
}

bool TrainSession::empty() const
{
    return save_.team_size < 1 || !working_ || original_member() == nullptr;
}

void TrainSession::next_member()
{
    if (empty())
        return;

    int start = edit_slot_;
    do {
        edit_slot_++;
        if (edit_slot_ >= MAX_TEAM_SIZE)
            edit_slot_ = 0;
    } while ((!save_.team_list[static_cast<std::size_t>(edit_slot_)] ||
              !picker_lobby_save_slot_editable(edit_slot_)) &&
             edit_slot_ != start);

    select_current_slot();
}

bool TrainSession::seek_slot(int slot)
{
    if (slot < 0 || slot >= MAX_TEAM_SIZE || !save_.team_list[static_cast<std::size_t>(slot)] ||
        !picker_lobby_save_slot_editable(slot))
    {
        return false;
    }
    edit_slot_ = slot;
    select_current_slot();
    return true;
}

void TrainSession::prev_member()
{
    if (empty())
        return;

    int start = edit_slot_;
    do {
        edit_slot_--;
        if (edit_slot_ < 0)
            edit_slot_ = MAX_TEAM_SIZE - 1;
    } while ((!save_.team_list[static_cast<std::size_t>(edit_slot_)] ||
              !picker_lobby_save_slot_editable(edit_slot_)) &&
             edit_slot_ != start);

    select_current_slot();
}

void TrainSession::increase_stat(Stat stat, int amount)
{
    if (!working_)
        return;

    // Self-heal before editing: if the real member was promoted (family
    // changed) since the working copy was snapshotted, edit the fresh
    // post-promotion stats. Editing the stale copy would clamp against
    // cross-family values and "put the old stats back" (issue #133).
    resync_if_promoted();

    const short delta = static_cast<short>(amount);

    if (stat == Stat::Level) {
        if (!stats_increased()) {
            short newlevel = static_cast<short>(static_cast<int>(working_->level) + delta);
            working_->upgrade_to_level(newlevel);
        }
    } else {
        if (!level_increased()) {
            switch (stat) {
            case Stat::Strength:
                working_->strength = static_cast<short>(static_cast<int>(working_->strength) + delta);
                break;
            case Stat::Dexterity:
                working_->dexterity = static_cast<short>(static_cast<int>(working_->dexterity) + delta);
                break;
            case Stat::Constitution:
                working_->constitution = static_cast<short>(static_cast<int>(working_->constitution) + delta);
                break;
            case Stat::Intelligence:
                working_->intelligence = static_cast<short>(static_cast<int>(working_->intelligence) + delta);
                break;
            case Stat::Armor:
                working_->armor = static_cast<short>(static_cast<int>(working_->armor) + delta);
                break;
            default:
                break;
            }
        }
    }

    clamp_working_stats();
}

void TrainSession::decrease_stat(Stat stat, int amount)
{
    if (!working_)
        return;

    // See increase_stat: never edit a working copy whose family went stale.
    resync_if_promoted();

    const short delta = static_cast<short>(amount);

    if (stat == Stat::Level) {
        if (!stats_increased()) {
            short newlevel = static_cast<short>(static_cast<int>(working_->level) - delta);
            const guy* const original = original_member();
            if (original && newlevel > 0 && newlevel >= original->level) {
                working_->upgrade_to_level(newlevel);
                if (working_->level == original->level)
                    working_->exp = original->exp;
            }
        }
    } else {
        if (!level_increased()) {
            switch (stat) {
            case Stat::Strength:
                working_->strength = static_cast<short>(static_cast<int>(working_->strength) - delta);
                break;
            case Stat::Dexterity:
                working_->dexterity = static_cast<short>(static_cast<int>(working_->dexterity) - delta);
                break;
            case Stat::Constitution:
                working_->constitution = static_cast<short>(static_cast<int>(working_->constitution) - delta);
                break;
            case Stat::Intelligence:
                working_->intelligence = static_cast<short>(static_cast<int>(working_->intelligence) - delta);
                break;
            case Stat::Armor:
                working_->armor = static_cast<short>(static_cast<int>(working_->armor) - delta);
                break;
            default:
                break;
            }
        }
    }

    clamp_working_stats();
}

void TrainSession::set_team(int team_num)
{
    if (!working_)
        return;

    working_->teamnum =
        static_cast<short>(std::clamp(team_num, 0, static_cast<int>(SCORE_TEAM_COUNT) - 1));
}

bool TrainSession::accept(bool force)
{
    // A family mismatch always means an external promotion (training never
    // edits family). Never statscopy stale cross-family stats over the
    // promoted member (bug A9 / issue #133) — resync first, which discards
    // pending edits and turns this into a no-op accept of the promotion.
    resync_if_promoted();

    guy* const original = original_member();
    if (!working_ || !original)
        return false;

    std::uint32_t cost = current_cost();

    // Infinite gold is the same "free purchase" path the right-mouse cheat
    // already takes: no affordability check, no cost==0 overflow revert, and
    // the wallet is never written.
    const bool free_purchase = force || gold_is_infinite(save_);

    if (!free_purchase) {
        // If cost is 0 but stats changed, that's a cost overflow — reject and revert
        if (cost == 0 && (working_->strength != original->strength ||
                          working_->dexterity != original->dexterity ||
                          working_->constitution != original->constitution ||
                          working_->intelligence != original->intelligence ||
                          working_->armor != original->armor ||
                          working_->level != original->level)) {
            statscopy(working_.get(), original);
            return false;
        }

        // working_->teamnum is copied verbatim from a save-loaded guy and is not
        // clamped on the accept() path; clamp at the index sites so a corrupt
        // save cannot drive an out-of-bounds read/write of m_totalcash[4].
        const int cash_team = std::clamp(static_cast<int>(working_->teamnum), 0,
                                         static_cast<int>(SCORE_TEAM_COUNT) - 1);
        if (cost > save_.m_totalcash[cash_team])
            return false;

        save_.m_totalcash[cash_team] -= cost;
    }

    if (original->level != working_->level)
        working_->upgrade_to_level(working_->level);

    statscopy(original, working_.get());

    return true;
}

TrainSession::SellResult TrainSession::sell_current(
    const std::function<bool()>& checkpoint)
{
    const guy* const member = original_member();
    if (!working_ || !member)
        return SellResult::NoMember;

    const int sold_slot = edit_slot_;
    const int cash_team = std::clamp(
        static_cast<int>(member->teamnum), 0,
        static_cast<int>(SCORE_TEAM_COUNT) - 1);
    const std::uint32_t payout = calculate_sell_value(*member);

    // The callback is intentionally the final fallible operation before the
    // wallet/roster mutation. A sale never proceeds without a durable way
    // back to the exact pre-sale company.
    if (!checkpoint || !checkpoint())
        return SellResult::CheckpointFailed;

    const std::uint64_t cash_after =
        static_cast<std::uint64_t>(save_.m_totalcash[cash_team]) + payout;
    save_.m_totalcash[cash_team] = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(
            cash_after, std::numeric_limits<std::uint32_t>::max()));

    // SaveData serialization requires a dense [0, team_size) roster. Remove
    // the selected member and stable-compact every survivor, including
    // defensive recovery from a sparse in-memory roster.
    int write_slot = 0;
    int preferred_slot = 0;
    for (int read_slot = 0; read_slot < MAX_TEAM_SIZE; ++read_slot)
    {
        if (!save_.team_list[static_cast<std::size_t>(read_slot)])
            continue;
        if (read_slot == sold_slot)
        {
            preferred_slot = write_slot;
            continue;
        }
        if (write_slot != read_slot)
            save_.team_list[static_cast<std::size_t>(write_slot)] =
                std::move(save_.team_list[static_cast<std::size_t>(read_slot)]);
        ++write_slot;
    }
    for (int slot = write_slot; slot < MAX_TEAM_SIZE; ++slot)
        save_.team_list[static_cast<std::size_t>(slot)].reset();
    save_.team_size = static_cast<unsigned char>(write_slot);

    if (write_slot == 0)
    {
        edit_slot_ = 0;
        working_.reset();
        return SellResult::Sold;
    }

    // Prefer the character that followed the sold member; when the last
    // member was sold, stay on the new last member. Skip any foreign network
    // rows exactly as PREV/NEXT do.
    const int first_candidate = std::min(preferred_slot, write_slot - 1);
    for (int offset = 0; offset < write_slot; ++offset)
    {
        const int candidate = (first_candidate + offset) % write_slot;
        if (save_.team_list[static_cast<std::size_t>(candidate)] &&
            picker_lobby_save_slot_editable(candidate))
        {
            edit_slot_ = candidate;
            select_current_slot();
            return SellResult::Sold;
        }
    }

    edit_slot_ = 0;
    working_.reset();
    return SellResult::Sold;
}

bool TrainSession::resync_if_promoted()
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return false;
    if (working_->family == original->family)
        return false;

    // The real team member changed family underneath us (DETAILS promote
    // button). Discard the stale working copy — pending unaccepted stat
    // edits are meaningless across a class change anyway (bug A9).
    select_current_slot();
    return true;
}

const guy& TrainSession::working_copy() const
{
    return *working_;
}

const guy& TrainSession::original() const
{
    return *original_member();
}

std::uint32_t TrainSession::current_cost() const
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return 0;
    return calculate_train_cost(*working_, *original);
}

std::uint32_t TrainSession::current_sell_value() const
{
    const guy* const member = original_member();
    return member != nullptr ? calculate_sell_value(*member) : 0;
}

bool TrainSession::level_increased() const
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return false;
    return original->level < working_->level;
}

bool TrainSession::stats_increased() const
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return false;
    if (level_increased())
        return false;
    return (original->strength < working_->strength
         || original->dexterity < working_->dexterity
         || original->constitution < working_->constitution
         || original->intelligence < working_->intelligence
         || original->armor < working_->armor);
}

int TrainSession::current_slot() const
{
    return edit_slot_;
}

void TrainSession::select_current_slot()
{
    const guy* const original = original_member();
    if (!original) {
        working_.reset();
        return;
    }

    working_ = std::make_unique<guy>(original->family);
    statscopy(working_.get(), original);
}

guy* TrainSession::original_member()
{
    if (edit_slot_ < 0 || edit_slot_ >= MAX_TEAM_SIZE)
        return nullptr;
    if (!picker_lobby_save_slot_editable(edit_slot_))
        return nullptr;
    return save_.team_list[static_cast<std::size_t>(edit_slot_)].get();
}

const guy* TrainSession::original_member() const
{
    if (edit_slot_ < 0 || edit_slot_ >= MAX_TEAM_SIZE)
        return nullptr;
    if (!picker_lobby_save_slot_editable(edit_slot_))
        return nullptr;
    return save_.team_list[static_cast<std::size_t>(edit_slot_)].get();
}

void TrainSession::clamp_working_stats()
{
    const guy* const original = original_member();
    if (!working_ || !original)
        return;

    if (working_->strength < original->strength)
        working_->strength = original->strength;
    if (working_->dexterity < original->dexterity)
        working_->dexterity = original->dexterity;
    if (working_->constitution < original->constitution)
        working_->constitution = original->constitution;
    if (working_->intelligence < original->intelligence)
        working_->intelligence = original->intelligence;
    if (working_->armor < original->armor)
        working_->armor = original->armor;
    if (working_->level < original->level)
        working_->upgrade_to_level(original->level);
}

// --- Base Camp player-seat rail geometry (#243) ---

int seats_still_claimable(const SeatClaimability& claim)
{
    if (!claim.multiplayer_enabled)
        return 0;
    const int local_room = claim.local_seat_cap - claim.local_count;
    const int global_room = claim.global_cap - claim.global_count;
    return std::max(0, std::min(local_room, global_room));
}

int base_camp_seat_rail_slot_cap(const SeatClaimability& claim)
{
    if (!claim.multiplayer_enabled)
        return 1;
    return std::clamp(claim.local_seat_cap, 1, kSeatRailSlots);
}

int base_camp_seat_rail_placeholder_count(const SeatClaimability& claim)
{
    return std::clamp(base_camp_seat_rail_slot_cap(claim) - claim.local_count,
                      0, kSeatRailSlots);
}

SeatRailLayout base_camp_seat_rail_layout(int visible_cards,
                                          int placeholder_count)
{
    SeatRailLayout out;
    out.shown_cards = std::clamp(visible_cards, 0, kSeatRailSlots);
    out.placeholder_count =
        std::clamp(placeholder_count, 0, kSeatRailSlots - out.shown_cards);
    // Every slot keeps its grid x whether or not it is live this frame: the
    // hidden ones are what a returning player's seat lands back on.
    for (int slot = 0; slot < kSeatRailSlots; ++slot) {
        out.slot_x[static_cast<std::size_t>(slot)] =
            kSeatRailX0 + slot * (kSeatRailCardWidth + kSeatRailGap);
    }
    return out;
}

// --- Player seats (#218 seat block) ---

std::string company_abbreviation(std::string_view company)
{
    std::string result;
    result.reserve(3);
    for (const char ch : company)
    {
        if (!std::isalnum(static_cast<unsigned char>(ch)))
            continue;
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        if (result.size() == 3)
            break;
    }
    return result.empty() ? "NET" : result;
}

std::string seat_identity_label(const ScenarioSeatRow& seat)
{
    std::string result = std::format(
        "P{} {}", seat.player_index + 1,
        seat.is_local ? std::string("YOU")
                      : company_abbreviation(seat.company));
    if (seat.ready)
        result += " [RDY]";
    return result;
}

std::string format_seat_summary(
    const std::vector<og::sim::LobbyPlayer>& players)
{
    if (players.empty())
        return "NO PLAYER SEATS";
    std::array<int, SCORE_TEAM_COUNT> counts{};
    for (const og::sim::LobbyPlayer& player : players)
    {
        if (player.team >= 0 && player.team < SCORE_TEAM_COUNT)
            ++counts[static_cast<std::size_t>(player.team)];
    }
    const int occupied = static_cast<int>(std::count_if(
        counts.begin(), counts.end(), [](int count) { return count > 0; }));
    const int player_count = static_cast<int>(players.size());
    if (occupied == 1)
        return "CO-OP";
    if (player_count == 4 && occupied == 2 &&
        std::count(counts.begin(), counts.end(), 2) == 2)
    {
        return "2 VS 2";
    }
    if (occupied == player_count)
        return "FREE-FOR-ALL";
    return "MIXED TEAMS";
}

std::vector<og::sim::LobbyPlayer> synthesize_local_lobby_players(
    const SaveData& save)
{
    std::vector<og::sim::LobbyPlayer> players;
    if (save.numplayers <= 0)
        return players;
    const std::vector<short> teams =
        derive_local_gameplay_seat_teams(save);
    for (std::size_t index = 0; index < teams.size(); ++index)
    {
        players.push_back(og::sim::LobbyPlayer{
            .player_index = static_cast<std::uint8_t>(index),
            .name = std::format("Player {}", index + 1),
            .company = save.save_name,
            .team = teams[index],
            .character_slots = {},
            .ready = false,
            .is_host = index == 0,
        });
    }
    return players;
}

// --- Scenario roster report (View Level) ---

namespace {

// Defensive cap for pathological custom levels (pagination handles real maps).
constexpr std::size_t kMaxScenarioReportRows = 200;

// The modes.core bot provenance tag (lib/mode_caps.lua BOT_MARK_BIT): every
// squad member add_squad_member spawns — init squads AND wiped-team revives
// — carries stats bit 65536, replicated via EntitySnapshot::bit_flags, so
// bot-vs-troop provenance is networked-exact on every mirror. The value is
// a wire-visible contract with the pack, pinned by the staged report tests.
constexpr std::int32_t kBotMarkBit = 65536;

// The shared MATCHED header-band mode var (lib/mode_match.lua MATCHED.SIZE,
// slot 5, mode-neutral by convention): non-zero exactly when the FAIR
// census ran with a roster, banked at staged init — the match.fills rule
// "matched with zero headcount is the plain squad" read back as a fact.
constexpr std::size_t kModeVarMatchedSize = 5;

// The shared lineup-facts mode var (lib/mode_match.lua MATCHED.ANNOUNCED,
// slot 4, co-tenanted because the mode-private band is spent): the ones
// digit is the matched announce latch; the digits above pack one base-100
// code per team, team t's code at 10 * 100^t (§3.4, bank_lineup_facts).
//
// THE SHARED DIGIT LAYOUT, written out here beside the decoder and beside
// `bank_lineup_facts` in mode_match.lua, because the two halves live in
// different languages and can only agree on paper:
//
//   slot value = latch                      (ones digit, MATCHED.ANNOUNCED)
//              + code(team 0) * 10
//              + code(team 1) * 10 * 100
//              + code(team 2) * 10 * 100^2
//              + code(team 3) * 10 * 100^3
//              + refusal      * 1e9         (kLineupRefusalBase below)
//
//   code = 0                 nothing banked — no squad of this team's
//                            fielded anybody, so the pane names no fill
//        = fill              the APPLIED FILL code itself, 1 = WEAK,
//                            2 = FAIR, 3 = STRONG, 4 = BRUTAL
//
// The code carried a +1 bias for as long as FAIR shared 0 with the DEFAULT
// and a bare code could not tell "a FAIR squad walked on" from "this team
// banked nothing". Amendment 4 (E4) makes 0 unambiguous a second way and
// for good: NONE is the stored 0 now, a NONE team spawns no squad, and a
// squad that never spawned banks nothing — so the four codes a bank can
// carry are exactly WEAK..BRUTAL and 0 is always "nothing". Amendment B7
// replaced the older mixed-radix `squad * 11 + offset` pair with this
// single value when the preset ordinals and the LV offset went away.
constexpr std::size_t kModeVarLineupFacts = 4;

// Decode team t's applied lineup code from the shared slot.
int lineup_fact_code(std::int32_t slot_value, int team)
{
    std::int64_t facts = static_cast<std::int64_t>(slot_value) / 10;
    for (int t = 0; t < team; ++t)
        facts /= 100;
    return static_cast<int>(facts % 100);
}

// The applied FILL value a code carries, or -1 when the team banked
// nothing (and for any code outside the four spawnable fills, which is
// what a stale bank from an older build reads as).
//
// E4: a banked code IS the stored fill of a squad that SPAWNED, so it runs
// WEAK..BRUTAL and never NONE — a NONE team fields no squad, and a squad
// that never walked on banks nothing. 0 is therefore the empty digit pair
// every unbanked team leaves behind and nothing else. The Lua half
// (packs/core/lib/lineup.lua bank_lineup_facts) banks exactly this,
// unbiased.
int lineup_fact_fill(int code)
{
    if (code < og::sim::kFillWeak || code > og::sim::kFillBrutal)
        return -1;
    return code;
}

// The refusal REASON digit, banked ABOVE the four team codes in the same
// shared slot (mode_match.lua REFUSAL_BASE): 10^9, the last decimal digit
// an i32 slot can hold beside the latch and the four base-100 codes
// (1 + 4*2 = 9 digits below it). 1 = a band mode refused for want of
// fighters; 0 = the team modes' sentence, which is also what every world
// that banks nothing reads as.
constexpr std::int32_t kLineupRefusalBase = 1000000000;

int lineup_refusal_code(std::int32_t slot_value)
{
    return static_cast<int>(
        (static_cast<std::int64_t>(slot_value) / kLineupRefusalBase) % 10);
}

bool is_score_team_index(int team)
{
    return team >= 0 && team < 4;
}

// The pane's row budget (the zone-submenu chassis: 48-char rows).
constexpr std::size_t kMaxReportLine = 48;

std::string clip_line(std::string line)
{
    if (line.size() > kMaxReportLine)
        line.resize(kMaxReportLine);
    return line;
}

// Display label for a staged fill (the honesty rules: squads never list
// families — the rows below the header list the ACTUAL staged squad;
// matched bots show headcount only, never levels).
std::string fill_display_label(ScenarioFill fill)
{
    switch (fill)
    {
        case ScenarioFill::Company: return "COMPANY";
        case ScenarioFill::Troops: return "MAP TROOPS";
        case ScenarioFill::Bots: return "BOT SQUAD";
        case ScenarioFill::Matched: return "MATCHED BOTS";
        case ScenarioFill::Generators: return "GENERATORS";
        case ScenarioFill::Empty: break;
    }
    return "NO FORCES";
}

// The count-only fallback (no staged world): authored domain from the
// start markers, activation from the D29 effective_team_mask clamp. The
// roster rule lives in the mode Lua alone — with no staged world the
// preview shows the clamp for every TROOPS value.
void fill_fallback_activation(ScenarioRosterReport& report,
                              GameWorld& world, const SaveData& save)
{
    // The engine anchor scan, on the caller's DISPOSABLE scratch world
    // (the exact scan launch step 0 runs; idempotent, rebuilt from
    // scratch). The anchor counts read back from world.respawn — the
    // cap rule has one home, no hand re-count here.
    og::sim::respawn_scan_anchors(world);
    for (int t = 0; t < 4; ++t)
    {
        report.team_anchor_count[static_cast<std::size_t>(t)] =
            world.respawn.anchor_count[static_cast<std::size_t>(t)];
    }
    const std::uint8_t authored_markers = og::sim::authored_team_mask(world);
    const std::uint8_t effective =
        og::sim::effective_team_mask(authored_markers, save.ctf_team_count);
    int active_count = 0;
    for (int t = 0; t < 4; ++t)
    {
        const auto ti = static_cast<std::size_t>(t);
        report.team_authored[ti] = (authored_markers & (1u << t)) != 0;
        report.team_active[ti] = (effective & (1u << t)) != 0;
        active_count += report.team_active[ti] ? 1 : 0;
    }
    report.will_activate = active_count >= 2;
    if (!report.will_activate)
    {
        for (bool& active : report.team_active)
            active = false;
    }
}

// The per-team staged census, shared by the mode arm and the classic arm
// (amendment C5: the packs/core lineup stage applies FILL and MAP UNITS on
// every campaign, so a classic staged world carries the same observable
// facts a mode's does). Live counts over the staged oblist (dead and
// dormant excluded): activity and fills from observable facts, never a
// rule twin; the banked lineup facts name the applied fill (B7/R4).
void census_staged_teams(ScenarioRosterReport& report,
                         const GameWorld& staged)
{
    std::array<int, 4> company{};
    std::array<int, 4> troops{};
    std::array<int, 4> bots{};
    std::array<int, 4> generators{};
    for (const auto& uptr : staged.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->dormant())
            continue;
        const int team = w->team_num();
        if (!is_score_team_index(team))
            continue;
        const auto ti = static_cast<std::size_t>(team);
        const Order order = w->query_order();
        if (order == Order::Living)
        {
            if (w->myguy != nullptr)
                company[ti]++;
            else if (w->stats() != nullptr &&
                     (w->stats()->bit_flags() & kBotMarkBit) != 0)
                bots[ti]++;
            else
                troops[ti]++;
        }
        else if (order == Order::Generator)
        {
            generators[ti]++;
        }
    }
    const bool matched = staged.mode.vars[kModeVarMatchedSize] > 0;
    for (int t = 0; t < 4; ++t)
    {
        const auto ti = static_cast<std::size_t>(t);
        const bool any = company[ti] > 0 || troops[ti] > 0 ||
            bots[ti] > 0 || generators[ti] > 0;
        report.team_active[ti] = any;
        // The match.fills display priority, read as facts: a
        // mixed company+troops team labels COMPANY exactly as
        // the plan did.
        if (company[ti] > 0)
        {
            report.team_fill[ti] = ScenarioFill::Company;
            report.team_fill_count[ti] = company[ti];
        }
        else if (troops[ti] > 0)
        {
            report.team_fill[ti] = ScenarioFill::Troops;
            report.team_fill_count[ti] = troops[ti];
        }
        else if (bots[ti] > 0)
        {
            report.team_fill[ti] = matched ? ScenarioFill::Matched
                                           : ScenarioFill::Bots;
            report.team_fill_count[ti] = bots[ti];
        }
        else if (generators[ti] > 0)
        {
            report.team_fill[ti] = ScenarioFill::Generators;
            report.team_fill_count[ti] = generators[ti];
        }
        else
        {
            report.team_fill[ti] = ScenarioFill::Empty;
            report.team_fill_count[ti] = 0;
        }
    }

    // Lineup facts (§3.4 as amended by B7): the spawn seam
    // banked the APPLIED per-team FILL code in the shared slot.
    // A team whose label is its COMPANY (or its map troops) can
    // still field a squad beside the occupants (review L2):
    // those bots are the team_squad_count column, and the same
    // banked fact names them, so the preview's count and its
    // fill word are the spawned facts on every shape.
    const std::int32_t lineup_facts =
        staged.mode.vars[kModeVarLineupFacts];
    for (int t = 0; t < 4; ++t)
    {
        const auto ti = static_cast<std::size_t>(t);
        if (report.team_fill[ti] != ScenarioFill::Bots &&
            report.team_fill[ti] != ScenarioFill::Matched)
        {
            // Bots beside an occupancy fill — 0 when the hard
            // shape left no room, and then no fact either (the
            // fold banks nothing for a squad that never
            // spawned), so the row stays the plain occupancy.
            report.team_squad_count[ti] = bots[ti];
            if (bots[ti] == 0)
                continue;
        }
        report.team_squad_fill[ti] =
            lineup_fact_fill(lineup_fact_code(lineup_facts, t));
    }
}

// Row scan shared by the staged and fallback arms: named entities (company
// fighters and authored NPCs alike) individually, nameless livings grouped
// by (team, family, level), generators aggregated per team, list order,
// team-major sort. Skips dead walkers always and
// dormant (delayed-spawn) walkers exactly as the keyframe capture does —
// the staged census is identical on every client (the documented preview
// carve-out; the fallback scratch world spawns nothing dormant-relevant).
void scan_roster_rows(ScenarioRosterReport& report, const GameWorld& world)
{
    std::vector<ScenarioRosterRow> generator_rows;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->dead() || w->dormant())
            continue;
        const Order order = w->query_order();
        if (order == Order::Living)
        {
            ScenarioRosterRow row;
            row.team = static_cast<short>(w->team_num());
            row.family = static_cast<short>(w->family());
            row.level = w->stats() != nullptr ? w->stats()->level() : 1;
            // The shared display-name precedence (walker.cpp): the fighter's
            // own myguy name first, an authored stats name second. A company
            // fighter's name never reached stats — create_team_walker only
            // stamps the owned guy — so this is what puts real character
            // names in the census instead of a "3x SOLDIER" group. Guy names
            // ride the keyframe (GuySnapshot::name), so every client agrees.
            const std::string_view display_name =
                w->stats() != nullptr ? entity_display_name(w)
                                      : std::string_view{};
            if (!display_name.empty())
            {
                row.named = true;
                row.name = display_name;
                report.rows.push_back(std::move(row));
            }
            else
            {
                auto match = std::find_if(
                    report.rows.begin(), report.rows.end(),
                    [&row](const ScenarioRosterRow& existing) {
                        return !existing.named && !existing.is_generator &&
                            existing.team == row.team &&
                            existing.family == row.family &&
                            existing.level == row.level;
                    });
                if (match != report.rows.end())
                    match->count++;
                else
                    report.rows.push_back(std::move(row));
            }
        }
        else if (order == Order::Generator)
        {
            const short team = static_cast<short>(w->team_num());
            auto match = std::find_if(
                generator_rows.begin(), generator_rows.end(),
                [team](const ScenarioRosterRow& existing) {
                    return existing.team == team;
                });
            if (match != generator_rows.end())
            {
                match->count++;
            }
            else
            {
                ScenarioRosterRow row;
                row.team = team;
                row.is_generator = true;
                row.family = static_cast<short>(w->family());
                generator_rows.push_back(std::move(row));
            }
        }
        if (report.rows.size() + generator_rows.size() >=
            kMaxScenarioReportRows)
        {
            break;
        }
    }
    report.rows.insert(report.rows.end(),
                       std::make_move_iterator(generator_rows.begin()),
                       std::make_move_iterator(generator_rows.end()));

    // Team-major presentation order (stable within a team = list order).
    std::stable_sort(report.rows.begin(), report.rows.end(),
                     [](const ScenarioRosterRow& a, const ScenarioRosterRow& b) {
                         return a.team < b.team;
                     });
}

} // namespace

ScenarioRosterReport build_scenario_roster_report(
    const GameWorld* staged, StagePreviewStatus status, const SaveData& save,
    GameWorld* fallback_world, const ScenarioSeatContext* seats)
{
    ScenarioRosterReport report;
    report.your_team = save.my_team;
    report.stage_failed = status == StagePreviewStatus::Failed;

    // The seat block (#218): resolve the caller's lobby seats into P#-sorted
    // display rows. Reading LobbyPlayer::team reads the record the launch
    // bindings are built FROM — display of an existing fact. is_local is
    // per-client presentation (YOU vs abbreviation), resolved here so the
    // formatter stays a pure function of the report.
    if (seats != nullptr && !seats->players.empty())
    {
        std::vector<og::sim::LobbyPlayer> players = seats->players;
        std::sort(players.begin(), players.end(),
                  [](const og::sim::LobbyPlayer& lhs,
                     const og::sim::LobbyPlayer& rhs) {
                      return lhs.player_index < rhs.player_index;
                  });
        report.seat_summary = format_seat_summary(players);
        for (const og::sim::LobbyPlayer& player : players)
        {
            report.seats.push_back(ScenarioSeatRow{
                .player_index = static_cast<int>(player.player_index),
                .company = player.company,
                .team = static_cast<short>(player.team),
                .ready = player.ready,
                .is_local =
                    std::find(seats->local_player_indices.begin(),
                              seats->local_player_indices.end(),
                              player.player_index) !=
                    seats->local_player_indices.end(),
            });
        }
    }

    if (staged != nullptr)
    {
        // The staged arm: every fact below is an OBSERVATION of the world
        // the launch will adopt — host stage, joiner mirror and the wire
        // all hold the same bytes, so every client reports identically.
        report.staged = true;
        report.is_versus = (staged->type & GameWorld::TYPE_SCRIPTED) != 0;
        if (report.is_versus)
        {
            report.will_activate = staged->mode.active;
            if (staged->mode.active)
            {
                report.mode_name.assign(staged->mode.name.data());
            }
            else if (staged->mode.init_attempted)
            {
                // Refusal vs no-mode disambiguation through the existing
                // diagnostic seam (level_hook_kinds_for, pinned by
                // test_modes_levels): a level registering on_mode_init that
                // attempted and refused gets the honest sentence; a
                // hook-less scripted level has no mode and the count-only
                // fallback answers below. Identical on joiner mirrors: the
                // mirror level-load runs the same mounted pack registration.
                const std::uint32_t kinds =
                    og::script::hooks::level_hook_kinds_for(staged->id);
                report.refusing = (kinds & (1u << 4)) != 0;  // LevelHook::ModeInit
                // The reason rides the shared facts slot, which the refusing
                // fold banked before it raised (a refusal keeps the staged
                // world, mode vars included).
                report.refusal_fighters =
                    lineup_refusal_code(
                        staged->mode.vars[kModeVarLineupFacts]) == 1;
            }

            // Anchor counts were banked by the REAL mode_stage_init scan at
            // stage time (RespawnState replicates), so a const read answers.
            for (int t = 0; t < 4; ++t)
            {
                report.team_anchor_count[static_cast<std::size_t>(t)] =
                    staged->respawn.anchor_count[static_cast<std::size_t>(t)];
            }

            if (staged->mode.active)
            {
                report.mode_census = true;
                census_staged_teams(report, *staged);
            }
            else if (!report.refusing)
            {
                // A hook-less scripted level: the count-only clamp answers
                // over the STAGED world (markers survive an init that never
                // ran; anchors were banked above, so only the mask math
                // runs here).
                const std::uint8_t authored_markers =
                    og::sim::authored_team_mask(*staged);
                const std::uint8_t effective = og::sim::effective_team_mask(
                    authored_markers, save.ctf_team_count);
                int active_count = 0;
                for (int t = 0; t < 4; ++t)
                {
                    const auto ti = static_cast<std::size_t>(t);
                    report.team_authored[ti] =
                        (authored_markers & (1u << t)) != 0;
                    report.team_active[ti] = (effective & (1u << t)) != 0;
                    active_count += report.team_active[ti] ? 1 : 0;
                }
                report.will_activate = active_count >= 2;
                if (!report.will_activate)
                {
                    for (bool& active : report.team_active)
                        active = false;
                }
            }
        }
        else
        {
            // The classic arm (amendment C5): the same census fold over the
            // same staged world — the packs/core lineup stage applied the
            // per-team strip and FILL squads at stage time, so the fills and
            // strips are observable facts here exactly as under a mode.
            // Classic levels never refuse (C4), so there is no activation
            // clamp: every team with anything standing gets its line.
            report.mode_census = true;
            census_staged_teams(report, *staged);
        }
        scan_roster_rows(report, *staged);
        return report;
    }

    if (fallback_world == nullptr)
    {
        // Nothing to read at all: the formatter renders the refusal lines
        // ("PREVIEW UNAVAILABLE", led by STAGING FAILED when it applies).
        report.unavailable = true;
        return report;
    }

    report.is_versus =
        (fallback_world->type & GameWorld::TYPE_SCRIPTED) != 0;
    if (report.is_versus)
        fill_fallback_activation(report, *fallback_world, save);
    scan_roster_rows(report, *fallback_world);
    return report;
}

std::vector<std::string> format_scenario_report_lines(
    const ScenarioRosterReport& report)
{
    std::vector<std::string> lines;

    // The honest degradation lead lines: an owner stage failure always
    // announces itself; with no world at all that announcement (or the
    // plain unavailability) is the whole report.
    if (report.stage_failed)
        lines.push_back(clip_line("STAGING FAILED"));
    if (report.unavailable)
    {
        lines.push_back(clip_line("PREVIEW UNAVAILABLE"));
        return lines;
    }

    if (report.is_versus || (report.staged && report.mode_census))
    {
        if (report.staged && report.mode_census)
        {
            // The staged arm: mode name, active-team count, per-team fills
            // — all censused from the world the launch adopts. Inactive
            // teams have no entities and no row (the rendered pane shows
            // absence honestly); the old "BOT CLASSES DRAWN AT START"
            // legend is gone because the rows below list the ACTUAL staged
            // squad (#235 delivered by deletion). A staged CLASSIC world
            // (amendment C5) renders the same team lines with no header —
            // there is no mode to name and no match to count teams for.
            if (report.is_versus)
            {
                int active_count = 0;
                for (int t = 0; t < 4; ++t)
                    active_count +=
                        report.team_active[static_cast<std::size_t>(t)] ? 1
                                                                        : 0;
                lines.push_back(clip_line(std::format(
                    "MATCH: {} - {} TEAMS ACTIVE", report.mode_name,
                    active_count)));
            }
            for (int t = 0; t < 4; ++t)
            {
                const auto ti = static_cast<std::size_t>(t);
                if (!report.team_active[ti])
                    continue;
                std::string fill = fill_display_label(report.team_fill[ti]);
                if (report.team_squad_count[ti] > 0 &&
                    report.team_fill[ti] != ScenarioFill::Empty &&
                    report.team_fill[ti] != ScenarioFill::Bots &&
                    report.team_fill[ti] != ScenarioFill::Matched)
                {
                    // A squad fielded BESIDE the occupants (review L2): both
                    // halves are named and both are counted, because both
                    // walk onto the floor. The squad half is the plain word
                    // BOTS now — B7 moved what KIND of squad it is out of
                    // the noun and into the trailing fill word, where one
                    // spelling answers for every shape.
                    fill += std::format("+BOTS ({}+{})",
                                        report.team_fill_count[ti],
                                        report.team_squad_count[ti]);
                }
                else if (report.team_fill[ti] != ScenarioFill::Empty)
                {
                    fill += std::format(" ({})", report.team_fill_count[ti]);
                }
                // The APPLIED fill word closes the row (B7): "BOT SQUAD (5)
                // FAIR", "COMPANY+BOTS (3+2) WEAK". A team that banked no
                // fill — map troops alone, a mode that ignored the knob —
                // ends at its count, so the pane never names a fill that
                // did not decide anything.
                //
                // Budget: the worst row is
                // "  YELLOW TEAM  ACTIVE - COMPANY+BOTS (3+2) BRUTAL"
                // (24 + 12 + 6 + 7 = 49), one over. The separator space
                // before the word is the first thing the budget spends, so
                // such a row glues the word to the count ("(3+2)BRUTAL",
                // 48) rather than losing a letter of it — a clipped word is
                // a different word.
                std::string line = std::format("  {} TEAM  ACTIVE - {}",
                                               og::sim::team_color_name(t),
                                               fill);
                if (report.team_squad_fill[ti] >= 0)
                {
                    const std::string_view token = lineup_fill_name(
                        static_cast<short>(report.team_squad_fill[ti]));
                    if (line.size() + 1 + token.size() > kMaxReportLine)
                        line += token;
                    else
                        line += " " + std::string(token);
                }
                lines.push_back(clip_line(line));
            }
        }
        else if (report.staged && report.refusing)
        {
            // The staged mode attempted init and refused: the verbatim
            // honest sentence. The staged world is KEPT as-is and rendered
            // below — it IS the world GO would adopt (classic rules over
            // the post-refusal state). A broken-pack init error reads the
            // same way: both leave classic rules, so the sentence is true
            // either way. WHICH sentence comes from the banked reason digit
            // (review L1): a band mode (FFA/mutant) has no teams to be short
            // of — it counts FIGHTERS — so the teams sentence would be a
            // false statement about the very refusal it is explaining.
            lines.push_back(clip_line(
                report.refusal_fighters
                    ? "MATCH WILL NOT START: FEWER THAN 2 FIGHTERS"
                    : "MATCH WILL NOT START: FEWER THAN 2 TEAMS"));
        }
        else
        {
            // Count-only fallback: a scripted level with no mode (no packs
            // or no on_mode_init hook), or no staged world at all. Today's
            // exact lines (the no-pack pins).
            int marker_teams = 0;
            for (const bool present : report.team_authored)
                marker_teams += present ? 1 : 0;
            lines.push_back(clip_line(std::format(
                "MATCH: {} AUTHORED TEAMS", marker_teams)));
            if (report.will_activate)
            {
                for (int t = 0; t < 4; ++t)
                {
                    if (!report.team_authored[static_cast<std::size_t>(t)])
                        continue;
                    lines.push_back(clip_line(std::format(
                        "  {} TEAM  MARKERS: {}  {}",
                        og::sim::team_color_name(t),
                        report.team_anchor_count[static_cast<std::size_t>(t)],
                        report.team_active[static_cast<std::size_t>(t)] ? "ACTIVE" : "INACTIVE")));
                }
            }
            else
            {
                lines.push_back(
                    clip_line("MATCH INACTIVE: FEWER THAN 2 AUTHORED TEAMS"));
            }
        }
    }

    // The seat block (#218): the lobby seats against their teams, directly
    // after the match block with no blank between — the first-block TRACE
    // seam carries it, and on non-versus reports it leads. One line per
    // seat (worst case 29 chars) avoids all wrapping; a 16-seat lobby rides
    // the existing pager.
    if (!report.seats.empty())
    {
        lines.push_back(clip_line("SEATS: " + report.seat_summary));
        for (const ScenarioSeatRow& seat : report.seats)
        {
            const std::string team_label =
                (seat.team >= 0 && seat.team < 4)
                    ? std::format("{} TEAM",
                                  og::sim::team_color_name(seat.team))
                    : std::format("TEAM {}", seat.team);
            lines.push_back(clip_line(std::format(
                "  {} - {}", seat_identity_label(seat), team_label)));
        }
    }

    short current_team = -1;
    bool first_team = !report.is_versus;
    for (const ScenarioRosterRow& row : report.rows)
    {
        if (row.team != current_team)
        {
            current_team = row.team;
            if (!first_team || !lines.empty())
                lines.emplace_back();
            first_team = false;
            // Score teams get their color name (matching the terminal team
            // rows and the CTF flag lines); anything beyond keeps the raw
            // index.
            std::string header = (current_team >= 0 && current_team < 4)
                ? std::format("{} TEAM",
                              og::sim::team_color_name(current_team))
                : std::format("TEAM {}", current_team);
            if (current_team == report.your_team)
                header += " (YOURS)";
            lines.push_back(clip_line(std::move(header)));
        }

        std::string text;
        if (row.is_generator)
        {
            text = std::format("  {}x GENERATOR", row.count);
        }
        else if (row.named)
        {
            text = std::format("  {} - {} Lv {}", row.name,
                               family_display_name(row.family), row.level);
        }
        else
        {
            text = std::format("  {}x {} Lv {}", row.count,
                               family_display_name(row.family), row.level);
        }
        lines.push_back(clip_line(std::move(text)));
    }
    return lines;
}

// --- LINEUP support ---

namespace {

// The family combat bases a fighter would actually spawn with. A family the
// registry does not carry (an unmounted pack class, or a .gtl family byte
// the save carries with no range check) prices as a soldier — the picker's
// own rule at picker_compute_guy_derived_stats.
const FamilyDescriptor* guy_family_descriptor(const guy& g)
{
    const FamilyDescriptor* descriptor =
        get_family_descriptor(static_cast<int>(g.family));
    if (descriptor == nullptr)
        descriptor = get_family_descriptor(FAMILY_SOLDIER);
    return descriptor;
}

CombatBases guy_combat_bases(const guy& g)
{
    const FamilyDescriptor* descriptor = guy_family_descriptor(g);
    return descriptor != nullptr ? descriptor->combat : CombatBases{};
}

}  // namespace

// --- LINEUP (docs/lineup-design.md) ------------------------------------

bool lineup_fighter_team_editable(const SaveData& save, int slot_index,
                                  bool zone_can_team, bool assign_mode)
{
    if (slot_index < 0 || slot_index >= MAX_TEAM_SIZE)
        return false;
    if (save.team_list[static_cast<std::size_t>(slot_index)] == nullptr)
        return false;
    if (!picker_lobby_save_slot_editable(slot_index))
        return false;
    if (!zone_can_team)
        return false;
    return !assign_mode;
}

bool lineup_fighter_team_editable(const SaveData& save, int slot_index,
                                  const CampaignZoneSession* zone,
                                  bool assign_mode)
{
    const bool can_team = zone == nullptr || zone->roster().can_team;
    return lineup_fighter_team_editable(save, slot_index, can_team,
                                        assign_mode);
}

bool lineup_zone_can_team(SaveData& save)
{
    CampaignZoneSession zone(save);
    zone.fetch();
    return zone.roster().can_team;
}

DerivedStats compute_derived_stats(const guy& g)
{
    const CombatBases bases = guy_combat_bases(g);
    return compute_derived_stats(g, bases.hp, bases.melee_damage,
                                 bases.stepsize, bases.fire_delay);
}

float derived_fire_delay(const guy& g)
{
    const float delay =
        guy_combat_bases(g).fire_delay - g.get_fire_frequency_bonus();
    return delay < 1.0f ? 1.0f : delay;
}

namespace {

// The campaign `lineup.power` hook is a Lua pcall, and the LINEUP page prices
// every deployed fighter on EVERY frame of an otherwise idle menu (the band
// draw and the FIGHTERS row draw both go through it). The hook is a pure
// function of the ROW the engine hands it — nothing else crosses the fence —
// so the answer is memoized on that row: a changed fighter is a different
// key, which makes a stale price impossible. The one thing the row cannot
// see is the HOOK itself changing, so the screens that hold the page clear
// the memo whenever the registration could have moved under them
// (lineup_power_cache_clear).
struct LineupPowerMemoEntry {
    og::script::hooks::LineupPowerRow row;
    std::optional<long long> price;
};

// Two well-stocked machines' worth of distinct rows; past that the memo
// starts over rather than growing without a bound.
constexpr std::size_t kLineupPowerMemoMax = 64;
std::vector<LineupPowerMemoEntry>& lineup_power_memo()
{
    static std::vector<LineupPowerMemoEntry> memo;
    return memo;
}

bool same_power_row(const og::script::hooks::LineupPowerRow& lhs,
                    const og::script::hooks::LineupPowerRow& rhs)
{
    return lhs.level == rhs.level && lhs.hp == rhs.hp && lhs.mp == rhs.mp &&
        lhs.armor == rhs.armor && lhs.damage == rhs.damage &&
        lhs.stepsize == rhs.stepsize &&
        lhs.fire_frequency == rhs.fire_frequency && lhs.family == rhs.family;
}

}  // namespace

void lineup_power_cache_clear() noexcept
{
    lineup_power_memo().clear();
}

std::array<int, 4> census_lineup_map_units(const GameWorld& world)
{
    // The B4 census the MAP UNITS box state gates on: the map's own
    // authored units per team. One walk for both worlds the surfaces
    // read — the SDL picker's RAW loaded level (nothing there is dead,
    // bot-marked or guy-owned, so every living counts, the pre-F3
    // arithmetic exactly) and the terminals' STAGED world, where the
    // fighters carry guys, a FILL squad carries the bot mark, and a
    // box-off team's units are retired dead — authored still, which is
    // what keeps the box alive to turn them back on. Generators are the
    // GENERATOR RATE knob's business and stay out of the count.
    std::array<int, 4> counts{};
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w == nullptr || w->query_order() != Order::Living)
            continue;
        if (w->myguy != nullptr)
            continue;
        if (w->stats() != nullptr &&
            (w->stats()->bit_flags() & kBotMarkBit) != 0)
            continue;
        const int team = w->team_num();
        if (is_score_team_index(team))
            ++counts[static_cast<std::size_t>(team)];
    }
    return counts;
}

std::optional<long long> lineup_power_for_guy(const guy& g)
{
    if (!og::script::hooks::campaign_lineup_registered())
        return std::nullopt;
    const DerivedStats ds = compute_derived_stats(g);
    og::script::hooks::LineupPowerRow row;
    // The name is the picker's own display helper (an unknown family reads
    // "BEAST"); only the NUMBERS fall back to a soldier's.
    row.family = family_display_name(static_cast<int>(g.family));
    row.level = static_cast<int>(g.level);
    // Truncation at the boundary, the §4.1 discipline: the campaign's
    // arithmetic is integer (og.div raises on a float), so the engine
    // hands over integers rather than making every book truncate.
    row.hp = static_cast<int>(ds.hp);
    row.mp = static_cast<int>(ds.mp);
    row.armor = static_cast<int>(ds.def);
    row.damage = static_cast<int>(ds.atk);
    row.stepsize = static_cast<int>(ds.spd);
    row.fire_frequency = static_cast<int>(derived_fire_delay(g));

    std::vector<LineupPowerMemoEntry>& memo = lineup_power_memo();
    for (const LineupPowerMemoEntry& entry : memo)
    {
        if (same_power_row(entry.row, row))
            return entry.price;
    }

    long long power = 0;
    const std::optional<long long> price =
        og::script::hooks::campaign_fighter_power(row, power)
        ? std::optional<long long>(power)
        : std::nullopt;
    if (memo.size() >= kLineupPowerMemoMax)
        memo.clear();
    memo.push_back(LineupPowerMemoEntry{std::move(row), price});
    return price;
}

std::string lineup_seat_label(const og::sim::LobbyPlayer& seat,
                              std::string_view short_name)
{
    const std::string owner = short_name.empty()
        ? company_abbreviation(seat.company)
        : std::string(short_name);
    return std::format("P{} {}", static_cast<int>(seat.player_index) + 1,
                       owner);
}

namespace {

// Sum a band's prices. ONE unpriced fighter voids the whole band: a total
// that silently omits a member is worse than no number at all.
struct LineupPowerAccumulator {
    long long total = 0;
    bool valid = true;

    void add(const LineupPowerFn& power, const guy& member)
    {
        if (!valid)
            return;
        if (!power) {
            valid = false;
            return;
        }
        const std::optional<long long> priced = power(member);
        if (!priced.has_value()) {
            valid = false;
            return;
        }
        total += *priced;
    }

    [[nodiscard]] std::optional<long long> result(int fighter_count) const
    {
        if (!valid || fighter_count <= 0)
            return std::nullopt;
        return total;
    }
};

}  // namespace

std::array<LineupTeamBand, 4> build_lineup_bands(
    const SaveData& own,
    std::span<const og::sim::LobbyPlayer> players,
    std::span<const std::uint8_t> local_player_indices,
    bool networked,
    const LineupPowerFn& power,
    const std::function<std::string(std::uint8_t)>& local_seat_short_name,
    std::span<const int> map_unit_counts)
{
    std::array<LineupTeamBand, 4> bands{};
    std::array<LineupPowerAccumulator, 4> prices{};
    for (int team = 0; team < 4; ++team)
    {
        const auto ti = static_cast<std::size_t>(team);
        bands[ti].team = team;
        // The staged census, handed in (B4). A caller with no staged world
        // passes nothing and every box reads NO MAP UNITS — the honest
        // answer when nobody has censused the map yet.
        if (ti < map_unit_counts.size())
            bands[ti].map_unit_count = std::max(map_unit_counts[ti], 0);
    }

    for (const og::sim::LobbyPlayer& seat : players)
    {
        if (seat.team < 0 || seat.team >= 4)
            continue;
        LineupTeamBand& band = bands[static_cast<std::size_t>(seat.team)];
        ++band.seat_count;
        const bool is_local =
            std::find(local_player_indices.begin(), local_player_indices.end(),
                      seat.player_index) != local_player_indices.end();
        std::string short_name;
        if (is_local && local_seat_short_name)
            short_name = local_seat_short_name(seat.player_index);
        band.seat_labels.push_back(lineup_seat_label(seat, short_name));
    }

    if (networked)
    {
        // Networked, the lobby is the whole census: every machine's own
        // company reaches this one as replicated slots, so counting the
        // local save again would count this machine's fighters twice.
        for (const og::sim::LobbyPlayer& seat : players)
        {
            for (const og::sim::LobbyCharacterSlot& slot : seat.character_slots)
            {
                if (!slot.deployed)
                    continue;
                const short team = slot.character.teamnum;
                if (team < 0 || team >= 4)
                    continue;
                LineupTeamBand& band = bands[static_cast<std::size_t>(team)];
                ++band.fighter_count;
                const std::unique_ptr<guy> member =
                    make_base_camp_display_guy(slot.character);
                prices[static_cast<std::size_t>(team)].add(power, *member);
            }
        }
    }
    else
    {
        for (const auto& member : own.team_list)
        {
            if (member == nullptr || !member->deployed)
                continue;
            const short team = member->teamnum;
            if (team < 0 || team >= 4)
                continue;
            LineupTeamBand& band = bands[static_cast<std::size_t>(team)];
            ++band.fighter_count;
            prices[static_cast<std::size_t>(team)].add(power, *member);
        }
    }

    for (std::size_t team = 0; team < bands.size(); ++team)
    {
        LineupTeamBand& band = bands[team];
        band.has_seat = band.seat_count > 0;
        band.power = prices[team].result(band.fighter_count);
        if (band.seat_count > 0 && band.fighter_count < band.seat_count)
        {
            band.diag = LineupTeamBand::Diag::NeedsFighters;
            band.needs = band.seat_count - band.fighter_count;
        }
        else if (band.seat_count == 0 && band.fighter_count > 0)
        {
            band.diag = LineupTeamBand::Diag::NoSeatAi;
        }
    }
    return bands;
}

// --- LINEUP labels ---

std::string_view lineup_fill_name(short fill)
{
    switch (fill)
    {
    case og::sim::kFillNone: return "NONE";
    case og::sim::kFillWeak: return "WEAK";
    case og::sim::kFillFair: return "FAIR";
    case og::sim::kFillStrong: return "STRONG";
    case og::sim::kFillBrutal: return "BRUTAL";
    default: break;
    }
    // Junk from a hand-edited save or an older build's sixth code. NONE is
    // the storage default and where clamp_fill lands a negative, so it is
    // the honest word for a value the engine does not recognise — and,
    // unlike the FAIR this used to answer, it promises no squad the level
    // will not field.
    return "NONE";
}

std::string format_lineup_fill_label(short fill)
{
    // The band's knob face is 80px = 12 characters, and "FILL: STRONG" /
    // "FILL: BRUTAL" are exactly 12 — the budget is spent, not merely
    // respected, so no value here may grow a letter.
    return std::format("FILL: {}", lineup_fill_name(fill));
}

std::string format_lineup_map_units_label(short map_units)
{
    // The SDL band draws this as the Base Camp deploy box (B9); the two
    // terminal clients have no box, so they read the same state as a word.
    return map_units == og::sim::kMapUnitsOn ? "MAP UNITS: ON"
                                            : "MAP UNITS: OFF";
}

std::string format_lineup_map_units_census(const LineupTeamBand& band)
{
    // B4: a team the map ships no units for has nothing to switch, so the
    // box is inert and the band says why instead of offering it. Empty
    // means the box is live and the fighter census owns the column.
    return band.map_unit_count > 0 ? std::string() : std::string("NO MAP UNITS");
}

std::string format_lineup_census(const LineupTeamBand& band)
{
    switch (band.diag)
    {
    case LineupTeamBand::Diag::NeedsFighters:
        return band.needs == 1
            ? std::string("NEEDS 1 FIGHTER")
            : std::format("NEEDS {} FIGHTERS", band.needs);
    case LineupTeamBand::Diag::NoSeatAi:
        return "NO SEAT: AI";
    case LineupTeamBand::Diag::None:
        break;
    }
    if (band.fighter_count <= 0)
        return "NO FIGHTERS";
    if (band.fighter_count == 1)
        return "1 FIGHTER";
    return std::format("{} FIGHTERS", band.fighter_count);
}

std::string format_lineup_power(std::optional<long long> power)
{
    return power.has_value() ? std::format("POWER {}", *power)
                             : std::string("POWER --");
}

std::string format_lineup_power_cell(std::optional<long long> power, int width)
{
    const int field = std::max(width, 2);
    std::string text = "--";
    if (power.has_value())
    {
        // Widest first; the first tier that FITS wins. Every step rounds
        // half away from zero, so the cell is a rounded number and never a
        // clipped one — a clipped number is a different number, and this
        // column exists to be compared.
        static constexpr struct { long long div; char suffix; } kTiers[] = {
            {1, '\0'},
            {1000LL, 'k'},
            {1000000LL, 'M'},
            {1000000000LL, 'G'},
            {1000000000000LL, 'T'},
            {1000000000000000LL, 'P'},
        };
        const long long value = *power;
        for (const auto& tier : kTiers)
        {
            long long scaled = value;
            if (tier.div > 1)
            {
                // Divide first: value + div/2 overflows near LLONG_MAX, and
                // the widest tier exists precisely for values that large.
                scaled = value / tier.div;
                const long long rest = value % tier.div;
                if ((rest < 0 ? -rest : rest) * 2 >= tier.div)
                    scaled += value >= 0 ? 1 : -1;
            }
            std::string candidate = std::to_string(scaled);
            if (tier.suffix != '\0')
                candidate.push_back(tier.suffix);
            if (static_cast<int>(candidate.size()) <= field)
            {
                text = std::move(candidate);
                break;
            }
        }
    }
    if (static_cast<int>(text.size()) > field)
        text.resize(static_cast<std::size_t>(field));
    return std::format("{:>{}}", text, field);
}

// The FILL wheel holds the five codes and nothing else, weakest to
// strongest: NONE, WEAK, FAIR, STRONG, BRUTAL. Since amendment 4 (E1) the
// display order IS the storage order AND the whole value space — NONE is
// the stored 0, so a fresh band sits on the wheel's first slot instead of
// somewhere off it, one click reaches WEAK and the wheel wraps both ways.
constexpr std::array<short, 5> kLineupFillWheel = {
    og::sim::kFillNone, og::sim::kFillWeak, og::sim::kFillFair,
    og::sim::kFillStrong, og::sim::kFillBrutal};

// The wheel slot a code sits in, or -1 for any junk a hand-edited save
// carries.
int lineup_fill_slot(short value)
{
    const auto steps = static_cast<int>(kLineupFillWheel.size());
    for (int i = 0; i < steps; ++i)
    {
        if (kLineupFillWheel[static_cast<std::size_t>(i)] == value)
            return i;
    }
    return -1;
}

short cycle_lineup_fill(short current, int dir)
{
    const auto steps = static_cast<int>(kLineupFillWheel.size());
    // The step leaves from the slot of the word the band is SHOWING, which
    // is now always the stored code's own slot — there is no resolution to
    // step from and no off-wheel value to enter at, so the first click on
    // gladiator's empty sides reaches WEAK the way the band reads. Junk
    // enters at NONE, the word lineup_fill_name gives it and the value
    // clamp_fill would land a negative on.
    int index = lineup_fill_slot(current);
    if (index < 0)
        index = lineup_fill_slot(og::sim::kFillNone);
    long long next = (static_cast<long long>(index) + dir) % steps;
    if (next < 0)
        next += steps;
    return kLineupFillWheel[static_cast<std::size_t>(next)];
}

short toggle_lineup_map_units(short current)
{
    // A box, so the step is a flip — and any junk value flips to ON, which
    // is the default and the classic behaviour.
    return current == og::sim::kMapUnitsOn ? og::sim::kMapUnitsOff
                                           : og::sim::kMapUnitsOn;
}

// --- SPLIT ---

LineupSplitPlan split_company(const SaveData& save,
                              std::span<const short> local_seat_teams,
                              LineupSplit mode,
                              const LineupPowerFn& power,
                              const std::function<bool(int)>& editable)
{
    LineupSplitPlan plan;

    std::vector<short> teams;
    for (const short team : local_seat_teams)
    {
        if (team < 0 || team >= 4)
            continue;
        if (std::find(teams.begin(), teams.end(), team) == teams.end())
            teams.push_back(team);
    }
    std::sort(teams.begin(), teams.end());
    if (teams.empty())
        return plan;

    struct Candidate {
        int slot = 0;
        long long power = 0;
        int level = 0;
    };
    std::vector<Candidate> candidates;
    bool priced = static_cast<bool>(power);
    for (int slot = 0; slot < MAX_TEAM_SIZE; ++slot)
    {
        const guy* member = save.team_list[static_cast<std::size_t>(slot)].get();
        if (member == nullptr || !member->deployed)
            continue;  // the bench is not in tonight's draft
        if (editable && !editable(slot))
        {
            ++plan.locked;
            continue;
        }
        Candidate candidate;
        candidate.slot = slot;
        candidate.level = static_cast<int>(member->level);
        if (priced)
        {
            const std::optional<long long> value = power(*member);
            if (value.has_value())
                candidate.power = *value;
            else
                priced = false;  // no metric: the whole draft falls to level
        }
        candidates.push_back(candidate);
    }
    if (candidates.empty())
        return plan;

    // A single seated team leaves nothing to divide: every mode is ALL TO 1.
    if (mode == LineupSplit::AllToFirst || teams.size() == 1)
    {
        for (const Candidate& candidate : candidates)
            plan.moves.emplace_back(candidate.slot, teams.front());
        return plan;
    }

    if (mode == LineupSplit::Fair)
    {
        std::stable_sort(candidates.begin(), candidates.end(),
                         [priced](const Candidate& a, const Candidate& b) {
                             const long long lhs = priced ? a.power : a.level;
                             const long long rhs = priced ? b.power : b.level;
                             if (lhs != rhs)
                                 return lhs > rhs;  // strongest first
                             return a.slot < b.slot;
                         });
    }

    const int count = static_cast<int>(teams.size());
    for (std::size_t pick = 0; pick < candidates.size(); ++pick)
    {
        const int round = static_cast<int>(pick) / count;
        const int within = static_cast<int>(pick) % count;
        // EVEN deals round-robin; FAIR snakes, so the last pick of a round
        // is also the first pick of the next one.
        const int index = (mode == LineupSplit::Fair && (round % 2) == 1)
            ? count - 1 - within
            : within;
        plan.moves.emplace_back(candidates[pick].slot,
                                teams[static_cast<std::size_t>(index)]);
    }
    return plan;
}

int apply_split(SaveData& save,
                std::span<const std::pair<int, short>> moves)
{
    int moved = 0;
    for (const auto& [slot, team] : moves)
    {
        const guy* member = (slot >= 0 && slot < MAX_TEAM_SIZE)
            ? save.team_list[static_cast<std::size_t>(slot)].get()
            : nullptr;
        if (member == nullptr || member->teamnum == team)
            continue;
        if (set_guy_team(save, slot, team))
            ++moved;
    }
    return moved;
}

// --- Networking machine rows ---

std::vector<NetworkingMachineRow> build_networking_machine_rows(
    std::span<const og::sim::LobbyPlayer> players,
    std::span<const std::uint8_t> local_player_indices,
    int label_chars)
{
    struct Machine {
        std::uint64_t key = 0;
        og::sim::LobbyMachineId machine_id = og::sim::kInvalidLobbyMachineId;
        std::uint8_t first_index = 0xff;
        std::string name;
        std::string company;
        std::vector<int> seats;
        bool is_host = false;
        bool is_local = false;
        bool all_ready = true;
    };
    std::vector<Machine> machines;
    for (std::size_t index = 0; index < players.size(); ++index)
    {
        const og::sim::LobbyPlayer& player = players[index];
        const std::uint64_t key = lobby_machine_group_key(player, index);
        auto it = std::find_if(machines.begin(), machines.end(),
                               [key](const Machine& m) { return m.key == key; });
        if (it == machines.end())
        {
            Machine machine;
            machine.key = key;
            machine.machine_id = player.machine_id;
            machine.first_index = player.player_index;
            machine.name = player.name;
            machine.company = player.company;
            machines.push_back(std::move(machine));
            it = std::prev(machines.end());
        }
        if (player.player_index < it->first_index)
        {
            // The machine is named by its LOWEST seat (§6): a later-listed
            // seat with a smaller P# renames the row.
            it->first_index = player.player_index;
            if (!player.company.empty())
                it->company = player.company;
            if (!player.name.empty())
                it->name = player.name;
        }
        if (it->company.empty())
            it->company = player.company;
        if (it->name.empty())
            it->name = player.name;
        it->seats.push_back(static_cast<int>(player.player_index) + 1);
        it->is_host = it->is_host || player.is_host;
        it->all_ready = it->all_ready && player.ready;
        it->is_local = it->is_local ||
            std::find(local_player_indices.begin(), local_player_indices.end(),
                      player.player_index) != local_player_indices.end();
    }
    std::stable_sort(machines.begin(), machines.end(),
                     [](const Machine& a, const Machine& b) {
                         return a.first_index < b.first_index;
                     });

    const std::size_t budget =
        label_chars > 0 ? static_cast<std::size_t>(label_chars) : 0u;
    std::vector<NetworkingMachineRow> rows;
    rows.reserve(machines.size());
    for (std::size_t index = 0; index < machines.size(); ++index)
    {
        Machine& machine = machines[index];
        std::sort(machine.seats.begin(), machine.seats.end());
        // The COMPANY leads (§6): the transport `name` is an opaque
        // net-<hex> identity on a relay lobby, so it is only the fallback
        // for a machine that has not named a company yet.
        const std::string identity = machine.company.empty()
            ? clip_chars(machine.name, 10)
            : clip_chars(machine.company, 16);
        std::string head = std::format("M{}", index + 1);
        if (!identity.empty())
            head += " " + identity;
        if (machine.is_host)
            head += " (HOST)";
        if (machine.is_local)
            head += " (YOU)";
        std::string seats;
        for (const int seat : machine.seats)
        {
            if (!seats.empty())
                seats += ' ';
            seats += std::format("P{}", seat);
        }
        if (!seats.empty())
            seats = "  " + seats;
        // Whole-token degradation, cheapest fact first: READY is derivable
        // from the colour the row draws in, the seat list is detail, and
        // only the identity itself is ever clipped mid-word.
        std::string label = head + seats;
        if (machine.all_ready)
            label += "  READY";
        if (label.size() > budget)
            label = head + seats;
        if (label.size() > budget)
            label = head;
        if (label.size() > budget)
            label = clip_chars(std::move(label), budget);

        NetworkingMachineRow row;
        row.machine_id = machine.machine_id;
        row.label = std::move(label);
        row.is_host = machine.is_host;
        row.is_local = machine.is_local;
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace og::ui
