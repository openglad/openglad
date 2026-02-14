#include <openglad/sim/simulator.h>

#include "unit.h"

OG_UNIT_TEST(test_simulator_determinism_same_seed_same_inputs)
{
    og::sim::Simulator s1(123u);
    og::sim::Simulator s2(123u);

    for (std::uint32_t i = 0; i < 10; ++i)
    {
        og::sim::Input in;
        in.cmd = i % 3;
        in.value = i * 11u;
        s1.step(in);
        s2.step(in);
    }

    OG_ASSERT(s1.state().tick == s2.state().tick);
    OG_ASSERT(s1.state().acc == s2.state().acc);
    OG_ASSERT(s1.events() == s2.events());
}

OG_UNIT_TEST(test_simulator_determinism_different_seed_differs)
{
    og::sim::Simulator s1(1u);
    og::sim::Simulator s2(2u);

    og::sim::Input in;
    in.cmd = 7;
    in.value = 42;
    for (int i = 0; i < 5; ++i)
    {
        s1.step(in);
        s2.step(in);
    }

    OG_ASSERT(s1.state().acc != s2.state().acc);
    OG_ASSERT(!(s1.events() == s2.events()));
}

OG_UNIT_TEST(test_simulator_snapshot_determinism)
{
    og::sim::Simulator s1(42u);
    og::sim::Simulator s2(42u);

    og::sim::InputSnapshot snap;
    snap.players[0].cmd = 1;
    snap.players[0].value = 10;
    snap.players[1].cmd = 2;
    snap.players[1].value = 20;

    for (int i = 0; i < 20; ++i)
    {
        s1.step(snap, 1.0f / 60.0f);
        s2.step(snap, 1.0f / 60.0f);
    }

    OG_ASSERT(s1.state().tick == s2.state().tick);
    OG_ASSERT(s1.state().acc == s2.state().acc);
    OG_ASSERT(s1.events() == s2.events());
    OG_ASSERT(s1.state().tick == 20);
}

OG_UNIT_TEST(test_simulator_clear_events)
{
    og::sim::Simulator s(99u);

    og::sim::Input in;
    in.cmd = 1;
    in.value = 5;
    s.step(in);
    OG_ASSERT(s.events().size() == 1);

    s.clear_events();
    OG_ASSERT(s.events().empty());

    s.step(in);
    OG_ASSERT(s.events().size() == 1);
}

OG_UNIT_TEST(test_simulator_event_kinds)
{
    og::sim::Simulator s(1u);

    og::sim::Input in;
    in.cmd = static_cast<std::uint32_t>(og::sim::EventKind::Damage);
    in.value = 100;
    s.step(in);

    OG_ASSERT(s.events().size() == 1);
    OG_ASSERT(s.events()[0].kind == og::sim::EventKind::Damage);
    OG_ASSERT(s.events()[0].a == 100);
    OG_ASSERT(s.events()[0].tick == 1);
}
