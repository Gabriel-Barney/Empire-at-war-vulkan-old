// alamo_headless --story: the speech clock (story_speech.hpp).
#include "story_speech.hpp"

#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "alamo/audio/audio_db.hpp"
#include "alamo/audio/audio_system.hpp"
#include "alamo/audio/decode.hpp"
#include "alamo/audio/sample_source.hpp"
#include "alamo/core/sim_rng.hpp"
#include "alamo/lua/story_model.hpp"
#include "alamo/sim/story_session.hpp"
#include "alamo/vfs/vfs.hpp"

namespace alamo_headless {

namespace {
constexpr double kTickSeconds = 1.0 / 30.0;

std::string_view trimmed(const std::vector<alamo::lua::StoryParam>& params, size_t i) {
    if (i >= params.size()) return {};
    std::string_view v = params[i].text;
    while (!v.empty() && static_cast<unsigned char>(v.front()) <= 0x20) v.remove_prefix(1);
    while (!v.empty() && static_cast<unsigned char>(v.back()) <= 0x20) v.remove_suffix(1);
    return v;
}

// The speech line a fired reward plays (alamo::game::story_reward_media): SPEECH Reward_Param1, MULTIMEDIA Reward_Param8.
std::string_view speech_of(const alamo::lua::StoryFired& f) {
    if (f.reward_type == "SPEECH") return trimmed(f.reward_params, 0);
    if (f.reward_type == "MULTIMEDIA") return trimmed(f.reward_params, 7);
    return {};
}
}  // namespace

struct StorySpeechClock::Impl {
    alamo::audio::AudioDatabase db;
    std::unique_ptr<alamo::audio::GameSampleSource> samples;
    std::unique_ptr<alamo::audio::NullBackend> backend;
    alamo::SimRng rng;  // FREE: the speech file pick only
    std::unique_ptr<alamo::audio::AudioSystem> system;
    std::map<std::string, std::optional<double>> lengths;  // sample file -> decoded seconds (nullopt = missing / undecodable)
    std::vector<std::string> done;                         // ended lines not handed to the story yet
    std::vector<size_t> cursor;                            // per galactic plot set: fired entries seen
    const alamo::lua::StoryModel* tac_model = nullptr;
    size_t tac_cursor = 0;
};

StorySpeechClock::StorySpeechClock() = default;
StorySpeechClock::~StorySpeechClock() = default;

bool StorySpeechClock::init(const alamo::vfs::Vfs& files, uint32_t seed, std::string* err) {
    auto m = std::make_unique<Impl>();
    if (!m->db.load(files)) {
        if (err) *err = "SFXEventFiles.xml missing";
        return false;
    }
    alamo::audio::GameSampleConfig sc;
    sc.sfx_megs = alamo::audio::GameSampleSource::default_sfx_megs(files, sc.language);
    m->samples = std::make_unique<alamo::audio::GameSampleSource>(files, sc);
    Impl* p = m.get();
    m->backend = std::make_unique<alamo::audio::NullBackend>([p](const alamo::audio::SampleRef& r) -> std::optional<double> {
        const auto it = p->lengths.find(r.file);
        if (it != p->lengths.end()) return it->second;
        std::optional<double> len;
        std::vector<uint8_t> bytes;
        if (p->samples->read(r, bytes))
            if (const std::optional<alamo::audio::AudioInfo> info = alamo::audio::probe_audio(bytes)) len = info->seconds();
        p->lengths.emplace(r.file, len);
        return len;
    });
    m->rng.seed(seed);
    m->system = std::make_unique<alamo::audio::AudioSystem>(m->db, *m->backend, m->rng);
    m->system->on_speech_done([p](const std::string& event, bool) { p->done.push_back(event); });
    impl_ = std::move(m);
    return true;
}

void StorySpeechClock::step(alamo::sim::StorySession& story, int32_t player) {
    if (!impl_) return;
    Impl& m = *impl_;
    auto scan = [&](const alamo::lua::StoryModel& model, size_t& cursor) {
        const std::vector<alamo::lua::StoryFired>& fired = model.fired();
        if (cursor > fired.size()) cursor = fired.size();
        for (; cursor < fired.size(); ++cursor) {
            const std::string_view line = speech_of(fired[cursor]);
            if (line.empty()) continue;
            m.system->stop_all_speech();  // alamo_game (STORY_PRESENTATION_REWARDS 1.4): the previous line is cut off
            if (m.system->play_speech(line)) ++speeches_;
        }
    };
    m.cursor.resize(story.plot_set_count(), 0);
    for (size_t set = 0; set < story.plot_set_count(); ++set) {
        if (story.plot_set_player(set) == player) scan(story.model(set), m.cursor[set]);
        else m.cursor[set] = story.model(set).fired().size();
    }
    const alamo::lua::StoryModel* tac = story.tactical_model();
    if (tac != m.tac_model) {  // a new battle's tactical plot set starts from its first fired entry
        m.tac_model = tac;
        m.tac_cursor = 0;
    }
    if (tac && story.tactical_player() == player) scan(*tac, m.tac_cursor);
    m.system->update(kTickSeconds);
    for (const std::string& e : m.done) {
        alamo::sim::StoryUiNotification n;
        n.kind = alamo::sim::StoryUiEvent::SpeechDone;
        n.player = player;
        n.name = e;
        n.tactical = tac != nullptr;
        story.notify_ui(n);
        ++ends_;
    }
    m.done.clear();
}

}  // namespace alamo_headless
