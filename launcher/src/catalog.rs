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
use std::io::Read;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

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
    /// Every run of digits in the folder's main `modinfo.json` text, sorted, each once (variants share the folder's).
    /// Lists written before the field existed have none: empty.
    pub modinfo_numbers: Vec<String>,
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

pub(crate) fn text(v: &Value, key: &str) -> String {
    v.get(key).and_then(Value::as_str).unwrap_or_default().to_owned()
}

/// The port writes '/' or '\\'; the launcher compares paths with `steam::same_path`, so both are fine. Empty = none.
pub(crate) fn path(v: &Value, key: &str) -> Option<PathBuf> {
    let s = text(v, key);
    (!s.is_empty()).then(|| PathBuf::from(s.replace('/', "\\")))
}

fn paths(v: &Value, key: &str) -> Vec<PathBuf> {
    v.get(key)
        .and_then(Value::as_array)
        .map(|a| a.iter().filter_map(Value::as_str).filter(|s| !s.is_empty()).map(|s| PathBuf::from(s.replace('/', "\\"))).collect())
        .unwrap_or_default()
}

pub(crate) fn strings(v: &Value, key: &str) -> Vec<String> {
    v.get(key).and_then(Value::as_array).map(|a| a.iter().filter_map(|s| s.as_str().map(str::to_owned)).collect()).unwrap_or_default()
}

pub(crate) fn flag(v: &Value, key: &str) -> bool {
    v.get(key).and_then(Value::as_bool).unwrap_or(false)
}

pub(crate) fn count(v: &Value, key: &str) -> usize {
    v.get(key).and_then(Value::as_u64).unwrap_or(0) as usize
}

pub(crate) fn list(v: &Value, key: &str) -> Vec<Value> {
    v.get(key).and_then(Value::as_array).cloned().unwrap_or_default()
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
            modinfo_numbers: strings(m, "modinfo_numbers"),
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

/// A folder as a check argument: no trailing separator, except a drive root ("E:\\" stays, "E:" would mean the drive's
/// current folder).
pub fn folder_arg(p: &Path) -> String {
    let s = p.to_string_lossy();
    let t = s.trim_end_matches(['\\', '/']);
    if t.len() == 2 && t.ends_with(':') {
        format!("{t}\\")
    } else {
        t.to_owned()
    }
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
        a.push(folder_arg(d));
    }
    a.push("--json".into());
    a.push("-".into());
    a
}

/// How long the `--list` check may take before the launcher gives up on it (it reads `modinfo.json` files only).
pub const LIST_TIMEOUT: Duration = Duration::from_secs(60);

/// Runs `exe` with `args` (no console window) and returns its stdout. A check that runs past `timeout` is stopped;
/// one that prints nothing says why from the last line of its stderr and its exit code.
pub fn run_check(exe: &Path, args: &[String], timeout: Duration) -> Result<String, String> {
    let mut cmd = Command::new(exe);
    cmd.args(args).stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
    }
    let mut child = cmd.spawn().map_err(|e| format!("cannot run {}: {e}", exe.display()))?;
    // Drain both pipes on threads so a full pipe never stalls the check while it is waited on.
    let drain = |pipe: Option<Box<dyn Read + Send>>| {
        std::thread::spawn(move || {
            let mut buf = Vec::new();
            if let Some(mut p) = pipe {
                let _ = p.read_to_end(&mut buf);
            }
            buf
        })
    };
    let out = drain(child.stdout.take().map(|p| Box::new(p) as Box<dyn Read + Send>));
    let err = drain(child.stderr.take().map(|p| Box::new(p) as Box<dyn Read + Send>));
    let start = Instant::now();
    let status = loop {
        match child.try_wait() {
            Ok(Some(s)) => break s,
            Ok(None) if start.elapsed() < timeout => std::thread::sleep(Duration::from_millis(20)),
            Ok(None) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(format!("{} did not finish within {} s", exe.display(), timeout.as_secs()));
            }
            Err(e) => return Err(format!("{}: {e}", exe.display())),
        }
    };
    let stdout = String::from_utf8_lossy(&out.join().unwrap_or_default()).into_owned();
    if stdout.trim().is_empty() {
        let stderr = String::from_utf8_lossy(&err.join().unwrap_or_default()).into_owned();
        let last = stderr.lines().rev().find(|l| !l.trim().is_empty()).unwrap_or("no output").trim().to_owned();
        return Err(format!("{} gave no report (exit code {}): {last}", exe.display(), status.code().unwrap_or(-1)));
    }
    Ok(stdout)
}

/// The catalog of this install: `exe --list` (see [`list_args`]).
pub fn load(exe: Option<&Path>, foc: Option<&Path>, extra: &[PathBuf]) -> Result<Catalog, String> {
    let exe = exe.ok_or("alamo_mod_check.exe was not found next to the game or the launcher, so no mods are listed")?;
    parse(&run_check(exe, &list_args(foc, extra), LIST_TIMEOUT)?)
}

/// The catalog for the launcher's settings: the check next to the chosen game (or the launcher), the game folder the
/// user chose (if it exists) and the user's extra mod folders. Also returns the check it found, which the conflicts
/// page and the profile stack run too.
pub fn load_for(settings: &crate::config::Settings) -> (Option<PathBuf>, Result<Catalog, String>) {
    let foc = settings.foc_dir_override.clone().filter(|p| p.is_dir());
    let exe = mod_check_exe(settings.game_exe.as_deref());
    let catalog = load(exe.as_deref(), foc.as_deref(), &settings.extra_mod_dirs);
    (exe, catalog)
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
        // Recorded before `modinfo_numbers` existed: every row parses with none.
        assert!(c.mods.iter().all(|m| m.modinfo_numbers.is_empty()));
    }

    #[test]
    fn modinfo_numbers_are_read_when_given() {
        let one = |row: &str| {
            let json = format!(r#"{{"schema":"alamo.mod_catalog","version":1,"install":{{}},"mods":[{row}]}}"#);
            parse(&json).unwrap().mods.remove(0)
        };
        let m = one(r#"{"key":"Mods\\A","kind":"local","modinfo_numbers":["1125571106","2"]}"#);
        assert_eq!(m.modinfo_numbers, ["1125571106", "2"]);
        assert!(one(r#"{"key":"Mods\\B","kind":"local"}"#).modinfo_numbers.is_empty());
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
        assert_eq!(folder_arg(Path::new("E:\\")), "E:\\");
        assert_eq!(folder_arg(Path::new("E:\\Mods\\")), "E:\\Mods");
    }
}
