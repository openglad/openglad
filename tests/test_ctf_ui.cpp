#include <gtest/gtest.h>

#include <openglad/core/ctf_constants.h>
#include <openglad/core/order.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gloader_ctf.h>

loader* sdl_entity_loader();

// CTF HUD/results coverage lands with the UI phase. Until then, assert the
// SDL loader registers usable CTF treasure entries (sprite or fallback).
TEST(CtfUi, sdl_loader_has_ctf_entries)
{
    loader* game_loader = sdl_entity_loader();
    ASSERT_NE(nullptr, game_loader);
    ASSERT_NE(nullptr,
              game_loader->graphics_for(Order::Treasure, og::FAMILY_FLAG));
    ASSERT_NE(nullptr,
              game_loader->graphics_for(Order::Treasure, og::FAMILY_CTF_POINT));
}
