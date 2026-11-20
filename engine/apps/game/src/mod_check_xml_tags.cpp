// Mod check, XML tags family (mod_check_detail.hpp): element tags the original does not read, per database (modc t15,
// t25, t37). The database table (xml_databases) is shared with the honour-gap check.
#include "mod_check_detail.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/core/ascii.hpp"
#include "alamo/data/xml.hpp"
#include "alamo/data/xml_list.hpp"
#include "alamo/objects/coverage.hpp"
#include "alamo/objects/field_schema.hpp"

namespace alamo::game::mod_check_detail {

namespace {

constexpr std::string_view kFaction[] = {"Faction"};
constexpr std::string_view kCampaign[] = {"Campaign"};
constexpr std::string_view kTradeRoute[] = {"TradeRoute"};
constexpr std::string_view kHardPoint[] = {"HardPoint"};
constexpr std::string_view kHardPointPort[] = {"Type"};  // render/hardpoints.cpp (hard point type)
constexpr std::string_view kGameConstants[] = {"GameConstants"};
constexpr std::string_view kGameConstantsPort[] = {"Ships_Per_Stack"};  // apps/game app_screens.cpp (fleet stacks)
constexpr std::string_view kSfxEvent[] = {"SFXEvent"};
constexpr std::string_view kMusicEvent[] = {"MusicEvent"};
// SpeechEvent has no table in the doc: what audio_db.cpp speech_fields() reads.
constexpr std::string_view kSpeechEventPort[] = {"Files", "Text_ID", "Volume_Percent", "Overlap_Test"};
constexpr std::string_view kAudio[] = {"Audio"};
constexpr std::string_view kAudioPort[] = {"Ambient_Map_Sounds"};  // audio/ambience.hpp (map ambient beds)
constexpr std::string_view kTacticalCamera[] = {"TacticalCamera"};
constexpr std::string_view kTacticalCameraPort[] = {"MinHeightAboveTerrain"};  // ui/camera.cpp
constexpr std::string_view kMovie[] = {"Movie"};
constexpr std::string_view kDifficulty[] = {"Difficulty_Adjustment"};
constexpr std::string_view kLightning[] = {"LightningEffect"};
constexpr std::string_view kPrioritySet[] = {"Priority_Set"};
constexpr std::string_view kMousePointer[] = {"MousePointer"};
constexpr std::string_view kCommandBar[] = {"CommandBarComponent"};
constexpr std::string_view kRadarMap[] = {"RadarMapSettings", "Radar_Map_Event", "Passability_Color_Settings"};
// The walker's own names (xml_field_tables.md "Elements read outside the descriptor tables"): attributes select a block.
constexpr std::string_view kRadarMapSpecial[] = {"Passability_Color_Settings", "Color", "Radar_Map_Event"};

constexpr XmlDatabase kXmlDatabases[] = {
    {"factions", "FactionFiles.xml", "", "Faction", kFaction, {}},
    {"campaigns", "CampaignFiles.xml", "", "Campaign", kCampaign, {}},
    {"trade routes", "TradeRouteFiles.xml", "", "TradeRoute", kTradeRoute, {}},
    {"hard points", "HardPointDataFiles.xml", "", "HardPoint", kHardPoint, kHardPointPort},
    {"game constants", "", "GameConstants.xml", "", kGameConstants, kGameConstantsPort},
    {"sfx events", "SFXEventFiles.xml", "", "SFXEvent", kSfxEvent, {}},
    {"music events", "", "MusicEvents.xml", "MusicEvent", kMusicEvent, {}},
    {"speech events", "", "SpeechEvents.xml", "SpeechEvent", {}, kSpeechEventPort},
    {"audio", "", "Audio.xml", "", kAudio, kAudioPort},
    {"tactical cameras", "", "TacticalCameras.xml", "TacticalCamera", kTacticalCamera, kTacticalCameraPort},
    {"movies", "", "Movies.xml", "Movie", kMovie, {}},
    {"difficulty adjustments", "", "DifficultyAdjustments.xml", "Difficulty_Adjustment", kDifficulty, {}},
    {"lightning effects", "", "LightningEffectTypes.xml", "LightningEffect", kLightning, {}},
    {"targeting priority sets", "TargetingPrioritySetFiles.xml", "", "Priority_Set", kPrioritySet, {}},
    {"mouse pointers", "", "MousePointers.xml", "MousePointer", kMousePointer, {}},  // as app game_main.cpp reads it
    {"command bar components", "CommandBarComponentFiles.xml", "", "*", kCommandBar, {}},
    {"radar map", "", "RadarMap.xml", "", kRadarMap, kRadarMapSpecial, true},
};

}  // namespace

std::span<const XmlDatabase> xml_databases() { return kXmlDatabases; }

// Every descendant of `n` with non-empty text, with its parent (walk databases).
static void walk_text_elements(data::XmlNode n, std::vector<std::pair<data::XmlNode, data::XmlNode>>& out) {
    for (data::XmlNode c : n.children()) {
        if (!c.text_trimmed().empty()) out.emplace_back(n, c);
        walk_text_elements(c, out);
    }
}

std::vector<std::pair<data::XmlNode, data::XmlNode>> checked_elements(const data::XmlDocument& doc, const XmlDatabase& db) {
    std::vector<std::pair<data::XmlNode, data::XmlNode>> checked;  // (entry, child)
    if (db.walk) {
        walk_text_elements(doc.root(), checked);
        return checked;
    }
    std::vector<data::XmlNode> entries;
    if (db.element.empty()) entries.push_back(doc.root());
    else
        for (data::XmlNode e : doc.root().children())
            if (db.element == "*" || iequals(e.name(), db.element)) entries.push_back(e);
    for (const data::XmlNode e : entries)
        for (data::XmlNode c : e.children()) checked.emplace_back(e, c);
    return checked;
}

// The Data\XML files of a database in load order, each once.
std::vector<std::string> database_files(const GameData& d, const XmlDatabase& db) {
    std::vector<std::string> out;
    if (db.list.empty()) {
        out.push_back("Data\\XML\\" + std::string(db.file));
        return out;
    }
    std::vector<uint8_t> bytes;
    if (!d.files->read("Data\\XML\\" + std::string(db.list), bytes)) return out;
    const data::XmlDocument doc = data::XmlDocument::parse(bytes, std::string(db.list));
    std::set<std::string> seen;
    for (data::ListedXmlEntry& en : data::listed_xml_entries(doc))  // cu5 t02 (none when !doc.ok())
        if (seen.insert(normalize_name(en.path)).second) out.push_back(std::move(en.path));
    return out;
}

namespace {

void check_database_tags(const GameData& d, const XmlDatabase& db, ModCheckReport& r) {
    std::set<std::string, std::less<>> known;
    for (const objects::FieldSchema& row : objects::field_schema())
        for (const std::string_view o : db.owners)
            if (iequals(row.owner, o)) known.insert(ascii_lower(row.tag));
    for (const std::string_view t : db.port_tags) known.insert(ascii_lower(t));
    struct Seen {
        UnknownXmlTag u;
        std::set<std::string> entries;
    };
    std::map<std::string, Seen> unknown;  // by lower-case tag: sorted, deterministic
    std::vector<uint8_t> bytes;
    for (const std::string& path : database_files(d, db)) {
        if (!d.files->read(path, bytes)) continue;  // a missing list entry is reported by the files check
        const data::XmlDocument doc = data::XmlDocument::parse(bytes, path);
        if (!doc.ok()) continue;  // its loader reports the parse error
        const bool in_mod = d.vfs && in_mod_layer(*d.vfs, path);
        {
            for (const auto& [e, c] : checked_elements(doc, db)) {
                const std::string entry = path + "#" + std::to_string(e.index());
                std::string key = ascii_lower(c.name());
                if (known.count(key)) continue;
                Seen& s = unknown[std::move(key)];
                if (s.u.occurrences++ == 0) {
                    s.u.database = std::string(db.name);
                    s.u.tag = std::string(c.name());
                    s.u.first_file = path;
                }
                s.u.in_mod = s.u.in_mod || in_mod;
                s.entries.insert(entry);
            }
        }
    }
    for (auto& [key, s] : unknown) {
        s.u.types = static_cast<uint32_t>(s.entries.size());
        r.unknown_xml_tags.push_back(std::move(s.u));
    }
}

// modc t37: tags with a field_schema() row that is not one of the hand-verified tables of another class, lower case.
// objects::find_field_schema falls back to any owner's row (the loader keeps that), so the report checks the owner here:
// a tag whose only rows are in the CommandBarComponent table (0xa38dd0: Hidden, Toggle, ...) or the RadarMapClass table
// (0xa39c50) is not read on an object type; each owner's Parse_Database_Entry reads only its own table
// (xml_field_tables.md, modc t25 sections, [H]). The other segment labels of the doc are its generator's guesses (see
// gen_field_schema.py OWNER_FIXES: "MousePointer" runs hold Land_Upgrade_Type, ...), so they do not make a tag unread.
constexpr std::string_view kVerifiedForeignOwners[] = {"CommandBarComponent", "RadarMapSettings", "Radar_Map_Event",
                                                       "Passability_Color_Settings"};

std::set<std::string, std::less<>> object_type_tags() {
    std::set<std::string, std::less<>> out;
    for (const objects::FieldSchema& row : objects::field_schema()) {
        bool foreign = false;
        for (const std::string_view o : kVerifiedForeignOwners) foreign = foreign || iequals(row.owner, o);
        if (!foreign) out.insert(ascii_lower(row.tag));
    }
    return out;
}

}  // namespace

void check_xml_tags(const GameData& d, ModCheckReport& r) {
    const objects::CoverageReport cov = objects::build_coverage(d.raw, 0);
    const auto& files = d.raw.files();
    const std::set<std::string, std::less<>> object_tags = object_type_tags();
    for (const objects::TagCoverage& t : cov.tags) {
        if (t.occurrences == 0) continue;
        if (!t.unknown() && (t.sub_object || object_tags.count(ascii_lower(t.name)) != 0)) continue;
        UnknownXmlTag u;
        u.database = "object types";
        u.tag = std::string(t.name);
        u.occurrences = t.occurrences;
        u.types = t.types;
        if (!t.files.empty() && t.files.front() < files.size()) u.first_file = files[t.files.front()].path;
        for (const uint32_t fi : t.files)
            if (fi < files.size() && d.vfs && in_mod_layer(*d.vfs, files[fi].path)) {
                u.in_mod = true;
                break;
            }
        r.unknown_xml_tags.push_back(std::move(u));
    }
    if (d.files)
        for (const XmlDatabase& db : kXmlDatabases) check_database_tags(d, db, r);
    std::stable_sort(r.unknown_xml_tags.begin(), r.unknown_xml_tags.end(), [](const UnknownXmlTag& a, const UnknownXmlTag& b) {
        if (a.occurrences != b.occurrences) return a.occurrences > b.occurrences;
        if (a.database != b.database) return a.database < b.database;
        return a.tag < b.tag;
    });
}

}  // namespace alamo::game::mod_check_detail
