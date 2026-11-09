#include <openglad/interface/game_context.h>
#include <openglad/platform/game_session.h>
#include <openglad/gameplay/gameplay_context.h>

#include <cassert>

void push_test_context(GameContext* context)
{
    assert(og::runtime::current_session != nullptr);
    og::runtime::SessionState& session = *og::runtime::current_session;
    GameContext& active = ctx();
    if (!session.test_context_active_) {
        session.test_context_rng_snapshot_ = active.rng;
        session.test_context_input_snapshot_ = active.input;
        session.test_context_active_ = true;
    }

    if (context) {
        if (context->rng)
            active.rng = context->rng;
        active.input = context->input;
        set_gameplay_rng_override(&context->rng);
    } else {
        set_gameplay_rng_override(nullptr);
    }
}

void pop_test_context()
{
    assert(og::runtime::current_session != nullptr);
    og::runtime::SessionState& session = *og::runtime::current_session;
    if (!session.test_context_active_)
        return;

    GameContext& active = ctx();
    active.rng = session.test_context_rng_snapshot_;
    active.input = session.test_context_input_snapshot_;
    session.test_context_active_ = false;
    set_gameplay_rng_override(nullptr);
}
