// alamo_game App: the mod picker (modc t08; ui IDD_PORT_MODS_DIALOG, game/mod_picker.hpp, docs/MODS.md §5.2).
// The rows are the base game and the rows of the mod catalog (ums1 t03: vfs::ModCatalog, one row per installed folder
// and modinfo variant; Steam workshop items found the way the STEAMMOD= resolver finds them, and <game>\Mods folders),
// each identified by its catalog key. Selecting a row runs the load report (game::check_mod_data, the same report alamo_mod_check
// prints, alamo.mod_report) on a worker thread - presentation only, its own Vfs and data, nothing the sim reads - and
// shows its summary; Launch restarts this exe with the row's resolved stack (ums1 t04: vfs::ModStack::launch_args, the
// list alamo_play and alamo_mod_check --resolve give too) and quits, since the data, text and GUI are loaded once per
// process.
#include "game_app.hpp"
#include "game_display.hpp"
#include "alamo/core/hash.hpp"  // normalize_name
#include "alamo/vfs/install_locations.hpp"
#include "alamo/vfs/mod_registry.hpp"

namespace alamo_game_app {
namespace {

std::vector<std::string> mod_args_of(const std::vector<std::string>& args) {
    std::vector<std::string> out;
    for (const std::string& a : args)
        if (game::is_mod_arg(a)) out.push_back(a);
    return out;
}

}  // namespace

const game::InstalledMod* App::find_mod_row(const std::string& key) const {
    for (const game::InstalledMod& m : installed_mods_)
        if (m.key == key) return &m;
    return nullptr;
}

void App::init_mod_picker(ui::MenuProviders& mp) {
    workshop_libraries_ = vfs::default_install_at(game_root_).steam_libraries;  // ums1 t01
    installed_mods_ = game::list_installed_mods(vfs::scan_installed_mods(game_root_, workshop_libraries_, false));
    std::printf("mods: %zu installed (%zu Steam libraries)\n", installed_mods_.size(), workshop_libraries_.size());
    // The active row: the first mod argument of this run (a stacked submod comes first), else the base game.
    const std::vector<std::string> active = mod_args_of(opt_.game_args);
    // A mod argument's value compared as a folder: '/' == '\\', any case (normalize_name).
    const std::string active_key = active.empty() ? std::string() : normalize_name(active.front());
    mp.mods = [this, active_key] {
        // Labels: the mod's name and where it is installed; the id (workshop item / folder) only when two mods share a
        // name (several Thrawn's Revenge versions) - the report's "Starts with:" line names it for every row.
        std::map<std::string, size_t> names;
        for (const game::InstalledMod& m : installed_mods_) ++names[m.name];
        bool active_marked = false;  // two variants share a folder (one argument): the first row is the active one
        std::vector<ui::ModPickerEntry> rows;
        ui::ModPickerEntry base;
        base.label = U"Base game (no mod)";
        base.active = active_key.empty();
        rows.push_back(std::move(base));
        for (const game::InstalledMod& m : installed_mods_) {
            ui::ModPickerEntry e;
            e.id = m.key;  // unique: the catalog key (1125571106, 2083545253:subA, Mods\<folder>)
            std::string label = m.name;
            if (m.name != m.id && names[m.name] > 1) label += "  [" + m.key + "]";
            label += m.workshop ? "  (Workshop)" : "  (Mods)";
            e.label = ui::utf8_to_utf32(label);
            e.active = !active_marked && !active_key.empty() && normalize_name(m.arg) == active_key;
            active_marked = active_marked || e.active;
            rows.push_back(std::move(e));
        }
        return rows;
    };
}

void App::check_mod(const std::string& id) {
    if (mod_launch_.count(id)) return;  // reported already (FrontEnd keeps the lines)
    if (mod_check_.valid()) {           // one check at a time: the newest request runs next
        if (id != mod_check_id_) {
            mod_check_next_ = id;
            mod_check_queued_ = true;
        }
        return;
    }
    mod_check_id_ = id;
    game::ModCheckOptions o;
    o.workshop_libraries = workshop_libraries_;
    o.shader_compile = false;  // the effect audit without the runtime DXC compile: lighter inside the running game
    const std::string root = game_root_;
    std::printf("mods: checking %s\n", id.empty() ? "the base game" : id.c_str());
    // mcx3 t06 / ums1 t04: resolve the row's stack first (declared and likely parents, submod first), then check that stack.
    mod_check_ = std::async(std::launch::async, [root, id, o] { return game::check_picked_mod(root, id, o); });
}

void App::poll_mod_check() {
    if (!mod_check_.valid() || mod_check_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const game::ModCheckReport r = mod_check_.get();
    const std::string id = mod_check_id_;
    std::vector<std::string> launch;
    if (!id.empty()) {
        const game::InstalledMod* row = find_mod_row(id);
        if (row) launch = game::mod_launch_args(*row, &r);
        if (launch.empty() && !r.base_eaw && !(r.resolved && r.stack.base_eaw)) launch.push_back(row ? row->arg : id);
    }
    mod_launch_[id] = launch;
    std::vector<std::u32string> lines;
    for (const std::string& l : game::mod_report_summary(r)) lines.push_back(ui::utf8_to_utf32(l));
    for (const std::string& l : game::mod_language_warnings(r, ui_.text.language)) lines.push_back(ui::utf8_to_utf32(l));
    if (!id.empty() && launch.empty()) {  // D1: a refused base-EaW mod has no launch
        mod_refused_.insert(id);
    } else if (!id.empty()) {
        mod_refused_.erase(id);
        std::string args;
        for (const std::string& a : launch) args += (args.empty() ? "" : " ") + a;
        lines.push_back(ui::utf8_to_utf32("Starts with: " + args));
    }
    std::printf("mods: %s: %zu errors, %zu warnings\n", id.empty() ? "base game" : id.c_str(), r.error_count(), r.warning_count());
    ui_.front.set_mod_report(id, std::move(lines));
    if (mod_check_queued_) {
        mod_check_queued_ = false;
        check_mod(mod_check_next_);
    }
}

void App::launch_mod(const std::string& id) {
    if (mod_refused_.count(id)) {  // D1: the report said why; nothing starts
        std::printf("mods: %s refused (base-EaW mod)\n", id.c_str());
        return;
    }
    const auto it = mod_launch_.find(id);
    const game::InstalledMod* row = find_mod_row(id);
    const std::vector<std::string> mod_args = it != mod_launch_.end() ? it->second
                                              : id.empty()             ? std::vector<std::string>{}
                                                                       : std::vector<std::string>{row ? row->arg : id};
    const std::vector<std::string> args = game::relaunch_args(opt_.argv, mod_args);
    std::string all;
    for (const std::string& a : args) all += " " + a;
    std::printf("mods: relaunch:%s\n", all.c_str());
    std::string err;
    if (!game::relaunch_self(game::windows_command_line(args), &err)) {
        notes_.push_back("mod launch failed: " + err);
        ui_.front.set_mod_report(id, {ui::utf8_to_utf32("Launch failed: " + err)});
        return;
    }
    screen_ = Screen::Quit;
}

}  // namespace alamo_game_app
