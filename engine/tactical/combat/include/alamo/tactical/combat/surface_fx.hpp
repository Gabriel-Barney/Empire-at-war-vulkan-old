// trn1 t02 - SurfaceFX terrain modifiers (docs/re/TRN1_TERRAIN_GAMEPLAY.md §2): the sim part of SURFACEFX.XML. A unit
// type with the SURFACE_FX land behaviour gets one instance per SurfaceFX_Name entry; each instance follows the surface
// type under the unit and, when it changes, takes the old surface's ModifierSet entries off and puts the new one's on:
//   good ground (12)          Defense_Mod -> list 0, Damage_Mod -> list 1
//   high ground (13)          FOW_Reveal_Range_Mod -> list 7, Fire_Range_Mod -> list 8
//   slow ground (14), water (4)  Speed_Mod -> list 5 (mud's Speed_Mod is parsed but never applied [H])
//   damage ground (15), lava (8) Terrain_Damage every Terrain_Damage_Delay seconds (Take_Damage kind 0x13)
// Entries: category 0, permanent, source = the unit + name_crc("Persistent_SurfaceFX_Unique_ID_" + SurfaceFX name).
// Status icons, ground colour tints and particles are presentation (render's SurfaceFxLibrary), not modelled here.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace alamo::data {
class XmlNode;
}

namespace alamo::tactical::combat {

// Surface type ids (Surface_Settings names, parser 0x356f60; TEM mini 14). 9-11 have no name (the cover cells of
// Get_Damage_Multiplier), 17 is a presentation-only id, 18 = space.
inline constexpr uint32_t kSurfaceTypeCount = 19;
inline constexpr uint32_t kSurfaceWater = 4, kSurfaceLava = 8, kSurfaceGoodGround = 12, kSurfaceHighGround = 13,
                          kSurfaceSlowGround = 14, kSurfaceDamageGround = 15;
inline constexpr uint32_t kSurfaceCoverFirst = 9, kSurfaceCoverLast = 11;
// -1 for an unknown name (ASCII case-insensitive).
int32_t surface_type_from_name(std::string_view name);

struct SurfaceSettings {
    float defense = 0.0f;               // Defense_Mod
    float damage = 0.0f;                // Damage_Mod
    float reveal_range = 0.0f;          // FOW_Reveal_Range_Mod
    float fire_range = 0.0f;            // Fire_Range_Mod
    float speed = 0.0f;                 // Speed_Mod
    float terrain_damage = 0.0f;        // Terrain_Damage
    float terrain_damage_delay = 0.0f;  // Terrain_Damage_Delay (seconds)
};

struct SurfaceFxDef {
    std::string name;
    uint32_t key_crc = 0;  // name_crc("Persistent_SurfaceFX_Unique_ID_" + upper-cased name): the entries' source key
    std::array<SurfaceSettings, kSurfaceTypeCount> by_surface{};
    // Any sim term on a surface the update reads (4, 8, 12-15): the defs without one need no instance.
    bool affects_sim() const;
};

// Every <SurfaceFX name="..."> of the SurfaceEffects root, document order; a later definition of a name replaces it.
std::vector<SurfaceFxDef> load_surface_fx(const data::XmlNode& root);
// Index of `name` (ASCII case-insensitive, trimmed) in `defs`, -1 when absent.
int32_t find_surface_fx(const std::vector<SurfaceFxDef>& defs, std::string_view name);

}  // namespace alamo::tactical::combat
