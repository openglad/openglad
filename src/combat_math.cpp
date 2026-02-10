#include "combat_math.h"

#include <cmath>

float compute_base_damage(float base_damage, RandomU32 rng)
{
    if (!rng)
        rng = [](Uint32 x) -> Uint32 { return (x == 0) ? 0u : 0u; };

    float d = base_damage;
    float sqrtd = sqrtf(d);
    // floor(sqrtd) from original implementation; random(0) should produce 0.
    return d - sqrtd / 2.0f + static_cast<float>(rng(static_cast<Uint32>(floorf(sqrtd))));
}

float compute_damage_reduction(float incoming_damage, float target_armor)
{
    if (incoming_damage <= 0)
        return 0;

    float reduction = target_armor / 2.0f;
    if (reduction > incoming_damage - 1)
        return incoming_damage - 1; // Always do at least 1 damage
    return reduction;
}

float compute_post_reduction_damage(float incoming_damage, float target_armor)
{
    float result = incoming_damage - compute_damage_reduction(incoming_damage, target_armor);
    if (result < 0)
        return 0;
    return result;
}

