// Asset audit for the mod load report (mcx2 t01, docs/MOD_COMPAT_DESIGN.md 12.1 `assets`): every art, audio, movie and
// map file a mod layer serves (its loose Data tree, or a MEG mounted from the mod's folder) is decoded headless with the
// runtime decoders, by family, and sorted into decoded / salvaged (the rule that applied, D5) / rejected (the reason).
//
// Families and their decoders (the ones the game uses on load):
//   model     Data\Art\Models\*.ALO that is not a particle system   alamo::alo::parse (salvage mode)
//   particle  Data\Art\Models\*.ALO starting with chunk 0x900       alamo::alo::parse (salvage mode)
//   animation Data\Art\Models\*.ALA                                 alamo::ala::parse (salvage mode)
//   texture   Data\Art\Textures\*.DDS / .TGA / .BMP                 assets::decode_texture
//   map       Data\Art\Maps\*.TED                                   terrain::parse_ted (when linked)
//   sound     Data\Audio\**\*.WAV / .MP3                            audio::probe_audio + decode_audio (when linked)
//   movie     Data\Art\Movies\**\*.BIK                              video::probe_bink_header (when linked)
// A rejection retail shares (a docs/re citation says retail does not show it either) is marked retail_equivalent and
// does not lower the grade (12.2). Files the base game ships are not audited (retail's own data; the tests cover them).
//
// Cache: with AssetAuditOptions::cache_dir, results are kept by (family, content hash, strict) in
// <cache_dir>\asset_audit_v1.txt, so rechecking an unchanged mod reads each file but decodes nothing.
// Deterministic: files are visited in sorted path order; the result does not depend on the cache.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace alamo::vfs {
class Vfs;
}

namespace alamo::game {

struct AssetAuditOptions {
    bool strict = false;    // --strict: every salvage is a rejection (for mod authors)
    std::string cache_dir;  // "" = no cache
};

struct AssetEntry {
    std::string path;    // game path, upper case ("DATA\ART\MODELS\X.ALO")
    std::string family;  // model, particle, animation, texture, map, sound, movie
    std::string rule;    // salvaged: the salvage rule id(s), ';'-joined; rejected: the reason
    bool retail_equivalent = false;  // rejected, and retail does not show it either (cite in `rule`)
};

struct AssetFamilyCount {
    size_t checked = 0, decoded = 0, salvaged = 0, rejected = 0;
};

struct AssetAudit {
    bool ran = false;
    size_t checked = 0, decoded = 0, salvaged_count = 0, rejected_count = 0, retail_equivalent = 0, cache_hits = 0;
    std::vector<AssetEntry> salvaged;  // path order
    std::vector<AssetEntry> rejected;  // path order
    std::map<std::string, AssetFamilyCount> by_family;
};

// Audits the files `v` serves from its mod layers (Vfs::mod_dirs()).
AssetAudit audit_mod_assets(const vfs::Vfs& v, const AssetAuditOptions& opt = {});
// Decodes one file as `family` (exposed for tests and the corpus runner). state: 0 decoded, 1 salvaged, 2 rejected.
int audit_asset_bytes(const std::string& family, const std::vector<uint8_t>& bytes, bool strict, std::string* rule,
                      bool* retail_equivalent);
// The `assets` value of the load report (MODS.md 5.1, version 4): {checked, decoded, salvaged:[{path, family, rule}],
// rejected:[{path, family, reason, retail_equivalent}], by_family:{...}}.
std::string asset_audit_json(const AssetAudit& a, int indent = 2);

}  // namespace alamo::game
