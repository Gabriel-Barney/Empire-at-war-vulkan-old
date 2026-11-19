// alamo_vfs ModEnvironment. See include/alamo/vfs/mod_environment.hpp.
#include "alamo/vfs/mod_environment.hpp"

#include <utility>

namespace alamo::vfs {

struct ModEnvironment::Impl {
    std::string game;
    std::vector<std::string> args;
    VfsConfig plan;
    std::unique_ptr<Vfs> vfs;
    std::vector<ModDescriptor> mounted;
    std::string refusal;
};

ModEnvironment::ModEnvironment(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ModEnvironment::~ModEnvironment() = default;

std::unique_ptr<ModEnvironment> ModEnvironment::open(std::string_view game, std::span<const std::string> args, alamo::IssueSink& issues,
                                                     std::string* error, ModEnvironmentOptions options) {
    auto impl = std::make_unique<Impl>();
    impl->game = std::string(game);
    impl->args.assign(args.begin(), args.end());
    // The mount plan: mount_game's rule (foc_root, EaW tier, then the switches; a switch with an empty value is skipped).
    impl->plan.foc_root = impl->game;
    impl->plan.eaw_root = std::move(options.eaw_root);
    impl->plan.steam_resolver = std::move(options.steam_resolver);
    parse_command_line(args, impl->plan);
    std::string err;
    impl->vfs = Vfs::create(impl->plan, &err, &issues);
    if (!impl->vfs) {
        if (error) *error = "VFS: " + err;
        return nullptr;
    }
    for (const std::string& dir : impl->vfs->mod_dirs()) impl->mounted.push_back(describe_mounted_mod(dir, impl->game, options.hash));
    for (const ModDescriptor& m : impl->mounted)
        if (m.game_tag == GameTag::Eaw) {  // D1: FoC mods only; the first base-EaW mod names the refusal
            impl->refusal = m.name + " (" + m.key() + "): " + kBaseEawRefusal;
            if (error) *error = impl->refusal;
            break;
        }
    return std::unique_ptr<ModEnvironment>(new ModEnvironment(std::move(impl)));
}

bool ModEnvironment::read(std::string_view path, std::vector<uint8_t>& out) const { return impl_->vfs->read(path, out); }
bool ModEnvironment::exists(std::string_view path) const { return impl_->vfs->exists(path); }
std::string ModEnvironment::origin(std::string_view path) const { return impl_->vfs->origin(path); }

const Vfs& ModEnvironment::vfs() const { return *impl_->vfs; }
Vfs& ModEnvironment::vfs() { return *impl_->vfs; }
const std::string& ModEnvironment::game() const { return impl_->game; }
const std::vector<std::string>& ModEnvironment::args() const { return impl_->args; }
const VfsConfig& ModEnvironment::plan() const { return impl_->plan; }
const std::vector<ModDescriptor>& ModEnvironment::mounted() const { return impl_->mounted; }

const ModDescriptor* ModEnvironment::owner(const Layer& layer) const {
    if (layer.mod < 0 || static_cast<size_t>(layer.mod) >= impl_->mounted.size()) return nullptr;
    return &impl_->mounted[static_cast<size_t>(layer.mod)];
}

const ModDescriptor* ModEnvironment::owner_of(std::string_view path) const {
    const auto r = impl_->vfs->resolve(path);
    if (!r) return nullptr;
    const Layer* l = impl_->vfs->layer_of(*r);
    return l ? owner(*l) : nullptr;
}

const Vfs* vfs_of(const alamo::FileProvider* files) {
    if (const auto* v = dynamic_cast<const Vfs*>(files)) return v;
    if (const auto* e = dynamic_cast<const ModEnvironment*>(files)) return &e->vfs();
    return nullptr;
}

bool ModEnvironment::refused() const { return !impl_->refusal.empty(); }
const std::string& ModEnvironment::refusal() const { return impl_->refusal; }

}  // namespace alamo::vfs
