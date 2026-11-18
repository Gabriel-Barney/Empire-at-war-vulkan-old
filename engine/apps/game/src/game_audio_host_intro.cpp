// aud1 t07: tactical intro speech (GameAudioHost; rules: docs/re/AUDIO.md §9.7).
#include "alamo/game/game_audio_host.hpp"

#include <string>
#include <string_view>
#include <vector>

#ifdef ALAMO_GAME_HAVE_AUDIO
#include "alamo/audio/audio_system.hpp"
#include "alamo/audio/game_audio.hpp"
#include "alamo/audio/game_audio_data.hpp"
#endif

namespace alamo::game {

namespace {

char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool iequal(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i])) return false;
    return true;
}

std::string_view trim(std::string_view s) {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    while (!s.empty() && space(s.back())) s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> split_commas(std::string_view s) {
    std::vector<std::string_view> out;
    for (;;) {
        const size_t c = s.find(',');
        out.push_back(trim(s.substr(0, c)));
        if (c == std::string_view::npos) break;
        s.remove_prefix(c + 1);
    }
    return out;
}

// Last occurrence of `tag` (as audio::FactionAudio::get). [port] A value naming several events ("A, B") speaks the first:
// the shipped data keeps one event per default field (the alternatives are commented out).
std::string_view single_field(GameAudioHost::FactionFieldList fields, std::string_view tag) {
    std::string_view v;
    for (const auto& [t, value] : fields)
        if (iequal(t, tag)) v = value;
    return split_commas(v).front();
}

bool named_present(std::string_view name, const std::function<bool(std::string_view)>& present) {
    return !name.empty() && !iequal(name, "None") && present && present(name);
}

// One conditional list (0x35db50): the first entry whose test holds, "" when none does.
std::string_view conditional(GameAudioHost::FactionFieldList fields, std::string_view tag, bool both,
                             const std::function<bool(std::string_view)>& present) {
    for (const auto& [t, value] : fields) {
        if (!iequal(t, tag)) continue;
        const std::vector<std::string_view> p = split_commas(value);
        if (p.size() < 4 || p[3].empty()) continue;  // [M] a malformed or event-less entry never matches
        const bool a = named_present(p[0], present), b = named_present(p[1], present), c = named_present(p[2], present);
        if ((both ? (a && b) : (a || b)) && !c) return p[3];
    }
    return {};
}

}  // namespace

std::string GameAudioHost::intro_speech_pick(FactionFieldList fields, const IntroSpeechFacts& f,
                                             const std::function<bool(std::string_view)>& present) {
    const std::string side = f.attacker ? "Attacker" : "Defender";
    // ledger: consumes Faction.SpeechEvent_Tactical_Intro_Land_Attacker
    // ledger: consumes Faction.SpeechEvent_Tactical_Intro_Land_Defender
    const std::string base = std::string("SpeechEvent_Tactical_Intro_") + (f.land ? "Land_" : "Space_") + side;
    // ledger: consumes Faction.SpeechEvent_Tactical_Intro_Space_Attacker
    // ledger: consumes Faction.SpeechEvent_Tactical_Intro_Space_Defender
    if (const std::string_view e = conditional(fields, base + "_Conditional_Or", false, present); !e.empty()) return std::string(e);
    if (const std::string_view e = conditional(fields, base + "_Conditional_And", true, present); !e.empty()) return std::string(e);
    if (f.land && f.last_location) {
        if (const std::string_view e = single_field(fields, base + "_Last_Location"); !e.empty()) return std::string(e);
    }
    // ledger: consumes Faction.SpeechEvent_Tactical_Intro_Land_Raid_Attacker
    // ledger: consumes Faction.SpeechEvent_Tactical_Intro_Land_Raid_Defender
    if (f.land && f.raid) {
        if (const std::string_view e = single_field(fields, "SpeechEvent_Tactical_Intro_Land_Raid_" + side); !e.empty())
            return std::string(e);
    }
    if (f.land && f.attacker && !f.defender_playable) return {};
    return std::string(single_field(fields, base));
}

GameAudioHost::IntroGate GameAudioHost::intro_speech_gate(bool eligible, uint32_t battle_frame, bool cinematic, bool speech_busy) {
    if (!eligible) return IntroGate::Stop;
    constexpr uint32_t kIntroDelayFrames = 10u * 30u;  // 10 s x FPS (0x140b0a340 = 30) of mode frames
    if (cinematic || battle_frame < kIntroDelayFrames || speech_busy) return IntroGate::Wait;
    return IntroGate::Speak;
}

std::string GameAudioHost::intro_speech_tick(uint32_t battle_frame, bool eligible, bool cinematic,
                                             const std::function<bool(IntroSpeechFacts&)>& facts,
                                             const std::function<bool(std::string_view)>& present) {
    if (battle_frame < intro_frame_) intro_decided_ = false;  // a new battle (its frames restart)
    intro_frame_ = battle_frame;
    if (intro_decided_) return {};
    bool busy = false;
#ifdef ALAMO_GAME_HAVE_AUDIO
    // [M] "the speech queue is not empty" read as: an entry queued or one playing.
    busy = system_ && (system_->speech_queue_size() > 0 || system_->speech_playing());
#endif
    const IntroGate g = intro_speech_gate(eligible, battle_frame, cinematic, busy);
    if (g == IntroGate::Wait) return {};
    intro_decided_ = true;  // step 3: set before the side test, so a battle without a local side stays silent
    if (g == IntroGate::Stop) return {};
    IntroSpeechFacts f;
    if (!facts || !facts(f)) return {};
#ifdef ALAMO_GAME_HAVE_AUDIO
    if (!game_ || !game_data_) return {};
    const audio::FactionAudio* fa = game_data_->find_faction(game_->local_faction());
    if (!fa) return {};
    std::string event = intro_speech_pick(fa->fields, f, present);
    speech(event);
    return event;
#else
    (void)present;
    return {};
#endif
}

}  // namespace alamo::game
