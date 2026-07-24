#include <openglad/platform/sai2x.h>

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
    CampaignEntry entry("org.openglad.test.draw-entry-does-not-exist", -1);
    entry.title = "Rendered Campaign";
    entry.version = "2.5";
    entry.rating = 3.5f;
    entry.authors = "OpenGlad Team";
    entry.contributors = "Test Contributors";
    entry.description = "First line\nSecond line";
    entry.suggested_power = 12;
    entry.num_levels = 1;
    entry.num_levels_completed = -1;

    const UiRect area{144, 35, 32, 32};
    entry.draw(area, 8);

    entry.rating = 0.0f;
    entry.suggested_power = 0;
    entry.num_levels = 2;
    entry.num_levels_completed = 1;
    entry.authors.clear();
    entry.contributors.clear();
    entry.description = "one\ntwo\nthree\nfour\nfive\nsix\nseven";
    entry.draw(area, -1);

    entry.suggested_power = 4;
    entry.draw(area, -1);
    entry.draw(area, 8);

    const auto* const rendered = static_cast<const unsigned char*>(
        surface_restore.surface->pixels);
    return std::equal(surface_restore.bytes.begin(), surface_restore.bytes.end(),
                      rendered)
               ? 1
               : 0;
}
