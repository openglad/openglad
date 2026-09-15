// EXPECTED VALUES for og::parity::event_kind_symbol — a test pin, not a rule.
//
// The one rule lives in tests/parity/state_dump.cpp::event_kind_symbol; this
// table is what tests/parity/test_state_dump_symbols.cpp expects that renderer
// to produce, so a silent rename of a symbol (a golden-byte producer, see
// state_dump.cpp's event dump) goes red here instead of quietly rewriting
// every golden that carries the old name.
//
// Adding an EventKind enumerator fails the -Werror=switch build in
// state_dump.cpp FIRST (that switch has no `default:` arm on purpose); only
// then does it need a row here.
#pragma once

#include <openglad/gameplay/event.h>

#include <iterator>
#include <string_view>
#include <utility>

inline constexpr std::pair<og::sim::EventKind, std::string_view>
    kEventKindSymbolPins[] = {
        {og::sim::EventKind::None,                    "none"},
        {og::sim::EventKind::PlaySound,               "play_sound"},
        {og::sim::EventKind::Notification,            "notification"},
        {og::sim::EventKind::SetPalette,              "set_palette"},
        {og::sim::EventKind::RequestRedraw,           "request_redraw"},
        {og::sim::EventKind::EndGame,                 "end_game"},
        {og::sim::EventKind::DamageTile,              "damage_tile"},
        {og::sim::EventKind::SetEnd,                  "set_end"},
        {og::sim::EventKind::RequestExitConfirmation, "request_exit_confirmation"},
        {og::sim::EventKind::WithdrawToLevel,         "withdraw_to_level"},
        {og::sim::EventKind::ScoreChange,             "score_change"},
        {og::sim::EventKind::DamageNumber,            "damage_number"},
};

static_assert(std::size(kEventKindSymbolPins) == 12,
              "a new EventKind first fails -Werror=switch in state_dump.cpp, "
              "then needs a row here");
