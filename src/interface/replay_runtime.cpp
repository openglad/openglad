#include <openglad/interface/replay_runtime.h>

#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/io_common.h>

#include <algorithm>
#include <vector>

namespace
{
constexpr std::string_view kLatestReplayFilename = "last-replay.ogr";

class ScopedReplayGameplayLoadActivation
{
public:
    explicit ScopedReplayGameplayLoadActivation(og::runtime::SessionState* session)
        : session_(session)
        , previous_(session_ ? session_->gameplay_active_ : false)
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = true;
    }

    ~ScopedReplayGameplayLoadActivation()
    {
        if (session_ != nullptr)
            session_->gameplay_active_ = previous_;
    }

    ScopedReplayGameplayLoadActivation(
        const ScopedReplayGameplayLoadActivation&) = delete;
    ScopedReplayGameplayLoadActivation& operator=(
        const ScopedReplayGameplayLoadActivation&) = delete;

private:
    og::runtime::SessionState* session_ = nullptr;
    bool previous_ = false;
};

std::vector<short> replay_view_teams(const og::sim::WorldSnapshot& snapshot,
                                     short max_views)
{
    std::vector<short> teams;
    teams.reserve(static_cast<std::size_t>(std::max<short>(max_views, 0)));

    for (const auto& guy_snapshot : snapshot.guy_snapshots)
    {
        if (guy_snapshot.teamnum == 0)
            continue;
        if (std::find(teams.begin(), teams.end(), guy_snapshot.teamnum) != teams.end())
            continue;

        teams.push_back(guy_snapshot.teamnum);
        if (static_cast<short>(teams.size()) >= max_views)
            break;
    }

    return teams;
}

void assign_replay_views(screen& game_screen,
                         const og::sim::WorldSnapshot& snapshot)
{
    const std::vector<short> teams =
        replay_view_teams(snapshot, game_screen.numviews);
    short view_index = 0;
    const short numviews = std::min<short>(
        game_screen.numviews,
        static_cast<short>(std::size(game_screen.viewob)));

    for (auto& view : game_screen.viewob)
    {
        if (!view || view_index >= numviews)
            break;

        view->my_team = (view_index < static_cast<short>(teams.size()))
            ? teams[static_cast<std::size_t>(view_index)]
            : 0;
        view->control = nullptr;
        ++view_index;
    }
}

std::filesystem::path replay_output_path()
{
    const std::string replay_dir = get_user_path() + "replays/";
    (void)create_dir(replay_dir);
    return std::filesystem::path(replay_dir) / kLatestReplayFilename;
}
} // namespace

bool og::runtime::initialize_replay_screen(screen& game_screen,
                                           og::sim::ReplayPlayer& player)
{
    const og::sim::ReplayHeader& header = player.header();
    const og::sim::WorldSnapshot& initial_snapshot = player.initial_snapshot();

    if (header.campaign_id.empty())
        return false;

    if (mount_campaign_package_with_error(header.campaign_id) !=
        CampaignPackageIoError::None)
    {
        return false;
    }

    ScopedReplayGameplayLoadActivation gameplay_load_active(og::runtime::current_session);
    const short desired_views = (header.player_count == 0)
        ? 1
        : static_cast<short>(header.player_count);

    game_screen.save_data.reset();
    game_screen.save_data.current_campaign = header.campaign_id;
    game_screen.save_data.current_levels[header.campaign_id] =
        static_cast<short>(header.level_id);
    game_screen.save_data.scen_num = static_cast<short>(header.level_id);
    game_screen.save_data.numplayers = header.player_count;

    game_screen.numviews = desired_views;
    game_screen.cleanup(desired_views);
    game_screen.initialize_views();
    game_screen.sync_world_from_save_data();

    game_screen.world().id = header.level_id;
    game_screen.world().rng_.state_ = header.initial_rng_state;
    if (!game_screen.load_level())
        return false;

    game_screen.sync_world_from_save_data();
    game_screen.world().difficulty =
        static_cast<short>(og::ui::difficulty_percent(
            og::runtime::current_session->current_difficulty_));
    for (auto& uptr : game_screen.world().oblist)
    {
        if (walker* w = uptr.get(); w != nullptr)
            w->set_difficulty(static_cast<Uint32>(w->stats()->level()));
    }

    og::sim::apply_snapshot(game_screen.world(), initial_snapshot);
    assign_replay_views(game_screen, initial_snapshot);
    game_screen.world().timer_wait = header.timer_wait;
    player.reset();
    return true;
}

void og::runtime::begin_replay_recording(screen& game_screen)
{
    if (og::runtime::current_session == nullptr)
        return;

    og::runtime::current_session->replay_recorder_.reset();
    og::runtime::current_session->replay_output_path_.clear();

    const std::string& campaign_id = game_screen.save_data.current_campaign;
    if (campaign_id.empty())
        return;

    og::runtime::current_session->replay_output_path_ = replay_output_path();
    og::runtime::current_session->replay_recorder_.emplace(og::sim::ReplayHeader{
        .version = og::sim::kReplayFormatVersion,
        .initial_rng_state = game_screen.world().rng_.state_,
        .level_id = game_screen.world().id,
        .player_count = static_cast<std::uint8_t>(game_screen.save_data.numplayers),
        .timer_wait = game_screen.world().timer_wait,
        .campaign_id = campaign_id,
    });
    og::runtime::current_session->replay_recorder_->record_initial_world(
        game_screen.world());
    og::runtime::current_session->replay_recorder_->record_input(
        game_screen.world().tick_count_ + 1u,
        InputState{});
}

void og::runtime::record_replay_input(screen& game_screen, const InputState& input)
{
    if (og::runtime::current_session == nullptr ||
        !og::runtime::current_session->replay_recorder_.has_value())
    {
        return;
    }

    og::runtime::current_session->replay_recorder_->record_input(
        game_screen.world().tick_count_ + 1u,
        input);
}

void og::runtime::finish_replay_recording()
{
    if (og::runtime::current_session == nullptr ||
        !og::runtime::current_session->replay_recorder_.has_value())
    {
        return;
    }

    og::sim::ReplayIoError io_error = og::sim::ReplayIoError::None;
    if (!og::runtime::current_session->replay_output_path_.empty() &&
        !og::runtime::current_session->replay_recorder_->write_file(
            og::runtime::current_session->replay_output_path_,
            &io_error))
    {
        LogError("replay_record_write_failed path={} error={}\n",
                 og::runtime::current_session->replay_output_path_.string(),
                 static_cast<int>(io_error));
    }

    og::runtime::current_session->replay_recorder_.reset();
}
