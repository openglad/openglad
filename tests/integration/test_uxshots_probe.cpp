// Company and Base Camp visual smoke tests. Drives the feature screens through
// picker_main with injector threads and verifies that each composed 320x200
// canvas is nonblank. Set UXSHOTS_DIR to retain the frames as PPM artifacts;
// normal test runs perform the visual smoke assertions without writing files.

#include "test_input_helpers.h"
#include "test_interact.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../../src/interface/ui/picker_sdl_defs.h"

void picker_main(Sint32 argc, char **argv);
Sint32 create_team_menu(Sint32 arg1);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
extern std::atomic_bool g_test_present_pause_requested;
extern std::atomic_bool g_test_present_paused;
// Engine keyboard-nav hook (picker_input.cpp, TESTING only).
extern int g_test_menu_nav_key;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState &pks() {
  return *og::runtime::current_session->picker_;
}

namespace {

constexpr int kTeamMenuTimeoutMs = 20000;
// Coverage instrumentation plus host swap pressure can occasionally leave the
// presenter descheduled for several seconds in the middle of a frame. Keep the
// synchronization bounded without mistaking that host pressure for a blank
// frame; the enclosing CTest timeout remains the final 420-second backstop.
constexpr Uint64 kFramePauseTimeoutMs = 30000;

class PresentedFramePause {
public:
  PresentedFramePause() {
    bool expected = false;
    if (!g_test_present_pause_requested.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
      fprintf(stderr,
              "  [uxshot] FAILED: another frame capture is already pending\n");
      fflush(stderr);
      abort();
    }

    const Uint64 deadline = SDL_GetTicks() + kFramePauseTimeoutMs;
    while (!g_test_present_paused.load(std::memory_order_acquire)) {
      if (SDL_GetTicks() >= deadline) {
        fprintf(stderr,
                "  [uxshot] FAILED: no fully presented frame within %llu ms\n",
                static_cast<unsigned long long>(kFramePauseTimeoutMs));
        fflush(stderr);
        abort();
      }
      SDL_Delay(1);
    }
    acquired_ = true;
  }

  ~PresentedFramePause() {
    if (!acquired_)
      return;

    g_test_present_pause_requested.store(false, std::memory_order_release);
    const Uint64 deadline = SDL_GetTicks() + kFramePauseTimeoutMs;
    while (g_test_present_paused.load(std::memory_order_acquire)) {
      if (SDL_GetTicks() >= deadline) {
        fprintf(stderr,
                "  [uxshot] presenter did not resume within %llu ms\n",
                static_cast<unsigned long long>(kFramePauseTimeoutMs));
        fflush(stderr);
        abort();
      }
      SDL_Delay(1);
    }
  }

  PresentedFramePause(const PresentedFramePause &) = delete;
  PresentedFramePause &operator=(const PresentedFramePause &) = delete;

  [[nodiscard]] bool acquired() const { return acquired_; }

private:
  bool acquired_ = false;
};

// A frozen 320x200 frame, RGB triplets in row-major order.
using FramePixels = std::vector<Uint8>;
// Optional per-shot pixel oracle: runs on the copied frame, after the
// presenter has resumed. Returning false fails the capture.
using FrameCheck = bool (*)(const FramePixels &);

// Verify the current canvas and optionally dump it as a binary PPM. Runs on
// the injector thread. The presenter handshake freezes exactly one completed
// frame while its pixels are copied, so a blank-frame assertion cannot pass or
// fail based on overlap with the menu loop's next clear/redraw pass.
bool capture_frame(const char *name, FrameCheck check = nullptr) {
  screen *scr = og::runtime::current_session->myscreen_;
  const char *output_dir = std::getenv("UXSHOTS_DIR");
  std::string path;
  FramePixels rgb;
  if (output_dir != nullptr && output_dir[0] != '\0') {
    std::error_code error;
    std::filesystem::create_directories(output_dir, error);
    if (error) {
      fprintf(stderr, "  [uxshot] FAILED to create %s: %s\n", output_dir,
              error.message().c_str());
      return false;
    }
    path = std::string(output_dir) + "/" + name + ".ppm";
  }
  const bool keep_pixels = !path.empty() || check != nullptr;
  if (keep_pixels)
    rgb.reserve(320 * 200 * 3);

  std::size_t nonblack_pixels = 0;
  {
    PresentedFramePause frame_pause;
    if (!frame_pause.acquired())
      return false;

    for (int y = 0; y < 200; ++y) {
      for (int x = 0; x < 320; ++x) {
        Uint8 r = 0, g = 0, b = 0;
        scr->get_pixel(x, y, &r, &g, &b);
        if (r != 0 || g != 0 || b != 0)
          ++nonblack_pixels;
        if (keep_pixels) {
          rgb.push_back(r);
          rgb.push_back(g);
          rgb.push_back(b);
        }
      }
    }
  }

  if (check != nullptr && !check(rgb)) {
    fprintf(stderr, "  [uxshot] %s: pixel check FAILED\n", name);
    return false;
  }

  if (!path.empty()) {
    FILE *f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
      fprintf(stderr, "  [uxshot] FAILED to open %s\n", path.c_str());
      return false;
    }
    fprintf(f, "P6\n320 200\n255\n");
    fwrite(rgb.data(), sizeof(Uint8), rgb.size(), f);
    fclose(f);
    fprintf(stderr, "  [uxshot] wrote %s\n", path.c_str());
  }
  fprintf(stderr, "  [uxshot] %s: %zu nonblack pixels\n", name,
          nonblack_pixels);
  return nonblack_pixels >= 1000;
}

void cleanup_picker_state() {
  for (int i = 0; i < 5; i++) {
    pks().backdrops[static_cast<std::size_t>(i)].reset();
    pks().backpics[i].free();
  }
  clear_allbuttons();
  og::runtime::current_session->localbuttons_ = nullptr;
  pks().main_columns_pix.reset();
  pks().main_columns_data.free();
  pks().main_title_logo_pix.reset();
  pks().main_title_logo_data.free();
}

bool wait_for_team_menu(int timeout_ms = kTeamMenuTimeoutMs) {
  int elapsed = 0;
  while (elapsed < timeout_ms) {
    if (has_interactable("hire_troops") && has_interactable("networking"))
      return true;
    SDL_Delay(50);
    elapsed += 50;
  }
  return false;
}

struct RosterSeed {
  const char *name;
  int family;
  short level;
  bool deployed;
};

bool seed_company_with_roster(const std::string &slot, const std::string &name,
                              std::int64_t last_played,
                              const std::vector<RosterSeed> &roster) {
  SaveData sd;
  sd.reset();
  sd.save_name = name;
  sd.current_campaign = "gladiator";
  sd.last_played_unix_s = last_played;
  int i = 0;
  for (const RosterSeed &seed : roster) {
    auto g = std::make_unique<guy>(seed.family);
    g->name = seed.name;
    g->upgrade_to_level(seed.level, true);
    g->deployed = seed.deployed;
    sd.team_list[static_cast<std::size_t>(i)] = std::move(g);
    ++i;
  }
  sd.team_size = static_cast<unsigned char>(i);
  return sd.save_with_error(slot) == SaveDataIoError::None;
}

bool seed_company(const std::string &slot, const std::string &name,
                  std::int64_t last_played) {
  return seed_company_with_roster(slot, name, last_played, {});
}

// The user's playtest shape: soldiers in rows 0-1 (the former F6 black-top
// regression case), vivid families later, mixed benched rows (F7
// inconsistency repro).
std::vector<RosterSeed> playtest_roster() {
  return {
      {"GORT", FAMILY_SOLDIER, 3, true},   {"HALDOR", FAMILY_SOLDIER, 2, true},
      {"MERRIN", FAMILY_MAGE, 2, false},   {"SYLVA", FAMILY_ELF, 4, true},
      {"BRAND", FAMILY_ARCHER, 3, false},  {"KEZRA", FAMILY_CLERIC, 1, true},
      {"FLINT", FAMILY_THIEF, 2, true},    {"OONA", FAMILY_DRUID, 1, false},
      {"BONES", FAMILY_SKELETON, 2, true},
  };
}

struct CompanySlotCleanup {
  std::vector<std::string> slots;
  ~CompanySlotCleanup() {
    for (const std::string &slot : slots) {
      for (const og::data::CompanyBackupInfo &backup :
           og::data::list_company_backups(slot))
        (void)og::data::delete_company_backup(slot, backup.seq);
      (void)remove_user_file("save/" + slot + ".gtl");
    }
  }
};

// Remove every company file so the no-company main-menu variant shows.
void reap_all_companies() {
  for (const og::data::CompanyInfo &info : og::data::list_companies()) {
    for (const og::data::CompanyBackupInfo &backup :
         og::data::list_company_backups(info.slot))
      (void)og::data::delete_company_backup(info.slot, backup.seq);
    (void)remove_user_file("save/" + info.slot + ".gtl");
  }
}

struct ShotState {
  bool finished = false;
  int captures = 0;
};

// --- 1. main menu, no companies --------------------------------------------

int mainmenu_no_company_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  wait_for_interactable("begin_new_game", 5000);
  SDL_Delay(1500); // FadeWithInitialDraw settle
  state->captures += capture_frame("mainmenu_no_company");
  interact("quit");
  state->finished = true;
  return 0;
}

TEST(UxShots, a_mainmenu_no_company) {
  trace_clear();
  reap_all_companies();
  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(mainmenu_no_company_injector, "ux_mm_none", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 1;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(1, state.captures);
}

// --- 2. main menu, companies present ---------------------------------------

int mainmenu_with_company_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  wait_for_interactable("load_company", 5000);
  SDL_Delay(1500);
  state->captures += capture_frame("mainmenu_with_company");
  interact("quit");
  state->finished = true;
  return 0;
}

TEST(UxShots, b_mainmenu_with_company) {
  trace_clear();
  CompanySlotCleanup cleanup{{"uxmm1", "uxmm2"}};
  ASSERT_TRUE(seed_company_with_roster("uxmm1", "GREY WOLF COMPANY", 1000,
                                       playtest_roster()));
  ASSERT_TRUE(seed_company_with_roster("uxmm2", "IRON KETTLE BAND", 2000,
                                       playtest_roster()));
  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(mainmenu_with_company_injector, "ux_mm_some", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 1;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(1, state.captures);
}

// --- 2a. game settings ------------------------------------------------------
// The global CONTROLS capture retired with that screen: per-player controls
// are shot from the seat-settings probe below.

int game_settings_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  if (wait_for_interactable("options", 5000)) {
    SDL_Delay(1500);
    interact("options");
    if (wait_for_interactable("options_back", 5000)) {
      SDL_Delay(1500);
      state->captures += capture_frame("game_settings");
      SDL_Delay(300);
      interact("options_back");
    }
  }
  state->finished = true;
  return 0;
}

TEST(UxShots, b1_game_settings) {
  trace_clear();
  reap_all_companies();
  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(game_settings_injector, "ux_settings", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 1;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(1, state.captures);
}

// --- 2b. per-seat settings --------------------------------------------------
// This probe used to capture the main-menu PLAYER SETTINGS screen. Its
// successor is deliberately reached from an owned Base Camp card so the
// screenshot covers the stable-seat editor in its real context.

int seat_settings_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  if (wait_for_interactable("continue_game", 5000)) {
    SDL_Delay(1500);
    interact("continue_game");
    if (wait_for_team_menu() &&
        wait_for_interactable("seat_card_0", 5000)) {
      SDL_Delay(500);
      interact("seat_card_0");
    }
    if (wait_for_interactable("seat_settings_back", 5000)) {
      SDL_Delay(1500);
      state->captures += capture_frame("seat_settings");
      interact("seat_settings_back");
    }
    if (wait_for_team_menu()) {
      SDL_Delay(300);
      interact("back");
    }
  }
  // This probe permits one main-menu call. Base Camp BACK therefore returns
  // through picker_main's test gate instead of materializing another QUIT
  // face; there is nothing left for the injector to dismiss.
  state->finished = true;
  return 0;
}

TEST(UxShots, b2_seat_settings) {
  trace_clear();
  CompanySlotCleanup cleanup{{"save0"}};
  ASSERT_TRUE(seed_company_with_roster("save0", "IRON KETTLE BAND", 1700259200,
                                       playtest_roster()));
  ASSERT_TRUE(og::data::set_active_company_slot("save0"));
  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(seat_settings_injector, "ux_seat", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 1;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(1, state.captures);
}

// --- 3. name entry ----------------------------------------------------------

int name_entry_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  wait_for_interactable("begin_new_game", 5000);
  SDL_Delay(1500);
  interact("begin_new_game");
  if (wait_for_interactable("company_name_reroll", 5000)) {
    SDL_Delay(1500);
    state->captures += capture_frame("name_entry");
    interact("back");
  }
  if (wait_for_interactable("begin_new_game", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  state->finished = true;
  return 0;
}

TEST(UxShots, c_name_entry) {
  trace_clear();
  ShotState state;
  SDL_Thread *thread = SDL_CreateThread(name_entry_injector, "ux_name", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 2;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(1, state.captures);
}

// --- 4. company list (modest) ----------------------------------------------

int company_list_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  wait_for_interactable("load_company", 5000);
  SDL_Delay(1500);
  interact("load_company");
  if (wait_for_interactable("company_row_0", 5000)) {
    SDL_Delay(1500); // FadeAroundEntry settle
    state->captures += capture_frame("company_list");
    interact("back");
  }
  if (wait_for_interactable("begin_new_game", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  state->finished = true;
  return 0;
}

TEST(UxShots, d_company_list) {
  trace_clear();
  CompanySlotCleanup cleanup{{"uxcl1", "uxcl2", "uxcl3", "uxcl4"}};
  ASSERT_TRUE(seed_company_with_roster("uxcl1", "GREY WOLF COMPANY", 1700000000,
                                       playtest_roster()));
  ASSERT_TRUE(seed_company("uxcl2", "THE COPPER SHIELDS", 1700086400));
  ASSERT_TRUE(seed_company_with_roster("uxcl3", "RED LANTERN CREW", 1700172800,
                                       playtest_roster()));
  ASSERT_TRUE(seed_company_with_roster("uxcl4", "IRON KETTLE BAND", 1700259200,
                                       playtest_roster()));
  ASSERT_TRUE(og::data::set_active_company_slot("uxcl4"));
  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(company_list_injector, "ux_list", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 2;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(1, state.captures);
}

// --- 5. company list, paged -------------------------------------------------

int company_list_paged_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  wait_for_interactable("load_company", 5000);
  SDL_Delay(1500);
  interact("load_company");
  if (wait_for_interactable("company_page_next", 5000)) {
    SDL_Delay(1500);
    state->captures += capture_frame("company_list_paged_p1");
    interact("company_page_next");
    SDL_Delay(600);
    state->captures += capture_frame("company_list_paged_p2");
    SDL_Delay(200);
    interact("back");
  }
  if (wait_for_interactable("begin_new_game", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  state->finished = true;
  return 0;
}

TEST(UxShots, e_company_list_paged) {
  trace_clear();
  std::vector<std::string> slots;
  for (int i = 0; i < 11; ++i)
    slots.push_back("uxpg" + std::to_string(i));
  CompanySlotCleanup cleanup{slots};
  const char *names[11] = {
      "GREY WOLF COMPANY", "THE COPPER SHIELDS", "RED LANTERN CREW",
      "OAKEN VANGUARD",    "SILVER FANG PACT",   "THE BLACK BANNERS",
      "STORMCALLER GUILD", "EMBER LEGION",       "THE PALE RIDERS",
      "GOLDEN BOAR CLUB",  "IRON KETTLE BAND"};
  for (int i = 0; i < 11; ++i)
    ASSERT_TRUE(seed_company(slots[static_cast<std::size_t>(i)], names[i],
                             1700000000 + i * 86400));
  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(company_list_paged_injector, "ux_paged", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 2;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(2, state.captures);
}

// --- 6. backups sub-view ----------------------------------------------------

int backups_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  wait_for_interactable("load_company", 5000);
  SDL_Delay(1500);
  interact("load_company");
  if (wait_for_interactable("company_bak_0", 5000)) {
    SDL_Delay(1500);
    interact("company_bak_0");
    if (wait_for_interactable("backup_row_0", 5000)) {
      SDL_Delay(1500);
      state->captures += capture_frame("backups");
      interact("back");
    }
    if (wait_for_interactable("company_del_0", 5000)) {
      SDL_Delay(400);
      interact("back");
    }
  }
  if (wait_for_interactable("begin_new_game", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  state->finished = true;
  return 0;
}

TEST(UxShots, f_backups) {
  trace_clear();
  CompanySlotCleanup cleanup{{"uxbk1"}};
  // Three snapshots at different levels/timestamps.
  for (int i = 0; i < 3; ++i) {
    SaveData sd;
    sd.reset();
    sd.save_name = "IRON KETTLE BAND";
    sd.current_campaign = "gladiator";
    sd.last_played_unix_s = 1700000000 + i * 90000;
    sd.scen_num = static_cast<short>(1 + i * 2);
    const std::vector<RosterSeed> roster = playtest_roster();
    for (std::size_t j = 0; j < roster.size(); ++j) {
      const RosterSeed seed = roster[j];
      auto g = std::make_unique<guy>(seed.family);
      g->name = seed.name;
      g->upgrade_to_level(seed.level, true);
      g->deployed = seed.deployed;
      sd.team_list[j] = std::move(g);
    }
    sd.team_size = static_cast<unsigned char>(roster.size());
    ASSERT_EQ(SaveDataIoError::None, sd.save_with_error("uxbk1"));
    ASSERT_TRUE(og::data::backup_company_now("uxbk1"));
  }
  ShotState state;
  SDL_Thread *thread = SDL_CreateThread(backups_injector, "ux_bk", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 2;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(1, state.captures);
}

// --- 7-9. base camp: populated / empty / paged ------------------------------

struct NamedShot {
  ShotState state;
  const char *name;
};

int basecamp_shot_injector(void *data) {
  og::runtime::ensure_thread_session();
  NamedShot *shot = static_cast<NamedShot *>(data);
  wait_for_interactable("continue_game", 5000);
  SDL_Delay(1500);
  interact("continue_game");
  if (wait_for_team_menu()) {
    SDL_Delay(1500);
    {
      screen *scr = og::runtime::current_session->myscreen_;
      fprintf(stderr, "[diag] session save team_size=%d name='%s' scen=%d\n",
              (int)scr->save_data.team_size, scr->save_data.save_name.c_str(),
              (int)scr->save_data.scen_num);
    }
    shot->state.captures += capture_frame(shot->name);
    SDL_Delay(200);
    interact("back");
  }
  if (wait_for_interactable("begin_new_game", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  shot->state.finished = true;
  return 0;
}

int basecamp_paged_injector(void *data) {
  og::runtime::ensure_thread_session();
  NamedShot *shot = static_cast<NamedShot *>(data);
  wait_for_interactable("continue_game", 5000);
  SDL_Delay(1500);
  interact("continue_game");
  if (wait_for_team_menu()) {
    SDL_Delay(1500);
    fprintf(
        stderr, "  [uxshot] basecamp team_size=%d save_name='%s' pager=%d\n",
        static_cast<int>(
            og::runtime::current_session->myscreen_->save_data.team_size),
        og::runtime::current_session->myscreen_->save_data.save_name.c_str(),
        has_interactable("roster_page_next") ? 1 : 0);
    {
      const std::vector<og::sim::LobbyPlayer> players = picker_lobby_players();
      fprintf(stderr, "  [uxshot] lobby players=%d\n",
              static_cast<int>(players.size()));
      for (const auto &p : players)
        fprintf(stderr, "  [uxshot]   seat team=%d slots=%d company='%s'\n",
                static_cast<int>(p.team),
                static_cast<int>(p.character_slots.size()), p.company.c_str());
    }
    shot->state.captures += capture_frame("basecamp_paged_p1");
    if (has_interactable("roster_page_next")) {
      interact("roster_page_next");
      SDL_Delay(1500);
      shot->state.captures += capture_frame("basecamp_paged_p2");
      SDL_Delay(120);
      shot->state.captures += capture_frame("basecamp_paged_p2b");
    }
    SDL_Delay(200);
    interact("back");
  }
  if (wait_for_interactable("begin_new_game", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  shot->state.finished = true;
  return 0;
}

int basecamp_three_seats_injector(void *data) {
  og::runtime::ensure_thread_session();
  NamedShot *shot = static_cast<NamedShot *>(data);
  wait_for_interactable("continue_game", 5000);
  SDL_Delay(1500);
  interact("continue_game");
  if (wait_for_team_menu() &&
      wait_for_interactable("add_seat", 5000)) {
    SDL_Delay(300);
    interact("add_seat");
    if (wait_for_interactable("seat_card_1", 5000)) {
      SDL_Delay(300);  // cross the intentional 250 ms + debounce
      interact("add_seat");
    }
    if (wait_for_interactable("seat_card_2", 5000)) {
      SDL_Delay(1500);
      shot->state.captures += capture_frame(shot->name);
    }
    SDL_Delay(200);
    interact("back");
  }
  if (wait_for_interactable("begin_new_game", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  shot->state.finished = true;
  return 0;
}

void run_basecamp_shot(NamedShot &shot, int (*injector)(void *)) {
  SDL_Thread *thread = SDL_CreateThread(injector, "ux_bc", &shot);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 2;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(shot.state.finished);
  ASSERT_GE(shot.state.captures, 1);
}

TEST(UxShots, g_basecamp_solo) {
  trace_clear();
  CompanySlotCleanup cleanup{{"save0"}};
  ASSERT_TRUE(seed_company_with_roster("save0", "IRON KETTLE BAND", 1700259200,
                                       playtest_roster()));
  ASSERT_TRUE(og::data::set_active_company_slot("save0"));
  NamedShot shot;
  shot.name = "basecamp_solo";
  run_basecamp_shot(shot, &basecamp_shot_injector);
}

TEST(UxShots, h_basecamp_empty) {
  trace_clear();
  CompanySlotCleanup cleanup{{"save0"}};
  ASSERT_TRUE(seed_company("save0", "IRON KETTLE BAND", 1700259200));
  ASSERT_TRUE(og::data::set_active_company_slot("save0"));
  NamedShot shot;
  shot.name = "basecamp_empty";
  run_basecamp_shot(shot, &basecamp_shot_injector);
}

TEST(UxShots, i_basecamp_three_local_seats) {
  trace_clear();
  CompanySlotCleanup cleanup{{"save0"}};
  ASSERT_TRUE(seed_company_with_roster("save0", "IRON KETTLE BAND", 1700259200,
                                       playtest_roster()));
  ASSERT_TRUE(og::data::set_active_company_slot("save0"));
  NamedShot shot;
  shot.name = "basecamp_three_local_seats";
  run_basecamp_shot(shot, &basecamp_three_seats_injector);
}

// --- 10-12. networked base camp: host / joiner / degraded alert -------------
// A fake lobby client (the test_view_team HostVisibilityPickerLobbyClient
// pattern) drives the §2.5 networked draw arm without sockets: merged
// roster, line-B session header, alert precedence.

class FakeNetLobbyClient final : public og::ui::IPickerLobbyClient {
public:
  void initialize_from_save() override {}
  void shutdown() override {}
  void sync_from_save() override {}
  void sync_roster_from_save() override {}
  void sync_settings_from_save() override {}
  void poll_and_apply() override {}
  void set_player_mode(int) override {}
  bool request_start_game() override { return false; }
  [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
  build_game_start_config() const override {
    return std::nullopt;
  }
  [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
  consume_game_start_config() override {
    return std::nullopt;
  }
  [[nodiscard]] bool start_request_pending() const noexcept override {
    return false;
  }
  [[nodiscard]] bool host_controls_visible() const noexcept override {
    return host_view;
  }
  [[nodiscard]] std::optional<std::string> connection_alert() const override {
    return alert;
  }
  [[nodiscard]] std::vector<og::sim::LobbyPlayer>
  lobby_players() const override {
    return players;
  }
  [[nodiscard]] std::vector<std::uint8_t>
  local_player_indices() const override {
    return local_indices;
  }
  [[nodiscard]] bool is_networked_session() const noexcept override {
    return true;
  }
  [[nodiscard]] std::string session_room_code() const override {
    return room_code;
  }

  bool host_view = true;
  std::optional<std::string> alert;
  std::vector<og::sim::LobbyPlayer> players;
  std::vector<std::uint8_t> local_indices;
  std::string room_code = "GLAD-7Q2F";
};

struct ActiveLobbyGuard {
  og::ui::IPickerLobbyClient *saved;
  explicit ActiveLobbyGuard(og::ui::IPickerLobbyClient *client)
      : saved(og::ui::active_picker_lobby_client()) {
    og::ui::install_active_picker_lobby_client(client);
  }
  ~ActiveLobbyGuard() { og::ui::install_active_picker_lobby_client(saved); }
};

og::sim::LobbyPlayer make_probe_seat(std::uint8_t index, const char *name,
                                     const char *company, bool is_host,
                                     bool ready,
                                     const std::vector<RosterSeed> &roster,
                                     short team = 0,
                                     og::sim::LobbyMachineId machine_id =
                                         og::sim::kInvalidLobbyMachineId) {
  og::sim::LobbyPlayer player;
  player.player_index = index;
  player.seat_id = static_cast<og::sim::LobbySeatId>(index) + 1;
  player.machine_id = machine_id;
  player.name = name;
  player.company = company;
  player.team = team;
  player.is_host = is_host;
  player.ready = ready;
  std::uint8_t i = 0;
  for (const RosterSeed &seed : roster) {
    og::sim::LobbyCharacterSlot slot;
    slot.slot_index = i++;
    slot.deployed = seed.deployed;
    slot.character.name = seed.name;
    slot.character.family = static_cast<std::int8_t>(seed.family);
    slot.character.level = seed.level;
    slot.character.teamnum = team;
    player.character_slots.push_back(std::move(slot));
  }
  return player;
}

int basecamp_net_injector(void *data) {
  og::runtime::ensure_thread_session();
  NamedShot *shot = static_cast<NamedShot *>(data);
  if (wait_for_team_menu()) {
    SDL_Delay(1500);
    shot->state.captures += capture_frame(shot->name);
    SDL_Delay(200);
    interact("back");
  }
  shot->state.finished = true;
  return 0;
}

void seed_session_save_for_net() {
  SaveData &save = og::runtime::current_session->myscreen_->save_data;
  save.reset();
  save.save_name = "IRON KETTLE BAND";
  save.current_campaign = "gladiator";
  save.scen_num = 1;
  save.numplayers = 1;
  const std::vector<RosterSeed> own = {
      {"GORT", FAMILY_SOLDIER, 3, true}, {"HALDOR", FAMILY_SOLDIER, 2, true},
      {"SYLVA", FAMILY_ELF, 4, true},    {"KEZRA", FAMILY_CLERIC, 1, false},
      {"FLINT", FAMILY_THIEF, 2, true},
  };
  int i = 0;
  for (const RosterSeed &seed : own) {
    auto g = std::make_unique<guy>(seed.family);
    g->name = seed.name;
    g->upgrade_to_level(seed.level, true);
    g->deployed = seed.deployed;
    save.team_list[static_cast<std::size_t>(i++)] = std::move(g);
  }
  save.team_size = static_cast<unsigned char>(i);
}

std::vector<RosterSeed> foreign_roster() {
  return {
      {"WREN", FAMILY_ARCHER, 2, true},
      {"ASHA", FAMILY_MAGE, 3, true},
      {"PIP", FAMILY_THIEF, 1, false},
  };
}

void run_basecamp_net_shot(const char *name, bool host_view,
                           std::optional<std::string> alert) {
  trace_clear();
  seed_session_save_for_net();
  FakeNetLobbyClient client;
  client.host_view = host_view;
  client.alert = std::move(alert);
  if (host_view) {
    client.players = {
        make_probe_seat(0, "net-host", "IRON KETTLE BAND", true, false, {},
                        0, 1),
        make_probe_seat(1, "net-join", "JOIN RIVER BAND", false, false,
                        foreign_roster(), 0, 2),
    };
    client.local_indices = {0};
  } else {
    // Joiner view: the OTHER machine (the host, IRON KETTLE BAND art
    // aside) owns the foreign rows; our session save plays the joiner.
    og::runtime::current_session->myscreen_->save_data.save_name =
        "JOIN RIVER BAND";
    client.players = {
        make_probe_seat(0, "net-host", "IRON KETTLE BAND", true, false,
                        foreign_roster(), 0, 1),
        make_probe_seat(1, "net-join", "JOIN RIVER BAND", false, false, {},
                        0, 2),
    };
    client.local_indices = {1};
  }
  ActiveLobbyGuard guard(&client);
  NamedShot shot;
  shot.name = name;
  SDL_Thread *thread = SDL_CreateThread(basecamp_net_injector, "ux_net", &shot);
  ASSERT_TRUE(thread != nullptr);
  create_team_menu(0);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  ASSERT_TRUE(shot.state.finished);
  ASSERT_EQ(1, shot.state.captures);
}

TEST(UxShots, j_basecamp_net_host) {
  run_basecamp_net_shot("basecamp_net_host", true, std::nullopt);
}

TEST(UxShots, k_basecamp_net_join) {
  run_basecamp_net_shot("basecamp_net_join", false, std::nullopt);
}

TEST(UxShots, l_basecamp_net_alert) {
  run_basecamp_net_shot("basecamp_net_alert", false,
                        std::optional<std::string>("Status: connection lost"));
}

// Seven authoritative seats exercise the compact four-card rail, its pager,
// and the MATCHUP overview reached through SCENARIO (#236 retired the rail's
// duplicate SEATS door). Internal network names intentionally differ from
// public company names so the screenshots catch any accidental
// transport-identity leak.
int basecamp_many_seats_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  if (wait_for_team_menu()) {
    SDL_Delay(1500);
    state->captures += capture_frame("basecamp_net_seats_p1");
    if (wait_for_interactable("seat_page_next", 5000)) {
      interact("seat_page_next");
      SDL_Delay(750);
      state->captures += capture_frame("basecamp_net_seats_p2");
    }
    interact("scenario");
    if (wait_for_interactable("matchup", 5000)) {
      SDL_Delay(300);
      interact("matchup");
    }
    if (wait_for_interactable("cross_control", 5000)) {
      SDL_Delay(1000);
      state->captures += capture_frame("matchup_network_seats");
      // MATCHUP back lands on the SCENARIO submenu; its back returns to
      // Base Camp, whose back leaves the screen.
      interact("back");
      if (wait_for_interactable("view_scenario", 5000)) {
        SDL_Delay(300);
        interact("back");
      }
      if (wait_for_team_menu(5000)) {
        SDL_Delay(250);
        interact("back");
      }
    }
  }
  state->finished = true;
  return 0;
}

// --- VIEW LEVEL staged preview (#218, C10) ---------------------------------

// The band region the staged world renders into: (8,16)-(310,91) in classic
// coordinates (picker_sdl_defs kViewScenarioPreviewBand*). A healed staged
// pitch fills the band with terrain, so a blank band means the pane died.
bool check_view_level_staged_band(const FramePixels &rgb) {
  std::size_t nonblack = 0;
  for (int y = 16; y < 16 + 76; ++y) {
    for (int x = 8; x < 8 + 303; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * 320 +
                             static_cast<std::size_t>(x)) * 3;
      if (rgb[i] != 0 || rgb[i + 1] != 0 || rgb[i + 2] != 0)
        ++nonblack;
    }
  }
  if (nonblack < 1000) {
    fprintf(stderr, "  [uxshot] staged band nearly blank: %zu nonblack\n",
            nonblack);
    return false;
  }
  return true;
}

// The HIRE portrait box, sampled strictly inside its bevel: show_guy draws the
// bar at (77,30)-(125,64) for the HIRE layout (description_box {11,71,180,90}
// puts centerx at 101, centery at 47) and then draws the walker through
// viewob[0]. The bar interior is a flat fill, so "is the guy in the box" is
// exactly "how many distinct colors are in the box". Measured on this flow:
// 19 distinct with the portrait drawn (stable across runs), 1 when VIEW LEVEL
// leaked its camera onto viewob[0] and the sprite was clipped off the box —
// an empty box fails this check, so the threshold is an oracle, not a floor
// for its own sake.
// No byte golden is possible here: show_guy picks its direction and animation
// frame from query_timer(), so two good frames differ.
bool check_hire_portrait(const FramePixels &rgb) {
  std::set<std::uint32_t> colors;
  for (int y = 31; y <= 63; ++y) {
    for (int x = 78; x <= 124; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * 320 +
                             static_cast<std::size_t>(x)) * 3;
      colors.insert((static_cast<std::uint32_t>(rgb[i]) << 16) |
                    (static_cast<std::uint32_t>(rgb[i + 1]) << 8) |
                    static_cast<std::uint32_t>(rgb[i + 2]));
    }
  }
  if (colors.size() < 10) {
    fprintf(stderr,
            "  [uxshot] hire portrait box nearly flat: %zu distinct colors\n",
            colors.size());
    return false;
  }
  return true;
}

int view_level_staged_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  wait_for_interactable("continue_game", 5000);
  SDL_Delay(1500);
  interact("continue_game");
  if (wait_for_team_menu()) {
    SDL_Delay(500);
    interact("scenario");
    if (wait_for_interactable("view_scenario", 5000)) {
      SDL_Delay(300);
      interact("view_scenario");
      // The pane-heal trace is the "viewer is up and staged" signal.
      bool pane_up = false;
      for (int waited = 0; waited < 10000 && !pane_up; waited += 100) {
        pane_up = trace_contains("picker", "view_scenario pane gen=");
        SDL_Delay(100);
      }
      if (pane_up) {
        // Let the pan settle a few frames before freezing one.
        SDL_Delay(800);
        state->captures +=
            capture_frame("view_level_staged", &check_view_level_staged_band);
        SDL_Delay(200);
        interact("back");
      }
      if (wait_for_interactable("matchup", 5000)) {
        SDL_Delay(300);
        interact("back");
      }
    }
    // The reported symptom rides the SAME menu session: with the preview
    // camera left on viewob[0], the HIRE portrait walker draws off its box
    // and only the canvas-direct bevel survives.
    if (wait_for_team_menu(5000)) {
      SDL_Delay(250);
      interact("hire_troops");
      if (wait_for_interactable("hire_me", 10000)) {
        SDL_Delay(800);
        state->captures +=
            capture_frame("hire_after_view_level", &check_hire_portrait);
        SDL_Delay(200);
        interact("back");
      }
    }
    if (wait_for_team_menu(5000)) {
      SDL_Delay(250);
      interact("back");
    }
  }
  if (wait_for_interactable("begin_new_game", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  state->finished = true;
  return 0;
}

// The staged-lobby headline still: VIEW LEVEL's preview band presents the
// dormant staged soccer pitch above the census rows. Doubles as proof media
// (UXSHOTS_DIR) and the blank-frame guard for the pane.
TEST(UxShots, n_view_level_staged) {
  trace_clear();
  CompanySlotCleanup cleanup{{"save0"}};
  {
    SaveData sd;
    sd.reset();
    sd.save_name = "IRON KETTLE BAND";
    sd.current_campaign = "modes";
    sd.scen_num = 820;
    sd.current_levels["modes"] = 820;
    auto gort = std::make_unique<guy>(FAMILY_SOLDIER);
    gort->name = "GORT";
    gort->upgrade_to_level(3, true);
    gort->deployed = true;
    sd.team_list[0] = std::move(gort);
    auto sylva = std::make_unique<guy>(FAMILY_ELF);
    sylva->name = "SYLVA";
    sylva->upgrade_to_level(4, true);
    sylva->deployed = true;
    sd.team_list[1] = std::move(sylva);
    sd.team_size = 2;
    ASSERT_EQ(SaveDataIoError::None, sd.save_with_error("save0"));
  }
  ASSERT_TRUE(og::data::set_active_company_slot("save0"));

  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(view_level_staged_injector, "ux_staged", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 2;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  // Both shots: the staged band, then the HIRE portrait drawn in the same
  // menu session right after the viewer closed.
  ASSERT_EQ(2, state.captures);

  // The save0 load mounted the modes campaign; restore the default.
  (void)unmount_campaign_package_with_error(get_mounted_campaign());
  (void)mount_campaign_package_with_error("gladiator");
}

TEST(UxShots, m_basecamp_many_seats_and_matchup) {
  trace_clear();
  seed_session_save_for_net();
  FakeNetLobbyClient client;
  client.players = {
      make_probe_seat(0, "net-a0", "IRON KETTLE BAND", true, true, {}, 0, 1),
      make_probe_seat(1, "net-a1", "IRON KETTLE BAND", false, true, {}, 0, 1),
      make_probe_seat(2, "net-b0", "RED LANTERN CREW", false, true, {}, 1, 2),
      make_probe_seat(3, "net-b1", "RED LANTERN CREW", false, true, {}, 1, 2),
      make_probe_seat(4, "net-c0", "COPPER SHIELDS", false, false, {}, 2, 3),
      make_probe_seat(5, "net-d0", "BLACK BANNERS", false, true, {}, 2, 4),
      make_probe_seat(6, "net-d1", "BLACK BANNERS", false, true, {}, 3, 4),
  };
  client.local_indices = {0, 1};
  ActiveLobbyGuard guard(&client);
  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(basecamp_many_seats_injector, "ux_seats", &state);
  ASSERT_TRUE(thread != nullptr);
  create_team_menu(0);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(3, state.captures);
}

// --- 13. full-screen help (#168): all three tabs plus a paged state -------

// Folder-tab merge oracle (#201 polish). The active tab and the content frame
// must read as one surface: across the connector band the active tab's column
// carries the frame's face color, and the frame's top bevel line is broken
// there while it survives under every inactive tab.
struct ProbeRgb {
  Uint8 r = 0, g = 0, b = 0;
  bool operator==(const ProbeRgb &) const = default;
};

ProbeRgb frame_pixel(const FramePixels &rgb, int x, int y) {
  const std::size_t index =
      (static_cast<std::size_t>(y) * 320 + static_cast<std::size_t>(x)) * 3;
  if (index + 2 >= rgb.size())
    return {};
  return {rgb[index], rgb[index + 1], rgb[index + 2]};
}

// Mid-tab column: away from both edge bevels and from the outline's side runs.
int help_tab_probe_x(int tab) { return kHelpTabX(tab) + kHelpTabWidth / 2; }

// `outline_rows` is how many rows of the active tab's connector band the
// keyboard-focus outline is allowed to steal (its bottom run draws after the
// content pass): 0 with no focus, 1 when the active tab is focused.
bool check_help_tab_merge(const FramePixels &rgb, int active_tab,
                          int outline_rows) {
  // Reference colors are read off the frame itself rather than the palette:
  // the injector thread's session carries its own palette registers, and a
  // same-frame comparison stays valid through a fade. The face sample sits
  // deep inside the frame, the bevel sample on the frame's top line right of
  // the tab strip; a fully black (unrendered) frame collapses them and fails.
  const ProbeRgb face =
      frame_pixel(rgb, kHelpFrameRight - 4, kHelpFrameBottom - 4);
  const ProbeRgb bevel = frame_pixel(rgb, kHelpFrameRight - 20, kHelpFrameTop);
  if (face == bevel) {
    fprintf(stderr, "  [uxshot] help merge: face and bevel samples match\n");
    return false;
  }

  for (int tab = 0; tab < 3; ++tab) {
    const int probe_x = help_tab_probe_x(tab);
    int face_rows = 0;
    for (int y = kHelpConnectorTop; y <= kHelpConnectorBottom; ++y) {
      if (frame_pixel(rgb, probe_x, y) == face)
        ++face_rows;
    }
    const int band_rows = kHelpConnectorBottom - kHelpConnectorTop + 1;
    const int want = (tab == active_tab) ? band_rows - outline_rows : 0;
    if (face_rows != want) {
      fprintf(stderr,
              "  [uxshot] help merge: tab %d connector has %d face rows, "
              "expected %d\n",
              tab, face_rows, want);
      return false;
    }
    // The frame's top bevel line: broken under the active tab, intact under
    // the others.
    const bool bevel_intact =
        frame_pixel(rgb, probe_x, kHelpFrameTop) == bevel;
    if (bevel_intact == (tab == active_tab)) {
      fprintf(stderr, "  [uxshot] help merge: tab %d frame bevel intact=%d\n",
              tab, static_cast<int>(bevel_intact));
      return false;
    }
  }
  return true;
}

bool check_help_controls_merged(const FramePixels &rgb) {
  return check_help_tab_merge(rgb, kHelpMenuControlsTabIndex, 0);
}

bool check_help_classes_merged(const FramePixels &rgb) {
  return check_help_tab_merge(rgb, kHelpMenuClassesTabIndex, 0);
}

// The same frame with no keyboard focus, kept as the reference the focus shot
// diffs against.
FramePixels g_help_editor_unfocused;

bool check_help_editor_merged(const FramePixels &rgb) {
  g_help_editor_unfocused = rgb;
  return check_help_tab_merge(rgb, kHelpMenuEditorTabIndex, 0);
}

// With the EDITOR tab focused the merge must survive except for the one row
// the outline's bottom run occupies, and the outline's left, right and top
// runs must all still be visible around the tab. The outline color is taken
// from the frame — whatever the focused frame paints where the unfocused one
// did not — and all three surviving runs must carry it. The runs breathe with
// the pulse (0..3px outside the face), so each is searched over its band.
bool check_help_editor_focused(const FramePixels &rgb) {
  if (!check_help_tab_merge(rgb, kHelpMenuEditorTabIndex, 1))
    return false;
  if (g_help_editor_unfocused.size() != rgb.size()) {
    fprintf(stderr, "  [uxshot] help focus: no unfocused reference frame\n");
    return false;
  }
  const int tab_x = kHelpTabX(kHelpMenuEditorTabIndex);
  const int pulse = 3;
  const int run_top = kHelpTabY + 4;
  const int run_bottom = kHelpTabY + 11;

  // The left run is the outline's first appearance; its color anchors the
  // other two runs.
  ProbeRgb outline;
  bool found_left = false;
  for (int y = run_top; y <= run_bottom && !found_left; ++y) {
    for (int x = tab_x - pulse; x <= tab_x; ++x) {
      const ProbeRgb here = frame_pixel(rgb, x, y);
      if (here != frame_pixel(g_help_editor_unfocused, x, y)) {
        outline = here;
        found_left = true;
        break;
      }
    }
  }
  if (!found_left) {
    fprintf(stderr, "  [uxshot] help focus: outline left run missing\n");
    return false;
  }
  auto run_found = [&](int x0, int x1, int y0, int y1) {
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x)
        if (frame_pixel(rgb, x, y) == outline)
          return true;
    return false;
  };
  if (!run_found(tab_x + kHelpTabWidth, tab_x + kHelpTabWidth + pulse, run_top,
                 run_bottom)) {
    fprintf(stderr, "  [uxshot] help focus: outline right run missing\n");
    return false;
  }
  // Above the tab: at the top of its swing the horizontal run can sit one row
  // off-canvas, so the band spans the whole outline width — the side runs
  // reach these rows too, and either way the outline still closes above the
  // tab. Nothing in the connector can reach this far up.
  if (!run_found(tab_x - pulse, tab_x + kHelpTabWidth + pulse, 0, kHelpTabY)) {
    fprintf(stderr, "  [uxshot] help focus: outline top run missing\n");
    return false;
  }
  return true;
}

// One keyboard-nav step, applied through the engine's testing hook (real key
// events can't be driven from an injector thread).
void help_nav_step(int key) {
  g_test_menu_nav_key = key;
  SDL_Delay(200);
}

// Park the pointer over the content frame. interact() leaves the cursor on the
// button it clicked, and that hover highlight is the same yellow as the
// keyboard-focus outline — the focus shot can only be told apart from its
// reference once no button is hovered.
void help_park_pointer() {
  const auto [win_x, win_y] = ui_canvas_to_window(300.0f, 100.0f);
  inject_mouse_motion(static_cast<int>(win_x), static_cast<int>(win_y));
  SDL_Delay(200);
}

int help_screen_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  wait_for_interactable("help", 5000);
  SDL_Delay(1500); // FadeWithInitialDraw settle on the main menu
  interact("help");
  if (wait_for_interactable("help_tab_classes", 5000)) {
    SDL_Delay(500);
    state->captures +=
        capture_frame("help_controls", &check_help_controls_merged);
    if (wait_for_interactable("help_page_next", 2000)) {
      SDL_Delay(300);
      interact("help_page_next");
      SDL_Delay(500);
      state->captures += capture_frame("help_controls_p2");
    }
    SDL_Delay(300);
    interact("help_tab_classes");
    SDL_Delay(500);
    state->captures +=
        capture_frame("help_classes", &check_help_classes_merged);
    SDL_Delay(300);
    interact("help_tab_editor");
    SDL_Delay(500);
    help_park_pointer();
    state->captures += capture_frame("help_editor", &check_help_editor_merged);
    // Walk the keyboard highlight from BACK onto the active (EDITOR) tab.
    help_nav_step(KEY_UP);
    help_nav_step(KEY_RIGHT);
    help_nav_step(KEY_RIGHT);
    state->captures +=
        capture_frame("help_editor_focus", &check_help_editor_focused);
    SDL_Delay(300);
    interact("help_back");
  }
  if (wait_for_interactable("begin_new_game", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  state->finished = true;
  return 0;
}

TEST(UxShots, n_help_screen) {
  trace_clear();
  reap_all_companies();
  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(help_screen_injector, "ux_help", &state);
  ASSERT_TRUE(thread != nullptr);
  g_picker_mainmenu_calls = 0;
  g_picker_max_mainmenu_calls = 2;
  picker_main(0, nullptr);
  SDL_WaitThread(thread, nullptr);
  cleanup_picker_state();
  g_picker_max_mainmenu_calls = 0;
  ASSERT_TRUE(state.finished);
  ASSERT_EQ(5, state.captures);
}

TEST(UxShots, i_basecamp_paged) {
  trace_clear();
  CompanySlotCleanup cleanup{{"save0"}};
  std::vector<RosterSeed> roster = playtest_roster();
  roster.push_back({"WREN", FAMILY_ARCHER, 2, true});
  roster.push_back({"DUNCAN", FAMILY_SOLDIER, 1, true});
  roster.push_back({"ASHA", FAMILY_MAGE, 3, false});
  roster.push_back({"PIP", FAMILY_THIEF, 1, true});
  roster.push_back({"ROWAN", FAMILY_ELF, 2, true});
  ASSERT_TRUE(seed_company_with_roster("save0", "IRON KETTLE BAND", 1700259200,
                                       roster));
  ASSERT_TRUE(og::data::set_active_company_slot("save0"));
  NamedShot shot;
  shot.name = "basecamp_paged_p1";
  run_basecamp_shot(shot, &basecamp_paged_injector);
  ASSERT_EQ(3, shot.state.captures);
}

} // namespace
