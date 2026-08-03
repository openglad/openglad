/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The SDL-free per-level game loop for the ncurses client. Local single-player
 * runs through the engine's own client-server architecture exactly like every
 * other OpenGlad game: an in-process GameServer ticks the authoritative world
 * and broadcasts snapshots; an in-process GameClient applies them to a mirror
 * world the renderer reads. This mirrors src/server/server_main.cpp (the
 * dedicated headless host) with a co-located client added.
 */
#include <openglad/platform/curses/curses_game_runtime.h>

#include <openglad/platform/curses/clock.h>
#include <openglad/platform/curses/curses_input.h>
#include <openglad/platform/curses/curses_renderer.h>
#include <openglad/platform/curses/terminal.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_client.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gparser.h> // cfg
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/progression.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/headless_server_runtime.h>
#include <openglad/server/headless_tick_interval.h>

#include <algorithm>
#include <memory>

namespace og::curses {

namespace {

// Pull human-readable notification text out of an event batch into `out`.
void collect_notifications(const og::sim::SimEventBatch& batch,
                           std::vector<std::string>& out)
{
    for (const og::sim::Event& ev : batch.events) {
        if (ev.kind == og::sim::EventKind::Notification && !ev.text.empty())
            out.push_back(ev.text);
    }
}

// Latched level-end state. The authoritative end (win/loss/exit/withdraw) is
// delivered as an EndGame/SetEnd game-flow event; we must NOT store it in the
// mirror world's end/ending fields, because the very next delta snapshot
// re-serializes the server world's end=0 (in return-to-lobby mode the server
// never sets its own world.end) and would clobber it. Latching in the session
// instead is durable.
struct PendingEnd {
    bool ended = false;
    short ending = 0;
    short next_level = -1;
};

// Apply terminal game-flow events: latch any level end into `end` and collect
// notification text. (In the SDL client this is screen::dispatch_sim_event_batch.)
void apply_game_flow_batch(const og::sim::SimEventBatch& batch, PendingEnd& end,
                           std::vector<std::string>& messages)
{
    for (const og::sim::Event& ev : batch.events) {
        switch (ev.kind) {
        case og::sim::EventKind::EndGame:
            end.ended = true;
            end.ending = static_cast<short>(static_cast<std::int32_t>(ev.a));
            end.next_level = static_cast<short>(static_cast<std::int32_t>(ev.b));
            break;
        case og::sim::EventKind::SetEnd:
            end.ended = true;
            break;
        case og::sim::EventKind::Notification:
            if (!ev.text.empty())
                messages.push_back(ev.text);
            break;
        default:
            break;
        }
    }
}

// Mirror update_primary_team_totals() from headless_server_runtime.cpp.
void update_primary_team_totals(SaveData& save)
{
    const int team = (save.my_team >= 0 && save.my_team < MAX_PLAYERS) ? save.my_team : 0;
    save.score = save.m_score[team];
    save.totalcash = save.m_totalcash[team];
    save.totalscore = save.m_totalscore[team];
}

// A complete in-process single-player session: authoritative server + co-located
// mirror client, both headless.
class LocalCursesSession final : public CursesGameSession
{
public:
    static std::unique_ptr<LocalCursesSession> create(SaveData& save, int difficulty,
                                                      std::string* error);

    ~LocalCursesSession() override
    {
        // Restore whatever gameplay context was active before this session ran.
        current_game = saved_game_;
    }

    void send_input(const InputState& input) override
    {
        pending_input_ = input;
        have_input_ = true;
    }

    void advance() override
    {
        GameWorld& sw = server_world();
        // The authoritative tick runs with the server's gameplay context active,
        // so server entity movement touches the server world's obmap.
        current_game = &server_ctx_;
        if (have_input_) {
            client_->send_input(pending_input_, sw.tick_count_ + 1);
            have_input_ = false;
        }
        server_->step();
        // Applying snapshots moves mirror entities; switch to the mirror context
        // so those updates touch the *mirror* obmap, not the server's.
        current_game = &client_ctx_;
        client_->poll_messages();
    }

    GameWorld& mirror_world() override { return client_level_->world(); }

    std::uint32_t followed_entity_id() const override
    {
        const auto& ids = client_->controlled_entity_ids();
        if (ids[0] != 0)
            return ids[0];
        // Fallback: the first living entity the local player controls.
        for (const auto& up : client_level_->world().oblist) {
            const walker* w = up.get();
            if (w && !w->dead() && w->user() >= 0)
                return w->entity_id();
        }
        return 0;
    }

    std::uint32_t next_input_tick() const override
    {
        return server_world().tick_count_ + 1;
    }

    bool ended() const override
    {
        return pending_end_.ended || client_level_->world().game_ended;
    }
    int ending() const override
    {
        return pending_end_.ended ? pending_end_.ending : client_level_->world().ending;
    }
    int next_level() const override
    {
        return pending_end_.ended ? pending_end_.next_level
                                  : client_level_->world().next_level;
    }

    void request_abort() override { client_->request_level_abort(); }

    std::vector<std::string> drain_messages() override
    {
        return std::exchange(messages_, {});
    }

    bool exit_prompt_pending() const override { return pending_exit_prompt_; }
    std::string exit_prompt_text() const override { return exit_prompt_text_; }
    void respond_to_exit_prompt(bool accept) override
    {
        if (!pending_exit_prompt_)
            return;
        pending_exit_prompt_ = false;
        exit_prompt_text_.clear();
        client_->send_exit_prompt_response(accept);
    }

    void commit_result_to_save() override
    {
        // The end state (win vs loss/exit/withdraw) is carried on the mirror by
        // the EndGame event the server forwarded; only a win (ending == 0)
        // advances the campaign and persists earned XP/gold. On a loss or
        // withdraw we leave the caller's save untouched so the player keeps the
        // roster they entered the level with (a defeat would otherwise persist an
        // empty team, since update_guys() drops dead members).
        if (!ended() || ending() != 0)
        {
            // Non-win exit (team wipe / timeout / quit-mission / withdraw —
            // the abort round-trips through the in-process server before the
            // loop breaks, so it latches ending==1 here): route the mode's
            // run-end policy exactly once (Classic: no-op).
            if (ended())
            {
                og::mode::LevelOutcome run_outcome;
                run_outcome.ending = static_cast<short>(ending());
                run_outcome.next_level = static_cast<short>(next_level());
                run_outcome.withdrawn = next_level() >= 0;
                og::mode::current_progression().on_run_ended(
                    server_save_, server_world(), run_outcome);
            }
            return; // only a win advances the campaign and persists earned XP/gold
        }

        // The CTF loss/rematch shape rides the classic WIN shape (ending==0,
        // next_level == this level) but the match was LOST: treat it like
        // every other loss so the level is never marked completed.
        if (is_mode_rematch_end(server_world(), ending(), next_level()))
            return;

        advance_save_after_win(server_save_, server_world(), next_level());

        // Persist back into the caller's save (team XP/levels, gold, progress).
        og::server::copy_headless_server_save_data(save_ref_, server_save_);
    }

#ifdef TESTING
    bool inject_exit_prompt_for_testing(std::string prompt,
                                        std::uint32_t synchronized_score);
#endif

private:
    LocalCursesSession(SaveData& save) : save_ref_(save) {}

    GameWorld& server_world() { return server_level_->world(); }
    const GameWorld& server_world() const { return server_level_->world(); }

    // Caller's save (written back by commit_result_to_save()).
    SaveData& save_ref_;

    // Server (authoritative) side.
    SaveData server_save_;
    std::unique_ptr<LevelRuntimeData> server_level_;
    og::sim::SimEventLog server_events_;
    GameplayContext server_ctx_;
    IRandom* server_rng_ptr_ = nullptr;
    bool server_active_ = true;
    std::shared_ptr<og::sim::InProcessTransport> server_transport_;
    std::shared_ptr<og::sim::InProcessTransport> client_transport_;
    std::unique_ptr<og::sim::GameServer> server_;
    og::sim::PeerId peer_id_ = 0;

    // Client (mirror) side. Has its own gameplay context so that mirror entity
    // updates register in the *mirror* world's obmap, never the server's.
    SaveData client_save_;
    std::unique_ptr<LevelRuntimeData> client_level_;
    og::sim::SimEventLog client_events_;
    GameplayContext client_ctx_;
    IRandom* client_rng_ptr_ = nullptr;
    bool client_active_ = true;

    std::unique_ptr<og::sim::GameClient> client_;

    GameplayContext* saved_game_ = nullptr;
    PendingEnd pending_end_;
    std::vector<std::string> messages_;
    InputState pending_input_;
    bool have_input_ = false;

    // Exit-prompt state: latched when the server asks to leave the level, cleared
    // when the player answers via respond_to_exit_prompt().
    bool pending_exit_prompt_ = false;
    std::string exit_prompt_text_;
};

std::unique_ptr<LocalCursesSession> LocalCursesSession::create(SaveData& save,
                                                               int difficulty,
                                                               std::string* error)
{
    auto set_error = [&](const char* msg) {
        if (error)
            *error = msg;
        return nullptr;
    };

    // factory: private ctor — make_unique not applicable
    std::unique_ptr<LocalCursesSession> s(new LocalCursesSession(save));
    og::server::copy_headless_server_save_data(s->server_save_, save);
    og::server::copy_headless_server_save_data(s->client_save_, save);

    const short level = s->server_save_.scen_num > 0 ? s->server_save_.scen_num : 1;

    // --- Server world: load level + spawn team (authoritative) ---
    s->server_level_ = std::make_unique<LevelRuntimeData>(
        level, true, &headless_level_data_hooks());
    GameWorld& sw = s->server_level_->world();
    s->server_rng_ptr_ = &sw.rng_;
    s->server_level_->set_sim_context(&s->server_save_, &sw.enemy_freeze,
                                      &s->server_events_, s->server_rng_ptr_, &cfg);
    s->server_ctx_.world = &sw;
    s->server_ctx_.save = &s->server_save_;
    s->server_ctx_.sim_events = &s->server_events_;
    s->server_ctx_.config = &cfg;
    s->server_ctx_.session_rng_ref = &s->server_rng_ptr_;
    s->server_ctx_.gameplay_active_ref = &s->server_active_;

    // Activate the server context for the level load (spawns run sim code).
    s->saved_game_ = current_game;
    current_game = &s->server_ctx_;

    if (!og::server::load_headless_level_from_save(*s->server_level_, s->server_save_,
                                                   difficulty, s->server_events_,
                                                   /*authoritative=*/true)) {
        current_game = s->saved_game_;
        return set_error("failed to load level for local game");
    }

    // --- Client mirror world: load the same level (grid + smoother), entities
    // are then replaced by the authoritative keyframe ---
    s->client_level_ = std::make_unique<LevelRuntimeData>(
        level, true, &headless_level_data_hooks());
    GameWorld& cw = s->client_level_->world();
    s->client_rng_ptr_ = &cw.rng_;
    s->client_level_->set_sim_context(&s->client_save_, &cw.enemy_freeze,
                                      &s->client_events_, s->client_rng_ptr_, &cfg);
    s->client_ctx_.world = &cw;
    s->client_ctx_.save = &s->client_save_;
    s->client_ctx_.sim_events = &s->client_events_;
    s->client_ctx_.config = &cfg;
    s->client_ctx_.session_rng_ref = &s->client_rng_ptr_;
    s->client_ctx_.gameplay_active_ref = &s->client_active_;
    // Load the mirror under the mirror's own context so its walkers register in
    // the mirror obmap (not the server's, which would collide with the player).
    current_game = &s->client_ctx_;
    if (!og::server::load_headless_level_from_save(*s->client_level_, s->client_save_,
                                                   difficulty, s->client_events_,
                                                   /*authoritative=*/false)) {
        current_game = s->saved_game_;
        return set_error("failed to load mirror level for local game");
    }

    // --- Transports + server ---
    s->server_transport_ = og::sim::InProcessTransport::create_server();
    s->server_transport_->accept_connections();
    s->server_ = std::make_unique<og::sim::GameServer>(sw, s->server_events_,
                                                       *s->server_transport_);
    s->server_->set_return_to_lobby_mode(true);
    s->server_->on_save_sync = [s_raw = s.get()] {
        og::server::sync_headless_server_save_data_from_world(
            s_raw->server_save_, s_raw->server_world());
    };
    // In return-to-lobby mode these hooks only need to confirm the request so the
    // server forwards the terminal EndGame to every display (the curses client
    // ends the level and returns to team build rather than reloading in-session).
    s->server_->on_withdraw_accepted = [](int) { return true; };
    s->server_->on_exit_accepted = [](int) { return true; };
    s->server_->on_level_transition = [](int) { return true; };

    s->client_transport_ = s->server_transport_->create_client_transport();
    s->peer_id_ = s->client_transport_->local_peer_id();
    s->server_->connect_client(s->peer_id_);
    s->server_->bind_player(s->peer_id_, 0,
                            static_cast<short>(s->server_save_.my_team), nullptr);

    // --- Client mirror ---
    s->client_ = std::make_unique<og::sim::GameClient>(*s->client_transport_,
                                                       s->peer_id_, &cw);
    auto* raw = s.get();
    s->client_->set_sim_event_batch_callback(
        [raw](const og::sim::SimEventBatch& b) { collect_notifications(b, raw->messages_); });
    s->client_->set_game_flow_event_batch_callback(
        [raw](const og::sim::SimEventBatch& b) {
            apply_game_flow_batch(b, raw->pending_end_, raw->messages_);
        });
    // Latch exit prompts so the level loop can ask the player y/n (they may
    // DECLINE and keep playing). The server holds the transition until we answer.
    s->client_->set_exit_prompt_callback(
        [raw](const og::sim::ExitPromptBroadcastMessage& msg) {
            raw->pending_exit_prompt_ = true;
            raw->exit_prompt_text_ =
                msg.prompt_text.empty() ? std::string("Exit the level?") : msg.prompt_text;
        });
    s->client_->send_client_ready();

    // Exchange the initial keyframe so the mirror world is populated before the
    // first render, swapping contexts so each side touches its own obmap.
    for (int i = 0; i < 4; ++i) {
        current_game = &s->server_ctx_;
        s->server_->step();
        current_game = &s->client_ctx_;
        s->client_->poll_messages();
    }

    return s;
}

} // namespace

std::unique_ptr<CursesGameSession> make_local_session(SaveData& save, int difficulty,
                                                      std::string* error)
{
    if (error)
        error->clear();
    return LocalCursesSession::create(save, difficulty, error);
}

#ifdef TESTING
#include "../../../tests/curses/curses_game_runtime_internal.inc"
#endif

std::string mission_verdict_line(const GameRunResult& result,
                                 const SaveData* save, const GameWorld* world)
{
    std::string verdict;
    if (result.mode_match) {
        if (result.mode_winner_team < 0 || result.local_team < 0)
            verdict = "Match over.";
        else
            verdict = result.local_team == result.mode_winner_team ? "Victory!"
                                                                  : "Defeat.";
    } else {
        verdict = result.ending == 0 ? "Victory!" : "Defeat.";
    }

    // Mode summary hook (Classic: empty). Appended, space-joined, when the
    // caller supplies the finished save/world pair.
    if (save != nullptr && world != nullptr) {
        for (const std::string& line :
             og::mode::current_progression().results_summary_lines(*save, *world))
        {
            if (line.empty())
                continue;
            verdict += ' ';
            verdict += line;
        }
    }
    return verdict;
}

bool is_mode_rematch_end(const GameWorld& world, int ending, int next_level)
{
    // Thin (world, ending, next) adapter over the shared predicate: the
    // curses call sites read the finished level id from the mirror world.
    return ending == 0 &&
           og::progression::mode_rematch_shape(world,
                                              static_cast<short>(world.id),
                                              static_cast<short>(next_level));
}

void advance_save_after_win(SaveData& save, const GameWorld& finished_world, int next_level)
{
    og::server::sync_headless_server_save_data_from_world(save, finished_world);

    // The shared win fold with this site's quirks preserved: a ZERO time
    // bonus (the curses client never awarded one — do not unify bonus math
    // here) and caller-resolved next_level < 0 -> scen_num + 1.
    og::progression::WinFoldContext fold_ctx;
    fold_ctx.rematch_shape = og::progression::mode_rematch_shape(
        finished_world, save, static_cast<short>(next_level));
    fold_ctx.finished_level = save.scen_num; // pre-advance cursor
    fold_ctx.outcome.ending = 0;
    fold_ctx.outcome.next_level = static_cast<short>(
        next_level >= 0 ? next_level : (save.scen_num + 1));
    og::progression::apply_win_fold(save, finished_world, fold_ctx);

    update_primary_team_totals(save);
}

namespace {

// Draw the exit-confirmation prompt over the current frame and block for the
// player's answer: y/Enter accept, n/Esc decline. Exhausted scripted input (in
// tests) counts as decline. The player is never forced through an exit.
bool ask_exit_prompt(ITerminal& term, CursesRenderer& renderer,
                     CursesGameSession& session, const LevelLoopOptions& opt)
{
    if (opt.render) {
        renderer.draw(term, session.mirror_world(), session.followed_entity_id(),
                      session.follow_engaged());
        std::string line = " ";
        line += session.exit_prompt_text();
        line += "   [y]es / [n]o ";
        term.put_str(term.rows() / 2, 0, line, Color::Yellow, Color::Blue, true);
        term.present();
    }
    for (;;) {
        const Key key = term.poll_key(true);
        if (key.is_none())
            return false; // no more scripted input -> decline (stay in the level)
        if (!key.is_press())
            continue; // ignore key-up / repeat / focus
        if (key.is_char(U'y') || key.is_char(U'Y') || key.is_enter())
            return true;
        if (key.is_char(U'n') || key.is_char(U'N') || key.code == KeyCode::Escape)
            return false;
    }
}

} // namespace

GameRunResult run_level_loop(CursesGameSession& session, ITerminal& term, IClock& clock,
                             CursesInput& input, CursesRenderer& renderer,
                             const LevelLoopOptions& opt)
{
    GameRunResult result;

    int frames = 0;
    for (;;) {
        if (opt.max_frames >= 0 && frames >= opt.max_frames)
            break;

        // The server asks before leaving the level and holds the transition until
        // we answer. Handle it BEFORE reading movement input — so the y/n answer is
        // never consumed as movement, and so a frozen mission ignores movement —
        // and let the player DECLINE and keep playing (never auto-accept).
        if (session.exit_prompt_pending()) {
            const bool accept = ask_exit_prompt(term, renderer, session, opt);
            session.respond_to_exit_prompt(accept);
            input.reset(); // drop keys held during the prompt so movement is clean
        }

        // --- input ---
        // Esc is the only key the loop intercepts (open in-game menu / withdraw).
        // Every other key is fed to CursesInput, which resolves it through the
        // player's real keybindings — nothing about which key does what is decided
        // here.
        bool quit_to_menu = false;
        for (;;) {
            const Key key = term.poll_key(false);
            if (key.is_none())
                break;
            if (CursesInput::meta_for_key(key) == MetaAction::OpenGameMenu) {
                // Esc opens the menu / withdraws — act on the press only (a
                // release event must not re-trigger it), and swallow it either way
                // since it is not a player binding.
                if (key.is_press())
                    quit_to_menu = true;
            } else {
                input.feed(key);
            }
        }
        if (quit_to_menu) {
            session.request_abort();
            result.withdrew = true;
        }

        const InputState st = input.sample();
        session.send_input(st);
        session.advance();

        for (std::string& msg : session.drain_messages())
            renderer.log_message(std::move(msg));

        if (opt.render)
            renderer.draw(term, session.mirror_world(), session.followed_entity_id(),
                          session.follow_engaged());

        ++frames;

        // A deliberate quit/withdraw returns to the menu without a win/loss
        // verdict; check it before the natural end so a withdraw (which the
        // server also reports as a level end) is not mislabeled a defeat.
        if (quit_to_menu)
            break;
        if (session.ended()) {
            result.ended = true;
            result.ending = session.ending();
            result.next_level = session.next_level();
            // Mode verdict context from the replicated mirror state: the
            // winner, the followed walker's team (match end revives every
            // player corpse, so it normally resolves alive), and whether
            // this is the loss/rematch shape.
            GameWorld& world = session.mirror_world();
            if ((world.type & GameWorld::TYPE_SCRIPTED) && world.mode.active) {
                result.mode_match = true;
                result.mode_winner_team = world.mode.winner_team;
                result.mode_rematch =
                    is_mode_rematch_end(world, result.ending, result.next_level);
                const std::uint32_t followed = session.followed_entity_id();
                const walker* avatar =
                    followed != 0 ? world.find_by_id(followed) : nullptr;
                if (avatar != nullptr)
                    result.local_team = avatar->team_num();
            }
            break;
        }

        if (!opt.no_pacing) {
            const std::uint32_t interval = og::server::compute_headless_tick_interval_ms(
                static_cast<short>(session.mirror_world().timer_wait));
            clock.sleep_ms(std::max<std::uint32_t>(interval, 1));
        }
    }

    session.commit_result_to_save();
    return result;
}

} // namespace og::curses
