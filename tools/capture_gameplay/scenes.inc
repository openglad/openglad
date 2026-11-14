// Cinematic recording commands. Included only by og_capture_gameplay.
// ---------------------------------------------------------------------------
// Epic spectator battles: the War of the Westlands war levels (6/7/8/14/15/17,
// moved from the Concept Playground's 605-610) with a hero crew deployed at
// the level start markers and a mode-seeking cinematic camera. The centroid of two distant fights is the empty field between
// them — so the camera seeks the densest hostile NEIGHBORHOOD instead, with
// scripted keyframes for floor cuts and establishing shots.
// ---------------------------------------------------------------------------
namespace epic_rec {

struct Key { int frame; int cx; int cy; int floor; bool forced; };

bool action_center(GameWorld& w, int floor, float& out_x, float& out_y)
{
    std::vector<walker*> alive;
    for (auto& up : w.oblist)
        if (walker* a = up.get();
            a != nullptr && !a->dead() && !a->dormant() &&
            a->query_order() == Order::Living &&
            static_cast<int>(a->floor()) == floor)
            alive.push_back(a);
    if (alive.empty())
        return false;
    walker* best = nullptr;
    int best_score = -1;
    for (walker* a : alive)
    {
        int score = 0;
        for (walker* b : alive)
        {
            if (a == b)
                continue;
            const long dx = static_cast<long>(a->xpos()) - b->xpos();
            const long dy = static_cast<long>(a->ypos()) - b->ypos();
            if (dx * dx + dy * dy < 160L * 160L)
                score += a->is_friendly(b) ? 1 : 3;
        }
        if (score > best_score)
        {
            best_score = score;
            best = a;
        }
    }
    long sx = 0, sy = 0;
    int n = 0;
    for (walker* b : alive)
    {
        const long dx = static_cast<long>(best->xpos()) - b->xpos();
        const long dy = static_cast<long>(best->ypos()) - b->ypos();
        if (dx * dx + dy * dy < 160L * 160L)
        {
            sx += static_cast<long>(b->xpos());
            sy += static_cast<long>(b->ypos());
            n++;
        }
    }
    out_x = static_cast<float>(sx) / static_cast<float>(n > 0 ? n : 1);
    out_y = static_cast<float>(sy) / static_cast<float>(n > 0 ? n : 1);
    return true;
}

// Optional per-frame hooks for staged scenes. `stage` runs on the
// AUTHORITATIVE server world before each sim tick (place a walker, poke a
// scripted beat) and may take the camera: return true with a target centre
// and floor to snap there for this frame. `keep` picks which frames are
// written (every frame when empty), so long films can dump a few windows.
struct Director
{
    std::function<bool(int frame, GameWorld& server, float& cx, float& cy,
                       int& floor)> stage;
    std::function<bool(int frame)> keep;
};

void record(const char* scene, int scen, WeatherKind kind,
            const std::vector<int>& roster,
            const std::vector<Key>& keys,
            const char* campaign = "westlands",
            const Director& director = {})
{
    screen* const s = og::runtime::current_session->myscreen_;
    // The crew deploys at the level's team-0 start position markers.
    gameplay_rec::build_save(s, campaign, scen, 1, roster, 7);
    glad_init();
    gameplay_rec::all_effects_on();
    gameplay_rec::force_weather(kind);
    screen* const server =
        og::runtime::local_transport_shadow_testing_server_screen(
            *og::runtime::current_game_session);
    // Free the player-controlled hero to fight with the rest of the crew.
    for (auto& uptr : server->world().oblist)
        if (walker* wk = uptr.get(); wk != nullptr && wk->user() != -1)
        {
            wk->set_act_type(ACT_RANDOM);
            // Film-armor the player's lead: a full team-0 wipe is an
            // EndGame loss (the Bridge of Shadow's undead tide ate the
            // whole crew at tick ~327 and cut the film). One immortal
            // extra keeps the bound team alive; everyone else stays
            // mortal — the deaths ARE the battle.
            wk->stats()->set_max_hitpoints(30000.0f);
            wk->stats()->set_hitpoints(30000.0f);
        }
    // A named hero's death ends a SAVE_ALL level (EndGame loss) — right in
    // gameplay, wrong for a fixed-length spectator film (the Mountain of
    // Fire cut at tick ~366 when the Ranger-King fell): strip the bit so
    // the war plays to the last scheduled frame. Server first — the mirror
    // re-syncs type from the authoritative snapshots.
    server->world().type = static_cast<char>(
        server->world().type & ~SCEN_TYPE_SAVE_ALL);
    s->world().type = static_cast<char>(
        s->world().type & ~SCEN_TYPE_SAVE_ALL);
    viewscreen* vs = s->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    const int world_w = static_cast<int>(s->world().grid.w) * GRID_SIZE;
    const int world_h = static_cast<int>(s->world().grid.h) * GRID_SIZE;
    const int total = keys.back().frame;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    size_t seg = 0;
    float cam_x = static_cast<float>(keys[0].cx);
    float cam_y = static_cast<float>(keys[0].cy);
    int last_floor = keys[0].floor;
    for (int f = 0; f < total; f++)
    {
        while (seg + 1 < keys.size() && keys[seg + 1].frame <= f)
            seg++;
        const Key& a = keys[seg];
        float tx = static_cast<float>(a.cx), ty = static_cast<float>(a.cy);
        int floor = a.floor;
        bool snap = a.forced;
        if (!a.forced)
            action_center(s->world(), a.floor, tx, ty);
        if (director.stage &&
            director.stage(f, server->world(), tx, ty, floor))
            snap = true;
        if (floor != last_floor || snap)
        {
            cam_x = tx; // snap on a floor cut or scripted anchor
            cam_y = ty;
            last_floor = floor;
        }
        cam_x += (tx - cam_x) * 0.05f; // eased drift toward the action
        cam_y += (ty - cam_y) * 0.05f;
        const int cx = static_cast<int>(cam_x);
        const int cy = static_cast<int>(cam_y);
        int topx = cx - 160, topy = cy - 100;
        if (topx < 0) topx = 0;
        if (topy < 0) topy = 0;
        if (topx > world_w - 320) topx = world_w - 320;
        if (topy > world_h - 200) topy = world_h - 200;
        // The transport shadow re-assigns viewport control every tick; the
        // manual camera only holds while control is null, so clear it both
        // before the sim tick and before the redraw.
        vs->editor_floor_override_ = static_cast<Sint32>(floor);
        vs->control = nullptr;
        s->set_level_draw_pos(topx, topy);
        if (game_frame_with_result(*s, st, deps) != GameFrameResult::Continue)
            break;
        if (director.keep && !director.keep(f))
            continue;
        vs->control = nullptr;
        s->set_level_draw_pos(topx, topy);
        s->redraw();
        s->swap();
        gameplay_rec::dump_screen(s, scene, f);
        if (::testing::Test::HasFatalFailure())
            break;
    }
    vs->editor_floor_override_ = -1;
    s->world().end = 0;
    s->world().delete_objects();
}

} // namespace epic_rec

TEST(GameLoop, zz_capture_epic_battles)
{
    ASSERT_NE(nullptr, getenv("OG_FX_CAPTURE_DIR")) << "capture tool needs an output directory";
    using epic_rec::Key;
    // OG_FX_CAPTURE_ONLY=<level id> records a single level (fast iteration).
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    // The six war stories live in War of the Westlands now (moved from the
    // concept package: 605->15, 606->14, 607->8, 608->6, 609->17, 610->7);
    // the keyframe scripts are unchanged — the battlefields moved intact.
    // The Deeping Wall — and at frame ~500, look to the east: the White
    // Rider arrives behind the flank in a teleport flash (delayed spawn).
    if (want("15"))
        epic_rec::record("epic_15", 15, WeatherKind::Rain,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_CLERIC, FAMILY_BARBARIAN, FAMILY_MAGE},
            {{0, 610, 660, 0, false}, {80, 610, 420, 0, false},
             {84, 260, 310, 1, false}, {170, 1020, 310, 1, false},
             {174, 632, 330, 0, false}, {495, 632, 360, 0, false},
             {500, 1216, 540, 0, true}, {575, 1216, 540, 0, true},
             {580, 900, 420, 0, false}, {800, 700, 380, 0, false}});
    if (want("14"))
        epic_rec::record("epic_14", 14, WeatherKind::Clouds,
            {FAMILY_DRUID, FAMILY_DRUID, FAMILY_ELF, FAMILY_ELF, FAMILY_ELF,
             FAMILY_BARBARIAN, FAMILY_BARBARIAN, FAMILY_ARCHMAGE},
            {{0, 120, 480, 0, false}, {110, 480, 470, 0, false},
             {114, 480, 480, 1, false}, {190, 480, 480, 1, false},
             {194, 480, 480, 2, false}, {270, 480, 480, 2, false},
             {274, 480, 480, 3, false}, {370, 480, 480, 3, false},
             {374, 480, 300, 0, false}, {480, 480, 640, 0, false}});
    if (want("8"))
        epic_rec::record("epic_8", 8, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
             FAMILY_BARBARIAN, FAMILY_BARBARIAN, FAMILY_CLERIC, FAMILY_ELF,
             FAMILY_ELF},
            {{0, 120, 320, 1, false}, {250, 560, 320, 1, false},
             {350, 560, 320, 1, false},
             {354, 560, 320, 0, false}, {440, 760, 320, 0, false}});
    if (want("6"))
        epic_rec::record("epic_6", 6, WeatherKind::None,
            {FAMILY_THIEF, FAMILY_THIEF, FAMILY_ELF, FAMILY_ELF,
             FAMILY_BARBARIAN, FAMILY_BARBARIAN, FAMILY_CLERIC,
             FAMILY_SOLDIER},
            {{0, 180, 180, 2, false}, {130, 760, 240, 2, false},
             {134, 760, 240, 1, false}, {270, 200, 660, 1, false},
             {274, 280, 660, 0, false}, {460, 480, 620, 0, false}});
    if (want("17"))
        epic_rec::record("epic_17", 17, WeatherKind::Clouds,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_MAGE, FAMILY_MAGE, FAMILY_CLERIC},
            {{0, 160, 300, 0, false}, {110, 400, 450, 0, false},
             {240, 730, 400, 0, false}, {400, 730, 400, 0, false},
             {404, 730, 300, 1, false}, {500, 730, 300, 1, false}});
    if (want("7"))
        epic_rec::record("epic_7", 7, WeatherKind::Rain,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
             FAMILY_ELF, FAMILY_ELF, FAMILY_BARBARIAN, FAMILY_BARBARIAN},
            {{0, 160, 330, 2, false}, {250, 1120, 330, 2, false},
             {254, 640, 180, 0, false}, {350, 640, 180, 0, false},
             {354, 640, 330, 2, false}, {480, 640, 330, 2, false}});
    // --- The new Westlands showpiece levels (built for this campaign). ---
    // The White City: open on the torn gate where the war-beast columns push
    // the breach, follow the street fighting, cut up to the wall-top
    // ramparts, hold the beacon on the tower's glass crown, then return to
    // the battle until the Horse-lord's dawn relief flashes in on the
    // north-west plain at tick 700.
    if (want("16"))
        epic_rec::record("epic_16", 16, WeatherKind::Clouds,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_CLERIC, FAMILY_MAGE, FAMILY_BARBARIAN},
            {{0, 672, 392, 0, true}, {70, 672, 392, 0, false},
             {150, 672, 100, 1, false}, {240, 1320, 408, 2, true},
             {330, 672, 392, 0, false}, {690, 168, 80, 0, true},
             {760, 168, 80, 0, true}});
    // The Mountain of Fire: establish the war host's wedge (the Bearer's
    // warded cleft sits at the west map edge), follow the plain battle,
    // hold the Undergate throat, then climb the cone — terrace ring, the
    // north lava fall, the summit rim over the caldera and the twin summit
    // cracks — and come back down to the war. The wild-men relief wakes at
    // tick 400 on the south-west plain.
    if (want("24"))
        epic_rec::record("epic_24", 24, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_BARBARIAN,
             FAMILY_BARBARIAN, FAMILY_ELF, FAMILY_ELF, FAMILY_CLERIC,
             FAMILY_MAGE},
            {{0, 130, 380, 0, true}, {80, 400, 390, 0, false},
             {250, 880, 392, 0, true}, {320, 1064, 240, 1, false},
             {400, 150, 530, 0, true}, {460, 400, 390, 0, false},
             {560, 1064, 200, 1, true}, {610, 1064, 392, 2, false},
             {700, 1008, 392, 2, true}, {760, 500, 390, 0, false},
             {800, 500, 390, 0, false}});
    // The High Pass tiles showcase: the new SNOW ground under the new Snow
    // weather kind (on this level snowfall is the terrain override —
    // blizzard country is always snowing; the force below just makes the
    // scene independent of that heuristic). Establish the muster on the
    // south apron by the frozen tarn, follow the tarn-pack fight, cut to
    // the chief's band at the mine gate, then drift back to the action.
    if (want("5"))
        epic_rec::record("epic_5", 5, WeatherKind::Snow,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_BARBARIAN,
             FAMILY_BARBARIAN, FAMILY_ELF, FAMILY_ELF, FAMILY_CLERIC,
             FAMILY_THIEF},
            {{0, 808, 888, 0, true}, {60, 808, 888, 0, false},
             {180, 392, 168, 0, true}, {240, 808, 880, 0, false},
             {300, 808, 880, 0, false}});
}

// ---------------------------------------------------------------------------
// War of the Westlands story gameplay: the two Bearer's-road levels captured
// as REAL gameplay (single viewport, the camera bound to the company's
// thief). The crew deploys at the level start markers; the claimed hero is
// freed to AI because no live input arrives in a capture and a statue lead
// makes a dull chase.
// ---------------------------------------------------------------------------
namespace gameplay_rec {

void record_story(const char* name, int scen, WeatherKind kind,
                  const std::vector<int>& roster, int level, int frames,
                  const char* campaign = "westlands")
{
    screen* const s = og::runtime::current_session->myscreen_;
    build_save(s, campaign, scen, 1, roster, level);
    glad_init();
    all_effects_on();
    force_weather(kind);
    screen* const server =
        og::runtime::local_transport_shadow_testing_server_screen(
            *og::runtime::current_game_session);
    ASSERT_NE(nullptr, server);
    for (auto& uptr : server->world().oblist)
        if (walker* wk = uptr.get(); wk != nullptr && wk->user() != -1)
        {
            wk->set_act_type(ACT_RANDOM);
            // Camera armor: when the lead falls, the display rebinds
            // control and the viewport has been seen drifting onto the
            // map-border bricks. The camera walker survives the scene;
            // the escort remains mortal (their deaths are the story).
            wk->stats()->set_max_hitpoints(30000.0f);
            wk->stats()->set_hitpoints(30000.0f);
        }
    // Roster guys carry default names ("SOLDIER", family names), and on a
    // SAVE_ALL level ANY named team-0 death is an EndGame loss — the Forest
    // Road film cut at tick ~150 when an escort fell. Strip the bit so the
    // scene runs its scheduled length (server first; the mirror re-syncs
    // type from the authoritative snapshots).
    server->world().type = static_cast<char>(
        server->world().type & ~SCEN_TYPE_SAVE_ALL);
    s->world().type = static_cast<char>(
        s->world().type & ~SCEN_TYPE_SAVE_ALL);
    // The Bearer is story cargo, parked far from the escort: the road
    // pickets can wear the level-3 thief down while the camera is with the
    // fighting, and "THE BEARER DIED" in the kill feed rather spoils the
    // campaign's premise. Film-armor him.
    for (auto& uptr : server->world().oblist)
        if (walker* wk = uptr.get();
            wk != nullptr && wk->stats() != nullptr &&
            wk->stats()->name == "The Bearer")
        {
            wk->stats()->set_max_hitpoints(30000.0f);
            wk->stats()->set_hitpoints(30000.0f);
        }

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    for (int f = 0; f < frames; f++)
    {
        if (game_frame_with_result(*s, st, deps) != GameFrameResult::Continue)
            break;
        s->redraw();
        s->swap();
        dump_viewport(s, name, f);
        if (::testing::Test::HasFatalFailure())
            break;
    }
    s->world().end = 0;
    s->world().delete_objects();
}

} // namespace gameplay_rec

TEST(GameLoop, zz_capture_westlands)
{
    ASSERT_NE(nullptr, getenv("OG_FX_CAPTURE_DIR")) << "capture tool needs an output directory";
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    // 2 The Forest Road — THE FLIGHT: rain over the corridor maze, the
    // Pale Riders waking behind the crew in waves (250/550/900). The
    // camera rides with the thief; the Bearer runs in the column with him
    // (Wave E1). 700 frames so the film reaches past the tick-500 wave —
    // a 420-frame cut ended before any pursuit orc woke, so the card
    // structurally could not show the chase (forest-pathing RCA note).
    if (want("2"))
        gameplay_rec::record_story(
            "westlands_flight", 2, WeatherKind::Rain,
            {FAMILY_THIEF, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_BARBARIAN, FAMILY_CLERIC, FAMILY_MAGE},
            6, 700);
    // 19 The Dead Marshes: the new MARSH tiles (shimmer + ripples) with
    // ghost-lights drifting off every mere at the firm-shelf line.
    if (want("19"))
        gameplay_rec::record_story(
            "westlands_marshes", 19, WeatherKind::Clouds,
            {FAMILY_THIEF, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_BARBARIAN, FAMILY_CLERIC, FAMILY_MAGE},
            6, 360);
}

// Wave E7 decor-ambience spot checks: short forced-camera establishing
// shots of the four representative dressings — marsh bones on the drowned
// battlefield, cinder grit + the march's dead on the ash, war-road pebbles
// and trampled-swathe bones on the plains, and forest-shrub undergrowth in
// the Golden Wood. Each scene is a 20-frame hold so sprites settle; the
// point is to LOOK at the frames.
TEST(GameLoop, zz_capture_westlands_decor)
{
    ASSERT_NE(nullptr, getenv("OG_FX_CAPTURE_DIR")) << "capture tool needs an output directory";
    using epic_rec::Key;
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    const std::vector<int> crew = {FAMILY_SOLDIER, FAMILY_SOLDIER,
                                   FAMILY_BARBARIAN, FAMILY_ELF, FAMILY_ELF,
                                   FAMILY_CLERIC, FAMILY_MAGE, FAMILY_THIEF};
    if (want("d19")) // bog island I1, the sunken dead among the meres
        epic_rec::record("decor_19_marshes", 19, WeatherKind::Clouds, crew,
                         {{0, 288, 272, 0, true}, {20, 288, 272, 0, true}});
    if (want("d23")) // the central lane: banner torches, grit, old bones
        epic_rec::record("decor_23_ash", 23, WeatherKind::None, crew,
                         {{0, 720, 320, 0, true}, {20, 720, 320, 0, true}});
    if (want("d13")) // the war-road through the column's trampled swathes
        epic_rec::record("decor_13_plains", 13, WeatherKind::Clouds, crew,
                         {{0, 672, 384, 0, true}, {20, 672, 384, 0, true}});
    if (want("d10")) // the west road under the eaves, undergrowth off it
        epic_rec::record("decor_10_wood", 10, WeatherKind::Clouds, crew,
                         {{0, 320, 320, 0, true}, {20, 320, 320, 0, true}});
}

// ---------------------------------------------------------------------------
// The Long Season (longseason): four scenes from the Brass
// Kettle Company's year. Ferry Right is real gameplay (the camera rides the
// lead soldier plugging the causeway mouth); the other three are keyframed
// spectator films per the epic_rec pattern. Keyframe pixel anchors come
// straight from the tools/longseason_mapgen builders (tile * 16).
// ---------------------------------------------------------------------------
TEST(GameLoop, zz_capture_longseason)
{
    ASSERT_NE(nullptr, getenv("OG_FX_CAPTURE_DIR")) << "capture tool needs an output directory";
    using epic_rec::Key;
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    const char* const ls = "longseason";
    // 2 The Ferry Right — spring flood, rain over the drowned river. Real
    // gameplay: the company holds the causeway's east mouth so the toll
    // runs. Knifemen open on the span; the boat wave beaches on the north
    // shallows at tick 400 and hits the landing's rear rank on camera.
    if (want("ls_2"))
        gameplay_rec::record_story(
            "ls_2", 2, WeatherKind::Rain,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ELF,
             FAMILY_BARBARIAN, FAMILY_THIEF, FAMILY_CLERIC, FAMILY_MAGE},
            4, 460, ls);
    // 14 The Long Toll — the Grey Tolls fort in winter, forced blizzard.
    // Establish the courtyard (braziers, the strongroom, the watch), follow
    // the wolf probe, cut to the west mouth for wave 1's wake flash (300),
    // ride that fight up the road, cut east for the 700 wave, then back to
    // the fort for the hold.
    if (want("ls_14"))
        epic_rec::record("ls_14", 14, WeatherKind::Snow,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_BARBARIAN,
             FAMILY_BARBARIAN, FAMILY_ELF, FAMILY_ELF, FAMILY_CLERIC,
             FAMILY_MAGE},
            {{0, 480, 480, 0, true}, {60, 480, 480, 0, false},
             {270, 64, 464, 0, true}, {340, 300, 470, 0, false},
             {660, 912, 464, 0, true}, {730, 700, 460, 0, false},
             {860, 480, 480, 0, false}, {960, 480, 480, 0, false}},
            ls);
    // 17 Ashfall Gate — the creditors' camps on the ash plain. Open on the
    // company's wedge under the banner, tour the three camps (palisades,
    // cook-fires, the tent and tower trickles), hold the gate throat's
    // golem wards and slag runnels, then settle on the wagon-corridor spine
    // fight; at 660 cut back west onto the north cut-purse trio (tiles
    // 10-14, y 6-13) for their tick-700 wake flash behind the crew's line.
    if (want("ls_17"))
        epic_rec::record("ls_17", 17, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_MAGE, FAMILY_CLERIC, FAMILY_BARBARIAN},
            {{0, 96, 336, 0, true}, {60, 352, 128, 0, true},
             {130, 352, 544, 0, true}, {200, 976, 336, 0, true},
             {270, 640, 336, 0, false}, {430, 640, 336, 0, false},
             {660, 192, 160, 0, true}, {760, 400, 340, 0, false},
             {860, 640, 336, 0, false}, {940, 640, 336, 0, false}},
            ls);
    // 18 The Warm Mint — the keyframed three-floor foundry climb. Floor 0:
    // the gatehall door war (Kettle holds the west wall at tile 3,21), the
    // casting floor's lava channels, the golem-warded stair chamber. Cut UP
    // at the aligned stair (57,21): the vault floor's collapse holes, melt
    // pools and warded heaps, then the counting rooms. Cut UP again (9,21):
    // the crucible lake, the rim elementals and ghosts, and The Founder on
    // the dais beside the master ledger — brazier-lit end to end.
    if (want("ls_18"))
        epic_rec::record("ls_18", 18, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_BARBARIAN, FAMILY_ELF,
             FAMILY_ELF, FAMILY_CLERIC, FAMILY_MAGE, FAMILY_ARCHMAGE},
            {{0, 96, 336, 0, true}, {70, 96, 336, 0, false},
             {200, 560, 336, 0, true}, {270, 560, 336, 0, false},
             {430, 912, 336, 0, true}, {500, 912, 336, 1, true},
             {570, 560, 320, 1, true}, {640, 560, 320, 1, false},
             {710, 144, 336, 1, true}, {780, 144, 336, 2, true},
             {850, 504, 344, 2, true}, {920, 832, 336, 2, true},
             {990, 832, 336, 2, false}, {1050, 832, 336, 2, false}},
            ls);
}

TEST(GameLoop, zz_capture_imaginations)
{
    ASSERT_NE(nullptr, getenv("OG_FX_CAPTURE_DIR")) << "capture tool needs an output directory";
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    // 1 The Raspberry Isle — the dream-log's island assault. Open on the
    // south landing beach, ride the charge up the paved causeway past its
    // sentry, breach the bailey's south gate, hold the two-ring castle
    // fight, then cut out to the mage tower and the bone tent on the
    // field ring before settling on the keep for the throne battle and
    // the tick-800 wave flash.
    if (want("imag_1"))
        epic_rec::record("imag_1", 1, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN},
            {{0, 504, 880, 0, true}, {80, 504, 760, 0, false},
             {200, 504, 688, 0, true}, {320, 504, 624, 0, false},
             {460, 504, 504, 0, true}, {560, 704, 240, 0, true},
             {660, 240, 704, 0, true}, {780, 504, 504, 0, true},
             {950, 504, 504, 0, false}},
            "imaginations");
    // 2 The Castle of Gold — the faerie war around our own castle. Open on
    // the keeper inside by the gate, follow the first faeries over the
    // rivers, look in on the Ghost Tent east of the walls, then hold on
    // the gate and the court while the waves press it.
    if (want("imag_2"))
        epic_rec::record("imag_2", 2, WeatherKind::None,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN},
            {{0, 504, 600, 0, true}, {120, 504, 720, 0, false},
             {300, 832, 528, 0, true}, {450, 504, 720, 0, true},
             {700, 504, 480, 0, false}, {1000, 504, 480, 0, false}},
            "imaginations");
    // 3 The Asteroid Castle — the spiked castle on its rock in space. Open
    // on the crew in the court, swing to the east gate as the first gust
    // bites on the spike band, look up at the Big One on the Balloon Rock
    // (floor 1) and follow the fight to the rock's edge, then watch the
    // captain's band wake on the southeast rim.
    if (want("imag_3"))
        epic_rec::record("imag_3", 3, WeatherKind::Snow,
            {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
             FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN},
            {{0, 248, 488, 0, true}, {200, 400, 480, 0, false},
             {340, 400, 480, 0, true}, {420, 784, 168, 1, true},
             {520, 736, 192, 0, false}, {1420, 896, 736, 0, true},
             {1700, 784, 784, 0, false}},
            "imaginations");
}

// ---------------------------------------------------------------------------
// Staged moments for the two castle levels (proof stills): the epic_rec film
// with a Director that sets up each beat on the server world — the crew in
// the court, the lead dropped onto a strongroom's gold bar (the gold beat
// is a keeper's pickup), the lead dropped within the Big One's wake range —
// and a camera that follows the named walker the beat is about.
// ---------------------------------------------------------------------------
namespace moments_rec {

walker* lead_of(GameWorld& w)
{
    for (auto& up : w.oblist)
        if (walker* a = up.get(); a != nullptr && !a->dead() &&
            a->query_order() == Order::Living && a->user() != -1)
            return a;
    return nullptr;
}

walker* named(GameWorld& w, const char* name, int family = -1)
{
    for (auto& up : w.oblist)
        if (walker* a = up.get(); a != nullptr && !a->dead() &&
            a->query_order() == Order::Living && !a->dormant() &&
            (family < 0 || a->family() == family) &&
            (name == nullptr || a->stats()->name == name))
            return a;
    return nullptr;
}

// Moves a walker to tile (tx, ty) on `floor`, floor first (obmap buckets
// follow every position change).
void place_at(walker* w, int tx, int ty, int floor)
{
    if (w == nullptr)
        return;
    w->set_floor(static_cast<unsigned char>(floor));
    w->setxy(tx * GRID_SIZE, ty * GRID_SIZE);
}

bool follow(walker* w, float& cx, float& cy, int& floor)
{
    if (w == nullptr)
        return false;
    cx = static_cast<float>(w->xpos());
    cy = static_cast<float>(w->ypos());
    floor = static_cast<int>(w->floor());
    return true;
}

bool in(int f, int a, int b) { return f >= a && f < b; }

// An awake hostile on the asteroid with its tile up against the spike band
// around scen 3's castle (the band is tiles 6..25 x 13..48 around the
// castle block 8..23 x 15..46; a hostile within one tile of it gets bitten).
walker* by_the_spikes(GameWorld& w)
{
    for (auto& up : w.oblist)
        if (walker* a = up.get(); a != nullptr && !a->dead() &&
            !a->dormant() && a->query_order() == Order::Living &&
            a->team_num() != 0 && a->floor() == 0)
        {
            const int tx = static_cast<int>(a->xpos()) / GRID_SIZE;
            const int ty = static_cast<int>(a->ypos()) / GRID_SIZE;
            const bool near_band = tx >= 5 && tx <= 26 && ty >= 12 && ty <= 49;
            const bool in_castle = tx >= 9 && tx <= 22 && ty >= 16 && ty <= 45;
            if (near_band && !in_castle)
                return a;
        }
    return nullptr;
}

// The awake hostile on floor 0 nearest tile (tx, ty), within `reach`
// tiles; nullptr when none is that close.
walker* nearest_hostile(GameWorld& w, int tx, int ty, int reach)
{
    walker* best = nullptr;
    long best_d = static_cast<long>(reach) * reach * GRID_SIZE * GRID_SIZE;
    for (auto& up : w.oblist)
        if (walker* a = up.get(); a != nullptr && !a->dead() &&
            !a->dormant() && a->query_order() == Order::Living &&
            a->team_num() != 0 && a->floor() == 0)
        {
            const long dx = static_cast<long>(a->xpos()) - tx * GRID_SIZE;
            const long dy = static_cast<long>(a->ypos()) - ty * GRID_SIZE;
            if (dx * dx + dy * dy <= best_d)
            {
                best_d = dx * dx + dy * dy;
                best = a;
            }
        }
    return best;
}

} // namespace moments_rec

TEST(GameLoop, zz_capture_imaginations_moments)
{
    ASSERT_NE(nullptr, getenv("OG_FX_CAPTURE_DIR")) << "capture tool needs an output directory";
    using epic_rec::Key;
    using namespace moments_rec;
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    const std::vector<int> crew = {
        FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
        FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN};
    // 2 The Castle of Gold: the crew in the court (three of the field crew
    // brought in beside the keeper), the keeper's first bar in the
    // southwest strongroom, the next (southeast room) filmed from the Ghost
    // Tent, the waves at the gate, the camera riding the Fae Queen in, and
    // last the lead walking from the south bridge onto the throne dais.
    if (want("imag_moments_2"))
    {
        epic_rec::Director d;
        // The camera holds where the Queen last flew once she pops.
        d.stage = [qx = -1.0f, qy = -1.0f](int f, GameWorld& w, float& cx,
                                          float& cy, int& floor) mutable
        {
            // The court: from the deploy until 140 the keeper and three of
            // the field crew are held on their marks by the gate, so the
            // gate's lines (on_load) and the dream-log's opening lines
            // (tick 60) read in order over them, and nobody walks over the
            // castle key (31,36) to put a pickup line between the two.
            // Then they go free.
            if (in(f, 2, 140))
            {
                place_at(lead_of(w), 31, 39, 0);
                int placed = 0;
                const int spots[3][2] = {{29, 39}, {33, 39}, {31, 41}};
                for (auto& up : w.oblist)
                    if (walker* a = up.get(); a != nullptr && placed < 3 &&
                        a->query_order() == Order::Living &&
                        a->team_num() == 0 && a->user() == -1 &&
                        a->myguy != nullptr)
                    {
                        place_at(a, spots[placed][0], spots[placed][1], 0);
                        ++placed;
                    }
            }
            // The keeper is dropped onto a bar just inside each room's
            // doors (the room's second bar lies beside it).
            if (f == 150)
                place_at(lead_of(w), 22, 34, 0);
            if (f == 300)
                place_at(lead_of(w), 39, 34, 0);
            // Once the tent shot is done, the keeper is handed the castle
            // key (13), as if he had stepped over it by the gate: the
            // strongroom doors open for him and he walks back out to the
            // war instead of bumping a locked door for the rest of the
            // film. (Handed over earlier, he would leave the first room and
            // empty the second before his drop onto its bar.)
            if (f == 420)
                if (walker* k = lead_of(w); k != nullptr)
                    k->set_keys(k->keys() | (1u << 13));
            // The waves at the gate: the camera finds the faerie nearest
            // the gate apron (31,44) while the war comes to the wall.
            if (in(f, 900, 1400))
                return follow(nearest_hostile(w, 31, 44, 14), cx, cy, floor);
            // The Fae Knights' wave (awake at 1500 on the shores) is set
            // down on the gate apron, flanking the causeway, where the
            // camera holds on the gate; the front pair stands two rows
            // below the wall so no knight overlaps the wall face. They
            // land at 1572, once a crewman's potion pickup line (about
            // 1495..1570) has left the HUD, so the gate beat plays under
            // no stray pickup text.
            if (f == 1572)
            {
                const int spots[6][2] = {{27, 46}, {35, 46}, {28, 48},
                                         {34, 48}, {26, 49}, {36, 49}};
                int placed = 0;
                for (auto& up : w.oblist)
                    if (walker* a = up.get(); a != nullptr && placed < 6 &&
                        !a->dead() && !a->dormant() &&
                        a->query_order() == Order::Living &&
                        a->team_num() != 0 && a->stats()->name == "Fae Knight")
                    {
                        place_at(a, spots[placed][0], spots[placed][1], 0);
                        ++placed;
                    }
            }
            // "Walk to the throne!": once the Queen's pop lines are gone,
            // the lead and two of the crew are set down on the south
            // bridge and walk north up the causeway, through the gate and
            // the court, onto the purple dais (the exit pad is at 31,28;
            // the two flank it at 30 and 32). The camera rides the lead.
            if (f >= 2580)
            {
                walker* k = lead_of(w);
                if (k == nullptr)
                    return false;
                walker* pair[2] = {nullptr, nullptr};
                int found = 0;
                for (auto& up : w.oblist)
                    if (walker* a = up.get(); a != nullptr && found < 2 &&
                        !a->dead() && a->query_order() == Order::Living &&
                        a->team_num() == 0 && a->user() == -1 &&
                        a->myguy != nullptr)
                        pair[found++] = a;
                if (f == 2580)
                {
                    place_at(k, 31, 58, 0);
                    k->set_act_type(ACT_GUARD);
                    for (int i = 0; i < found; ++i)
                    {
                        place_at(pair[i], 30 + 2 * i, 59, 0);
                        pair[i]->set_act_type(ACT_GUARD);
                    }
                }
                if (k->ypos() > 28 * GRID_SIZE)
                    k->walkstep(0, -1);
                for (int i = 0; i < found; ++i)
                    if (pair[i]->ypos() > 29 * GRID_SIZE)
                        pair[i]->walkstep(0, -1);
                return follow(k, cx, cy, floor);
            }
            // Just before her wave wakes, the Queen and her four knights
            // (still dormant on the south shore) are drawn in tight, the
            // Queen at the head of a V of knights in the middle of the
            // shot, so the wake flashes mark her and her escort together.
            if (f == 2196)
            {
                const int knight_spots[4][2] = {{29, 61}, {33, 61},
                                                {27, 62}, {35, 62}};
                int k = 0;
                for (auto& up : w.oblist)
                    if (walker* a = up.get(); a != nullptr && !a->dead() &&
                        a->dormant() && a->query_order() == Order::Living &&
                        a->ypos() >= 59 * GRID_SIZE)
                    {
                        if (a->stats()->name == "Fae Queen")
                            place_at(a, 31, 60, 0);
                        else if (a->stats()->name == "Fae Knight" && k < 4)
                        {
                            place_at(a, knight_spots[k][0],
                                     knight_spots[k][1], 0);
                            ++k;
                        }
                    }
            }
            // The Queen's wave wakes on the south shore (spawn_delay 2200)
            // under "The Fae Queen is coming." (tick 2150): the keyframe
            // holds the camera on her post until she wakes, then it rides
            // her, her knights flanking her on the shore.
            if (f <= 2200)
                return false;
            if (follow(named(w, "Fae Queen"), cx, cy, floor))
            {
                qx = cx;
                qy = cy;
                return true;
            }
            if (qx < 0.0f)
                return false;
            cx = qx;
            cy = qy;
            return true;
        };
        d.keep = [](int f)
        {
            return f % 3 == 0 &&
                (in(f, 0, 120) || in(f, 140, 230) || in(f, 290, 420) ||
                 in(f, 900, 1400) || in(f, 1500, 1900) ||
                 in(f, 2190, 2900));
        };
        epic_rec::record("imag_moments_2", 2, WeatherKind::None, crew,
            {{0, 504, 600, 0, true}, {140, 376, 560, 0, true},
             {290, 832, 528, 0, true}, {900, 504, 680, 0, true},
             {1500, 504, 704, 0, true},
             {2190, 504, 984, 0, true}, {2900, 504, 448, 0, true}},
            "imaginations", d);
    }
    // 3 The Asteroid Castle: the Purple Man at the east gate, space past
    // the Balloon pit, the Big One on the rock with the key (floor 1). The
    // crew but the lead are posted in the court ("we sit in the castle");
    // the lead
    // is dropped within the Big One's wake range and the camera rides him
    // off the rock and down. Then the first gust is set down in sight of
    // the gate and the camera finds any bad guy up against the spikes, and
    // Black Dash's band is set down by the east gate to break on them.
    if (want("imag_moments_3"))
    {
        epic_rec::Director d;
        d.stage = [](int f, GameWorld& w, float& cx, float& cy, int& floor)
        {
            if (f == 2)
                for (auto& up : w.oblist)
                    if (walker* a = up.get(); a != nullptr &&
                        a->query_order() == Order::Living &&
                        a->team_num() == 0 && a->user() == -1 &&
                        a->myguy != nullptr)
                        a->set_act_type(ACT_GUARD);
            if (f == 150)
                place_at(lead_of(w), 42, 15, 0);
            if (f == 330)
            {
                // The first gust's four (awake, on the east rim) set down
                // along the east band, north and south of the gate, in
                // sight of the crew on the gate apron.
                const int spots[4][2] = {{26, 20}, {26, 40}, {27, 23},
                                         {27, 37}};
                int placed = 0;
                for (auto& up : w.oblist)
                    if (walker* a = up.get(); a != nullptr && placed < 4 &&
                        !a->dead() && !a->dormant() &&
                        a->query_order() == Order::Living &&
                        a->team_num() != 0 &&
                        a->xpos() >= 50 * GRID_SIZE &&
                        (a->family() == FAMILY_ORC ||
                         a->family() == FAMILY_FAERIE))
                    {
                        place_at(a, spots[placed][0], spots[placed][1], 0);
                        ++placed;
                    }
            }
            if (f == 1402)
            {
                // Black Dash and his two orcs (awake at 1400 on the
                // southeast rim) set down against the east spike band, just
                // north and south of the gate apron (rows 29..31): they
                // fight the crew on the apron with the spikes biting them,
                // and break on them in front of the gate. The band's faerie
                // stays out on the rim, so the war still ends on the staged
                // last raider below.
                const int spots[3][2] = {{26, 27}, {26, 33}, {26, 25}};
                int others = 1;
                for (auto& up : w.oblist)
                    if (walker* a = up.get(); a != nullptr &&
                        !a->dead() && !a->dormant() &&
                        a->query_order() == Order::Living &&
                        a->team_num() != 0 &&
                        a->xpos() >= 50 * GRID_SIZE &&
                        a->ypos() >= 40 * GRID_SIZE &&
                        a->family() != FAMILY_FAERIE)
                    {
                        if (a->stats()->name == "Black Dash")
                            place_at(a, spots[0][0], spots[0][1], 0);
                        else if (others < 3)
                        {
                            place_at(a, spots[others][0], spots[others][1],
                                     0);
                            ++others;
                        }
                    }
            }
            if (f == 1560)
            {
                // The war's end, staged: every bad guy left (dormant ones
                // too) is struck off quietly but one awake raider, who is
                // set down at one hit point against the south spike band
                // beside the big door's apron. The next spike bite kills
                // him inside the sim tick, so the level script sees the
                // last bad guy fall and sets the rainbow sparkling, band by
                // band, for 180 ticks: past the 100-tick win lines, so the
                // last frames show the sparkles with no HUD text.
                walker* last = nullptr;
                for (auto& up : w.oblist)
                    if (walker* a = up.get(); a != nullptr && !a->dead() &&
                        a->query_order() == Order::Living &&
                        a->team_num() != 0)
                    {
                        if (last == nullptr && !a->dormant() &&
                            a->floor() == 0 && a->family() != FAMILY_GOLEM)
                        {
                            last = a;
                            continue;
                        }
                        a->set_dead(1);
                    }
                if (last != nullptr)
                {
                    place_at(last, 18, 49, 0);
                    last->stats()->set_hitpoints(1.0f);
                }
            }
            // The key-bearer at the big door, once the win lines are gone
            // (they run to about 1660) and the arc has been filmed
            // sparkling with all three doors shut: the lead, with key 3,
            // walks south into each of the three doors in turn (court
            // side), then on through the open middle door and the striped
            // vestibule onto the exit pad (15,45).
            const int door_x[3] = {14, 16, 15};
            for (int i = 0; i < 3; ++i)
                if (f == 1700 + 40 * i)
                    if (walker* k = lead_of(w); k != nullptr)
                    {
                        k->set_keys(k->keys() | (1u << 3));
                        k->set_act_type(ACT_GUARD);
                        place_at(k, door_x[i], 41, 0);
                        k->stats()->set_command(COMMAND_WALK, 6, 0, 1);
                    }
            if (f >= 1820)
                if (walker* k = lead_of(w);
                    k != nullptr && k->ypos() < 45 * GRID_SIZE)
                    k->walkstep(0, 1);
            if (in(f, 150, 330))
                return follow(named(w, nullptr, FAMILY_GOLEM), cx, cy, floor);
            if (in(f, 330, 1390))
                return follow(by_the_spikes(w), cx, cy, floor);
            return false;
        };
        d.keep = [](int f) { return f % 3 == 0; };
        // From 1402 the camera holds on the east gate and its spike band
        // for Black Dash's charge. From 1550 it frames the big door with
        // the whole rainbow arc below it (rows 49..55), so the win show
        // plays in view; from
        // 1700 it rises half a tile to take in the key-bearer coming down
        // to the doors (row 42) with the arc's outer ring (row 53) still
        // whole at the bottom edge.
        epic_rec::record("imag_moments_3", 3, WeatherKind::Snow, crew,
            {{0, 416, 440, 0, true}, {60, 860, 150, 0, true},
             {120, 784, 168, 1, true}, {330, 360, 480, 0, true},
             {1390, 904, 736, 0, true}, {1402, 424, 488, 0, true},
             {1550, 240, 780, 0, true},
             {1700, 248, 772, 0, true}, {1900, 248, 772, 0, true}},
            "imaginations", d);
    }
}

// ---------------------------------------------------------------------------
// Whole-level overviews: every floor of a level rendered at native tile scale
// with its placed units, as a grid of 320x200 camera tiles for stitching.
// Same setup as epic_rec::record (save, glad_init, free spectator camera);
// one sim tick deploys the crew, then nothing moves while the camera steps
// across the map. The editor floor override draws the dormant waves too,
// and the notification lines are cleared off every tile.
// Tiles land in $OG_FX_CAPTURE_DIR/<scene>/f<floor>_x<topx>_y<topy>.ppm.
// ---------------------------------------------------------------------------
namespace overview_rec {

void dump_tile(screen* s, const char* scene, int floor, int topx, int topy)
{
    char dir[512];
    gameplay_rec::dump_dir_for(scene, dir, sizeof(dir));
    char path[600];
    snprintf(path, sizeof(path), "%s/f%d_x%04d_y%04d.ppm", dir, floor, topx,
             topy);
    FILE* fp = fopen(path, "wb");
    ASSERT_NE(nullptr, fp);
    fprintf(fp, "P6\n320 200\n255\n");
    for (int j = 0; j < 200; j++)
        for (int i = 0; i < 320; i++)
        {
            Uint8 r, g, b;
            s->get_pixel(i, j, &r, &g, &b);
            fputc(r, fp);
            fputc(g, fp);
            fputc(b, fp);
        }
    fclose(fp);
}

// The tile origins along one axis: steps of `step`, the last clamped so the
// final tile ends flush with the map edge.
std::vector<int> origins(int extent, int step)
{
    std::vector<int> out;
    for (int o = 0; o < extent; o += step)
        out.push_back(o + step > extent ? extent - step : o);
    return out;
}

void record(const char* scene, int scen, const char* campaign)
{
    screen* const s = og::runtime::current_session->myscreen_;
    gameplay_rec::build_save(s, campaign, scen, 1,
        {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_ELF,
         FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN}, 7);
    glad_init();
    gameplay_rec::force_weather(WeatherKind::None);
    viewscreen* vs = s->viewob[0].get();
    ASSERT_NE(nullptr, vs);

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    vs->control = nullptr;
    ASSERT_EQ(GameFrameResult::Continue,
              game_frame_with_result(*s, st, deps));
    gameplay_rec::force_weather(WeatherKind::None);

    const int world_w = static_cast<int>(s->world().grid.w) * GRID_SIZE;
    const int world_h = static_cast<int>(s->world().grid.h) * GRID_SIZE;
    const int floors = static_cast<int>(s->world().floor_count());
    for (int floor = 0; floor < floors; ++floor)
        for (int topy : origins(world_h, 200))
            for (int topx : origins(world_w, 320))
            {
                vs->editor_floor_override_ = static_cast<Sint32>(floor);
                vs->control = nullptr;
                vs->clear_text(); // no notification lines over the map
                s->set_level_draw_pos(topx, topy);
                s->redraw();
                s->swap();
                dump_tile(s, scene, floor, topx, topy);
                if (::testing::Test::HasFatalFailure())
                    return;
            }
    vs->editor_floor_override_ = -1;
    s->world().end = 0;
    s->world().delete_objects();
}

} // namespace overview_rec

TEST(GameLoop, zz_capture_imaginations_overview)
{
    ASSERT_NE(nullptr, getenv("OG_FX_CAPTURE_DIR")) << "capture tool needs an output directory";
    const char* only = getenv("OG_FX_CAPTURE_ONLY");
    const auto want = [&](const char* n) {
        return only == nullptr || strcmp(only, n) == 0;
    };
    // Scen 2 The Castle of Gold (one floor), scen 3 The Asteroid Castle
    // (the asteroid and the Balloon Rock's sky floor).
    if (want("imag_overview_2"))
        overview_rec::record("imag_overview_2", 2, "imaginations");
    if (want("imag_overview_3"))
        overview_rec::record("imag_overview_3", 3, "imaginations");
}
