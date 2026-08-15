/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The scripted-campaign picker session (issue #206,
// docs/campaign-scripting-design.md "The picker contract"). SDL-free: the
// page-fetch/select/act state machine every surface (SDL, text, curses)
// drives. All navigation state lives here in C++ — the Lua hook is pure
// (page id in, page description out) and a page is fetched ONCE per
// navigation or action, never per frame.
//
// The session decorates fetched pages against the SaveData it borrows
// (CLEARED/CURRENT level markers, scenario-title label fill, action
// affordability) and owns the cost debit for priced actions (debit first,
// then dispatch — the C++-owns-the-cost rule). It never writes
// save.scen_num: a Level choice surfaces as Outcome::SetLevel and each
// client applies its own level-set tail (host gating included — the
// session stays policy-free).

#include <openglad/gameplay/script/campaign_hooks.h>

#include <cstddef>
#include <string>
#include <vector>

class SaveData;

namespace og::ui {

class CampaignPickerSession {
public:
    using Kind = og::script::hooks::CampaignPageEntry::Kind;

    // Page depth bound from the design doc ("page depth <= 4").
    static constexpr int kMaxPageDepth = 4;

    // One selectable row, decorated for display. `label` is the script's
    // label, or the scenario title (save-side fill) for level rows that
    // ship without one.
    struct Row {
        std::string id;
        std::string label;
        std::string note;
        Kind kind = Kind::Level;
        int level = 0;
        int cost = 0;
        // Level-row decoration from the save (false for other kinds).
        bool cleared = false;
        bool current = false;
        // Action-row affordability (cost == 0 or the acting team's wallet
        // covers it; always true for other kinds). Surfaces show refused
        // rows rather than hiding them — the terminal listing contract.
        bool affordable = true;
        [[nodiscard]] bool is_level() const { return kind == Kind::Level; }
    };

    struct DecoratedPage {
        std::string title;
        std::vector<std::string> lines;
        std::vector<Row> rows;
    };

    enum class OutcomeKind : std::uint8_t {
        None,        // no-op (bad index)
        OpenedPage,  // page() now shows the pushed page
        SetLevel,    // caller applies its client-specific level-set tail
        Acted,       // action ran; page() already refetched; toast may wait
        Refused,     // nothing happened; `reason` names why
    };

    struct Outcome {
        OutcomeKind kind = OutcomeKind::None;
        int level = 0;       // SetLevel: the chosen scenario id
        std::string reason;  // Refused
    };

    // Borrows the save for decoration and the wallet; keep the session
    // shorter-lived than the SaveData (the provider-install discipline).
    explicit CampaignPickerSession(SaveData& save);

    // Fetch the root page. False = no scripted picker (not registered,
    // hook errored, malformed page) — callers fall back to the stock UI.
    bool open();

    [[nodiscard]] bool is_open() const { return !stack_.empty(); }
    [[nodiscard]] const DecoratedPage& page() const { return page_; }
    // Page-stack depth; the root page is depth 1.
    [[nodiscard]] int depth() const { return static_cast<int>(stack_.size()); }

    // Activate row `entry_index` of the current page. Kind::Page pushes and
    // fetches (depth-capped); Kind::Level answers SetLevel WITHOUT touching
    // the save; Kind::Action debits first (skipped under infinite gold),
    // dispatches picker_action, stores the toast, and refetches the current
    // page so labels/state re-derive.
    Outcome choose(std::size_t entry_index);

    // Pop one page and refetch it. False when already at the root (the
    // caller closes the picker) — or when the refetch fails, which closes
    // the session (a malformed page answers "no scripted picker").
    bool back();

    // Refetch the current page in place (after a caller-applied level set,
    // so CURRENT markers re-derive). Fetch-per-action, never per frame.
    void refresh();

    // The pending action toast, cleared by the read ("" = none).
    std::string take_message();

private:
    bool fetch(const std::string& page_id, DecoratedPage& out) const;

    SaveData& save_;
    std::vector<std::string> stack_;  // page ids; front() is the "" root
    DecoratedPage page_;
    std::string message_;
};

// One row as a text line: label, note, cost ("60g") and the CLEARED/CURRENT
// level markers, clipped to `budget` chars. Shared by the surfaces so the
// SDL faces and the terminal listings agree on decoration.
std::string campaign_picker_row_text(const CampaignPickerSession::Row& row,
                                     std::size_t budget);

} // namespace og::ui
