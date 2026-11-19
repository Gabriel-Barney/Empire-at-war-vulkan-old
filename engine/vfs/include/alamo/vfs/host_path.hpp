// Host path keys and UTF-8 host paths for the mod system (ums4 t04 / t06, Stage 10): one comparison rule for every place
// that asks "is this the same folder" (the catalog's find_root, the stack's launch list, the Steam library list, the
// submod hint's candidates) and one UTF-8 conversion for the launchers. Before ums4 the catalog, the install search, the
// Steam lookup and alamo_play each kept their own copy.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace alamo::vfs {

// A comparison key for a path or catalog key as written: ASCII lower case, '/' == '\\', no trailing separator.
// "Mods/AOW/" and "mods\\aow" give the same key. No file system access.
std::string path_key(std::string_view path);

// A folder's identity: the path made absolute (against the current folder) and lexically normal ("." / ".." folded),
// then, when the folder exists, the final path the file system reaches it by (Windows: GetFinalPathNameByHandleW, so a
// junction, a symbolic link and a subst drive lead to the folder they point at), then path_key. A mod reached two ways
// (through a junction and directly) is one folder. A folder that does not exist keeps its lexical form.
std::string folder_key(std::string_view path);
// folder_key(a) == folder_key(b).
bool same_folder(std::string_view a, std::string_view b);

// UTF-8 text of a host path in its native form (Windows: '\\' as written, no generic '/' rewrite; core's path_to_utf8
// gives the generic form). For paths shown to the user or passed on a command line.
std::string native_utf8(const std::filesystem::path& p);

}  // namespace alamo::vfs
