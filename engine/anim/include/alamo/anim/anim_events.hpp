// R3 animation events (docs/re/AUDIO.md §5 / §6.2: Data\XML\AnimationSFXMaps.txt, AnimSFXMapper 0x4770a0).
// Each line: `SOUND <clip> <time> <SFX event>` or `SURFACE <clip> <time> <SurfaceFX> <bone>` (tab / space
// separated; `;`, `#`, `//` comment lines). <clip> is the ALA file stem (e.g. ei_trooper_move_00). SURFACE rows are
// the foot plants (INFANTRY_FOOT_PLANT on B_Foot_L / B_Foot_R), body impacts of death clips, etc.
//
// Firing rule (AUDIO.md §9.4, 0x4770a0 / 0x4775d0): the time column is the file integer / 100 in animation frames
// [H for / 100, M for the frame unit]; an event fires when the primary slot's clip frame passes it during one sim
// advance, half-open (before, after]; a loop wrap fires every row with t > before or t <= after, so a row at 0 fires
// only on a wrap. [port] Set_Animation's "fire skipped events" start (rows in (0, start] at once) is not modelled.
// Events never feed back into the sim (visual / audio output only), so they are not part of any CRC.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace alamo::anim {

struct AnimEventDef {
    enum class Kind : uint8_t { Sound = 0, Surface = 1 };
    Kind kind = Kind::Surface;
    uint32_t time = 0;  // the file value: / 100 = animation frames (§9.4)
    std::string name;  // SFX event / SurfaceFX name
    std::string bone;  // SURFACE: bone name ("" for SOUND)
};

class AnimEventMap {
public:
    // Parses AnimationSFXMaps.txt text; appends (file, line) diagnostics for malformed lines. Returns rows added.
    size_t parse(std::string_view text, std::vector<std::string>* diagnostics = nullptr);
    // Events of a clip, by ALA stem or full path (case-insensitive, directory and .ALA stripped); time order.
    const std::vector<AnimEventDef>* find(std::string_view clip) const;
    size_t size() const { return rows_; }
    size_t clips() const { return by_clip_.size(); }

private:
    std::map<std::string, std::vector<AnimEventDef>> by_clip_;  // upper-case stem (lookup only)
    size_t rows_ = 0;
};

// Upper-case ALA stem of a clip path ("DATA\ART\MODELS\EI_TROOPER_MOVE_00.ALA" -> "EI_TROOPER_MOVE_00").
std::string clip_stem(std::string_view path);

// Indices into `events` crossed by an advance of a clip of `count` frames at `fps` from frame `before` to frame
// `after` (rule above). `wrapped` = the loop wrapped during the advance.
void crossed_events(const std::vector<AnimEventDef>& events, float fps, uint32_t count, float before, float after, bool wrapped,
                    std::vector<uint32_t>& out);

}  // namespace alamo::anim
