#include <openglad/gameplay/game_client.h>

#include <algorithm>
#include <chrono>

namespace og::sim {

void GameClient::testing_set_render_interpolation_elapsed_ms(float elapsed_ms)
{
    last_snapshot_receive_time_ =
        InterpolationClock::now() -
        std::chrono::duration_cast<InterpolationClock::duration>(
            std::chrono::duration<float, std::milli>(
                std::max(elapsed_ms, 0.0f)));
}

void GameClient::testing_set_last_outbound_activity_elapsed_ms(float elapsed_ms)
{
    last_outbound_activity_time_ =
        InterpolationClock::now() -
        std::chrono::duration_cast<InterpolationClock::duration>(
            std::chrono::duration<float, std::milli>(
                std::max(elapsed_ms, 0.0f)));
}

void GameClient::testing_set_transport_disconnect_elapsed_ms(float elapsed_ms)
{
    connection_lost_notified_ = false;
    link_.backdate_loss(elapsed_ms, InterpolationClock::now());
}

} // namespace og::sim
