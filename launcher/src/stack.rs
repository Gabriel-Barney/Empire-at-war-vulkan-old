//! The resolved mod stack of a profile (mcx3 t06, docs/MOD_COMPAT_DESIGN.md §5.2 / §15): runs the port's
//! `alamo_mod_check --resolve` on the profile's mods and reads its JSON report. The check owns the rules
//! (declared `modinfo.json` dependencies, the submod hint's likely parent, cycle and base-EaW errors, the
//! two-total-conversion warning); the launcher only shows the result and can take its order.
//!
//! Facts this relies on (`engine/apps/game/tools/mod_check_main.cpp`, `mod_check.cpp`):
//! * `alamo_mod_check.exe` sits next to `alamo_game.exe`;
//! * `--resolve` takes the mod arguments as the picked mods and checks the resolved stack; `--json -`
//!   prints only the JSON report (`alamo.mod_report` version 4) on stdout;
//! * the report's `stack` object has `args` (launch order), `mods` (`key`, `arg`, `name`, `reason`),
//!   `errors`, `warnings`; `grade` has `grade`, `g3_static`, `blocking`; `base_eaw` is the D1 refusal;
//! * a stack argument is `STEAMMOD=<id>` (a Workshop item) or `MODPATH=<path>` (`Mods\<name>` relative to
//!   the FoC folder, or absolute).

use serde_json::Value;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::mpsc::{channel, Receiver};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct StackMod {
    pub key: String,
    pub arg: String,
    pub name: String,
    /// "picked", "dependency of <key>", "likely parent of <key>".
    pub reason: String,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Resolved {
    /// Launch order: each mod before the mods it depends on.
    pub mods: Vec<StackMod>,
    pub errors: Vec<String>,
    pub warnings: Vec<String>,
    /// "G0", "G1", ...; empty when the check stopped at the stack (a stack error).
    pub grade: String,
    pub g3_static: bool,
    pub blocking: usize,
    /// A mod of the stack is a base-EaW mod: the port refuses it (D1).
    pub base_eaw: bool,
}

impl Resolved {
    /// "G1 · 3 items keep G3 open", for the badge.
    pub fn badge(&self) -> Option<String> {
        if self.grade.is_empty() {
            return None;
        }
        Some(if self.g3_static {
            format!("{} · G3 static part ok", self.grade)
        } else {
            format!("{} · {} item{} keep G3 open", self.grade, self.blocking, if self.blocking == 1 { "" } else { "s" })
        })
    }
}

fn strings(v: &Value) -> Vec<String> {
    v.as_array().map(|a| a.iter().filter_map(|s| s.as_str().map(str::to_owned)).collect()).unwrap_or_default()
}

fn text(v: &Value, key: &str) -> String {
    v.get(key).and_then(Value::as_str).unwrap_or_default().to_owned()
}

/// Reads the JSON report `alamo_mod_check --resolve --json -` prints.
pub fn parse_report(json: &str) -> Result<Resolved, String> {
    let v: Value = serde_json::from_str(json).map_err(|e| format!("the mod check's report is not JSON: {e}"))?;
    if v.get("schema").and_then(Value::as_str) != Some("alamo.mod_report") {
        return Err("not an alamo.mod_report".into());
    }
    let stack = v.get("stack").ok_or("the report has no stack (an alamo_mod_check from before mcx3?)")?;
    let mut r = Resolved {
        errors: strings(&stack["errors"]),
        warnings: strings(&stack["warnings"]),
        base_eaw: v.get("base_eaw").and_then(Value::as_bool).unwrap_or(false),
        ..Default::default()
    };
    if let Some(mods) = stack.get("mods").and_then(Value::as_array) {
        for m in mods {
            r.mods.push(StackMod { key: text(m, "key"), arg: text(m, "arg"), name: text(m, "name"), reason: text(m, "reason") });
        }
    }
    if let Some(g) = v.get("grade") {
        r.grade = text(g, "grade");
        r.g3_static = g.get("g3_static").and_then(Value::as_bool).unwrap_or(false);
        r.blocking = g.get("blocking").and_then(Value::as_array).map(Vec::len).unwrap_or(0);
    }
    if !r.errors.is_empty() {
        r.grade.clear();  // the check stopped at the stack: no grade of this stack
    }
    Ok(r)
}

/// The folder a stack argument mounts: `STEAMMOD=<id>` in the Workshop content folder, `MODPATH=` relative to
/// the FoC folder or absolute. None for anything else (or a Workshop item without a known content folder).
pub fn arg_path(arg: &str, foc: &Path, workshop_dir: Option<&Path>) -> Option<PathBuf> {
    let (key, value) = arg.split_once('=')?;
    if value.is_empty() {
        return None;
    }
    match key.to_ascii_uppercase().as_str() {
        "STEAMMOD" => {
            let id = value.split(':').next()?;
            id.bytes().all(|b| b.is_ascii_digit()).then(|| workshop_dir.map(|w| w.join(id)))?
        }
        "MODPATH" => {
            let p = PathBuf::from(value.replace('/', "\\"));
            Some(if p.is_absolute() { p } else { foc.join(p) })
        }
        _ => None,
    }
}

/// `alamo_mod_check.exe` next to the game.
pub fn mod_check_exe(game_exe: &Path) -> PathBuf {
    game_exe.with_file_name("alamo_mod_check.exe")
}

/// The check's arguments for a profile's mods (in the profile's order: the picked mods).
pub fn check_args(foc: &Path, mods: &[PathBuf]) -> Vec<String> {
    let mut a: Vec<String> = ["--game", &foc.to_string_lossy(), "--resolve", "--no-shaders", "--no-dxc", "--no-fx-prebuild", "--no-assets", "--json", "-"]
        .iter()
        .map(|s| s.to_string())
        .collect();
    a.extend(mods.iter().map(|m| format!("MODPATH={}", m.to_string_lossy().trim_end_matches(['\\', '/']))));
    a
}

/// One resolution at a time on a worker thread.
#[derive(Default)]
pub struct Resolver {
    rx: Option<Receiver<Result<Resolved, String>>>,
    /// The profile mods the last result is for.
    pub for_mods: Vec<PathBuf>,
    pub result: Option<Result<Resolved, String>>,
}

impl Resolver {
    pub fn start(&mut self, exe: PathBuf, foc: PathBuf, mods: Vec<PathBuf>, ctx: egui::Context) {
        let (tx, rx) = channel();
        self.rx = Some(rx);
        self.result = None;
        self.for_mods = mods.clone();
        std::thread::spawn(move || {
            let mut cmd = Command::new(&exe);
            cmd.args(check_args(&foc, &mods));
            #[cfg(windows)]
            {
                use std::os::windows::process::CommandExt;
                cmd.creation_flags(0x0800_0000); // CREATE_NO_WINDOW
            }
            let out = match cmd.output() {
                Ok(o) => {
                    let stdout = String::from_utf8_lossy(&o.stdout);
                    parse_report(&stdout)
                }
                Err(e) => Err(format!("cannot run {}: {e}", exe.display())),
            };
            let _ = tx.send(out);
            ctx.request_repaint();
        });
    }

    /// Takes a finished result in; true while the check still runs.
    pub fn poll(&mut self) -> bool {
        if let Some(rx) = &self.rx {
            match rx.try_recv() {
                Ok(r) => {
                    self.result = Some(r);
                    self.rx = None;
                }
                Err(std::sync::mpsc::TryRecvError::Disconnected) => {
                    self.result = Some(Err("the mod check stopped without a report".into()));
                    self.rx = None;
                }
                Err(std::sync::mpsc::TryRecvError::Empty) => {}
            }
        }
        self.rx.is_some()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const REPORT: &str = r#"{
      "schema": "alamo.mod_report", "version": 4, "result": "ok",
      "base_eaw": false,
      "stack": {
        "resolved": true,
        "args": ["MODPATH=E:\\Art of war", "STEAMMOD=1125571106"],
        "mods": [
          {"key": "Art of war", "arg": "MODPATH=E:\\Art of war", "name": "AOW patch", "reason": "picked"},
          {"key": "1125571106", "arg": "STEAMMOD=1125571106", "name": "Thrawn's Revenge", "reason": "likely parent of Art of war"}
        ],
        "errors": [], "warnings": ["two total conversions in one stack: a and b"], "fingerprint": "00ff"
      },
      "grade": {"grade": "G1", "g3_static": false, "blocking": ["x", "y"], "check_errors": []}
    }"#;

    #[test]
    fn the_report_gives_the_stack_and_the_grade() {
        let r = parse_report(REPORT).unwrap();
        assert_eq!(r.mods.len(), 2);
        assert_eq!(r.mods[1].reason, "likely parent of Art of war");
        assert_eq!(r.warnings.len(), 1);
        assert_eq!(r.badge().as_deref(), Some("G1 · 2 items keep G3 open"));
        assert!(parse_report("{}").is_err());
        assert!(parse_report("not json").is_err());
    }

    #[test]
    fn a_stack_error_has_no_grade() {
        let json = REPORT.replace(r#""errors": []"#, r#""errors": ["dependency cycle: A -> B -> A"]"#);
        let r = parse_report(&json).unwrap();
        assert_eq!(r.errors.len(), 1);
        assert_eq!(r.badge(), None);
    }

    #[test]
    fn stack_arguments_map_to_folders() {
        let foc = Path::new("C:\\Games\\corruption");
        let ws = Path::new("C:\\Steam\\steamapps\\workshop\\content\\32470");
        assert_eq!(arg_path("STEAMMOD=1125571106", foc, Some(ws)), Some(ws.join("1125571106")));
        assert_eq!(arg_path("STEAMMOD=400:subA", foc, Some(ws)), Some(ws.join("400")));
        assert_eq!(arg_path("STEAMMOD=1", foc, None), None);
        assert_eq!(arg_path("MODPATH=Mods\\AOW", foc, None), Some(foc.join("Mods\\AOW")));
        assert_eq!(arg_path("modpath=D:/Mods/X", foc, None), Some(PathBuf::from("D:\\Mods\\X")));
        assert_eq!(arg_path("FALLBACKPATH=x", foc, None), None);
        assert_eq!(arg_path("STEAMMOD=", foc, Some(ws)), None);
    }

    #[test]
    fn the_check_gets_the_profile_mods_in_order() {
        let a = check_args(Path::new("C:\\FoC"), &[PathBuf::from("C:\\m\\Sub\\"), PathBuf::from("C:\\m\\Parent")]);
        assert_eq!(&a[a.len() - 2..], ["MODPATH=C:\\m\\Sub", "MODPATH=C:\\m\\Parent"]);
        assert!(a.contains(&"--resolve".to_string()) && a.contains(&"--json".to_string()));
    }
}
