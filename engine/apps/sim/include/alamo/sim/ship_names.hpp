// ref5 t04: ship names (docs/re/MCX8_BINDINGS_OBJECTS.md §"Ship names"). GameConstants ShipNameTextFiles maps object
// types to name lists (UTF-16LE text files); every SELECTABLE object of the serviced mode gets one name once: the
// parent-mode object's when that one has a name, else the next name of its type's shuffled list (drawn without
// replacement; a list that ran out starts over with a Roman numeral). Start_Game resets the cursors. Lua
// GameObjectWrapper.Get_Name reads the stored text (quoted, e.g. "\"Allecto I\"") through lua::ObjectBridge::ship_name.
// [port] the shuffle is the port's own Fisher-Yates on a small generator seeded from the synced random state (read,
// never drawn), so naming does not change the synced stream.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "alamo/core/bytes.hpp"
#include "alamo/objects/object_store.hpp"

namespace alamo::objects {
class GameObjectTypes;
}

namespace alamo::sim {

class ShipNames {
public:
    // Reads a whole file by name as written (the session's file resolution); nullopt when it is missing.
    using ReadFile = std::function<std::optional<std::vector<uint8_t>>(const std::string& name)>;

    // Builds the lists from the ShipNameTextFiles text: tokens split at space, comma, tab and newline, read as (type,
    // file) pairs. Types resolve case-insensitively ("None" or an unknown name: no type). Pairs naming an equal file
    // (case-insensitive) share one list; a missing or empty file gives none. Problems are appended to `log`.
    void load(std::string_view spec, const objects::GameObjectTypes& types, const ReadFile& read,
              std::vector<std::string>* log = nullptr);
    // The names of a list file (UTF-16LE): the first code unit skipped (BOM), LF ends a line, CR and NUL dropped,
    // nothing else trimmed, empty lines skipped; UTF-8 out.
    static std::vector<std::string> parse_list(const std::vector<uint8_t>& bytes);
    // A Roman numeral (standard subtractive form) for 1..3999; "" otherwise.
    static std::string roman(uint32_t n);

    // Start_Game: cursors and wrap counts back to 0, the next pick shuffles again. new_session also forgets every
    // object's name (ids restart with a new game).
    void start_game(bool new_session);
    // Names every SELECTABLE object of `store` that has not been assigned yet, in store order. `parent`: the store of
    // the mode below (the parent-mode objects), or null. `seed`: the synced random state at this call (read only).
    void service(const objects::ObjectStore& store, const objects::ObjectStore* parent, uint32_t seed);
    // The next name of `type`'s list, quoted ("" without a list).
    std::string pick(objects::TypeIndex type, uint32_t seed);

    // The stored (quoted) name of an object; "" when it has none.
    std::string_view name(objects::ObjectId id) const;
    bool assigned(objects::ObjectId id) const { return names_.count(id) != 0; }
    size_t list_count() const { return lists_.size(); }

    // N1 save chunk "ship_names": the names, then the lists (names in their current order, cursor, wrap count) and the
    // shuffled flag. read() keeps the type map from load() and refuses a save with a different list layout.
    void write(ByteWriter& w) const;
    bool read(ByteReader& r);

private:
    struct List {
        std::string file;
        std::vector<std::string> names;
        uint32_t cursor = 0, wrap = 0;
    };
    std::vector<List> lists_;
    std::map<objects::TypeIndex, uint32_t> by_type_;  // type -> list index
    std::map<objects::ObjectId, std::string> names_;  // assigned objects (an empty name: assigned, none found)
    bool shuffled_ = false;
};

}  // namespace alamo::sim
