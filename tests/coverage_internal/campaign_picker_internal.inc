#include <openglad/platform/sai2x.h>

#include <atomic>

namespace
{
std::atomic<std::uint64_t> s_campaign_picker_entered_count{0};
std::atomic<std::uint64_t> s_campaign_picker_action_count{0};
std::atomic<bool> s_campaign_picker_abort_pending{false};
// Default ON: an un-driven browser accepts the current campaign after its
// first presented frame (pick_campaign). The integration listener re-arms it
// at every test start; tests that drive the browser disarm it.
std::atomic<bool> s_campaign_picker_auto_accept{true};
} // namespace

void campaign_picker_testing_set_auto_accept(bool enabled)
{
    s_campaign_picker_auto_accept.store(enabled, std::memory_order_release);
}

bool campaign_picker_testing_auto_accept()
{
    return s_campaign_picker_auto_accept.load(std::memory_order_acquire);
}

void campaign_picker_testing_input_reset()
{
    s_campaign_picker_entered_count.store(0, std::memory_order_release);
    s_campaign_picker_action_count.store(0, std::memory_order_release);
    s_campaign_picker_abort_pending.store(false, std::memory_order_release);
    // A test resetting the browser's input is about to DRIVE it.
    s_campaign_picker_auto_accept.store(false, std::memory_order_release);
}

void campaign_picker_testing_abort()
{
    s_campaign_picker_abort_pending.store(true, std::memory_order_release);
}

std::uint64_t campaign_picker_testing_entered_count()
{
    return s_campaign_picker_entered_count.load(std::memory_order_acquire);
}

std::uint64_t campaign_picker_testing_action_count()
{
    return s_campaign_picker_action_count.load(std::memory_order_acquire);
}

bool campaign_picker_testing_take_abort()
{
    return s_campaign_picker_abort_pending.exchange(
        false, std::memory_order_acquire);
}

void campaign_picker_testing_mark_entered()
{
    s_campaign_picker_entered_count.fetch_add(1, std::memory_order_release);
}

void campaign_picker_testing_mark_action()
{
    s_campaign_picker_action_count.fetch_add(1, std::memory_order_release);
}

// CampaignEntry::draw paints the detail pane's metadata rows. A pixel-diff
// against a pre-draw snapshot cannot tell them apart (the fitted title alone
// moves pixels), so pin the TEXT: render each arm, snapshot the three bands
// that hold nothing but metadata rows, then render ONLY the expected strings
// at the expected coordinates in the expected colors and compare the bands
// byte for byte. A dropped row, a wrong string, a shifted row or a wrong
// colour all show up. Return 0 on success; see the codes below.
//
//   2      = no render surface to read
//   10*N+1 = arm N's band bytes differ from the expected script
//   10*N+2 = arm N's expected script inked nothing (the pin would be vacuous)
int campaign_picker_testing_exercise_entry_draw_paths()
{
    screen& current_screen = *og::runtime::current_session->myscreen_;
    ScopedUiCanvas canvas_target(current_screen);

    struct RenderSurfaceRestore
    {
        SDL_Surface* surface = nullptr;
        std::vector<unsigned char> bytes;

        RenderSurfaceRestore()
        {
            surface = E_Screen ? E_Screen->render : nullptr;
            if (surface == nullptr || surface->pixels == nullptr)
                return;
            const std::size_t byte_count =
                static_cast<std::size_t>(surface->pitch) *
                static_cast<std::size_t>(surface->h);
            const auto* const first =
                static_cast<const unsigned char*>(surface->pixels);
            bytes.assign(first, first + byte_count);
        }

        ~RenderSurfaceRestore()
        {
            if (surface == nullptr || surface->pixels == nullptr ||
                E_Screen == nullptr || E_Screen->render != surface)
            {
                return;
            }
            const std::size_t byte_count =
                static_cast<std::size_t>(surface->pitch) *
                static_cast<std::size_t>(surface->h);
            if (byte_count == bytes.size())
                std::memcpy(surface->pixels, bytes.data(), bytes.size());
        }
    } surface_restore;

    if (surface_restore.surface == nullptr || surface_restore.bytes.empty())
        return 2;

    struct CampaignMountRestore
    {
        std::string campaign = get_mounted_campaign();
        ~CampaignMountRestore()
        {
            if (!campaign.empty() && get_mounted_campaign() != campaign)
                (void)mount_campaign_package_with_error(campaign);
        }
    } mount_restore;

    SDL_Surface* const surface = surface_restore.surface;
    auto* const pixels = static_cast<unsigned char*>(surface->pixels);
    const std::size_t pitch = static_cast<std::size_t>(surface->pitch);

    const og::ui::CampaignPickerLayout layout = og::ui::campaign_picker_layout();
    const int cx = layout.title_center_x;
    const int row_pitch = 8;              // draw()'s y += 8 between rows
    const int first_row_y = layout.icon.y + layout.icon.h + 4;
    text& loadtext = current_screen.text_normal;

    // The three bands draw() writes text into and nothing else: the title /
    // version rows above the icon button, the power / completion / authors
    // rows between the icon button and the description box, and the
    // contributors row below the description box.
    struct Band { int y; int h; };
    const Band bands[3] = {
        {layout.title_y, (layout.icon.y - 2) - layout.title_y},
        {first_row_y, layout.desc_box.y - first_row_y},
        {layout.more_button.y, surface->h - layout.more_button.y},
    };

    const auto clear_bands = [&]() {
        for (const Band& band : bands)
        {
            for (int row = band.y; row < band.y + band.h && row < surface->h;
                 ++row)
            {
                std::memset(pixels + pitch * static_cast<std::size_t>(row), 0,
                            pitch);
            }
        }
    };
    const auto snapshot_bands = [&]() {
        std::vector<unsigned char> out;
        for (const Band& band : bands)
        {
            for (int row = band.y; row < band.y + band.h && row < surface->h;
                 ++row)
            {
                const unsigned char* const first =
                    pixels + pitch * static_cast<std::size_t>(row);
                out.insert(out.end(), first, first + pitch);
            }
        }
        return out;
    };

    struct Line
    {
        int x = 0;
        int y = 0;
        std::string text;
        unsigned char color = 0;
    };
    const auto centered = [&](int y, std::string line, unsigned char color) {
        const int x = cx - static_cast<int>(line.size()) * 3;
        return Line{x, y, std::move(line), color};
    };

    // The deliberately absent package leaves a self-contained entry with no
    // icon allocation. Populate the same metadata shapes supplied by real
    // campaign.yaml files, then exercise each presentation arm directly.
    CampaignEntry entry("test.draw-entry-does-not-exist", -1);
    entry.title = "Rendered Campaign";
    entry.version = "2.5";
    entry.rating = 3.5f;
    entry.authors = "OpenGlad Team";
    entry.contributors = "Test Contributors";
    entry.description = "First line\nSecond line";
    entry.suggested_power = 12;
    entry.num_levels = 1;
    entry.num_levels_completed = -1;

    const auto run_arm = [&](int team_power, const std::vector<Line>& expected,
                             int arm) -> int {
        clear_bands();
        entry.draw(team_power);
        const std::vector<unsigned char> actual = snapshot_bands();

        clear_bands();
        for (const Line& line : expected)
        {
            loadtext.write_xy(line.x, line.y, line.text.c_str(), line.color,
                              static_cast<short>(1));
        }
        const std::vector<unsigned char> wanted = snapshot_bands();

        const bool inked = std::any_of(
            wanted.begin(), wanted.end(),
            [](unsigned char byte) { return byte != 0; });
        if (!inked)
            return 10 * arm + 2;
        return actual == wanted ? 0 : 10 * arm + 1;
    };

    // Arm 1: rated, versioned, a team below the suggested power (so the
    // Suggested row is RED), an uncounted level list, authors and
    // contributors. "Thanks to Test Contributors" is 27 chars against the
    // MORE-row budget of (more_button.x - desc_box.x - 6) / 6 = 21, so
    // fit_text_to_chars keeps 18 and appends "...".
    const int thanks_budget =
        (layout.more_button.x - layout.desc_box.x - 6) / 6;
    if (thanks_budget != 21)
        return 12;  // the pinned Thanks string below was derived from 21
    int arm_result = run_arm(
        8,
        {centered(layout.title_y, "Rendered Campaign", WHITE),
         Line{cx - 3 * 3, layout.title_y + 10, "***", WHITE},
         Line{cx + 3 * 3 + 6, layout.title_y + 10, "v2.5", WHITE},
         centered(first_row_y, "Your Power: 8", LIGHT_GREEN),
         centered(first_row_y + row_pitch, "Suggested Power: 12", RED),
         centered(first_row_y + 2 * row_pitch, "1 level", WHITE),
         centered(first_row_y + 3 * row_pitch, "By OpenGlad Team", WHITE),
         Line{layout.desc_box.x, layout.more_button.y + 2,
              "Thanks to Test Con...", WHITE}},
        1);
    if (arm_result != 0)
        return arm_result;

    // Arm 2: unrated (the version centers alone), no power row at all
    // (team_power < 0 AND suggested_power == 0), a counted completion line,
    // no authors and no contributors.
    entry.rating = 0.0f;
    entry.suggested_power = 0;
    entry.num_levels = 2;
    entry.num_levels_completed = 1;
    entry.authors.clear();
    entry.contributors.clear();
    entry.description = "one\ntwo\nthree\nfour\nfive\nsix\nseven";
    arm_result = run_arm(
        -1,
        {centered(layout.title_y, "Rendered Campaign", WHITE),
         centered(layout.title_y + 10, "v2.5", WHITE),
         centered(first_row_y, "1 out of 2 completed", WHITE)},
        2);
    if (arm_result != 0)
        return arm_result;

    // Arm 3: no team to compare against, so the Suggested row stands alone
    // and stays LIGHT_GREEN.
    entry.suggested_power = 4;
    arm_result = run_arm(
        -1,
        {centered(layout.title_y, "Rendered Campaign", WHITE),
         centered(layout.title_y + 10, "v2.5", WHITE),
         centered(first_row_y, "Suggested Power: 4", LIGHT_GREEN),
         centered(first_row_y + row_pitch, "1 out of 2 completed", WHITE)},
        3);
    if (arm_result != 0)
        return arm_result;

    // Arm 4: the same suggested power, now MET -- the row must turn
    // LIGHT_GREEN where arm 1's unmet row was RED.
    return run_arm(
        8,
        {centered(layout.title_y, "Rendered Campaign", WHITE),
         centered(layout.title_y + 10, "v2.5", WHITE),
         centered(first_row_y, "Your Power: 8", LIGHT_GREEN),
         centered(first_row_y + row_pitch, "Suggested Power: 4", LIGHT_GREEN),
         centered(first_row_y + 2 * row_pitch, "1 out of 2 completed", WHITE)},
        4);
}
