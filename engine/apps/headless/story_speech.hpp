// alamo_headless --story: the speech clock of a headless story run (storyb t17).
//
// The game reports a story speech's end (STORY_SPEECH_DONE) when the audio system finishes the line (alamo_game: the
// fired MULTIMEDIA / SPEECH rewards of the local player's plot sets -> GameAudioHost::speech -> AudioSystem::on_speech_done
// -> StorySession::notify_ui). A headless run has no audio, so every dialog line waited the event's 60 s Timeout and a
// scripted cinematic that waits for each line (UM01's CINE_Start_Mission_B) ran minutes late. StorySpeechClock plays the
// same rewards through an AudioSystem on the null backend, each sample lasting its real decoded length
// (audio::probe_audio), advanced 1/30 s per tick, and hands the ends to the story session after the tick, as alamo_game's
// benchmark mode does (app_benchmark.cpp: the audio clock advances 1/30 s per tick). [port] The sim sees the ends through
// notify_ui, the path the game's recorded story UI commands take; the result is the same in every run and worker count
// (sample lengths and the free RNG's file pick are fixed by the data and the seed).
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace alamo {
class FileProvider;
namespace vfs {
class Vfs;
}
namespace sim {
class StorySession;
}
}  // namespace alamo

namespace alamo_headless {

class StorySpeechClock {
public:
    StorySpeechClock();
    ~StorySpeechClock();
    StorySpeechClock(const StorySpeechClock&) = delete;
    StorySpeechClock& operator=(const StorySpeechClock&) = delete;

    // Loads the speech events and the sample source. False (with `err`) when the audio data cannot be read: the run keeps
    // the 60 s Timeouts then.
    bool init(const alamo::vfs::Vfs& files, uint32_t seed, std::string* err);
    // After every tick: the speech rewards `player`'s plot sets fired since the last call start (each one flushes the
    // speech queue first, as alamo_game does), the audio advances one tick, and the lines that ended go to the story
    // session as SpeechDone notifications (tactical while a tactical plot set is attached).
    void step(alamo::sim::StorySession& story, int32_t player);

    uint32_t speeches() const { return speeches_; }
    uint32_t ends() const { return ends_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t speeches_ = 0, ends_ = 0;
};

}  // namespace alamo_headless
