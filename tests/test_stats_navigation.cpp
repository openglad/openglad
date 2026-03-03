#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include "test_framework.h"

#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> make_walker(char family)
{
    guy g(family);
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
        w->setxy(100, 100);
    return w;
}

void test_stats_navigation_blocked_helpers_and_follow_fallback()
{
    auto w = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    if (!w)
        return;

    // Exercise the directional switch tables used by movement heuristics.
    const unsigned char dirs[] = {FACE_UP,        FACE_UP_RIGHT,  FACE_RIGHT,     FACE_DOWN_RIGHT,
                                  FACE_DOWN,      FACE_DOWN_LEFT, FACE_LEFT,      FACE_UP_LEFT,
                                  (unsigned char)99};
    for (unsigned char d : dirs) {
        w->curdir = static_cast<char>(d);
        (void)w->stats()->forward_blocked();
        (void)w->stats()->right_forward_blocked();
        (void)w->stats()->right_back_blocked();
    }

    // Cover COMMAND_FOLLOW selection logic without relying on any pre-existing
    // view controls (other tests may clear them).
    struct ScreenGuard
    {
        int old_numviews;
        walker* old_c0;
        walker* old_c1;
        int old_yo0;
        int old_yo1;
        explicit ScreenGuard(screen* s)
            : old_numviews(s->numviews)
            , old_c0(nullptr)
            , old_c1(nullptr)
            , old_yo0(0)
            , old_yo1(0)
        {
            if (s->viewob[0]) {
                old_c0 = s->viewob[0]->control;
                if (old_c0)
                    old_yo0 = old_c0->yo_delay;
            }
            if (s->viewob[1]) {
                old_c1 = s->viewob[1]->control;
                if (old_c1)
                    old_yo1 = old_c1->yo_delay;
            }
        }
        ~ScreenGuard()
        {
            og::runtime::current_session->myscreen_->numviews = old_numviews;
            if (og::runtime::current_session->myscreen_->viewob[0]) {
                og::runtime::current_session->myscreen_->viewob[0]->control = old_c0;
                if (old_c0)
                    old_c0->yo_delay = old_yo0;
            }
            if (og::runtime::current_session->myscreen_->viewob[1]) {
                og::runtime::current_session->myscreen_->viewob[1]->control = old_c1;
                if (old_c1)
                    old_c1->yo_delay = old_yo1;
            }
        }
        ScreenGuard(const ScreenGuard&) = delete;
        ScreenGuard& operator=(const ScreenGuard&) = delete;
    } guard(og::runtime::current_session->myscreen_);

    if (!(og::runtime::current_session->myscreen_->viewob[0]))
        return;

    // Always cover the numviews==1 branch deterministically.
    auto view0_control = make_walker(FAMILY_SOLDIER);
    TEST_ASSERT(view0_control != nullptr, "view0 control created");
    if (!view0_control)
        return;
    view0_control->setxy(200, 200);
    og::runtime::current_session->myscreen_->viewob[0]->control = view0_control.get();
    og::runtime::current_session->myscreen_->numviews = 1;

    w->foe = nullptr;
    w->leader = nullptr;
    w->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
    (void)w->stats()->do_command();

    // Accept either outcome; the code may clear leader when already too close.
    TEST_ASSERT((w->leader == nullptr) || (w->leader == og::runtime::current_session->myscreen_->viewob[0]->control),
                "single-view follow should be stable");

    // Optionally cover the 2-view branch where neither view has yo_delay.
    if (og::runtime::current_session->myscreen_->viewob[1]) {
        auto view1_control = make_walker(FAMILY_SOLDIER);
        if (view1_control) {
            view1_control->setxy(300, 300);
            og::runtime::current_session->myscreen_->viewob[1]->control = view1_control.get();
            og::runtime::current_session->myscreen_->viewob[0]->control->yo_delay = 0;
            og::runtime::current_session->myscreen_->viewob[1]->control->yo_delay = 0;
            og::runtime::current_session->myscreen_->numviews = 2;

            w->foe = nullptr;
            w->leader = nullptr;
            w->stats()->force_command(COMMAND_FOLLOW, 1, 0, 0);
            (void)w->stats()->do_command();
            TEST_ASSERT(w->leader == nullptr, "two-view follow with no yo_delay should not pick a leader");
        }
    }
}
REGISTER_TEST(test_stats_navigation_blocked_helpers_and_follow_fallback);
