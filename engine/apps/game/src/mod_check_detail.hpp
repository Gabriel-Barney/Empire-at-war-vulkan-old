// Private to the mod check (ref3 t04): the check families behind check_mod_data (mod_check.hpp), one file each, and the
// helpers more than one family uses. check_mod_data (mod_check.cpp) calls them in its order; each fills its part of the
// ModCheckReport and reads only what loaded (GameData). Nothing here is part of alamo_game_core's interface.
//
//   family         file                         fills (ModCheckReport)
//   files          mod_check_files.cpp          mod_folders, missing_files, submod, base_eaw_suspect
//   content        mod_check_content.cpp        counts, skirmish_maps, limits, campaigns_*
//   XML tags       mod_check_xml_tags.cpp       unknown_xml_tags
//   shaders        mod_check_shaders.cpp        shaders (and prebuild_mod_effects, public)
//   Lua            mod_check_lua.cpp            lua_*, unbound_lua_calls, binding_gaps / binding_calls, retail_equivalent
//   honour gaps    mod_check_honour_gaps.cpp    honour_gaps
//   story          mod_check_story.cpp          story, retail_equivalent
//   token coverage mod_check_tokens.cpp         token_coverage, retail_equivalent
//   grade          mod_check_grade.cpp          retail_equivalent, issues, grade (and loader_diagnostic_file, public)
// The reports: mod_report_text.cpp (format_mod_report), mod_report_json.cpp (every JSON document, one writer).
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "alamo/core/hash.hpp"  // normalize_name, iequals
#include "alamo/data/xml.hpp"
#include "alamo/game/mod_check.hpp"

namespace alamo::vfs {
class Vfs;
}

namespace alamo::game::mod_check_detail {

// ---- shared helpers ----

// True when `path` resolves to a file of a mod folder (the VFS mod layer). mod_check_files.cpp.
bool in_mod_layer(const vfs::Vfs& v, std::string_view path);

// "MeshGloss.fx" / "Data\Art\Shaders\MESHGLOSS.FXO" -> "MESHGLOSS" (the form ShaderIssue::effect has; also a script's
// upper-case base name). mod_check_shaders.cpp.
std::string effect_base(std::string_view name);

// modc t15: the XML databases besides the object types that the port loads. Per database: where its entries are (the
// <File> entries of a Data\XML list, as the loader reads them, or one Data\XML file), the entry element below the root
// (or the root itself), and which child tags count as read: the rows of the original's descriptor table for the class
// (docs/re/xml_field_tables.md through objects::field_schema(), by segment owner) plus the tags the port's loader reads
// that no row lists (cited per database). Only direct children of an entry are checked, except for a walked file.
// modc t25 (docs/re/xml_field_tables.md "modc t25" sections): CommandBarComponents.xml, whose entries are every child of
// the root (the element name is the component name); RadarMap.xml, which the original walks as a whole (no element path
// checked, a known tag read at any depth, an element without text only a container).
struct XmlDatabase {
    std::string_view name;
    std::string_view list;     // Data\XML\<list> holds the entry files; "" = `file`
    std::string_view file;     // Data\XML\<file> holds the entries
    std::string_view element;  // entry element below the root; "" = the root element is the one entry; "*" = every child
    std::span<const std::string_view> owners;     // field_schema() segment owners
    std::span<const std::string_view> port_tags;  // read by the port's loader, no table row
    bool walk = false;  // every element with text at any depth below the root is checked; entries = its parents
};

// The databases, in report order (mod_check_xml_tags.cpp; the honour-gap check reads the same files).
std::span<const XmlDatabase> xml_databases();

// The Data\XML files of a database in load order, each once.
std::vector<std::string> database_files(const GameData& d, const XmlDatabase& db);

// The (entry, child) element pairs of `doc` the database's checks look at: for a walk database every element with text
// at any depth with its parent, else every direct child of each entry element.
std::vector<std::pair<data::XmlNode, data::XmlNode>> checked_elements(const data::XmlDocument& doc, const XmlDatabase& db);

// ---- the families, in check_mod_data's order ----

void count_content(const GameData& d, ModCheckReport& r);                      // content
void check_campaigns(const GameData& d, ModCheckReport& r);                    // content
void list_mod_files(const vfs::Vfs& v, ModCheckReport& r);                     // files
void check_files(const GameData& d, ModCheckReport& r);                        // files
void check_xml_tags(const GameData& d, ModCheckReport& r);                     // XML tags
void check_lua(const GameData& d, ModCheckReport& r, bool run_chunks, int script_budget);                   // Lua
void check_shaders(const GameData& d, ModCheckReport& r, bool compile, const std::string& fx_cache_dir);  // shaders
void check_submod(const GameData& d, ModCheckReport& r, const ModCheckOptions& o);                         // files
void check_base_eaw_suspect(const GameData& d, ModCheckReport& r);             // files
// Report version 4 (mcx0 t06, docs/MOD_COMPAT_DESIGN.md §12.1), after the checks they read:
void check_honour_gaps(const GameData& d, ModCheckReport& r);                  // honour gaps
void check_story(const GameData& d, ModCheckReport& r);                        // story
void check_token_coverage(const GameData& d, ModCheckReport& r);               // token coverage
void retail_equivalent_from_checks(ModCheckReport& r);                         // grade
void compat_issues_and_grade(ModCheckReport& r, const vfs::Vfs* v, const alamo::IssueSink& vfs_issues);  // grade
void loader_issues(const GameData& d, ModCheckReport& r);                      // grade (ums4 t07)

}  // namespace alamo::game::mod_check_detail
