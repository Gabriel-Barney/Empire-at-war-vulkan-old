// ui4 t04: the FoC galactic planet map ("Base Layout", docs/re/GALACTIC_PLANET_MAP.md) - pure layout rules the host feeds
// into ui::GalacticPlanetMap: marker classes (§4.1), pad groups (§4.2), structures on the pads (§4.4), landed units on the
// unit pads (§4.5) and the ground-force stacks in orbit (§5). No sim or render state is read here.
#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace alamo::game {

// §4.1: the class of a UI marker (.ted top chunk 2) by its marker type. Tests in this order, the first match wins.
enum class MapMarkerClass : uint8_t { None, Out, In, Defender, Specific, Reinforcement, Bunker, BuildPad };
// `type_name` = the marker's object type name (any case); `has_specific` = the type has Marker_For_Specific_Object_Type
// entries.
MapMarkerClass classify_map_marker(std::string_view type_name, bool has_specific);

inline constexpr uint32_t kNoMapType = 0xFFFFFFFFu;

struct PlanetMapMarker {
    int32_t id = -1;                 // marker object id (.ted mini 0x0C)
    MapMarkerClass cls = MapMarkerClass::None;
    float x = 0.0f, y = 0.0f;        // 0..1 across the map (minis 0x0E / 0x0F)
    std::vector<uint32_t> specific;  // Marker_For_Specific_Object_Type (type indices), Specific markers
};

// §4.2 pad groups: build_pad00..32 = In, then Specific, then Out markers (tag 1 for In / Out, 3 for Specific); 33..39 =
// Reinforcement; 40..79 = Bunker, then Build pad; unit_pad00..09 = Defender. Markers keep file order within a class;
// markers past a group's size are dropped.
enum class MapPadGroup : uint8_t { Build, Unit };
struct PlanetMapPad {
    MapPadGroup group = MapPadGroup::Build;
    int32_t index = 0;               // NN of build_padNN / unit_padNN
    int32_t marker = -1;             // the marker's object id
    MapMarkerClass cls = MapMarkerClass::None;
    uint8_t tag = 0;                 // pad data slot 3: 1 In / Out, 3 Specific, 0 other
    float x = 0.0f, y = 0.0f;
    std::vector<uint32_t> specific;
    // Occupant (§4.4 / §4.5): an object (structure or landed unit, its object id) or a structure type (holds_type).
    uint32_t object = 0;
    uint32_t type = kNoMapType;      // the occupant's type (icon), kNoMapType = empty
    bool holds_type = false;         // §4.4 steps 1-2: a Specific pad shows a type, stays disabled, cannot be dragged
    bool dimmed = false;             // §4.4 step 2: tint (255, 255, 255, 192)
    float shield_radius = 0.0f;      // §4.4 step 2: Base_Shield_Radius of the type (world units), 0 = none
};
std::vector<PlanetMapPad> planet_map_pads(std::span<const PlanetMapMarker> markers);
inline constexpr int32_t kMapBuildPads = 80, kMapUnitPads = 10, kMapOrbitStacks = 20;

// §4.4: one ground special structure of the planet (PD +0x150), in list order.
struct PlanetMapStructure {
    uint32_t object = 0;
    uint32_t type = kNoMapType;
    uint8_t base = 0;                        // Base_Position: 0 none, 1 IN_BASE, 2 OUT_BASE
    int32_t marker = -1;                     // remembered marker (object +0x3b0), -1 none
    uint32_t place_other = kNoMapType;       // Place_Other_Type_At_Every_Specific_Marker_Position
    std::vector<uint32_t> additional;        // Tactical_Additional_Structure_Type
};
// Shield radius per type (Base_Shield_Radius, 0 none).
using MapShieldRadius = std::function<float(uint32_t type)>;
// A SetMarkerID the map sends (COMMANDS kind 53): object +0x3b0 := marker. [port] only when it changes the object's
// remembered marker (retail resends the same ids on every rebuild; the sim state is the same).
struct MapMarkerSend {
    uint32_t object = 0;
    int32_t marker = -1;
};
// Fills the pads' occupants (§4.4): step 1 every Specific pad whose marker lists a structure's place_other type gets the
// structure's type; step 2 each other structure type and each additional type (each once) takes the first free Specific
// pad listing it (dimmed, with its shield radius); step 3 the structures not placed in step 2 go by Base_Position - a
// remembered marker first, then the first free pad of their group (In: tag-1 pads from In markers, Out: from Out
// markers), falling over to the other group when it is full; each step-3 structure is sent its pad's marker. Structures
// without a pad are not shown.
void place_map_structures(std::vector<PlanetMapPad>& pads, std::span<const PlanetMapStructure> structures,
                          const MapShieldRadius& shield = {}, std::vector<MapMarkerSend>* sends = nullptr);
// §4.5: landed units (the planet owner's, surface list order; `types` and `markers` parallel, `markers` = the remembered
// marker, -1 none, may be empty). A unit with a marker takes the free unit pad with that marker id, else it is sent -1 and
// waits; then the units without a marker, then the waiting ones, take the free unit pads in pad order and are sent the
// pad's marker. Units past the pads are not shown.
void place_map_units(std::vector<PlanetMapPad>& pads, std::span<const uint32_t> units, std::span<const uint32_t> types,
                     std::span<const int32_t> markers = {}, std::vector<MapMarkerSend>* sends = nullptr);

// §5: one ground-force stack in orbit (land_fleet_NN): the local player's units with the land behaviour in the planet's
// orbiting fleets, stacked by display type. Input order = fleet order, then contents order; the representative is the
// first unit of the type. [port] stacks are ordered by type index ascending (retail: by the type object's address).
struct MapOrbitUnit {
    uint32_t unit = 0, fleet = 0, type = kNoMapType;
};
struct MapOrbitStack {
    uint32_t type = kNoMapType;
    uint32_t unit = 0, fleet = 0;  // representative
    uint32_t count = 0;
};
std::vector<MapOrbitStack> map_orbit_stacks(std::span<const MapOrbitUnit> units);

}  // namespace alamo::game
