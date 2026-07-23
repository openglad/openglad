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
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

void picker_main(Sint32 argc, char **argv);
Sint32 create_team_menu(Sint32 arg1);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState &pks() {
  return *og::runtime::current_session->picker_;
}

namespace {

constexpr int kTeamMenuTimeoutMs = 20000;

// Verify the current canvas and optionally dump it as a binary PPM. Runs on
// the injector thread while the menu loop repaints identical frames, so each
// caller settles the menu first.
bool capture_frame(const char *name) {
  screen *scr = og::runtime::current_session->myscreen_;
  const char *output_dir = std::getenv("UXSHOTS_DIR");
  FILE *f = nullptr;
  std::string path;
  if (output_dir != nullptr && output_dir[0] != '\0') {
    std::error_code error;
    std::filesystem::create_directories(output_dir, error);
    if (error) {
      fprintf(stderr, "  [uxshot] FAILED to create %s: %s\n", output_dir,
              error.message().c_str());
      return false;
    }
    path = std::string(output_dir) + "/" + name + ".ppm";
    f = fopen(path.c_str(), "wb");
    if (f == nullptr) {
      fprintf(stderr, "  [uxshot] FAILED to open %s\n", path.c_str());
      return false;
    }
    fprintf(f, "P6\n320 200\n255\n");
  }

  std::size_t nonblack_pixels = 0;
  for (int y = 0; y < 200; ++y) {
    for (int x = 0; x < 320; ++x) {
      Uint8 r = 0, g = 0, b = 0;
      scr->get_pixel(x, y, &r, &g, &b);
      if (r != 0 || g != 0 || b != 0)
        ++nonblack_pixels;
      if (f != nullptr) {
        fputc(r, f);
        fputc(g, f);
        fputc(b, f);
      }
    }
  }
  if (f != nullptr) {
    fclose(f);
    fprintf(stderr, "  [uxshot] wrote %s\n", path.c_str());
  }
  fprintf(stderr, "  [uxshot] %s: %zu nonblack pixels\n", name,
          nonblack_pixels);
  return nonblack_pixels >= 1000;
}

void cleanup_picker_state() {
  for (int i = 0; i < 5; i++) {
    pks().backdrops[i].reset();
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
  sd.current_campaign = "org.openglad.gladiator";
  sd.last_played_unix_s = last_played;
  int i = 0;
  for (const RosterSeed &seed : roster) {
    auto g = std::make_unique<guy>(seed.family);
    g->name = seed.name;
    g->upgrade_to_level(seed.level, true);
    g->deployed = seed.deployed;
    sd.team_list[i] = std::move(g);
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

// --- 2a. player settings ----------------------------------------------------

int player_settings_injector(void *data) {
  og::runtime::ensure_thread_session();
  ShotState *state = static_cast<ShotState *>(data);
  if (wait_for_interactable("player_settings", 5000)) {
    SDL_Delay(1500);
    interact("player_settings");
    if (wait_for_interactable("player_settings_back", 5000)) {
      SDL_Delay(1500);
      state->captures += capture_frame("player_settings");
      interact("player_settings_back");
    }
  }
  if (wait_for_interactable("quit", 10000)) {
    SDL_Delay(750);
    interact("quit");
  }
  state->finished = true;
  return 0;
}

TEST(UxShots, b2_player_settings) {
  trace_clear();
  reap_all_companies();
  ShotState state;
  SDL_Thread *thread =
      SDL_CreateThread(player_settings_injector, "ux_players", &state);
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
    sd.current_campaign = "org.openglad.gladiator";
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
  save.current_campaign = "org.openglad.gladiator";
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
    save.team_list[i++] = std::move(g);
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
// and the MATCHUP overview reached through the SEATS label. Internal network
// names intentionally differ from public company names so the screenshots
// catch any accidental transport-identity leak.
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
    interact("seats");
    if (wait_for_interactable("cross_control", 5000)) {
      SDL_Delay(1000);
      state->captures += capture_frame("matchup_network_seats");
      interact("back");
      if (wait_for_team_menu(5000)) {
        SDL_Delay(250);
        interact("back");
      }
    }
  }
  state->finished = true;
  return 0;
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
