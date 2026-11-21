// ums2 t01: vfs::ModEnvironment on a synthetic install - the mount plan from the command line, one descriptor per
// mounted mod, layer owners, owner_of, the VFS findings in the caller's IssueSink and the base-EaW refusal (D1).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "alamo/core/compat_issue.hpp"
#include "alamo/core/host_fs.hpp"
#include "alamo/meg/meg.h"
#include "alamo/test/support.hpp"
#include "alamo/vfs/mod_environment.hpp"

namespace fs = std::filesystem;
using ::alamo::test::g_fail;
using alamo::vfs::ModEnvironment;

namespace {

void put_u16(std::string& s, uint32_t v) {
    s.push_back(static_cast<char>(v & 0xFFu));
    s.push_back(static_cast<char>((v >> 8) & 0xFFu));
}
void put_u32(std::string& s, uint32_t v) {
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>((v >> (8 * i)) & 0xFFu));
}

// MEG V1 with the records sorted by CRC (sorted = false: descending, a malformed table).
void write_meg(const fs::path& p, const std::vector<std::pair<std::string, std::string>>& files, bool sorted = true) {
    const uint32_t n = static_cast<uint32_t>(files.size());
    std::string head;
    put_u32(head, n);
    put_u32(head, n);
    for (const auto& f : files) {
        put_u16(head, static_cast<uint32_t>(f.first.size()));
        head += f.first;
    }
    struct Rec { uint32_t crc, name_index; };
    std::vector<Rec> recs;
    for (uint32_t i = 0; i < n; ++i) recs.push_back({alamo::meg::name_crc(files[i].first), i});
    std::sort(recs.begin(), recs.end(), [sorted](const Rec& a, const Rec& b) { return sorted ? a.crc < b.crc : a.crc > b.crc; });
    uint32_t offset = static_cast<uint32_t>(head.size()) + 20u * n;
    std::string table, data;
    for (uint32_t i = 0; i < n; ++i) {
        const std::string& payload = files[recs[i].name_index].second;
        put_u32(table, recs[i].crc);
        put_u32(table, i);
        put_u32(table, static_cast<uint32_t>(payload.size()));
        put_u32(table, offset);
        put_u32(table, recs[i].name_index);
        offset += static_cast<uint32_t>(payload.size());
        data += payload;
    }
    alamo::test::write_file(p, head + table + data);
}

std::string read_str(const alamo::FileProvider& f, std::string_view path) {
    std::vector<uint8_t> b;
    return f.read(path, b) ? std::string(b.begin(), b.end()) : std::string("<missing>");
}

}  // namespace

int main() {
    const fs::path T = fs::temp_directory_path() / "alamo_vfs_env_test";
    std::error_code ec;
    fs::remove_all(T, ec);
    const fs::path FOC = T / "corruption";
    const fs::path SUB = FOC / "Mods" / "Sub";
    const fs::path PARENT = FOC / "Mods" / "Parent";
    const fs::path EAWMOD = FOC / "Mods" / "OldEaw";
    const auto u8 = [](const fs::path& p) { return alamo::path_to_utf8(p); };
    using alamo::test::write_file;

    write_file(FOC / "Data" / "XML" / "Base.xml", "BASE");
    write_file(FOC / "Data" / "XML" / "Shared.xml", "BASE");
    write_file(FOC / "Data" / "MegaFiles.xml", "<Mega_Files><File>Data\\Config.meg</File></Mega_Files>");
    write_meg(FOC / "Data" / "Config.meg", {{"DATA\\XML\\CONFIG.XML", "BASECONFIG"}});
    write_file(SUB / "Data" / "XML" / "Shared.xml", "SUB");
    write_file(SUB / "modinfo.json", R"({"name": "Sub Mod", "version": "1.2"})");
    write_file(PARENT / "Data" / "XML" / "Parent.xml", "PARENT");
    write_file(PARENT / "Data" / "MegaFiles.xml", "<Mega_Files><File>Data\\Parent.meg</File><File>Data\\Config.meg</File></Mega_Files>");
    write_meg(PARENT / "Data" / "Parent.meg", {{"DATA\\XML\\P1.XML", "P1"}, {"DATA\\XML\\P2.XML", "P2"}, {"DATA\\XML\\P3.XML", "P3"}}, false);
    write_file(EAWMOD / "Data" / "XML" / "Old.xml", "OLD");
    write_file(EAWMOD / "modinfo.json", R"({"name": "Old EaW Mod", "steamdata": {"tags": ["EAW"]}})");

    // ---- A two-mod stack: plan, descriptors, layer owners, issues ----
    alamo::test::section("stack Sub > Parent");
    {
        const std::vector<std::string> args = {"MODPATH=Mods\\Sub", "MODPATH=Mods\\Parent", "FALLBACKPATH=", "IGNORED"};
        alamo::IssueSink issues;
        std::string err;
        alamo::vfs::ModEnvironmentOptions opt;
        opt.eaw_root = std::string();
        const auto env = ModEnvironment::open(u8(FOC), args, issues, &err, opt);
        CHECK(env != nullptr);
        if (env) {
            CHECK(!env->refused() && env->refusal().empty());
            CHECK(env->args() == args && env->game() == u8(FOC));
            CHECK(env->plan().mod_search.size() == 2 && env->plan().fallback_paths.empty());
            CHECK(env->mounted().size() == 2 && env->vfs().mod_dirs().size() == 2);
            if (env->mounted().size() == 2) {
                CHECK(env->mounted()[0].name == "Sub Mod" && env->mounted()[0].version == "1.2");
                CHECK(env->mounted()[0].kind == alamo::vfs::ModKind::LocalMods && env->mounted()[1].id == "Parent");
            }
            // FileProvider: the Vfs's answers.
            CHECK(read_str(*env, "Data\\XML\\Shared.xml") == "SUB");
            CHECK(env->exists("Data\\XML\\Base.xml") && !env->exists("Data\\XML\\Nope.xml"));
            CHECK(env->origin("Data\\XML\\Base.xml") == env->vfs().origin("Data\\XML\\Base.xml"));
            // vfs_of: hosts handed a FileProvider (the renderer's effect order) find the Vfs behind an environment too.
            const alamo::FileProvider* fp = env.get();
            CHECK(alamo::vfs::vfs_of(fp) == &env->vfs() && alamo::vfs::vfs_of(&env->vfs()) == &env->vfs());
            CHECK(alamo::vfs::vfs_of(nullptr) == nullptr);
            // Owners: a mod's loose file, a MEG from the mod's own list, the base Config.meg it names, a base file.
            const auto* s = env->owner_of("Data\\XML\\Shared.xml");
            CHECK(s && s->name == "Sub Mod");
            const auto* p = env->owner_of("Data\\XML\\Parent.xml");
            CHECK(p && p->id == "Parent");
            CHECK(env->owner_of("Data\\XML\\Config.xml") == nullptr);
            CHECK(env->owner_of("Data\\XML\\Base.xml") == nullptr);
            CHECK(env->owner_of("Data\\XML\\Nope.xml") == nullptr);
            size_t mod_megs = 0;
            for (const alamo::vfs::Layer& l : env->vfs().layers())
                if (l.kind == alamo::vfs::Layer::Kind::Meg && env->owner(l)) {
                    ++mod_megs;
                    CHECK(env->owner(l)->id == "Parent");
                }
            CHECK(mod_megs == 1);
            // The VFS's findings went to the caller's sink with the owning mod's layer (Parent = search position 1).
            const alamo::CompatIssue* i = nullptr;
            for (const alamo::CompatIssue& c : issues.issues())
                if (c.kind == alamo::IssueKind::MegUnsorted) i = &c;
            CHECK(i && i->layer == 1 && i->retail_equivalent);
        }
    }

    // ---- D1: a base-EaW mod is refused, the environment still describes it ----
    alamo::test::section("base-EaW refusal");
    {
        const std::vector<std::string> args = {"MODPATH=Mods\\OldEaw"};
        alamo::IssueSink issues;
        std::string err;
        alamo::vfs::ModEnvironmentOptions opt;
        opt.eaw_root = std::string();
        opt.hash = false;
        const auto env = ModEnvironment::open(u8(FOC), args, issues, &err, opt);
        CHECK(env != nullptr);
        if (env) {
            CHECK(env->refused());
            CHECK(err == env->refusal() && err.find("Old EaW Mod") == 0 && err.find(alamo::vfs::kBaseEawRefusal) != std::string::npos);
            CHECK(env->mounted().size() == 1 && env->mounted()[0].content_hash == 0);
        }
    }

    // ---- No FoC folder: nullptr and a "VFS: " error ----
    alamo::test::section("bad game folder");
    {
        alamo::IssueSink issues;
        std::string err;
        CHECK(ModEnvironment::open(u8(T / "missing"), {}, issues, &err) == nullptr);
        CHECK(err.rfind("VFS: ", 0) == 0);
    }

    fs::remove_all(T, ec);
    return alamo::test::report("alamo_vfs_env_tests");
}
