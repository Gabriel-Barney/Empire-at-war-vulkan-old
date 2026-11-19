//! The install and the installed mods, as the port's `alamo_mod_check --list --json` reports them (ums3, Stage 10:
//! the Command Center keeps no mod scanner of its own; docs/MODS.md 3.1.2).
//!
//! Facts this relies on (`engine/apps/game/tools/mod_check_main.cpp`, `mod_check.hpp` `format_mod_catalog_json`):
//! * `alamo_mod_check.exe` sits next to `alamo_game.exe` (and in the test build, next to the launcher);
//! * `--list --json -` prints only the JSON on stdout: schema `alamo.mod_catalog`, version 1;
//! * `--game DIR` names the FoC folder; without it the check finds the game the way the port does (ALAMO_FOC_DATA,
//!   then the Steam libraries); `--mod-dir DIR` adds a user folder (one mod, or a folder of mods); `--no-hash` skips
//!   the content hashes;
//! * `install` has `foc`, `eaw`, `mods_dir`, `steam_roots`, `steam_libraries`, `workshop_dirs`, `mod_folders`;
//! * `mods` lists one row per installed folder and `modinfo.json` variant, in the port's order: workshop items by
//!   numeric id, then `Mods\` folders by name, then the extra folders. `kind` is `workshop`, `local` or `path`.

use serde_json::Value;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Workshop,
    Local,
    Path,
}

#[derive(Clone, Debug, PartialEq)]
pub struct Dependency {
    /// 0 a local path, 1 a Workshop id, 2 virtual (the eaw.modinfo spec).
    pub modtype: u64,
    pub identifier: String,
    pub name: String,
}

#[derive(Clone, Debug, PartialEq)]
pub struct CatalogMod {
    /// Unique across kinds: `1125571106`, `2083545253:subA`, `Mods\AOW`, or the folder of an extra mod.
    pub key: String,
    pub arg: String,
    pub kind: Kind,
    pub id: String,
    /// Empty for a folder's main mod, else the `<variant>` of `<variant>-modinfo.json`.
    pub variant: String,
    pub root: PathBuf,
    pub name: String,
    /// `modinfo.json` gave the name (else the folder name stands in).
    pub named: bool,
    pub summary: String,
    pub version: String,
    pub icon: String,
    pub published_id: String,
    pub has_modinfo: bool,
    pub has_data: bool,
    /// "FOC", "EAW" or "".
    pub game_tag: String,
    pub dependencies: Vec<Dependency>,
    pub warnings: Vec<String>,
}

impl CatalogMod {
    /// The Workshop id: the folder's for a Workshop item, else `steamdata.publishedfileid` when given.
    pub fn workshop_id(&self) -> Option<String> {
        if self.kind == Kind::Workshop {
            return Some(self.id.clone());
        }
        (!self.published_id.is_empty()).then(|| self.published_id.clone())
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Install {
    pub foc: Option<PathBuf>,
    pub eaw: Option<PathBuf>,
    pub mods_dir: Option<PathBuf>,
    pub steam_roots: Vec<PathBuf>,
    pub steam_libraries: Vec<PathBuf>,
    /// The libraries' FoC Workshop content folders that exist (`...\workshop\content\32470`).
    pub workshop_dirs: Vec<PathBuf>,
    pub mod_folders: Vec<PathBuf>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Catalog {
    pub install: Install,
    pub mods: Vec<CatalogMod>,
}

fn text(v: &Value, key: &str) -> String {
    v.get(key).and_then(Value::as_str).unwrap_or_default().to_owned()
}

/// The port writes '/' or '\\'; the launcher compares paths with `steam::same_path`, so both are fine. Empty = none.
fn path(v: &Value, key: &str) -> Option<PathBuf> {
    let s = text(v, key);
    (!s.is_empty()).then(|| PathBuf::from(s.replace('/', "\\")))
}

fn paths(v: &Value, key: &str) -> Vec<PathBuf> {
    v.get(key)
        .and_then(Value::as_array)
        .map(|a| a.iter().filter_map(Value::as_str).filter(|s| !s.is_empty()).map(|s| PathBuf::from(s.replace('/', "\\"))).collect())
        .unwrap_or_default()
}

fn strings(v: &Value, key: &str) -> Vec<String> {
    v.get(key).and_then(Value::as_array).map(|a| a.iter().filter_map(|s| s.as_str().map(str::to_owned)).collect()).unwrap_or_default()
}

fn flag(v: &Value, key: &str) -> bool {
    v.get(key).and_then(Value::as_bool).unwrap_or(false)
}

/// Reads the JSON `alamo_mod_check --list --json -` prints.
pub fn parse(json: &str) -> Result<Catalog, String> {
    let v: Value = serde_json::from_str(json).map_err(|e| format!("the mod list is not JSON: {e}"))?;
    if v.get("schema").and_then(Value::as_str) != Some("alamo.mod_catalog") {
        return Err("not an alamo.mod_catalog (an alamo_mod_check from before ums1?)".into());
    }
    let i = v.get("install").ok_or("the mod list has no install section")?;
    let install = Install {
        foc: path(i, "foc"),
        eaw: path(i, "eaw"),
        mods_dir: path(i, "mods_dir"),
        steam_roots: paths(i, "steam_roots"),
        steam_libraries: paths(i, "steam_libraries"),
        workshop_dirs: paths(i, "workshop_dirs"),
        mod_folders: paths(i, "mod_folders"),
    };
    let mut mods = Vec::new();
    for m in v.get("mods").and_then(Value::as_array).into_iter().flatten() {
        let kind = match text(m, "kind").as_str() {
            "workshop" => Kind::Workshop,
            "local" => Kind::Local,
            _ => Kind::Path,
        };
        let dependencies = m
            .get("dependencies")
            .and_then(Value::as_array)
            .into_iter()
            .flatten()
            .map(|d| Dependency {
                modtype: d.get("modtype").and_then(Value::as_u64).unwrap_or(0),
                identifier: text(d, "identifier"),
                name: text(d, "name"),
            })
            .collect();
        mods.push(CatalogMod {
            key: text(m, "key"),
            arg: text(m, "arg"),
            kind,
            id: text(m, "id"),
            variant: text(m, "variant"),
            root: path(m, "root").unwrap_or_default(),
            name: text(m, "name"),
            named: flag(m, "named"),
            summary: text(m, "summary"),
            version: text(m, "version"),
            icon: text(m, "icon"),
            published_id: text(m, "published_id"),
            has_modinfo: flag(m, "has_modinfo"),
            has_data: flag(m, "has_data"),
            game_tag: text(m, "game_tag"),
            dependencies,
            warnings: strings(m, "warnings"),
        });
    }
    Ok(Catalog { install, mods })
}

/// `alamo_mod_check.exe` next to the game, else next to the launcher (the test build ships them together).
pub fn mod_check_exe(game_exe: Option<&Path>) -> Option<PathBuf> {
    let beside_game = game_exe.map(crate::stack::mod_check_exe).filter(|p| p.is_file());
    beside_game.or_else(|| {
        std::env::current_exe().ok().and_then(|e| e.parent().map(|d| d.join("alamo_mod_check.exe"))).filter(|p| p.is_file())
    })
}

/// The check's arguments: the game folder when the user chose one, every extra mod folder, no content hashes.
pub fn list_args(foc: Option<&Path>, extra: &[PathBuf]) -> Vec<String> {
    let mut a: Vec<String> = Vec::new();
    if let Some(f) = foc {
        a.push("--game".into());
        a.push(f.to_string_lossy().into_owned());
    }
    a.push("--list".into());
    a.push("--no-hash".into());
    for d in extra {
        a.push("--mod-dir".into());
        a.push(d.to_string_lossy().trim_end_matches(['\\', '/']).to_owned());
    }
    a.push("--json".into());
    a.push("-".into());
    a
}

/// Runs `exe` with `args` (no console window) and returns its stdout.
pub fn run_check(exe: &Path, args: &[String]) -> Result<String, String> {
    let mut cmd = Command::new(exe);
    cmd.args(args);
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    let out = cmd.output().map_err(|e| format!("cannot run {}: {e}", exe.display()))?;
    Ok(String::from_utf8_lossy(&out.stdout).into_owned())
}

/// The catalog of this install: `exe --list` (see [`list_args`]).
pub fn load(exe: Option<&Path>, foc: Option<&Path>, extra: &[PathBuf]) -> Result<Catalog, String> {
    let exe = exe.ok_or("alamo_mod_check.exe was not found next to the game or the launcher")?;
    parse(&run_check(exe, &list_args(foc, extra))?)
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// `alamo_mod_check --list` on the port's conformance install (engine/apps/game/testdata/conformance/stacks, its
    /// Steam library as ALAMO_STEAM_ROOT), recorded 2026-11-18 with the folder renamed to C:/Fixture.
    pub(crate) const RECORDED: &str = include_str!("testdata/mod_catalog.json");

    #[test]
    fn the_recorded_list_gives_the_install_and_every_row() {
        let c = parse(RECORDED).unwrap();
        assert_eq!(c.install.foc.as_deref(), Some(Path::new("C:\\Fixture\\corruption")));
        assert_eq!(c.install.eaw.as_deref(), Some(Path::new("C:\\Fixture\\GameData")));
        assert_eq!(c.install.steam_libraries, vec![PathBuf::from("C:\\Fixture\\steamlib")]);
        assert_eq!(c.install.workshop_dirs, vec![PathBuf::from("C:\\Fixture\\steamlib\\steamapps\\workshop\\content\\32470")]);
        let keys: Vec<&str> = c.mods.iter().map(|m| m.key.as_str()).collect();
        assert_eq!(
            keys,
            [
                "900000201:subA", "900000201:subB", "Mods\\CycleA", "Mods\\CycleB", "Mods\\EawGuess", "Mods\\EawOnly",
                "Mods\\OtherTc", "Mods\\StackHinted", "Mods\\StackParent", "Mods\\StackSub", "Mods\\StackTop"
            ]
        );
        let a = &c.mods[0];
        assert_eq!((a.kind, a.id.as_str(), a.variant.as_str(), a.name.as_str(), a.named), (Kind::Workshop, "900000201", "subA", "Ws Patch A", true));
        assert_eq!(a.arg, "STEAMMOD=900000201");
        assert_eq!(a.workshop_id().as_deref(), Some("900000201"));
        assert_eq!(a.dependencies, vec![Dependency { modtype: 0, identifier: "Mods\\StackParent".into(), name: String::new() }]);
        let sub = c.mods.iter().find(|m| m.key == "Mods\\StackSub").unwrap();
        assert_eq!((sub.kind, sub.name.as_str(), sub.version.as_str(), sub.has_modinfo), (Kind::Local, "Stack Sub", "1.1", true));
        assert_eq!(sub.root, PathBuf::from("C:\\Fixture\\corruption\\Mods\\StackSub"));
        let eaw = c.mods.iter().find(|m| m.key == "Mods\\EawOnly").unwrap();
        assert_eq!(eaw.game_tag, "EAW");
        let top = c.mods.iter().find(|m| m.key == "Mods\\StackTop").unwrap();
        assert!(!top.named && top.name == "StackTop" && top.workshop_id().is_none());
    }

    #[test]
    fn other_json_is_refused() {
        assert!(parse("{}").is_err());
        assert!(parse("not json").is_err());
        assert!(parse(r#"{"schema": "alamo.mod_report"}"#).is_err());
    }

    #[test]
    fn the_list_gets_the_chosen_game_and_extra_folders() {
        let a = list_args(Some(Path::new("D:\\FoC")), &[PathBuf::from("E:\\My Mods\\")]);
        assert_eq!(a, ["--game", "D:\\FoC", "--list", "--no-hash", "--mod-dir", "E:\\My Mods", "--json", "-"]);
        assert_eq!(list_args(None, &[]), ["--list", "--no-hash", "--json", "-"]);
    }
}
