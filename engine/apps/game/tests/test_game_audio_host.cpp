// alamo_game_audio_host_tests: GameAudioHost's movie music (STRATEGIC_GAPS §7.3 / §7.4), data-free: a synthetic audio tree
// in memory and the null backend. The movie-music flag (Movies.xml Has_Credits) starts mode 14 music and the movie's end
// restores the music it replaced; Halt_Game_Music stops the music for the movie and brings the mode music back after it;
// a MusicEvent_Frame cue is mode 17. Movies without those flags never reach the host (MovieSlot only calls it for them).
// Without engine/audio the host is inactive: the test then only checks that every call is a harmless no-op.
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "alamo/core/file_provider.hpp"
#include "alamo/game/game_audio_host.hpp"
#include "alamo/game/game_data.hpp"
#include "alamo/tactical/combat/hero_clash.hpp"
#include "alamo/objects/object_types.hpp"            // aud1 t05
#include "alamo/tactical/abilities/ability_types.hpp"  // aud1 t05
#include "alamo/test/support.hpp"
#ifdef ALAMO_GAME_HAVE_AUDIO
#include "alamo/audio/audio_system.hpp"
#endif

using namespace alamo;
using namespace alamo::game;

using ::alamo::test::g_fail;

namespace {

class MemoryProvider final : public FileProvider {
public:
    void add(const std::string& path, const std::string& text) {
        files_[normalize_path(path)] = std::vector<uint8_t>(text.begin(), text.end());
    }
    bool read(std::string_view path, std::vector<uint8_t>& out) const override {
        auto it = files_.find(normalize_path(path));
        if (it == files_.end()) return false;
        out = it->second;
        return true;
    }
    bool exists(std::string_view path) const override { return files_.count(normalize_path(path)) != 0; }
    std::string origin(std::string_view path) const override { return exists(path) ? "mem:" + std::string(path) : ""; }

private:
    std::map<std::string, std::vector<uint8_t>> files_;
};

const char* kSfx = R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="GUI_Click"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Is_GUI>Yes</Is_GUI><Samples>CLICK.WAV</Samples></SFXEvent>
</SFXEvents>
)";
const char* kMusic = R"(<?xml version="1.0"?>
<MusicEvents>
  <MusicEvent Name="Gal_Rebel"><Files>G1.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Game_Credits_Music_Event"><Files>C1.MP3, C2.MP3, C3.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Main_Menu_Music_Event"><Files>MENU.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Movie_Cue"><Files>CUE.MP3</Files></MusicEvent>
  <MusicEvent Name="Amb_Space"><Files>AS.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Amb_Space_DS"><Files>ADS.MP3</Files><Loop>Yes</Loop></MusicEvent>
</MusicEvents>
)";
const char* kFactions = R"(<?xml version="1.0" ?>
<Factions>
  <Faction Name="Rebel"><Strategic_Map_Music_Event> Gal_Rebel </Strategic_Map_Music_Event>
    <Music_Event_List_Ambient> Space, Amb_Space </Music_Event_List_Ambient>
    <Music_Event_Space_Ambient_Super_Weapon> Amb_Space_DS </Music_Event_Space_Ambient_Super_Weapon></Faction>
</Factions>
)";

[[maybe_unused]] std::unique_ptr<MemoryProvider> make_tree(bool credits_music) {
    auto p = std::make_unique<MemoryProvider>();
    p->add("Data\\XML\\SFXEventFiles.xml", "<SFXEvent_Files><File>SFXEventsTest.xml</File></SFXEvent_Files>");
    p->add("Data\\XML\\SFXEventsTest.xml", kSfx);
    std::string music = kMusic;
    if (!credits_music) {  // the event the credits movie asks for is missing from the database
        const size_t a = music.find("  <MusicEvent Name=\"Game_Credits_Music_Event\"");
        const size_t b = music.find('\n', a);
        music.erase(a, b - a + 1);
    }
    p->add("Data\\XML\\MusicEvents.xml", music);
    p->add("Data\\XML\\SpeechEvents.xml", "<SpeechEvents/>");
    p->add("Data\\XML\\Audio.xml", "<Audio/>");
    p->add("Data\\XML\\FactionFiles.xml", "<Faction_Files><File>Factions.xml</File></Faction_Files>");
    p->add("Data\\XML\\Factions.xml", kFactions);
    return p;
}

#ifdef ALAMO_GAME_HAVE_AUDIO
std::string cur(GameAudioHost& h) { return h.system()->current_music(); }
uint8_t mode(GameAudioHost& h) { return h.system()->music_mode(); }

void test_movie_music() {
    GameData data;
    data.files = make_tree(true);
    GameAudioHost h;
    std::string err;
    CHECK(h.init(data, AudioBackendKind::Null, 7, &err));
    CHECK(h.active() && h.system());
    if (!h.active()) return;
    h.music(MusicScene::Galactic, "Rebel");
    CHECK(cur(h) == "Gal_Rebel" && mode(h) == audio::music_mode::kGalactic);

    // The movie-music flag (Has_Credits): mode 14, the credits event; the movie ending restores the galactic music.
    h.movie_started(true, false);
    CHECK(cur(h) == "Game_Credits_Music_Event" && mode(h) == audio::music_mode::kMovie && mode(h) == 14);
    CHECK(h.stats().movie_music == 1);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel" && mode(h) == audio::music_mode::kGalactic);
    CHECK(h.stats().movie_restores == 1);
    h.movie_ended();  // nothing to undo: no change
    CHECK(cur(h) == "Gal_Rebel" && h.stats().movie_restores == 1);

    // Halt_Game_Music: silence while the movie plays, the mode music comes back with the movie's end.
    h.movie_started(false, true);
    CHECK(cur(h).empty() && h.stats().movie_halts == 1);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel" && mode(h) == audio::music_mode::kGalactic);

    // Halt wins over the movie-music flag (a halted movie plays no music of its own).
    h.movie_started(true, true);
    CHECK(cur(h).empty() && h.stats().movie_music == 1);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel");

    // A music cue (17) replaces the music; the movie's end restores it.
    h.movie_cue("Movie_Cue");
    CHECK(cur(h) == "Movie_Cue" && mode(h) == audio::music_mode::kMovieCue && mode(h) == 17);
    CHECK(h.stats().movie_cues == 1);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel" && mode(h) == audio::music_mode::kGalactic);
    h.movie_cue("");  // empty names and unknown events change nothing
    h.movie_cue("No_Such_Event");
    CHECK(cur(h) == "Gal_Rebel");
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel");

    // The movie's music already gave way to something else (the new mode's music): ending the movie leaves it alone.
    h.movie_started(true, false);
    CHECK(mode(h) == audio::music_mode::kMovie);
    h.music(MusicScene::MainMenu, "Rebel");  // a mode change replaces the movie music
    h.movie_ended();
    CHECK(mode(h) != audio::music_mode::kMovie);
}

void test_missing_credits_event() {
    GameData data;
    data.files = make_tree(false);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 7));
    if (!h.active()) return;
    h.music(MusicScene::Galactic, "Rebel");
    h.movie_started(true, false);  // no such event: the music stays, nothing to restore
    CHECK(cur(h) == "Gal_Rebel" && h.stats().movie_music == 0);
    h.movie_ended();
    CHECK(cur(h) == "Gal_Rebel" && h.stats().movie_restores == 0);
}

// aud4 b0286 (AUDIO.md §2.3): the first super-weapon report of a battle re-picks the ambient music with the
// _Super_Weapon variant; later reports only set the flag; battle_reset starts a new battle's first report.
void test_super_weapon_music() {
    GameData data;
    data.files = make_tree(true);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 7));
    if (!h.active()) return;
    h.music(MusicScene::Battle, "Rebel", true);
    CHECK(cur(h) == "Amb_Space" && mode(h) == audio::music_mode::kTacticalAmbient);
    h.super_weapon_report(true);
    CHECK(cur(h) == "Amb_Space_DS" && mode(h) == audio::music_mode::kTacticalAmbient);
    h.super_weapon_report(false);  // the super weapon died: the music stays until the next mode switch
    CHECK(cur(h) == "Amb_Space_DS");
    h.battle_reset();
    h.super_weapon_report(true);  // a new battle's first report: the same event is already playing (not restarted)
    CHECK(cur(h) == "Amb_Space_DS");
    h.battle_reset();
    h.super_weapon_report(false);
    CHECK(cur(h) == "Amb_Space");
}
#endif

// aud4 b0289 / b0300: garrison entry line and the hero clash win speech (both builds; presentation rules only).
void test_garrison_and_clash_lines() {
    using H = GameAudioHost;
    CHECK(H::garrison_line(1, 1) == "SFXEvent_Bunker_Garrisoned");
    CHECK(H::garrison_line(2, 1).empty());   // an enemy / ally unit entering: no local line
    CHECK(H::garrison_line(-1, -1).empty());  // no local player
    tactical::combat::HeroClashData d;
    d.entries.resize(2);
    d.entries[1].first_speech = "Vader_Win";
    d.entries[1].second_speech = "Obiwan_Win";
    CHECK(H::clash_win_speech(&d, 1, true) == "Vader_Win");
    CHECK(H::clash_win_speech(&d, 1, false) == "Obiwan_Win");
    CHECK(H::clash_win_speech(&d, 0, true).empty());
    CHECK(H::clash_win_speech(&d, 5, true).empty() && H::clash_win_speech(nullptr, 0, true).empty());
}

void test_inactive() {
    GameData data;
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Off, 1));
    CHECK(!h.active());
    h.movie_started(true, true);
    h.movie_cue("Movie_Cue");
    h.movie_ended();
    CHECK(h.stats().movie_music == 0 && h.stats().movie_cues == 0 && h.stats().movie_halts == 0);
}

// ccpgu t01 (SKIRMISH_SETUP 4.2 announcer): the countdown lines at 60 / 30 / 15 / 5 s (x 30 frames), by the leader's relation
// to the local player; none for a Neutral observer or while not armed. Both builds (the decision is presentation logic only).
void test_control_point_lines() {
    using H = GameAudioHost;
    const std::string_view owned30 = "SFXEvent_HUD_Reinforcement_Point_Owned_30_Seconds";
    // Arming at 900 (the shipped 30 s), seen as 900 or, when the first countdown step fell in the same tick, 899.
    CHECK(H::control_point_line(-1, 900, 1, 1, false) == owned30);
    CHECK(H::control_point_line(-1, 899, 1, 1, false) == owned30);
    CHECK(H::control_point_line(-1, 899, 2, 1, false) == "SFXEvent_HUD_Reinforcement_Point_Ally_Owned_30_Seconds");
    CHECK(H::control_point_line(-1, 899, 2, 1, true) == "SFXEvent_HUD_Reinforcement_Point_Enemy_Owned_30_Seconds");
    CHECK(H::control_point_line(-1, 899, 2, -1, true).empty());  // no local player
    CHECK(H::control_point_line(5, -1, -1, 1, false).empty());   // disarmed
    // Counting down: each mark once, on the tick that reaches it.
    CHECK(H::control_point_line(899, 898, 1, 1, false).empty());
    CHECK(H::control_point_line(451, 450, 1, 1, false) == "SFXEvent_HUD_Reinforcement_Point_Owned_15_Seconds");
    CHECK(H::control_point_line(450, 449, 1, 1, false).empty());
    CHECK(H::control_point_line(151, 150, 2, 1, true) == "SFXEvent_HUD_Reinforcement_Point_Enemy_Owned_05_Seconds");
    CHECK(H::control_point_line(1801, 1800, 1, 1, false) == "SFXEvent_HUD_Reinforcement_Point_Owned_60_Seconds");
    // A re-arm (900 again after a capture) plays the 30 s line again.
    CHECK(H::control_point_line(300, 900, 1, 1, false) == owned30);
    // Over the shipped 30 s countdown exactly three lines: 30 s at arming, 15 s, 5 s.
    int lines = 0, prev = -1;
    for (int t = 900; t >= 0; --t) {
        lines += H::control_point_line(prev, t, 1, 1, false).empty() ? 0 : 1;
        prev = t;
    }
    CHECK(lines == 3);
}

// aud1 t07 (AUDIO.md §9.7): the tactical intro speech pick, its gate, and (with engine/audio) one queued line.
void test_intro_speech_t07() {
    using H = GameAudioHost;
    using P = std::pair<std::string, std::string>;
    const std::vector<P> rebel = {  // shaped like the shipped Factions.xml Rebel entries
        {"SpeechEvent_Tactical_Intro_Land_Attacker", "LAND_ATT"},
        {"SpeechEvent_Tactical_Intro_Land_Defender", "LAND_DEF"},
        {"SpeechEvent_Tactical_Intro_Space_Attacker", "SPACE_ATT, SPACE_ATT_ALT"},
        {"SpeechEvent_Tactical_Intro_Space_Defender", "SPACE_DEF"},
        {"SpeechEvent_Tactical_Intro_Land_Raid_Attacker", "RAID_ATT"},
        {"SpeechEvent_Tactical_Intro_Land_Raid_Defender", ""},
        {"SpeechEvent_Tactical_Intro_Land_Attacker_Last_Location", "LAST_ATT"},
        {"SpeechEvent_Tactical_Intro_Land_Defender_Last_Location", "LAST_DEF"},
        {"SpeechEvent_Tactical_Intro_Space_Defender_Conditional_And", "Death_Star, Luke_Skywalker, None, AND_DS_LUKE"},
        {"SpeechEvent_Tactical_Intro_Space_Defender_Conditional_Or", "Death_Star, None, Luke_Skywalker, OR_DS_NO_LUKE"},
        {"SpeechEvent_Tactical_Intro_Space_Defender_Conditional_Or", "Mon_Mothma, None, None, OR_MOTHMA"},
    };
    std::vector<std::string> here;
    const std::function<bool(std::string_view)> present = [&here](std::string_view n) {
        for (const std::string& s : here)
            if (s == n) return true;
        return false;
    };
    H::IntroSpeechFacts f;
    // Defaults by side and map; a multi-event value speaks its first event [port].
    f.land = true;
    f.attacker = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAND_ATT");
    f.attacker = false;
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAND_DEF");
    f.land = false;
    f.attacker = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_ATT");
    f.attacker = false;
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_DEF");
    // Conditional lists: Or before And (the first match wins), C absent required, "None" never present.
    here = {"Death_Star"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "OR_DS_NO_LUKE");
    here = {"Death_Star", "Luke_Skywalker"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "AND_DS_LUKE");
    here = {"Death_Star", "Luke_Skywalker", "Mon_Mothma"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "OR_MOTHMA");  // the Or list is searched completely first
    here = {"Luke_Skywalker"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_DEF");
    f.attacker = true;  // the defender's lists are not the attacker's
    here = {"Mon_Mothma"};
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_ATT");
    here.clear();
    // Land: last location before raid before the default; space ignores both.
    f.land = true;
    f.last_location = true;
    f.raid = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAST_ATT");
    f.last_location = false;
    CHECK(H::intro_speech_pick(rebel, f, present) == "RAID_ATT");
    f.attacker = false;  // an empty raid field falls through to the default
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAND_DEF");
    f.land = false;
    f.last_location = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "SPACE_DEF");
    // The land attacker default needs a playable defender; the other defaults do not.
    f = {};
    f.land = true;
    f.attacker = true;
    f.defender_playable = false;
    CHECK(H::intro_speech_pick(rebel, f, present).empty());
    f.raid = true;
    CHECK(H::intro_speech_pick(rebel, f, present) == "RAID_ATT");
    f.raid = false;
    f.attacker = false;
    CHECK(H::intro_speech_pick(rebel, f, present) == "LAND_DEF");
    CHECK(H::intro_speech_pick({}, f, present).empty());  // a faction without the fields says nothing

    // Gate: 10 s x 30 fps of battle frames, no cinematic, no speech; never outside a campaign / in a linked battle.
    CHECK(H::intro_speech_gate(false, 1000, false, false) == H::IntroGate::Stop);
    CHECK(H::intro_speech_gate(true, 299, false, false) == H::IntroGate::Wait);
    CHECK(H::intro_speech_gate(true, 300, false, false) == H::IntroGate::Speak);
    CHECK(H::intro_speech_gate(true, 300, true, false) == H::IntroGate::Wait);
    CHECK(H::intro_speech_gate(true, 300, false, true) == H::IntroGate::Wait);

    // The tick: decided once per battle; a lower frame starts a new battle. Inactive host: nothing queued.
    {
        GameAudioHost idle;
        int asked = 0;
        const auto facts = [&asked](H::IntroSpeechFacts& x) {
            ++asked;
            x.land = false;
            return true;
        };
        CHECK(idle.intro_speech_tick(10, true, false, facts, present).empty() && asked == 0);
        CHECK(idle.intro_speech_tick(300, true, false, facts, present).empty() && asked == 1);
        CHECK(idle.intro_speech_tick(301, true, false, facts, present).empty() && asked == 1);
        CHECK(idle.intro_speech_tick(0, true, false, facts, present).empty() && asked == 1);
        CHECK(idle.intro_speech_tick(300, true, false, facts, present).empty() && asked == 2);
    }
#ifdef ALAMO_GAME_HAVE_AUDIO
    {
        GameData data;
        auto tree = make_tree(true);
        tree->add("Data\\XML\\SpeechEvents.xml",
                  "<SpeechEvents><SpeechEvent Name=\"GRS_INTRO_SPACE_DEFEND_R_00\"><Files> I_ENG.MP3 </Files></SpeechEvent>"
                  "</SpeechEvents>");
        tree->add("Data\\XML\\Factions.xml",
                  "<Factions><Faction Name=\"Rebel\"><Strategic_Map_Music_Event> Gal_Rebel </Strategic_Map_Music_Event>"
                  "<SpeechEvent_Tactical_Intro_Space_Defender> GRS_INTRO_SPACE_DEFEND_R_00 </SpeechEvent_Tactical_Intro_Space_Defender>"
                  "</Faction></Factions>");
        data.files = std::move(tree);
        GameAudioHost h;
        std::string err;
        CHECK(h.init(data, AudioBackendKind::Null, 7, &err));
        if (!h.active()) return;
        h.music(MusicScene::Galactic, "Rebel");
        const auto defender = [](H::IntroSpeechFacts& x) {
            x.land = false;
            x.attacker = false;
            return true;
        };
        const uint64_t before = h.stats().speech;
        CHECK(h.intro_speech_tick(299, true, false, defender, present).empty());
        CHECK(h.intro_speech_tick(300, true, false, defender, present) == "GRS_INTRO_SPACE_DEFEND_R_00");
        CHECK(h.stats().speech == before + 1);
        CHECK(h.intro_speech_tick(400, true, false, defender, present).empty() && h.stats().speech == before + 1);
        // Not the local player's battle side: decided, silent.
        CHECK(h.intro_speech_tick(0, true, false, [](H::IntroSpeechFacts&) { return false; }, present).empty());
    }
#endif
}


#ifdef ALAMO_GAME_HAVE_AUDIO
// aud1 (AUDIO.md §9): a scripted land battle (Temperate, rain) and a space battle through GameAudioHost - ambient then
// battle music from the map terrain's list, the weather loop and SFXEvent_Weather_Begin, an AnimationSFXMaps SOUND row,
// a hardpoint Fire_SFXEvent, the base-attack line (rate-limited), and TSW / Lua Play_Music not replaced by combat.
const char* kAud1Sfx = R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Rain_Line"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>RAIN_LINE.WAV</Samples></SFXEvent>
  <SFXEvent Name="Rain_Loop"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Play_Count>-1</Play_Count><Samples>RAIN.WAV</Samples></SFXEvent>
  <SFXEvent Name="Base_Line"><Is_2D>Yes</Is_2D><Is_3D>No</Is_3D><Samples>BASE.WAV</Samples></SFXEvent>
  <SFXEvent Name="Anim_Stomp"><Samples>STOMP.WAV</Samples><Max_Instances>4</Max_Instances></SFXEvent>
  <SFXEvent Name="HP_Turbolaser"><Samples>TL.WAV</Samples><Max_Instances>8</Max_Instances></SFXEvent>
  <SFXEvent Name="Ship_Fire"><Samples>SF.WAV</Samples><Max_Instances>8</Max_Instances></SFXEvent>
  <SFXEvent Name="Hyper_Arrive"><Samples>HYPER.WAV</Samples><Max_Instances>8</Max_Instances></SFXEvent>
</SFXEvents>
)";
const char* kAud1Music = R"(<?xml version="1.0"?>
<MusicEvents>
  <MusicEvent Name="Amb_Temperate"><Files>AT.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Bat_Temperate"><Files>BT.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Amb_Desert"><Files>AD.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Amb_Space"><Files>AS.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="Bat_Space"><Files>BS.MP3</Files><Loop>Yes</Loop></MusicEvent>
  <MusicEvent Name="TSW_Song"><Files>TSW.MP3</Files></MusicEvent>
  <MusicEvent Name="Lua_Song"><Files>LUA.MP3</Files></MusicEvent>
</MusicEvents>
)";
const char* kAud1Factions = R"(<?xml version="1.0" ?>
<Factions>
  <Faction Name="Rebel">
    <Music_Event_List_Ambient> Space, Amb_Space </Music_Event_List_Ambient>
    <Music_Event_List_Ambient> Temperate, Amb_Temperate </Music_Event_List_Ambient>
    <Music_Event_List_Ambient> Desert, Amb_Desert </Music_Event_List_Ambient>
    <Music_Event_List_Battle> Space, Bat_Space </Music_Event_List_Battle>
    <Music_Event_List_Battle> Temperate, Bat_Temperate </Music_Event_List_Battle>
    <SFXEvent_Weather_Begin> RAIN, Rain_Line </SFXEvent_Weather_Begin>
    <SFXEvent_Land_Base_Under_Attack_Announcement> Base_Line </SFXEvent_Land_Base_Under_Attack_Announcement>
    <SFXEvent_Arrive_From_Hyperspace> Hyper_Arrive </SFXEvent_Arrive_From_Hyperspace>
  </Faction>
</Factions>
)";
const char* kAud1Weather = R"(<?xml version="1.0" ?>
<WeatherAudio>
  <Weather_SFXEvent_Loop> CLEAR, 0.00, None </Weather_SFXEvent_Loop>
  <Weather_SFXEvent_Loop> RAIN, 0.01, Rain_Loop </Weather_SFXEvent_Loop>
  <Weather_SFXEvent_Loop> RAIN, 0.00, None </Weather_SFXEvent_Loop>
</WeatherAudio>
)";

int starts(GameAudioHost& h, std::string_view ev) {
    int n = 0;
    for (const audio::LogEntry& e : h.system()->log()) n += e.op == audio::LogOp::SfxStart && e.event == ev ? 1 : 0;
    return n;
}

void test_battle_hookups_aud1() {
    using H = GameAudioHost;
    // Pure rules first: terrain keys (map mini 06, §9.1), fire event choice (§9.5), base-attack test (§9.8).
    CHECK(H::terrain_name(0) == "Temperate" && H::terrain_name(6) == "Urban" && H::terrain_name(7) == "Space");
    CHECK(H::terrain_name(8).empty());
    CHECK(H::fire_event("HP_Turbolaser", "Ship_Fire") == "HP_Turbolaser" && H::fire_event("", "Ship_Fire") == "Ship_Fire");
    CHECK(H::fire_event("", "").empty());
    CHECK(H::base_attacked(true, true, 1, 1, false, false));
    CHECK(!H::base_attacked(false, true, 1, 1, false, false));  // not a base object (a unit, a plain turret)
    CHECK(!H::base_attacked(true, false, 1, 1, false, false));  // attacker not hostile
    CHECK(!H::base_attacked(true, true, 2, 1, true, false));    // an ally's base without Is_Community_Property
    CHECK(H::base_attacked(true, true, 2, 1, true, true));      // ... with it
    CHECK(!H::base_attacked(true, true, 3, 1, false, true));    // an enemy's base
    CHECK(H::weather_name(1) == "RAIN" && H::weather_name(0) == "CLEAR" && H::weather_name(9) == "CLEAR");

    auto tree = std::make_unique<MemoryProvider>();
    tree->add("Data\\XML\\SFXEventFiles.xml", "<SFXEvent_Files><File>SFXEventsAud1.xml</File></SFXEvent_Files>");
    tree->add("Data\\XML\\SFXEventsAud1.xml", kAud1Sfx);
    tree->add("Data\\XML\\MusicEvents.xml", kAud1Music);
    tree->add("Data\\XML\\SpeechEvents.xml", "<SpeechEvents/>");
    tree->add("Data\\XML\\Audio.xml",
              "<Audio><Delay_Between_Land_Base_Attack_Announcement_Seconds> 60 </Delay_Between_Land_Base_Attack_Announcement_Seconds>"
              "<Music_Land_Battle_To_Ambient_Peace_Seconds> 15.0 </Music_Land_Battle_To_Ambient_Peace_Seconds></Audio>");
    tree->add("Data\\XML\\FactionFiles.xml", "<Faction_Files><File>Factions.xml</File></Faction_Files>");
    tree->add("Data\\XML\\Factions.xml", kAud1Factions);
    tree->add("Data\\XML\\WeatherAudio.xml", kAud1Weather);
    GameData data;
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 11));
    if (!h.active()) return;
    const Vec3 p{10.0f, 20.0f, 0.0f};
    h.set_object_positions({{5, p}, {6, p}});

    // Land battle, Temperate map: ambient music from the Temperate row (mode 6).
    h.music(MusicScene::Battle, "Rebel", false, H::terrain_name(0));
    CHECK(cur(h) == "Amb_Temperate" && mode(h) == audio::music_mode::kTacticalAmbient);
    // Rain starts: the faction line once (intensity 0 -> > 0), the weather loop on the next update.
    h.weather("RAIN", 0.6f);
    h.weather("RAIN", 0.8f);
    CHECK(starts(h, "Rain_Line") == 1);
    h.update(0.05);
    CHECK(starts(h, "Rain_Loop") == 1);
    // An AnimationSFXMaps SOUND row, attached to its object.
    const H::AnimSound stomp[] = {{5, "Anim_Stomp", p}};
    CHECK(h.animation_sounds(stomp) == 1 && h.stats().anim_sounds == 1);
    // A hardpoint shot plays the hardpoint's Fire_SFXEvent, not the ship's SFXEvent_Fire.
    CHECK(h.weapon_fired(6, "HP_Turbolaser", "Ship_Fire", p) != 0);
    CHECK(starts(h, "HP_Turbolaser") == 1 && starts(h, "Ship_Fire") == 0);
    // Combat involving the local player: battle music from the Temperate row (mode 7).
    h.combat(true, false);
    CHECK(cur(h) == "Bat_Temperate" && mode(h) == audio::music_mode::kTacticalBattle);
    // Base under attack: the faction line, then nothing for 60 s x 30 ticks.
    h.combat(false, true);
    CHECK(starts(h, "Base_Line") == 1);
    h.update(2.0);
    h.combat(false, true);
    CHECK(starts(h, "Base_Line") == 1);
    for (int t = 0; t < 1800; ++t) h.sim_tick();  // also past the 15 s peace timeout: back to ambient
    CHECK(cur(h) == "Amb_Temperate");
    h.update(2.0);  // the first line's instance ends (Max_Instances 1)
    h.update(2.0);
    h.combat(false, true);
    CHECK(starts(h, "Base_Line") == 2);
    // TSW activation music (mode 8) and Lua Play_Music (mode 12) are not replaced by combat (§9.2).
    h.system()->play_music("TSW_Song", audio::music_mode::kSuperWeapon, false, false);
    h.combat(true, false);
    CHECK(cur(h) == "TSW_Song" && mode(h) == audio::music_mode::kSuperWeapon);
    h.system()->play_music("Lua_Song", audio::music_mode::kLua, false, true);
    h.combat(true, false);
    for (int t = 0; t < 900; ++t) h.sim_tick();  // no peace timeout out of mode 12 either
    CHECK(cur(h) == "Lua_Song" && mode(h) == audio::music_mode::kLua);

    // Another land terrain picks its own row.
    h.music(MusicScene::None);
    h.music(MusicScene::Battle, "Rebel", false, H::terrain_name(2));
    CHECK(cur(h) == "Amb_Desert");

    // Space battle: the Space row, ambient then battle.
    h.music(MusicScene::None);
    h.music(MusicScene::Battle, "Rebel", true, H::terrain_name(7));
    CHECK(cur(h) == "Amb_Space" && mode(h) == audio::music_mode::kTacticalAmbient);
    h.combat(true, false);
    CHECK(cur(h) == "Bat_Space" && mode(h) == audio::music_mode::kTacticalBattle);
    // A hyperspace arrival: the owner faction's SFXEvent_Arrive_From_Hyperspace, attached to the ship (§9.6).
    CHECK(h.hyperspace_arrival(6, "Rebel", p) != 0 && starts(h, "Hyper_Arrive") == 1 && h.hyperspace_sfx_played() == 1);
    CHECK(h.hyperspace_arrival(6, "", p) == 0);  // no owner faction: nothing
}
#endif

// aud1 t05 (AUDIO.md §9.6): the ability tag pick through the type's Unit_Ability -> <Abilities> entry, the departure tag
// pick, the shield transitions, and (with engine/audio) the activation playing on the user and the target.
void test_object_sfx_t05() {
    using H = GameAudioHost;
    MemoryProvider files;
    files.add("Data\\XML\\GameObjectFiles.xml", "<L><File>T05.xml</File></L>");
    files.add("Data\\XML\\T05.xml", R"(<?xml version="1.0"?>
<SpaceUnits>
  <SpaceUnit Name="Hero_Ship">
    <Unit_Abilities_Data SubObjectList="Yes">
      <Unit_Ability><Type>STEALTH</Type><GUI_Activated_Ability_Name> Hide </GUI_Activated_Ability_Name></Unit_Ability>
      <Unit_Ability><Type>SPRINT</Type></Unit_Ability>
    </Unit_Abilities_Data>
    <Abilities SubObjectList="Yes">
      <Stealth_Ability Name="Hide"><SFXEvent_Activate> Unit_Stealth_On </SFXEvent_Activate></Stealth_Ability>
      <Force_Healing_Ability Name="Heal2"><SFXEvent_Activate>Heal_On</SFXEvent_Activate></Force_Healing_Ability>
    </Abilities>
  </SpaceUnit>
  <SpaceUnit Name="Healer">
    <Abilities SubObjectList="Yes">
      <Force_Healing_Ability Name="Heal"><SFXEvent_Activate>Heal_On</SFXEvent_Activate>
        <SFXEvent_Target_Affected>Heal_Target</SFXEvent_Target_Affected></Force_Healing_Ability>
    </Abilities>
  </SpaceUnit>
</SpaceUnits>
)");
    objects::ObjectTypeDatabase raw;
    CHECK(raw.load(files));
    objects::GameObjectTypes types;
    CHECK(types.build(raw));
    const objects::TypeIndex hero = types.resolve_name("Hero_Ship"), healer = types.resolve_name("Healer");
    CHECK(hero != objects::kNone && healer != objects::kNone);
    if (hero == objects::kNone || healer == objects::kNone) return;
    namespace ta = tactical::abilities;
    H::AbilitySfx s = H::ability_sfx(types, hero, ta::kStealth);  // forwarded by name
    CHECK(s.activate == "Unit_Stealth_On" && s.target_affected.empty());
    CHECK(H::ability_sfx(types, hero, ta::kSprint).activate.empty());  // a record without a special: nothing
    CHECK(H::ability_sfx(types, hero, 0).activate.empty());            // two entries, no record: no guess
    s = H::ability_sfx(types, healer, ta::kAreaEffectHeal);            // [port] the only entry
    CHECK(s.activate == "Heal_On" && s.target_affected == "Heal_Target");
    CHECK(H::ability_sfx(types, objects::kNone, ta::kStealth).activate.empty());

    CHECK(H::departure_sfx("Type_Jump", "Faction_Exit") == "Type_Jump");
    CHECK(H::departure_sfx("", "Faction_Exit") == "Faction_Exit");
    CHECK(H::shield_transition(-1, 0.0f) == 0 && H::shield_transition(-1, 1.0f) == 0);
    CHECK(H::shield_transition(1, 0.0f) == 1 && H::shield_transition(1, 0.5f) == 0);
    CHECK(H::shield_transition(0, 0.01f) == 2 && H::shield_transition(0, 0.0f) == 0);

    GameAudioHost off;  // never initialised: no-op
    off.ability_effect(types, healer, ta::kAreaEffectHeal, 5, Vec3{}, 6, Vec3{});
    CHECK(off.ability_sfx_played() == 0);
#ifdef ALAMO_GAME_HAVE_AUDIO
    GameData data;
    auto tree = make_tree(true);
    tree->add("Data\\XML\\SFXEventsTest.xml", R"(<?xml version="1.0"?>
<SFXEvents>
  <SFXEvent Name="Heal_On"><Is_3D>Yes</Is_3D><Samples>HEAL.WAV</Samples></SFXEvent>
  <SFXEvent Name="Heal_Target"><Is_3D>Yes</Is_3D><Samples>HEALT.WAV</Samples></SFXEvent>
</SFXEvents>
)");
    data.files = std::move(tree);
    GameAudioHost h;
    CHECK(h.init(data, AudioBackendKind::Null, 3));
    if (!h.active()) return;
    h.set_listener(Vec3{0, 0, 100}, Vec3{0, 0, 0});
    h.ability_effect(types, healer, ta::kAreaEffectHeal, 5, Vec3{10, 0, 0}, 6, Vec3{20, 0, 0});
    CHECK(h.ability_sfx_played() == 2);
    bool user = false, target = false;
    for (const audio::LogEntry& e : h.system()->log()) {
        if (e.op != audio::LogOp::SfxStart) continue;
        user = user || (e.event == "Heal_On" && e.owner == 5);
        target = target || (e.event == "Heal_Target" && e.owner == 6);
    }
    CHECK(user && target);
    h.ability_effect(types, healer, ta::kAreaEffectHeal, 5, Vec3{10, 0, 0}, 0, Vec3{});  // no target: the activation only
    CHECK(h.ability_sfx_played() == 3);
#endif
}

}  // namespace

int main() {
    test_object_sfx_t05();
    test_inactive();
    test_control_point_lines();
    test_garrison_and_clash_lines();
    test_intro_speech_t07();
#ifdef ALAMO_GAME_HAVE_AUDIO
    test_movie_music();
    test_missing_credits_event();
    test_super_weapon_music();
    test_battle_hookups_aud1();
#endif
    std::printf(g_fail == 0 ? "game audio host tests passed\n" : "%d game audio host check(s) FAILED\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
