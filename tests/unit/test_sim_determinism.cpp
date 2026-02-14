#include <openglad/sim/simulator.h>

#include "unit.h"

static bool events_equal(const std::vector<og::sim::Event>& a, const std::vector<og::sim::Event>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].tick != b[i].tick) return false;
        if (a[i].kind != b[i].kind) return false;
        if (a[i].a != b[i].a) return false;
        if (a[i].b != b[i].b) return false;
    }
    return true;
}

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
    OG_ASSERT(events_equal(s1.events(), s2.events()));
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
    OG_ASSERT(!events_equal(s1.events(), s2.events()));
}

