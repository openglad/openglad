#include <openglad/platform/sai2x.h>

#include <atomic>

namespace
{
std::atomic<std::uint64_t> s_campaign_picker_entered_count{0};
std::atomic<std::uint64_t> s_campaign_picker_action_count{0};
std::atomic<bool> s_campaign_picker_abort_pending{false};
} // namespace

void campaign_picker_testing_input_reset()
{
    s_campaign_picker_entered_count.store(0, std::memory_order_release);
    s_campaign_picker_action_count.store(0, std::memory_order_release);
    s_campaign_picker_abort_pending.store(false, std::memory_order_release);
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

    entry.draw(8);

    entry.rating = 0.0f;
    entry.suggested_power = 0;
    entry.num_levels = 2;
    entry.num_levels_completed = 1;
    entry.authors.clear();
    entry.contributors.clear();
    entry.description = "one\ntwo\nthree\nfour\nfive\nsix\nseven";
    entry.draw(-1);

    entry.suggested_power = 4;
    entry.draw(-1);
    entry.draw(8);

    const auto* const rendered = static_cast<const unsigned char*>(
        surface_restore.surface->pixels);
    return std::equal(surface_restore.bytes.begin(), surface_restore.bytes.end(),
                      rendered)
               ? 1
               : 0;
}
