// mcx1 t05 (D4): test-data authoring helper for synthetic GameObjectType XML.
//
// A type's galactic role comes only from the DUMMY_* ids of its generic <Behavior> list (docs/re/MCX1_RE_ANSWERS.md Q7.2),
// never from its element tag. Real FoC types carry those behaviours; the port's older synthetic fixtures were written
// with the vanilla element tags alone. with_galactic_dummies() writes the behaviour such a fixture type would carry in
// the shipped data: right after each opening tag of a listed element it inserts one extra <Behavior> element (repeated
// list tags append, OBJECT_TYPES.md §4.4), so the fixture's own <Behavior> text is untouched. Variants are skipped: a
// variant's own <Behavior> would replace its parent's list (§3.3), and it inherits the parent's DUMMY_* id anyway.
//   StarBase -> DUMMY_STAR_BASE; GroundStructure / GroundBase / SpecialStructure -> DUMMY_GROUND_STRUCTURE;
//   Squadron -> DUMMY_SPACE_FIGHTER_SQUADRON; SpaceUnit -> DUMMY_STARSHIP;
//   GroundCompany / HeroCompany / GroundInfantry / GroundVehicle -> DUMMY_GROUND_COMPANY.
// An element whose own <Behavior> list already names one of these ids is left alone.
#pragma once
#include <string>
#include <string_view>

namespace alamo::test {

inline std::string with_galactic_dummies(std::string xml) {
    struct Row {
        std::string_view tag, dummy;
    };
    static constexpr Row kRows[] = {
        {"StarBase", "DUMMY_STAR_BASE"},
        {"GroundStructure", "DUMMY_GROUND_STRUCTURE"},
        {"GroundBase", "DUMMY_GROUND_STRUCTURE"},
        {"SpecialStructure", "DUMMY_GROUND_STRUCTURE"},
        {"Squadron", "DUMMY_SPACE_FIGHTER_SQUADRON"},
        {"SpaceUnit", "DUMMY_STARSHIP"},
        {"GroundCompany", "DUMMY_GROUND_COMPANY"},
        {"HeroCompany", "DUMMY_GROUND_COMPANY"},
        {"GroundInfantry", "DUMMY_GROUND_COMPANY"},
        {"GroundVehicle", "DUMMY_GROUND_COMPANY"},
    };
    for (const Row& r : kRows) {
        const std::string open = "<" + std::string(r.tag) + " ";
        const std::string close = "</" + std::string(r.tag) + ">";
        size_t at = 0;
        while ((at = xml.find(open, at)) != std::string::npos) {
            const size_t gt = xml.find('>', at);
            if (gt == std::string::npos) break;
            if (xml[gt - 1] == '/') {  // <SpaceUnit Name="x"/>: no body to put the behaviour in
                at = gt;
                continue;
            }
            const size_t end = xml.find(close, gt);
            const std::string_view body =
                end == std::string::npos ? std::string_view(xml).substr(gt) : std::string_view(xml).substr(gt, end - gt);
            // Does a generic <Behavior> list of this element already name one of the galactic DUMMY_* ids? (An id under
            // Space / Land / GalacticBehavior does not count for the role.)
            bool has_role = false;
            for (size_t b = body.find("<Behavior>"); b != std::string_view::npos && !has_role; b = body.find("<Behavior>", b + 1)) {
                const size_t e = body.find("</Behavior>", b);
                const std::string_view list = body.substr(b, e == std::string_view::npos ? std::string_view::npos : e - b);
                for (const Row& k : kRows) has_role = has_role || list.find(k.dummy) != std::string_view::npos;
            }
            if (body.find("Variant_Of_Existing_Type") != std::string_view::npos || has_role) {
                at = gt;
                continue;
            }
            const std::string ins = "<Behavior>" + std::string(r.dummy) + "</Behavior>";
            xml.insert(gt + 1, ins);
            at = gt + 1 + ins.size();
        }
    }
    return xml;
}

}  // namespace alamo::test
